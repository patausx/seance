// Included by units.cpp. All clean-only state is separate from historical OSC.
#include "../dsp/osc_clean.h"
namespace seance {
namespace {
uint32_t next_osc_seed() {
    // Construction order, not wall clock/address/audio block size. Construction
    // is UI/patch work under the existing engine guard, never the audio loop.
    static uint32_t sequence = 0x05c11a7e;
    sequence += 0x9e3779b9u;
    uint32_t seed = sequence;
    seed = (seed ^ (seed >> 16)) * 0x7feb352du;
    seed = (seed ^ (seed >> 15)) * 0x846ca68bu;
    return (seed ^ (seed >> 16)) | 1u;
}
}
void OscUnit::set_visual_capture(bool enabled) {
    enabled = enabled && clean && !bypass;
    if (enabled == visual_enabled_) return;
    visual_enabled_ = enabled;
    const uint32_t generation = visual_.generation + 1;
    visual_ = Visual{};
    visual_.generation = generation;
    visual_work_ = Visual{};
    visual_pitch_pending_ = 0;
    visual_cycle_started_ = false;
    visual_cycle_samples_ = visual_history_clock_ = 0;
    visual_warmup_ = CLEAN_TAPS + 2;
    visual_last_phase_ = 0;
}
void OscUnit::reset() {
    set_visual_capture(false);
    if (!clean) return; // historical reset() was inherited and deliberately empty
    clean_ready_ = false;
    clean_phase_ = 0;
    clean_rng_ = clean_seed_;
    clean_drift_count_ = 0;
    clean_drift_ = clean_drift_phase_error_ = 0;
    clean_drift_target_ = osc_clean::random_bipolar(clean_rng_);
    clean_saw_ = 1; clean_tri_ = 0; clean_pulse_ = 1;
    clean_previous_fold_ = clean_previous_sub_ = clean_previous_pre_ = 0;
    clean_fir_head_ = 0;
    std::fill(clean_fir_, clean_fir_ + CLEAN_TAPS, 0.0f);
    std::fill(clean_pre_fir_, clean_pre_fir_ + CLEAN_TAPS, 0.0f);
    std::fill(clean_phase_delay_, clean_phase_delay_ + CLEAN_TAPS, 0u);
}
void OscUnit::clean_control_tick() {
    const float inv = 1.0f / 32767.0f;
    clean_target_[P_PITCH] = osc_clean::pitch_hz(params[P_PITCH].effective, pitch_quantize, pitch_scale);
    if (arp_voice.semitones) clean_target_[P_PITCH] = std::min(14000.f, clean_target_[P_PITCH] * arp_pitch_ratio());
    clean_nominal_hz_ = osc_clean::pitch_hz(params[P_PITCH].base, pitch_quantize, pitch_scale);
    clean_target_[P_PW] = .25f + .5f * std::max(0, int(params[P_PW].effective)) * inv;
    clean_target_[P_GAIN] = std::max(0, int(params[P_GAIN].effective)) * inv;
    clean_target_[P_FOLD] = 1 + 6 * std::max(0, int(params[P_FOLD].effective)) * inv;
    clean_target_[P_SUB] = std::max(0, int(params[P_SUB].effective)) * inv;
    clean_target_[P_DRIFT] = std::max(0, int(params[P_DRIFT].effective)) * inv;
    const int shape = shape_morph ? std::max(0, int(params[P_SHAPE].effective)) : int(shape_anchor(wave));
    clean_target_[P_SHAPE] = shape <= 10922 ? float(shape) / 10922.0f :
        (shape <= 21845 ? 1 + float(shape - 10922) / 10923.0f : 2 + float(shape - 21845) / 10922.0f);
    if (!clean_ready_) {
        std::copy(clean_target_, clean_target_ + P_COUNT, clean_value_);
        std::copy(clean_value_, clean_value_ + P_COUNT, clean_previous_);
        clean_pulse_ = clean_value_[P_PW] >= .5f ? 1.0f : -1.0f;
        clean_ready_ = true;
        clean_ramp_ = 0;
    } else {
        clean_ramp_ = 2 * CONTROL_INTERVAL;
        for (int p = 0; p < P_COUNT; ++p)
            clean_step_[p] = (clean_target_[p] - clean_value_[p]) / float(2 * CONTROL_INTERVAL);
    }
    if (bypass) set_visual_capture(false);
}
void OscUnit::collect_visual(uint32_t phase, q15 pre, q15 out, float frequency) {
    visual_.frequency_millihz = uint32_t(frequency * 1000.0f + .5f);
    if (++visual_history_clock_ >= ENGINE_SR / 100) {
        visual_history_clock_ = 0;
        // Keep the actual original float division at its 100Hz timestamp.
        // A bounded private ring defers only conversion, never audio/clock work.
        visual_pitch_ratio_[visual_.pitch_head] = frequency / clean_nominal_hz_;
        visual_pitch_pending_ |= uint64_t(1) << visual_.pitch_head;
        visual_.pitch_head = uint8_t((visual_.pitch_head + 1) % Visual::HISTORY);
        if (visual_.pitch_count < Visual::HISTORY) ++visual_.pitch_count;
    }
    if (visual_warmup_) { --visual_warmup_; visual_last_phase_ = phase; return; }
    if (phase < visual_last_phase_) {
        if (visual_cycle_started_) {
            std::copy(visual_work_.pre_min, visual_work_.pre_min + Visual::COLUMNS, visual_.pre_min);
            std::copy(visual_work_.pre_max, visual_work_.pre_max + Visual::COLUMNS, visual_.pre_max);
            std::copy(visual_work_.out_min, visual_work_.out_min + Visual::COLUMNS, visual_.out_min);
            std::copy(visual_work_.out_max, visual_work_.out_max + Visual::COLUMNS, visual_.out_max);
            std::copy(visual_work_.occupied, visual_work_.occupied + Visual::COLUMNS, visual_.occupied);
            visual_.samples_per_cycle = visual_cycle_samples_;
            visual_.valid = true;
            ++visual_.generation;
        }
        visual_work_ = Visual{};
        visual_cycle_samples_ = 0;
        visual_cycle_started_ = true;
    }
    visual_last_phase_ = phase;
    if (!visual_cycle_started_) return;
    ++visual_cycle_samples_;
    const int bin = int((uint64_t(phase) * Visual::COLUMNS) >> 32);
    if (!visual_work_.occupied[bin]) {
        visual_work_.occupied[bin] = 1;
        visual_work_.pre_min[bin] = visual_work_.pre_max[bin] = pre;
        visual_work_.out_min[bin] = visual_work_.out_max[bin] = out;
    } else {
        visual_work_.pre_min[bin] = std::min(visual_work_.pre_min[bin], pre);
        visual_work_.pre_max[bin] = std::max(visual_work_.pre_max[bin], pre);
        visual_work_.out_min[bin] = std::min(visual_work_.out_min[bin], out);
        visual_work_.out_max[bin] = std::max(visual_work_.out_max[bin], out);
    }
}
void OscUnit::flush_visual_pitch() {
    if (!visual_pitch_pending_) return;
    for (int slot = 0; slot < Visual::HISTORY; ++slot) {
        if (visual_pitch_pending_ & (uint64_t(1) << slot))
            visual_.pitch_cents[slot] = int16_t(std::max(-32767.f, std::min(32767.f,
                std::round(1200.f * std::log2(visual_pitch_ratio_[slot])))));
    }
    visual_pitch_pending_ = 0;
}
void OscUnit::clean_process(q15* buf, int frames) {
    if (bypass) { set_visual_capture(false); return; }
    if (!clean_ready_) clean_control_tick();
    static_assert(CLEAN_TAPS == osc_clean::TAPS, "clean FIR state size");
    const float* const sine_lut = osc_clean::sine_table(); // one init-guard check per block
    for (int i = 0; i < frames; ++i) {
        // Independent full-precision random walk, not integer delta/4096. The
        // walk continues at depth zero, but multiplying by *target* zero removes
        // all detune immediately (no stuck residual after the knob reaches zero).
        if (++clean_drift_count_ >= 16384) {
            clean_drift_count_ = 0;
            clean_drift_target_ = osc_clean::random_bipolar(clean_rng_);
        }
        clean_drift_ += (clean_drift_target_ - clean_drift_) * (1.0f / 4096.0f);
        float frequency = clean_value_[P_PITCH];
        int newest = clean_fir_head_;
        for (int oversample = 0; oversample < 2; ++oversample) {
            if (clean_ramp_) {
                for (int p = 0; p < P_COUNT; ++p) clean_value_[p] += clean_step_[p];
                if (!--clean_ramp_) std::copy(clean_target_, clean_target_ + P_COUNT, clean_value_);
            }
            const float drift = clean_target_[P_DRIFT] == 0 ? 0 : clean_value_[P_DRIFT] * clean_drift_;
            frequency = clean_value_[P_PITCH] + clean_value_[P_PITCH] * drift * .0157f;
            // Add drift in phase-increment space with fractional error feedback:
            // forming float(1+tiny_drift) would reintroduce a low-depth deadband.
            const float base_increment = clean_value_[P_PITCH] * (4294967296.0f / (ENGINE_SR * 4.0f));
            if (clean_target_[P_DRIFT] == 0) clean_drift_phase_error_ = 0;
            else clean_drift_phase_error_ += base_increment * drift * .0157f;
            const int32_t drift_increment = int32_t(clean_drift_phase_error_);
            clean_drift_phase_error_ -= float(drift_increment);
            // Sub-period accumulator avoids cumulative odd-increment truncation.
            const uint32_t increment = uint32_t(int64_t(uint32_t(base_increment)) + drift_increment);
            const uint32_t previous_phase = clean_phase_;
            const uint32_t main_phase = previous_phase * 2u;
            const float p = float(main_phase >> 8) * (1.0f / 16777216.0f);
            const float delta = float(increment) * (2.0f * osc_clean::INV_PHASE);
            const float unwrapped = p + delta;
            clean_phase_ += increment;
            const float now = float((clean_phase_ * 2u) >> 8) * (1.0f / 16777216.0f);
            const float width = clean_value_[P_PW], previous_width = clean_previous_[P_PW];
            float saw = 1 - 2 * now;
            float tri = osc_clean::triangle(now);
            float pulse_phase = now - .25f + .5f * width;
            if (pulse_phase < 0) pulse_phase += 1;
            if (pulse_phase >= 1) pulse_phase -= 1;
            float pulse = pulse_phase < width ? 1.0f : -1.0f;
            float previous_saw = clean_saw_, previous_tri = clean_tri_, previous_pulse = clean_pulse_;
            osc_clean::step_event(p, unwrapped, 2, previous_saw, saw);
            osc_clean::ramp_event(p - .25f, unwrapped - .25f, -8 * delta, previous_tri, tri);
            osc_clean::ramp_event(p - .75f, unwrapped - .75f, 8 * delta, previous_tri, tri);
            osc_clean::step_event(p - .25f + .5f * previous_width,
                unwrapped - .25f + .5f * width, 2, previous_pulse, pulse);
            osc_clean::step_event(p - .25f - .5f * previous_width,
                unwrapped - .25f - .5f * width, -2, previous_pulse, pulse);
            clean_saw_ = saw; clean_tri_ = tri; clean_pulse_ = pulse;
            // Static PWM has exactly zero analytic mean. Symmetric edges keep
            // its fundamental aligned with sine/triangle/falling-saw for any PW.
            previous_pulse = (previous_pulse - (2 * previous_width - 1)) /
                (2 * std::max(previous_width, 1 - previous_width));
            const float anchors[4] = {osc_clean::sine(sine_lut, main_phase), previous_tri, previous_saw, previous_pulse};
            const float shape = clean_previous_[P_SHAPE];
            const int anchor = std::min(2, int(shape));
            const float raw = std::max(-1.0f, std::min(1.0f,
                anchors[anchor] + (anchors[anchor + 1] - anchors[anchor]) * (shape - anchor)));
            const float driven = raw * clean_previous_[P_FOLD];
            const float folded = osc_clean::fold_adaa(driven, clean_previous_fold_);
            const float sub = osc_clean::sine(sine_lut, previous_phase);
            const float aligned_sub = .5f * (sub + clean_previous_sub_);
            const float mix = clean_previous_[P_SUB], gain = clean_previous_[P_GAIN];
            newest = clean_fir_head_;
            clean_fir_[newest] = (folded + (aligned_sub - folded) * mix) * gain;
            if (visual_enabled_) {
                clean_pre_fir_[newest] = .5f * (raw + clean_previous_pre_) * gain;
                clean_phase_delay_[newest] = clean_phase_;
            }
            clean_previous_fold_ = driven; clean_previous_sub_ = sub; clean_previous_pre_ = raw;
            std::copy(clean_value_, clean_value_ + P_COUNT, clean_previous_);
            if (++clean_fir_head_ == CLEAN_TAPS) clean_fir_head_ = 0;
        }
        // One FIR output per two local samples. Nothing changes global ENGINE_SR.
        buf[i] = osc_clean::q15(osc_clean::decimate(clean_fir_, newest));
        if (visual_enabled_) {
            int a = newest - 24, b = newest - 25;
            if (a < 0) a += CLEAN_TAPS;
            if (b < 0) b += CLEAN_TAPS;
            // BLEP 1 + ADAA .5 + FIR 23 = 24.5 internal frames. Wrapped
            // midpoint of actual delayed phases also follows pitch ramps.
            const uint32_t phase = clean_phase_delay_[b] +
                ((clean_phase_delay_[a] - clean_phase_delay_[b]) >> 1);
            collect_visual(phase, osc_clean::q15(osc_clean::decimate(clean_pre_fir_, newest)), buf[i], frequency);
        }
    }
    // Eager block-boundary flush preserves live const Visual& semantics: even
    // a reference retained before process sees a fully coherent payload after
    // return. Under the same audio guard, <=48 exact conversions, no heap;
    // repeated visual() reads do no work. Overwritten slots need no conversion.
    flush_visual_pitch();
}
} // namespace seance
