#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <opencv2/opencv.hpp>
#include <string>

class CameraCapture {
public:
    CameraCapture(int device_id = 0, int width = 1280, int height = 720);
    ~CameraCapture();

    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    bool capture_and_save(const std::string& filename);

private:
    cv::VideoCapture cap_;
};

#endif
