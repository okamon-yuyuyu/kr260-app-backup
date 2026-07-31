#include "camera.hpp"

#include <iostream>
#include <stdexcept>
#include <unistd.h>

CameraCapture::CameraCapture(int device_id, int width, int height)
    : cap_(device_id, cv::CAP_V4L2) {
    if (!cap_.isOpened()) {
        throw std::runtime_error("Camera open failed");
    }

    cap_.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height);

    // 可能ならカメラバッファを小さくする
    cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);
}

CameraCapture::~CameraCapture() {
    cap_.release();
}

bool CameraCapture::capture_and_save(const std::string& filename) {
    cv::Mat frame;

    // 古いフレームを捨てて、なるべく最新の画像を取る
    for (int i = 0; i < 5; ++i) {
        cap_.grab();
        usleep(30000);  // 30 ms
    }

    cap_ >> frame;

    if (frame.empty()) {
        std::cerr << "Frame capture failed" << std::endl;
        return false;
    }

    cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);

    return cv::imwrite(filename, frame);
}