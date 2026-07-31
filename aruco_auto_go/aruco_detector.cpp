#include "aruco_detector.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

ArucoDetector::ArucoDetector(int camera_id,
                             int width,
                             int height,
                             const std::string& yaml_path,
                             double marker_length_m)
    : cap_(camera_id, cv::CAP_V4L2), marker_length_m_(marker_length_m) {

    cv::FileStorage fs(yaml_path, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        throw std::runtime_error("camera yaml open failed: " + yaml_path);
    }

    fs["camera_matrix"] >> camera_matrix_;
    fs["dist_coeffs"] >> dist_coeffs_;
    fs.release();

    if (!cap_.isOpened()) {
        throw std::runtime_error("camera open failed");
    }

    cap_.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);

    dictionary_ = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);

    const float L = static_cast<float>(marker_length_m_ / 2.0);
    object_points_.push_back(cv::Point3f(-L,  L, 0)); // 左上
    object_points_.push_back(cv::Point3f( L,  L, 0)); // 右上
    object_points_.push_back(cv::Point3f( L, -L, 0)); // 右下
    object_points_.push_back(cv::Point3f(-L, -L, 0)); // 左下
}

ArucoDetector::~ArucoDetector() {
    cap_.release();
}

void ArucoDetector::flush_old_frames() {
    for (int i = 0; i < 5; ++i) {
        cap_.grab();
        usleep(30000); // 30 ms
    }
}

bool ArucoDetector::detect(ArucoPose& pose, const std::string& save_path) {
    pose = ArucoPose{};

    flush_old_frames();

    cv::Mat frame;
    cap_ >> frame;
    if (frame.empty()) {
        std::cerr << "frame capture failed" << std::endl;
        return false;
    }

    cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);

    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners;
    cv::aruco::detectMarkers(frame, dictionary_, corners, ids);

    if (ids.empty()) {
        if (!save_path.empty()) {
            cv::imwrite(save_path, frame);
        }
        return false;
    }

    // 複数見えた場合はいちばん先頭のマーカを使う
    const size_t i = 0;
    cv::Mat rvec, tvec;
    const bool ok = cv::solvePnP(
        object_points_,
        corners[i],
        camera_matrix_,
        dist_coeffs_,
        rvec,
        tvec
    );

    if (!ok) {
        if (!save_path.empty()) {
            cv::imwrite(save_path, frame);
        }
        return false;
    }

    const double x = tvec.at<double>(0);
    const double y = tvec.at<double>(1);
    const double z = tvec.at<double>(2);

    pose.detected = true;
    pose.id = ids[i];
    pose.x_mm = x * 1000.0;
    pose.y_mm = y * 1000.0;
    pose.z_mm = z * 1000.0;
    pose.distance_mm = std::sqrt(x * x + y * y + z * z) * 1000.0;
    pose.angle_deg = std::atan2(x, z) * 180.0 / M_PI;

    cv::Mat R;
    cv::Rodrigues(rvec, R);

    const double sy = std::sqrt(
        R.at<double>(0,0) * R.at<double>(0,0) +
        R.at<double>(1,0) * R.at<double>(1,0)
    );

    const bool singular = sy < 1e-6;
    double roll, pitch, yaw;

    if (!singular) {
        roll  = std::atan2(R.at<double>(2,1), R.at<double>(2,2));
        pitch = std::atan2(-R.at<double>(2,0), sy);
        yaw   = std::atan2(R.at<double>(1,0), R.at<double>(0,0));
    } else {
        roll  = std::atan2(-R.at<double>(1,2), R.at<double>(1,1));
        pitch = std::atan2(-R.at<double>(2,0), sy);
        yaw   = 0.0;
    }

    pose.roll_deg  = roll  * 180.0 / M_PI;
    pose.pitch_deg = pitch * 180.0 / M_PI;
    pose.yaw_deg   = yaw   * 180.0 / M_PI;

    cv::aruco::drawDetectedMarkers(frame, corners, ids);
    cv::drawFrameAxes(frame, camera_matrix_, dist_coeffs_, rvec, tvec, marker_length_m_ * 0.5);

    char text1[128];
    char text2[128];
    char text3[128];
    std::snprintf(text1, sizeof(text1), "ID=%d X=%.0f Y=%.0f Z=%.0f mm", pose.id, pose.x_mm, pose.y_mm, pose.z_mm);
    std::snprintf(text2, sizeof(text2), "Angle=%.1f deg Dist=%.0f mm", pose.angle_deg, pose.distance_mm);
    std::snprintf(text3, sizeof(text3), "Roll=%.1f Pitch=%.1f Yaw=%.1f deg", pose.roll_deg, pose.pitch_deg, pose.yaw_deg);

    cv::putText(frame, text1, cv::Point(20, 40), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
    cv::putText(frame, text2, cv::Point(20, 80), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
    cv::putText(frame, text3, cv::Point(20, 120), cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2);

    if (!save_path.empty()) {
        cv::imwrite(save_path, frame);
    }

    return true;
}
