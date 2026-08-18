#include "aruco_detector.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

ArucoDetector::ArucoDetector(const std::string& yaml_path,
                             double marker_length_m,
                             int target_id,
                             double target_z_mm,
                             double angle_threshold_deg)
    : marker_length_m_(marker_length_m),
      target_id_(target_id),
      target_z_mm_(target_z_mm),
      angle_threshold_deg_(angle_threshold_deg) {
    cv::FileStorage fs(yaml_path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        throw std::runtime_error("failed to open camera yaml: " + yaml_path);
    }

    fs["camera_matrix"] >> camera_matrix_;
    fs["dist_coeffs"] >> dist_coeffs_;
    fs.release();

    if (camera_matrix_.empty() || dist_coeffs_.empty()) {
        throw std::runtime_error("camera_matrix or dist_coeffs is empty in yaml: " + yaml_path);
    }
}

ArucoResult ArucoDetector::detect(const cv::Mat& frame) const {
    ArucoResult result;

    if (frame.empty()) {
        return result;
    }

    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners;
    auto dictionary = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);

    cv::aruco::detectMarkers(frame, dictionary, corners, ids);
    if (ids.empty()) {
        return result;
    }

    std::vector<cv::Vec3d> rvecs, tvecs;
    cv::aruco::estimatePoseSingleMarkers(corners, marker_length_m_, camera_matrix_, dist_coeffs_, rvecs, tvecs);

    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] != target_id_) {
            continue;
        }

        const double x = tvecs[i][0];
        const double y = tvecs[i][1];
        const double z = tvecs[i][2];

        result.detected = true;
        result.id = ids[i];
        result.x_mm = x * 1000.0;
        result.y_mm = y * 1000.0;
        result.z_mm = z * 1000.0;
        result.angle_deg = std::atan2(x, z) * 180.0 / M_PI;
        return result;
    }

    return result;
}

bool ArucoDetector::is_goal(const ArucoResult& r) const {
    return r.detected &&
           r.z_mm <= target_z_mm_ &&
           std::abs(r.angle_deg) <= angle_threshold_deg_;
}
