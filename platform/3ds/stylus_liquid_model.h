#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace seance::platform::ui3ds::stylus_liquid {

constexpr int kSize = 64;
constexpr int kBytes = kSize * kSize * 4;
constexpr uint64_t kIntroMs = 220;
constexpr uint64_t kReleaseMs = 350;
// A stalled capture stream is not a trajectory; resume starts a fresh contact.
constexpr uint64_t kGapMs = 250;
constexpr float kJumpPixels = 48.f;

struct Point { float x = 0.f, y = 0.f; };
struct Kernel {
    float x = 0.f, y = 0.f; // local mask coordinates, primary always (32,32)
    float sigma = 1.f, weight = 0.f;
};
struct State {
    bool alive = false, held = false;
    int x = 0, y = 0;
    uint64_t last_ms = 0, started_ms = 0, released_ms = 0;
    float release_arrive = 0.f;
    Point followers[3]{};
};
struct Frame {
    bool active = false;
    int x = 0, y = 0; // screen origin; intentionally NOT display-clamped
    float opacity = 0.f, scale = 0.f;
    Kernel kernels[4]{};
};
static_assert(sizeof(State) + sizeof(Frame) <= 2048, "small passive stylus state");

namespace detail {
inline float smooth(float a, float b, float value) {
    float t = (value - a) / (b - a);
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    return t * t * (3.f - 2.f * t);
}
inline void constrain(float& x, float& y, float distance) {
    const float squared = x*x + y*y;
    if (squared > distance*distance) {
        const float ratio = distance / std::sqrt(squared);
        x *= ratio; y *= ratio;
    }
}
inline float phase(uint64_t elapsed, uint64_t period) {
    return 6.28318530718f * float(elapsed % period) / float(period);
}
inline void begin(State& s, int x, int y, uint64_t now) {
    s = {};
    s.alive = s.held = true;
    s.x = x; s.y = y;
    s.started_ms = s.last_ms = now;
    for (auto& p : s.followers) p = {float(x), float(y)};
}
} // namespace detail

// Observation only: no input ownership, engine dependency, heap, or implicit clock.
// Coordinates are validated only while held: lift has no meaningful HID position.
// Invalid held input clears both motion and release, never leaving a stale echo.
inline void capture(State& s, Frame& frame, bool held, int touch_x, int touch_y,
                    uint64_t now_ms) {
    frame = {};
    if (held && (touch_x < 0 || touch_x >= 320 || touch_y < 0 || touch_y >= 240)) {
        s = {};
        return;
    }
    if (s.alive && (now_ms < s.last_ms || now_ms - s.last_ms > kGapMs))
        s = {};
    if (!s.alive) {
        if (!held) return;
        detail::begin(s, touch_x, touch_y, now_ms);
    } else if (held) {
        const float dx = float(touch_x - s.x), dy = float(touch_y - s.y);
        if (!s.held || dx*dx + dy*dy > kJumpPixels*kJumpPixels)
            detail::begin(s, touch_x, touch_y, now_ms);
    }

    const float dt = float(now_ms - s.last_ms);
    const uint64_t elapsed = now_ms - s.started_ms;
    if (held) {
        s.x = touch_x; s.y = touch_y;
    } else if (s.held) {
        s.released_ms = now_ms;
        // Do not finish the intro after a short tap has already lifted.
        s.release_arrive = detail::smooth(0.f, float(kIntroMs), float(elapsed));
    }
    s.held = held;
    s.last_ms = now_ms;
    if (!held && now_ms - s.released_ms >= kReleaseMs) {
        s = {};
        return;
    }
    constexpr float tau[3] = {55.f, 105.f, 160.f};
    for (int i = 0; i < 3; ++i) {
        const float follow = 1.f - std::exp(-dt / tau[i]);
        s.followers[i].x += (float(s.x) - s.followers[i].x) * follow;
        s.followers[i].y += (float(s.y) - s.followers[i].y) * follow;
    }

    const float arrive = held ? detail::smooth(0.f, float(kIntroMs), float(elapsed))
                             : s.release_arrive;
    const float release = held ? 0.f : detail::smooth(0.f, float(kReleaseMs),
                                                   float(now_ms - s.released_ms));
    frame.active = true;
    frame.x = s.x - kSize/2; frame.y = s.y - kSize/2;
    frame.opacity = arrive * (1.f - release);
    frame.scale = (.25f + .75f * arrive) * (1.f - .85f * release);
    const float breath = 1.f + .035f * std::sin(detail::phase(elapsed, 1700));
    frame.kernels[0] = {32.f, 32.f, 4.3f * breath * frame.scale, 1.f};
    constexpr float orbit[3] = {3.4f, 4.1f, 3.3f};
    constexpr float sigma[3] = {3.6f, 3.1f, 2.6f};
    constexpr float weight[3] = {.86f, .76f, .62f};
    // Millisecond periods keep long-held contacts precise without large angles.
    constexpr uint64_t period[3] = {2992, 3396, 3927};
    float previous_x = 0.f, previous_y = 0.f;
    for (int i = 0; i < 3; ++i) {
        float dx = s.followers[i].x - float(s.x);
        float dy = s.followers[i].y - float(s.y);
        detail::constrain(dx, dy, 23.f);
        const float angle = detail::phase(elapsed, period[i]) + float(i)*2.1f;
        float cx = dx + orbit[i]*std::cos(angle);
        float cy = dy + orbit[i]*std::sin(angle);
        dx = cx - previous_x; dy = cy - previous_y;
        detail::constrain(dx, dy, 5.8f);
        cx = previous_x + dx; cy = previous_y + dy;
        frame.kernels[i+1] = {32.f + cx*frame.scale, 32.f + cy*frame.scale,
                             sigma[i]*frame.scale, weight[i]};
        previous_x = cx; previous_y = cy;
    }
}

