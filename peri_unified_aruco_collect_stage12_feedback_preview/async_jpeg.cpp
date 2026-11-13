#include "async_jpeg.hpp"
#include <chrono>
#include <csignal>
#include <filesystem>
#include <stdexcept>
extern volatile std::sig_atomic_t g_stop;
namespace {
using Clock=std::chrono::steady_clock;
double elapsed_ms(Clock::time_point t){
    return std::chrono::duration<double,std::milli>(Clock::now()-t).count();
}
}
AsyncJpegWriter::AsyncJpegWriter(int quality,std::size_t capacity)
    : quality_(quality),capacity_(capacity) {
    if(quality<1||quality>100||capacity==0)throw std::invalid_argument("Invalid JPEG writer settings");
    worker_=std::thread(&AsyncJpegWriter::work,this);
}
AsyncJpegWriter::~AsyncJpegWriter(){
    {std::lock_guard<std::mutex> lock(mutex_);closing_=true;}
    cv_ready_.notify_all();cv_space_.notify_all();
    if(worker_.joinable())worker_.join(); // flush queued images before destroying camera/session
}
SaveTicket AsyncJpegWriter::submit(const std::string& file,const cv::Mat& frame){
    if(frame.empty())throw std::runtime_error("Cannot enqueue an empty camera image");
    const auto begin=Clock::now();
    std::unique_lock<std::mutex> lock(mutex_);
    while(outstanding_>=capacity_ && !closing_ && !failed_.load() && !g_stop)
        cv_space_.wait_for(lock,std::chrono::milliseconds(10));
    if(closing_||failed_.load()||g_stop)
        throw std::runtime_error("JPEG queue stopped, failed or interrupted; no new motion ACK");
    auto job=std::make_unique<Job>();
    job->file=file;
    job->image=frame.clone(); // own pixels; camera may overwrite source on next frame
    if(job->image.empty())throw std::runtime_error("JPEG image clone failed");
    auto future=job->promise.get_future();
    queue_.push_back(std::move(job));
    ++outstanding_;
    const double wait=elapsed_ms(begin);
    lock.unlock();cv_ready_.notify_one();
    return {std::move(future),wait};
}
void AsyncJpegWriter::work() noexcept {
    for(;;){
        std::unique_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_ready_.wait(lock,[&]{return closing_||!queue_.empty();});
            if(queue_.empty()&&closing_)return;
            job=std::move(queue_.front());queue_.pop_front();
        }
        SaveReport report;
        const auto t=Clock::now();
        try {
            report.ok=cv::imwrite(job->file,job->image,{cv::IMWRITE_JPEG_QUALITY,quality_});
            if(report.ok){
                std::error_code ec;
                report.bytes=std::filesystem::file_size(job->file,ec);
                if(ec || report.bytes==0){
                    report.ok=false;report.error="JPEG file missing/empty after write";
                }
            }else report.error="cv::imwrite returned false";
        }catch(const std::exception& e){report.error=e.what();report.ok=false;}
        catch(...){report.error="unknown JPEG exception";report.ok=false;}
        report.jpeg_ms=elapsed_ms(t);
        if(!report.ok)failed_.store(true);
        try{job->promise.set_value(std::move(report));}
        catch(...){failed_.store(true);}
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(outstanding_>0)--outstanding_;
        }
        cv_space_.notify_all();
    }
}

