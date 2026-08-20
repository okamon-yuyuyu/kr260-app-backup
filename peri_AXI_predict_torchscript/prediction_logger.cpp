#include "prediction_logger.hpp"

#include <iomanip>
#include <stdexcept>

PredictionLogger::PredictionLogger(
    const std::string& csv_path,
    const ModelConfig& config,
    std::optional<double> start_battery_voltage_v)
    : ofs_(csv_path),
      config_(config),
      start_battery_voltage_v_(start_battery_voltage_v) {
    if (!ofs_) {
        throw std::runtime_error("failed to open prediction log: " + csv_path);
    }

    ofs_ << "index,timestamp,image_path,backend,architecture,model_type,"
            "predicted_label,predicted_command,confidence,preprocess_ms,"
            "inference_ms,total_predict_ms,status,command_sent,current_posture,"
            "start_battery_voltage_v";
    for (int command : config_.commands) {
        ofs_ << ",prob_cmd" << command;
    }
    ofs_ << ",raw0,raw1,raw2,raw3,raw12_0,raw12_1,raw12_2,raw12_3,"
            "diff0,diff1,diff2,diff3,aruco_detected,aruco_id,aruco_x_mm,"
            "aruco_y_mm,aruco_z_mm,aruco_angle_deg\n";
    ofs_.flush();
}

std::string PredictionLogger::csv_escape(const std::string& value) {
    if (value.find_first_of(",\"\n") == std::string::npos) {
        return value;
    }
    std::string escaped = "\"";
    for (char c : value) {
        if (c == '\"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }
    escaped += '"';
    return escaped;
}

void PredictionLogger::log(
    int index,
    const std::string& timestamp,
    const std::string& image_path,
    const PredictionResult& prediction,
    const std::string& status,
    bool command_sent,
    int current_posture,
    const EncoderValues& raw,
    const EncoderValues& raw12,
    const EncoderValues& diff,
    const ArucoResult& aruco) {
    ofs_ << index << ','
         << csv_escape(timestamp) << ','
         << csv_escape(image_path) << ','
         << "torchscript_libtorch_cpu,"
         << csv_escape(config_.architecture) << ','
         << csv_escape(config_.model_type) << ','
         << prediction.label << ','
         << prediction.command << ','
         << std::fixed << std::setprecision(6) << prediction.confidence << ','
         << std::setprecision(3) << prediction.preprocess_ms << ','
         << std::setprecision(3) << prediction.inference_ms << ','
         << std::setprecision(3) << prediction.total_ms << ','
         << csv_escape(status) << ','
         << (command_sent ? 1 : 0) << ','
         << current_posture << ',';

    if (start_battery_voltage_v_) {
        ofs_ << std::setprecision(3) << *start_battery_voltage_v_;
    }

    for (std::size_t i = 0; i < config_.commands.size(); ++i) {
        ofs_ << ',';
        if (i < prediction.probabilities.size()) {
            ofs_ << std::setprecision(6) << prediction.probabilities[i];
        }
    }

    ofs_ << ',' << raw.enc0 << ',' << raw.enc1 << ',' << raw.enc2 << ',' << raw.enc3
         << ',' << raw12.enc0 << ',' << raw12.enc1 << ',' << raw12.enc2 << ',' << raw12.enc3
         << ',' << diff.enc0 << ',' << diff.enc1 << ',' << diff.enc2 << ',' << diff.enc3
         << ',' << (aruco.detected ? 1 : 0)
         << ',' << aruco.id
         << ',' << std::setprecision(3) << aruco.x_mm
         << ',' << aruco.y_mm
         << ',' << aruco.z_mm
         << ',' << aruco.angle_deg
         << '\n';
    ofs_.flush();
}
