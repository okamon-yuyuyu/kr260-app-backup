#include "axi_launch.hpp"
#include <istream>
#include <ostream>
#include <stdexcept>

namespace {
std::string input_line(std::istream& in,std::ostream& out,const char* prompt){
    out<<prompt<<std::flush;
    std::string line;
    if(!std::getline(in,line))throw std::runtime_error("Input closed during AXI mode selection");
    return line;
}
}

AxiLaunch select_axi_launch(const std::string& specified_control,
                            const std::string& specified_path,
                            std::istream& in,
                            std::ostream& out){
    AxiLaunch result{specified_control,specified_path};
    // Maintain non-interactive Stage 11 CLI: --commands file implies AXI auto.
    if(result.control.empty()&&!result.command_path.empty()){
        result.control="auto";
        out<<"AXI AUTO selected from --commands.\n";
    }
    while(result.control.empty()){
        result.control=input_line(in,out,"Mode (manual/auto): ");
        if(result.control!="manual"&&result.control!="auto"){
            out<<"Please enter manual or auto.\n";
            result.control.clear();
        }
    }
    if(result.control!="manual"&&result.control!="auto")
        throw std::runtime_error("AXI control must be manual or auto");
    if(result.control=="manual"){
        if(!result.command_path.empty())
            throw std::runtime_error("AXI manual cannot use --commands; select auto instead");
        return result;
    }
    while(result.command_path.empty()){
        result.command_path=input_line(in,out,"Command txt path: ");
        if(result.command_path.empty())out<<"Enter a command-file path.\n";
    }
    return result;
}

