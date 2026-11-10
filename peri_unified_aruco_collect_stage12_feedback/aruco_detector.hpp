#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/aruco.hpp>
#include <string>

struct ArucoResult {
    bool detected=false;
    int id=-1;
    double x_mm=0.0,y_mm=0.0,z_mm=0.0,angle_deg=0.0;
};
class ArucoDetector {
public:
    ArucoDetector(const std::string& yaml_path="/home/ubuntu/yaml/camera_many.yaml",
                  double marker_length_m=0.025,int target_id=0,
                  double target_z_mm=300.0,double angle_threshold_deg=15.0);
    ArucoResult detect(const cv::Mat& frame) const;
    bool is_goal(const ArucoResult& result)const;
    double target_z_mm()const{return target_z_mm_;}
    double angle_threshold_deg()const{return angle_threshold_deg_;}
private:
    cv::Mat camera_matrix_,dist_coeffs_;
    double marker_length_m_,target_z_mm_,angle_threshold_deg_;
    int target_id_;
};

