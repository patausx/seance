#pragma once
#include "../../core/engine/units.h"
#include "ring_reference_data.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace seance::platform::ui3ds::ring_reference {
constexpr int PARAMS=6,SAMPLES=1024,COLUMNS=243,X0=64;
constexpr int ZERO=117,HEIGHT=21,CARRIER_ZERO=154,CARRIER_HEIGHT=6;
// Per-column entry/exit and extrema; only final pixel coordinates cross into draw.
struct Trace {
    uint8_t first[COLUMNS]={},last[COLUMNS]={},lo[COLUMNS]={},hi[COLUMNS]={};
};
struct Plot {Trace source,shaped,carrier;};
static_assert(sizeof(Plot)==12*COLUMNS,"fixed Ring reference pixels, no pointers or PCM");
inline uint32_t increment(q15 value) {
    const uint32_t scaled=uint32_t(std::max(0,int(value)))*64u;
    const unsigned i=scaled>>15,f=scaled&32767u;
    return data::INCREMENT[i]+uint32_t((uint64_t(data::INCREMENT[i+1]-data::INCREMENT[i])*f)>>15);
}
inline double hz(q15 value) {return double(increment(value))*ENGINE_SR/4294967296.0;}
inline q15 source_sample(int i) {return data::BURST[i];}
inline q15 carrier_sample(uint16_t phase) {
    const int tri=phase<32768?int(phase)*2-32768:32767-(int(phase)-32768)*2;
    return trackr::fx::sat_q15(int32_t((int64_t(tri)*(65536-std::abs(tri)))>>15));
}
inline uint8_t y(q15 value,int zero=ZERO,int height=HEIGHT) {
    const int scaled=int(value)*height;
    const int offset=scaled>=0?(scaled+16384)/32768:-((-scaled+16384)/32768);
    return uint8_t(zero-offset);
}
inline void append(Trace& trace,int column,uint8_t value,bool first) {
    if(first)trace.first[column]=trace.lo[column]=trace.hi[column]=value;
    else {trace.lo[column]=std::min(trace.lo[column],value);trace.hi[column]=std::max(trace.hi[column],value);}
    trace.last[column]=value;
}
inline void plot(const q15* params,bool bypass,Plot& result) {
    // Private, reset, stack-only processor. Never call the live unit, follow live
    // phase or read the audio chain. Caller caches this outside Guard and GPU.
    RingUnit reference(false);
    for(int p=0;p<PARAMS;++p)reference.params[p].effective=params[p];
    reference.reset();
    int previous=-1;
    for(int i=0;i<SAMPLES;++i) {
        const q15 input=source_sample(i);q15 output=input;
        if(!bypass)reference.process(&output,1);
        const int column=i*(COLUMNS-1)/(SAMPLES-1);
        const bool first=column!=previous;
        append(result.source,column,y(input),first);
        append(result.shaped,column,y(output),first);
        // Depth-zero and bypass intentionally retain the reset phase: paused,
        // not an invented free-running oscillator behind an exact dry result.
        append(result.carrier,column,y(carrier_sample(reference.visual_phase()),CARRIER_ZERO,CARRIER_HEIGHT),first);
        previous=column;
    }
}
}
