// Included by units.cpp. Four-line mono FDN; legacy process stays pinned there.
#include "units.h"
#include "reverb_dsp.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace seance::reverb_dsp {

struct Ramp {
    int32_t value = 0, target = 0, step = 0;
    uint16_t remaining = 0;
    void snap(int32_t next) { value = target = next; step = 0; remaining = 0; }
    void set(int32_t next) {
        if (target == next) return;
        target = next; remaining = RAMP_FRAMES; step = (next - value) / RAMP_FRAMES;
    }
    int32_t tick() {
        if (remaining && --remaining == 0) value = target;
        else if (remaining) value += step;
        return value;
    }
};
struct Highpass {
    int32_t input = 0, output = 0;
    int32_t tick(int32_t next) {
        // ~10 Hz, <=unity gain. Zero input converges to exact zero, with no
        // negative shift bias or lowpass-estimator deadband. Wet/send only.
        output = limit((int64_t(next - input) + output) * 511 / 512);
        input = next;
        return output;
    }
};
struct SendCut {
    int32_t input = 0, output = 0;
    int32_t tick(int32_t next, int32_t pole) {
        if (pole == ONE) output = next; // exact OFF, also warms state for enable
        else output = limit((int64_t(output) * pole +
                             int64_t(next - input) * ((ONE + pole) / 2)) / ONE);
        input = next;
        return output;
    }
};
inline void envelope(int32_t value, int32_t& meter) {
    const int32_t magnitude = std::min<int32_t>(32767, (value < 0 ? -value : value) / AUDIO_SCALE) * 256;
    if (magnitude >= meter) meter = magnitude;
    else meter -= std::max<int32_t>(1, meter / 8192);
}
struct State {
    int32_t memory[STORAGE_SAMPLES] = {};
    int32_t predelay[PREDELAY_CAPACITY] = {};
    uint32_t pre_write = 0, pre_a = 0, pre_b = 0;
    uint16_t pre_pos = 0;
    q15 pre_raw_a = 0, pre_raw_b = 0, pre_pending = 0;
    bool pre_changing = false;
    Ramp cut_pole, duck_amount;
    SendCut send_cut;
    int32_t cut_cache = -1, duck_cache = -1, detector = 0, duck_gain = ONE;
    int32_t returns[4] = {};
    uint32_t write[4] = {}, length_a[4] = {}, length_b[4] = {};
    int32_t low[4] = {};
    Ramp gain_a[4], gain_b[4], damp, damp_raw, mix, excitation_gate, hold;
    Highpass input_hp, output_hp;
    uint32_t diffuse_a = 0, diffuse_b = 0;
    uint16_t size_pos = 0;
    q15 size_a = 0, size_b = 0, pending_size = 0;
    int32_t decay_cache = -1, damp_cache = -1, mix_cache = -1;
    int32_t energy = 0; // q15 magnitude with eight fractional bits, audio-rate
    bool initialized = false, changing_size = false, freeze_requested = false;

