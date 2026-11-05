#pragma once
// FRACT's approved physical spring-grab presentation. Displacement is NOT saved
// DAMAGE, an effective/live parameter, PCM capture, an audio gate or a trigger.
// Inputs carry no clock; only capture(now, ...) advances the return trajectory.
#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace fract_grab {
constexpr int kHome = 204;
constexpr int kLeft = 24;
constexpr int kRight = 212;
constexpr int kAxisY = 114;
constexpr uint64_t kReturnMs = 400;
constexpr int kQ = 256;

struct Key {
    uintptr_t engine = 0, unit = 0;
    int chain = -1, slot = -1, view = -1, bank = -1, step = -1;
    bool locked = false;
    uintptr_t audio = 0;
    int lock_chain = -1;
    bool operator==(const Key& b) const {
        return engine == b.engine && unit == b.unit && chain == b.chain &&
            slot == b.slot && view == b.view && bank == b.bank &&
            step == b.step && locked == b.locked && audio == b.audio &&
            lock_chain == b.lock_chain;
    }
};
struct Pose {
    int16_t x = kHome;
    bool physical = false, returning = false, enabled = false;
};

class Model {
    Key key_{};
    int32_t offset_ = 0, pressed_offset_ = 0, released_offset_ = 0;
    int32_t first_rebound_ = 0, second_rebound_ = 0;
    int origin_x_ = 0;
    uint64_t now_ = 0, released_at_ = 0;
    uint32_t captures_ = 0;
    bool bound_ = false, enabled_ = false, physical_ = false;
    bool returning_ = false, pending_release_ = false;

    static int32_t bounded(int64_t q) {
        return int32_t(std::clamp<int64_t>(q, (kLeft-kHome)*kQ, (kRight-kHome)*kQ));
    }
    static int32_t interpolate(int32_t a, int32_t b, uint64_t t, uint64_t duration) {
        // Absolute-time cubic smoothstep: exact at the knots, zero velocity at
        // each rebound, independent of how many captures partition the interval.
        const int64_t u = int64_t(std::min(t, duration)*65536/duration);
        const int64_t eased = (u*u*(3*65536-2*u)+(int64_t(1)<<31)) >> 32;
        return int32_t(a+(int64_t(b)-a)*eased/65536);
    }
    void bind(const Key& key, bool enabled) {
        if (!bound_ || !(key_ == key) || enabled_ != enabled) {
            cancel(); key_ = key; bound_ = true;
        }
        enabled_ = enabled;
        if (!enabled) cancel();
    }
public:
    void cancel() {
        offset_ = pressed_offset_ = released_offset_ = 0;
        first_rebound_ = second_rebound_ = 0;
        origin_x_ = 0; released_at_ = 0;
        physical_ = returning_ = pending_release_ = false;
    }
    void invalidate() {
        cancel(); bound_ = enabled_ = false; key_ = {};
    }
    void down(int tx, const Key& key, bool enabled) {
        bind(key, enabled);
        if (!enabled_) return;
        // A new press catches the last presented pose, even during return. No
        // jump-to-home, animation completion wait or clock read at this boundary.
        returning_ = pending_release_ = false;
        physical_ = true; origin_x_ = tx; pressed_offset_ = offset_;
    }
    void move(int tx) {
        if (!physical_ || !enabled_) return;
        const int64_t dx = int64_t(tx)-origin_x_;
        // Match the existing 4px stationary deadzone, but keep this independent
        // visual stroke origin. The editor remains the only DAMAGE writer.
        const int64_t displacement = dx >= -4 && dx <= 4 ? 0 : dx*kQ;
        offset_ = bounded(int64_t(pressed_offset_)+displacement);
    }
    void lift() {
        if (!physical_) return;
        physical_ = false;
        released_offset_ = offset_;
        // Two diminishing opposite extrema, <=8px then <=2px. No coil, live
        // telemetry, parameter reset or parameter override is represented here.
        first_rebound_ = std::clamp<int32_t>(-released_offset_/18, -8*kQ, 8*kQ);
        second_rebound_ = std::clamp<int32_t>(released_offset_/72, -2*kQ, 2*kQ);
        returning_ = pending_release_ = offset_ != 0;
    }
    void capture(uint64_t now, const Key& key, bool enabled) {
        ++captures_;
        if (bound_ && now < now_) cancel(); // a discontinuous clock is not a spring
        now_ = now;
        bind(key, enabled);
        if (!returning_) return;
        if (pending_release_) {
            // Lift already released audio. The first subsequent display capture
            // anchors visual time, at most one display interval after that lift.
            released_at_ = now; pending_release_ = false;
        }
        const uint64_t elapsed = now-released_at_;
        if (elapsed >= kReturnMs) {
            offset_ = 0; returning_ = false;
        } else if (elapsed <= 220) {
            offset_ = interpolate(released_offset_, first_rebound_, elapsed, 220);
        } else if (elapsed <= 325) {
            offset_ = interpolate(first_rebound_, second_rebound_, elapsed-220, 105);
        } else {
            offset_ = interpolate(second_rebound_, 0, elapsed-325, 75);
        }
        offset_ = bounded(offset_);
    }
    Pose pose() const {
        const int rounded = offset_ >= 0 ? (offset_+kQ/2)/kQ : -((-offset_+kQ/2)/kQ);
        return {int16_t(kHome+rounded), physical_, returning_, enabled_};
    }
    int32_t offset_q8() const { return offset_; }
    uint32_t captures() const { return captures_; }
};
static_assert(sizeof(Model) <= 128 && std::is_trivially_copyable<Model>::value,
              "bounded presentation; no engine/PCM/storage ownership");
static_assert(sizeof(Pose) <= 8 && std::is_trivially_copyable<Pose>::value,
              "one small frozen pose; draws cannot tick the model");
} // namespace fract_grab
