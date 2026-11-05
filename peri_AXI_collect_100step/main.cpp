#include "aruco_detector.hpp"
#include "camera.hpp"
#include "command.hpp"
#include "dead_band_setting.hpp"
#include "emergency_monitor.hpp"
#include "encoder.hpp"
#include "logger.hpp"
#include "safety_logger.hpp"
#include "wire_adjuster.hpp"

#include <chrono>
#include <cctype>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t stop_signal = 0;

void handle_signal(int signal_number) {
    stop_signal = signal_number;
}

bool signal_stop_requested() {
    return stop_signal != 0;
}

std::string signal_stop_reason() {
    if (stop_signal == SIGINT) {
        return "ctrl_c";
    }
    if (stop_signal == SIGTERM) {
        return "sigterm";
    }
    return "signal";
}

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
                             int current_posture,
                             PeristalsisCommand& cmd,
                             int& posture_after_done) {
    posture_after_done = current_posture;

    if (input == "g") {
        cmd = make_go_command(current_posture);
        return true;
    }

    try {
        std::size_t parsed = 0;
        const int posture = std::stoi(input, &parsed);
        if (parsed == input.size() && 0 <= posture && posture <= 100) {
            posture_after_done = posture;
            cmd = make_posture_command(posture, posture);
            return true;
        }
    } catch (const std::exception&) {
    }

    return false;
}

PeristalsisCommand make_exit_reset_command() {
    PeristalsisCommand cmd;
    cmd.id = 999;
    cmd.name = "exit_reset_posture_50";
    cmd.stick1 = 50;
    cmd.stick3 = 50;
    cmd.stroke_right = 0;
    cmd.stroke_left = 0;
    cmd.stroke_up = 0;
    cmd.stroke_down = 0;
    return cmd;
}

