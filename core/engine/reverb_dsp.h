// Compact mono REV coefficient laws shared by audio and frozen UI projection.
// No instance, allocation, clock, PCM access or floating-point/transcendental work.
#pragma once
#include "unit.h"

namespace seance::reverb_dsp {

constexpr int32_t ONE = 1 << 20;                 // coefficients, exact unity
constexpr int32_t AUDIO_SCALE = 1024;            // q15 -> signed q25
constexpr int32_t LIMIT = (1 << 28) - 1;         // eight FS internal headroom
constexpr uint16_t RAMP_FRAMES = 64;
constexpr uint16_t SIZE_FRAMES = 640;             // 20 ms, coalesced dual reads
constexpr uint32_t BASE[4] = {887, 1151, 1451, 1823};
constexpr uint32_t CAPACITY[4] = {1331, 1727, 2177, 2735};
constexpr uint32_t OFFSET[4] = {0, 1331, 3058, 5235};
constexpr uint32_t FDN_SAMPLES = 7970;
constexpr uint32_t DIFFUSER_A = 113, DIFFUSER_B = 173;
constexpr uint32_t STORAGE_SAMPLES = FDN_SAMPLES + DIFFUSER_A + DIFFUSER_B;
constexpr uint32_t PREDELAY_MAX = ENGINE_SR * 150u / 1000u;
constexpr uint32_t PREDELAY_CAPACITY = PREDELAY_MAX + 1u;
constexpr uint16_t PREDELAY_FRAMES = 640;           // coalesced 20 ms two-tap fade

inline uint32_t unipolar(q15 raw) { return raw > 0 ? uint32_t(raw) : 0u; }
inline uint32_t predelay_ms(q15 raw) { return (150u * unipolar(raw) + 16383u) / 32767u; }
inline uint32_t predelay_samples(q15 raw) { return (PREDELAY_MAX * unipolar(raw) + 16383u) / 32767u; }
// OFF is a true bypass; the first active value starts at 20 Hz, then a musical
// quadratic sweep to 1 kHz. This is send EQ, never tank damping or dry EQ.
inline uint32_t low_cut_hz(q15 raw) {
    const uint64_t u = unipolar(raw);
    return u ? 20u + uint32_t((980ull * u * u + 536838144ull) / 1073676289ull) : 0u;
}
inline uint32_t duck_percent(q15 raw) { return (100u * unipolar(raw) + 16383u) / 32767u; }
inline int32_t low_cut_pole(q15 raw) {
    // Bilinear one-pole highpass: pi~=355/113. The analog nominal cutoff is
    // within 0.4% of digital cutoff at 1 kHz/32 kHz; no runtime transcendental.
    const int64_t sr = int64_t(ENGINE_SR) * 113;
    const int64_t f = int64_t(low_cut_hz(raw)) * 355;
    return int32_t((sr - f) * ONE / (sr + f));
}

// Nominal low-frequency -60 dB amplitude time, not an acoustic-room measurement.
inline uint32_t decay_ms(q15 raw) {
    const uint64_t u = unipolar(raw);
    constexpr uint64_t denominator = 32767ull * 32767ull;
    return 200u + uint32_t((7800ull * u * u + denominator / 2) / denominator);
}

inline uint32_t length_samples(q15 raw, int lane) {
    lane = lane < 0 ? 0 : lane > 3 ? 3 : lane;
    return BASE[lane] * (32767u + 2u * unipolar(raw)) / 65534u;
}

// q20 table of 10^(-3*i/256), rounded to nearest; 516 B read-only.
// Linear interpolation is <0.0003 absolute coefficient error over the supported
// lengths/T60 domain. Only accepted SIZE/DEC changes evaluate this mapping.
constexpr int32_t ATTENUATION[129] = {
    1048576, 1020660, 993488, 967038, 941293, 916233, 891841, 868098,
    844987, 822491, 800594, 779280, 758534, 738340, 718683, 699550,
    680926, 662798, 645153, 627977, 611259, 594985, 579145, 563727,
    548719, 534111, 519891, 506050, 492578, 479464, 466700, 454275,
    442181, 430409, 418950, 407797, 396940, 386372, 376086, 366074,
    356328, 346842, 337608, 328620, 319871, 311355, 303066, 294998,
    287144, 279500, 272059, 264816, 257766, 250903, 244223, 237722,
    231393, 225233, 219236, 213400, 207718, 202188, 196806, 191566,
    186466, 181502, 176670, 171966, 167388, 162932, 158594, 154372,
    150262, 146262, 142368, 138578, 134888, 131297, 127802, 124400,
    121088, 117864, 114726, 111672, 108699, 105805, 102988, 100246,
    97578, 94980, 92451, 89990, 87594, 85262, 82992, 80783,
    78632, 76539, 74501, 72518, 70587, 68708, 66879, 65098,
    63365, 61678, 60036, 58438, 56882, 55368, 53894, 52459,
    51062, 49703, 48380, 47092, 45838, 44618, 43430, 42274,
    41148, 40053, 38986, 37948, 36938, 35955, 34998, 34066,
    33159,
};
inline int32_t attenuation(uint32_t samples, uint32_t milliseconds) {
    if (milliseconds < 200) milliseconds = 200;
    if (milliseconds > 8000) milliseconds = 8000;
    const uint32_t position = uint32_t((uint64_t(samples) * 16777216u) /
                                        (milliseconds * uint32_t(ENGINE_SR / 1000)));
    const uint32_t index = position >> 16;
    if (index >= 128) return ATTENUATION[128];
    return ATTENUATION[index] - int32_t((uint32_t(ATTENUATION[index] - ATTENUATION[index + 1]) *
                                       (position & 65535u)) >> 16);
}

// A true open endpoint and progressively darker feedback only (~208 Hz at max).
inline int32_t damping_alpha(q15 raw) {
    const uint64_t open = 32767u - unipolar(raw);
    constexpr int32_t dark = (ONE + 12) / 25;
    constexpr uint64_t denominator = 32767ull * 32767ull;
    return dark + int32_t((uint64_t(ONE - dark) * open * open + denominator / 2) / denominator);
}
// 1/(1+x), x in [0,4], q20 knots spaced at 1/16. Bounded table interpolation
// avoids a variable 64-bit division on every audio sample (ARM has no divider).
constexpr int32_t DUCK_GAIN[65] = {
    1048576, 986895, 932068, 883011, 838861, 798915, 762601, 729444,
    699051, 671089, 645278, 621378, 599186, 578525, 559241, 541201,
    524288, 508401, 493448, 479349, 466034, 453438, 441506, 430185,
    419430, 409201, 399458, 390168, 381300, 372827, 364722, 356962,
    349525, 342392, 335544, 328965, 322639, 316551, 310689, 305040,
    299593, 294337, 289262, 284360, 279620, 275036, 270600, 266305,
    262144, 258111, 254200, 250406, 246724, 243148, 239675, 236299,
    233017, 229825, 226719, 223696, 220753, 217886, 215093, 212370,
    209715
};
inline int32_t duck_gain(int32_t reduction) {
    const uint32_t x = uint32_t(reduction);
    const uint32_t index = x >> 16;
    if (index >= 64) return DUCK_GAIN[64];
    return DUCK_GAIN[index] - int32_t((uint32_t(DUCK_GAIN[index] - DUCK_GAIN[index + 1]) *
                                       (x & 65535u)) >> 16);
}

inline int32_t amount(q15 raw) {
    return int32_t(uint64_t(unipolar(raw)) * ONE / 32767u);
}
inline int32_t multiply(int32_t value, int32_t coefficient) {
    // Truncation TOWARD zero, rather than a negative arithmetic-shift bias.
    return int32_t((int64_t(value) * coefficient) / ONE);
}
inline int32_t blend(int32_t a, int32_t b, int32_t coefficient) {
    return int32_t((int64_t(a) * ONE + int64_t(b - a) * coefficient) / ONE);
}
inline int32_t limit(int64_t value) {
    return value > LIMIT ? LIMIT : value < -LIMIT ? -LIMIT : int32_t(value);
}
inline void scatter(const int32_t (&x)[4], int32_t (&y)[4]) {
    // H/2 is orthogonal. Each signed /2 rounds toward zero, so the transform
    // cannot add vector energy even at the last internal fractional audio bit.
    // Inputs are bounded to +/-LIMIT; all four-term sums fit signed int32.
    y[0] = (x[0] + x[1] + x[2] + x[3]) / 2;
    y[1] = (x[0] - x[1] + x[2] - x[3]) / 2;
    y[2] = (x[0] + x[1] - x[2] - x[3]) / 2;
    y[3] = (x[0] - x[1] - x[2] + x[3]) / 2;
}

struct State; // Audio-owned fixed allocation; no UI code observes its storage.

} // namespace seance::reverb_dsp
