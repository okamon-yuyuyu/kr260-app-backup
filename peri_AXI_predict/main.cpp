#include "aruco_detector.hpp"
#include "camera.hpp"
#include "command.hpp"
#include "dead_band_setting.hpp"
#include "encoder.hpp"
#include "model_predictor.hpp"
#include "prediction_logger.hpp"
#include "wire_adjuster.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void handle_signal(int) {
    stop_requested = 1;
}

struct Options {
    std::string model_path;
    std::string config_path;
    std::string experiment_name;
    std::string camera_yaml = "/home/ubuntu/yaml/camera_many.yaml";
    int camera_id = 0;
    int max_steps = 500;
    std::optional<float> min_confidence_override;
    bool preview = false;
};

void print_usage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n"
        << "  --model PATH            ONNX model path\n"
        << "  --config PATH           model YAML path\n"
        << "  --experiment NAME       output run name prefix\n"
        << "  --min-confidence VALUE  override YAML threshold (0..1)\n"
        << "  --max-steps N           safety step limit (default: 500)\n"
        << "  --camera-id N           V4L2 camera ID (default: 0)\n"
        << "  --camera-yaml PATH      ArUco calibration YAML\n"
        << "  --preview               inference/logging only; do not access AXI\n"
        << "  --help                  show this help\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument("missing value after " + arg);
            }
            return argv[++i];
        };

        if (arg == "--model") {
            options.model_path = require_value();
        } else if (arg == "--config") {
            options.config_path = require_value();
        } else if (arg == "--experiment") {
            options.experiment_name = require_value();
        } else if (arg == "--min-confidence") {
            options.min_confidence_override = std::stof(require_value());
        } else if (arg == "--max-steps") {
            options.max_steps = std::stoi(require_value());
        } else if (arg == "--camera-id") {
            options.camera_id = std::stoi(require_value());
        } else if (arg == "--camera-yaml") {
            options.camera_yaml = require_value();
        } else if (arg == "--preview") {
            options.preview = true;
        } else if (arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }

    if (options.max_steps <= 0) {
        throw std::invalid_argument("max-steps must be positive");
    }
    if (options.min_confidence_override &&
        (*options.min_confidence_override < 0.0F ||
         *options.min_confidence_override > 1.0F)) {
        throw std::invalid_argument("min-confidence must be between 0 and 1");
    }
    return options;
}

std::string now_string() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string make_image_filename(const std::string& image_dir,
                                int index,
                                const PredictionResult& prediction,
                                const std::string& suffix = "") {
    std::ostringstream oss;
    oss << image_dir << "/img_" << std::setw(5) << std::setfill('0') << index;
    if (prediction.command >= 0) {
        oss << "_pred" << prediction.command
            << "_conf" << std::fixed << std::setprecision(3)
            << prediction.confidence;
    }
    if (!suffix.empty()) {
        oss << '_' << suffix;
    }
    oss << '_' << now_string() << ".jpg";
    return oss.str();
}

PeristalsisCommand make_exit_reset_command() {
    PeristalsisCommand cmd;
    cmd.id = 999;
    cmd.name = "exit_reset_posture_15";
    cmd.stick1 = 15;
    cmd.stick3 = 15;
    return cmd;
}

void reset_posture_to_15(CommandInterface& command) {
    std::cout << "Reset posture to 15...\n";
    command.send(make_exit_reset_command());
    if (command.wait_done(10000)) {
        std::cout << "posture reset done\n";
    } else {
        std::cerr << "timeout waiting posture reset\n";
    }
}

void print_prediction(const PredictionResult& p, const ModelConfig& config) {
    std::cout << "prediction: cmd" << p.command
              << " confidence=" << std::fixed << std::setprecision(3)
              << p.confidence
              << " inference=" << std::setprecision(1) << p.inference_ms << " ms\n";
    std::cout << "probabilities:";
    for (std::size_t i = 0; i < p.probabilities.size(); ++i) {
        std::cout << " cmd" << config.commands[i] << '='
                  << std::setprecision(3) << p.probabilities[i];
    }
    std::cout << '\n';
}

EncoderValues read_or_zero(const std::unique_ptr<EncoderReader>& encoder,
                           int kind) {
    if (!encoder) {
        return {};
    }
    if (kind == 0) {
        return encoder->read_raw();
    }
    if (kind == 1) {
        return encoder->read_raw12();
    }
    return encoder->read_diff();
}

}  // namespace

