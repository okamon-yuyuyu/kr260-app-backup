#include "command.hpp"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

struct ExpectedPosture {
    int command_id;
    uint8_t stick3;
    int bend_offset;
};

constexpr std::array<ExpectedPosture, 17> kExpected{{
    {1, 7, -4000}, {12, 8, -3500}, {2, 9, -3000},
    {23, 10, -2500}, {3, 11, -2000}, {34, 12, -1500},
    {4, 13, -1000}, {45, 14, -500}, {5, 15, 0},
    {56, 16, 500}, {6, 17, 1000}, {67, 18, 1500},
    {7, 19, 2000}, {78, 20, 2500}, {8, 21, 3000},
    {89, 22, 3500}, {9, 23, 4000},
}};

}  // namespace

int main() {
    for (const auto& expected : kExpected) {
        uint8_t posture = 0;
        if (!posture_from_command_id(expected.command_id, posture)) {
            std::cerr << "mapping missing for cmd" << expected.command_id << '\n';
            return 1;
        }
        if (posture != expected.stick3) {
            std::cerr << "unexpected stick3 for cmd" << expected.command_id
                      << ": " << static_cast<int>(posture) << '\n';
            return 1;
        }

        const PeristalsisCommand cmd = make_posture_command(expected.command_id);
        const int actual_offset =
            (static_cast<int>(cmd.stick3) - 15) * static_cast<int>(cmd.rom2);
        if (cmd.rom2 != 500 || cmd.rom3 != 500 ||
            actual_offset != expected.bend_offset) {
            std::cerr << "unexpected ROM/offset for cmd" << expected.command_id
                      << ": rom2=" << cmd.rom2
                      << " rom3=" << cmd.rom3
                      << " offset=" << actual_offset << '\n';
            return 1;
        }

        const PeristalsisCommand go = make_go_command(posture);
        if (go.stick3 != posture || go.rom2 != 500 || go.rom3 != 500) {
            std::cerr << "go command did not preserve cmd" << expected.command_id
                      << " posture/ROM\n";
            return 1;
        }
    }

    uint8_t unused = 0;
    for (const int invalid : {0, 10, 11, 13, 44, 46, 90, 100, 900}) {
        if (posture_from_command_id(invalid, unused)) {
            std::cerr << "invalid command accepted: " << invalid << '\n';
            return 1;
        }
    }

    std::cout << "PASS: 17 posture mappings and go-command ROM persistence\n";
    return 0;
}
