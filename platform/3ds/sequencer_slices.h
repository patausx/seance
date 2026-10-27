#pragma once
#include "../../core/engine/engine.h"
#include "../../core/engine/play_window.h"
#include "sequencer_layout.h"

namespace seance::platform::ui3ds::seq_slice {
constexpr int MODE = 7;
constexpr int WAVE_COLUMNS = 352;
constexpr int WAVE_PROBES = 8; // bounded sampled envelope, NOT a full PCM peak scan
inline int decode(q15 value) {
    return value <= 0 ? 0 : ((uint32_t)value * 17u) >> 15;
}
inline q15 encode(int slice) {
    return slice <= 0 ? 0 : (q15)(((slice > 16 ? 16 : slice) * 32768 + 16384) / 17);
}
inline uint16_t valid_mask(const trackr::synth::Sample& sample) {
    if (sample.channels != 1 && sample.channels != 2) return 0;
    const uint32_t frames = sample.playable_frames();
    uint16_t mask = 0;
    for (int i = 0; i < 16; ++i) {
        const uint32_t a = sample.chops[i];
        if (a == UINT32_MAX || a >= frames) continue;
        uint32_t b = frames;
        for (uint32_t c : sample.chops) if (c > a && c < b) b = c;
        if (b - a >= 2) mask |= (uint16_t)(1u << i);
    }
    return mask;
}
inline int count(uint16_t mask) {
    int n = 0;
    for (; mask; mask >>= 1) n += mask & 1;
    return n;
}
// Ordinal among valid IDs, with zero reserved for the original START/LEN window.
inline int at(uint16_t mask, int ordinal) {
    if (ordinal <= 0) return 0;
    for (int i = 1; i <= 16; ++i)
        if ((mask & (1u << (i - 1))) && --ordinal == 0) return i;
    return 0;
}
struct Cell { uint8_t slice = 0; bool locked = false; };
struct Row {
    int8_t slot = -1;
    bool loaded = false;
    uint16_t valid = 0;
    Cell cells[seq_layout::PAGE_STEPS];
};
struct Frame {
    Row rows[NUM_CHAINS];
    int chain = -1, step = -1, first = 0, sample = -1;
    // Original IDs retained, including sparse/out-of-order legacy chop tables.
    uint16_t begin[16] = {}, end[16] = {}, reversed = 0;
    int8_t low[WAVE_COLUMNS] = {}, high[WAVE_COLUMNS] = {};
    uint16_t window_begin = 0, window_end = 0;
    uint16_t visual_gain_q8 = 256; // display-only boost1..64x, before 8-bit projection
    uint32_t frames = 0, pcm_reads = 0;
    uint64_t milliseconds = 0;
    uint8_t requested = 0, applied = 0;
    bool loaded = false, window_valid = false, locked = false;
    bool reverse = false, loop = false, modulated = false;
};
static_assert(sizeof(Frame) <= 1024, "bounded slice capture separate from hot snapshot");
} // namespace seance::platform::ui3ds::seq_slice

namespace seance::platform::ui3ds {
const seq_slice::Frame& sequencer_slices();
} // namespace seance::platform::ui3ds
