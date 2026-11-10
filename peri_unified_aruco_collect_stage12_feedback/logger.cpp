#include "logger.hpp"
#include <filesystem>
#include <iomanip>
#include <stdexcept>
namespace {
void value(std::ostream& out,int n) {if(n>=0)out<<n;}
void encoder_columns(std::ostream& out,const EncoderSample& s){
    if(!s.valid){for(int i=0;i<12;++i)out<<',';return;}
    const auto four=[&out](const EncoderValues& v){out<<','<<v.enc0<<','<<v.enc1<<','<<v.enc2<<','<<v.enc3;};
    four(s.raw);four(s.raw12);four(s.diff);
}
void legacy_encoder_columns(std::ostream& out,const EncoderSample& s){
    // All 12 columns are blank if sample unavailable; never silently synthesize zeros.
    if(!s.valid){for(int i=0;i<12;++i)out<<',';return;}
    auto v=[&out](const EncoderValues& r){out<<','<<r.enc0<<','<<r.enc1<<','<<r.enc2<<','<<r.enc3;};
    v(s.raw);v(s.raw12);v(s.diff);
}
}
Logger::Logger(const std::string& csv_path,double voltage)
    :out_(csv_path),legacy_(std::filesystem::path(csv_path).parent_path()/"legacy_log.csv"),battery_voltage_(voltage){
    if(!out_||!legacy_)throw std::runtime_error("Failed to create log.csv and legacy_log.csv");
    // Preserve ALL Stage10 columns in their original order, append original GitHub data fields.
    out_<<"event_no,mode,battery_voltage_v,event_type,request_seq,step_id,q,step_raw,image_path,aruco_detected,"
           "aruco_id,x_mm,y_mm,z_mm,angle_deg,request_seen_ms,capture_completed_ms,"
           "ack_or_send_ms,step_observed_ms,quality,camera_duration_ms,aruco_duration_ms,queue_wait_ms,jpeg_ms,jpeg_bytes,jpeg_status,"
           "timestamp,command_id,command_name,rom0,rom1,rom2,rom3,stroke_right,stroke_left,stroke_up,stroke_down,"
           "stick1,stick3,raw0,raw1,raw2,raw3,raw12_0,raw12_1,raw12_2,raw12_3,diff0,diff1,diff2,diff3,base0,base1,base2,base3\n";
    // Exact original peri_AXI_collect_safe/logger.cpp header, including column order.
    legacy_<<"index,timestamp,mode,start_battery_voltage_v,event,id,command_name,image_path,"
             "aruco_detected,aruco_id,aruco_x_mm,aruco_y_mm,aruco_z_mm,aruco_angle_deg,"
             "rom0,rom1,rom2,rom3,stroke_right,stroke_left,stroke_up,stroke_down,"
             "stick1,stick3,raw0,raw1,raw2,raw3,raw12_0,raw12_1,raw12_2,raw12_3,diff0,diff1,diff2,diff3\n";
    out_<<std::fixed<<std::setprecision(3);
    legacy_<<std::fixed<<std::setprecision(3);
}
void Logger::write(const LogEvent& e){
    out_<<e.no<<','<<e.mode<<','<<battery_voltage_<<','<<e.kind<<',';
    if(e.request>=0)out_<<e.request;
    out_<<',';
    if(e.step>=0)out_<<e.step;
    out_<<',';
    if(e.q>=0)out_<<e.q;
    out_<<','<<"0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<e.step_raw
        <<std::dec<<std::setfill(' ')<<','<<e.image<<','<<(e.aruco.detected?1:0)<<',';
    if(e.aruco.detected)
        out_<<e.aruco.id<<','<<e.aruco.x_mm<<','<<e.aruco.y_mm<<','
            <<e.aruco.z_mm<<','<<e.aruco.angle_deg;
    else out_<<",,,,";
    out_<<','<<e.request_ms<<','<<e.capture_ms<<','<<e.ack_ms<<','<<e.label_ms
        <<','<<e.quality<<','<<e.camera_duration_ms<<','<<e.aruco_duration_ms<<','
        <<e.queue_wait_ms<<','<<e.jpeg_ms<<','<<e.jpeg_bytes<<','<<e.jpeg_status;
    out_<<','<<e.timestamp<<',';value(out_,e.command_id);out_<<','<<e.command_name<<',';
    value(out_,e.rom0);out_<<',';value(out_,e.rom1);out_<<',';
    value(out_,e.rom2);out_<<',';value(out_,e.rom3);out_<<',';
    value(out_,e.stroke_right);out_<<',';value(out_,e.stroke_left);out_<<',';
    value(out_,e.stroke_up);out_<<',';value(out_,e.stroke_down);out_<<',';
    value(out_,e.stick1);out_<<',';value(out_,e.stick3);
    encoder_columns(out_,e.enc);
    if(e.enc.valid)out_<<','<<e.enc.base.enc0<<','<<e.enc.base.enc1<<','<<e.enc.base.enc2<<','<<e.enc.base.enc3;
    else out_<<",,,,";
    out_<<'\n';out_.flush();

    // Legacy schema contains only actual action/goal records, not new Stage10 status/error/reset events.
    if(e.kind!="ACTION"&&e.kind!="GOAL")return;
    legacy_<<e.no<<','<<e.timestamp<<','<<e.mode<<','<<battery_voltage_<<','
           <<(e.kind=="ACTION"?"action":"goal")<<',';
    value(legacy_,e.command_id);
    legacy_<<','<<e.command_name<<','<<e.image<<','<<(e.aruco.detected?1:0)<<',';
    if(e.aruco.detected) legacy_<<e.aruco.id<<','<<e.aruco.x_mm<<','<<e.aruco.y_mm<<','<<e.aruco.z_mm<<','<<e.aruco.angle_deg;
    else legacy_<<e.aruco.id<<","<<e.aruco.x_mm<<","<<e.aruco.y_mm<<","<<e.aruco.z_mm<<","<<e.aruco.angle_deg; // old logger uses zeros when missing
    legacy_<<',';value(legacy_,e.rom0);legacy_<<',';value(legacy_,e.rom1);
    legacy_<<',';value(legacy_,e.rom2);legacy_<<',';value(legacy_,e.rom3);
    legacy_<<',';value(legacy_,e.stroke_right);legacy_<<',';value(legacy_,e.stroke_left);
    legacy_<<',';value(legacy_,e.stroke_up);legacy_<<',';value(legacy_,e.stroke_down);
    legacy_<<',';value(legacy_,e.stick1);legacy_<<',';value(legacy_,e.stick3);
    legacy_encoder_columns(legacy_,e.enc);
    legacy_<<'\n';legacy_.flush();
}

