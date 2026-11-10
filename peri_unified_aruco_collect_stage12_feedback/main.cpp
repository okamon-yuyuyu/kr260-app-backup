// Unified KR260 7-bit AXI / PWM gate camera + ArUco data collector.
// WARNING: This application DOES NOT provide an emergency motor stop.
#include "aruco_detector.hpp"
#include "async_jpeg.hpp"
#include "camera.hpp"
#include "encoder.hpp"
#include "safety_logger.hpp"
#include "command.hpp"
#include "logger.hpp"
#include "protocol.hpp"
#include "wire_adjuster.hpp"
#include "axi_launch.hpp"
#include "feedback_tcp.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <chrono>
#include <deque>
#include <future>
#include <utility>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
volatile std::sig_atomic_t g_stop=0;
namespace {
void on_signal(int){g_stop=1;}
struct Config {
    std::string name="P1_trial01",mode,axi_control,commands_path;
    fs::path output="sessions";
    CameraConfig camera;
    int settle_ms=50,poll_ms=2,rom=100,stroke=2000,step_timeout_ms=2000,done_timeout_ms=10000;
    int feedback_port=5001;
    int jpeg_queue=3;
    std::array<std::uint16_t,4> wire_target{{725,3453,2400,267}};
    int deadband_manual=5,deadband_auto=20;
    double battery_voltage_v=0.0;
    std::string yaml="/home/ubuntu/yaml/camera_many.yaml";
    double marker_m=0.025,goal_z_mm=300.0,goal_angle_deg=15.0;
};
int number(const std::string& s,int lo,int hi,const std::string& what){
    std::size_t end=0;int result=std::stoi(s,&end);
    if(end!=s.size()||result<lo||result>hi)throw std::runtime_error("Invalid "+what+": "+s);
    return result;
}
double real(const std::string& s,double lo,double hi,const std::string& what){
    std::size_t end=0;double result=std::stod(s,&end);
    if(end!=s.size()||!(result>=lo&&result<=hi))throw std::runtime_error("Invalid "+what+": "+s);
    return result;
}
Config parse(int argc,char** argv){
    Config cfg;
    for(int i=1;i<argc;i++){
        const std::string k=argv[i];
        if(k=="-h"||k=="--help"){
            std::cout<<"Usage: sudo ./peri_collect --name P1_trial01 [--mode axi|pwm|feedback] [--camera 0] "
                     "[--settle-ms 50] [--flush-frames 2] [--flush-sleep-ms 0] "
                     "[--yaml /home/ubuntu/yaml/camera_many.yaml] [--marker-mm 25] "
                     "[--goal-z-mm 300] [--goal-angle-deg 15] [--rom 100] [--stroke 2000] "
                     "[--jpeg-queue 3] [--axi-control manual|auto] [--commands commands.txt (AXI auto)] "
                     "[--feedback-port 5001 (feedback mode)]\n";
            std::exit(0);
        }
        if(i+1>=argc)throw std::runtime_error("Missing value for "+k);
        std::string v=argv[++i];
        if(k=="--name")cfg.name=v;
        else if(k=="--mode")cfg.mode=v;
        else if(k=="--output")cfg.output=v;
        else if(k=="--camera")cfg.camera.device=number(v,0,32,k);
        else if(k=="--width")cfg.camera.width=number(v,160,4096,k);
        else if(k=="--height")cfg.camera.height=number(v,120,4096,k);
        else if(k=="--rotate"){
            if(v!="cw"&&v!="none")throw std::runtime_error("--rotate: cw or none");
            cfg.camera.rotate_cw=(v=="cw");
        }
        else if(k=="--jpeg-quality")cfg.camera.jpeg_quality=number(v,1,100,k);
        else if(k=="--jpeg-queue")cfg.jpeg_queue=number(v,1,16,k);
        else if(k=="--settle-ms")cfg.settle_ms=number(v,0,5000,k);
        else if(k=="--flush-frames")cfg.camera.flush_frames=number(v,0,20,k);
        else if(k=="--flush-sleep-ms")cfg.camera.flush_sleep_ms=number(v,0,100,k);
        else if(k=="--poll-ms")cfg.poll_ms=number(v,1,100,k);
        else if(k=="--step-timeout-ms")cfg.step_timeout_ms=number(v,100,30000,k);
        else if(k=="--done-timeout-ms")cfg.done_timeout_ms=number(v,100,60000,k);
        else if(k=="--rom")cfg.rom=number(v,1,3000,k);
        else if(k=="--stroke")cfg.stroke=number(v,1,10000,k);
        else if(k=="--yaml")cfg.yaml=v;
        else if(k=="--marker-mm")cfg.marker_m=real(v,1.0,500.0,k)/1000.0;
        else if(k=="--goal-z-mm")cfg.goal_z_mm=real(v,1.0,10000.0,k);
        else if(k=="--goal-angle-deg")cfg.goal_angle_deg=real(v,0.0,180.0,k);
        else if(k=="--commands")cfg.commands_path=v;
        else if(k=="--axi-control")cfg.axi_control=v;
        else if(k=="--feedback-port")cfg.feedback_port=number(v,1,65535,k);
        else throw std::runtime_error("Unknown option "+k);
    }
    if(cfg.name.empty()||cfg.name=="."||cfg.name=="..")throw std::runtime_error("Empty/unsafe --name");
    for(char ch:cfg.name){if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='-'||ch=='_'))
        throw std::runtime_error("--name: A-Z a-z 0-9 _ - only");}
    if(!cfg.mode.empty()&&cfg.mode!="axi"&&cfg.mode!="pwm"&&cfg.mode!="feedback")
        throw std::runtime_error("--mode must be axi, pwm or feedback");
    if(!cfg.axi_control.empty()&&cfg.axi_control!="manual"&&cfg.axi_control!="auto")
        throw std::runtime_error("--axi-control must be manual or auto");
    if(cfg.mode=="pwm"&&(!cfg.commands_path.empty()||!cfg.axi_control.empty()))
        throw std::runtime_error("--commands / --axi-control are only for AXI mode");
    if(cfg.mode=="feedback"&&(!cfg.commands_path.empty()||!cfg.axi_control.empty()))
        throw std::runtime_error("--commands / --axi-control are not used in feedback mode");
    return cfg;
}
std::string ask(const std::string& prompt){std::cout<<prompt<<std::flush;std::string v;std::getline(std::cin,v);return v;}
// Match the familiar y/n parameter screen; a blank field keeps its value.
bool ask_yes_no(const std::string& question){
    while(true){
        const std::string s=ask(question+" (y/n): ");
        if(s=="y"||s=="Y")return true;
        if(s=="n"||s=="N")return false;
        if(!std::cin)throw std::runtime_error("Input closed");
        std::cout<<"Please enter y or n.\n";
    }
}
int ask_integer(const std::string& label,int current,int lo,int hi){
    while(true){
        const std::string s=ask(label+" ["+std::to_string(current)+"]: ");
        if(s.empty())return current;
        try{return number(s,lo,hi,label);}catch(const std::exception&){
            std::cout<<"Enter a number from "<<lo<<" to "<<hi<<", or Enter to keep.\n";
        }
        if(!std::cin)throw std::runtime_error("Input closed");
    }
}
double ask_voltage(){
    while(true){
        const auto s=ask("Battery voltage at start [V]: ");
        try{return real(s,0.001,100.0,"battery voltage");}
        catch(const std::exception&){std::cout<<"Enter positive voltage (e.g. 12.3).\n";}
        if(!std::cin)throw std::runtime_error("Input closed");
    }
}
void parameter_screen(Config& cfg){
    std::cout<<"\nCurrent parameters: target_raw="
             <<cfg.wire_target[0]<<","<<cfg.wire_target[1]<<","<<cfg.wire_target[2]<<","<<cfg.wire_target[3]
             <<"; deadband="<<cfg.deadband_manual<<","<<cfg.deadband_auto
             <<"; ROM2/3="<<cfg.rom<<"; AXI stroke="<<cfg.stroke
             <<"; settle="<<cfg.settle_ms<<"ms\n";
    if(ask_yes_no("Change parameters?")){
        for(int i=0;i<4;i++)cfg.wire_target[i]=static_cast<std::uint16_t>(
            ask_integer("target_raw"+std::to_string(i),cfg.wire_target[i],1,4095));
        cfg.deadband_manual=ask_integer("deadband manual",cfg.deadband_manual,0,63);
        cfg.deadband_auto=ask_integer("deadband auto",cfg.deadband_auto,0,63);
        cfg.rom=ask_integer("ROM2/3",cfg.rom,1,3000);
        cfg.stroke=ask_integer("AXI stroke",cfg.stroke,1,10000);
        cfg.settle_ms=ask_integer("camera settle ms",cfg.settle_ms,0,5000);
    }
    cfg.battery_voltage_v=ask_voltage();
    std::cout<<"Battery voltage = "<<cfg.battery_voltage_v<<" V\n";
}
bool confirm(const std::string& prompt,const std::string& word){return ask(prompt+"\nType "+word+" to continue: ")==word;}
std::string now_stamp(){
    const std::time_t tt=std::time(nullptr);std::tm tm{};localtime_r(&tt,&tm);
    std::ostringstream o;o<<std::put_time(&tm,"%Y%m%d_%H%M%S");return o.str();
}
std::string wall_timestamp(){
    const auto now=std::chrono::system_clock::now();
    const std::time_t tt=std::chrono::system_clock::to_time_t(now);
    std::tm tm{};localtime_r(&tt,&tm);
    const auto milli=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())%1000;
    std::ostringstream o;o<<std::put_time(&tm,"%Y%m%d_%H%M%S")
                           <<'_'<<std::setw(3)<<std::setfill('0')<<milli.count();
    return o.str();
}
void init_action_fields(LogEvent& e, const Config& cfg, Hardware& hw, bool motion){
    // Keep the real ROM register values (REG0 and REG1) rather than silently writing guessed defaults.
    const auto rom01=hw.rom01(), rom23=hw.rom23();
    e.rom0=static_cast<int>(rom01&0xffffu);
    e.rom1=static_cast<int>((rom01>>16)&0xffffu);
    e.rom2=static_cast<int>(rom23&0xffffu);
    e.rom3=static_cast<int>((rom23>>16)&0xffffu);
    e.command_id=motion?100:900;
    e.command_name=motion?"go":"goal_reached";
    e.stick1=50; // unused horizontal movement axis, held at neutral in unified PL
    e.stroke_right=motion?(e.mode=="PWM"?2000:cfg.stroke):0;
    e.stroke_left=e.stroke_right;
    e.stroke_up=0;e.stroke_down=0;
}
void sleep_ms(int ms){
    for(int remaining=ms;remaining>0&&!g_stop;remaining-=10)
        std::this_thread::sleep_for(Milliseconds(std::min(remaining,10)));
}
double time_ms(Moment start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
std::string image_name(int n,const std::string& mode,const std::string& what){
    std::ostringstream o;
    o<<"images/frame_"<<std::setw(5)<<std::setfill('0')<<n<<"_"<<mode<<"_"<<what<<".jpg";
    return o.str();
}
std::vector<int> load_commands(const std::string& path){
    std::ifstream f(path);if(!f)throw std::runtime_error("Cannot read command file: "+path);
    std::vector<int> seq;std::string s;
    while(std::getline(f,s)){
        const auto comment=s.find('#');if(comment!=std::string::npos)s.erase(comment);
        std::istringstream in(s);std::string tok,extra;
        if(!(in>>tok))continue;
        if(tok=="q"){
            if(!(in>>tok))throw std::runtime_error("Missing q in command file");
        }
        const int q=number(tok,0,100,"command file q");
        if(in>>extra)throw std::runtime_error("Extra token in command file: "+extra);
        seq.push_back(q);
    }
    if(seq.empty())throw std::runtime_error("Command file is empty");
    return seq;
}
void print_aruco(const ArucoResult& r,const ArucoDetector& a){
    if(!r.detected){std::cout<<"ArUco: ID=0 not detected\n";return;}
    std::cout<<"ArUco: ID="<<r.id<<" X="<<r.x_mm<<" Y="<<r.y_mm
             <<" Z="<<r.z_mm<<"mm angle="<<r.angle_deg
             <<"deg GOAL="<<(a.is_goal(r)?"YES":"NO")<<"\n";
}
// After an ACK / AXI start, wait for a different REG5 step id.
// Capture gate ensures only one PWM step may be issued per request.
bool step_after(Hardware& hw,protocol::Step before,int timeout_ms,int poll_ms,
                protocol::Step& found){
    const auto deadline=Clock::now()+Milliseconds(timeout_ms);
    while(!g_stop && Clock::now()<deadline){
        const auto s=hw.step();
        if(s.valid && (!before.valid || s.id!=before.id)){found=s;return true;}
        std::this_thread::sleep_for(Milliseconds(poll_ms));
    }
    return false;
}
void annotate_label(LogEvent& e,const protocol::Step& step,const protocol::Step& prev,bool pwm){
    e.step=step.id;e.q=static_cast<int>(step.q);e.step_raw=step.raw;
    if((step.pwm!=pwm))e.quality="SOURCE_MISMATCH";
    if(step.q>100)e.quality="Q_OUT_OF_RANGE";
    if(prev.valid && protocol::seq_delta(prev.id,step.id)!=1)e.quality="STEP_GAP";
}
// A log event is finalized only after the JPEG worker confirms its result.
// This avoids putting quality=OK into log.csv for a file that has not yet been saved.
struct PendingRecord {
    LogEvent event;
    std::future<SaveReport> jpeg;
};
using PendingLogs=std::deque<PendingRecord>;
bool flush_logs(Logger& logger,PendingLogs& pending,bool all){
    bool good=true;
    while(!pending.empty()){
        auto& first=pending.front();
        if(first.jpeg.valid()){
            if(!all && first.jpeg.wait_for(Milliseconds(0))!=std::future_status::ready)break;
            SaveReport report=first.jpeg.get();
            first.event.jpeg_ms=report.jpeg_ms;
            first.event.jpeg_bytes=report.bytes;
            first.event.jpeg_status=report.ok ? "SAVED" : "FAILED";
            if(!report.ok){
                good=false;
                first.event.quality+=(first.event.quality=="OK" ? "+JPEG_SAVE_FAILED" : "+JPEG_SAVE_FAILED");
                std::cerr<<"JPEG SAVE FAILED for "<<first.event.image<<": "<<report.error<<"\n";
            }
        }
        logger.write(first.event);
        pending.pop_front();
    }
    return good;
}
void record_event(Logger& logger,PendingLogs& pending,LogEvent e,
                  std::future<SaveReport>& saved){
    pending.push_back({std::move(e),std::move(saved)});
    (void)flush_logs(logger,pending,false);
}
// PL request is held until capture, ArUco GOAL check and successful queue submission.
// JPEG compression/write runs in a worker concurrently with next motion.
LogEvent get_frame(int event,const std::string& mode,CameraCapture& cam,
                   EncoderReader& encoder,
                   const ArucoDetector& detector,const fs::path& out,
                   AsyncJpegWriter& writer,std::future<SaveReport>& saved,
                   Moment start,LogEvent e){
    if(g_stop)throw std::runtime_error("Interrupted before capture");
    cv::Mat img;
    const auto t_cam=Clock::now();
    if(!cam.capture(img))throw std::runtime_error("Camera capture failed: no motion permission sent");
    e.camera_duration_ms=time_ms(t_cam);
    e.capture_ms=time_ms(start);
    // Sample the exact 16-bit raw, 12-bit raw and zero-referenced delta
    // from the same pre-action observation. Never sample after the motion ACK.
    e.timestamp=wall_timestamp();
    e.enc=encoder.sample();
    const auto t_aruco=Clock::now();
    e.aruco=detector.detect(img);
    e.aruco_duration_ms=time_ms(t_aruco);
    e.image=image_name(event,mode,"pre_action");
    // Snapshot is deep-copied by submit(), and the queue has a strict capacity.
    // The main thread may ACK as soon as this returns (not after JPEG disk write).
    auto ticket=writer.submit((out/e.image).string(),img);
    saved=std::move(ticket.result);
    e.queue_wait_ms=ticket.queue_wait_ms;
    e.jpeg_status="PENDING";
    print_aruco(e.aruco,detector);
    return e;
}

struct RunResult {bool ok=true;bool goal=false;int events=0;};
RunResult run_pwm(const Config& cfg,Hardware& hw,CameraCapture& camera,
             EncoderReader& encoder,
             const ArucoDetector& aruco,Logger& logger,const fs::path& out,Moment start,
             AsyncJpegWriter& writer,PendingLogs& pending,std::ofstream& command_log){
    std::cout<<"PWM: R-stick must be neutral at start; physical motor cutoff available.\n";
    if(g_stop)return {false,false,0};
    const auto base_req=hw.request();const auto base_step=hw.step();
    hw.arm_pwm();
    std::cout<<"PWM ARMED. Hold R-stick UP for repeated capture -> ArUco -> ACK -> q/forward.\n"
             <<"On GOAL, NO ACK will be sent; release R-stick to neutral.\n";
    int event=0;auto last_request=base_req.seq;auto last_step=base_step;
    auto heartbeat=Clock::now();
    RunResult result;
    while(!g_stop){
        if(!flush_logs(logger,pending,false)||writer.failed()){result.ok=false;std::cerr<<"JPEG write failure: NO MORE ACK.\n";break;}
        if(!(hw.command()&protocol::MODE_PWM)||!(hw.camera_ctrl()&protocol::CAMERA_ENABLE)){
            std::cerr<<"Unexpected mode/gate change, no ACK.\n";result.ok=false;break;
        }
        const auto req=hw.request();
        if(!req.waiting||req.seq==last_request){
            if(Clock::now()-heartbeat>std::chrono::seconds(10)){
                std::cout<<"WAIT REG7 waiting="<<req.waiting<<" seq="<<req.seq<<"\n";
                heartbeat=Clock::now();
            }
            std::this_thread::sleep_for(Milliseconds(cfg.poll_ms));continue;
        }
        heartbeat=Clock::now();last_request=req.seq;
        std::future<SaveReport> image_save;
        LogEvent e;e.no=++event;result.events=event;e.mode="PWM";e.kind="ACTION";e.request=req.seq;e.request_ms=time_ms(start);
        sleep_ms(cfg.settle_ms);
        if(g_stop)break;
        auto check=hw.request();
        if(!check.waiting||check.seq!=req.seq){
            e.kind="SKIPPED";e.quality="REQUEST_CANCELED_BEFORE_CAPTURE";record_event(logger,pending,e,image_save);continue;
        }
        try{e=get_frame(event,"PWM",camera,encoder,aruco,out,writer,image_save,start,e);}
        catch(const std::exception& ex){
            e.kind="ERROR";e.quality="CAMERA_OR_JPEG_FAILURE_NO_ACK";record_event(logger,pending,e,image_save);
            std::cerr<<ex.what()<<"\n";result.ok=false;break;
        }
        init_action_fields(e,cfg,hw,true);
        if(aruco.is_goal(e.aruco)){
            e.kind="GOAL";e.quality="GOAL_NO_ACK";
            init_action_fields(e,cfg,hw,false);
            if(last_step.valid)e.stick3=static_cast<int>(last_step.q);
            record_event(logger,pending,e,image_save);
            std::cout<<"GOAL: Z <= "<<cfg.goal_z_mm<<"mm and abs(angle) <= "<<cfg.goal_angle_deg
                     <<"deg. No ACK; next movement is blocked while gate stays enabled.\n";
            result.goal=true;
            break;
        }
        if(g_stop){e.kind="SKIPPED";e.quality="INTERRUPTED_BEFORE_ACK";record_event(logger,pending,e,image_save);break;}
        check=hw.request();
        if(!check.waiting||check.seq!=req.seq){
            e.kind="SKIPPED";e.quality="REQUEST_CANCELED_BEFORE_ACK";record_event(logger,pending,e,image_save);continue;
        }
        if(!(hw.command()&protocol::MODE_PWM)||!(hw.camera_ctrl()&protocol::CAMERA_ENABLE)){
            e.kind="ERROR";e.quality="MODE_CHANGED_BEFORE_ACK";record_event(logger,pending,e,image_save);result.ok=false;break;
        }
        // Only pre-ACK essentials must finish: capture, ArUco, queue submission.
        // Disk-write errors discovered asynchronously stop *subsequent* motion.
        if(!flush_logs(logger,pending,false)||writer.failed()){
            e.kind="ERROR";e.quality="JPEG_FAILURE_BEFORE_ACK";
            record_event(logger,pending,e,image_save);result.ok=false;break;
        }
        // JPEG write is pending in worker; ensure we never ACK on a failed queue.
        hw.ack(req.seq);e.ack_ms=time_ms(start);
        protocol::Step s;
        if(step_after(hw,last_step,cfg.step_timeout_ms,cfg.poll_ms,s)){
            annotate_label(e,s,last_step,true);e.stick3=static_cast<int>(s.q);last_step=s;
            e.label_ms=time_ms(start);
        }else{
            e.quality=g_stop?"INTERRUPTED_AFTER_ACK":"NO_STEP_AFTER_ACK";
            result.ok=false;
        }
        record_event(logger,pending,e,image_save);
        if(e.quality=="OK" && e.q>=0 && e.q<=100){
            command_log<<e.q<<'\n';command_log.flush();
        }
        std::cout<<"PWM: req="<<req.seq<<" step="<<e.step<<" q="<<e.q<<" "<<e.quality<<"\n";
        if(!result.ok)break;
    }
    return result;
}

RunResult run_axi(const Config& cfg,Hardware& hw,CameraCapture& camera,
             EncoderReader& encoder,
             const ArucoDetector& aruco,Logger& logger,const fs::path& out,Moment start,
             AsyncJpegWriter& writer,PendingLogs& pending,std::ofstream& command_log){
    std::cout<<"AXI: R-stick must remain NEUTRAL; otherwise legacy straight test may run.\n";
    if(g_stop)return {false,false,0};
    const bool replay = cfg.axi_control == "auto";
    const std::vector<int> cmds=replay ? load_commands(cfg.commands_path) : std::vector<int>{};
    if(replay){
        std::cout<<"AXI AUTO: "<<cmds.size()<<" commands from "<<cfg.commands_path<<"\n";
        // Keep the legacy research application's explicit pre-replay confirmation.
        if(!ask_yes_no("Robot at safe initial posture, RIGHT stick NEUTRAL, and motor power cutoff ready. Start AXI AUTO replay?")){
            std::cout<<"AXI auto replay cancelled BEFORE any command.\n";
            return {true,false,0};
        }
    }else{
        std::cout<<"AXI MANUAL: enter q 0..100, g, b or x for each step.\n";
    }
    if(g_stop)return {false,false,0};
    hw.arm_axi();
    int seqpos=0,last_q=50,event=0;
    RunResult result;
    auto prev=hw.step();
    while(!g_stop){
        if(!flush_logs(logger,pending,false)||writer.failed()){result.ok=false;std::cerr<<"JPEG write failure: no more AXI commands.\n";break;}
        // AXI command is not sent until image capture and ArUco decision succeed.
        sleep_ms(cfg.settle_ms);
        if(g_stop)break;
        std::future<SaveReport> image_save;
        LogEvent e;e.no=++event;result.events=event;e.mode="AXI";e.kind="ACTION";e.request_ms=time_ms(start);
        try{e=get_frame(event,"AXI",camera,encoder,aruco,out,writer,image_save,start,e);}
        catch(const std::exception& ex){
            e.kind="ERROR";e.quality="CAMERA_OR_JPEG_FAILURE";record_event(logger,pending,e,image_save);
            std::cerr<<ex.what()<<"\n";return {false,false,event};
        }
        init_action_fields(e,cfg,hw,true);
        if(aruco.is_goal(e.aruco)){
            e.kind="GOAL";e.quality="GOAL_NO_COMMAND";
            init_action_fields(e,cfg,hw,false);
            if(prev.valid)e.stick3=static_cast<int>(prev.q);
            record_event(logger,pending,e,image_save);
            std::cout<<"GOAL: No AXI forward command sent.\n";result.goal=true;break;
        }
        int q=-1;
        if(replay){
            if(seqpos>=static_cast<int>(cmds.size())){
                e.kind="END";e.quality="COMMANDS_EXHAUSTED";record_event(logger,pending,e,image_save);
                std::cout<<"End of command file. No more commands.\n";break;
            }
            q=cmds[seqpos++];
            std::cout<<"Replay "<<seqpos<<"/"<<cmds.size()<<": q="<<q<<"\n";
        }else{
            const std::string answer=ask("AXI next (q 0..100 | g=repeat | b=encoder base | x=end) > ");
            if(g_stop||answer=="x"){
                e.kind="END";e.quality="USER_END_NO_COMMAND";record_event(logger,pending,e,image_save);break;
            }
            if(answer=="b"){
                encoder.set_base();
                const auto b=encoder.base();
                e.kind="BASE";e.quality="BASE_UPDATED_NO_COMMAND";
                // The captured frame precedes the BASE update; note this in log.csv.
                record_event(logger,pending,e,image_save);
                std::cout<<"Encoder base updated to: "<<b.enc0<<","<<b.enc1<<","<<b.enc2<<","<<b.enc3<<"\n";
                continue;
            }
            if(answer=="g")q=last_q;
            else {
                std::istringstream in(answer);std::string token,other;
                if(!(in>>token)){
                    e.kind="SKIPPED";e.quality="EMPTY_INPUT";record_event(logger,pending,e,image_save);continue;
                }
                if(token=="q"){
                    if(!(in>>token)){
                        e.kind="SKIPPED";e.quality="EMPTY_Q";record_event(logger,pending,e,image_save);continue;
                    }
                }
                try{q=number(token,0,100,"q");}
                catch(const std::exception&){
                    e.kind="SKIPPED";e.quality="BAD_Q";record_event(logger,pending,e,image_save);std::cout<<"Enter q 0..100, g or x\n";continue;
                }
                if(in>>other){
                    e.kind="SKIPPED";e.quality="EXTRA_INPUT";record_event(logger,pending,e,image_save);continue;
                }
            }
        }
        if(g_stop){e.kind="END";e.quality="INTERRUPTED_BEFORE_START";record_event(logger,pending,e,image_save);break;}
        if(!flush_logs(logger,pending,false)||writer.failed()){
            e.kind="ERROR";e.quality="JPEG_FAILURE_BEFORE_AXI_SEND";
            record_event(logger,pending,e,image_save);result.ok=false;break;
        }
        e.q=q;e.stick3=q;last_q=q;
        hw.axi_send(q,static_cast<std::uint16_t>(cfg.stroke));e.ack_ms=time_ms(start);
        protocol::Step s;
        if(step_after(hw,prev,cfg.step_timeout_ms,cfg.poll_ms,s)){
            annotate_label(e,s,prev,false);e.stick3=static_cast<int>(s.q);prev=s;e.label_ms=time_ms(start);
            if(e.q!=q)e.quality="LABEL_Q_MISMATCH";
        }else e.quality="NO_STEP_AFTER_AXI_START";
        const bool finished=hw.axi_wait_done(cfg.done_timeout_ms);
        if(!finished && !g_stop)e.quality="DONE_TIMEOUT_UNCONFIRMED";
        record_event(logger,pending,e,image_save);
        if(e.quality=="OK" && e.q>=0 && e.q<=100){
            command_log<<e.q<<'\n';command_log.flush();
        }
        std::cout<<"AXI: step="<<e.step<<" q="<<e.q<<" "<<e.quality
                 <<" done="<<finished<<"\n";
        if(e.quality!="OK"||g_stop){
            std::cerr<<"Stopping AXI sequence on abnormal event; do not assume the motors are stopped.\n";
            return {false,false,event};
        }
    }
    return result;
}

// PC owns the overhead screenshot measurement and command selection. The PS
// owns one AXI posture+crawl command, pre-action UVC capture/logging, and PL
// completion polling. PC is allowed to send the next MOVE only after DONE.
RunResult run_feedback(const Config& cfg,Hardware& hw,CameraCapture& camera,
             EncoderReader& encoder,
             const ArucoDetector& aruco,Logger& logger,const fs::path& out,Moment start,
             AsyncJpegWriter& writer,PendingLogs& pending,std::ofstream& command_log){
    RunResult result;
    if(g_stop)return {false,false,0};
    if(!confirm("ONLINE FEEDBACK: RIGHT stick NEUTRAL, robot at the stated q, and physical motor cutoff ready.","READY")){
        std::cout<<"Feedback mode cancelled before opening TCP or moving.\n";
        return result;
    }

    std::ofstream feedback_log(out/"feedback_commands.csv");
    if(!feedback_log)throw std::runtime_error("Cannot create feedback_commands.csv");
    feedback_log<<"sequence,q_current,q_requested,q_observed,step_id,pl_done_signal,status,command_ms,timestamp\n";
    feedback_log<<std::fixed<<std::setprecision(3);

    FeedbackTcpServer server(cfg.feedback_port);
    server.listen();
    std::cout<<"Feedback TCP server listening on 0.0.0.0:"<<cfg.feedback_port
             <<". Connect the PC client after it is armed.\n";
    while(!g_stop&&!server.accept_client(250)){}
    if(g_stop)return {false,false,0};
    std::cout<<"Feedback PC connected. Waiting for HELLO 1 <initial_q>.\n";
    auto respond=[&](const std::string& line){
        try{server.send_line(line);return true;}
        catch(const std::exception& ex){
            std::cerr<<"Feedback TCP reply failed: "<<ex.what()<<"\n";
            result.ok=false;return false;
        }
    };

    auto read_message=[&]() -> std::optional<std::string> {
        while(!g_stop&&server.connected()){
            auto line=server.read_line(250);
            if(line)return line;
        }
        return std::nullopt;
    };
    auto hello=read_message();
    if(!hello||g_stop)return {false,false,0};
    std::istringstream hello_in(*hello);
    std::string verb,extra;
    int version=0,current_q=-1;
    if(!(hello_in>>verb>>version>>current_q)||(hello_in>>extra)||verb!="HELLO"||version!=1||current_q<0||current_q>100){
        (void)respond("ERR 0 BAD_HELLO");
        return {false,false,0};
    }
    if(!respond("READY 1 "+std::to_string(current_q)))return {false,false,0};
    std::cout<<"Feedback session armed: initial q="<<current_q<<"\n";

    int event=0,last_sequence=0;
    auto previous_step=hw.step();
    while(!g_stop){
        if(!flush_logs(logger,pending,false)||writer.failed()){
            std::cerr<<"JPEG write failure: no further feedback commands.\n";
            result.ok=false;break;
        }
        auto message=read_message();
        if(!message){
            if(g_stop)break;
            if(!server.connected()){
                std::cerr<<"Feedback PC disconnected. No further AXI command will be accepted.\n";
                result.ok=false;break;
            }
            continue;
        }
        std::istringstream in(*message);
        std::string type,tail;
        in>>type;
        if(type=="STOP"){
            if(in>>tail){(void)respond("ERR 0 BAD_STOP");result.ok=false;break;}
            (void)respond("BYE");
            std::cout<<"PC requested feedback session stop. No further command will be issued.\n";
            break;
        }
        if(type=="GOAL"){
            int seq=-1,q=-1;
            if(!(in>>seq>>q)||(in>>tail)||seq!=last_sequence||q!=current_q){
                (void)respond("ERR 0 GOAL_STATE_MISMATCH");result.ok=false;break;
            }
            std::future<SaveReport> image_save;
            LogEvent e;e.no=++event;result.events=event;e.mode="AXI";e.kind="GOAL";
            e.request=seq;e.request_ms=time_ms(start);e.q=current_q;e.stick3=current_q;
            try{
                sleep_ms(cfg.settle_ms);
                e=get_frame(event,"AXI_FB",camera,encoder,aruco,out,writer,image_save,start,e);
            }catch(const std::exception& ex){
                e.kind="ERROR";e.quality="GOAL_CAPTURE_FAILED";
                record_event(logger,pending,e,image_save);
                std::cerr<<"PC GOAL capture failed: "<<ex.what()<<"\n";
                (void)respond("ERR "+std::to_string(seq)+" GOAL_CAPTURE_FAILED");
                result.ok=false;break;
            }
            init_action_fields(e,cfg,hw,false);
            e.kind="GOAL";e.command_id=900;e.command_name="pc_goal_line";
            e.q=current_q;e.stick3=current_q;e.quality="PC_GOAL_LINE";
            record_event(logger,pending,e,image_save);
            feedback_log<<seq<<','<<current_q<<",,,"<<(previous_step.valid?std::to_string(previous_step.id):"")
                        <<",,PC_GOAL_LINE,0,"<<e.timestamp<<'\n';feedback_log.flush();
            (void)respond("GOAL_ACK "+std::to_string(seq)+" "+std::to_string(current_q));
            std::cout<<"PC reported GOAL_LINE; no movement command sent.\n";
            result.goal=true;result.events=event;break;
        }
        if(type!="MOVE"){
            (void)respond("ERR 0 EXPECTED_MOVE_OR_GOAL_OR_STOP");result.ok=false;break;
        }

        int seq=-1,q_command_current=-1,q_next=-1;
        if(!(in>>seq>>q_command_current>>q_next)||(in>>tail)||
           seq!=last_sequence+1||q_command_current!=current_q||q_next<30||q_next>70){
            (void)respond("ERR 0 INVALID_SEQUENCE_OR_Q");result.ok=false;break;
        }
        const auto command_started=Clock::now();
        std::future<SaveReport> image_save;
        LogEvent e;e.no=++event;result.events=event;e.mode="AXI";e.kind="ACTION";
        e.request=seq;e.request_ms=time_ms(start);e.q=q_next;e.stick3=q_next;
        try{
            sleep_ms(cfg.settle_ms);
            e=get_frame(event,"AXI_FB",camera,encoder,aruco,out,writer,image_save,start,e);
        }catch(const std::exception& ex){
            e.kind="ERROR";e.quality="CAMERA_OR_JPEG_FAILURE_NO_AXI";
            record_event(logger,pending,e,image_save);
            std::cerr<<"Pre-action capture failed: "<<ex.what()<<"\n";
            (void)respond("ERR "+std::to_string(seq)+" PREACTION_CAPTURE_FAILED");
            result.ok=false;break;
        }
        init_action_fields(e,cfg,hw,true);
        e.command_id=200;e.command_name="pc_online_feedback";e.request=seq;e.q=q_next;e.stick3=q_next;
        if(aruco.is_goal(e.aruco)){
            e.kind="GOAL";e.quality="GOAL_NO_AXI_COMMAND";e.command_id=900;
            e.command_name="aruco_goal_before_feedback_move";e.q=current_q;e.stick3=current_q;
            init_action_fields(e,cfg,hw,false);e.command_id=900;
            e.command_name="aruco_goal_before_feedback_move";e.q=current_q;e.stick3=current_q;
            record_event(logger,pending,e,image_save);
            feedback_log<<seq<<','<<current_q<<','<<q_next<<",,"<<(previous_step.valid?std::to_string(previous_step.id):"")
                        <<",,ARUCO_GOAL_NO_COMMAND,0,"<<e.timestamp<<'\n';feedback_log.flush();
            (void)respond("GOAL "+std::to_string(seq)+" "+std::to_string(current_q));
            result.goal=true;result.events=event;break;
        }
        if(!flush_logs(logger,pending,false)||writer.failed()){
            e.kind="ERROR";e.quality="JPEG_FAILURE_BEFORE_AXI_SEND";
            record_event(logger,pending,e,image_save);
            (void)respond("ERR "+std::to_string(seq)+" JPEG_FAILURE_BEFORE_AXI");
            result.ok=false;break;
        }

        bool step_seen=false,done_signal=false;
        protocol::Step observed;
        try{
            hw.axi_send(q_next,static_cast<std::uint16_t>(cfg.stroke));
            e.ack_ms=time_ms(start);
            step_seen=step_after(hw,previous_step,cfg.step_timeout_ms,cfg.poll_ms,observed);
            if(step_seen){
                annotate_label(e,observed,previous_step,false);
                e.label_ms=time_ms(start);
                if(observed.q!=static_cast<unsigned>(q_next))e.quality="LABEL_Q_MISMATCH";
                previous_step=observed;
            }else e.quality="NO_STEP_AFTER_AXI_START";
            done_signal=hw.axi_wait_done(cfg.done_timeout_ms);
            if(!done_signal)e.quality="DONE_TIMEOUT_UNCONFIRMED";
        }catch(const std::exception& ex){
            e.quality="AXI_SEND_OR_WAIT_ERROR";
            std::cerr<<"Feedback AXI error: "<<ex.what()<<"\n";
        }
        const bool completed=step_seen&&done_signal&&observed.q==static_cast<unsigned>(q_next)&&
                             e.quality=="OK"&&!g_stop;
        if(!completed&&e.quality=="OK")e.quality="COMMAND_NOT_CONFIRMED";
        record_event(logger,pending,e,image_save);
        const double command_ms=time_ms(command_started);
        feedback_log<<seq<<','<<q_command_current<<','<<q_next<<',';
        if(step_seen)feedback_log<<observed.q;
        feedback_log<<',';
        if(step_seen)feedback_log<<observed.id;
        feedback_log<<','<<(done_signal?1:0)<<','<<(completed?"PL_DONE_SIGNAL":"STOP_UNCONFIRMED")
                    <<','<<command_ms<<','<<wall_timestamp()<<'\n';feedback_log.flush();
        std::cout<<"FEEDBACK: seq="<<seq<<" q="<<q_command_current<<"->"<<q_next
                 <<" step="<<(step_seen?std::to_string(observed.id):"missing")
                 <<" PL_DONE_SIGNAL="<<done_signal<<"\n";
        if(!completed){
            (void)respond("ERR "+std::to_string(seq)+" "+e.quality);
            result.ok=false;break;
        }
        command_log<<q_next<<'\n';command_log.flush();
        current_q=q_next;last_sequence=seq;
        if(!respond("DONE "+std::to_string(seq)+" "+std::to_string(q_next)))break;
    }
    return result;
}

// q=50, stroke=0 is a posture-only AXI command; it must NOT advance or increment REG5.
// Only call when hardware has returned to AXI mode and R-stick is physically neutral.
bool reset_posture_50(Hardware& hw,const Config& cfg,Logger& logger,EncoderReader& encoder,int next_event){
    std::cout<<"Reset posture -> q=50 (AXI stroke=0, NO forward step)\n";
    LogEvent e;e.no=next_event;e.mode="AXI";e.kind="RESET";e.q=50;
    e.timestamp=wall_timestamp();e.enc=encoder.sample();
    e.command_id=999;e.command_name="reset_posture_50";
    init_action_fields(e,cfg,hw,false);e.command_id=999;e.command_name="reset_posture_50";e.stick3=50;
    e.quality="RESET_DONE_UNCONFIRMED";
    try{
        if(hw.command()&protocol::MODE_PWM)throw std::runtime_error("Still PWM mode");
        if(hw.command()&protocol::START_BIT)throw std::runtime_error("AXI START is high");
        hw.axi_send(50,0);
        if(hw.axi_wait_done(cfg.done_timeout_ms)){
            e.quality="RESET_DONE_SIGNAL";
            logger.write(e);
            std::cout<<"Posture reset done signal received (motor arrival not independently verified).\n";
            return true;
        }
        e.quality=g_stop?"RESET_INTERRUPTED":"RESET_TIMEOUT";
    }catch(const std::exception& ex){
        e.quality="RESET_SEND_ERROR";
        std::cerr<<"Reset error: "<<ex.what()<<"\n";
    }
    logger.write(e);
    std::cerr<<"Posture reset NOT CONFIRMED. Physical motor cutoff / inspect robot.\n";
    return false;
}
}
int main(int argc,char** argv){
    std::signal(SIGINT,on_signal);std::signal(SIGTERM,on_signal);
    try{
        Config cfg=parse(argc,argv);
        if(cfg.mode.empty())cfg.mode=ask("Mode (axi/pwm/feedback): ");
        if(cfg.mode!="axi"&&cfg.mode!="pwm"&&cfg.mode!="feedback")
            throw std::runtime_error("Mode must be axi, pwm or feedback");
        if(cfg.mode=="pwm"&&(!cfg.commands_path.empty()||!cfg.axi_control.empty()))
            throw std::runtime_error("AXI control options cannot be used in PWM mode");
        if(cfg.mode=="axi"){
            // Restore the old peri_AXI_collect_safe interactive manual/auto choice.
            // CLI --commands continues to mean auto, without requiring another prompt.
            const AxiLaunch launch=select_axi_launch(cfg.axi_control,cfg.commands_path,std::cin,std::cout);
            cfg.axi_control=launch.control;
            cfg.commands_path=launch.command_path;
            if(cfg.axi_control=="auto"){
                // Validate before modifying hardware or beginning wire adjustment.
                const auto test=load_commands(cfg.commands_path);
                std::cout<<"AXI AUTO replay file: "<<test.size()<<" q values (0..100)\n";
            }else{
                std::cout<<"AXI MANUAL selected.\n";
            }
        }else if(cfg.mode=="feedback"){
            std::cout<<"PC ONLINE FEEDBACK selected; actuator is AXI and commands arrive over TCP.\n";
        }
        std::cout<<"--- Unified KR260 dataset collection / "<<cfg.mode<<" ---\n";
        parameter_screen(cfg);  // y/n -> optional edits -> voltage, identically in AXI / PWM.
        std::cout<<"ArUco ID=0 DICT_4X4_50 size="<<cfg.marker_m*1000<<"mm; goal Z<="
                 <<cfg.goal_z_mm<<"mm and |angle|<="<<cfg.goal_angle_deg<<"deg\n";
        std::cout<<"IMPORTANT: Motor power OFF for register initialization; R-stick NEUTRAL, toggle LOW.\n";
        Hardware hw;hw.show_status();
        hw.prepare(cfg.rom,cfg.wire_target,cfg.deadband_manual,cfg.deadband_auto);
        // Old peri_AXI_collect_safe live encoder display restored for BOTH modes.
        // Allow physical wire adjustment/verification before opening the camera
        // or accepting any AXI/PWM forward movement.
        WireAdjuster wire_adjuster;
        if(!wire_adjuster.wait_user_confirm())
            throw std::runtime_error("Wire adjustment was interrupted; no motion command sent");
        if(g_stop)throw std::runtime_error("Interrupted after wire adjustment");
        // Original GitHub program records raw16, raw12 and diff-to-base for every pre-action frame.
        // Base is set AFTER the operator finishes wire adjustment, before the first image.
        EncoderReader encoder;
        encoder.set_base();
        const auto encoder_base=encoder.base();
        std::cout<<"Encoder base(16bit): "<<encoder_base.enc0<<","<<encoder_base.enc1
                 <<","<<encoder_base.enc2<<","<<encoder_base.enc3<<"\n";
        CameraCapture camera(cfg.camera);
        ArucoDetector aruco(cfg.yaml,cfg.marker_m,0,cfg.goal_z_mm,cfg.goal_angle_deg);
        const auto dir=cfg.output/(now_stamp()+"_"+cfg.name+"_"+cfg.mode);
        if(fs::exists(dir))throw std::runtime_error("Output directory exists: "+dir.string());
        fs::create_directories(dir/"images");
        Logger log((dir/"log.csv").string(),cfg.battery_voltage_v);
        SafetyLogger safety_log((dir/"emergency_stop.csv").string());
        std::ofstream command_log(dir/"commands.txt");
        if(!command_log)throw std::runtime_error("Cannot create commands.txt");
        std::ofstream meta(dir/"session.txt");
        meta<<"mode="<<cfg.mode<<"\nname="<<cfg.name<<"\nyaml="<<cfg.yaml
            <<"\nmarker_length_m="<<cfg.marker_m<<"\ngoal_z_mm="<<cfg.goal_z_mm
            <<"\ngoal_angle_deg="<<cfg.goal_angle_deg<<"\nsettle_ms="<<cfg.settle_ms
            <<"\nflush_frames="<<cfg.camera.flush_frames
            <<"\nflush_sleep_ms="<<cfg.camera.flush_sleep_ms
            <<"\nrom2_3="<<cfg.rom<<"\naxi_stroke="<<cfg.stroke
            <<"\naxi_control="<<cfg.axi_control
            <<"\ncommands="<<cfg.commands_path
            <<"\nstart_battery_voltage_v="<<cfg.battery_voltage_v
            <<"\nwire_targets="<<cfg.wire_target[0]<<","<<cfg.wire_target[1]<<","<<cfg.wire_target[2]<<","<<cfg.wire_target[3]
            <<"\ndeadband_manual="<<cfg.deadband_manual<<"\ndeadband_auto="<<cfg.deadband_auto
            <<"\ncontrol_source="<<(cfg.mode=="feedback"?"PC_ONLINE_FEEDBACK":(cfg.mode=="pwm"?"PWM_EXPERT":"AXI_FIXED_OR_MANUAL"))
            <<"\nfeedback_protocol=1\nfeedback_tcp_port="<<cfg.feedback_port
            <<"\nencoder_base16="<<encoder_base.enc0<<","<<encoder_base.enc1<<","<<encoder_base.enc2<<","<<encoder_base.enc3
            <<"\nencoder_sample_time=pre_action_camera_frame\n" 
            <<"reg5_event_is_step_START_not_motor_completion=1\n"
            <<"jpeg_writer=async\njpeg_queue_capacity="<<cfg.jpeg_queue<<"\n";
        meta.close();
        std::cout<<"Output: "<<dir<<"\n";
        const auto start=Clock::now();
        AsyncJpegWriter writer(cfg.camera.jpeg_quality,static_cast<std::size_t>(cfg.jpeg_queue));
        PendingLogs pending;
        RunResult result=cfg.mode=="pwm" ? run_pwm(cfg,hw,camera,encoder,aruco,log,dir,start,writer,pending,command_log)
                          : cfg.mode=="feedback" ? run_feedback(cfg,hw,camera,encoder,aruco,log,dir,start,writer,pending,command_log)
                          : run_axi(cfg,hw,camera,encoder,aruco,log,dir,start,writer,pending,command_log);
        // Drain in event order, e.g. before GOAL video hold; guarantees no
        // incomplete image labels in CSV at normal shutdown.
        const bool jpeg_ok=flush_logs(log,pending,true);
        if(!jpeg_ok||writer.failed()){
            result.ok=false;
            std::cerr<<"Asynchronous JPEG failure: inspect log.csv; motor power must be controlled physically.\n";
        }
        // Match the old safety log header. Unlike the original program, this unified
        // program cannot guarantee a motor stop and does not perform a blocking
        // extra camera capture in the fault path.
        if(g_stop||!result.ok){
            const auto status=hw.step();
            const std::string reason=g_stop?"ctrl_c_or_sigterm":"collection_error_or_timeout";
            safety_log.log_abort(wall_timestamp(),reason,result.events,
                                 status.valid?static_cast<int>(status.id):-1,
                                 status.valid?static_cast<int>(status.q):-1,
                                 encoder.sample());
        }

        // AXI and PWM follow the SAME goal-video procedure. After GOAL is
        // recorded, run_axi has sent no more forward commands, and run_pwm
        // has NOT ACKed the pending REG7 request. The robot stays at the
        // goal posture until the operator finishes filming and confirms.
        if(result.goal && result.ok && !g_stop){
            std::cout<<"\n========== GOAL: VIDEO HOLD ==========\n"
                     <<"Goal image/ArUco result recorded. No next forward command will be sent.\n"
                     <<"Keep the current GOAL posture until you have finished recording the trajectory video.\n"
                     <<"Return RIGHT stick to NEUTRAL (and LEFT stick up to neutral); verify that the robot is stopped.\n"
                     <<"Type y ONLY AFTER the video is finished and the robot is safe to reset.\n";
            if(ask_yes_no("Finished recording, sticks NEUTRAL, robot stopped: reset posture to q=50 WITHOUT forward motion?")){
                if(g_stop){
                    result.ok=false;
                    std::cerr<<"Interrupted before reset; no reset command was sent.\n";
                }else{
                    // The PWM gate is still waiting with no ACK after GOAL.
                    // Switch to AXI only after an explicit physical-neutral
                    // confirmation; otherwise the legacy R-stick-UP test may run.
                    if(cfg.mode=="pwm"){
                        std::cout<<"PWM -> AXI mode (RIGHT stick confirmed neutral).\n";
                        hw.cleanup_to_axi();
                    }
                    result.ok=reset_posture_50(hw,cfg,log,encoder,result.events+1);
                }
            }else{
                LogEvent skip;
                skip.no=result.events+1;
                skip.timestamp=wall_timestamp();skip.enc=encoder.sample();
                skip.mode=cfg.mode=="pwm" ? "PWM" : "AXI";
                skip.kind="RESET";
                skip.q=50;
                skip.quality="RESET_SKIPPED_BY_USER";
                log.write(skip);
                std::cout<<"Posture reset SKIPPED. Goal posture preserved.\n";
                if(cfg.mode=="pwm"){
                    std::cerr<<"PWM capture gate/mode remains ENABLED and waiting without ACK. "
                                "Keep RIGHT stick neutral; physically disable motor power before any "
                                "manual mode change or next run.\n";
                }
            }
        }else if(cfg.mode=="pwm"){
            // Non-goal termination: same safe-neutral cleanup as Stage 8;
            // do not send a posture reset if there was no confirmed GOAL.
            std::cout<<"PWM collection ended without GOAL. No further ACK.\n"
                     <<"Release RIGHT stick to neutral (also LEFT stick up); confirm motor stop.\n";
            if(ask_yes_no("Is RIGHT stick physically NEUTRAL, robot stopped, toggle LOW, and safe to switch AXI?")){
                if(!g_stop)hw.cleanup_to_axi();
            }else{
                std::cerr<<"PWM/GATE LEFT ENABLED. Cut motor power before manual recovery.\n";
            }
        }
        std::cout<<"Collection ended in "<<cfg.mode<<" mode. Ctrl+C is not a motor stop.\n";
        std::cout<<"CSV: "<<(dir/"log.csv")<<"\n"
                 <<"Legacy CSV: "<<(dir/"legacy_log.csv")<<"\n"
                 <<"Replay: "<<(dir/"commands.txt")<<"\n";
        return result.ok?0:2;
    }catch(const std::exception& ex){
        std::cerr<<"ERROR: "<<ex.what()<<"\n"
                 <<"This app is not an emergency motor stop. R-stick NEUTRAL / physical power cutoff if needed.\n";
        return 1;
    }
}

