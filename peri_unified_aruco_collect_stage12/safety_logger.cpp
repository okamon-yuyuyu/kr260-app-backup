#include "safety_logger.hpp"
#include <stdexcept>
SafetyLogger::SafetyLogger(const std::string& filename):out_(filename){
    if(!out_)throw std::runtime_error("Unable to create emergency_stop.csv: "+filename);
    out_<<"timestamp,reason,command_no,last_completed_command_no,active_command,"
            "current_posture_before_reset,image_path,"
            "active_command_done_before_reset,reset_done,"
            "aruco_detected,aruco_id,aruco_x_mm,aruco_y_mm,aruco_z_mm,aruco_angle_deg,"
            "raw0,raw1,raw2,raw3,raw12_0,raw12_1,raw12_2,raw12_3,diff0,diff1,diff2,diff3\n";
}
void SafetyLogger::log_abort(const std::string& time,const std::string& reason,
                             int event,int /*last_step*/,int posture,const EncoderSample& e){
    // We have observed only an accepted step, NOT a verified completed step.
    // last_completed_command_no and completion flags are deliberately EMPTY.
    out_<<time<<','<<reason<<','<<event<<",,unconfirmed_motion,";
    if(posture>=0)out_<<posture;
    out_<<",,,,0,,,,,"; // empty image/completion fields, ArUco NOT sampled on this event
    if(e.valid){
        const auto v=[&](const EncoderValues& p){out_<<','<<p.enc0<<','<<p.enc1<<','<<p.enc2<<','<<p.enc3;};
        v(e.raw);v(e.raw12);v(e.diff);
    }else for(int i=0;i<12;i++)out_<<',';
    out_<<'\n';out_.flush();
}
