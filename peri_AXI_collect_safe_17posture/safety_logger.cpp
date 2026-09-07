#include "safety_logger.hpp"

#include <stdexcept>

SafetyLogger::SafetyLogger(const std::string& csv_path) : ofs_(csv_path) {
    if (!ofs_) {
        throw std::runtime_error("failed to open safety log file: " + csv_path);
    }

    ofs_ << "timestamp,reason,command_no,last_completed_command_no,active_command,"
         << "current_posture_before_reset,image_path,"
         << "active_command_done_before_reset,reset_done,"
         << "aruco_detected,aruco_id,aruco_x_mm,aruco_y_mm,aruco_z_mm,aruco_angle_deg,"
         << "raw0,raw1,raw2,raw3,"
         << "raw12_0,raw12_1,raw12_2,raw12_3,"
         << "diff0,diff1,diff2,diff3\n";
    ofs_.flush();
}

void SafetyLogger::log_emergency_stop(const EmergencyStopRecord& r) {
    ofs_ << r.timestamp << ','
         << r.reason << ','
         << r.command_no << ','
         << r.last_completed_command_no << ','
         << r.active_command << ','
         << r.current_posture_before_reset << ','
         << r.image_path << ','
         << (r.active_command_done_before_reset ? 1 : 0) << ','
         << (r.reset_done ? 1 : 0) << ','
         << (r.aruco.detected ? 1 : 0) << ','
         << r.aruco.id << ','
         << r.aruco.x_mm << ',' << r.aruco.y_mm << ','
         << r.aruco.z_mm << ',' << r.aruco.angle_deg << ','
         << r.raw.enc0 << ',' << r.raw.enc1 << ',' << r.raw.enc2 << ',' << r.raw.enc3 << ','
         << r.raw12.enc0 << ',' << r.raw12.enc1 << ',' << r.raw12.enc2 << ',' << r.raw12.enc3 << ','
         << r.diff.enc0 << ',' << r.diff.enc1 << ',' << r.diff.enc2 << ',' << r.diff.enc3
         << '\n';
    ofs_.flush();
}
