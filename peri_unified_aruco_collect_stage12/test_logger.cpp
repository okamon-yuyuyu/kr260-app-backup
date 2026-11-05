#include "logger.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
namespace fs=std::filesystem;
std::vector<std::string> split(const std::string& s){
    std::istringstream in(s);std::string word;std::vector<std::string> result;
    while(std::getline(in,word,','))result.push_back(word);
    if(!s.empty()&&s.back()==',')result.emplace_back();
    return result;
}
std::vector<std::string> line(const fs::path& p){
    std::ifstream f(p);assert(f);
    std::vector<std::string> rows;std::string s;
    while(std::getline(f,s))rows.push_back(s);
    return rows;
}
int main(){
    const auto dir=fs::temp_directory_path()/"stage11_logger_csv_test";
    fs::create_directories(dir);
    {
        Logger logger((dir/"log.csv").string(),11.6);
        LogEvent e;e.no=7;e.mode="PWM";e.kind="ACTION";e.timestamp="20261021_120102_345";
        e.request=5;e.step=51;e.q=74;e.step_raw=0x0180194a;
        e.image="images/frame_00007_PWM_pre_action.jpg";
        e.command_id=100;e.command_name="go";
        e.rom0=0;e.rom1=0;e.rom2=100;e.rom3=100;
        e.stroke_right=2000;e.stroke_left=2000;e.stroke_up=0;e.stroke_down=0;
        e.stick1=50;e.stick3=74;
        e.enc.valid=true;
        e.enc.raw={1000,2000,3000,4000};
        e.enc.raw12={725,3453,2700,267};
        e.enc.diff={-12,24,-36,48};
        e.enc.base={1012,1976,3036,3952};
        e.jpeg_status="SAVED";e.quality="OK";
        logger.write(e);
        LogEvent goal=e;goal.no=8;goal.kind="GOAL";goal.command_id=900;
        goal.command_name="goal_reached";goal.q=-1;goal.aruco.detected=true;
        goal.aruco.id=0;goal.aruco.z_mm=275;goal.aruco.angle_deg=-7;
        goal.stroke_right=0;goal.stroke_left=0;
        logger.write(goal);
        LogEvent reset=e;reset.no=9;reset.kind="RESET";logger.write(reset);
    }
    const auto a=line(dir/"log.csv"),b=line(dir/"legacy_log.csv");
    assert(a.size()==4&&b.size()==3);
    for(const auto& row:a) assert(split(row).size()==split(a[0]).size());
    for(const auto& row:b) assert(split(row).size()==split(b[0]).size());
    auto ah=split(a[0]),av=split(a[1]);
    auto bh=split(b[0]),bv=split(b[1]);
    const auto get=[&](const auto& h,const auto& v,const std::string& k){
        for(std::size_t i=0;i<h.size();++i)if(h[i]==k)return v[i];
        assert(false);return std::string{};
    };
    assert(get(ah,av,"raw0")=="1000");
    assert(get(ah,av,"raw12_1")=="3453");
    assert(get(ah,av,"diff0")=="-12");
    assert(get(ah,av,"diff2")=="-36");
    assert(get(ah,av,"base0")=="1012");
    assert(get(bh,bv,"raw0")=="1000");
    assert(get(bh,bv,"diff2")=="-36");
    assert(get(bh,bv,"rom2")=="100");
    assert(get(bh,bv,"stroke_right")=="2000");
    assert(get(bh,bv,"stick3")=="74");
    assert(get(bh,bv,"start_battery_voltage_v")=="11.600");
    // The legacy header must match the original GitHub logger.cpp EXACTLY.
    const std::string expected="index,timestamp,mode,start_battery_voltage_v,event,id,command_name,image_path,"
        "aruco_detected,aruco_id,aruco_x_mm,aruco_y_mm,aruco_z_mm,aruco_angle_deg,"
        "rom0,rom1,rom2,rom3,stroke_right,stroke_left,stroke_up,stroke_down,"
        "stick1,stick3,raw0,raw1,raw2,raw3,raw12_0,raw12_1,raw12_2,raw12_3,diff0,diff1,diff2,diff3";
    assert(b[0]==expected);
    fs::remove_all(dir);
    std::cout<<"Stage11 log.csv + legacy_log.csv schema and values PASS\n";
}
