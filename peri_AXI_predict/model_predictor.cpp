#include "model_predictor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <limits>
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

}  // namespace

ModelConfig ModelPredictor::load_config(const std::string& path) {
    cv::FileStorage fs(path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        throw std::runtime_error("failed to open model config: " + path);
    }

    ModelConfig c;
    fs["model_type"] >> c.model_type;
    fs["image_height"] >> c.image_height;
    fs["image_width"] >> c.image_width;
    fs["image_mode"] >> c.image_mode;
    fs["input_channels"] >> c.input_channels;
    c.mean = read_sequence<float>(fs["mean"]);
    c.std = read_sequence<float>(fs["std"]);
    c.commands = read_sequence<int>(fs["commands"]);

    if (!fs["min_confidence"].empty()) {
        fs["min_confidence"] >> c.min_confidence;
    }
    fs.release();

    c.image_mode = upper_copy(c.image_mode);

    if (c.image_height <= 0 || c.image_width <= 0) {
        throw std::runtime_error("image_height/image_width must be positive in model config");
    }
    if (c.input_channels != 1 && c.input_channels != 3) {
        throw std::runtime_error("input_channels must be 1 or 3 in model config");
    }
    if (c.image_mode != "GRAY" && c.image_mode != "RGB") {
        throw std::runtime_error("image_mode must be GRAY or RGB in model config");
    }
    if (c.image_mode == "RGB" && c.input_channels != 3) {
        throw std::runtime_error("RGB model must have input_channels=3");
    }
    if (c.commands.empty()) {
        throw std::runtime_error("commands is empty in model config");
    }
    if (c.mean.empty()) {
        c.mean.assign(c.input_channels, 0.0F);
    }
    if (c.std.empty()) {
        c.std.assign(c.input_channels, 1.0F);
    }
    if (static_cast<int>(c.mean.size()) != c.input_channels ||
        static_cast<int>(c.std.size()) != c.input_channels) {
        throw std::runtime_error("mean/std length must equal input_channels");
    }
    for (float s : c.std) {
        if (!(s > 0.0F)) {
            throw std::runtime_error("every std value must be positive");
        }
    }
    if (c.min_confidence < 0.0F || c.min_confidence > 1.0F) {
        throw std::runtime_error("min_confidence must be between 0 and 1");
    }

    return c;
}

ModelPredictor::ModelPredictor(const std::string& onnx_path,
                               const std::string& config_path)
    : config_(load_config(config_path)),
      net_(cv::dnn::readNetFromONNX(onnx_path)) {
    if (net_.empty()) {
        throw std::runtime_error("failed to load ONNX model: " + onnx_path);
    }
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
}

cv::Mat ModelPredictor::preprocess(const cv::Mat& bgr_frame) const {
    if (bgr_frame.empty()) {
        throw std::invalid_argument("cannot preprocess an empty frame");
    }

    cv::Mat image;
    if (config_.image_mode == "GRAY") {
        cv::Mat gray;
        cv::cvtColor(bgr_frame, gray, cv::COLOR_BGR2GRAY);
        if (config_.input_channels == 1) {
            image = gray;
        } else {
            cv::cvtColor(gray, image, cv::COLOR_GRAY2BGR);
        }
    } else {
        image = bgr_frame;
    }

    const bool swap_rb = config_.input_channels == 3;
    cv::Mat blob = cv::dnn::blobFromImage(
        image,
        1.0 / 255.0,
        cv::Size(config_.image_width, config_.image_height),
        cv::Scalar(),
        swap_rb,
        false,
        CV_32F);

    const int plane_size = config_.image_height * config_.image_width;
    float* data = reinterpret_cast<float*>(blob.data);
    for (int c = 0; c < config_.input_channels; ++c) {
        float* plane = data + c * plane_size;
        const float mean = config_.mean[c];
        const float std = config_.std[c];
        for (int i = 0; i < plane_size; ++i) {
            plane[i] = (plane[i] - mean) / std;
        }
    }

    return blob;
}

PredictionResult ModelPredictor::predict(const cv::Mat& bgr_frame) {
    cv::Mat blob = preprocess(bgr_frame);
    net_.setInput(blob);

    const auto start = std::chrono::steady_clock::now();
    cv::Mat output = net_.forward();
    const auto end = std::chrono::steady_clock::now();

    if (output.empty() || output.depth() != CV_32F) {
        throw std::runtime_error("model returned an empty or non-float output");
    }
    if (static_cast<std::size_t>(output.total()) != config_.commands.size()) {
        throw std::runtime_error(
            "model output count does not match commands count: output=" +
            std::to_string(output.total()) + " commands=" +
            std::to_string(config_.commands.size()));
    }

    const float* logits = output.ptr<float>();
    const int count = static_cast<int>(output.total());
    const float max_logit = *std::max_element(logits, logits + count);

    std::vector<float> probabilities(count);
    float sum = 0.0F;
    for (int i = 0; i < count; ++i) {
        probabilities[i] = std::exp(logits[i] - max_logit);
        sum += probabilities[i];
    }
    if (!(sum > std::numeric_limits<float>::epsilon())) {
        throw std::runtime_error("softmax sum is invalid");
    }
    for (float& p : probabilities) {
        p /= sum;
    }

    const auto best = std::max_element(probabilities.begin(), probabilities.end());
    const int label = static_cast<int>(std::distance(probabilities.begin(), best));

    PredictionResult result;
    result.label = label;
    result.command = config_.commands[label];
    result.confidence = *best;
    result.probabilities = std::move(probabilities);
    result.inference_ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    return result;
}
