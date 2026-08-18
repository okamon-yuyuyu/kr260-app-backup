#ifndef MODEL_PREDICTOR_HPP
#define MODEL_PREDICTOR_HPP

#include <opencv2/dnn.hpp>
#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

struct ModelConfig {
    std::string model_type;
    int image_height = 0;
    int image_width = 0;
    std::string image_mode;
    int input_channels = 0;
    std::vector<float> mean;
    std::vector<float> std;
    std::vector<int> commands;
    float min_confidence = 0.50F;
};

struct PredictionResult {
    int label = -1;
    int command = -1;
    float confidence = 0.0F;
    std::vector<float> probabilities;
    double inference_ms = 0.0;
};

class ModelPredictor {
public:
    ModelPredictor(const std::string& onnx_path,
                   const std::string& config_path);

    PredictionResult predict(const cv::Mat& bgr_frame);
    const ModelConfig& config() const { return config_; }

private:
    static ModelConfig load_config(const std::string& path);
    cv::Mat preprocess(const cv::Mat& bgr_frame) const;

    ModelConfig config_;
    cv::dnn::Net net_;
};

#endif
