#pragma once
#include <cstddef>
#include <array>
#include "protocol.hpp"
#include <cstdint>
#include <string>
#include <vector>

class Mapping {
public:
    Mapping(int fd,std::uint64_t base);
    ~Mapping();
    Mapping(const Mapping&)=delete;
    Mapping& operator=(const Mapping&)=delete;
    std::uint32_t get(unsigned idx) const;
    void put(unsigned idx,std::uint32_t v);
    static std::string hex(std::uint32_t v);
private:
    volatile std::uint32_t* data_=nullptr;
    std::size_t bytes_=0;
};
class Hardware {
public:
    Hardware();
    void prepare(int rom,const std::array<std::uint16_t,4>& targets,
                 int manual_deadband,int auto_deadband);
    void show_status()const;
    void arm_pwm();  // REG6 ON before REG4 PWM ON
    void arm_axi();  // REG4 AXI, REG6 OFF (requires physical neutral)
    void cleanup_to_axi(); // requires physical neutral, no active motion
    void ack(std::uint16_t seq);
    std::uint32_t command()const{return cmd_.get(protocol::COMMAND);}
    std::uint32_t camera_ctrl()const{return cmd_.get(protocol::CAMERA_CTRL);}
    std::uint32_t rom01()const{return cmd_.get(0);}
    std::uint32_t rom23()const{return cmd_.get(protocol::ROM23);}
    protocol::Request request()const{return protocol::req_decode(cmd_.get(protocol::CAMERA_STATUS));}
    protocol::Step step()const{return protocol::step_decode(cmd_.get(protocol::STEP));}
    void axi_send(int q,std::uint16_t stroke=2000);
    bool axi_wait_done(int timeout_ms);
private:
    struct Fd {int fd=-1;Fd();~Fd();} fd_;
    Mapping cmd_,wire_,dead_;
};

