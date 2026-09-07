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
        if (is_done()) {
            return true;
        }
        usleep(interval_us);
    }

    return false;
}

bool CommandInterface::is_done() const {
    const uint32_t v = reg_[REG_CMD];
    return ((v >> CMD_DONE_BIT) & 0x1U) != 0U;
}

uint32_t CommandInterface::read_cmd_reg() const {
    return reg_[REG_CMD];
}

bool posture_from_command_id(int id, uint8_t& posture) {
    if (1 <= id && id <= 9) {
        // Old cmd1..9 used stick3=11..19 with ROM=1000.  With ROM=500,
        // stick3=7,9,...,23 gives the same physical target angles.
        posture = static_cast<uint8_t>(7 + 2 * (id - 1));
        return true;
    }

    switch (id) {
        case 12: posture = 8;  return true;
        case 23: posture = 10; return true;
        case 34: posture = 12; return true;
        case 45: posture = 14; return true;
        case 56: posture = 16; return true;
        case 67: posture = 18; return true;
        case 78: posture = 20; return true;
        case 89: posture = 22; return true;
        default: return false;
    }
}

PeristalsisCommand make_posture_command(int id) {
    uint8_t posture = 15;
    if (!posture_from_command_id(id, posture)) {
        throw std::invalid_argument("unknown posture command id");
    }

    PeristalsisCommand cmd;
    cmd.id = id;
    cmd.name = "posture_" + std::to_string(posture);

    cmd.stick1 = 15;       // unused
    cmd.stick3 = posture;  // 17-level horizontal posture

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
