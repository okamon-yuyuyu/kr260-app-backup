#include "safety_logger.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
namespace fs=std::filesystem;
static std::vector<std::string> split(const std::string& s){
    std::istringstream f(s);std::string part;std::vector<std::string> a;
    while(std::getline(f,part,','))a.push_back(part);
    if(!s.empty()&&s.back()==',')a.emplace_back();
    return a;
}
int main(){
    auto p=fs::temp_directory_path()/"stage11_emergency_test.csv";
    {
        SafetyLogger writer(p.string());
        EncoderSample sample;sample.valid=true;
        sample.raw={100,200,300,400};sample.raw12={500,600,700,800};
        sample.diff={-100,0,100,200};
        writer.log_abort("20261021_123456_123","ctrl_c_or_sigterm",12,13,50,sample);
    }
    std::ifstream f(p);std::string header,data;
    assert(std::getline(f,header));assert(std::getline(f,data));
    const auto names=split(header),values=split(data);
    if(names.size()!=values.size()){
        std::cerr<<"Emergency CSV column mismatch "<<names.size()<<" vs "<<values.size()<<"\n";
        for(std::size_t i=0;i<values.size();++i)std::cerr<<i<<"="<<values[i]<<"\n";
        return 1;
    }
    auto get=[&](std::string n){for(std::size_t i=0;i<names.size();i++)if(n==names[i])return values[i];return std::string{};};
    assert(get("aruco_detected")=="0");
    assert(get("raw0")=="100");
    assert(get("raw12_3")=="800");
    assert(get("diff0")=="-100");
    fs::remove(p);
    std::cout<<"Stage11 emergency_stop.csv test PASS\n";
}

