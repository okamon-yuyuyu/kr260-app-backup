#pragma once
#include <cstdint>

namespace protocol {
constexpr std::uint64_t CMD_BASE=0xA0010000ULL;
constexpr std::uint64_t WIRE_BASE=0xA0020000ULL;
constexpr std::uint64_t DEAD_BASE=0xA0040000ULL;
constexpr unsigned ROM23=1, COMMAND=4, STEP=5, CAMERA_CTRL=6, CAMERA_STATUS=7;
constexpr std::uint32_t MODE_PWM=(1u<<16), START_BIT=(1u<<14);
constexpr std::uint32_t CAMERA_ENABLE=1u;

struct Step {
    std::uint16_t id=0;
    unsigned q=0;
    bool pwm=false;
    bool valid=false;
    std::uint32_t raw=0;
};
inline Step step_decode(std::uint32_t r) {
    return {static_cast<std::uint16_t>((r>>7)&0xffffu),
            static_cast<unsigned>(r&0x7fu), (r&(1u<<23))!=0,
            (r&(1u<<24))!=0, r};
}
struct Request {
    std::uint16_t seq=0;
    bool waiting=false;
    std::uint32_t raw=0;
};
inline Request req_decode(std::uint32_t r) {
    return {static_cast<std::uint16_t>(r>>16), (r&1u)!=0, r};
}
inline std::uint32_t reg6(std::uint16_t ack, bool enable) {
    return (static_cast<std::uint32_t>(ack)<<16) | (enable ? 1u : 0u);
}
inline std::uint16_t seq_delta(std::uint16_t before, std::uint16_t after) {
    return static_cast<std::uint16_t>(after-before);
}
}

