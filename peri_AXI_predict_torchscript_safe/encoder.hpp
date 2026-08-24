#ifndef ENCODER_HPP
#define ENCODER_HPP

#include <cstdint>

struct EncoderValues {
    int32_t enc0 = 0;
    int32_t enc1 = 0;
    int32_t enc2 = 0;
    int32_t enc3 = 0;
};

class EncoderReader {
public:
    explicit EncoderReader(uintptr_t baseaddr_16bit = 0xA0000000UL,
                           uintptr_t baseaddr_12bit = 0xA0030000UL);
    ~EncoderReader();

    EncoderReader(const EncoderReader&) = delete;
    EncoderReader& operator=(const EncoderReader&) = delete;

    EncoderValues read_raw() const;
    EncoderValues read_raw12() const;
    void set_base();
    EncoderValues read_diff() const;
    EncoderValues base() const { return base_; }

private:
    int fd_ = -1;
    void* map_base_16bit_ = nullptr;
    void* map_base_12bit_ = nullptr;
    volatile uint32_t* reg_16bit_ = nullptr;
    volatile uint32_t* reg_12bit_ = nullptr;
    uintptr_t baseaddr_16bit_;
    uintptr_t baseaddr_12bit_;
    EncoderValues base_{};

    static constexpr uint32_t MAP_SIZE = 4096UL;
    static constexpr uint32_t MAP_MASK = MAP_SIZE - 1;
};

#endif
