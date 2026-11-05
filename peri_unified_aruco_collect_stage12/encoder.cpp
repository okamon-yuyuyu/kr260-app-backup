#include "encoder.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
namespace {
constexpr std::uintptr_t BASE16=0xA0000000UL;
constexpr std::uintptr_t BASE12=0xA0030000UL;
}
EncoderReader::EncoderReader(){
    const long page=sysconf(_SC_PAGESIZE);
    if(page<=0)throw std::runtime_error("sysconf page size failed");
    length_=static_cast<std::size_t>(page);
    fd_=::open("/dev/mem",O_RDONLY|O_SYNC);
    if(fd_<0)throw std::runtime_error(std::string("open encoder /dev/mem: ")+strerror(errno));
    map16_=mmap(nullptr,length_,PROT_READ,MAP_SHARED,fd_,static_cast<off_t>(BASE16));
    if(map16_==MAP_FAILED){map16_=nullptr; const auto why=std::string(strerror(errno));::close(fd_);fd_=-1;throw std::runtime_error("mmap 16-bit encoder: "+why);}
    map12_=mmap(nullptr,length_,PROT_READ,MAP_SHARED,fd_,static_cast<off_t>(BASE12));
    if(map12_==MAP_FAILED){map12_=nullptr;const auto why=std::string(strerror(errno));munmap(map16_,length_);map16_=nullptr;::close(fd_);fd_=-1;throw std::runtime_error("mmap 12-bit encoder: "+why);}
    regs16_=static_cast<volatile const std::uint32_t*>(map16_);
    regs12_=static_cast<volatile const std::uint32_t*>(map12_);
}
EncoderReader::~EncoderReader(){
    if(map12_)munmap(map12_,length_);
    if(map16_)munmap(map16_,length_);
    if(fd_>=0)::close(fd_);
}
EncoderValues EncoderReader::read_raw() const {
    const auto a=regs16_[0], b=regs16_[1];
    return {static_cast<std::int32_t>(a&0xffffu),static_cast<std::int32_t>((a>>16)&0xffffu),
            static_cast<std::int32_t>(b&0xffffu),static_cast<std::int32_t>((b>>16)&0xffffu)};
}
EncoderValues EncoderReader::read_raw12() const {
    const auto a=regs12_[0], b=regs12_[1];
    return {static_cast<std::int32_t>(a&0xfffu),static_cast<std::int32_t>((a>>12)&0xfffu),
            static_cast<std::int32_t>(b&0xfffu),static_cast<std::int32_t>((b>>12)&0xfffu)};
}
void EncoderReader::set_base(){base_=read_raw();}
EncoderSample EncoderReader::sample() const {
    EncoderSample s;s.raw=read_raw();s.raw12=read_raw12();s.base=base_;
    s.diff={s.raw.enc0-base_.enc0,s.raw.enc1-base_.enc1,s.raw.enc2-base_.enc2,s.raw.enc3-base_.enc3};
    s.valid=true;return s;
}
