#include <opencv2/opencv.hpp>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <filesystem>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

std::string getTimeString()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm_now;
    localtime_r(&t, &tm_now);

    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y%m%d_%H%M%S");

    return oss.str();
}

int main()
{
    std::string experiment_name;

    std::cout << "実験名を入力してください: ";
    std::getline(std::cin, experiment_name);

    if (experiment_name.empty())
    {
        std::cerr << "実験名が空です" << std::endl;
        return -1;
    }

    fs::path save_dir = experiment_name;

    if (!fs::exists(save_dir))
    {
        fs::create_directories(save_dir);
    }

    cv::VideoCapture cap(0, cv::CAP_V4L2);

    if (!cap.isOpened())
    {
        std::cerr << "Camera open failed" << std::endl;
        return -1;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

    std::cout << "Actual resolution : "
              << cap.get(cv::CAP_PROP_FRAME_WIDTH)
              << " x "
              << cap.get(cv::CAP_PROP_FRAME_HEIGHT)
              << std::endl;

    int count = 0;
    std::string input;
    cv::Mat frame;

    std::cout << std::endl;
    std::cout << "Enterキーで保存" << std::endl;
    std::cout << "q + Enterで終了" << std::endl;

    while (true)
    {
        std::getline(std::cin, input);

        if (input == "q")
        {
            break;
        }

        // 古いフレームを捨てる
        for (int i = 0; i < 5; ++i)
        {
            cap.grab();
            usleep(30000);
        }

        cap >> frame;

        if (frame.empty())
        {
            std::cerr << "Frame capture failed" << std::endl;
            continue;
        }

        // 学習時と同じ縦向き画像にする
        cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);

        std::string time_str = getTimeString();

        std::ostringstream filename;
        filename << experiment_name
                 << "_"
                 << time_str
                 << "_"
                 << std::setw(4)
                 << std::setfill('0')
                 << count
                 << ".jpg";

        fs::path save_path = save_dir / filename.str();

        std::vector<int> params;
        params.push_back(cv::IMWRITE_JPEG_QUALITY);
        params.push_back(100);

        if (!cv::imwrite(save_path.string(), frame, params))
        {
            std::cerr << "Save failed" << std::endl;
            continue;
        }

        std::cout << "Saved : "
                  << save_path.string()
                  << std::endl;

        count++;
    }

    cap.release();

    return 0;
}