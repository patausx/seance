// fixed-point math: q15 (audio samples) and q16.16 (phases, params)
// deterministic, fast on arm11 without vfp
#pragma once
#include <cstdint>

namespace trackr::fx {

using q15  = int16_t;   // audio sample [-1, 1) → [-32768, 32767]
using q31  = int32_t;   // high-precision mix accumulator
using q16  = int32_t;   // 16.16 - for oscillator phases and params
using uq16 = uint32_t;

constexpr int Q15_SHIFT = 15;
constexpr int Q16_SHIFT = 16;
constexpr q15 Q15_ONE   = 0x7FFF;
constexpr q16 Q16_ONE   = 1 << Q16_SHIFT;

// saturating conversion q31 -> q15 (prevents clipping wrap)
inline q15 sat_q15(q31 x) {
    if (x >  32767) return  32767;
    if (x < -32768) return -32768;
    return static_cast<q15>(x);
}

// q15 * q15 -> q15 (with rounding)
inline q15 mul_q15(q15 a, q15 b) {
    return static_cast<q15>((static_cast<int32_t>(a) * b + (1 << 14)) >> 15);
}

// linear interpolation between two q15 samples, frac in q16
// IMPORTANT: we use int64 for the intermediate multiply to avoid overflow:
// diff (up to +/-65535) * frac (up to 65535) = up to ~4.3e9 - doesn't fit in int32_t (max 2.1e9)
// the bug shows up as random crackle during sample playback
inline q15 lerp_q15(q15 a, q15 b, uq16 frac) {
    int32_t diff = static_cast<int32_t>(b) - a;
    return static_cast<q15>(a + (int32_t)(((int64_t)diff * (int64_t)frac) >> 16));
}

// soft cubic saturation from q31 to q15 - a smooth knee, unlike hard sat_q15.
// idea: for |v|<=knee it passes through linearly. above that - cubic taper into the asymptote.
// removes hard-clipping crackle when summing multiple sources.
//
// Bit-exact with the original int64 formulation for every int32 input
// (tests/test_soft_clip_exact.py compares all 2^32 values). After the knee clamp
// every intermediate fits in 32 bits, so ARM11 needs no software 64-bit division:
// both divisors are constants and become multiplies.
inline q15 soft_clip_q31(q31 v) {
    constexpr q31 KNEE  = 24000;            // linear range up to ~73% amplitude, above that - cubic
    constexpr q31 RANGE = 32767 - KNEE;     // 8767
    constexpr uint32_t SPAN = uint32_t(RANGE) * 4;   // 35068: full taper width
    static_assert(uint64_t(SPAN) * 32767 <= UINT32_MAX, "over * 32767 must fit");
    static_assert(uint64_t(32767) * 32767 <= UINT32_MAX, "r * r and r2 * r must fit");
    if (v >= -KNEE && v <= KNEE) return (q15)v;     // linear
    const bool neg = v < 0;
    // Unsigned magnitude is defined for INT32_MIN too (2^31).
    const uint32_t mag = neg ? 0u - uint32_t(v) : uint32_t(v);
    uint32_t over = mag - uint32_t(KNEE);            // >= 1
    if (over > SPAN) over = SPAN;
    const uint32_t r  = (over * 32767u) / SPAN;      // q15 [0..32767]
    const uint32_t r2 = (r * r) >> 15;
    const uint32_t r3 = (r2 * r) >> 15;              // r3 <= r
    const uint32_t shaped = r - r3 / 3;              // [0..32767]: the old clamps were unreachable
    const q31 result = KNEE + q31((shaped * uint32_t(RANGE)) >> 15);   // <= 32766
    return neg ? (q15)(-result) : (q15)result;
}

} // namespace trackr::fx
