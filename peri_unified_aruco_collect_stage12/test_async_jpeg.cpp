#include "async_jpeg.hpp"
#include <csignal>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
volatile std::sig_atomic_t g_stop=0;
namespace fs=std::filesystem;
int main(){
    const auto dir=fs::temp_directory_path()/"kr260_async_jpeg_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    try {
        cv::Mat img(64,64,CV_8UC3,cv::Scalar(20,70,130));
        AsyncJpegWriter saver(90,2);
        std::vector<SaveTicket> tickets;
        for(int i=0;i<5;i++)
            tickets.emplace_back(saver.submit((dir/("frame_"+std::to_string(i)+".jpg")).string(),img));
        for(int i=0;i<5;i++){
            const auto report=tickets[i].result.get();
            if(!report.ok||report.bytes==0)throw std::runtime_error("JPEG test save failed");
        }
        if(saver.failed())throw std::runtime_error("Unexpected saver failure");
        auto bad=saver.submit((dir/"nonexistent"/"bad.jpg").string(),img);
        auto failed=bad.result.get();
        if(failed.ok||!saver.failed())throw std::runtime_error("Failed-write detection broken");
        bool rejected=false;
        try{(void)saver.submit((dir/"not_written.jpg").string(),img);}
        catch(const std::exception&){rejected=true;}
        if(!rejected)throw std::runtime_error("Subsequent submission not blocked after JPEG failure");
    }catch(...){fs::remove_all(dir);throw;}
    fs::remove_all(dir);
    std::cout<<"Async JPEG writer PASS (queue, completion, failure latch)\n";
}
