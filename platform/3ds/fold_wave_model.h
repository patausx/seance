#pragma once
// UI-only reference-wave model of the unchanged Fold. Never called from audio.
// Two fixed cycles, 0.75FS, no normalization/DC removal/oversampling of audio.
#include "../../core/engine/units.h"
#include <cmath>
#include <cstdint>

namespace seance::platform::ui3ds::fold_wave_model {
constexpr int X0=28, X1=306, ZERO=132, HEIGHT=36;
constexpr int COLUMNS=X1-X0+1, OVERSAMPLE=8;
constexpr int SAMPLES=(COLUMNS-1)*OVERSAMPLE+1;
constexpr int PARAMS=5;
static_assert(FoldUnit::P_DRIVE==0 && FoldUnit::P_TRIM==1 && FoldUnit::P_MODE==2 &&
              FoldUnit::P_BIAS==3 && FoldUnit::P_MIX==4,"unchanged Fold parameter order");

inline int clamp(int value,int lo=0,int hi=32767) {
    return value<lo?lo:value>hi?hi:value;
}
inline q15 sample(q15 dry,const q15* p) {
    // Exactly the existing integer shaper for legal parameter values, including
    // sign conventions and near-wet/unity Q15 endpoints. Validated against DSP.
    const int32_t drive=32767+clamp(p[FoldUnit::P_DRIVE])*7;
    const int32_t bias=clamp(p[FoldUnit::P_BIAS],-32767);
    const int32_t x=int32_t((int64_t(dry)*drive)>>15)+bias;
    int32_t folded=x;
    for(int k=0;k<8 && (folded>32767 || folded<-32768);++k) {
        if(folded>32767)folded=65534-folded;
        if(folded<-32768)folded=-65536-folded;
    }
    const int32_t wrapped=((x+32768)&0xffff)-32768;
    const int32_t diode=x<0?-(x>>2):x;
    const int32_t mode=clamp(p[FoldUnit::P_MODE]);
    int32_t shaped=mode<16384?folded+int32_t((int64_t(wrapped-folded)*mode)>>14):
        wrapped+int32_t((int64_t(diode-wrapped)*(mode-16384))>>14);
    shaped=int32_t((int64_t(shaped-bias)*clamp(p[FoldUnit::P_TRIM]))>>15);
    return q15(clamp(dry+int32_t((int64_t(shaped-dry)*clamp(p[FoldUnit::P_MIX]))>>15),-32768));
}
inline q15 source_sample(int index) {
    index=clamp(index,0,SAMPLES-1);
    return q15(std::lround(24575.0*std::sin(12.56637061435917295385*index/(SAMPLES-1))));
}
inline uint8_t y(q15 value) {
    // Symmetric rounding and fixed +/-1FS rails. No division by signal peak.
    return uint8_t(ZERO-(int(value)*HEIGHT+(value>=0?16384:-16384))/32768);
}
struct Column { uint8_t first=ZERO,last=ZERO,low=ZERO,high=ZERO; };
struct Plot { Column shaped[COLUMNS]; uint8_t source[COLUMNS]={}; };
inline void plot(const q15* p,bool bypass,Plot& out) {
    // Initializes once outside the audio guard and GPU submission. The bounded
    // reference table is synthetic, not an engine/PCM ring-buffer read.
    static q15 reference[SAMPLES];
    static bool initialized=false;
    if(!initialized) {
        for(int i=0;i<SAMPLES;++i)reference[i]=source_sample(i);
        initialized=true;
    }
    for(int col=0;col<COLUMNS;++col) {
        const int center=col*OVERSAMPLE;
        const int first=clamp(center-OVERSAMPLE/2,0,SAMPLES-1);
        const int last=clamp(center+OVERSAMPLE/2,0,SAMPLES-1);
        Column& c=out.shaped[col];c.low=255;c.high=0;
        out.source[col]=y(reference[center]);
        for(int i=first;i<=last;++i) {
            const uint8_t point=y(bypass?reference[i]:sample(reference[i],p));
            if(i==first)c.first=point;
            if(i==last)c.last=point;
            if(point<c.low)c.low=point;
            if(point>c.high)c.high=point;
        }
    }
}
} // namespace seance::platform::ui3ds::fold_wave_model
