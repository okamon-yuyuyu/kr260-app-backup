#ifndef ARUCO_DETECTOR_HPP
#define ARUCO_DETECTOR_HPP

#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>
#include <string>

struct ArucoPose {
    bool detected = false;
    int id = -1;

    double x_mm = 0.0;
    double y_mm = 0.0;
    double z_mm = 0.0;
    double distance_mm = 0.0;
    double angle_deg = 0.0;   // atan2(x, z): カメラ正面からの左右角度

    double roll_deg = 0.0;
    double pitch_deg = 0.0;
    double yaw_deg = 0.0;
};

class ArucoDetector {
public:
    ArucoDetector(int camera_id = 0,
                  int width = 1280,
                  int height = 720,
                  const std::string& yaml_path = "/home/ubuntu/yaml/camera_many.yaml",
                  double marker_length_m = 0.05);
    ~ArucoDetector();

    ArucoDetector(const ArucoDetector&) = delete;
    ArucoDetector& operator=(const ArucoDetector&) = delete;

    bool detect(ArucoPose& pose, const std::string& save_path = "");

private:
    cv::VideoCapture cap_;
    cv::Mat camera_matrix_;
    cv::Mat dist_coeffs_;
    cv::Ptr<cv::aruco::Dictionary> dictionary_;
    std::vector<cv::Point3f> object_points_;
    double marker_length_m_;

    void flush_old_frames();
};

#endif
