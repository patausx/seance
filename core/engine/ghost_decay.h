#pragma once
#include <cstdint>

namespace seance::ghost_decay {
// -ln(feedback), q24. Log-spaced from feedback 18000/32768 to 0.999.
// 32 intervals; authored offline, no exp/log or allocation in the audio callback.
inline uint16_t gain(int value, uint32_t root_delay_q8) {
    static constexpr uint32_t loss[] = {
        10050906, 8230298, 6739473, 5518694, 4519045, 3700472, 3030173, 2481292,
        2031834, 1663790, 1362413, 1115628, 913544, 748066, 612563, 501604,
        410744, 336342, 275418, 225529, 184677, 151225, 123832, 101401,
        83034, 67993, 55677, 45592, 37333, 30571, 25033, 20499, 16786
    };
    const uint32_t v = value < 0 ? 0u : value > 32767 ? 32767u : (uint32_t)value;
    const uint32_t position = v * 32u;
    const uint32_t index = position / 32767u, fraction = position % 32767u;
    int64_t x = loss[index];
    if (index < 32) x += ((int64_t)loss[index + 1] - loss[index]) * fraction / 32767;
    // Reference is the original default TONE=15000, FINE=0, without drift.
    constexpr uint32_t reference = (uint32_t)((149ULL * (32767u + (32767u - 15000u) * 3u) * 256u) >> 15);
    x = x * root_delay_q8 / reference;
    if (x > (1 << 24)) x = 1 << 24; // outside supported geometry: stay bounded
    // exp(-x), eight terms, |x|<=1; <1 q15 LSB including quantisation.
    static constexpr int32_t coefficients[] = {
        16777216, -16777216, 8388608, -2796203, 699051, -139810, 23302, -3329, 416
    };
    int64_t result = coefficients[8];
    for (int n = 7; n >= 0; --n) result = coefficients[n] + ((result * x) >> 24);
    int32_t g = (int32_t)((result + 256) / 512);
    if (g > 32755) g = 32755; // always lossy, never a disguised freeze
    if (g < 0) g = 0;
    return (uint16_t)g;
}
} // namespace seance::ghost_decay
