#pragma once
// Pure stored-value formatting: never consult a focused frame or a live engine.
#include "../../core/engine/inst_unit.h"
#include <cstdio>
#include <cstddef>

namespace seance::platform::ui3ds::inst_readout {
inline void note_text(char* out, size_t size, int midi) {
    static constexpr const char* names[] = {"c","c#","d","d#","e","f","f#","g","g#","a","a#","b"};
    if (!out || !size) return;
    midi = midi < 0 ? 0 : midi > 127 ? 127 : midi;
    std::snprintf(out, size, "%s%d", names[midi % 12], midi / 12 - 1);
}
inline void value_text(char* out, size_t size, int param, q15 value) {
    if (!out || !size) return;
    using I = InstUnit;
    switch (param) {
        case I::P_INSTRUMENT: std::snprintf(out,size,"%s",I::instrument_name(I::instrument_index(value))); break;
        case I::P_NOTE: note_text(out,size,I::note_midi(value)); break;
        case I::P_LENGTH: std::snprintf(out,size,"%ums",unsigned(I::length_ms(value))); break;
        case I::P_ATTACK: std::snprintf(out,size,"%ums",unsigned(I::attack_ms(value))); break;
        case I::P_RELEASE: std::snprintf(out,size,"%ums",unsigned(I::release_ms(value))); break;
        case I::P_DECAY: std::snprintf(out,size,"%ums",unsigned(I::decay_ms(value))); break;
        case I::P_HARDNESS:
        case I::P_POSITION:
        case I::P_DAMPING:
        case I::P_STRENGTH:
        case I::P_TONE:
        case I::P_LEVEL: std::snprintf(out,size,"%d%%",((value < 0 ? 0 : int(value))*100+16383)/32767); break;
        default: std::snprintf(out,size,"--"); break;
    }
}
} // namespace seance::platform::ui3ds::inst_readout
