// wire_adjuster.cpp
#include "wire_adjuster.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

WireAdjuster::WireAdjuster(uintptr_t encoder_base, uintptr_t wire_base) {
    fd_ = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd_ < 0) {
        throw std::runtime_error(std::string("open /dev/mem failed: ") + std::strerror(errno));
    }

    enc_map_ = mmap(nullptr, MAP_SIZE, PROT_READ, MAP_SHARED, fd_, encoder_base);
    if (enc_map_ == MAP_FAILED) {
        close(fd_);
        throw std::runtime_error(std::string("mmap wire encoder failed: ") + std::strerror(errno));
    }

    wire_map_ = mmap(nullptr, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, wire_base);
    if (wire_map_ == MAP_FAILED) {
        munmap(enc_map_, MAP_SIZE);
        close(fd_);
        throw std::runtime_error(std::string("mmap wire setting failed: ") + std::strerror(errno));
    }

    enc_reg_ = reinterpret_cast<volatile uint32_t*>(enc_map_);
    wire_reg_ = reinterpret_cast<volatile uint32_t*>(wire_map_);
}

WireAdjuster::~WireAdjuster() {
    if (wire_map_ && wire_map_ != MAP_FAILED) munmap(wire_map_, MAP_SIZE);
    if (enc_map_ && enc_map_ != MAP_FAILED) munmap(enc_map_, MAP_SIZE);
    if (fd_ >= 0) close(fd_);
}

void WireAdjuster::set_target_raw(uint16_t raw0, uint16_t raw1, uint16_t raw2, uint16_t raw3) {
    wire_reg_[0] = raw0 & 0x0FFF;
    wire_reg_[1] = raw1 & 0x0FFF;
    wire_reg_[2] = raw2 & 0x0FFF;
    wire_reg_[3] = raw3 & 0x0FFF;

    std::cout << "target_raw set\n";
    std::cout << "target_raw0 = " << raw0 << "\n";
    std::cout << "target_raw1 = " << raw1 << "\n";
    std::cout << "target_raw2 = " << raw2 << "\n";
    std::cout << "target_raw3 = " << raw3 << "\n\n";
}

void WireAdjuster::wait_user_confirm() {
    std::cout << "Wire adjustment running...\n";
    std::cout << "目標姿勢になったら y を入力してください\n\n";

    while (true) {
        const uint32_t reg0 = enc_reg_[0];
        const uint32_t reg1 = enc_reg_[1];

        const uint32_t enc0 =  reg0        & 0x0FFF;
        const uint32_t enc1 = (reg0 >> 12) & 0x0FFF;
        const uint32_t enc2 =  reg1        & 0x0FFF;
        const uint32_t enc3 = (reg1 >> 12) & 0x0FFF;

        std::cout << "\renc0=" << enc0
                  << "  enc1=" << enc1
                  << "  enc2=" << enc2
                  << "  enc3=" << enc3
                  << "   input y + Enter when OK: "
                  << std::flush;

        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;

        if (select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout) > 0) {
            std::string input;
            std::cin >> input;
            if (input == "y" || input == "Y") {
                std::cout << "\nWire adjustment confirmed.\n\n";
                break;
            }
        }
    }
}