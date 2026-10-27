#pragma once
#include "unit.h"
#include <cmath>

namespace seance::delay_time {
// Modern-only mapping. The old six-parameter delay deliberately never uses it.
constexpr uint32_t MIN_FRAMES = 32, MAX_FRAMES = 128000;
inline int raw(q15 value) { return value < 0 ? 0 : value; }
inline uint32_t free_frames(q15 value) {
    if (value <= 0) return MIN_FRAMES;
    if (value == 32767) return MAX_FRAMES;
    return (uint32_t)(32.0 * std::pow(4000.0, raw(value) / 32767.0) + 0.5);
}
inline q15 free_value(uint32_t samples) {
    if (samples <= MIN_FRAMES) return 0;
    if (samples >= MAX_FRAMES) return 32767;
    return (q15)(std::log(samples / 32.0) / std::log(4000.0) * 32767.0 + 0.5);
}
inline int division(q15 value) { return (raw(value) * 8) >> 15; }
inline int feel(q15 value) { return (raw(value) * 3) >> 15; }
inline q15 division_value(int index) {
    return (q15)((index < 0 ? 0 : index > 7 ? 7 : index) * 4096 + 2048);
}
inline q15 feel_value(int index) {
    return (q15)(((index < 0 ? 0 : index > 2 ? 2 : index) * 32768 + 16384) / 3);
}
inline uint32_t tone_hz(q15 value) {
    if (value <= 0) return 200;
    if (value == 32767) return 12000;
    return (uint32_t)(200.0 * std::pow(60.0, raw(value) / 32767.0) + 0.5);
}
inline uint32_t sync_frames(q15 div, q15 timing, int32_t bpm_q16, bool* limited = nullptr) {
    // 1/64 note = 1/16 quarter-note beat; 2/1 = eight beats.
    uint64_t numerator = (uint64_t)32000 * 60 * 65536 * (1u << division(div));
    uint64_t denominator = (uint64_t)(bpm_q16 > 0 ? bpm_q16 : 1) * 16;
    if (feel(timing) == 1) { numerator *= 3; denominator *= 2; }
    if (feel(timing) == 2) { numerator *= 2; denominator *= 3; }
    const bool clipped = numerator < denominator * MIN_FRAMES || numerator > denominator * MAX_FRAMES;
    const uint64_t frames = (numerator + denominator / 2) / denominator;
    if (limited) *limited = clipped;
    return frames < MIN_FRAMES ? MIN_FRAMES : frames > MAX_FRAMES ? MAX_FRAMES : (uint32_t)frames;
}
} // namespace seance::delay_time
