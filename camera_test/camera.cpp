#include <opencv2/opencv.hpp>
#include <iostream>
#include <iomanip>
#include <sstream>

int main() {
    cv::VideoCapture cap(0, cv::CAP_V4L2);

    if (!cap.isOpened()) {
        std::cerr << "Camera open failed" << std::endl;
        return -1;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);

    int count = 0;
    std::string input;
    cv::Mat frame;

    std::cout << "Enterキーで画像保存, q + Enterで終了" << std::endl;

    while (true) {
        std::getline(std::cin, input);

        if (input == "q") {
            break;
        }

        cap >> frame;
        cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);

        if (frame.empty()) {
            std::cerr << "Frame capture failed" << std::endl;
            continue;
        }

        std::ostringstream filename;
        filename << "camera_"
                 << std::setw(4) << std::setfill('0')
                 << count
                 << ".jpg";

        cv::imwrite(filename.str(), frame);
        std::cout << "Saved: " << filename.str() << std::endl;

        count++;
    }

    cap.release();
    return 0;
}