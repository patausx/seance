#pragma once
#include "../../core/engine/paul_unit.h"
#include <algorithm>
#include <cstdio>
namespace seance::platform::ui3ds::paul_readout {
static_assert(uint8_t(PaulUnit::Direction::Forward)==0&&uint8_t(PaulUnit::Direction::Reverse)==1&&
    uint8_t(PaulUnit::Direction::PingPong)==2,"frozen direction wire IDs");
// Frozen configuration text; numeric DTO values are the public Direction wire IDs.
inline const char* direction_text(uint8_t direction) {
    return direction==1?"reverse":direction==2?"ping-pong":"forward";
}
// Integer formatting does not depend on target-libc floating conversion.
inline void time_text(char* out,size_t n,uint64_t milliseconds) {
    std::snprintf(out,n,"%lu.%03lu s",static_cast<unsigned long>(milliseconds/1000),static_cast<unsigned long>(milliseconds%1000));
}
inline void value_text(char* out,size_t n,int p,q15 v) {
    if(!out||!n)return;
    if(p==PaulUnit::P_STRETCH){const unsigned hundred=unsigned((uint64_t(PaulUnit::stretch_q16(v))*100+32768)/65536);std::snprintf(out,n,"x%u.%02u",hundred/100,hundred%100);}
    else if(p==PaulUnit::P_WINDOW){const unsigned frames=PaulUnit::window_frames(v);std::snprintf(out,n,"%u / %ums",frames,(frames*1000+16000)/32000);}
    else if(p==PaulUnit::P_PITCH){const int cents=PaulUnit::pitch_cents(v);const unsigned magnitude=unsigned(cents<0?-cents:cents);std::snprintf(out,n,"%c%u.%02ust",cents<0?'-':'+',magnitude/100,magnitude%100);}
    else if(p==PaulUnit::P_GATE)std::snprintf(out,n,"%u/4 step",unsigned(PaulUnit::gate_quarters(v)));
    else if(p==PaulUnit::P_FREEZE||p==PaulUnit::P_LOOP)std::snprintf(out,n,"%s",v>16383?"on":"off");
    else if(p<0||p>=PaulUnit::P_COUNT)std::snprintf(out,n,"--");
    else std::snprintf(out,n,"%d%%",std::max(0,int(v))*100/32767);
}
}
