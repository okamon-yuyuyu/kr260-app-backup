#include "command.hpp"
#include <array>
#include <csignal>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
extern volatile std::sig_atomic_t g_stop;
namespace {using namespace std::chrono_literals;}
Mapping::Mapping(int fd,std::uint64_t base){
    const long sz=sysconf(_SC_PAGESIZE);
    if(sz<=0||base%static_cast<std::uint64_t>(sz)!=0)throw std::runtime_error("Bad mmap base/page");
    bytes_=static_cast<std::size_t>(sz);
    void* p=mmap(nullptr,bytes_,PROT_READ|PROT_WRITE,MAP_SHARED,fd,static_cast<off_t>(base));
    if(p==MAP_FAILED)throw std::runtime_error("mmap failed at "+hex(static_cast<std::uint32_t>(base)));
    data_=static_cast<volatile std::uint32_t*>(p);
}
Mapping::~Mapping(){if(data_)munmap(const_cast<void*>(static_cast<const volatile void*>(data_)),bytes_);}
std::uint32_t Mapping::get(unsigned idx)const{return data_[idx];}
void Mapping::put(unsigned idx,std::uint32_t v){data_[idx]=v;__sync_synchronize();}
std::string Mapping::hex(std::uint32_t v){std::ostringstream s;s<<std::hex<<std::setw(8)<<std::setfill('0')<<v;return s.str();}
Hardware::Fd::Fd(){fd=::open("/dev/mem",O_RDWR|O_SYNC);if(fd<0)throw std::runtime_error("Open /dev/mem failed; run sudo");}
Hardware::Fd::~Fd(){if(fd>=0)::close(fd);}
Hardware::Hardware():fd_(),cmd_(fd_.fd,protocol::CMD_BASE),wire_(fd_.fd,protocol::WIRE_BASE),dead_(fd_.fd,protocol::DEAD_BASE){}
void Hardware::show_status()const{
    const auto req=request();
    std::cout<<"REG4=0x"<<Mapping::hex(command())<<" REG5=0x"<<Mapping::hex(cmd_.get(protocol::STEP))
             <<" REG6=0x"<<Mapping::hex(camera_ctrl())<<" REG7=0x"<<Mapping::hex(req.raw)
             <<" waiting="<<req.waiting<<" seq="<<req.seq<<"\n"
             <<"ROM23=0x"<<Mapping::hex(cmd_.get(protocol::ROM23))
             <<" wire="<<wire_.get(0)<<","<<wire_.get(1)<<","<<wire_.get(2)<<","<<wire_.get(3)
             <<" dead="<<dead_.get(0)<<","<<dead_.get(1)<<"\n";
}
void Hardware::prepare(int rom,const std::array<std::uint16_t,4>& raw,
                       int manual_deadband,int auto_deadband){
    if((command()&(protocol::MODE_PWM|protocol::START_BIT))!=0)
        throw std::runtime_error("PREPARE requires REG4 AXI, START=0. Neutralize and safely reset previous session first.");
    for(const auto v:raw)if(v==0||v>4095)throw std::runtime_error("Invalid wire target");
    if(manual_deadband<0||manual_deadband>63||auto_deadband<0||auto_deadband>63)
        throw std::runtime_error("Invalid deadband");
    for(unsigned i=0;i<4;i++)wire_.put(i,raw[i]);
    dead_.put(0,manual_deadband);dead_.put(1,auto_deadband);
    const std::uint32_t romval=static_cast<std::uint32_t>(rom);
    const std::uint32_t word=(romval<<16)|romval;
    cmd_.put(protocol::ROM23,word);
    for(unsigned i=0;i<4;i++)if(wire_.get(i)!=raw[i])throw std::runtime_error("Wire register verify failed");
    if(dead_.get(0)!=static_cast<unsigned>(manual_deadband)||
       dead_.get(1)!=static_cast<unsigned>(auto_deadband)||cmd_.get(protocol::ROM23)!=word)
        throw std::runtime_error("Deadband / ROM readback failed");
    std::cout<<"Register setup verified: wire="<<raw[0]<<","<<raw[1]<<","<<raw[2]<<","<<raw[3]
             <<"; deadband="<<manual_deadband<<","<<auto_deadband<<"; ROM2/3="<<rom<<"\n";
}
void Hardware::arm_pwm(){
    if(command()&(protocol::MODE_PWM|protocol::START_BIT))throw std::runtime_error("REG4 must be AXI with START=0 before PWM arm");
    if(request().waiting)throw std::runtime_error("REG7 already waiting. Check FPGA session/reset before ARM");
    cmd_.put(protocol::CAMERA_CTRL,protocol::reg6(0,true));
    if(cmd_.get(protocol::CAMERA_CTRL)!=protocol::reg6(0,true))throw std::runtime_error("REG6 arm verify failed");
    const std::uint32_t r4=command();
    cmd_.put(protocol::COMMAND,(r4&~(protocol::START_BIT|(1u<<15)))|protocol::MODE_PWM);
    if(!(command()&protocol::MODE_PWM))throw std::runtime_error("REG4 PWM verify failed");
}
void Hardware::arm_axi(){
    if(command()&(protocol::MODE_PWM|protocol::START_BIT))throw std::runtime_error("AXI requested but REG4 not in AXI idle");
    if(request().waiting)throw std::runtime_error("REG7 waiting; previous PWM session may be active");
    cmd_.put(protocol::CAMERA_CTRL,0);
    if(cmd_.get(protocol::CAMERA_CTRL)!=0)throw std::runtime_error("REG6 off verify failed");
}
void Hardware::cleanup_to_axi(){
    const std::uint32_t v=command();
    cmd_.put(protocol::COMMAND,v&~(protocol::MODE_PWM|protocol::START_BIT|(1u<<15)));
    std::this_thread::sleep_for(20ms);
    cmd_.put(protocol::CAMERA_CTRL,0);
    show_status();
}
void Hardware::ack(std::uint16_t seq){
    const auto word=protocol::reg6(seq,true);
    cmd_.put(protocol::CAMERA_CTRL,word);
    if(camera_ctrl()!=word)throw std::runtime_error("REG6 ACK readback mismatch");
}
void Hardware::axi_send(int q,std::uint16_t stroke){
    if(q<0||q>100)throw std::runtime_error("q out of range");
    if(command() & protocol::MODE_PWM)throw std::runtime_error("Cannot send AXI command while PWM mode");
    // PL 7-bit: REG4[13:7] is stick3 (q); [14] rising-edge start; [16]=0.
    // REG2 is [15:0] right stroke and [31:16] left stroke.
    cmd_.put(2,(std::uint32_t(stroke)<<16)|stroke);
    cmd_.put(3,0);
    const std::uint32_t base=(50u /* unused stick1 */)| (static_cast<std::uint32_t>(q)<<7);
    cmd_.put(protocol::COMMAND,base);
    std::this_thread::sleep_for(2ms);
    cmd_.put(protocol::COMMAND,base|protocol::START_BIT);
    std::this_thread::sleep_for(2ms);
    cmd_.put(protocol::COMMAND,base);
}
bool Hardware::axi_wait_done(int timeout_ms){
    // Look for done=0 (new command accepted), then done=1. Existing PL sets
    // done high for timeouts too, so completion *status* is not guaranteed.
    bool saw_low=false;
    for(int i=0;i<timeout_ms && !g_stop;i++){
        const bool high=(command()&(1u<<15))!=0;
        if(!high)saw_low=true;
        if(saw_low&&high)return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}
