#include "aruco_detector.hpp"
#include "command.hpp"
#include "encoder.hpp"
#include "logger.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

int main() {
    const double target_z_mm = 300.0;
    const double angle_threshold_deg = 15.0;
    const int current_posture = 15;
    const int max_steps = 100;

    std::string experiment_name;
    std::cout << "Experiment name: ";
    std::cin >> experiment_name;

    const std::string run_name = experiment_name + "_" + now_string();
    const std::string out_dir = "data/" + run_name;
    const std::string img_dir = out_dir + "/images";
    const std::string csv_path = out_dir + "/log.csv";

    try {
        std::filesystem::create_directories(img_dir);

        ArucoDetector detector;
        CommandInterface command;
        EncoderReader encoder;
        Logger logger(csv_path);

        std::cout << "auto aruco go start\n";
        std::cout << "stop condition: abs(angle_deg) <= " << angle_threshold_deg
                  << " && z_mm <= " << target_z_mm << "\n";
        std::cout << "current posture = " << current_posture << "\n";

        for (int index = 0; index < max_steps; ++index) {
            const std::string timestamp = now_string();
            const std::string image_path = make_image_filename(img_dir, index, 0);

            ArucoPose pose;
            const bool detected = detector.detect(pose, image_path);

            if (!detected) {
                std::cout << "[" << index << "] marker not detected -> stop command\n";

                const PeristalsisCommand stop_cmd = make_stop_command(current_posture);
                const EncoderValues raw = encoder.read_raw();
                const EncoderValues diff = encoder.read_diff();
                logger.log(index, timestamp, image_path, stop_cmd, raw, diff);

                command.send(stop_cmd);
                command.wait_done(3000);
                usleep(300000);
                continue;
            }

            std::cout << "[" << index << "] "
                      << "ID=" << pose.id
                      << " X=" << pose.x_mm << " mm"
                      << " Y=" << pose.y_mm << " mm"
                      << " Z=" << pose.z_mm << " mm"
                      << " angle=" << pose.angle_deg << " deg";

            const bool stop_condition =
                (std::abs(pose.angle_deg) <= angle_threshold_deg) &&
                (pose.z_mm <= target_z_mm);

            PeristalsisCommand cmd;
            if (stop_condition) {
                cmd = make_stop_command(current_posture);
                std::cout << " -> STOP\n";
            } else {
                cmd = make_go_command(current_posture);
                std::cout << " -> GO\n";
            }

            const EncoderValues raw = encoder.read_raw();
            const EncoderValues diff = encoder.read_diff();
            logger.log(index, timestamp, image_path, cmd, raw, diff);

            command.send(cmd);

            if (stop_condition) {
                command.wait_done(3000);
                std::cout << "finished: target reached\n";
                break;
            }

            const bool done = command.wait_done(10000);
            if (!done) {
                std::cout << "timeout waiting done\n";
            }

            usleep(100000); // 次の撮影前に少し待つ
        }

    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
