// Clean OSC local-rate helpers. Original implementation; no borrowed code.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace seance { namespace osc_clean {
constexpr int TAPS = 47;
constexpr float HEADROOM = 0.6016473f; // 1 / sum(abs(FIR)); includes float margin
constexpr float INV_PHASE = 1.0f / 4294967296.0f;
// 47-tap symmetric Blackman-windowed sinc, fc=.2 at 64kHz, DC normalized.
// Zero taps are mathematical sinc/window zeros, not runtime approximations.
constexpr float FIR[24] = {
    0, 1.4376542386e-05f, 9.9398984878e-05f, 0,
    -4.7156853153e-04f, -5.0332077891e-04f, 8.0635632836e-04f,
    1.9842747916e-03f, 0, -4.1195252670e-03f, -3.5246971626e-03f,
    4.7809022764e-03f, 1.0317072328e-02f, 0, -1.7672832082e-02f,
    -1.4124034046e-02f, 1.8217326168e-02f, 3.8136271687e-02f, 0,
    -6.6907117675e-02f, -5.8202704868e-02f, 9.0728290960e-02f,
    3.0043403672e-01f, 4.0001498725e-01f
};
inline const float* sine_table() {
    static const struct Table {
        float v[1025];
        Table() {
            for (int i = 0; i < 1024; ++i)
                v[i] = float(std::sin(6.2831853071795864769 * i / 1024.0));
            v[1024] = 0;
        }
    } table;
    return table.v;
}
inline float sine(uint32_t phase) {
    const unsigned i = phase >> 22;
    const float f = float(phase & 0x3fffffu) * (1.0f / 4194304.0f);
    const float* t = sine_table();
    return t[i] + (t[i + 1] - t[i]) * f;
}
inline float pitch_hz(int raw, bool quantize) {
    float note = 24.0f + std::max(0, std::min(32767, raw)) * (72.0f / 32768.0f);
    if (quantize) note = std::floor(note + .5f);
    return 440.0f * std::exp2((note - 69.0f) / 12.0f);
}
inline int floor_small(float x) { const int n = int(x); return n - (x < float(n)); }
inline float triangle(float phase) {
    return phase < .25f ? 4 * phase : (phase < .75f ? 2 - 4 * phase : 4 * phase - 4);
}
// A step at fractional time t between previous and current samples. The
// previous sample is retained for one internal frame, permitting true event
// timing (including backwards-moving PWM edges), not a width-static polyBLEP.
inline void step_event(float a, float b, float jump, float& previous, float& current) {
    const int ia = floor_small(a), ib = floor_small(b);
    if (ia == ib) return;
    const bool forward = b > a;
    const float edge = float(forward ? ia + 1 : ia);
    const float t = std::max(0.0f, std::min(1.0f, (edge - a) / (b - a)));
    if (!forward) jump = -jump;
    const float before = 1 - t;
    previous += .5f * jump * before * before;
    current -= .5f * jump * t * t;
}
// Integral of the step residual: derivative discontinuity smoothing (BLAMP).
inline void ramp_event(float a, float b, float slope_jump, float& previous, float& current) {
    if (floor_small(a) == floor_small(b)) return;
    const float t = std::max(0.0f, std::min(1.0f, (float(floor_small(a) + 1) - a) / (b - a)));
    const float before = 1 - t;
    previous += slope_jump * before * before * before * (1.0f / 6.0f);
    current += slope_jump * t * t * t * (1.0f / 6.0f);
}
inline float fold(float x) {
    const float u = x - 4.0f * std::floor((x + 1.0f) * .25f);
    return u <= 1.0f ? u : 2.0f - u;
}
inline float fold_integral(float x) {
    const float u = x - 4.0f * std::floor((x + 1.0f) * .25f);
    return u <= 1.0f ? .5f * u * u : 2.0f * u - .5f * u * u - 1.0f;
}
inline float fold_adaa(float x, float previous) {
    // Same linear segment gives an exact average without cancellation. Across
    // an extremely close corner use midpoint (error <= |dx|/2, <=.0005 FS).
    if (std::floor((x + 1) * .5f) == std::floor((previous + 1) * .5f))
        return .5f * (fold(x) + fold(previous));
    const float delta = x - previous;
    if (std::fabs(delta) < .001f) return fold(.5f * (x + previous));
    return std::max(-1.0f, std::min(1.0f, (fold_integral(x) - fold_integral(previous)) / delta));
}
inline float decimate(const float* ring, int newest) {
    float value = 0;
    for (int tap = 0; tap < TAPS / 2; ++tap) {
        int a = newest - tap, b = newest - (TAPS - 1 - tap);
        if (a < 0) a += TAPS;
        if (b < 0) b += TAPS;
        value += FIR[tap] * (ring[a] + ring[b]);
    }
    int center = newest - TAPS / 2;
    if (center < 0) center += TAPS;
    return (value + FIR[TAPS / 2] * ring[center]) * HEADROOM;
}
inline int16_t q15(float value) {
    return int16_t(std::max(-32767.0f, std::min(32767.0f, value * 32767.0f)));
}
inline uint32_t random(uint32_t& state) {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
}
inline float random_bipolar(uint32_t& state) {
    return float(random(state) >> 8) * (2.0f / 16777215.0f) - 1.0f;
}
} } // namespace seance::osc_clean
