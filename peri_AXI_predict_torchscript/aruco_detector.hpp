#ifndef ARUCO_DETECTOR_HPP
#define ARUCO_DETECTOR_HPP

#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>
#include <string>

struct ArucoResult {
    bool detected = false;
    int id = -1;
    double x_mm = 0.0;
    double y_mm = 0.0;
    double z_mm = 0.0;
    double angle_deg = 0.0;  // atan2(X, Z). Positive means marker is to the right of the camera axis.
};

class ArucoDetector {
public:
    ArucoDetector(const std::string& yaml_path = "/home/ubuntu/yaml/camera_many.yaml",
                  double marker_length_m = 0.025,
                  int target_id = 0,
                  double target_z_mm = 300.0,
                  double angle_threshold_deg = 15.0);

    ArucoResult detect(const cv::Mat& frame) const;
    bool is_goal(const ArucoResult& r) const;

    double target_z_mm() const { return target_z_mm_; }
    double angle_threshold_deg() const { return angle_threshold_deg_; }

private:
    cv::Mat camera_matrix_;
    cv::Mat dist_coeffs_;
    double marker_length_m_;
    int target_id_;
    double target_z_mm_;
    double angle_threshold_deg_;
};

#endif
