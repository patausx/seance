// Temporary master-bus gestures; independent from module/global FX and patch data.
#pragma once
#include "unit.h"
#include <cstddef>
#include <new>

namespace seance {
enum class TransitionFxKind : uint8_t { None, Filter, Echo, Roll };
struct TransitionFxState {
    TransitionFxKind kind = TransitionFxKind::None;
    int16_t amount = 0;
    bool ready = false; // Filter usable; others need storage, Roll also a captured slice
    bool echo_tail = false;
    bool releasing = false; // requested gesture ended; an audible exit ramp remains
    uint8_t roll_division = 0;
    uint32_t epoch = 0;
};
static_assert(sizeof(TransitionFxState) <= 32, "bounded scalar capture");

class TransitionFx {
public:
    static constexpr int MIN_BPM = 20, MAX_BPM = 300; // native engine / applied clock limits
    static constexpr uint32_t MAX_FRAMES = ENGINE_SR * 60 / MIN_BPM;
    static constexpr uint32_t RAMP_FRAMES = 256; // 8 ms, audio-clocked
    static constexpr uint32_t SEAM_FRAMES = 64;  // 2 ms fade through head sample
    static constexpr uint32_t TAIL_FRAMES = ENGINE_SR * 8; // release + 8 s + 8 ms maximum
    static constexpr size_t STORAGE_BYTES = size_t(MAX_FRAMES) * 4 * sizeof(q15);
    explicit TransitionFx(bool allocate_storage = true);
    ~TransitionFx();
    TransitionFx(const TransitionFx&) = delete;
    TransitionFx& operator=(const TransitionFx&) = delete;
    bool set(TransitionFxKind kind, int amount, int32_t applied_bpm_q16);
    void release();
    void stop(); // release with finite tail, invalidate UI gesture epoch
    void reset(); // O(1) invalidate every old sample, no big memset
    TransitionFxState state() const;
    void process(q15* stereo, int frames);
    static uint32_t beat_frames(int32_t applied_bpm_q16);
    uint32_t echo_delay_frames() const { return echo_pending_ ? echo_pending_ : echo_length_; }
    uint32_t roll_length_frames() const { return roll_wanted_; }
private:
    struct Ramp {
        int32_t value=0, start=0, target=0;
        uint16_t left=0;
        void to(int32_t v);
        int32_t next();
    };
    q15* storage_ = nullptr;
    TransitionFxState state_;
    Ramp filter_, echo_send_, echo_return_, roll_wet_;
    int64_t filter_low_[2] = {};
    uint32_t history_write_=0, history_valid_=0;
    bool frozen_=false;
    uint32_t roll_beat_=16000, roll_wanted_=16000, roll_length_=0, roll_position_=0;
    uint32_t echo_write_=0, echo_valid_=0, echo_length_=16000, echo_pending_=0;
    uint32_t tail_left_=0;
    bool echo_running_=false, echo_stopping_=false;
    void start_echo(uint32_t length);
    q15 loop_sample(uint32_t position, int channel) const;
};
} // namespace seance

