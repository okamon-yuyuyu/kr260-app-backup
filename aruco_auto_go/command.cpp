#include "command.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

CommandInterface::CommandInterface(uintptr_t baseaddr) : baseaddr_(baseaddr) {
    fd_ = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd_ < 0) {
        throw std::runtime_error(std::string("open /dev/mem failed: ") + std::strerror(errno));
    }

    map_base_ = mmap(nullptr, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                     fd_, baseaddr_ & ~static_cast<uintptr_t>(MAP_MASK));
    if (map_base_ == MAP_FAILED) {
        close(fd_);
        fd_ = -1;
        throw std::runtime_error(std::string("mmap command failed: ") + std::strerror(errno));
    }

    reg_ = reinterpret_cast<volatile uint32_t*>(
        static_cast<char*>(map_base_) + (baseaddr_ & MAP_MASK));
}

CommandInterface::~CommandInterface() {
    if (map_base_ && map_base_ != MAP_FAILED) {
        munmap(map_base_, MAP_SIZE);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

void CommandInterface::send(const PeristalsisCommand& cmd) {
    reg_[REG_ROM_L] = (static_cast<uint32_t>(cmd.rom1) << 16) | cmd.rom0;
    reg_[REG_ROM_H] = (static_cast<uint32_t>(cmd.rom3) << 16) | cmd.rom2;

    reg_[REG_STROKE_L] = (static_cast<uint32_t>(cmd.stroke_left) << 16) | cmd.stroke_right;
    reg_[REG_STROKE_H] = (static_cast<uint32_t>(cmd.stroke_down) << 16) | cmd.stroke_up;

    const uint32_t cmd_reg =
        (static_cast<uint32_t>(cmd.stick1 & 0x1F) << 0) |
        (static_cast<uint32_t>(cmd.stick3 & 0x1F) << 5);

    reg_[REG_CMD] = cmd_reg;
    usleep(1000);
    reg_[REG_CMD] = cmd_reg | (1U << CMD_START_BIT);
    usleep(1000);
    reg_[REG_CMD] = cmd_reg;
}

bool CommandInterface::wait_done(int timeout_ms) {
    const int interval_us = 1000;
    const int max_count = timeout_ms;

    for (int i = 0; i < max_count; ++i) {
        const uint32_t v = reg_[REG_CMD];
        if (((v >> CMD_DONE_BIT) & 0x1U) != 0U) {
            return true;
        }
        usleep(interval_us);
    }

    return false;
}

uint32_t CommandInterface::read_cmd_reg() const {
    return reg_[REG_CMD];
}

PeristalsisCommand make_posture_command(int id, int posture) {
    if (!(1 <= id && id <= 9)) {
        throw std::invalid_argument("unknown posture command id");
    }

    PeristalsisCommand cmd;
    cmd.id = id;
    cmd.name = "posture_" + std::to_string(posture);

    cmd.stick1 = 15;       // unused
    cmd.stick3 = posture;  // posture

    cmd.stroke_right = 0;
    cmd.stroke_left  = 0;
    cmd.stroke_up    = 0;
    cmd.stroke_down  = 0;

    return cmd;
}

PeristalsisCommand make_go_command(int posture) {
    PeristalsisCommand cmd;
    cmd.id = 100;
    cmd.name = "go";

    cmd.stick1 = 15;       // unused
    cmd.stick3 = posture;  // current posture

    cmd.stroke_right = 2000;
    cmd.stroke_left  = 2000;
    cmd.stroke_up    = 0;
    cmd.stroke_down  = 0;

    return cmd;
}

PeristalsisCommand make_stop_command(int posture) {
    PeristalsisCommand cmd;
    cmd.id = 200;
    cmd.name = "stop";

    cmd.stick1 = 15;       // unused
    cmd.stick3 = posture;  // current posture

    cmd.stroke_right = 0;
    cmd.stroke_left  = 0;
    cmd.stroke_up    = 0;
    cmd.stroke_down  = 0;

    return cmd;
}