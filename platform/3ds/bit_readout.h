#pragma once
#include "../../core/engine/units.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace seance::platform::ui3ds::bit_readout {
// Scalar/version-explicit: safe for score locks, never borrows another card's UI frame.
inline void value_text(char* out,size_t size,int p,q15 value,bool modern){
    using C=BitUnit;
    if(p<0||p>=(modern?C::P_COUNT:C::LEGACY_COUNT)){std::snprintf(out,size,"--");return;}
    const int v=std::max(0,int(value));
    if(!modern){
        if(p==C::P_BITS)std::snprintf(out,size,"%ubits",unsigned(3+((v*13)>>15)));
        else if(p==C::P_RATE){const unsigned hold=1+unsigned((32767-v)*31/32767);
            std::snprintf(out,size,"%uhz",unsigned((32000+hold/2)/hold));}
        else if(p==C::P_JITTER)std::snprintf(out,size,"-%u/+%usmp",unsigned(v*128/1048576),unsigned(v*127/1048576));
        else if(p==C::P_DRIFT){const int raw=std::max(-32767,int(value));
            std::snprintf(out,size,"%+.1fhz",double(raw)*32000.0/8388608.0);}
        else std::snprintf(out,size,"%u%%",unsigned((v*100+16383)/32767));
        return;
    }
    if(p==C::P_BITS)std::snprintf(out,size,"%dbit",C::bit_depth(value));
    else if(p==C::P_RATE||p==C::P_TONE){const uint32_t mhz=p==C::P_RATE?C::rate_millihz(value):C::tone_millihz(value);
        if(!mhz)std::snprintf(out,size,"off");
        else if(mhz>=1000000){const unsigned hundredths=(mhz+5000)/10000;
            std::snprintf(out,size,"%u.%02ukhz",hundredths/100,hundredths%100);}
        else {const unsigned tenth=(mhz+50)/100;std::snprintf(out,size,"%u.%uhz",tenth/10,tenth%10);}}
    else if(p==C::P_DRIVE||p==C::P_GAIN){const int db=p==C::P_DRIVE?C::drive_centidb(value):C::gain_centidb(value);
        std::snprintf(out,size,"%c%d.%02ddb",db<0?'-':'+',std::abs(db)/100,std::abs(db)%100);}
    else if(p==C::P_DRIFT){const int hz=C::drift_millihz(value),mag=std::abs(hz);
        if(value==0)std::snprintf(out,size,"0 / off");
        else if(mag<10)std::snprintf(out,size,"%c<0.01hz",value<0?'-':'+');
        else std::snprintf(out,size,"%c%d.%02dhz",value<0?'-':'+',mag/1000,(mag%1000)/10);}
    else if(p==C::P_DROP_TIME){const uint32_t tenth=(uint64_t(C::drop_frames(value))*10000+ENGINE_SR/2)/ENGINE_SR;
        std::snprintf(out,size,"%u.%ums",unsigned(tenth/10),unsigned(tenth%10));}
    else std::snprintf(out,size,"%u%%",unsigned((uint32_t(v)*100+16383)/32767));
}
} // namespace seance::platform::ui3ds::bit_readout
