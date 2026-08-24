#ifndef TORCHSCRIPT_PREDICTOR_HPP
#define TORCHSCRIPT_PREDICTOR_HPP

#include <opencv2/opencv.hpp>
#include <torch/script.h>

#include <string>
#include <vector>

struct ModelConfig {
    std::string architecture;
    std::string model_type;
    int image_height = 0;
    int image_width = 0;
    std::string image_mode;
    int input_channels = 0;
    std::vector<float> mean;
    std::vector<float> std;
    std::vector<int> commands;
    float min_confidence = 0.0F;
};

struct PredictionResult {
    int label = -1;
    int command = -1;
    float confidence = 0.0F;
    std::vector<float> probabilities;
    double preprocess_ms = 0.0;
    double inference_ms = 0.0;
    double total_ms = 0.0;
};

class TorchScriptPredictor {
public:
    TorchScriptPredictor(const std::string& model_path,
                         const std::string& config_path,
                         int torch_threads = 4,
                         int warmup_runs = 5);

    PredictionResult predict(const cv::Mat& bgr_frame);
    const ModelConfig& config() const { return config_; }

private:
    static ModelConfig load_config(const std::string& path);
    torch::Tensor preprocess(const cv::Mat& bgr_frame) const;
    void warmup(int count);

    ModelConfig config_;
    torch::jit::script::Module module_;
};

#endif
