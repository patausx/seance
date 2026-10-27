// Pure Tilt mappings shared by DSP and the pre-GPU small-signal UI model.
// Original pivot/shelf tables are frozen. No allocations, lazy state or audio floats.
#pragma once
#include "unit.h"

namespace seance::tilt_eq {
constexpr q15 TRIM_UNITY = 26214;
inline constexpr uint16_t PIVOT_COEFF[65] = {
    794,847,903,963,1026,1094,1166,1243,1325,1412,1504,1603,1708,
    1819,1937,2063,2197,2339,2489,2649,2819,2999,3190,3393,3607,3834,
    4074,4328,4596,4879,5178,5493,5825,6174,6541,6927,7332,7755,8199,
    8662,9146,9649,10172,10715,11278,11858,12457,13072,13703,14348,
    15005,15672,16348,17028,17711,18394,19074,19747,20409,21059,21691,
    22304,22892,23455,23988
};
inline constexpr uint32_t BOOST_Q16[33] = {
    65536,68427,71446,74598,77890,81326,84914,88660,92572,96656,100921,
    105373,110022,114876,119944,125236,130762,136531,142554,148844,155410,
    162267,169426,176901,184706,192855,201363,210247,219523,229208,239321,
    249879,260904
};
inline constexpr uint16_t CUT_Q16[33] = {
    65535,62767,60115,57574,55142,52812,50580,48443,46396,44435,42558,
    40760,39037,37388,35808,34295,32846,31458,30129,28856,27636,26469,
    25350,24279,23253,22270,21329,20428,19565,18738,17947,17188,16462
};
inline constexpr uint32_t TRIM_Q16[81] = {
    4135,4317,4508,4707,4915,5131,5358,5594,5841,6099,
    6368,6649,6942,7248,7568,7902,8250,8614,8995,9391,
    9806,10238,10690,11162,11654,12168,12705,13266,13851,14462,
    15100,15766,16462,17188,17947,18738,19565,20428,21329,22270,
    23253,24279,25350,26469,27636,28856,30129,31458,32846,34295,
    35808,37388,39037,40760,42558,44435,46396,48443,50580,52812,
    55142,57574,60115,62767,65536,68427,71446,74598,77890,81326,
    84914,88660,92572,96656,100921,105373,110022,114876,119944,125236,
    130762
};

inline int32_t coeff(q15 freq, bool legacy_range_zero = false) {
    // Preserve old uint16 interpretation, including malformed stored negatives;
    // ordinary engine-resolved unipolar values are 0..32767.
    if (legacy_range_zero) return 256 + ((uint32_t)(uint16_t)freq * 12000u >> 15);
    const uint32_t scaled = (uint32_t)(uint16_t)freq * 64u;
    uint32_t index = scaled >> 15, fraction = scaled & 32767u;
    if (index >= 64) { index = 63; fraction = 32767; }
    return PIVOT_COEFF[index] + (int32_t)(((int64_t)(PIVOT_COEFF[index+1] -
        PIVOT_COEFF[index]) * fraction) >> 15);
}
inline uint32_t max_step(q15 range) {
    return 8u + ((uint32_t)(range < 0 ? 0 : range) * 24u >> 15);
}
inline void shelf_gains(q15 tilt, q15 range, bool modern, uint32_t& low, uint32_t& high) {
    uint32_t magnitude = tilt < 0 ? (uint32_t)(-(int32_t)tilt) : (uint16_t)tilt;
    uint32_t boost, cut;
    if (!modern) {
        const uint32_t limit = 8u + ((uint32_t)(uint16_t)range * 24u >> 15);
        uint32_t step = (magnitude * limit + 16383u) / 32767u;
        if (step > 32) step = 32;
        boost = BOOST_Q16[step]; cut = CUT_Q16[step];
    } else {
        if (magnitude > 32767) magnitude = 32767;
        const uint32_t position = magnitude * max_step(range);
        const uint32_t index = position / 32767u, fraction = position % 32767u;
        // max_step <=31, so index+1 always remains in the original 33-entry LUT.
        boost = BOOST_Q16[index] + (uint32_t)(((uint64_t)(BOOST_Q16[index+1]-
            BOOST_Q16[index]) * fraction) / 32767u);
        // Signed floor, matching fixed-point shelf rounding without unsigned wrap.
        cut = CUT_Q16[index] - (uint32_t)(((uint64_t)(CUT_Q16[index]-
            CUT_Q16[index+1]) * fraction + 32766u) / 32767u);
        if (tilt == 0) boost = cut = 65536; // truthful exact neutral
    }
    low = tilt >= 0 ? cut : boost;
    high = tilt >= 0 ? boost : cut;
}
// The exact unity anchor differs from a single ideal straight line by <0.001 dB.
// This double-valued display helper is never called by process().
inline double trim_db(q15 value) {
    const int raw = value < 0 ? 0 : value;
    return raw <= TRIM_UNITY ? -24.0 + 24.0 * raw / TRIM_UNITY
        : 6.0 * (raw - TRIM_UNITY) / (32767 - TRIM_UNITY);
}
inline uint32_t trim_gain_q16(q15 value) {
    const uint32_t raw = value < 0 ? 0 : (uint16_t)value;
    if (raw == TRIM_UNITY) return 65536;
    if (raw == 32767) return TRIM_Q16[80];
    const uint32_t numerator = raw < TRIM_UNITY ? raw * 64u : (raw-TRIM_UNITY) * 16u;
    const uint32_t denominator = raw < TRIM_UNITY ? TRIM_UNITY : 32767-TRIM_UNITY;
    const uint32_t index = (raw < TRIM_UNITY ? 0u : 64u) + numerator / denominator;
    const uint32_t fraction = numerator % denominator;
    return TRIM_Q16[index] + (uint32_t)(((uint64_t)(TRIM_Q16[index+1]-
        TRIM_Q16[index]) * fraction) / denominator);
}
} // namespace seance::tilt_eq