// Header-only: legacy focused test targets link engine.cpp explicitly.
namespace seance {
namespace transition_fx_detail {
inline int clamp_int(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }
inline q15 bounded(int64_t x) { return (q15)(x < -32768 ? -32768 : x > 32767 ? 32767 : x); }
inline int32_t blend(int32_t dry, int32_t wet, int32_t amount) {
    return dry + (int32_t)((int64_t(wet - dry) * amount) / 32767);
}
}
inline TransitionFx::TransitionFx(bool allocate_storage) : storage_(allocate_storage ? new(std::nothrow) q15[STORAGE_BYTES / sizeof(q15)] : nullptr) {}
inline TransitionFx::~TransitionFx() { delete[] storage_; }
inline void TransitionFx::Ramp::to(int32_t v) {
    if (target == v) return;
    start = value; target = v; left = RAMP_FRAMES;
}
inline int32_t TransitionFx::Ramp::next() {
    if (left) {
        --left;
        value = start + (int32_t)((int64_t(target - start) * (RAMP_FRAMES - left)) / RAMP_FRAMES);
    }
    return value;
}
inline uint32_t TransitionFx::beat_frames(int32_t bpm) {
    bpm = transition_fx_detail::clamp_int(bpm, MIN_BPM * 65536, MAX_BPM * 65536);
    return (uint32_t)((uint64_t(ENGINE_SR) * 60 * 65536 + uint32_t(bpm) / 2) / uint32_t(bpm));
}
inline TransitionFxState TransitionFx::state() const {
    TransitionFxState s = state_;
    s.ready = s.kind == TransitionFxKind::Filter || (storage_ && (s.kind != TransitionFxKind::Roll ||
        (frozen_ && roll_length_ == roll_wanted_)));
    s.echo_tail = echo_running_ && s.kind != TransitionFxKind::Echo;
    s.releasing = (s.kind != TransitionFxKind::Filter && filter_.value != 0) ||
                  (s.kind != TransitionFxKind::Roll && roll_wet_.value != 0) ||
                  (s.kind != TransitionFxKind::Echo && echo_send_.value != 0);
    return s;
}
inline void TransitionFx::start_echo(uint32_t length) {
    echo_write_ = echo_valid_ = 0; echo_length_ = length; echo_pending_ = 0;
    echo_running_ = true; echo_stopping_ = false; tail_left_ = 0;
    echo_return_ = {}; echo_return_.value = echo_return_.target = 32767;
    echo_send_ = {}; echo_send_.to(state_.amount);
}
inline bool TransitionFx::set(TransitionFxKind kind, int amount, int32_t bpm) {
    if ((uint8_t)kind > (uint8_t)TransitionFxKind::Roll) return false;
    if (kind == TransitionFxKind::None) { release(); return true; }
    if (!storage_ && kind != TransitionFxKind::Filter) return false;
    amount = kind == TransitionFxKind::Filter ? transition_fx_detail::clamp_int(amount, -32767, 32767) :
             kind == TransitionFxKind::Roll ? transition_fx_detail::clamp_int(amount, 0, 3) : transition_fx_detail::clamp_int(amount, 0, 32767);
    const bool entering = state_.kind != kind;
    if (entering) release();
    state_.kind = kind; state_.amount = (int16_t)amount;
    if (kind == TransitionFxKind::Filter) filter_.to(amount);
    if (kind == TransitionFxKind::Echo) {
        if (entering) {
            const uint32_t wanted = beat_frames(bpm);
            if (!echo_running_) start_echo(wanted);
            else if (wanted != echo_length_ || echo_pending_) {
                // Old tail fades at its original delay; new line starts only
                // once inaudible. No pointer jump through audible old history.
                echo_pending_ = wanted; echo_stopping_ = true;
                echo_send_.to(0); echo_return_.to(0);
            } else {
                echo_stopping_ = false; tail_left_ = 0; echo_return_.to(32767);
            }
        }
        if (!echo_pending_) echo_send_.to(amount);
    }
    if (kind == TransitionFxKind::Roll) {
        if (entering) roll_beat_ = beat_frames(bpm);
        state_.roll_division = (uint8_t)amount;
        roll_wanted_ = roll_beat_ >> amount;
        if (frozen_ && roll_length_ != roll_wanted_) roll_wet_.to(0);
        else if (frozen_) roll_wet_.to(32767);
    }
    return true;
}
inline void TransitionFx::release() {
    if (state_.kind == TransitionFxKind::Echo && echo_running_) {
        tail_left_ = TAIL_FRAMES;
        // An interrupted queued re-entry must never resurrect a released send.
        echo_pending_ = 0;
    }
    state_.kind = TransitionFxKind::None; state_.amount = 0;
    filter_.to(0); echo_send_.to(0); roll_wet_.to(0);
}
inline void TransitionFx::stop() { release(); ++state_.epoch; }
inline void TransitionFx::reset() {
    const uint32_t epoch = state_.epoch + 1;
    state_ = {}; state_.epoch = epoch;
    filter_ = {}; echo_send_ = {}; echo_return_ = {}; roll_wet_ = {};
    filter_low_[0] = filter_low_[1] = 0;
    history_write_ = history_valid_ = 0; frozen_ = false;
    roll_beat_ = roll_wanted_ = 16000; roll_length_ = roll_position_ = 0;
    echo_write_ = echo_valid_ = echo_pending_ = tail_left_ = 0; echo_length_ = 16000;
    echo_running_ = echo_stopping_ = false;
}
inline q15 TransitionFx::loop_sample(uint32_t position, int channel) const {
    const uint32_t start = (history_write_ + MAX_FRAMES - roll_length_) % MAX_FRAMES;
    const q15* history = storage_ + MAX_FRAMES * 2;
    int32_t sample = history[((start + position) % MAX_FRAMES) * 2 + channel];
    if (position >= roll_length_ - SEAM_FRAMES) {
        // Last 64 samples crossfade to the captured head sample. Period stays
        // exact (no overlap shortening); the boundary itself has equal values.
        const int32_t head = history[start * 2 + channel];
        const uint32_t fade = position - (roll_length_ - SEAM_FRAMES) + 1;
        sample += (int32_t)((int64_t(head - sample) * fade) / SEAM_FRAMES);
    }
    return (q15)sample;
}
inline void TransitionFx::process(q15* out, int frames) {
    if (!out || frames <= 0) return;
    q15* history = storage_ ? storage_ + MAX_FRAMES * 2 : nullptr;
    // Common idle/center path records history only; output is not even written.
    // No coefficient math or ramp work when every audible transient is gone.
    if (!frozen_ && state_.kind != TransitionFxKind::Roll &&
        !filter_.value && !filter_.target && !echo_running_ && !roll_wet_.value) {
        if (storage_) for (int frame = 0; frame < frames; ++frame) {
            history[history_write_ * 2] = out[frame * 2];
            history[history_write_ * 2 + 1] = out[frame * 2 + 1];
            if (++history_write_ == MAX_FRAMES) history_write_ = 0;
            if (history_valid_ < MAX_FRAMES) ++history_valid_;
        }
        filter_low_[0] = int64_t(out[(frames - 1) * 2]) * 65536;
        filter_low_[1] = int64_t(out[(frames - 1) * 2 + 1]) * 65536;
        return;
    }
    for (int frame = 0; frame < frames; ++frame) {
        if (frozen_ && roll_wet_.value == 0 && roll_wet_.target == 0) {
            if (state_.kind == TransitionFxKind::Roll && history_valid_ >= roll_wanted_) {
                roll_length_ = roll_wanted_; roll_position_ = 0; roll_wet_.to(32767);
            } else {
                // The frozen interval is no longer recent after release. Resume
                // recording with fresh validity, never mixing old/gapped history.
                frozen_ = false; history_write_ = history_valid_ = 0;
            }
        }
        if (!frozen_ && state_.kind == TransitionFxKind::Roll && history_valid_ >= roll_wanted_) {
            frozen_ = true; roll_length_ = roll_wanted_; roll_position_ = 0;
            roll_wet_.to(32767);
        }
        if (echo_running_ && echo_stopping_ && !echo_return_.value && !echo_send_.value) {
            const uint32_t pending = echo_pending_;
            echo_running_ = echo_stopping_ = false; echo_valid_ = echo_pending_ = 0;
            if (pending && state_.kind == TransitionFxKind::Echo) start_echo(pending);
        }
        if (echo_running_ && state_.kind != TransitionFxKind::Echo && tail_left_) {
            if (!--tail_left_) { echo_stopping_ = true; echo_return_.to(0); }
        }
        const int32_t filter = filter_.next(), wet = roll_wet_.next();
        const int32_t send = echo_send_.next(), echo_gain = echo_return_.next();
        const int32_t strength = filter < 0 ? -filter : filter;
        // Stable one-pole coefficient, bounded 128..32768. Negative sweep
        // closes LP; positive sweep raises HP. No runtime libm/table scans.
        int32_t coefficient = 0;
        if (strength) {
            const int32_t curve = (int32_t)((int64_t(strength) * strength) / 32767);
            coefficient = 128 + (int32_t)((int64_t(curve) * 28000) / 32767);
            if (filter < 0) {
                const int32_t open = 32767 - strength;
                const int32_t square = (int32_t)((int64_t(open) * open) / 32767);
                coefficient = 128 + (int32_t)((int64_t(square) * square * 32640) / (int64_t(32767) * 32767));
            }
        }
        const uint32_t echo_read = (echo_write_ + MAX_FRAMES - echo_length_) % MAX_FRAMES;
        for (int channel = 0; channel < 2; ++channel) {
            const int32_t dry = out[frame * 2 + channel];
            if (storage_ && !frozen_) history[history_write_ * 2 + channel] = (q15)dry;
            int32_t sample = wet && frozen_ ? transition_fx_detail::blend(dry, loop_sample(roll_position_, channel), wet) : dry;
            if (strength) {
                const int64_t target = int64_t(sample) * 65536;
                filter_low_[channel] += ((target - filter_low_[channel]) * coefficient) / 32768;
                const int32_t low = (int32_t)(filter_low_[channel] / 65536);
                sample = transition_fx_detail::blend(sample, filter < 0 ? low : sample - low, strength);
            } else filter_low_[channel] = int64_t(sample) * 65536;
            if (echo_running_) {
                const int32_t delayed = echo_valid_ >= echo_length_ ? storage_[echo_read * 2 + channel] : 0;
                storage_[echo_write_ * 2 + channel] = transition_fx_detail::bounded((int64_t(sample) * send) / 32767 + delayed / 2);
                sample += (int32_t)((int64_t(delayed) * echo_gain) / 32767);
            }
            out[frame * 2 + channel] = transition_fx_detail::bounded(sample);
        }
        if (storage_ && !frozen_) {
            if (++history_write_ == MAX_FRAMES) history_write_ = 0;
            if (history_valid_ < MAX_FRAMES) ++history_valid_;
        } else if (frozen_ && ++roll_position_ == roll_length_) roll_position_ = 0;
        if (echo_running_) {
            if (++echo_write_ == MAX_FRAMES) echo_write_ = 0;
            if (echo_valid_ < MAX_FRAMES) ++echo_valid_;
        }
    }
}
} // namespace seance
