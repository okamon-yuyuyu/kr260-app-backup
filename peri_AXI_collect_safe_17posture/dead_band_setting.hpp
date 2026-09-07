#ifndef DEAD_BAND_SETTING_HPP
#define DEAD_BAND_SETTING_HPP

#include <cstdint>

class DeadBandSetting {
public:
    explicit DeadBandSetting(uintptr_t baseaddr = 0xA0040000UL);
    ~DeadBandSetting();

    DeadBandSetting(const DeadBandSetting&) = delete;
    DeadBandSetting& operator=(const DeadBandSetting&) = delete;

    void set(uint32_t manual_value, uint32_t auto_value);
    uint32_t read_manual() const;
    uint32_t read_auto() const;

private:
    int fd_ = -1;
    void* map_base_ = nullptr;
    volatile uint32_t* reg_ = nullptr;
    uintptr_t baseaddr_;

    static constexpr uint32_t MAP_SIZE = 4096UL;
    static constexpr uint32_t MAP_MASK = MAP_SIZE - 1;
    static constexpr uint32_t DEAD_BAND_MASK = 0x3FU;

    static constexpr int REG_MANUAL = 0;
    static constexpr int REG_AUTO = 1;
};

#endif
