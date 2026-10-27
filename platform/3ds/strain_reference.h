#pragma once
#include "../../core/engine/units.h"
#include "strain_reference_data.h"
#include <algorithm>
#include <cstdint>

namespace seance::platform::ui3ds::strain_reference {
constexpr int PARAMS=6,SAMPLES=1024,BURST=512,COLUMNS=243,X0=64;
constexpr int ZERO=120,HEIGHT=24;
// Pixel coordinates only: entry/exit/extrema preserve each fixed-scale column.
struct Trace {
    uint8_t first[COLUMNS]={},last[COLUMNS]={},lo[COLUMNS]={},hi[COLUMNS]={};
};
struct Plot {Trace source,shaped;};
static_assert(sizeof(Plot)==8*COLUMNS,"fixed Strain reference pixels, no pointers or PCM");
static_assert(ENGINE_SR==32000 && CONTROL_INTERVAL==32,"approved reference cadence");
inline q15 source_sample(int i) {return i<BURST?data::BURST[i]:0;}
inline uint8_t y(q15 value) {
    const int scaled=int(value)*HEIGHT;
    const int offset=scaled>=0?(scaled+16384)/32768:-((-scaled+16384)/32768);
    return uint8_t(ZERO-offset);
}
inline void append(Trace& trace,int column,uint8_t value,bool first) {
    if(first)trace.first[column]=trace.lo[column]=trace.hi[column]=value;
    else {trace.lo[column]=std::min(trace.lo[column],value);trace.hi[column]=std::max(trace.hi[column],value);}
    trace.last[column]=value;
}
inline void plot(const q15* params,bool bypass,Plot& result) {
    // Private reset state, stack-only storage. Match tools/strain_review.cpp:
    // snapped params, reset's own tick, then 1kHz pre-block ticks. Never use the
    // live unit/phase/PCM, nor treat this 32ms test as a full body-motion cycle.
    StrainUnit reference(false); // explicit historical six-parameter DSP
    for(int p=0;p<PARAMS;++p){reference.params[p].base=params[p];reference.params[p].snap();}
    reference.reset();
    int previous=-1;
    for(int block=0;block<SAMPLES;block+=CONTROL_INTERVAL) {
        q15 output[CONTROL_INTERVAL];
        for(int i=0;i<CONTROL_INTERVAL;++i)output[i]=source_sample(block+i);
        if(!bypass) {
            reference.control_tick();
            reference.process(output,CONTROL_INTERVAL);
        }
        for(int i=0;i<CONTROL_INTERVAL;++i) {
            const int sample=block+i,column=sample*(COLUMNS-1)/(SAMPLES-1);
            const bool first=column!=previous;
            append(result.source,column,y(source_sample(sample)),first);
            append(result.shaped,column,y(output[i]),first);
            previous=column;
        }
    }
}
}
