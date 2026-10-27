#pragma once
// Picker-only pixel motion; sampled by input/capture, never by draw.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace seance::platform::ui3ds::picker_scroll {
struct State {
    float position = 0, velocity = 0; // pixels, pixels/ms
    uint64_t tick = 0, last_move = 0;

    bool moving() const { return std::fabs(velocity) >= .025f; }
    void stop(uint64_t now) { velocity = 0; tick = now; }
    void set(float value, float limit, uint64_t now) {
        position = std::clamp(value, 0.f, limit);
        stop(now); last_move = now;
    }
    void drag(float value, float limit, uint64_t now) {
        const float next = std::clamp(value, 0.f, limit), delta = next - position;
        const auto elapsed = now >= tick ? now - tick : 0;
        if (elapsed) {
            const float dt = float(std::min<uint64_t>(elapsed, 100));
            if (elapsed > 90) velocity = 0;
            const float speed = std::clamp(delta / dt, -1.8f, 1.8f);
            velocity += (speed - velocity) * (dt / (24.f + dt));
        }
        if (delta != 0) last_move = now;
        position = next; tick = now;
        if ((position <= 0 && velocity < 0) || (position >= limit && velocity > 0)) velocity = 0;
    }
    void release(uint64_t now) {
        if (now < last_move || now - last_move > 90 || !moving()) velocity = 0;
        tick = now;
    }
    void sample(float limit, uint64_t now) {
        if (now < tick || now - tick > 250) { stop(now); return; }
        const float elapsed = float(now - tick); tick = now;
        if (!moving()) { velocity = 0; return; }
        constexpr float friction = .0045f;
        const float sign = velocity < 0 ? -1.f : 1.f, speed = std::fabs(velocity);
        const float dt = std::min(elapsed, speed / friction);
        position = std::clamp(position + sign * (speed * dt - .5f * friction * dt * dt), 0.f, limit);
        velocity = sign * std::max(0.f, speed - friction * dt);
        if ((position <= 0 && velocity < 0) || (position >= limit && velocity > 0)) velocity = 0;
    }
};
} // namespace seance::platform::ui3ds::picker_scroll
