#pragma once
#include "../../core/engine/units.h"
#include <cstdint>

namespace seance::platform::ui3ds::bit_reference {
constexpr int PARAMS=7, COLUMNS=288, X0=16, ZERO=119, HEIGHT=32;
struct Plot { uint8_t source[COLUMNS]={}, shaped[COLUMNS]={}; };
inline q15 source_sample(int i) {
    const int phase=i%96;
    const int triangle=phase<24?phase:phase<72?48-phase:phase-96;
    return q15(triangle*24575/24);
}
inline uint8_t y(q15 value) { return uint8_t(ZERO-int(value)*HEIGHT/32768); }
inline void plot(const q15* params,bool bypass,Plot& result) {
    // Private stack-only processor, reset seed/phase on each reference. This is
    // NOT the live unit, live PCM or a continuously running preview voice.
    BitUnit reference(false); // explicitly retain the seven-parameter legacy DSP
    q15 samples[COLUMNS];
    for(int p=0;p<PARAMS;++p)reference.params[p].effective=params[p];
    for(int i=0;i<COLUMNS;++i) {
        samples[i]=source_sample(i);result.source[i]=y(samples[i]);
    }
    if(!bypass)reference.process(samples,COLUMNS);
    for(int i=0;i<COLUMNS;++i)result.shaped[i]=y(samples[i]);
}
}