    void reset() {
        // No large temporary on the audio/main stack; reset is bounded, no heap.
        std::memset(memory, 0, sizeof memory);
        std::memset(predelay, 0, sizeof predelay);
        pre_write = pre_a = pre_b = pre_pos = 0;
        pre_raw_a = pre_raw_b = pre_pending = 0; pre_changing = false;
        cut_pole = {}; duck_amount = {}; send_cut = {};
        cut_cache = duck_cache = -1; detector = 0; duck_gain = ONE;
        for (auto& value : returns) value = 0;
        for (int lane = 0; lane < 4; ++lane) {
            write[lane] = length_a[lane] = length_b[lane] = 0;
            low[lane] = 0; gain_a[lane] = {}; gain_b[lane] = {};
        }
        damp = {}; damp_raw = {}; mix = {}; excitation_gate = {}; hold = {};
        input_hp = {}; output_hp = {};
        diffuse_a = diffuse_b = size_pos = 0;
        size_a = size_b = pending_size = 0;
        decay_cache = damp_cache = mix_cache = -1;
        energy = 0; initialized = changing_size = freeze_requested = false;
    }
    void begin_size() {
        if (pending_size == size_a) return;
        size_b = pending_size; changing_size = true; size_pos = 0;
        const uint32_t milliseconds = decay_ms(q15(decay_cache));
        for (int lane = 0; lane < 4; ++lane) {
            length_b[lane] = length_samples(size_b, lane);
            // The new head enters at zero weight: no coefficient ramp is needed.
            gain_b[lane].snap(attenuation(length_b[lane], milliseconds));
        }
    }
    void prepare(const Param* params) {
        using R = ReverbUnit;
        pending_size = q15(unipolar(params[R::P_SIZE].effective));
        const int32_t next_decay = int32_t(unipolar(params[R::P_DECAY].effective));
        const int32_t next_damp = int32_t(unipolar(params[R::P_DAMP].effective));
        const int32_t next_mix = int32_t(unipolar(params[R::P_MIX].effective));
        const bool want_freeze = params[R::P_FREEZE].effective > 16384;
        pre_pending = q15(unipolar(params[R::P_PREDELAY].effective));
        const int32_t next_cut = int32_t(unipolar(params[R::P_LOW_CUT].effective));
        const int32_t next_duck = int32_t(unipolar(params[R::P_DUCK].effective));
        if (!initialized) {
            pre_raw_a = pre_raw_b = pre_pending;
            pre_a = pre_b = predelay_samples(pre_pending);
            cut_pole.snap(low_cut_pole(q15(next_cut))); cut_cache = next_cut;
            duck_amount.snap(want_freeze ? 0 : amount(q15(next_duck))); duck_cache = next_duck;
            size_a = size_b = pending_size;
            const uint32_t milliseconds = decay_ms(q15(next_decay));
            for (int lane = 0; lane < 4; ++lane) {
                length_a[lane] = length_b[lane] = length_samples(size_a, lane);
                gain_a[lane].snap(attenuation(length_a[lane], milliseconds));
                gain_b[lane] = gain_a[lane];
            }
            damp.snap(damping_alpha(q15(next_damp))); damp_raw.snap(next_damp);
            mix.snap(amount(q15(next_mix)));
            decay_cache = next_decay; damp_cache = next_damp; mix_cache = next_mix;
            initialized = true;
        }
        freeze_requested = want_freeze;
        if (cut_cache != next_cut) { cut_cache = next_cut; cut_pole.set(low_cut_pole(q15(next_cut))); }
        duck_cache = next_duck;
        // Freeze owns wet gain: unity within the same 64 samples as excitation
        // closure, regardless of subsequent input. Never mutate the saved amount.
        duck_amount.set(want_freeze ? 0 : amount(q15(next_duck)));
        excitation_gate.set(want_freeze ? ONE : 0);
        // Never capture a permanent blended tap (which would be lossy and could
        // re-use memory twice). Finish the ONE already committed size transition
        // at ordinary attenuation, then enter unity in exactly 64 more samples.
        hold.set(want_freeze && !changing_size ? ONE : 0);
        if (mix_cache != next_mix) {
            mix_cache = next_mix; mix.set(amount(q15(next_mix)));
        }
        if (want_freeze) return; // MIX is live; SIZE, DEC and DAMP stay captured.
        if (damp_cache != next_damp) {
            damp_cache = next_damp;
            damp.set(damping_alpha(q15(next_damp))); damp_raw.set(next_damp);
        }
        if (decay_cache != next_decay) {
            decay_cache = next_decay;
            const uint32_t milliseconds = decay_ms(q15(next_decay));
            for (int lane = 0; lane < 4; ++lane) {
                gain_a[lane].set(attenuation(length_a[lane], milliseconds));
                if (changing_size) gain_b[lane].set(attenuation(length_b[lane], milliseconds));
            }
        }
    }
    int32_t predelay_tick(int32_t value) {
        if (!pre_changing && pre_pending != pre_raw_a) {
            pre_raw_b = pre_pending; pre_b = predelay_samples(pre_raw_b);
            if (pre_a == pre_b) pre_raw_a = pre_raw_b;
            else { pre_changing = true; pre_pos = 0; }
        }
        predelay[pre_write] = value; // history is always after the excitation gate
        const auto tap = [&](uint32_t delay) {
            return predelay[pre_write >= delay ? pre_write - delay : pre_write + PREDELAY_CAPACITY - delay];
        };
        int32_t out = tap(pre_a);
        if (pre_changing) {
            out = blend(out, tap(pre_b), int32_t(++pre_pos) * ONE / PREDELAY_FRAMES);
            if (pre_pos == PREDELAY_FRAMES) {
                pre_changing = false; pre_pos = 0; pre_a = pre_b; pre_raw_a = pre_raw_b;
            }
        }
        if (++pre_write == PREDELAY_CAPACITY) pre_write = 0;
        return out;
    }
    void detector_tick(int32_t gated) {
        const int32_t magnitude = (gated < 0 ? -gated : gated) / (AUDIO_SCALE / 256);
        const int32_t delta = magnitude - detector;
        const int32_t divisor = delta > 0 ? 64 : 6400; // ~2ms attack / 200ms release
        if (delta > 0) detector += std::max<int32_t>(1, delta / divisor);
        else if (delta < 0) detector -= std::max<int32_t>(1, -delta / divisor);
        const int32_t amt = duck_amount.tick();
        const int32_t reduction = int32_t((int64_t(amt) * detector) / (32768 * 256 / 4));
        duck_gain = reverb_dsp::duck_gain(reduction);
    }
    int32_t read(int lane, uint32_t delay) const {
        const uint32_t position = write[lane] >= delay ? write[lane] - delay :
                                  write[lane] + CAPACITY[lane] - delay;
        return memory[OFFSET[lane] + position];
    }
    int32_t diffuse(int32_t value) {
        // Two short Schroeder allpasses, g=1/2; no modulation/transcendentals.
        // Four-times attenuated send leaves ample headroom for their state.
        int32_t* const first = memory + FDN_SAMPLES;
        int32_t out = first[diffuse_a] - value / 2;
        first[diffuse_a] = limit(int64_t(value) + out / 2);
        if (++diffuse_a == DIFFUSER_A) diffuse_a = 0;
        value = out;
        int32_t* const second = first + DIFFUSER_A;
        out = second[diffuse_b] - value / 2;
        second[diffuse_b] = limit(int64_t(value) + out / 2);
        if (++diffuse_b == DIFFUSER_B) diffuse_b = 0;
        return limit(out);
    }
};
static_assert(sizeof(State) <= 52 * 1024, "REV fixed state budget including 4801 q25 predelay samples");
static_assert(4ll * LIMIT < INT32_MAX, "bounded Hadamard sums");

} // namespace seance::reverb_dsp

