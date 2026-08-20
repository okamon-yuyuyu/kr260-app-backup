#include "torchscript_predictor.hpp"

#include <ATen/Parallel.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>

namespace {

std::string upper_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

template <typename T>
std::vector<T> read_sequence(const cv::FileNode& node) {
    std::vector<T> values;
    if (node.empty() || node.type() != cv::FileNode::SEQ) {
        return values;
    }
    for (const auto& item : node) {
        values.push_back(static_cast<T>(item));
    }
    return values;
}

double milliseconds(std::chrono::steady_clock::time_point start,
                    std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

}  // namespace

ModelConfig TorchScriptPredictor::load_config(const std::string& path) {
    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        throw std::runtime_error("failed to open model config: " + path);
    }

    ModelConfig config;
    fs["architecture"] >> config.architecture;
    fs["model_type"] >> config.model_type;
    fs["image_height"] >> config.image_height;
    fs["image_width"] >> config.image_width;
    fs["image_mode"] >> config.image_mode;
    fs["input_channels"] >> config.input_channels;
    config.mean = read_sequence<float>(fs["mean"]);
    config.std = read_sequence<float>(fs["std"]);
    config.commands = read_sequence<int>(fs["commands"]);
    if (!fs["min_confidence"].empty()) {
        fs["min_confidence"] >> config.min_confidence;
    }
    fs.release();

    config.image_mode = upper_copy(config.image_mode);
    if (config.architecture.empty()) {
        throw std::runtime_error("architecture is empty in model config");
    }
    if (config.image_height <= 0 || config.image_width <= 0) {
        throw std::runtime_error("image_height/image_width must be positive");
    }
    if (config.input_channels != 1 && config.input_channels != 3) {
        throw std::runtime_error("input_channels must be 1 or 3");
    }
    if (config.image_mode != "GRAY" && config.image_mode != "RGB") {
        throw std::runtime_error("image_mode must be GRAY or RGB");
    }
    if (config.image_mode == "RGB" && config.input_channels != 3) {
        throw std::runtime_error("RGB mode requires input_channels=3");
    }
    if (config.commands.empty()) {
        throw std::runtime_error("commands is empty in model config");
    }
    if (config.mean.empty()) {
        config.mean.assign(config.input_channels, 0.0F);
    }
    if (config.std.empty()) {
        config.std.assign(config.input_channels, 1.0F);
    }
    if (static_cast<int>(config.mean.size()) != config.input_channels ||
        static_cast<int>(config.std.size()) != config.input_channels) {
        throw std::runtime_error("mean/std length must equal input_channels");
    }
    for (float value : config.std) {
        if (!(value > 0.0F)) {
            throw std::runtime_error("every std value must be positive");
        }
    }
    if (config.min_confidence < 0.0F || config.min_confidence > 1.0F) {
        throw std::runtime_error("min_confidence must be between 0 and 1");
    }
    return config;
}

TorchScriptPredictor::TorchScriptPredictor(const std::string& model_path,
                                           const std::string& config_path,
                                           int torch_threads,
                                           int warmup_runs)
    : config_(load_config(config_path)),
      module_(torch::jit::load(model_path, torch::Device(torch::kCPU))) {
    if (torch_threads <= 0) {
        throw std::invalid_argument("torch_threads must be positive");
    }
    if (warmup_runs < 0) {
        throw std::invalid_argument("warmup_runs must not be negative");
    }

    at::set_num_threads(torch_threads);
    at::set_num_interop_threads(1);
    module_.eval();
    warmup(warmup_runs);
}

torch::Tensor TorchScriptPredictor::preprocess(const cv::Mat& bgr_frame) const {
    if (bgr_frame.empty()) {
        throw std::invalid_argument("cannot preprocess an empty image");
    }

    cv::Mat image;
    if (config_.image_mode == "GRAY") {
        cv::Mat gray;
        cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);
        if (config_.input_channels == 1) {
            image = gray;
        } else {
            cv::cvtColor(gray, image, cv::COLOR_GRAY2RGB);
        }
    } else {
        cv::cvtColor(bgr_frame, image, cv::COLOR_BGR2RGB);
    }

    cv::Mat resized;
    cv::resize(image, resized,
               cv::Size(config_.image_width, config_.image_height),
               0.0, 0.0, cv::INTER_LINEAR);

    cv::Mat float_image;
    resized.convertTo(float_image, CV_32F, 1.0 / 255.0);

    torch::Tensor input;
    if (config_.input_channels == 1) {
        input = torch::from_blob(
                    float_image.data,
                    {1, 1, config_.image_height, config_.image_width},
                    torch::TensorOptions().dtype(torch::kFloat32))
                    .clone();
    } else {
        input = torch::from_blob(
                    float_image.data,
                    {1, config_.image_height, config_.image_width, 3},
                    torch::TensorOptions().dtype(torch::kFloat32))
                    .permute({0, 3, 1, 2})
                    .contiguous();
    }

    for (int channel = 0; channel < config_.input_channels; ++channel) {
        input[0][channel].sub_(config_.mean[channel]).div_(config_.std[channel]);
    }
    return input;
}

void TorchScriptPredictor::warmup(int count) {
    c10::InferenceMode inference_mode;
    torch::Tensor dummy = torch::zeros(
        {1, config_.input_channels, config_.image_height, config_.image_width},
        torch::TensorOptions().dtype(torch::kFloat32));
    for (int i = 0; i < count; ++i) {
        module_.forward({dummy});
    }
}

PredictionResult TorchScriptPredictor::predict(const cv::Mat& bgr_frame) {
    const auto total_start = std::chrono::steady_clock::now();
    torch::Tensor input = preprocess(bgr_frame);
    const auto inference_start = std::chrono::steady_clock::now();

    torch::Tensor output;
    {
        c10::InferenceMode inference_mode;
        output = module_.forward({input}).toTensor();
    }
    const auto inference_end = std::chrono::steady_clock::now();

    if (output.dim() != 2 || output.size(0) != 1) {
        throw std::runtime_error("model output must have shape [1, classes]");
    }
    if (output.size(1) != static_cast<int64_t>(config_.commands.size())) {
        throw std::runtime_error(
            "model output count does not match commands count: output=" +
            std::to_string(output.size(1)) + " commands=" +
            std::to_string(config_.commands.size()));
    }

    torch::Tensor probabilities_tensor =
        torch::softmax(output, 1).to(torch::kCPU).contiguous();
    const int label = probabilities_tensor.argmax(1).item<int>();
    const float* probability_data = probabilities_tensor.data_ptr<float>();
    std::vector<float> probabilities(
        probability_data, probability_data + probabilities_tensor.size(1));
    const auto total_end = std::chrono::steady_clock::now();

    PredictionResult result;
    result.label = label;
    result.command = config_.commands.at(label);
    result.confidence = probabilities.at(label);
    result.probabilities = std::move(probabilities);
    result.preprocess_ms = milliseconds(total_start, inference_start);
    result.inference_ms = milliseconds(inference_start, inference_end);
    result.total_ms = milliseconds(total_start, total_end);
    return result;
}
