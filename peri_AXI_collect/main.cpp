#include "aruco_detector.hpp"
#include "camera.hpp"
#include "command.hpp"
#include "dead_band_setting.hpp"
#include "encoder.hpp"
#include "logger.hpp"
#include "wire_adjuster.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& s) {
    std::size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) {
        ++first;
    }

    std::size_t last = s.size();
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) {
        --last;
    }
    return s.substr(first, last - first);
}

std::vector<std::string> load_command_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        throw std::runtime_error("failed to open command file: " + path);
    }

    std::vector<std::string> commands;
    std::string line;

    while (std::getline(ifs, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        commands.push_back(line);
    }

    return commands;
}

bool make_command_from_input(const std::string& input,
                             int& current_posture,
                             PeristalsisCommand& cmd) {
    if (input == "g") {
        cmd = make_go_command(current_posture);
        return true;
    }

    try {
        const int id = std::stoi(input);
        if (1 <= id && id <= 9) {
            current_posture = 10 + id;
            cmd = make_posture_command(id, current_posture);
            return true;
        }
    } catch (const std::exception&) {
    }

    return false;
}

PeristalsisCommand make_exit_reset_command() {
    PeristalsisCommand cmd;
    cmd.id = 999;
    cmd.name = "exit_reset_posture_15";

    cmd.stick1 = 15;
    cmd.stick3 = 15;

    cmd.stroke_right = 0;
    cmd.stroke_left  = 0;
    cmd.stroke_up    = 0;
    cmd.stroke_down  = 0;

    return cmd;
}

void reset_posture_to_15(CommandInterface& command) {
    std::cout << "Reset posture to 15...\n";

    const PeristalsisCommand reset_cmd = make_exit_reset_command();
    command.send(reset_cmd);

    const bool done = command.wait_done(10000);
    if (done) {
        std::cout << "posture reset done\n";
    } else {
        std::cout << "timeout waiting posture reset\n";
    }
}

void print_aruco_status(const ArucoResult& r) {
    if (!r.detected) {
        std::cout << "ArUco: not detected\n";
        return;
    }

    std::cout << "ArUco: ID=" << r.id
              << " X=" << r.x_mm << " mm"
              << " Y=" << r.y_mm << " mm"
              << " Z=" << r.z_mm << " mm"
              << " angle=" << r.angle_deg << " deg\n";
}

}  // namespace

int main() {
    try {
        constexpr uint32_t DEAD_BAND_MANUAL = 5;
        constexpr uint32_t DEAD_BAND_AUTO = 20;

        DeadBandSetting dead_band;
        WireAdjuster wire;

        // Set dead bands before writing the wire-adjustment targets.
        dead_band.set(
            DEAD_BAND_MANUAL,
            DEAD_BAND_AUTO
        );

        wire.set_target_raw(
            725,
            3453,
            2700,
            267
            // r_pre=2100
            // l_pre=2243
        );

        wire.wait_user_confirm();
    } catch (const std::exception& e) {
        std::cerr << "wire adjustment error: " << e.what() << std::endl;
        return 1;
    }

    std::string experiment_name;
    std::string mode;

    std::cout << "Experiment name: ";
    std::cin >> experiment_name;

    std::cout << "Mode (manual/auto): ";
    std::cin >> mode;

    if (!(mode == "manual" || mode == "auto")) {
        std::cerr << "mode must be manual or auto\n";
        return 1;
    }

    std::string auto_command_path;
    std::vector<std::string> auto_commands;

    if (mode == "auto") {
        std::cout << "Command txt path: ";
        std::cin >> auto_command_path;
    }

    const std::string run_name = experiment_name + "_" + mode + "_" + now_string();
    const std::string out_dir = "data/" + run_name;
    const std::string img_dir = out_dir + "/images";
    const std::string csv_path = out_dir + "/log.csv";
    const std::string command_log_path = out_dir + "/commands.txt";

    try {
        if (mode == "auto") {
            auto_commands = load_command_file(auto_command_path);
            if (auto_commands.empty()) {
                throw std::runtime_error("command file is empty: " + auto_command_path);
            }
        }

        std::filesystem::create_directories(img_dir);

        CommandInterface command;
        EncoderReader encoder;
        CameraCapture camera;
        ArucoDetector aruco;
        Logger logger(csv_path);
        std::ofstream command_log(command_log_path);

        if (!command_log) {
            throw std::runtime_error("failed to open command log: " + command_log_path);
        }

        std::cout << "\nOutput directory: " << out_dir << "\n";
        std::cout << "Goal condition: Z <= " << aruco.target_z_mm()
                  << " mm and |angle| <= " << aruco.angle_threshold_deg() << " deg\n";
        std::cout << "Commands: 1-9 posture, g go, b set encoder base, x exit\n\n";

        int current_posture = 15;
        int index = 0;
        std::size_t auto_pos = 0;

        while (true) {
            cv::Mat frame;
            if (!camera.capture(frame)) {
                std::cerr << "image capture failed\n";
                continue;
            }

            const ArucoResult aruco_result = aruco.detect(frame);
            print_aruco_status(aruco_result);

            const EncoderValues raw = encoder.read_raw();
            const EncoderValues raw12 = encoder.read_raw12();
            const EncoderValues diff = encoder.read_diff();
            const std::string timestamp = now_string();

            if (aruco.is_goal(aruco_result)) {
                const std::string image_path = make_image_filename(img_dir, index, 900);
                if (camera.save_image(image_path, frame)) {
                    logger.log_goal(
                        index, timestamp, mode, image_path, raw, raw12, diff, aruco_result);
                }
                std::cout << "Goal reached by ArUco threshold. Stop data collection.\n";

                reset_posture_to_15(command);
                break;
            }

            std::string input;

            if (mode == "manual") {
                std::cout << "> ";
                std::cin >> input;
            } else {
                if (auto_pos >= auto_commands.size()) {
                    std::cout << "End of command file. Stop data collection.\n";

                    reset_posture_to_15(command);
                    break;
                }
                input = auto_commands[auto_pos++];
                std::cout << "> " << input << "\n";
            }

            if (input == "x") {
                std::cout << "exit\n";

                reset_posture_to_15(command);
                break;
            }

            if (input == "b") {
                encoder.set_base();
                command_log << input << '\n';
                command_log.flush();
                std::cout << "encoder base updated\n";
                continue;
            }

            PeristalsisCommand cmd;
            if (!make_command_from_input(input, current_posture, cmd)) {
                std::cout << "unknown command\n";
                continue;
            }

            const std::string image_path = make_image_filename(img_dir, index, cmd.id);
            const bool image_ok = camera.save_image(image_path, frame);

            if (!image_ok) {
                std::cerr << "image save failed, command is not sent\n";
                continue;
            }

            logger.log_action(
                index, timestamp, mode, image_path, cmd, raw, raw12, diff, aruco_result);
            command_log << input << '\n';
            command_log.flush();

            std::cout << "saved: " << image_path << "\n";
            std::cout << "send command: " << cmd.name
                      << " posture=" << static_cast<int>(cmd.stick3) << "\n";

            command.send(cmd);

            const bool done = command.wait_done(10000);
            if (done) {
                std::cout << "done\n";
            } else {
                std::cout << "timeout waiting done\n";
            }

            ++index;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
