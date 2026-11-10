#pragma once
#include <opencv2/opencv.hpp>
#include <string>

struct CameraConfig {
    int device=0,width=1280,height=720;
    int flush_frames=2,flush_sleep_ms=0,jpeg_quality=95;
    bool rotate_cw=true;
};
class CameraCapture {
public:
    explicit CameraCapture(CameraConfig cfg);
    bool capture(cv::Mat& frame);  // flush old queued frames, then capture
    bool save_image(const std::string& path,const cv::Mat& frame) const;
private:
    CameraConfig cfg_;
    cv::VideoCapture cap_;
};

