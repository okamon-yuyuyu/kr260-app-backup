#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Live four-channel encoder/target monitor as in old peri_AXI_collect_safe.
// READ ONLY: actual target_raw registers are written by Hardware::prepare().
class WireAdjuster {
public:
    WireAdjuster();
    ~WireAdjuster();
    WireAdjuster(const WireAdjuster&) = delete;
    WireAdjuster& operator=(const WireAdjuster&) = delete;

    // Show live encoder and current target_raw, ask user to confirm physical adjustment.
    // Returns false if Ctrl+C is received; throws if the user cancels or hardware fails.
    bool wait_user_confirm() const;

private:
    struct Snapshot {
        std::array<std::uint16_t, 4> encoder{};
        std::array<std::uint16_t, 4> target{};
    };
    Snapshot snapshot() const;

    int fd_ = -1;
    std::size_t map_len_ = 0;
    void* enc_map_ = nullptr;
    void* wire_map_ = nullptr;
    volatile const std::uint32_t* enc_reg_ = nullptr;
    volatile const std::uint32_t* wire_reg_ = nullptr;
};
