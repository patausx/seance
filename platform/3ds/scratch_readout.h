#pragma once
#include "../../core/engine/units.h"
#include <algorithm>
#include <cstdio>
namespace seance::platform::ui3ds::scratch_readout {
// Stateless physical units shared by SCRT, inspector and independent chain UI.
inline void value_text(char* out,size_t n,int p,q15 v) {
    if(!out||!n)return;
    if(p==ScratchUnit::P_SPEED)std::snprintf(out,n,"%+.2fx",double(ScratchUnit::speed_q16(v))/65536.0);
    else if(p==ScratchUnit::P_PITCH)std::snprintf(out,n,"%+.2fst",double(ScratchUnit::pitch_cents(v))/100.0);
    else if(p==ScratchUnit::P_WINDOW)std::snprintf(out,n,"%u smp",unsigned(ScratchUnit::window_frames(v)));
    else if(p==ScratchUnit::P_GATE)std::snprintf(out,n,"%u/4 step",unsigned(ScratchUnit::gate_quarters(v)));
    else if(p<0||p>=ScratchUnit::P_COUNT)std::snprintf(out,n,"--");
    else std::snprintf(out,n,"%d%%",std::max(0,int(v))*100/32767);
}
}
