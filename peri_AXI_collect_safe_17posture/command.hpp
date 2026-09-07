#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <cstdint>
#include <string>

struct PeristalsisCommand {
    int id = 0;

    uint16_t rom0 = 1000;
    uint16_t rom1 = 1000;
    // Horizontal posture resolution is doubled while preserving the
    // physical range of the legacy commands 1-9.
    uint16_t rom2 = 500;
    uint16_t rom3 = 500;

    uint16_t stroke_right = 0;
    uint16_t stroke_left  = 0;
    uint16_t stroke_up    = 0;
    uint16_t stroke_down  = 0;

    uint8_t stick1 = 15;
    uint8_t stick3 = 15;

    std::string name;
};

class CommandInterface {
public:
    explicit CommandInterface(uintptr_t baseaddr = 0xA0010000UL);
    ~CommandInterface();

    CommandInterface(const CommandInterface&) = delete;
    CommandInterface& operator=(const CommandInterface&) = delete;

    void send(const PeristalsisCommand& cmd);
    bool wait_done(int timeout_ms = 10000);
    bool is_done() const;
    uint32_t read_cmd_reg() const;

private:
    int fd_ = -1;
    void* map_base_ = nullptr;
    volatile uint32_t* reg_ = nullptr;
    uintptr_t baseaddr_;

    static constexpr uint32_t MAP_SIZE = 4096UL;
    static constexpr uint32_t MAP_MASK = MAP_SIZE - 1;

    static constexpr int REG_ROM_L    = 0;
    static constexpr int REG_ROM_H    = 1;
    static constexpr int REG_STROKE_L = 2;
    static constexpr int REG_STROKE_H = 3;
    static constexpr int REG_CMD      = 4;

    static constexpr int CMD_START_BIT = 10;
    static constexpr int CMD_DONE_BIT  = 11;
};

// Legacy commands 1-9 keep their original physical postures.  Commands
// 12,23,...,89 select the midpoint between the adjacent legacy commands.
bool posture_from_command_id(int id, uint8_t& posture);
PeristalsisCommand make_posture_command(int id);
PeristalsisCommand make_go_command(int posture);

#endif
