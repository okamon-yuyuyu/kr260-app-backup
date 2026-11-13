#pragma once

#include <opencv2/opencv.hpp>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

// A loopback-only HTTP preview for VS Code Remote SSH port forwarding.
class ManualPreviewServer {
public:
    explicit ManualPreviewServer(std::uint16_t port = 8765);
    ~ManualPreviewServer();
    ManualPreviewServer(const ManualPreviewServer&) = delete;
    ManualPreviewServer& operator=(const ManualPreviewServer&) = delete;
    void start();
    void publish(const cv::Mat& frame);
    std::uint16_t port() const noexcept { return port_; }
private:
    void serve() noexcept;
    void handle_client(int client) noexcept;
    std::uint16_t port_;
    int server_fd_ = -1;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
    std::mutex image_mutex_;
    std::vector<unsigned char> latest_jpeg_;
};
