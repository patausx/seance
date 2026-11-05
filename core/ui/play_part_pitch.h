// Independent per-part musical pitch. Offline fixed ratios; no libm/allocation.
// Historical q8 encode/decode remain compatibility helpers, NOT pitch storage.
#pragma once
#include <cstdint>

namespace seance { namespace play_part_pitch {
constexpr int kMin = -24, kMax = 24;
// Rounded 256 * 2^(semitones/12), offline literal table, identity exactly256.
constexpr uint16_t kRates[kMax - kMin + 1] = {
    64,68,72,76,81,85,91,96,102,108,114,121,
    128,136,144,152,161,171,181,192,203,215,228,242,
    256,271,287,304,323,342,362,384,406,431,456,483,
    512,542,575,609,645,683,724,767,813,861,912,967,1024
};
// Rounded 65536 * 2^(semitones/12); identity is exactly 65536.
constexpr int32_t kRatios[kMax - kMin + 1] = {
    16384,17358,18390,19484,20643,21870,23170,24548,26008,27554,29193,30929,
    32768,34716,36781,38968,41285,43740,46341,49097,52016,55109,58386,61858,
    65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715,
    131072,138866,147123,155872,165140,174960,185364,196386,208064,220436,233544,247431,262144
};
inline int32_t ratio_q16(int semitones) {
    const int clamped = semitones < kMin ? kMin : semitones > kMax ? kMax : semitones;
    return kRatios[clamped - kMin];
}
inline bool valid(int semitones) { return semitones >= kMin && semitones <= kMax; }
inline uint16_t encode(int semitones) {
    const int clamped = semitones < kMin ? kMin : semitones > kMax ? kMax : semitones;
    return kRates[clamped - kMin];
}
// Nearest q8 table entry; ties choose the lower semitone. Input saturation
// naturally produces -24/+24 without changing Sample::cut_speed compatibility.
inline int decode(uint16_t rate) {
    int best = 0;
    uint32_t distance = rate > kRates[0] ? rate - kRates[0] : kRates[0] - rate;
    for (int i = 1; i <= kMax - kMin; ++i) {
        const uint32_t delta = rate > kRates[i] ? rate - kRates[i] : kRates[i] - rate;
        if (delta < distance) { best = i; distance = delta; }
    }
    return kMin + best;
}
} } // namespace seance::play_part_pitch
