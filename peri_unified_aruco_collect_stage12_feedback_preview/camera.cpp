#include "camera.hpp"
#include <csignal>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <chrono>
extern volatile std::sig_atomic_t g_stop;

CameraCapture::CameraCapture(CameraConfig cfg):cfg_(cfg),cap_(cfg.device,cv::CAP_V4L2){
    if(!cap_.isOpened())throw std::runtime_error("Camera open failed");
    cap_.set(cv::CAP_PROP_FRAME_WIDTH,cfg.width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT,cfg.height);
    cap_.set(cv::CAP_PROP_BUFFERSIZE,1);
    std::cout<<"Camera: "<<cap_.get(cv::CAP_PROP_FRAME_WIDTH)<<"x"<<cap_.get(cv::CAP_PROP_FRAME_HEIGHT)
             <<" FPS="<<cap_.get(cv::CAP_PROP_FPS)<<" (V4L2, buffer=1)\n";
    cv::Mat warm;
    for(int i=0;i<5;i++)if(!cap_.read(warm))throw std::runtime_error("Camera warm-up failed");
}
bool CameraCapture::capture(cv::Mat& frame){
    for(int i=0;i<cfg_.flush_frames && !g_stop;i++){
        if(!cap_.grab())return false;
        if(cfg_.flush_sleep_ms>0)std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.flush_sleep_ms));
    }
    if(g_stop || !cap_.read(frame) || frame.empty())return false;
    if(cfg_.rotate_cw)cv::rotate(frame,frame,cv::ROTATE_90_CLOCKWISE);
    return true;
}
bool CameraCapture::save_image(const std::string& path,const cv::Mat& frame) const {
    if(frame.empty())return false;
    try{return cv::imwrite(path,frame,{cv::IMWRITE_JPEG_QUALITY,cfg_.jpeg_quality});}
    catch(const cv::Exception& e){std::cerr<<"JPEG write failed: "<<e.what()<<"\n";return false;}
}

