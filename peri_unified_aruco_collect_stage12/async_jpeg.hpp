#pragma once
#include <opencv2/opencv.hpp>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Single camera owner (main thread), single JPEG worker. No AXI accesses in worker.
struct SaveReport {
    bool ok = false;
    double jpeg_ms = -1.0;
    std::uintmax_t bytes = 0;
    std::string error;
};
struct SaveTicket {
    std::future<SaveReport> result;
    double queue_wait_ms = -1.0; // includes image deep-copy and queue wait
};
class AsyncJpegWriter {
public:
    AsyncJpegWriter(int quality, std::size_t capacity=3);
    ~AsyncJpegWriter();
    AsyncJpegWriter(const AsyncJpegWriter&) = delete;
    AsyncJpegWriter& operator=(const AsyncJpegWriter&) = delete;
    SaveTicket submit(const std::string& file, const cv::Mat& frame);
    bool failed() const noexcept { return failed_.load(); }
    std::size_t capacity() const noexcept { return capacity_; }
private:
    struct Job {
        std::string file;
        cv::Mat image;
        std::promise<SaveReport> promise;
    };
    void work() noexcept;
    const int quality_;
    const std::size_t capacity_;
    std::atomic<bool> failed_{false};
    std::mutex mutex_;
    std::condition_variable cv_ready_, cv_space_;
    std::deque<std::unique_ptr<Job>> queue_;
    std::size_t outstanding_=0; // queued + being written
    bool closing_=false;
    std::thread worker_;
};
