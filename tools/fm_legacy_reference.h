// Frozen verbatim pre-expansion FM class + implementation (2026-09-10).
// Only class name/namespace changed; independent sample-exact regression oracle.
#pragma once
#include "engine/units.h"
#include <algorithm>
namespace fm_legacy_reference {
using namespace seance;
using trackr::fx::sat_q15;
static uint32_t pitch_phase_increment(q15 pitch, bool quantize) {
    int note100 = 2400 + (int)(((int64_t)(uint16_t)pitch * 7200) >> 15);
    if (quantize) note100 = ((note100 + 50) / 100) * 100;
    uint32_t semis = (uint32_t)(note100 / 100);
    static const uint32_t SEMI_Q16[12] = {
        65536, 69433, 73562, 77936, 82570, 87480,
        92682, 98193, 104032, 110218, 116772, 123715
    };
    const uint32_t octave = semis / 12u;
    const uint32_t note = semis % 12u;
    uint64_t freq_q16 = ((uint64_t)1071616 * SEMI_Q16[note]) >> 16;
    freq_q16 <<= octave;
    return (uint32_t)((freq_q16 << 16) / ENGINE_SR);
}

static int32_t fast_sine(uint32_t phase) {
    const uint32_t p = phase >> 16;
    const int32_t tri = p < 0x8000u ? (int32_t)p * 2 - 32768
                                     : 32767 - ((int32_t)p - 32768) * 2;
    const int32_t magnitude = tri < 0 ? -tri : tri;
    return (int32_t)(((int64_t)tri * (65536 - magnitude)) >> 15);
}

class LegacyFmUnit : public Unit {
public:
    LegacyFmUnit();
    void process(q15* buf, int frames) override;
    void control_tick() override;
    void trigger() override;
    SequencerProperties sequencer_properties() const override {
        return drone
            ? SequencerProperties{true, false, SequenceLifecycle::Flow, P_PITCH, P_GAIN, P_GAIN, -1}
            : SequencerProperties{true, false, SequenceLifecycle::Strike, P_PITCH, P_GAIN, -1, P_DECAY};
    }
    void reset() override;

    enum {
        P_PITCH = 0, P_RATIO, P_INDEX, P_FEEDBACK, P_DECAY, P_GAIN,
        P_INDEX_FALL, P_ATTACK, P_RATIO_FINE, P_FEEDBACK_FALL
    };
    uint16_t visual_phase() const { return (uint16_t)(carrier_phase_ >> 16); }
    uint16_t visual_envelope() const { return (uint16_t)env_; }
    uint16_t visual_index_envelope() const { return (uint16_t)index_env_; }
    uint16_t visual_feedback_envelope() const { return (uint16_t)feedback_env_; }

    // Patch-persisted performance policies. False/false exactly preserves the
    // original free-running, raw two-operator voice.
    bool phase_reset = false;
    bool alias_safe = false;
    bool drone = false; // UNIT config bit 0x04; DECAY/ATK/IFALL/FFALL dormant

