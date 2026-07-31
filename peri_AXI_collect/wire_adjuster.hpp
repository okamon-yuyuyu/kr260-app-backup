// wire_adjuster.hpp
#ifndef WIRE_ADJUSTER_HPP
#define WIRE_ADJUSTER_HPP

#include <cstdint>

class WireAdjuster {
public:
    WireAdjuster(uintptr_t encoder_base = 0xA0030000UL,
                 uintptr_t wire_base = 0xA0020000UL);
    ~WireAdjuster();

    void set_target_raw(uint16_t raw0, uint16_t raw1, uint16_t raw2, uint16_t raw3);
    void wait_user_confirm();

private:
    int fd_ = -1;
    void* enc_map_ = nullptr;
    void* wire_map_ = nullptr;
    volatile uint32_t* enc_reg_ = nullptr;
    volatile uint32_t* wire_reg_ = nullptr;

    static constexpr uint32_t MAP_SIZE = 0x10000;
};

#endif