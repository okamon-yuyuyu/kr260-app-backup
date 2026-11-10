#include "protocol.hpp"
#include <cassert>
#include <iostream>
int main(){
    using namespace protocol;
    assert(reg6(0x1234,true)==0x12340001);
    assert(req_decode(0x12340001).seq==0x1234);
    assert(req_decode(0x12340001).waiting);
    auto s=step_decode(0x01800e35);
    assert(s.id==28 && s.q==53 && s.pwm && s.valid);
    assert(seq_delta(65535,0)==1);
    assert(seq_delta(24,25)==1);
    assert(seq_delta(24,26)==2);
    std::cout<<"REG5/REG6/REG7 protocol decode test PASS\n";
}

