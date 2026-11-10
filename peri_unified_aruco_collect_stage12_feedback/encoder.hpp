#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

struct EncoderValues {
    std::int32_t enc0=0, enc1=0, enc2=0, enc3=0;
};
struct EncoderSample {
    bool valid=false;
    EncoderValues raw,raw12,diff,base;
};
// Matches the original peri_AXI_collect_safe registers and bit packing.
class EncoderReader {
public:
    EncoderReader();
    ~EncoderReader();
    EncoderReader(const EncoderReader&)=delete;
    EncoderReader& operator=(const EncoderReader&)=delete;
    void set_base();
    EncoderSample sample() const;
    EncoderValues base() const { return base_; }
private:
    int fd_=-1;
    void* map16_=nullptr;
    void* map12_=nullptr;
    std::size_t length_=0;
    volatile const std::uint32_t* regs16_=nullptr;
    volatile const std::uint32_t* regs12_=nullptr;
    EncoderValues base_{};
    EncoderValues read_raw() const;
    EncoderValues read_raw12() const;
};

