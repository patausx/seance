#pragma once
// Value formatting shared by the frozen REV inspector and modulation lens.
// These are settings, not a measured impulse response or measured RT60.
#include "../../core/engine/reverb_dsp.h"
#include <algorithm>
#include <cstdio>

namespace seance::platform::ui3ds::rev_readout {
inline unsigned raw(q15 value) { return unsigned(std::max(0, int(value))); }
inline unsigned legacy_length(q15 size, int lane) {
    constexpr unsigned offset[3]={733,997,1201}, span[3]={1499,1901,2309}, cap[3]={2000,2400,3000};
    lane=std::max(0,std::min(lane,2));
    return std::min(cap[lane],offset[lane]+((span[lane]*(16384u+raw(size)))>>16));
}
inline unsigned milliseconds(unsigned samples) { return (samples*1000u+ENGINE_SR/2)/ENGINE_SR; }
inline void value(char* out,size_t size,int param,q15 v,bool modern) {
    const unsigned n=raw(v);
    if(param==0 && !modern) {
        std::snprintf(out,size,"%u-%ums",milliseconds(legacy_length(v,0)),milliseconds(legacy_length(v,2)));
    } else if(param==1) {
        if(modern) {
            const unsigned hundredths=(reverb_dsp::decay_ms(v)+5)/10;
            std::snprintf(out,size,"%u.%02us",hundredths/100,hundredths%100);
        } else {
            const unsigned fb=6000u+((n*24000u)>>15), tenth=(fb*1000u+16384u)/32768u;
            std::snprintf(out,size,"%u.%u%%",tenth/10,tenth%10);
        }
    } else if(param==4) std::snprintf(out,size,"%s",n>16384?"on":"off");
    else if(param==5 && modern) std::snprintf(out,size,"%ums",unsigned(reverb_dsp::predelay_ms(v)));
    else if(param==6 && modern) {
        const unsigned hz=reverb_dsp::low_cut_hz(v);
        if(hz)std::snprintf(out,size,"%uhz",hz);else std::snprintf(out,size,"off");
    } else if(param==7 && modern) std::snprintf(out,size,"%u%%",unsigned(reverb_dsp::duck_percent(v)));
    else std::snprintf(out,size,"%u%%",(n*100u+16383u)/32767u);
}
} // namespace seance::platform::ui3ds::rev_readout
