#include "dead_band_setting.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

DeadBandSetting::DeadBandSetting(uintptr_t baseaddr) : baseaddr_(baseaddr) {
    fd_ = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd_ < 0) {
        throw std::runtime_error(
            std::string("open /dev/mem failed: ") + std::strerror(errno));
    }

    map_base_ = mmap(nullptr, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                     fd_, baseaddr_ & ~static_cast<uintptr_t>(MAP_MASK));
    if (map_base_ == MAP_FAILED) {
        close(fd_);
        fd_ = -1;
        throw std::runtime_error(
            std::string("mmap dead band setting failed: ") + std::strerror(errno));
    }

    reg_ = reinterpret_cast<volatile uint32_t*>(
        static_cast<char*>(map_base_) + (baseaddr_ & MAP_MASK));
}

DeadBandSetting::~DeadBandSetting() {
    if (map_base_ && map_base_ != MAP_FAILED) {
        munmap(map_base_, MAP_SIZE);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

void DeadBandSetting::set(uint32_t manual_value, uint32_t auto_value) {
    if (manual_value > DEAD_BAND_MASK || auto_value > DEAD_BAND_MASK) {
        throw std::invalid_argument("dead band must be in the range 0 to 63");
    }

    reg_[REG_MANUAL] = manual_value;
    reg_[REG_AUTO] = auto_value;
    std::atomic_thread_fence(std::memory_order_seq_cst);

    const uint32_t manual_readback = read_manual();
    const uint32_t auto_readback = read_auto();

    if (manual_readback != manual_value || auto_readback != auto_value) {
        throw std::runtime_error(
            "dead band readback mismatch: manual=" +
            std::to_string(manual_readback) + "/" + std::to_string(manual_value) +
            ", auto=" + std::to_string(auto_readback) + "/" +
            std::to_string(auto_value));
    }

    std::cout << "dead band set\n";
    std::cout << "manual = " << manual_readback << "\n";
    std::cout << "auto   = " << auto_readback << "\n\n";
}

uint32_t DeadBandSetting::read_manual() const {
    return reg_[REG_MANUAL] & DEAD_BAND_MASK;
}

uint32_t DeadBandSetting::read_auto() const {
    return reg_[REG_AUTO] & DEAD_BAND_MASK;
}
