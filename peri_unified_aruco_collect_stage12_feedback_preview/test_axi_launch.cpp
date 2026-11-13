#include "axi_launch.hpp"
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <string>

int main(){
    {std::istringstream in("manual\n");std::ostringstream out;
     auto x=select_axi_launch("","",in,out);
     assert(x.control=="manual"&&x.command_path.empty());
     assert(out.str().find("Mode (manual/auto): ")!=std::string::npos);}
    {std::istringstream in("bad\nauto\ncommands.txt\n");std::ostringstream out;
     auto x=select_axi_launch("","",in,out);
     assert(x.control=="auto"&&x.command_path=="commands.txt");
     assert(out.str().find("Please enter manual or auto")!=std::string::npos);}
    {std::istringstream in("\nreplay.txt\n");std::ostringstream out;
     auto x=select_axi_launch("auto","",in,out);
     assert(x.control=="auto"&&x.command_path=="replay.txt");}
    {std::istringstream in;std::ostringstream out;
     auto x=select_axi_launch("","already.txt",in,out);
     assert(x.control=="auto"&&x.command_path=="already.txt");}
    {std::istringstream in;std::ostringstream out;
     auto x=select_axi_launch("manual","",in,out);
     assert(x.control=="manual");}
    {std::istringstream in;std::ostringstream out;
     bool ok=false;try{select_axi_launch("manual","already.txt",in,out);}catch(const std::runtime_error&){ok=true;}assert(ok);}
    {std::istringstream in;std::ostringstream out;
     bool ok=false;try{select_axi_launch("","",in,out);}catch(const std::runtime_error&){ok=true;}assert(ok);}
}

