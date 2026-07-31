#include "encoder.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

EncoderReader::EncoderReader(uintptr_t baseaddr) : baseaddr_(baseaddr) {
    fd_ = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd_ < 0) {
        throw std::runtime_error(std::string("open /dev/mem failed: ") + std::strerror(errno));
    }

    map_base_ = mmap(nullptr, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                     fd_, baseaddr_ & ~static_cast<uintptr_t>(MAP_MASK));
    if (map_base_ == MAP_FAILED) {
        close(fd_);
        fd_ = -1;
        throw std::runtime_error(std::string("mmap encoder failed: ") + std::strerror(errno));
    }

    reg_ = reinterpret_cast<volatile uint32_t*>(
        static_cast<char*>(map_base_) + (baseaddr_ & MAP_MASK));

    set_base();
}

EncoderReader::~EncoderReader() {
    if (map_base_ && map_base_ != MAP_FAILED) {
        munmap(map_base_, MAP_SIZE);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

EncoderValues EncoderReader::read_raw() const {
    const uint32_t raw0 = reg_[0];
    const uint32_t raw1 = reg_[1];

    EncoderValues v;
    v.enc0 = raw0 & 0xFFFF;
    v.enc1 = (raw0 >> 16) & 0xFFFF;
    v.enc2 = raw1 & 0xFFFF;
    v.enc3 = (raw1 >> 16) & 0xFFFF;
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