bool reset_posture_to_50(CommandInterface& command) {
    std::cout << "Reset posture to 50 (neutral)...\n";
    command.send(make_exit_reset_command());

    if (command.wait_done(10000)) {
        std::cout << "posture reset done\n";
        return true;
    }

    std::cerr << "timeout waiting posture reset\n";
    return false;
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

enum class WaitResult {
    done,
    stop_requested,
    timeout,
};

WaitResult wait_done_interruptible(CommandInterface& command,
                                   const EmergencyKeyMonitor& emergency_monitor,
                                   int timeout_ms) {
    for (int elapsed_ms = 0; elapsed_ms < timeout_ms; ++elapsed_ms) {
        if (signal_stop_requested() || emergency_monitor.requested()) {
            return WaitResult::stop_requested;
        }

        if (command.is_done()) {
            return WaitResult::done;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return WaitResult::timeout;
}

std::string current_stop_reason(const EmergencyKeyMonitor& emergency_monitor) {
    if (signal_stop_requested()) {
        return signal_stop_reason();
    }
    if (emergency_monitor.requested()) {
        return "emergency_key_e";
    }
    return "emergency_stop";
}

void capture_emergency_snapshot(EmergencyStopRecord& record,
                                CameraCapture& camera,
                                EncoderReader& encoder,
                                const ArucoDetector& aruco,
                                const std::string& img_dir,
                                int index) {
    record.raw = encoder.read_raw();
    record.raw12 = encoder.read_raw12();
    record.diff = encoder.read_diff();

    cv::Mat frame;
    if (!camera.capture(frame)) {
        std::cerr << "emergency image capture failed\n";
        return;
    }

    record.aruco = aruco.detect(frame);
    record.image_path = make_image_filename(img_dir, index, 998);

    if (!camera.save_image(record.image_path, frame)) {
        std::cerr << "emergency image save failed\n";
        record.image_path.clear();
    }
}

}  // namespace

int main() {
    std::unique_ptr<CommandInterface> command;
    EmergencyKeyMonitor emergency_monitor;
    bool reset_attempted = false;

    try {
        constexpr uint32_t DEAD_BAND_MANUAL = 5;
        constexpr uint32_t DEAD_BAND_AUTO = 20;

        DeadBandSetting dead_band;
        WireAdjuster wire;
        dead_band.set(DEAD_BAND_MANUAL, DEAD_BAND_AUTO);
        wire.set_target_raw(725, 3453, 3300, 267);
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
    std::optional<double> start_battery_voltage_v;

    if (mode == "auto") {
        std::cout << "Command txt path: ";
        std::cin >> auto_command_path;

        double voltage = 0.0;
        while (true) {
            std::cout << "Battery voltage at start [V]: ";

            if (std::cin >> voltage && voltage > 0.0) {
                start_battery_voltage_v = voltage;
                break;
            }

            std::cout << "Enter a positive number (example: 12.3).\n";
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }

    const std::string run_name = experiment_name + "_" + mode + "_" + now_string();
    const std::string out_dir = "data/" + run_name;
    const std::string img_dir = out_dir + "/images";
    const std::string csv_path = out_dir + "/log.csv";
    const std::string safety_csv_path = out_dir + "/emergency_stop.csv";
    const std::string command_log_path = out_dir + "/commands.txt";

    try {
        if (mode == "auto") {
            auto_commands = load_command_file(auto_command_path);
            if (auto_commands.empty()) {
                throw std::runtime_error("command file is empty: " + auto_command_path);
            }
        }

        std::filesystem::create_directories(img_dir);

        command = std::make_unique<CommandInterface>();
        EncoderReader encoder;
        CameraCapture camera;
        ArucoDetector aruco;
        Logger logger(csv_path, start_battery_voltage_v);
        SafetyLogger safety_logger(safety_csv_path);
        std::ofstream command_log(command_log_path);

        if (!command_log) {
            throw std::runtime_error("failed to open command log: " + command_log_path);
        }

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        if (mode == "auto") {
            std::string start_confirm;
            std::cout << "Confirm neutral posture 50 and a clear emergency-stop path.\n"
                      << "Enter y immediately before fixed-command replay: ";
            std::cin >> start_confirm;
            if (!(start_confirm == "y" || start_confirm == "Y")) {
                throw std::runtime_error("auto replay cancelled by user");
            }
        }

        bool emergency_key_enabled = false;
        if (mode == "auto") {
            emergency_key_enabled = emergency_monitor.start();
            if (!emergency_key_enabled) {
                std::cerr << "warning: Enter-free e key is unavailable; use Ctrl+C for safe stop\n";
            }
        }

        std::cout << "\nOutput directory: " << out_dir << "\n";
        std::cout << "Goal condition: Z <= " << aruco.target_z_mm()
                  << " mm and |angle| <= " << aruco.angle_threshold_deg() << " deg\n";
        std::cout << "Commands: 0-100 posture (50 neutral), g go, b set encoder base, x exit\n";
        if (emergency_key_enabled) {
            std::cout << "SAFETY: press e (no Enter) or Ctrl+C to stop replay safely.\n";
        } else {
            std::cout << "SAFETY: press Ctrl+C to stop safely.\n";
        }
        std::cout << '\n';

        int current_posture = 50;
        int index = 0;
        std::size_t auto_pos = 0;
        std::size_t manual_command_count = 0;
        std::size_t last_completed_command_no = 0;
        std::optional<EmergencyStopRecord> emergency_record;

        while (true) {
            if (signal_stop_requested() || emergency_monitor.requested()) {
                EmergencyStopRecord record;
                record.timestamp = now_string();
                record.reason = current_stop_reason(emergency_monitor);
                record.command_no = last_completed_command_no;
                record.last_completed_command_no = last_completed_command_no;
                record.active_command = "between_commands";
                record.current_posture_before_reset = current_posture;
                record.active_command_done_before_reset = true;
                capture_emergency_snapshot(record, camera, encoder, aruco, img_dir, index);
                emergency_record = record;
                break;
            }

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
                break;
            }

            std::string input;
            std::size_t command_no = 0;

            if (mode == "manual") {
                std::cout << "> ";
                if (!(std::cin >> input)) {
                    if (signal_stop_requested()) {
                        EmergencyStopRecord record;
                        record.timestamp = now_string();
                        record.reason = signal_stop_reason();
                        record.command_no = last_completed_command_no;
                        record.last_completed_command_no = last_completed_command_no;
                        record.active_command = "manual_input";
                        record.current_posture_before_reset = current_posture;
                        record.active_command_done_before_reset = true;
                        capture_emergency_snapshot(
                            record, camera, encoder, aruco, img_dir, index);
                        emergency_record = record;
                        break;
                    }
                    throw std::runtime_error("failed to read manual command");
                }
                command_no = ++manual_command_count;
            } else {
                if (auto_pos >= auto_commands.size()) {
                    std::cout << "End of command file. Stop data collection.\n";
                    break;
                }

                command_no = auto_pos + 1;
                input = auto_commands[auto_pos++];
                std::cout << "> [command " << command_no << "/"
                          << auto_commands.size() << "] " << input << "\n";
            }

            if (input == "e" || input == "E") {
                EmergencyStopRecord record;
                record.timestamp = now_string();
                record.reason = "emergency_key_e";
                record.command_no = last_completed_command_no;
                record.last_completed_command_no = last_completed_command_no;
                record.active_command = "between_commands";
                record.current_posture_before_reset = current_posture;
                record.active_command_done_before_reset = true;
                capture_emergency_snapshot(record, camera, encoder, aruco, img_dir, index);
                emergency_record = record;
                break;
            }

            if (input == "x") {
                std::cout << "exit\n";
                break;
            }

            if (input == "b") {
                encoder.set_base();
                command_log << input << '\n';
                command_log.flush();
                last_completed_command_no = command_no;
                std::cout << "encoder base updated\n";
                continue;
            }

            PeristalsisCommand cmd;
            int posture_after_done = current_posture;
            if (!make_command_from_input(
                    input, current_posture, cmd, posture_after_done)) {
                std::cout << "unknown command\n";
                continue;
            }

            const std::string image_path = make_image_filename(img_dir, index, cmd.id);
            const bool image_ok = camera.save_image(image_path, frame);

            if (!image_ok) {
                std::cerr << "image save failed, command is not sent\n";
                continue;
            }

            // A stop request may arrive during image capture or saving. Check once
            // more immediately before logging/sending so a new motion is not started.
            if (signal_stop_requested() || emergency_monitor.requested()) {
                EmergencyStopRecord record;
                record.timestamp = now_string();
                record.reason = current_stop_reason(emergency_monitor) + "_before_send";
                record.command_no = last_completed_command_no;
                record.last_completed_command_no = last_completed_command_no;
                record.active_command = "between_commands";
                record.current_posture_before_reset = current_posture;
                record.active_command_done_before_reset = true;
                capture_emergency_snapshot(record, camera, encoder, aruco, img_dir, index);
                emergency_record = record;
                break;
            }

            logger.log_action(
                index, timestamp, mode, image_path, cmd, raw, raw12, diff, aruco_result);
            command_log << input << '\n';
            command_log.flush();

            std::cout << "saved: " << image_path << "\n";
            std::cout << "send command " << command_no << ": " << cmd.name
                      << " posture=" << static_cast<int>(cmd.stick3) << "\n";

            command->send(cmd);
            const WaitResult wait_result =
                wait_done_interruptible(*command, emergency_monitor, 10000);

            if (wait_result == WaitResult::done) {
                current_posture = posture_after_done;
                last_completed_command_no = command_no;
                std::cout << "done\n";
                ++index;
                continue;
            }

            EmergencyStopRecord record;
            record.timestamp = now_string();
            record.command_no = command_no;
            record.last_completed_command_no = last_completed_command_no;
            record.active_command = input;
            record.current_posture_before_reset = current_posture;

            if (wait_result == WaitResult::stop_requested) {
                record.reason = current_stop_reason(emergency_monitor);
                std::cout << "\nEMERGENCY STOP requested during command "
                          << command_no << " (" << input << ").\n"
                          << "No additional command will be sent.\n";

                capture_emergency_snapshot(record, camera, encoder, aruco, img_dir, index);

                std::cout << "Waiting for the active PL command to finish before posture reset...\n";
                record.active_command_done_before_reset = command->wait_done(10000);
                if (record.active_command_done_before_reset) {
                    current_posture = posture_after_done;
                    record.current_posture_before_reset = current_posture;
                }
                if (!record.active_command_done_before_reset) {
                    std::cerr << "active command did not finish before reset attempt\n";
                }
            } else {
                record.reason = "command_timeout";
                std::cerr << "Command " << command_no
                          << " completion timeout. Stop replay.\n";
                capture_emergency_snapshot(record, camera, encoder, aruco, img_dir, index);
                record.active_command_done_before_reset = false;
            }

            emergency_record = record;
            break;
        }

        emergency_monitor.stop();

        reset_attempted = true;
        bool reset_done = false;
        try {
            reset_done = reset_posture_to_50(*command);
        } catch (const std::exception& reset_error) {
            std::cerr << "posture reset error: " << reset_error.what() << '\n';
        }

        if (emergency_record) {
            emergency_record->reset_done = reset_done;
            safety_logger.log_emergency_stop(*emergency_record);

            std::cout << "Emergency record: command_no="
                      << emergency_record->command_no
                      << " active_command=" << emergency_record->active_command
                      << " log=" << safety_csv_path << '\n';
        }

        std::cout << "Data collection finished. Output: " << out_dir << '\n';
        return reset_done ? 0 : 2;
    } catch (const std::exception& e) {
        emergency_monitor.stop();
        std::cerr << "error: " << e.what() << std::endl;

        if (command && !reset_attempted) {
            try {
                reset_attempted = true;
                reset_posture_to_50(*command);
            } catch (const std::exception& reset_error) {
                std::cerr << "posture reset error: " << reset_error.what() << '\n';
            }
        }
        return 1;
    }
}
