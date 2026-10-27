// sigil - drawable cv source. the soul of seance.
//
// a hand-drawn curve (stylus on the bottom screen) that becomes a modulator:
//   Loop     - free-running lfo of arbitrary shape (rate hz or tempo-sync later)
//   OneShot  - drawn envelope, fired by gate/trig
//   Steps    - curve quantized to 16 steps -> drawable step sequencer
//
// data: 128 points, q15 [-1..+1]. drawing writes via set_point / stroke_to
// (lerp between sparse touch samples). playback: 32-bit phase accumulator,
// lerp between table points, evaluated at CONTROL RATE (once per block),
// so chains of many sigils cost near-zero cpu.
//
// chaining: sigils are evaluated in slot order; a sigil's output can modulate
// a later sigil's rate/depth (see engine.h routing).
#pragma once
#include "../audio/fixed.h"

namespace seance {

constexpr int SIGIL_POINTS = 128;

enum class SigilMode : uint8_t { Loop = 0, OneShot, Steps, RandomStep, RandomSmooth, Follow, Count };

inline const char* sigil_mode_name(SigilMode m) {
    switch (m) {
        case SigilMode::Loop:         return "loop";
        case SigilMode::OneShot:      return "shot";
        case SigilMode::Steps:        return "step";
        case SigilMode::RandomStep:   return "rnd";
        case SigilMode::RandomSmooth: return "smooth";
        case SigilMode::Follow:       return "follow";
        default:                 return "?";
    }
}

struct Sigil {
    // === the drawing ===
    trackr::fx::q15 table[SIGIL_POINTS] = {};   // hand-drawn curve
    bool     drawn = false;                     // anything on the canvas?

    // === playback params ===
    SigilMode mode  = SigilMode::Loop;
    uint32_t phase      = 0;                    // 0..0xFFFFFFFF = one pass
    uint32_t phase_inc  = 0;                    // set via set_rate_*
    bool     running    = true;                 // OneShot: false after completing
    trackr::fx::q15 depth = trackr::fx::Q15_ONE; // output attenuverter (+/-)
    trackr::fx::q15 value = 0;                  // last evaluated output (read by router)
    uint32_t random_state = 0x53494731u;
    trackr::fx::q15 random_from = 0, random_to = 0;
    bool follow_active = false;

    // rate as normalized q15 [0..32767] -> 0.05Hz .. 40Hz exponential-ish
    // (control-rate eval, so even 40hz is fine at 1khz control rate)
    void set_rate_norm(trackr::fx::q15 norm, int control_rate_hz) {
        // piecewise-linear approx of exp: 0.05 + (n/32767)^2 * 40 hz
        int64_t n = norm;
        int64_t n2 = (n * n) >> 15;                     // q15
        // freq_q16 = 0.05*65536 + n2 * 40 * 65536 / 32767  ~= 3277 + n2*80
        int64_t freq_q16 = 3277 + n2 * 80;
        phase_inc = (uint32_t)((freq_q16 << 16) / (uint32_t)control_rate_hz);
    }

    void set_rate_sync(int bpm, int division, int control_rate_hz) {
        static const uint8_t NUM[6] = { 1, 1, 1, 2, 4, 8 };
        static const uint8_t DEN[6] = { 4, 2, 1, 1, 1, 1 };
        if (division < 0) division = 0;
        if (division > 5) division = 5;
        if (bpm < 20) bpm = 20;
        uint64_t num = (uint64_t)(uint32_t)bpm * NUM[division] * 0x100000000ULL;
        uint64_t den = (uint64_t)60 * DEN[division] * (uint32_t)control_rate_hz;
        phase_inc = (uint32_t)(num / den);
    }

    // Fractional clock path; integral wrapper above remains bit-identical.
    void set_rate_sync_q16(int32_t bpm_q16, int division, int control_rate_hz) {
        static const uint8_t NUM[6] = {1, 1, 1, 2, 4, 8};
        static const uint8_t DEN[6] = {4, 2, 1, 1, 1, 1};
        if (division < 0) division = 0;
        if (division > 5) division = 5;
        if (bpm_q16 < 20 * 65536) bpm_q16 = 20 * 65536;
        if (bpm_q16 > 300 * 65536) bpm_q16 = 300 * 65536;
        phase_inc = (uint32_t)(((uint64_t)(uint32_t)bpm_q16 * NUM[division] * 65536u) /
            ((uint64_t)60 * DEN[division] * (uint32_t)control_rate_hz));
    }