namespace seance {

ReverbUnit::ReverbUnit(bool modern) : modern_(modern) {
    kind = UnitKind::Reverb;
    // Every historical raw index AND default stays unchanged.
    add_param("SIZE", 18000); add_param("DEC", 20000); add_param("MIX", 9000);
    add_param("DAMP", 9000); add_param("FRZ", 0);
    add_param("PRE", 0); add_param("CUT", 0); add_param("DUCK", 0);
    (void)set_modern(modern);
}
ReverbUnit::~ReverbUnit() { delete[] buf_; delete dense_; }
bool ReverbUnit::set_modern(bool modern) {
    if (modern == modern_ && ready()) return true;
    if (modern) {
        auto* next = new(std::nothrow) reverb_dsp::State;
        if (!next) return false;
        delete[] buf_; buf_ = nullptr;
        delete dense_; dense_ = next;
    } else {
        q15* next = new(std::nothrow) q15[N];
        if (!next) return false;
        delete dense_; dense_ = nullptr;
        delete[] buf_; buf_ = next;
    }
    modern_ = modern;
    num_params = modern ? P_COUNT : P_PREDELAY;
    reset();
    return true;
}
void ReverbUnit::reset() {
    if (buf_) std::memset(buf_, 0, N * sizeof(q15));
    if (dense_) dense_->reset();
    a_ = b_ = c_ = 0;
    damp_a_ = damp_b_ = damp_c_ = 0;
    visual_size_ = -1;
    freeze_mix_ = 0;
    visual_energy_ = visual_damping_ = 0;
    for (auto& value : visual_effective_) value = 0;
    visual_ready_ = false;
}
q15 ReverbUnit::visual_effective(int p) const {
    return visual_effective_[p < 0 ? 0 : p >= P_COUNT ? P_COUNT - 1 : p];
}
q15 ReverbUnit::visual_return(int lane) const {
    return modern_ && dense_ ? q15(dense_->returns[lane < 0 ? 0 : lane > 3 ? 3 : lane] / 256) : 0;
}
q15 ReverbUnit::visual_duck_gain() const {
    return modern_ && dense_ ? q15(int64_t(dense_->duck_gain) * 32767 / reverb_dsp::ONE) : 32767;
}
q15 ReverbUnit::visual_size() const {
    using namespace reverb_dsp;
    if (modern_ && dense_ && dense_->initialized) {
        if (!dense_->changing_size) return dense_->size_a;
        return q15(blend(dense_->size_a, dense_->size_b,
                         int32_t(dense_->size_pos) * ONE / SIZE_FRAMES));
    }
    return visual_size_ < 0 ? q15(unipolar(params[P_SIZE].effective)) : visual_size_;
}
bool ReverbUnit::visual_transition() const {
    return modern_ && dense_ && dense_->changing_size;
}
void ReverbUnit::process(q15* buf, int frames) {
    if (!buf || frames <= 0 || !ready()) return;
    if (modern_) process_modern(buf, frames);
    else {
        visual_size_ = q15(reverb_dsp::unipolar(params[P_SIZE].effective));
        process_legacy(buf, frames);
        for (int p = 0; p <= P_FREEZE; ++p) visual_effective_[p] = params[p].effective;
    }
    visual_ready_ = true;
}
void ReverbUnit::process_modern(q15* buf, int frames) {
    using namespace reverb_dsp;
    State& state = *dense_;
    state.prepare(params);
    for (int frame = 0; frame < frames; ++frame) {
        if (!state.changing_size && !state.freeze_requested && state.hold.value == 0)
            state.begin_size(); // latest requested size is coalesced, never re-anchored mid-fade
        const int32_t dry = int32_t(buf[frame]) * AUDIO_SCALE;
        const int32_t closed = state.excitation_gate.tick(), held = state.hold.tick();
        const int32_t mix = state.mix.tick(), alpha = state.damp.tick();
        state.damp_raw.tick();
        // Gate BEFORE all send state as well as AFTER diffusion. Audio received
        // while fully frozen can never enter the tank, even later on release.
        const int32_t gated = multiply(dry, ONE - closed);
        state.detector_tick(gated);
        const int32_t delayed = state.predelay_tick(gated);
        const int32_t send = state.input_hp.tick(state.send_cut.tick(delayed, state.cut_pole.tick())) / 4;
        const int32_t excitation = multiply(state.diffuse(send), ONE - closed);
        const int32_t weight = state.changing_size ? int32_t(++state.size_pos) * ONE / SIZE_FRAMES : 0;
        int32_t raw[4], returned[4], scattered[4];
        for (int lane = 0; lane < 4; ++lane) {
            const int32_t a = state.read(lane, state.length_a[lane]);
            int32_t attenuated = multiply(a, state.gain_a[lane].tick());
            raw[lane] = a;
            if (state.changing_size) {
                const int32_t b = state.read(lane, state.length_b[lane]);
                raw[lane] = blend(a, b, weight);
                attenuated = blend(attenuated, multiply(b, state.gain_b[lane].tick()), weight);
            }
            envelope(raw[lane], state.returns[lane]);
            // Single rounded weighted sum, not increment-with-deadband: normal
            // tails have no self-sustaining integer LP floor. Alpha=ONE is open.
            state.low[lane] = blend(state.low[lane], attenuated, alpha);
            returned[lane] = held == ONE ? raw[lane] : held == 0 ? state.low[lane] :
                             blend(state.low[lane], raw[lane], held);
        }
        scatter(returned, scattered);
        for (int lane = 0; lane < 4; ++lane) {
            const int32_t injection = lane == 1 ? -excitation : excitation;
            state.memory[OFFSET[lane] + state.write[lane]] = limit(int64_t(scattered[lane]) + injection);
            if (++state.write[lane] == CAPACITY[lane]) state.write[lane] = 0;
        }
        // Signed projection, 12 dB wet summing headroom; no dry EQ or dry gain.
        const int32_t wet = state.output_hp.tick((raw[0] + raw[1] - raw[2] + raw[3]) / 4);
        const int32_t magnitude = std::min<int32_t>(32767, (wet < 0 ? -wet : wet) / AUDIO_SCALE) * 256;
        if (magnitude >= state.energy) state.energy = magnitude;
        else state.energy -= std::max<int32_t>(1, state.energy / 8192);
        const int32_t audible_wet = state.duck_gain == ONE ? wet : multiply(wet, state.duck_gain);
        buf[frame] = trackr::fx::sat_q15(blend(dry, audible_wet, mix) / AUDIO_SCALE);
        if (state.changing_size && state.size_pos == SIZE_FRAMES) {
            state.changing_size = false; state.size_pos = 0; state.size_a = state.size_b;
            for (int lane = 0; lane < 4; ++lane) {
                state.length_a[lane] = state.length_b[lane];
                state.gain_a[lane] = state.gain_b[lane];
            }
            if (state.freeze_requested) state.hold.set(ONE);
        }
    }
    visual_energy_ = q15(state.energy / 256);
    visual_effective_[P_SIZE] = state.changing_size ? state.size_b : state.size_a;
    visual_effective_[P_DECAY] = q15(state.decay_cache);
    visual_effective_[P_DAMP] = q15(state.damp_cache);
    visual_effective_[P_MIX] = q15(state.mix_cache);
    visual_effective_[P_FREEZE] = state.freeze_requested ? 32767 : 0;
    visual_effective_[P_PREDELAY] = state.pre_changing ? state.pre_raw_b : state.pre_raw_a;
    visual_effective_[P_LOW_CUT] = q15(state.cut_cache);
    visual_effective_[P_DUCK] = q15(state.duck_cache);
    visual_damping_ = q15(state.damp_raw.value);
    freeze_mix_ = state.hold.value == ONE ? 32767 : q15((int64_t(state.hold.value) * 32767) / ONE);
}

} // namespace seance
