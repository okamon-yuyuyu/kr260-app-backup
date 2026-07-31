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
    explicit EncoderReader(uintptr_t baseaddr = 0xA0000000UL);
    ~EncoderReader();

    EncoderReader(const EncoderReader&) = delete;
    EncoderReader& operator=(const EncoderReader&) = delete;

    EncoderValues read_raw() const;
    void set_base();
    EncoderValues read_diff() const;
    EncoderValues base() const { return base_; }

private:
    int fd_ = -1;
    void* map_base_ = nullptr;
    volatile uint32_t* reg_ = nullptr;
    uintptr_t baseaddr_;
    EncoderValues base_{};

    static constexpr uint32_t MAP_SIZE = 4096UL;
    static constexpr uint32_t MAP_MASK = MAP_SIZE - 1;
};

#endif
