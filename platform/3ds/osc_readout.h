#pragma once
// Pure mode-aware physical readouts. No captured frame, Engine or audio access.
#include "../../core/engine/units.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace seance::platform::ui3ds::osc_readout {
inline double midi(q15 value,bool clean,bool quantize,MusicalScale scale={}) {
    const int raw=std::max(0,int(value));
    if(quantize&&musical_scale::active(scale))
        return (clean?0:12)+musical_scale::snap_cents(scale,2400+(raw*7200>>15),24,96);
    if(!clean){
        // The preserved integer helper starts from C0 as if it were MIDI0.
        const int cents=2400+(raw*7200>>15);
        return 12+(cents+(quantize?50:0))/100;
    }
    const double note=24.0+double(raw)*72.0/32768.0;
    return quantize?std::floor(note+0.5):note;
}
inline double hz(q15 value,bool clean,bool quantize,MusicalScale scale={}) {
    return 440.0*std::pow(2.0,(midi(value,clean,quantize,scale)-69.0)/12.0);
}
inline bool value_text(char* out,size_t size,int param,q15 value,bool clean,bool quantize,MusicalScale scale={}) {
    if(param==OscUnit::P_PITCH){
        static const char* notes[]={"c","c#","d","d#","e","f","f#","g","g#","a","a#","b"};
        const double pitch=midi(value,clean,quantize,scale);
        const int note=int(std::floor(pitch+0.5)),cents=int(std::lround((pitch-note)*100));
        if(clean&&!quantize)std::snprintf(out,size,"%s%d%+03d / %.1fhz",notes[note%12],note/12-1,cents,hz(value,clean,quantize,scale));
        else std::snprintf(out,size,"%s%d / %.1fhz",notes[note%12],note/12-1,hz(value,clean,quantize,scale));
        return true;
    }
    if(param==OscUnit::P_PW){
        std::snprintf(out,size,"%.1f%%",25.0+double(std::max(0,int(value)))*(clean?50.0/32767.0:25.0/32768.0));
        return true;
    }
    if(param==OscUnit::P_SUB){
        std::snprintf(out,size,"%u%% / %s -1",unsigned((std::max(0,int(value))*100+16383)/32767),clean?"sine":"square");return true;
    }
    return false;
}
} // namespace seance::platform::ui3ds::osc_readout
