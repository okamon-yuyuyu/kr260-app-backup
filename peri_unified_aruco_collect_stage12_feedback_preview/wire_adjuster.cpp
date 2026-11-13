#include "wire_adjuster.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>

extern volatile std::sig_atomic_t g_stop;

namespace {
constexpr std::uintptr_t ENCODER_BASE = 0xA0030000UL;
constexpr std::uintptr_t WIRE_BASE = 0xA0020000UL;
constexpr std::uint32_t ENCODER_MASK = 0x0FFFu;
}

WireAdjuster::WireAdjuster() {
    const long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) throw std::runtime_error("Cannot read memory page size");
    map_len_ = static_cast<std::size_t>(page);
    if (ENCODER_BASE % map_len_ || WIRE_BASE % map_len_)
        throw std::runtime_error("Encoder/Wire address not page-aligned");

    fd_ = ::open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd_ < 0)
        throw std::runtime_error(std::string("open /dev/mem failed: ") + std::strerror(errno));

    enc_map_ = mmap(nullptr, map_len_, PROT_READ, MAP_SHARED, fd_,
                    static_cast<off_t>(ENCODER_BASE));
    if (enc_map_ == MAP_FAILED) {
        enc_map_ = nullptr;
        const std::string what = std::strerror(errno);
        ::close(fd_); fd_ = -1;
        throw std::runtime_error("mmap encoder failed: " + what);
    }

    wire_map_ = mmap(nullptr, map_len_, PROT_READ, MAP_SHARED, fd_,
                     static_cast<off_t>(WIRE_BASE));
    if (wire_map_ == MAP_FAILED) {
        wire_map_ = nullptr;
        const std::string what = std::strerror(errno);
        munmap(enc_map_, map_len_); enc_map_ = nullptr;
        ::close(fd_); fd_ = -1;
        throw std::runtime_error("mmap wire target failed: " + what);
    }
    enc_reg_ = static_cast<volatile const std::uint32_t*>(enc_map_);
    wire_reg_ = static_cast<volatile const std::uint32_t*>(wire_map_);
}

WireAdjuster::~WireAdjuster() {
    if (wire_map_) munmap(wire_map_, map_len_);
    if (enc_map_) munmap(enc_map_, map_len_);
    if (fd_ >= 0) ::close(fd_);
}

WireAdjuster::Snapshot WireAdjuster::snapshot() const {
    Snapshot result;
    // Current encoder IP, same packing as old wire_adjuster.cpp:
    // [11:0] enc0, [23:12] enc1, next register enc2 and enc3.
    const std::uint32_t reg0 = enc_reg_[0];
    const std::uint32_t reg1 = enc_reg_[1];
    result.encoder = {{
        static_cast<std::uint16_t>(reg0 & ENCODER_MASK),
        static_cast<std::uint16_t>((reg0 >> 12) & ENCODER_MASK),
        static_cast<std::uint16_t>(reg1 & ENCODER_MASK),
        static_cast<std::uint16_t>((reg1 >> 12) & ENCODER_MASK)
    }};
    for (unsigned i = 0; i < 4; ++i)
        result.target[i] = static_cast<std::uint16_t>(wire_reg_[i] & ENCODER_MASK);
    return result;
}

bool WireAdjuster::wait_user_confirm() const {
    const auto initial = snapshot();
    std::cout << "\nWire adjustment running...\n"
              << "目標姿勢になったら y を入力してください（y + Enter）\n"
              << "Encoder values and targets are 12-bit raw counts (0..4095), NOT degrees.\n"
              << "Target: ";
    for (unsigned i = 0; i < 4; ++i) {
        if (i) std::cout << "  ";
        std::cout << "target_raw" << i << "=" << initial.target[i];
    }
    std::cout << "\n入力: y=調整確認 / x=中止 / Ctrl+C=中止\n\n";

    while (!g_stop) {
        const auto s = snapshot();
        std::cout << "\r\033[2K";
        for (unsigned i = 0; i < 4; ++i) {
            std::cout << "enc" << i << "=" << std::setw(4) << s.encoder[i]
                      << "/" << std::setw(4) << s.target[i] << "  ";
        }
        std::cout << "input y + Enter when OK: " << std::flush;

        fd_set input;
        FD_ZERO(&input);
        FD_SET(STDIN_FILENO, &input);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // Refresh live readings at ~10 Hz.
        const int ready = select(STDIN_FILENO + 1, &input, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("select(stdin) failed: ") + std::strerror(errno));
        }
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &input)) {
            std::string answer;
            if (!std::getline(std::cin, answer)) {
                if (g_stop) break;
                throw std::runtime_error("Wire adjustment input closed");
            }
            if (answer == "y" || answer == "Y") {
                const auto last = snapshot();
                std::cout << "\nWire adjustment confirmed by user.\nFinal encoder: ";
                for (unsigned i = 0; i < 4; ++i) {
                    if (i) std::cout << ", ";
                    std::cout << "enc" << i << "=" << last.encoder[i];
                }
                std::cout << "\n\n";
                return true;
            }
            if (answer == "x" || answer == "X" || answer == "q" || answer == "Q") {
                std::cout << "\n";
                throw std::runtime_error("Wire adjustment canceled by user");
            }
            std::cout << "\nEnter y to confirm or x to cancel.\n";
        }
    }
    std::cout << "\nWire adjustment interrupted.\n";
    return false;
}

