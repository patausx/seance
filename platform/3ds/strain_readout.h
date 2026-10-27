#pragma once
#include "../../core/engine/units.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace seance::platform::ui3ds::strain_readout {
inline void value_text(char* out,size_t size,int p,q15 value,bool modern){
    using C=StrainUnit;
    if(p<0||p>=(modern?C::P_COUNT:C::LEGACY_COUNT)){std::snprintf(out,size,"--");return;}
    const int v=std::max(0,int(value));
    if(p==C::P_DRIVE)std::snprintf(out,size,"%.2fx",double(32767+v*7)/32768.0);
    else if(p==C::P_BIAS){
        const int raw=std::max(-32767,int(value));
        const int amount=(std::abs(raw)*100+16383)/32767*(raw<0?-1:1);
        if(amount)std::snprintf(out,size,"%+d%%",amount);else std::snprintf(out,size,"0%%");
    }else if(p==C::P_TRIM){
        const int db=C::trim_centidb(value),tenths=(std::abs(db)+5)/10;
        std::snprintf(out,size,"%s%d.%ddb",db<0&&tenths?"-":db>0&&tenths?"+":"",tenths/10,tenths%10);
    }else if(p==C::P_RATE){
        const uint32_t millihz=(C::rate_microhz(value)+500)/1000;
        std::snprintf(out,size,"%u.%03uhz",unsigned(millihz/1000),unsigned(millihz%1000));
    }else std::snprintf(out,size,"%u%%",unsigned((v*100+16383)/32767));
}
inline void cycle_text(char* out,size_t size,q15 rate){
    const auto hz=StrainUnit::rate_microhz(rate);
    std::snprintf(out,size,hz>=1000000?"%.3fs":"%.2fs",1000000.0/double(hz));
}
} // namespace seance::platform::ui3ds::strain_readout
