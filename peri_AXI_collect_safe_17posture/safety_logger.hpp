#ifndef SAFETY_LOGGER_HPP
#define SAFETY_LOGGER_HPP

#include "aruco_detector.hpp"
#include "encoder.hpp"

#include <cstddef>
#include <fstream>
#include <string>

struct EmergencyStopRecord {
    std::string timestamp;
    std::string reason;
    std::size_t command_no = 0;
    std::size_t last_completed_command_no = 0;
    std::string active_command;
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

    void log_emergency_stop(const EmergencyStopRecord& record);

private:
    std::ofstream ofs_;
};

#endif
