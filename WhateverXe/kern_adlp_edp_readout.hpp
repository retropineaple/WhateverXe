// ADL-P eDP boot readout: firmware-programmed panel path facts for the
// -wex-adlp-readout diagnostic. Register offsets from Linux v6.12
// intel_display_regs/intel_pps_regs/intel_backlight_regs/intel_psr_regs and
// intel_dp_aux_regs. No pipe, PLL, PPS or backlight state is modified.
// The only writes are native AUX DPCD reads on an idle, powered AUX A.
#ifndef KERN_ADLP_EDP_READOUT_HPP
#define KERN_ADLP_EDP_READOUT_HPP
#include <stdint.h>
#include <stddef.h>
namespace ADLPEDPReadout {
// Pipe/transcoder A, DDI A, PPS 0, backlight controllers 0/1, PSR on
// transcoder A, DPLL0/1 and DDI A clock routing, Combo PHY A.
constexpr uint32_t Registers[] {
    // Transcoder A timings, M/N, DDI function, MSA, DP transport.
    0x60000,0x60004,0x60008,0x6000C,0x60010,0x60014,0x6001C,
    0x60030,0x60034,0x60040,0x60044,0x60400,0x60404,0x60410,0x60540,0x60544,
    // Pipe A configuration, primary plane A control/stride/surface/size.
    0x70008,0x70030,0x70180,0x70188,0x7019C,0x70190,0x70198,
    // PSR1/PSR2 on transcoder A.
    0x60800,0x60840,0x60900,0x60940,
    // DDI A buffer and AUX A control.
    0x64000,0x64010,
    // Power wells: PW1/2 and DDI/AUX IO.
    0x45404,0x45444,0x45454,
    // CDCLK, DPLL0/1 enable, DPCLKA_CFGCR0, DPLL0/1 CFGCR0/1.
    0x46000,0x46010,0x46014,0x164280,0x164284,0x164288,0x16428C,0x164290,
    // PPS 0: status, control, on/off delays, divisor.
    0xC7200,0xC7204,0xC7208,0xC720C,0xC7210,
    // Backlight controllers 0/1 (CNP-style): control, frequency, duty.
    0xC8250,0xC8254,0xC8258,0xC8350,0xC8354,0xC8358,
    // South chicken / utility pin.
    0xC2000,0x48400,
    // Combo PHY A: CL_DW5/DW10, COMP_DW0/1/3/8/9/10, lane 0..3 TX/PCS.
    0x162014,0x162028,0x162100,0x162104,0x16210C,0x162120,0x162124,0x162128,
    0x162804,0x162888,0x162890,0x162894,0x16289C,
    0x162990,0x162A90,0x162B90,
};
constexpr unsigned RegisterCount=sizeof(Registers)/sizeof(Registers[0]);

// DPCD windows captured with native AUX reads (16 bytes each).
constexpr uint32_t Windows[] {
    0x000,0x010,0x060,0x070,0x090,0x100,0x110,0x170,0x200,0x2002,
    0x600,0x700,0x710,0x720,0x730,0x740,0x750,0x760,0x770,
};
constexpr unsigned WindowCount=sizeof(Windows)/sizeof(Windows[0]);

enum class AuxStatus : uint8_t {Unread,Ack,Nack,Defer,Timeout,ReceiveError,Busy,NoPower,Short};
struct Window {uint32_t address;uint8_t status,bytes,reserved[2];uint8_t data[16];};
static_assert(sizeof(Window)==24,"published layout");

struct Aux {
    static constexpr uint32_t Control=0x64010,Data=0x64014;
    static constexpr uint32_t Busy=1U<<31,Done=1U<<30,TimeoutError=1U<<28,ReceiveError=1U<<25;
    static constexpr uint32_t TBT=1U<<11;
    // Linux v6.12 skl_get_aux_send_ctl with max timeout and the
    // 18/26-cycle firmware/hardware sync lengths.
    static constexpr uint32_t send(uint32_t bytes) {
        return Busy|Done|TimeoutError|(3U<<26)|ReceiveError|((bytes&31)<<20)|(17U<<5)|25U;
    }
    template<class IO> static AuxStatus transfer(IO &io,const uint8_t *tx,uint32_t txBytes,
                                                 uint8_t *rx,uint32_t &rxBytes) {
        rxBytes=0;
        if(txBytes<1 || txBytes>20)return AuxStatus::Short;
        const auto before=io.read(Control);
        if(before==UINT32_MAX || (before&Busy))return AuxStatus::Busy;
        if(before&TBT)return AuxStatus::Busy; // not a combo-routed AUX
        for(unsigned w=0;w<5;++w) {
            uint32_t v=0;
            for(unsigned b=0;b<4;++b) {
                const auto i=4*w+b;
                v|=uint32_t(i<txBytes ? tx[i] : 0)<<(24-8*b);
            }
            io.write(Data+4*w,v);
        }
        io.write(Control,send(txBytes));
        uint32_t status=Busy;
        for(unsigned spin=0;spin<200 && (status&Busy);++spin){io.delay();status=io.read(Control);}
        // Acknowledge DONE/error status as Linux does after each transfer.
        io.write(Control,status&(Done|TimeoutError|ReceiveError));
        if(status&Busy)return AuxStatus::Timeout;
        if(status&TimeoutError)return AuxStatus::Timeout;
        if(status&ReceiveError)return AuxStatus::ReceiveError;
        auto n=(status>>20)&31;
        if(!n || n>20)return AuxStatus::Short;
        for(unsigned i=0;i<n;++i) {
            const auto v=io.read(Data+4*(i/4));
            rx[i]=uint8_t(v>>(24-8*(i%4)));
        }
        rxBytes=n;
        return AuxStatus::Ack;
    }
    template<class IO> static AuxStatus read(IO &io,uint32_t address,uint8_t *out,uint8_t &bytes) {
        bytes=0;
        const uint8_t tx[4] {uint8_t(0x90|((address>>16)&15)),uint8_t(address>>8),uint8_t(address),15};
        for(unsigned attempt=0;attempt<7;++attempt) {
            uint8_t rx[20] {};uint32_t n=0;
            const auto s=transfer(io,tx,4,rx,n);
            if(s!=AuxStatus::Ack)return s;
            const auto reply=(rx[0]>>4)&3;
            if(reply==2){for(unsigned i=0;i<5;++i)io.delay();continue;}
            if(reply==1)return AuxStatus::Nack;
            bytes=uint8_t(n-1);
            for(unsigned i=0;i+1<n && i<16;++i)out[i]=rx[i+1];
            return AuxStatus::Ack;
        }
        return AuxStatus::Defer;
    }
};

struct Result {
    uint32_t version {1},auxBefore {},auxPower {},windowsRead {};
    uint32_t registers[RegisterCount][2] {};
    Window windows[WindowCount] {};
};

// IO: read/write/delay (delay ~100us). AUX is attempted only when the AUX A
// power well reports enabled and the controller is idle and combo-routed.
template<class IO> void capture(IO &io,Result &r,bool dpcd) {
    for(unsigned i=0;i<RegisterCount;++i){r.registers[i][0]=Registers[i];r.registers[i][1]=io.read(Registers[i]);}
    r.auxPower=io.read(0x45444);r.auxBefore=io.read(Aux::Control);
    for(unsigned i=0;i<WindowCount;++i)r.windows[i].address=Windows[i];
    if(!dpcd)return;
    const bool powered=r.auxPower!=UINT32_MAX && (r.auxPower&1);
    for(unsigned i=0;i<WindowCount;++i) {
        auto &w=r.windows[i];
        if(!powered){w.status=uint8_t(AuxStatus::NoPower);continue;}
        uint8_t n=0;
        w.status=uint8_t(Aux::read(io,w.address,w.data,n));w.bytes=n;
        if(w.status==uint8_t(AuxStatus::Ack))++r.windowsRead;
        else if(w.status==uint8_t(AuxStatus::Busy) || w.status==uint8_t(AuxStatus::Timeout))break;
    }
}
}
#endif
