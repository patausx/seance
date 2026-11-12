#pragma once
// ROT parameter text shared by the editor and the chains card. Pure formatting:
// no Engine, PCM, GPU or allocation.
#include "../../core/engine/rot_unit.h"
#include <algorithm>
#include <cstdio>
namespace seance::platform::ui3ds::rot_readout {
inline void value_text(char* out,size_t n,int p,q15 v){
    if(unsigned(p)>=12){std::snprintf(out,n,"--");return;}
    if(p==RotUnit::P_PITCH)std::snprintf(out,n,"%+dst",RotUnit::pitch_semitones(v));
    else if(p==RotUnit::P_LENGTH){const unsigned ms=unsigned(RotUnit::length_frames(v)/32);std::snprintf(out,n,"%u.%03us",ms/1000,ms%1000);}
    else std::snprintf(out,n,"%u%%",unsigned(std::max(0,int(v)))*100/32767);
}
}