int main(int argc, char** argv) {
    std::unique_ptr<CommandInterface> command;
    bool posture_reset = false;

    try {
        Options options = parse_options(argc, argv);

        if (options.model_path.empty()) {
            std::cout << "ONNX model path: ";
            std::cin >> options.model_path;
        }
        if (options.config_path.empty()) {
            std::cout << "Model YAML path: ";
            std::cin >> options.config_path;
        }
        if (options.experiment_name.empty()) {
            std::cout << "Experiment name: ";
            std::cin >> options.experiment_name;
        }

        ModelPredictor predictor(options.model_path, options.config_path);
        ModelConfig config = predictor.config();
        const float min_confidence =
            options.min_confidence_override.value_or(config.min_confidence);

        std::cout << "\nModel loaded\n"
                  << "  type: " << config.model_type << '\n'
                  << "  input: " << config.input_channels << 'x'
                  << config.image_height << 'x' << config.image_width << '\n'
                  << "  mode: " << config.image_mode << '\n'
                  << "  min confidence: " << min_confidence << '\n'
                  << "  preview: " << (options.preview ? "yes" : "no") << "\n\n";

        double voltage = 0.0;
        while (true) {
            std::cout << "Battery voltage at start [V]: ";
            if (std::cin >> voltage && voltage > 0.0) {
                break;
            }
            std::cout << "Enter a positive number (example: 12.3).\n";
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }

        const std::string run_name =
            options.experiment_name + "_predict_" + now_string();
        const std::string out_dir = "data/" + run_name;
        const std::string image_dir = out_dir + "/images";
        const std::string csv_path = out_dir + "/prediction_log.csv";
        std::filesystem::create_directories(image_dir);

        CameraCapture camera(options.camera_id);
        ArucoDetector aruco(options.camera_yaml);
        PredictionLogger logger(csv_path, config, voltage);
        std::unique_ptr<EncoderReader> encoder;

        if (!options.preview) {
            constexpr uint32_t DEAD_BAND_MANUAL = 5;
            constexpr uint32_t DEAD_BAND_AUTO = 20;

            DeadBandSetting dead_band;
            WireAdjuster wire;
            dead_band.set(DEAD_BAND_MANUAL, DEAD_BAND_AUTO);
            wire.set_target_raw(725, 3453, 2700, 267);
            wire.wait_user_confirm();

            command = std::make_unique<CommandInterface>();
            encoder = std::make_unique<EncoderReader>();
        }

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        std::cout << "Output directory: " << out_dir << '\n'
                  << "Goal: ArUco Z <= " << aruco.target_z_mm()
                  << " mm and |angle| <= " << aruco.angle_threshold_deg() << " deg\n"
                  << "Press Ctrl+C to stop safely.\n\n";

        int current_posture = 15;
        int index = 0;

        while (!stop_requested && index < options.max_steps) {
            cv::Mat frame;
            if (!camera.capture(frame)) {
                std::cerr << "image capture failed; stopping\n";
                break;
            }

            const ArucoResult aruco_result = aruco.detect(frame);
            const EncoderValues raw = read_or_zero(encoder, 0);
            const EncoderValues raw12 = read_or_zero(encoder, 1);
            const EncoderValues diff = read_or_zero(encoder, 2);
            const std::string timestamp = now_string();

            if (aruco.is_goal(aruco_result)) {
                PredictionResult goal;
                goal.command = 900;
                goal.confidence = 1.0F;
                const std::string image_path =
                    make_image_filename(image_dir, index, goal, "aruco_goal");
                camera.save_image(image_path, frame);
                logger.log(index, timestamp, image_path, goal, "aruco_goal", false,
                           current_posture, raw, raw12, diff, aruco_result);
                std::cout << "ArUco goal reached.\n";
                break;
            }

            const PredictionResult prediction = predictor.predict(frame);
            print_prediction(prediction, config);

            const std::string image_path =
                make_image_filename(image_dir, index, prediction);
            if (!camera.save_image(image_path, frame)) {
                throw std::runtime_error("failed to save inference image; command not sent");
            }

            if (prediction.confidence < min_confidence) {
                logger.log(index, timestamp, image_path, prediction, "low_confidence",
                           false, current_posture, raw, raw12, diff, aruco_result);
                std::cout << "Confidence below threshold. Stop without sending command.\n";
                break;
            }

            if (prediction.command == 900) {
                logger.log(index, timestamp, image_path, prediction, "model_stop",
                           false, current_posture, raw, raw12, diff, aruco_result);
                std::cout << "Model predicted cmd900. Stop.\n";
                break;
            }

            PeristalsisCommand motion;
            if (1 <= prediction.command && prediction.command <= 9) {
                const int predicted_posture = 10 + prediction.command;
                motion = make_posture_command(prediction.command, predicted_posture);
                if (!options.preview) {
                    current_posture = predicted_posture;
                }
            } else if (prediction.command == 100) {
                motion = make_go_command(current_posture);
            } else {
                logger.log(index, timestamp, image_path, prediction,
                           "unsupported_command", false, current_posture,
                           raw, raw12, diff, aruco_result);
                std::cerr << "Unsupported predicted command: "
                          << prediction.command << ". Stop.\n";
                break;
            }

            if (options.preview) {
                logger.log(index, timestamp, image_path, prediction, "preview",
                           false, current_posture, raw, raw12, diff, aruco_result);
                ++index;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            command->send(motion);
            const bool done = command->wait_done(10000);
            logger.log(index, timestamp, image_path, prediction,
                       done ? "done" : "command_timeout", true,
                       current_posture, raw, raw12, diff, aruco_result);
            ++index;

            if (!done) {
                std::cerr << "Command completion timeout. Stop.\n";
                break;
            }
        }

        if (index >= options.max_steps) {
            std::cout << "Maximum step count reached.\n";
        } else if (stop_requested) {
            std::cout << "Stop requested by signal.\n";
        }

        if (command) {
            reset_posture_to_15(*command);
            posture_reset = true;
        }

        std::cout << "Prediction finished. Log: " << csv_path << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        if (command && !posture_reset) {
            try {
                reset_posture_to_15(*command);
            } catch (const std::exception& reset_error) {
                std::cerr << "posture reset error: " << reset_error.what() << '\n';
            }
        }
        return 1;
    }
}
