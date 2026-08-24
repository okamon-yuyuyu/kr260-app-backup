#ifndef SAFETY_LOGGER_HPP
#define SAFETY_LOGGER_HPP

#include "aruco_detector.hpp"
#include "encoder.hpp"

#include <cstddef>
#include <fstream>
#include <string>

struct InferenceSafetyRecord {
    std::string timestamp;
    std::string reason;
    std::string phase;
    std::size_t command_no = 0;
    std::size_t last_completed_command_no = 0;
    int predicted_command = -1;
    float confidence = 0.0F;
    int current_posture_before_reset = 15;
    std::string image_path;
    bool active_command_done_before_reset = false;
    bool reset_done = false;
    EncoderValues raw;
    EncoderValues raw12;
    EncoderValues diff;
    ArucoResult aruco;
};

class SafetyLogger {
public:
    explicit SafetyLogger(const std::string& csv_path);

    void log_emergency_stop(const InferenceSafetyRecord& record);

private:
    std::ofstream ofs_;
};

#endif