    // === drawing api ===
    // x [0..SIGIL_POINTS-1], y q15. call per touch sample; use stroke_to for
    // sparse touches (fast strokes skip columns - we lerp the gap).
    void set_point(int x, trackr::fx::q15 y) {
        if (x < 0) x = 0;
        if (x >= SIGIL_POINTS) x = SIGIL_POINTS - 1;
        table[x] = y;
        drawn = true;
    }
    // connect (x0,y0)..(x1,y1) with a line - fills skipped columns
    void stroke_to(int x0, trackr::fx::q15 y0, int x1, trackr::fx::q15 y1) {
        if (x0 == x1) { set_point(x1, y1); return; }
        if (x0 > x1) { int tx = x0; x0 = x1; x1 = tx;
                       trackr::fx::q15 ty = y0; y0 = y1; y1 = ty; }
        for (int x = x0; x <= x1; ++x) {
            int32_t t = (int32_t)(x - x0) * 32767 / (x1 - x0);
            int32_t y = y0 + (((int32_t)(y1 - y0)) * t >> 15);
            set_point(x, (trackr::fx::q15)y);
        }
    }
    void clear() {
        for (auto& p : table) p = 0;
        drawn = false;
    }

    // === transport ===
    void trigger() {          // gate for OneShot / phase reset for Loop
        phase = 0;
        running = true;
    }

    // PLAY-follow mode is driven by the actual playback cursor, not an estimated
    // timer. The first PLAY in the destination chain owns the phase; no PLAY means
    // silence, so FOLLOW can never pretend to track a sample that is not running.
    void follow(uint32_t playback_phase, bool active) {
        phase = playback_phase;
        follow_active = active;
    }

    // one control-rate step -> q15 output (also cached in .value)
    trackr::fx::q15 step() {
        using trackr::fx::q15;
        if (!drawn && mode != SigilMode::RandomStep && mode != SigilMode::RandomSmooth) { value = 0; return 0; }

        if (mode == SigilMode::Follow) {
            if (!follow_active) { value = 0; return value; }
        } else if (mode == SigilMode::OneShot) {
            if (!running) { value = 0; return value; }
            uint32_t prev = phase;
            phase += phase_inc;
            if (phase < prev) {            // wrapped = envelope done
                running = false;
                phase = 0xFFFFFFFF;
                q15 v = (q15)(((int32_t)table[SIGIL_POINTS - 1] * depth) >> 15);
                value = v;
                return v;
            }
        } else {
            uint32_t prev = phase;
            phase += phase_inc;
            if ((mode == SigilMode::RandomStep || mode == SigilMode::RandomSmooth) && phase < prev) {
                random_state = random_state * 1664525u + 1013904223u;
                random_from = random_to;
                random_to = (q15)(random_state >> 16);
            }
        }

        // phase -> table position
        uint32_t pos = (uint32_t)(((uint64_t)phase * SIGIL_POINTS) >> 32); // 0..127
        q15 raw;
        if (mode == SigilMode::RandomStep) {
            raw = random_to;
        } else if (mode == SigilMode::RandomSmooth) {
            uint16_t t = (uint16_t)(phase >> 16);
            // smoothstep t*t*(3-2t), q16, calculated only at control rate.
            uint32_t t2 = ((uint32_t)t * t) >> 16;
            uint32_t sm = (t2 * (196608u - ((uint32_t)t << 1))) >> 16;
            if (sm > 65535u) sm = 65535u;
            raw = trackr::fx::lerp_q15(random_from, random_to, (uint16_t)sm);
        } else if (mode == SigilMode::Steps) {
            // quantize to 16 steps: sample the middle of each 8-point bucket
            uint32_t stp = pos >> 3;                    // 0..15
            raw = table[stp * 8 + 4];
        } else {
            // lerp between pos and pos+1
            uint32_t frac = (uint32_t)((((uint64_t)phase * SIGIL_POINTS) >> 16) & 0xFFFF);
            int nxt = (pos + 1) % SIGIL_POINTS;
            if (mode == SigilMode::OneShot && nxt == 0) nxt = SIGIL_POINTS - 1;
            raw = trackr::fx::lerp_q15(table[pos], table[nxt], frac);
        }
        value = (q15)(((int32_t)raw * depth) >> 15);
        return value;
    }

    // Steps mode helper: current step index + "did we cross into a new step"
    // (for firing triggers into downstream units)
    int current_step() const {
        return (int)((((uint64_t)phase * SIGIL_POINTS) >> 32) >> 3);
    }
};

} // namespace seance
