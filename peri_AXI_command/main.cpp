#include "camera.hpp"
#include "command.hpp"
#include "encoder.hpp"
#include "logger.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    std::string experiment_name;

    std::cout << "Experiment name: ";
    std::cin >> experiment_name;

    const std::string run_name =
        experiment_name + "_" + now_string();

    const std::string out_dir = "data/" + run_name;
    const std::string img_dir = out_dir + "/images";
    const std::string csv_path = out_dir + "/log.csv";

    try {
        std::filesystem::create_directories(img_dir);

        CommandInterface command;
        EncoderReader encoder;
        CameraCapture camera;
        Logger logger(csv_path);

        std::cout << "1-9 : set posture and hold\n";
        std::cout << "g   : go / crawl with current posture\n";
        std::cout << "s   : stop with current posture\n";
        std::cout << "b   : set encoder base\n";
        std::cout << "x   : exit\n";

        int current_posture = 15;
        int index = 0;

        while (true) {
            std::cout << "> ";

            std::string input;
            std::cin >> input;

            if (input == "x") {
                break;
            }

            if (input == "b") {
                encoder.set_base();
                std::cout << "encoder base updated\n";
                continue;
            }

            PeristalsisCommand cmd;

            try {
                if (input == "g") {
                    cmd = make_go_command(current_posture);
                } else if (input == "s") {
                    cmd = make_stop_command(current_posture);
                } else {
                    int id = std::stoi(input);

                    if (1 <= id && id <= 9) {
                        current_posture = 10 + id;
                        cmd = make_posture_command(id, current_posture);
                    } else {
                        throw std::invalid_argument("unknown command");
                    }
                }
            } catch (const std::exception&) {
                std::cout << "unknown command\n";
                continue;
            }

            const EncoderValues raw = encoder.read_raw();
            const EncoderValues diff = encoder.read_diff();

            const std::string timestamp = now_string();
            const std::string image_path =
                make_image_filename(img_dir, index, cmd.id);

            const bool image_ok = camera.capture_and_save(image_path);

            if (!image_ok) {
                std::cerr << "image save failed, command is not sent\n";
                continue;
            }

            logger.log(
                index,
                timestamp,
                image_path,
                cmd,
                raw,
                diff
            );

            std::cout << "saved: " << image_path << "\n";
            std::cout << "send command: " << cmd.name
                      << " posture=" << static_cast<int>(cmd.stick3)
                      << "\n";

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