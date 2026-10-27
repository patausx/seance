// Per-card opt-in exact source-frame edits. All endpoints are end-exclusive.
#pragma once
#include <cstdint>

namespace seance {
struct PlayEditState {
    bool enabled = false;
    uint32_t source_frames = 0;
    uint32_t start = 0, end = 0;
    uint32_t fade_in = 0, fade_out = 0;
    uint32_t loop_start = 0, loop_end = 0, crossfade = 0;
    int16_t anchor_start = 0, anchor_length = 32767;
};

inline bool play_edit_valid(const PlayEditState& e) {
    if (!e.enabled) return true;
    return e.source_frames >= 2 && e.start < e.end && e.end <= e.source_frames &&
        e.end - e.start >= 2 && e.fade_in <= e.end - e.start &&
        e.fade_out <= e.end - e.start && e.loop_start >= e.start &&
        e.loop_start < e.loop_end && e.loop_end <= e.end &&
        e.loop_end - e.loop_start >= 2 &&
        e.crossfade <= (e.loop_end - e.loop_start - 1) / 2 &&
        e.anchor_start >= 0 && e.anchor_length >= 0;
}

inline bool play_edit_equal(const PlayEditState& a, const PlayEditState& b) {
    return a.enabled == b.enabled && a.source_frames == b.source_frames &&
        a.start == b.start && a.end == b.end && a.fade_in == b.fade_in &&
        a.fade_out == b.fade_out && a.loop_start == b.loop_start &&
        a.loop_end == b.loop_end && a.crossfade == b.crossfade &&
        a.anchor_start == b.anchor_start && a.anchor_length == b.anchor_length;
}

inline uint32_t play_edit_clamp(int64_t value, uint32_t lo, uint32_t hi) {
    return value < (int64_t)lo ? lo : value > (int64_t)hi ? hi : (uint32_t)value;
}

// Zero delta is exactly the authored frame, regardless of source/Q15 resolution.
// Base changes, modulation, TEMP, scenes and locks all project through this same
// anchor. A shorter projected trim clamps rather than crosses either edge.
inline void play_edit_trim(const PlayEditState& e, int16_t start, int16_t length,
                           uint32_t& a, uint32_t& b) {
    const int64_t ds = (int64_t)((int32_t)start - e.anchor_start) * (e.source_frames - 1) / 32767;
    const int64_t dl = (int64_t)((int32_t)length - e.anchor_length) * e.source_frames / 32767;
    a = play_edit_clamp((int64_t)e.start + ds, 0, e.source_frames - 2);
    const uint32_t n = play_edit_clamp((int64_t)(e.end - e.start) + dl, 2, e.source_frames);
    b = play_edit_clamp((int64_t)a + n, a + 2, e.source_frames);
}
} // namespace seance
