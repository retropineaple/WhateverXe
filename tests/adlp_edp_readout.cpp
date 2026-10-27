#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include "kern_adlp_edp_readout.hpp"
using namespace ADLPEDPReadout;
// Synthetic AUX sink: answers native reads from a DPCD map, optionally
// deferring once, and records every control write.
struct IO {
    std::map<uint32_t,uint32_t> regs;
    std::map<uint32_t,uint8_t> dpcd;
    std::vector<uint32_t> controls;
    unsigned defers {};bool timeout {};
    uint32_t read(uint32_t reg) {return regs.count(reg) ? regs[reg] : 0;}
    void delay() {}
    void write(uint32_t reg,uint32_t v) {
        if(reg!=Aux::Control){regs[reg]=v;return;}
        controls.push_back(v);
        if(!(v&Aux::Busy)){regs[reg]&=~(v&(Aux::Done|Aux::TimeoutError|Aux::ReceiveError));return;}
        assert(((v>>20)&31)==4 && !(v&Aux::TBT) && (v&31)==25 && ((v>>5)&31)==17 && ((v>>26)&3)==3);
        if(timeout){regs[reg]=Aux::Done|Aux::TimeoutError;return;}
        const auto h=regs[Aux::Data];
        assert((h>>28)==9);
        const auto address=(h>>8)&0xFFFFF,len=(h&0xFF)+1;
        uint8_t rx[20] {};uint32_t n=1;
        if(defers){--defers;rx[0]=0x20;}
        else for(uint32_t i=0;i<len;++i)rx[n++]=dpcd.count(address+i) ? dpcd[address+i] : 0;
        for(unsigned w=0;w<5;++w) {
            uint32_t d=0;for(unsigned b=0;b<4;++b)d|=uint32_t(rx[4*w+b])<<(24-8*b);
            regs[Aux::Data+4*w]=d;
        }
        regs[reg]=Aux::Done|(n<<20);
    }
};
int main() {
    {
        IO io;io.regs[0x45444]=3;io.regs[0x64000]=0x80000001;io.defers=1;
        for(uint32_t i=0;i<16;++i){io.dpcd[0x000+i]=uint8_t(0x14+i);io.dpcd[0x700+i]=uint8_t(0xA0+i);}
        Result r;capture(io,r,true);
        assert(r.windowsRead==WindowCount && r.registers[0][0]==0x60000);
        bool sawDdi=false;for(auto &x:r.registers)if(x[0]==0x64000){sawDdi=x[1]==0x80000001;}
        assert(sawDdi);
        assert(r.windows[0].status==uint8_t(AuxStatus::Ack) && r.windows[0].bytes==16 &&
            r.windows[0].data[0]==0x14 && r.windows[0].data[15]==0x23);
        assert(r.windows[11].address==0x700 && r.windows[11].data[0]==0xA0 && r.windows[11].data[2]==0xA2);
        // Every send is followed by a status acknowledge without SEND_BUSY.
        for(size_t i=0;i<io.controls.size();i+=2)assert((io.controls[i]&Aux::Busy) && !(io.controls[i+1]&Aux::Busy));
    }
    {
        // Unpowered AUX: registers only, no AUX writes.
        IO io;io.regs[0x45444]=2;Result r;capture(io,r,true);
        assert(!r.windowsRead && io.controls.empty() && r.windows[0].status==uint8_t(AuxStatus::NoPower));
    }
    {
        // Busy controller (firmware command in flight) is never touched.
        IO io;io.regs[0x45444]=1;io.regs[Aux::Control]=Aux::Busy;Result r;capture(io,r,true);
        assert(!r.windowsRead && io.controls.empty() && r.windows[0].status==uint8_t(AuxStatus::Busy));
    }
    {
        // Timeout stops the window sweep after the first failure.
        IO io;io.regs[0x45444]=1;io.timeout=true;Result r;capture(io,r,true);
        assert(!r.windowsRead && io.controls.size()==2 && r.windows[0].status==uint8_t(AuxStatus::Timeout) &&
            r.windows[1].status==uint8_t(AuxStatus::Unread));
    }
    {
        // -wex-adlp-readout-noaux: registers only.
        IO io;io.regs[0x45444]=1;Result r;capture(io,r,false);
        assert(io.controls.empty() && !r.windowsRead);
    }
    puts("adlp_edp_readout ok");
}