    // Control-rate only. Strike retains the original Q12 fine truncation;
    // drone adds the exact signed RFINE/(4*32767) ratio before final rounding.
    static uint32_t modulation_increment(uint32_t carrier, q15 ratio, q15 fine,
                                         bool drone_mode, bool safe);
    // Full-scale mode entry takes 512 samples / 16ms at 32kHz. Mode exit uses
    // the saved strike decay, capped to this per-sample fall until a real strike.
    static constexpr int DRONE_FADE_STEP = 64;

private:
    uint32_t carrier_phase_ = 0;
    uint32_t mod_phase_ = 0;
    uint32_t carrier_inc_ = 0;
    uint32_t mod_inc_ = 0;
    q15 last_mod_ = 0;
    q15 env_ = 0;
    q15 index_env_ = 32767;
    q15 feedback_env_ = 32767;
    uint16_t decay_step_ = 8;
    uint16_t attack_step_ = 32767;
    uint16_t index_fall_step_ = 0;
    uint16_t feedback_fall_step_ = 0;
    bool attacking_ = false;
    bool rendered_drone_ = false;
    bool drone_release_ = false;
};

LegacyFmUnit::LegacyFmUnit() {
    kind = UnitKind::Fm;
    add_param("PTCH", 16384);
    add_param("RATIO", 8192);
    add_param("INDEX", 13926);
    add_param("FDBK", 13762);
    add_param("DECAY", 12000);
    add_param("GAIN", 22000);
    // Appended controls are neutral at zero so dormant SNC7 ParamBank entries
    // preserve every patch authored by the original six-parameter FM voice.
    add_param("IFALL", 0);
    add_param("ATK", 0);
    add_param("RFINE", 0, true);
    add_param("FFALL", 0);
}

void LegacyFmUnit::reset() {
    carrier_phase_ = mod_phase_ = 0;
    last_mod_ = 0;
    env_ = 0;
    index_env_ = feedback_env_ = 32767;
    attacking_ = false;
    if (rendered_drone_) carrier_inc_ = 0; // refresh policy after reset/mode change
    rendered_drone_ = drone_release_ = false;
}

void LegacyFmUnit::trigger() {
    if (drone) return; // no phase, feedback-memory, envelope or attack restart
    if (rendered_drone_) {
        rendered_drone_ = false;
        control_tick();
    }
    drone_release_ = false;
    if (phase_reset) {
        carrier_phase_ = mod_phase_ = 0;
        last_mod_ = 0;
    }
    index_env_ = feedback_env_ = 32767;
    attacking_ = params[P_ATTACK].effective > 0;
    env_ = attacking_ ? 1 : 32767;
}

uint32_t LegacyFmUnit::modulation_increment(uint32_t carrier, q15 ratio, q15 fine,
                                       bool drone_mode, bool safe) {
    int32_t ratio_q12 = 2048 + (int32_t)((uint32_t)(uint16_t)ratio * 30720u / 32767u);
    uint64_t mod;
    if (drone_mode) {
        // Keep the existing coarse RATIO grid but do not throw away the low
        // RFINE bits. Positive rational numerator, one final truncation. Largest
        // product is <2^63 even for the full uint32 carrier and q15 input range.
        int64_t numerator = (int64_t)ratio_q12 * 32767 + (int32_t)fine * 1024;
        const int64_t lo = (int64_t)1024 * 32767, hi = (int64_t)33792 * 32767;
        if (numerator < lo) numerator = lo;
        if (numerator > hi) numerator = hi;
        mod = (uint64_t)carrier * (uint64_t)numerator / (4096ull * 32767ull);
    } else {
        // Exact legacy path for all ten controls, including signed division.
        ratio_q12 += (int32_t)fine * 1024 / 32767;
        if (ratio_q12 < 1024) ratio_q12 = 1024;
        if (ratio_q12 > 33792) ratio_q12 = 33792;
        mod = ((uint64_t)carrier * (uint32_t)ratio_q12) >> 12;
    }
    constexpr uint32_t SAFE_MOD_INCREMENT = 1610612736u; // 12 kHz at 32 kHz
    const uint64_t ceiling = safe ? SAFE_MOD_INCREMENT : 0xffffffffu;
    return (uint32_t)(mod > ceiling ? ceiling : mod);
}

void LegacyFmUnit::control_tick() {
    // FM is a note-lane source: use the same rounded MIDI 24..96 contract that
    // the sequencer displays, including the exact upper endpoint.
    carrier_inc_ = pitch_phase_increment(params[P_PITCH].effective, true);
    // Generic Param slew intentionally truncates its final three q15 steps.
    // In drone only, let the fine-ratio control actually reach each saved LSB;
    // otherwise precision edits settle to an audible-but-wrong nearby ratio.
    auto& fine = params[P_RATIO_FINE];
    if (drone && fine.slew_ready) {
        const int remaining = (int)fine.target - fine.effective;
        if (remaining >= -3 && remaining <= 3) fine.effective = fine.target;
    }
    mod_inc_ = modulation_increment(carrier_inc_, params[P_RATIO].effective,
                                   params[P_RATIO_FINE].effective, drone, alias_safe);

    const uint32_t decay = (uint16_t)params[P_DECAY].effective;
    decay_step_ = (uint16_t)(16u + ((32767u - decay) * 496u) / 32767u);
    const uint32_t attack_samples = 1u + (uint32_t)(uint16_t)params[P_ATTACK].effective * 639u / 32767u;
    attack_step_ = (uint16_t)((32766u + attack_samples - 1u) / attack_samples);
    const uint32_t index_fall = (uint16_t)params[P_INDEX_FALL].effective;
    const uint32_t feedback_fall = (uint16_t)params[P_FEEDBACK_FALL].effective;
    index_fall_step_ = index_fall ? (uint16_t)(8u + index_fall * 248u / 32767u) : 0;
    feedback_fall_step_ = feedback_fall ? (uint16_t)(8u + feedback_fall * 248u / 32767u) : 0;
}

void LegacyFmUnit::process(q15* buf, int frames) {
    const q15 base_index = params[P_INDEX].effective;
    const q15 base_feedback = params[P_FEEDBACK].effective;
    const q15 gain = params[P_GAIN].effective;
    if (frames <= 0) return;
    const bool enable_index_fall = params[P_INDEX_FALL].effective != 0;
    const bool enable_feedback_fall = params[P_FEEDBACK_FALL].effective != 0;
    if (drone != rendered_drone_) {
        // Neither direction resets either phase or feedback memory. A mode
        // switch is not a trigger; resume decay from the exact current level.
        attacking_ = false;
        drone_release_ = !drone;
        if (drone) {
            if (!enable_index_fall) index_env_ = 32767;
            if (!enable_feedback_fall) feedback_env_ = 32767;
        }
        rendered_drone_ = drone;
        control_tick();
    } else if (carrier_inc_ == 0) control_tick();
    for (int i = 0; i < frames; ++i) {
        const q15 index = (drone ? index_env_ < 32767 : enable_index_fall)
            ? (q15)(((int32_t)base_index * index_env_) >> 15) : base_index;
        const q15 feedback = (drone ? feedback_env_ < 32767 : enable_feedback_fall)
            ? (q15)(((int32_t)base_feedback * feedback_env_) >> 15) : base_feedback;
        mod_phase_ += mod_inc_;
        const int32_t feedback_phase = (int32_t)((int64_t)last_mod_ * feedback * 2);
        last_mod_ = sat_q15(fast_sine(mod_phase_ + (uint32_t)feedback_phase));
        const int32_t modulation = (int32_t)(((int64_t)last_mod_ * index * 8) >> 15);
        carrier_phase_ += carrier_inc_;
        const uint32_t carrier_phase_mod = (uint32_t)((int64_t)modulation * 32768);
        const int32_t carrier = fast_sine(carrier_phase_ + carrier_phase_mod);
        buf[i] = sat_q15((int32_t)(((int64_t)carrier * env_ * gain) >> 30));

        if (drone) {
            // Bounded linear entry/recovery, then genuinely sustained operators.
            // Saved attack/decays are dormant, never rewritten to fake a hold.
            env_ = (q15)std::min(32767, (int)env_ + DRONE_FADE_STEP);
            index_env_ = (q15)std::min(32767, (int)index_env_ + DRONE_FADE_STEP);
            feedback_env_ = (q15)std::min(32767, (int)feedback_env_ + DRONE_FADE_STEP);
            continue;
        }
        if (attacking_) {
            const int32_t next = (int32_t)env_ + attack_step_;
            if (next >= 32767) { env_ = 32767; attacking_ = false; }
            else env_ = (q15)next;
        } else if (env_ > 0) {
            int32_t fall = ((int32_t)env_ * decay_step_) >> 15;
            if (fall < 1) fall = 1;
            if (drone_release_ && fall > DRONE_FADE_STEP) fall = DRONE_FADE_STEP;
            env_ = (q15)(env_ > fall ? env_ - fall : 0);
        }
        if (index_fall_step_ && index_env_ > 0) {
            int32_t fall = ((int32_t)index_env_ * index_fall_step_) >> 15;
            if (fall < 1) fall = 1;
            index_env_ = (q15)(index_env_ > fall ? index_env_ - fall : 0);
        }
        if (feedback_fall_step_ && feedback_env_ > 0) {
            int32_t fall = ((int32_t)feedback_env_ * feedback_fall_step_) >> 15;
            if (fall < 1) fall = 1;
            feedback_env_ = (q15)(feedback_env_ > fall ? feedback_env_ - fall : 0);
        }
    }
}

}
