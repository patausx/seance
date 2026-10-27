// Universal sigil oscillator settings. Drawings live independently in Sigil.
#pragma once
#include "unit.h"

namespace seance {
enum class SigilShape : uint8_t { Drawn = 0, Sine, Triangle, Saw, Square, Random, Smooth, Count };
enum class SigilRestart : uint8_t { Free = 0, Transport, Count };
struct SigilConfig {
    SigilShape shape = SigilShape::Drawn;
    SigilRestart restart = SigilRestart::Free;
    uint16_t rate_millihz = 1000, phase_degrees = 0;
    bool unipolar = false;
    bool legacy = true; // old playback/depth; oscillator edit opts out
    bool legacy_rate = false; // independently retain normalized ~40Hz law after shape/phase edits
    bool enabled = true; // inactive PBAY migration; retained config, silent output
};
inline bool sigil_config_valid(const SigilConfig& c) {
    return c.shape < SigilShape::Count && c.restart < SigilRestart::Count &&
        c.rate_millihz >= 50 && c.rate_millihz <= 20000 && c.phase_degrees < 360 &&
        (!c.legacy || c.shape == SigilShape::Drawn);
}
inline uint32_t sigil_phase_offset(uint16_t degrees) {
    return (uint32_t)(((uint64_t)degrees << 32) / 360);
}
inline bool sigil_config_equal(const SigilConfig& a, const SigilConfig& b) {
    return a.shape == b.shape && a.restart == b.restart && a.rate_millihz == b.rate_millihz &&
        a.phase_degrees == b.phase_degrees && a.unipolar == b.unipolar &&
        a.legacy == b.legacy && a.legacy_rate == b.legacy_rate && a.enabled == b.enabled;
}
// Full physical phase: sine/triangle start zero rising; saw negative; square positive.
// Drawn/random/smooth require a captured table or runtime targets, not a periodic oracle.
inline q15 sigil_wave_sample(SigilShape shape, uint32_t phase) {
    if (shape == SigilShape::Square) return phase < 0x80000000u ? 32767 : -32767;
    if (shape == SigilShape::Saw) return (q15)((int32_t)(((uint64_t)phase * 65534u) >> 32) - 32767);
    if (shape != SigilShape::Sine && shape != SigilShape::Triangle) return 0;
    const uint32_t quadrant = phase >> 30;
    const uint32_t within = phase & 0x3fffffffu;
    const uint32_t rising = (quadrant & 1u) ? 0x40000000u - within : within;
    int32_t magnitude;
    if (shape == SigilShape::Triangle) {
        magnitude = (int32_t)(((uint64_t)rising * 32767u) >> 30);
    } else {
        static constexpr int16_t sine[65] = {
            0,804,1608,2410,3212,4011,4808,5602,6393,7179,7962,8739,9512,
            10278,11039,11793,12539,13279,14010,14732,15446,16151,16846,
            17530,18204,18868,19519,20159,20787,21403,22005,22594,23170,
            23731,24279,24811,25329,25832,26319,26790,27245,27683,28105,
            28510,28898,29268,29621,29956,30273,30571,30852,31113,31356,
            31580,31785,31971,32137,32285,32412,32521,32609,32678,32728,
            32757,32767
        };
        const unsigned pos = rising >> 24;
        magnitude = pos == 64 ? 32767 : sine[pos] +
            (int32_t)(((uint64_t)(sine[pos+1] - sine[pos]) * (rising & 0xffffffu)) >> 24);
    }
    return (q15)(quadrant >= 2 ? -magnitude : magnitude);
}
} // namespace seance
