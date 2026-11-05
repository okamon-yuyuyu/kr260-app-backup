#pragma once
#include "aruco_detector.hpp"
#include "protocol.hpp"
#include "encoder.hpp"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>

using Moment=std::chrono::steady_clock::time_point;
using Milliseconds=std::chrono::milliseconds;
struct LogEvent {
    int no=0;
    std::string mode,kind,image,quality="OK",timestamp;
    int request=-1,step=-1,q=-1;
    std::uint32_t step_raw=0;
    ArucoResult aruco;
    EncoderSample enc;
    // Effective command values, recorded at the pre-action image and updated on actual command acceptance.
    int command_id=-1,stick1=-1,stick3=-1;
    std::string command_name;
    int rom0=-1,rom1=-1,rom2=-1,rom3=-1;
    int stroke_right=-1,stroke_left=-1,stroke_up=-1,stroke_down=-1;
    double request_ms=-1,capture_ms=-1,ack_ms=-1,label_ms=-1;
    double camera_duration_ms=-1,aruco_duration_ms=-1,queue_wait_ms=-1,jpeg_ms=-1;
    std::uintmax_t jpeg_bytes=0;
    std::string jpeg_status="NOT_REQUESTED";
};
class Logger {
public:
    explicit Logger(const std::string& csv_path,double battery_voltage);
    void write(const LogEvent& e);
private:
    std::ofstream out_, legacy_;
    double battery_voltage_;
};
