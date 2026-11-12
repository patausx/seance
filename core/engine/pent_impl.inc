// Included once by units.cpp. PENT uses only static integer tables on audio.
#include "pent_tables.h"
namespace seance {
namespace pent_dsp {
static constexpr uint32_t GAIN_SUM_Q16 = pent_tables::VOICE_GAIN_Q16[0] + pent_tables::VOICE_GAIN_Q16[1] +
    pent_tables::VOICE_GAIN_Q16[2] + pent_tables::VOICE_GAIN_Q16[3] + pent_tables::VOICE_GAIN_Q16[4];
static_assert(uint64_t(GAIN_SUM_Q16) * pent_tables::FIR_L1_Q15 < 65536ull * 32768ull,
              "fixed oscillator/FIR headroom fits q15 without a limiter");
inline int32_t positive(q15 v) { return v < 0 ? 0 : v; }
inline int32_t sine(uint32_t phase) {
    const uint32_t i = phase >> 21, f = (phase >> 5) & 65535u;
    const int32_t a = pent_tables::SINE[i];
    return a + int32_t((int64_t(pent_tables::SINE[i + 1] - a) * f) >> 16);
}
inline int32_t shape(int32_t input, int32_t drive) {
    const int32_t magnitude = input < 0 ? -input : input;
    const uint32_t x = uint32_t(magnitude) >> 7, f = uint32_t(magnitude) & 127u;
    const uint32_t position = uint32_t(drive) * 64u;
    const uint32_t row = drive >= 32767 ? 64u : position >> 15;
    const uint32_t blend = drive >= 32767 ? 0u : position & 32767u;
    const uint16_t* a = pent_tables::SHAPE + row * 257u + x;
    int32_t value = a[0] + ((int32_t(a[1]) - a[0]) * int32_t(f) >> 7);
    if (blend) {
        const uint16_t* b = a + 257;
        const int32_t other = b[0] + ((int32_t(b[1]) - b[0]) * int32_t(f) >> 7);
        value += int32_t((int64_t(other - value) * blend) >> 15);
    }
    return input < 0 ? -value : value;
}
// Control-rate only: signed centicents, +/-50 cents; interpolation of immutable exp2.
uint32_t detune_q24(int cents100) {
    const int octave = cents100 < 0 ? -1 : 0;
    const uint32_t p = uint32_t(int64_t(cents100 - octave * 120000) * 65536 / 120000);
    const uint32_t i = p >> 8, f = p & 255u;
    const uint32_t a = pent_tables::EXP2_Q24[i];
    const uint32_t result = a + uint32_t((uint64_t(pent_tables::EXP2_Q24[i + 1] - a) * f) >> 8);
    return octave < 0 ? result >> 1 : result;
}
inline uint16_t follow(uint16_t previous, uint32_t magnitude) {
    // Actual absolute activity with sample-clock peak release (~32ms), no UI timer.
    const uint16_t decayed = previous ? uint16_t(previous - ((previous + 2047u) >> 11)) : 0;
    return uint16_t(magnitude > decayed ? magnitude : decayed);
}
} // namespace pent_dsp

int PentUnit::root_note(q15 value) { return (pent_dsp::positive(value) * 120 + 16383) / 32767; }
q15 PentUnit::root_value(int midi) {
    midi = midi < 0 ? 0 : midi > 120 ? 120 : midi;
    return q15((midi * 32767 + 60) / 120);
}
uint32_t PentUnit::root_millihz(q15 value) { return pent_tables::ROOT_MILLIHZ[root_note(value)]; }
uint32_t PentUnit::ratio_q16(q15 value) {
    const uint32_t raw = uint32_t(pent_dsp::positive(value));
    for (int i = 0; i < 9; ++i)
        if (raw == uint32_t(pent_tables::RATIO_RAW[i])) return pent_tables::RATIO_Q16[i];
    const uint32_t position = uint32_t(uint64_t(raw) * 262144u / 32767u);
    const uint32_t octave = position >> 16, fraction = position & 65535u;
    const uint32_t i = fraction >> 8, f = fraction & 255u;
    const uint32_t a = pent_tables::EXP2_Q24[i];
    const uint32_t result = a + uint32_t((uint64_t(pent_tables::EXP2_Q24[i + 1] - a) * f) >> 8);
    return uint32_t((uint64_t(result) << octave) >> 10); // Q24 -> Q16, then /4
}
q15 PentUnit::ratio_value(uint32_t numerator, uint32_t denominator) {
    if (!denominator) return 0;
    const uint64_t wanted = (uint64_t(numerator) * 65536u + denominator / 2u) / denominator;
    if (wanted <= 16384) return 0;
    if (wanted >= 262144) return 32767;
    for (int i = 0; i < 9; ++i)
        if (wanted == pent_tables::RATIO_Q16[i]) return pent_tables::RATIO_RAW[i];
    // Bounded UI/config search, no log/libm; lower-bound inverse of the monotone map.
    int lo = 0, hi = 32767;
    for (int i = 0; i < 15; ++i) {
        const int mid = (lo + hi) / 2;
        if (ratio_q16(q15(mid)) < wanted) lo = mid + 1; else hi = mid;
    }
    if (lo && wanted - ratio_q16(q15(lo - 1)) < ratio_q16(q15(lo)) - wanted) --lo;
    return q15(lo);
}
int PentUnit::spread_centicents(q15 value) { return (pent_dsp::positive(value) * 5000 + 16383) / 32767; }
uint32_t PentUnit::drift_millihz(q15 value) { return uint32_t((pent_dsp::positive(value) * 2000 + 16383) / 32767); }
int PentUnit::drift_depth_centicents(q15 value) { return (pent_dsp::positive(value) * 2500 + 16383) / 32767; }
uint16_t PentUnit::percent(q15 value) { return uint16_t((pent_dsp::positive(value) * 100 + 16383) / 32767); }

PentUnit::PentUnit() {
    kind = UnitKind::Pent;
    add_param("ROOT", root_value(38)); add_param("SPREAD", DEFAULT_SPREAD);
    add_param("BOND", DEFAULT_BOND); add_param("DRIFT", DEFAULT_DRIFT);
    add_param("DRIVE", DEFAULT_DRIVE); add_param("GAIN", 32767);
    add_param("A", ratio_value(1)); add_param("B", ratio_value(5,4));
    add_param("C", ratio_value(3,2)); add_param("D", ratio_value(7,4));
    add_param("E", ratio_value(2)); add_param("DEPTH", DEFAULT_DRIFT_DEPTH);
    reset(); // does NOT configure from constructor params; clone/load are still cold
}
void PentUnit::Ramp::aim(int32_t next, bool snap) {
    if (snap) { value = target = next; step = 0; remaining = 0; }
    else if (next != target) {
        target = next; step = int32_t((int64_t(next) - value) / RAMP_SAMPLES);
        remaining = RAMP_SAMPLES;
    }
}
int32_t PentUnit::Ramp::tick() {
    if (remaining) {
        value += step;
        if (!--remaining) value = target; // exact endpoint including gain zero
    }
    return value;
}
bool PentUnit::set_width(Width value) {
    if (value != Width::Mono && value != Width::Stereo) return false;
    if (width != value) {
        width = value;
        // Width changes never fold old stereo history into mono (or duplicate mono).
        // Only the new output path's fixed FIR latency starts cold; voices continue.
        for (auto& channel : fir_) for (auto& sample : channel) sample = 0;
    }
    return true;
}
void PentUnit::reset() {
    for (int i = 0; i < VOICES; ++i) {
        phase_[i] = pent_tables::INITIAL_PHASE[i]; drift_phase_[i] = pent_tables::DRIFT_PHASE[i];
        previous_[i] = 0; increment_[i] = {}; applied_increment_[i] = 0;
    }
    for (auto& channel : fir_) for (auto& sample : channel) sample = 0;
    for (auto& control : controls_) control = {};
    bond_turns_ = {}; drift_increment_ = {};
    fir_head_ = control_clock_ = 0; configured_ = limited_ = false;
    for (auto& ratio : ratio_) ratio = 0;
    spread_ = drift_depth_ = 0;
    visual_ = Visual{};
}
void PentUnit::control_tick() {
    // Param resolution is engine-owned. Mapping/ramps run on this source's actual
    // 32-frame clock in configure(), never on arbitrary process calls/UI captures.
}
void PentUnit::configure() {
    const bool snap = !configured_;
    q15 values[P_COUNT];
    bool changed[P_COUNT];
    for (int p = 0; p < P_COUNT; ++p) {
        const Param& param = params[p];
        int v = param.slew_ready ? param.effective : param.control_source();
        // Generic integer slew can stall 1..3 LSB from an endpoint/ratio anchor.
        if (param.slew_ready && int(param.target) - v >= -3 && int(param.target) - v <= 3) v = param.target;
        values[p] = q15(v < 0 ? 0 : v);
        changed[p] = snap || int32_t(values[p]) * 65536 != controls_[p].target;
        controls_[p].aim(int32_t(values[p]) * 65536, snap);
    }
    if (changed[P_BOND])
        bond_turns_.aim(int32_t(uint64_t(uint16_t(values[P_BOND])) * pent_tables::BOND_TURNS / 32767u), snap);
    if (changed[P_DRIFT])
        drift_increment_.aim(int32_t((uint64_t(drift_millihz(values[P_DRIFT])) << 32) / 64000000u), snap);
    if (changed[P_SPREAD]) spread_ = spread_centicents(values[P_SPREAD]);
    if (changed[P_DRIFT_DEPTH]) drift_depth_ = drift_depth_centicents(values[P_DRIFT_DEPTH]);
    int note = root_note(values[P_ROOT]) + arp_voice.semitones;
    note = note < 0 ? 0 : note > 120 ? 120 : note;
    const uint32_t root = pent_tables::ROOT_MILLIHZ[note];
    limited_ = false;
    for (int i = 0; i < VOICES; ++i) {
        if (changed[P_RATIO_A + i]) ratio_[i] = ratio_q16(values[P_RATIO_A + i]);
        const int detune = (i - 2) * spread_ / 4;
        const int drift = int(int64_t(pent_dsp::sine(drift_phase_[i])) * drift_depth_ / 32767);
        const uint64_t nominal = (uint64_t(root) * ratio_[i]) >> 16;
        const uint64_t frequency = (nominal * pent_dsp::detune_q24(detune + drift)) >> 24;
        const bool limited = frequency > 12000000u;
        limited_ = limited_ || limited;
        const uint32_t hz1000 = limited ? 12000000u : uint32_t(frequency);
        increment_[i].aim(int32_t((uint64_t(hz1000) << 32) / 64000000u), snap);
    }
    configured_ = true;
}
void PentUnit::synthesize(int32_t (&sum)[2], int channels) {
    for (auto& control : controls_) control.tick();
    const int32_t depth = bond_turns_.tick(), drift_inc = drift_increment_.tick();
    const int32_t drive = controls_[P_DRIVE].value >> 16;
    int32_t next[VOICES];
    int64_t mix[2] = {};
    for (int i = 0; i < VOICES; ++i) {
        // ONLY 0->2->4->1->3->0. Incoming=(i+3)%5, outgoing=(i+2)%5.
        // Read EVERY neighbour from previous_, then commit all five below.
        const int incoming = i < 2 ? i + 3 : i - 2;
        const int outgoing = i < 3 ? i + 2 : i - 3;
        const int32_t neighbours = int32_t((int64_t(previous_[incoming]) * 44564 +
                                         int64_t(previous_[outgoing]) * 20972) >> 16);
        const int32_t pm = int32_t((int64_t(neighbours) * depth) >> 15);
        next[i] = pent_dsp::shape(pent_dsp::sine(phase_[i] + uint32_t(pm)), drive);
        visual_.phase[i] = uint16_t(phase_[i] >> 16);
        visual_.drift_phase[i] = uint16_t(drift_phase_[i] >> 16);
        visual_.level[i] = pent_dsp::follow(visual_.level[i], uint32_t(next[i] < 0 ? -next[i] : next[i]));
        // Rounded reciprocal of maximum PM, error <1 q15; shift-only on ARM11.
        const uint32_t excursion = uint32_t((uint64_t(pm < 0 ? -pm : pm) * 41176u) >> 30);
        visual_.bond[i] = pent_dsp::follow(visual_.bond[i], excursion);
        const int32_t voice = int32_t((int64_t(next[i]) * pent_tables::VOICE_GAIN_Q16[i]) >> 16);
        if (channels == 1) mix[0] += voice;
        else {
            mix[0] += (int64_t(voice) * pent_tables::PAN_LEFT[i]) >> 15;
            mix[1] += (int64_t(voice) * pent_tables::PAN_RIGHT[i]) >> 15;
        }
        applied_increment_[i] = uint32_t(increment_[i].tick());
        phase_[i] += applied_increment_[i];
        drift_phase_[i] += uint32_t(drift_inc);
    }
    for (int i = 0; i < VOICES; ++i) previous_[i] = next[i];
    // Fixed .10 per-voice headroom, then FIR, then explicit gain. No limiter,
    // AGC, compressor, noise, post-FX or signal-dependent coefficient changes.
    sum[0] = int32_t(mix[0]); sum[1] = int32_t(mix[1]);
}
void PentUnit::render(q15* buf, int frames, int channels) {
    for (int frame = 0; frame < frames; ++frame) {
        if (!control_clock_) configure();
        for (int sub = 0; sub < 2; ++sub) {
            int32_t sum[2]; synthesize(sum, channels);
            for (int ch = 0; ch < channels; ++ch) fir_[ch][fir_head_] = q15(sum[ch]);
            if (++fir_head_ == FIR_TAPS) fir_head_ = 0;
        }
        const int32_t gain = controls_[P_GAIN].value >> 16;
        bool audible = false;
        for (int ch = 0; ch < channels; ++ch) {
            // Symmetric 47 taps: 24 widened MACs/channel/frame. Circular ring,
            // no divide/modulo helper, 23.0 internal-sample / 0.359375ms delay.
            int newest = fir_head_ ? fir_head_ - 1 : FIR_TAPS - 1;
            int oldest = fir_head_;
            int64_t filtered = 0;
            for (int k = 0; k < FIR_TAPS / 2; ++k) {
                filtered += int64_t(int32_t(fir_[ch][newest]) + fir_[ch][oldest]) * pent_tables::FIR_Q15[k];
                if (--newest < 0) newest = FIR_TAPS - 1;
                if (++oldest == FIR_TAPS) oldest = 0;
            }
            filtered += int64_t(fir_[ch][newest]) * pent_tables::FIR_Q15[FIR_TAPS / 2];
            const int32_t pcm = gain ? int32_t(((filtered >> 15) * gain) >> 15) : 0;
            // Analytic L1 headroom keeps this cast below q15 range, no saturation.
            *buf++ = q15(pcm); audible = audible || pcm != 0;
        }
        for (int p = 0; p < P_COUNT; ++p) visual_.effective[p] = q15(controls_[p].value >> 16);
        ++visual_.frames; visual_.ready = true; visual_.audible = audible;
        visual_.limited = limited_;
        control_clock_ = uint8_t((control_clock_ + 1) & 31);
    }
}
void PentUnit::process(q15* buf, int frames) {
    if (!buf || frames <= 0 || width != Width::Mono) return; // no implicit stereo fold
    render(buf, frames, 1);
}
bool PentUnit::process_stereo(q15* buf, int frames) {
    if (!buf || frames <= 0 || width != Width::Stereo) return false;
    render(buf, frames, 2); return true;
}
PentUnit::Visual PentUnit::visual() const {
    Visual out = visual_;
    if (out.ready) for (int i = 0; i < VOICES; ++i)
        out.frequency_millihz[i] = uint32_t((uint64_t(applied_increment_[i]) * 64000000u + (1ull << 31)) >> 32);
    return out; // pure guarded copy/conversion; no clock, audio or RNG mutation
}
} // namespace seance
