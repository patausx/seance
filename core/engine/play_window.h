// Exact PLAY control projection. No PCM reads, allocations, or mutable state.
// Callers reading a live Sample must hold the engine/audio guard. UI callers pass
// selected-step locks/base, not the currently playing unit's effective values.
#pragma once
#include "../synth/sampler.h"
#include "play_edit.h"

namespace seance {

struct PlayWindow {
    bool valid = false;
    uint32_t start = 0, end = 0; // source frames, end exclusive
    int32_t increment = 1024;   // signed source frames per output frame, q16
    uint8_t slice = 0;          // applied chop 1..16; 0 means STRT/LEN fallback
    bool edited = false;       // opt-in path, never changes old projection/audio
    uint32_t fade_in = 0, fade_out = 0;
    uint32_t loop_start = 0, loop_end = 0, crossfade = 0;
    // Same endpoint mapping as PLAY telemetry: end maps the final source frame.
    uint16_t normalized_start = 0, normalized_end = 0;

    // Nominal full-window traversal at 32 kHz, truncated to milliseconds. This is
    // an estimated ONE PASS, not a gate, sequencer step length, or voice lifetime.
    // Loop/retrigger, edge interpolation, modulation and later locks can change
    // audible lifetime; therefore loop is intentionally not an input.
    uint64_t traversal_ms() const {
        if (!valid || increment == 0) return 0;
        const uint32_t magnitude = (uint32_t)(increment < 0 ? -increment : increment);
        return (uint64_t)(end - start) * 65536u * 1000u /
               ((uint64_t)magnitude * trackr::synth::SAMPLER_SR);
    }
};

namespace play_window_detail {
inline int32_t quantize_speed(int32_t v) {
    if (v == 0) return 0;
    const bool neg = v < 0;
    uint32_t mag = (uint32_t)(neg ? -v : v);
    uint32_t inc = mag * 8u;
    if (inc < 1024) inc = 1024;
    // Exact original integer candidates and strict tie rule. In particular the
    // 262136 ceiling excludes the mathematical +24st ratio 262144.
    static const uint32_t SEMI_Q16[12] = {
        65536, 69433, 73562, 77936, 82570, 87480,
        92682, 98199, 104032, 110218, 116773, 123715
    };
    uint32_t best = 1024;
    uint32_t best_diff = inc > best ? inc - best : best - inc;
    for (int semi = -72; semi <= 24; ++semi) {
        int oct = semi / 12;
        int rem = semi % 12;
        if (rem < 0) { rem += 12; --oct; }
        uint32_t ratio = SEMI_Q16[rem];
        if (oct >= 0) ratio <<= oct;
        else          ratio >>= -oct;
        if (ratio < 1024 || ratio > 262136) continue;
        uint32_t diff = inc > ratio ? inc - ratio : ratio - inc;
        if (diff < best_diff) { best = ratio; best_diff = diff; }
    }
    return neg ? -(int32_t)best : (int32_t)best;
}
} // namespace play_window_detail

// Metadata overload supports bounded testing without allocating enormous PCM.
// chops points to Sample::MAX_CHOPS entries, or nullptr for no chop metadata.
// modern=true intersects sample-wide trim with an explicitly selected slice;
// missing or trimmed-out modern slices are silent, never a whole-file fallback.
// Unipolar controls normally lie in 0..32767. The uint16 casts and uint32 window
// arithmetic deliberately retain the legacy DSP behavior even for malformed
// controls/extreme frame counts; valid rejects windows process() cannot play.
inline PlayWindow play_window(uint32_t frames, const uint32_t* chops,
                              uint16_t reverse_mask, trackr::fx::q15 speed,
                              trackr::fx::q15 start, trackr::fx::q15 length,
                              trackr::fx::q15 slice_value, bool pitch_quantize,
                              bool modern = false) {
    PlayWindow out;
    int32_t v = speed;
    bool rev = v < 0;
    if (rev) v = -v;
    out.increment = pitch_quantize
        ? play_window_detail::quantize_speed(rev ? -v : v) : v * 8;
    if (out.increment < 0 ? -out.increment < 1024 : out.increment < 1024)
        out.increment = rev ? -1024 : 1024;
    // Preserve legacy ordering: unquantized -1..-127 ends up +1024. Do not turn
    // a projection extraction into an unrequested audible speed-floor fix.
    if (!pitch_quantize && rev) out.increment = -out.increment;
    if (frames < 2) return out;

    out.start = (uint32_t)((uint64_t)(uint16_t)start * (frames - 1) / 32767u);
    uint32_t len = (uint32_t)((uint64_t)(uint16_t)length * frames / 32767u);
    if (len < 2) len = 2;
    if (out.start > frames - 2) out.start = frames - 2;
    out.end = out.start + len;
    if (out.end > frames) out.end = frames;

    int slice = (int)(((uint32_t)(uint16_t)slice_value *
                      (trackr::synth::Sample::MAX_CHOPS + 1)) >> 15);
    if (slice > trackr::synth::Sample::MAX_CHOPS) slice = trackr::synth::Sample::MAX_CHOPS;
    bool found_slice = false;
    if (slice > 0 && chops) {
        uint32_t a = chops[slice - 1];
        if (a != 0xFFFFFFFFu && a < frames) {
            uint32_t b = frames;
            for (int i = 0; i < trackr::synth::Sample::MAX_CHOPS; ++i) {
                uint32_t c = chops[i];
                if (c != 0xFFFFFFFFu && c > a && c < b) b = c;
            }
            if (b > a + 1) {
                found_slice = true;
                out.start = modern ? (out.start > a ? out.start : a) : a;
                out.end = modern ? (out.end < b ? out.end : b) : b;
                out.slice = (uint8_t)slice;
                if (reverse_mask & (uint16_t)(1u << (slice - 1)))
                    out.increment = -out.increment;
            }
        }
    }
    if (modern && slice > 0 && !found_slice) {
        out.slice = (uint8_t)slice;
        out.start = out.end = 0;
    }
    out.valid = out.end <= frames && out.start + 1 < out.end;
    out.normalized_start = (uint16_t)((uint64_t)out.start * 32767u / (frames - 1));
    out.normalized_end = (uint16_t)((uint64_t)(modern && !out.end ? 0 : out.end - 1) * 32767u / (frames - 1));
    return out;
}

inline PlayWindow play_window(const trackr::synth::Sample& sample,
                              trackr::fx::q15 speed, trackr::fx::q15 start,
                              trackr::fx::q15 length, trackr::fx::q15 slice,
                              bool pitch_quantize, bool modern = false) {
    auto out = play_window(sample.playable_frames(), sample.chops, sample.slice_reverse_mask,
                       speed, start, length, slice, pitch_quantize, modern);
    if (modern && out.slice) out.increment = (int32_t)((int64_t)out.increment *
        trackr::synth::Sample::cut_speed(sample.slice_speed_q8[out.slice - 1]) / 256);
    return out;
}

// Exact opt-in projection. Identity is a caller responsibility; frames alone are
// not an identity token. The PlayUnit wrapper additionally verifies slot,
// generation, allocation, channels and playable length before using this helper.
inline PlayWindow play_edit_window(uint32_t frames, const uint32_t* chops,
                                   uint16_t reverse_mask, trackr::fx::q15 speed,
                                   trackr::fx::q15 start, trackr::fx::q15 length,
                                   trackr::fx::q15 slice, bool pitch_quantize,
                                   bool modern, const PlayEditState& edit) {
    if (!edit.enabled) return play_window(frames, chops, reverse_mask, speed,
        start, length, slice, pitch_quantize, modern);
    PlayWindow out;
    out.edited = true;
    if (!modern || !play_edit_valid(edit) || frames != edit.source_frames) return out;
    // Resolve slice identity/reverse against the entire source, then intersect
    // exact trim. Never substitute an absent or excluded slice with the WAV.
    out = play_window(frames, chops, reverse_mask, speed, 0, 32767, slice, pitch_quantize, true);
    out.edited = true;
    if (!out.valid) return out;
    uint32_t a, b;
    play_edit_trim(edit, start, length, a, b);
    const uint32_t full_a = a, full_b = b;
    if (a < out.start) a = out.start;
    if (b > out.end) b = out.end;
    out.start = a; out.end = b;
    out.valid = a < b && b - a >= 2;
    if (!out.valid) { out.start = out.end = 0; return out; }
    const uint32_t span = b - a;
    out.fade_in = edit.fade_in < span ? edit.fade_in : span;
    out.fade_out = edit.fade_out < span ? edit.fade_out : span;
    const uint32_t base_span = edit.end - edit.start;
    const uint32_t projected_span = full_b - full_a;
    uint32_t la = full_a + (uint32_t)((uint64_t)(edit.loop_start - edit.start) * projected_span / base_span);
    uint32_t lb = full_a + (uint32_t)((uint64_t)(edit.loop_end - edit.start) * projected_span / base_span);
    if (la < a) la = a;
    if (lb > b) lb = b;
    if (la < lb && lb - la >= 2) {
        out.loop_start = la; out.loop_end = lb;
        const uint32_t maximum = (lb - la - 1) / 2;
        out.crossfade = edit.crossfade < maximum ? edit.crossfade : maximum;
    }
    out.normalized_start = (uint16_t)((uint64_t)a * 32767u / (frames - 1));
    out.normalized_end = (uint16_t)((uint64_t)(b - 1) * 32767u / (frames - 1));
    return out;
}

inline PlayWindow play_edit_window(const trackr::synth::Sample& sample,
                                   trackr::fx::q15 speed, trackr::fx::q15 start,
                                   trackr::fx::q15 length, trackr::fx::q15 slice,
                                   bool pitch_quantize, bool modern,
                                   const PlayEditState& edit) {
    auto out = play_edit_window(sample.playable_frames(), sample.chops,
        sample.slice_reverse_mask, speed, start, length, slice, pitch_quantize, modern, edit);
    if (modern && out.slice) out.increment = (int32_t)((int64_t)out.increment *
        trackr::synth::Sample::cut_speed(sample.slice_speed_q8[out.slice - 1]) / 256);
    return out;
}

} // namespace seance
