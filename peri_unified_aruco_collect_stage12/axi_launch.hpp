#ifndef AXI_LAUNCH_HPP
#define AXI_LAUNCH_HPP
#include <iosfwd>
#include <string>
struct AxiLaunch {
    std::string control;       // manual or auto (AXI-only sub-mode)
    std::string command_path;  // required in auto; empty in manual
};
AxiLaunch select_axi_launch(const std::string& specified_control,
                            const std::string& specified_path,
                            std::istream& input,
                            std::ostream& output);
#endif
