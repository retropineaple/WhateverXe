#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "kern_vbt.hpp"
#include "kern_display13_firmware.hpp"
#include "kern_display13_fb_topology.hpp"
using namespace XELP;
using namespace XELP::VBT;
static void w16(std::vector<uint8_t>&b,size_t at,uint16_t v){b[at]=uint8_t(v);b[at+1]=uint8_t(v>>8);}
static void w32(std::vector<uint8_t>&b,size_t at,uint32_t v){w16(b,at,uint16_t(v));w16(b,at+2,uint16_t(v>>16));}
// General Definitions plus optional panel blocks 40/27/44 (BDB 250).
static std::vector<uint8_t> table(const std::vector<Port>&ports,bool panel) {
    const size_t children=5+39*ports.size();
    const size_t blocks=3+children+(panel?(3+1)+(3+212)+(3+56):0);
    std::vector<uint8_t> b(48+22+blocks);
    memcpy(b.data(),"$VBT",4);w16(b,20,100);w16(b,22,48);w16(b,24,uint16_t(b.size()));w32(b,28,48);
    memcpy(b.data()+48,"BIOS_DATA_BLOCK ",16);w16(b,64,250);w16(b,66,22);w16(b,68,uint16_t(b.size()-48));
    size_t at=70;b[at]=2;w16(b,at+1,uint16_t(children));b[at+3+4]=39;
    for(size_t i=0;i<ports.size();++i) {
        auto c=at+3+5+39*i;const auto &p=ports[i];w16(b,c+2,uint16_t(p.type));
        b[c+16]=uint8_t(p.dvo);b[c+19]=uint8_t(p.ddc);b[c+25]=uint8_t(p.aux);b[c+23]=uint8_t(p.flags);b[c+33]=uint8_t(p.capabilities);
    }
    at+=3+children;
    if(panel) {
        b[at]=40;w16(b,at+1,1);b[at+3]=2;at+=4;          // panel type 2
        b[at]=27;w16(b,at+1,212);w16(b,at+3+20,2100);at+=3+212; // T1+T3 for type 2
        b[at]=44;w16(b,at+1,56);at+=3+56;
    }
    return b;
}
int main() {
    // HP Envy x360 13-bf shape: internal DP on Combo A and B, then a later
    // child for B again. Strict parsing keeps the upstream Conflict verdict.
    const std::vector<Port> envy {{0x1806,10,3,0x40,0xc0,0},{0x1806,7,0,0x10,0xc0,0},{0x60d2,1,2,0,0,0}};
    auto bytes=table(envy,true);
    auto strict=parse(bytes.data(),bytes.size(),Family::AlderLakeP);
    assert(strict.status==Status::Conflict && !display13PanelDP(strict));
    auto tolerant=parse(bytes.data(),bytes.size(),Family::AlderLakeP,true);
    assert(tolerant.valid() && tolerant.present==3 && tolerant.duplicates==1);
    assert(tolerant.port[1].type==0x1806); // first child for B wins, as in Linux
    assert(tolerant.panel.valid && tolerant.panel.ddi==0 && tolerant.panel.type==2 && tolerant.panel.delays[0]==2100);
    assert(display13PanelDP(tolerant)==1);
    // Two internal panels without preferA remain unassociated.
    auto twoPanels=parse(bytes.data(),bytes.size(),Family::AlderLakeP,false);
    assert(!twoPanels.panel.valid);
    // The Combo A DP route is not offered for a non-A panel, a shared AUX A,
    // an HDMI A record or a VBT without panel blocks.
    // Panel blocks are optional (PnP-selected panel type 0xFF): the route only
    // needs the internal DP child on Combo A.
    auto noPanel=table(envy,false);auto q=parse(noPanel.data(),noPanel.size(),Family::AlderLakeP,true);
    assert(q.valid() && !q.panel.valid && display13PanelDP(q)==1);
    auto shared=tolerant;shared.present|=1U<<3;shared.port[3]={0x68c6,9,0,0x40,0xc0,0};
    assert(!display13PanelDP(shared));
    auto hdmiA=tolerant;hdmiA.port[0].type=0x60d2;assert(!display13PanelDP(hdmiA));
    auto bPanel=tolerant;bPanel.panel.ddi=1;assert(!display13PanelDP(bPanel));

    // Wiring: Combo A fixed DP is admitted only with the explicit comboDP mask.
    Display13OutputWiring w;w.valid=true;w.dp=1;
    assert(w.described() && !Display13FBTopology::supportedWiring(w,true) &&
        Display13FBTopology::supportedWiring(w,true,1));
    assert(!Display13FBTopology::supportedWiring(w,true,2)); // Combo B is not part of this route.
    auto b=w;b.dp=2;assert(!Display13FBTopology::supportedWiring(b,true,1));
    // The firmware DP GOP on DDI A selects the boot connector only via DP.
    assert(Display13FBTopology::bootDDI(w,0x8A010106,true)==0);
    assert(Display13FBTopology::bootDDI(w,0x8A010106,false)==UINT32_MAX);
    // Native platform: the panel record becomes the fixed Combo DP record.
    std::vector<uint8_t> in(Display13FBTopology::Bytes),out(Display13FBTopology::Bytes);
    in[0x12]=9;
    for(unsigned d=0;d<9;++d) {
        uint32_t rec[6]={d,d<3?d+1:d+6,1,d<3?0U:1U,0x400,0x201};
        if(!d){uint32_t panel[6]={0,0,0,0,2,0x18};memcpy(rec,panel,24);}
        if(d==1){uint32_t comboB[6]={1,2,1,0,0x400,0x201};memcpy(rec,comboB,24);}
        memcpy(in.data()+0x20+24*d,rec,24);
    }
    assert(Display13FBTopology::apply(out.data(),Display13FBTopology::Bytes,in.data(),w));
    const uint32_t expectA[6]={0,1,1,0,0x400,0x201};
    assert(!memcmp(out.data()+0x20,expectA,24));
    assert(w.matches(reinterpret_cast<const uint32_t *>(out.data()+0x20)));
    uint32_t excluded[2];memcpy(excluded,out.data()+0x20+24+16,8);assert(excluded[0]==1 && excluded[1]==0);
    // Hotplug keeps Combo A and masks Combo B/C plus every TC source.
    assert(Display13FBTopology::excludedHotplug(out.data(),Display13FBTopology::Bytes,w)==(0x3F000000U|(6U<<16)));
    // The real HP Envy x360 13-bf0059tu VBT (BDB 247, panel_type 0xFF).
    std::string fixture=__FILE__;fixture=fixture.substr(0,fixture.rfind('/')+1)+"fixtures/hp-envy-x360-13-bf-vbt.bin";
    if(FILE *f=fopen(fixture.c_str(),"rb")) {
        std::vector<uint8_t> raw(65536);raw.resize(fread(raw.data(),1,raw.size(),f));fclose(f);
        auto real=parse(raw.data(),raw.size(),Family::AlderLakeP,true);
        assert(real.valid() && real.version==247 && real.duplicates==1 && !real.panel.valid);
        assert(real.present==(3U|(0xFU<<3)) && real.port[0].type==0x1806 && real.port[0].aux==0x40);
        assert(real.port[1].type==0x1806); // eDP-B placeholder wins over the later HDMI-B child
        assert(display13PanelDP(real)==1);
        auto fixed=display13Fixed(real,Display13OutputWiring::Physical);
        assert(fixed.valid && !fixed.hdmi && !fixed.dp); // TC children are Type-C capable, not fixed
        fixed.dp|=display13PanelDP(real);
        assert(Display13FBTopology::supportedWiring(fixed,true,1) && fixed.selected()==1);
        assert(parse(raw.data(),raw.size(),Family::AlderLakeP).status==Status::Conflict);
    } else assert(!"missing VBT fixture");
    puts("adlp_panel_dp ok");
}
