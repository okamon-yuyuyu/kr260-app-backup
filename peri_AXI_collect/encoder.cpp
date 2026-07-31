#include "encoder.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

EncoderReader::EncoderReader(uintptr_t baseaddr_16bit, uintptr_t baseaddr_12bit)
    : baseaddr_16bit_(baseaddr_16bit),
      baseaddr_12bit_(baseaddr_12bit) {
    fd_ = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd_ < 0) {
        throw std::runtime_error(std::string("open /dev/mem failed: ") + std::strerror(errno));
    }

    map_base_16bit_ = mmap(
        nullptr, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_,
        baseaddr_16bit_ & ~static_cast<uintptr_t>(MAP_MASK));
    if (map_base_16bit_ == MAP_FAILED) {
        close(fd_);
        fd_ = -1;
        throw std::runtime_error(
            std::string("mmap 16-bit encoder failed: ") + std::strerror(errno));
    }

    map_base_12bit_ = mmap(
        nullptr, MAP_SIZE, PROT_READ, MAP_SHARED, fd_,
        baseaddr_12bit_ & ~static_cast<uintptr_t>(MAP_MASK));
    if (map_base_12bit_ == MAP_FAILED) {
        munmap(map_base_16bit_, MAP_SIZE);
        map_base_16bit_ = nullptr;
        close(fd_);
        fd_ = -1;
        throw std::runtime_error(
            std::string("mmap 12-bit encoder failed: ") + std::strerror(errno));
    }

    reg_16bit_ = reinterpret_cast<volatile uint32_t*>(
        static_cast<char*>(map_base_16bit_) + (baseaddr_16bit_ & MAP_MASK));
    reg_12bit_ = reinterpret_cast<volatile uint32_t*>(
        static_cast<char*>(map_base_12bit_) + (baseaddr_12bit_ & MAP_MASK));

    set_base();
}

EncoderReader::~EncoderReader() {
    if (map_base_12bit_ && map_base_12bit_ != MAP_FAILED) {
        munmap(map_base_12bit_, MAP_SIZE);
    }
    if (map_base_16bit_ && map_base_16bit_ != MAP_FAILED) {
        munmap(map_base_16bit_, MAP_SIZE);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

EncoderValues EncoderReader::read_raw() const {
    const uint32_t raw0 = reg_16bit_[0];
    const uint32_t raw1 = reg_16bit_[1];

    EncoderValues v;
    v.enc0 = raw0 & 0xFFFF;
    v.enc1 = (raw0 >> 16) & 0xFFFF;
    v.enc2 = raw1 & 0xFFFF;
    v.enc3 = (raw1 >> 16) & 0xFFFF;
    return v;
}

EncoderValues EncoderReader::read_raw12() const {
    const uint32_t raw0 = reg_12bit_[0];
    const uint32_t raw1 = reg_12bit_[1];

    EncoderValues v;
    v.enc0 = raw0 & 0x0FFF;
    v.enc1 = (raw0 >> 12) & 0x0FFF;
    v.enc2 = raw1 & 0x0FFF;
    v.enc3 = (raw1 >> 12) & 0x0FFF;
    return v;
}

void EncoderReader::set_base() {
    base_ = read_raw();
}

EncoderValues EncoderReader::read_diff() const {
    const EncoderValues now = read_raw();
    EncoderValues d;
    d.enc0 = now.enc0 - base_.enc0;
    d.enc1 = now.enc1 - base_.enc1;
    d.enc2 = now.enc2 - base_.enc2;
    d.enc3 = now.enc3 - base_.enc3;
    return d;
}
