#ifndef PREDICTION_LOGGER_HPP
#define PREDICTION_LOGGER_HPP

#include "aruco_detector.hpp"
#include "encoder.hpp"
#include "model_predictor.hpp"

#include <fstream>
#include <optional>
#include <string>

class PredictionLogger {
public:
    PredictionLogger(const std::string& csv_path,
                     const ModelConfig& config,
                     std::optional<double> start_battery_voltage_v);

    void log(int index,
             const std::string& timestamp,
             const std::string& image_path,
             const PredictionResult& prediction,
             const std::string& status,
             bool command_sent,
             int current_posture,
             const EncoderValues& raw,
             const EncoderValues& raw12,
             const EncoderValues& diff,
             const ArucoResult& aruco);

private:
    static std::string csv_escape(const std::string& value);

    std::ofstream ofs_;
    ModelConfig config_;
    std::optional<double> start_battery_voltage_v_;
};

#endif