namespace detail {
// Compact separable approximation to exp(-d*d/(2*sigma*sigma)):
// max(0, 1-d*d/(16*sigma*sigma))^8. Three squarings, support 4 sigma;
// slightly tighter shoulders than a Gaussian, same smooth connected field style.
inline uint16_t axis(float distance, float inverse, float weight) {
    float q = 1.f - distance*distance*inverse;
    if (q <= 0.f) return 0;
    q *= q; q *= q; q *= q;
    return uint16_t(q * weight * 4096.f + .5f);
}
inline unsigned morton(int x, int y) {
    const unsigned xx = unsigned(x), yy = unsigned(y);
    const unsigned interleaved = (xx & 1u) | ((yy & 1u) << 1) |
        ((xx & 2u) << 1) | ((yy & 2u) << 2) |
        ((xx & 4u) << 2) | ((yy & 4u) << 3);
    return ((yy >> 3) * (kSize/8) + (xx >> 3)) * 64u + interleaved;
}
} // namespace detail

// Pure frozen raster. Caller supplies kBytes; RGBA8 bytes ALL equal coverage:
// premultiplied white, independent of byte channel ordering. Logical y=0 is the
// top of the mask; GPU adapter owns the texture-v convention and viewport clip.
// 2x2 area sampling, Q12 axes, a 129-byte coverage LUT. Scratch: 2177 bytes;
// no per-pixel exp/sin/sqrt, no allocations, no framebuffer/theme access.
inline void paint(uint8_t* data, const Frame& frame) {
    if (!data) return;
    std::memset(data, 0, kBytes);
    if (!frame.active || frame.opacity <= 0.f) return;
    uint16_t axes[4][2][kSize*2];
    for (int k = 0; k < 4; ++k) {
        const Kernel& kernel = frame.kernels[k];
        const float inverse = 1.f / (16.f * kernel.sigma * kernel.sigma);
        for (int p = 0; p < kSize*2; ++p) {
            const float sample = (float(p) + .5f) * .5f;
            axes[k][0][p] = detail::axis(sample - kernel.x, inverse, kernel.weight);
            axes[k][1][p] = detail::axis(sample - kernel.y, inverse, 1.f);
        }
    }
    uint8_t coverage[129];
    for (int i = 0; i <= 128; ++i) {
        const float field = float(i) / 64.f;
        const float a = .14f * detail::smooth(.025f, .42f, field) +
                        .55f * detail::smooth(.35f, .76f, field) +
                        .16f * detail::smooth(.85f, 1.8f, field);
        unsigned value = unsigned(255.f * a * frame.opacity + .5f);
        coverage[i] = uint8_t(value > 217u ? 217u : value);
    }
    for (int y = 0; y < kSize; ++y) for (int x = 0; x < kSize; ++x) {
        unsigned sum = 0;
        for (int sy = 0; sy < 2; ++sy) for (int sx = 0; sx < 2; ++sx) {
            uint32_t field = 0;
            for (int k = 0; k < 4; ++k)
                field += uint32_t(axes[k][0][2*x+sx]) * axes[k][1][2*y+sy];
            // Q24 products -> 1/64 field LUT, rounded; values >=2 saturate.
            const unsigned index = (field + (1u << 17)) >> 18;
            sum += coverage[index > 128u ? 128u : index];
        }
        const uint8_t a = uint8_t((sum + 2u) / 4u);
        const unsigned offset = 4u * detail::morton(x, y);
        data[offset] = data[offset+1] = data[offset+2] = data[offset+3] = a;
    }
}

} // namespace seance::platform::ui3ds::stylus_liquid
