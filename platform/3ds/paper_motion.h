#pragma once
// Small deterministic surface model. Only the UI presentation uses this clock;
// no engine, random state, allocations, pixels from a framebuffer or audio input.
#include <cmath>
#include <cstdint>
#include "ui_palette.h"

namespace seance::platform::ui3ds::paper_motion {
constexpr int SIZE = 64;
constexpr int BYTES = SIZE * SIZE * 4;
constexpr int UPDATE_MS = 100;
inline float wave(uint64_t ms, unsigned period, float phase) {
    return std::sin(float(ms % period) * (6.28318530718f / period) + phase);
}
struct Spot { float x, y, inv_rx, inv_ry, depth; };
struct Field {
    Spot spots[3];
    explicit Field(uint64_t ms) : spots{
        {70 + 88 * wave(ms, 67000, .2f), 53 + 48 * wave(ms, 89000, 1.1f),
         1 / (178 + 25 * wave(ms, 101000, .5f)), 1 / (128 + 19 * wave(ms, 79000, 1.4f)), 3.3f},
        {309 + 92 * wave(ms, 83000, 2.8f), 179 + 53 * wave(ms, 71000, .3f),
         1 / (201 + 28 * wave(ms, 97000, 2.3f)), 1 / (151 + 22 * wave(ms, 107000, .8f)), 3.1f},
        {195 + 79 * wave(ms, 109000, 4.2f), 110 + 82 * wave(ms, 93000, 3.6f),
         1 / (164 + 21 * wave(ms, 73000, 1.9f)), 1 / (133 + 18 * wave(ms, 113000, 2.1f)), 2.0f}
    } {}
    uint32_t color(float x, float y) const {
        float shade = 0;
        for (const auto& s : spots) {
            const float dx = (x - s.x) * s.inv_rx, dy = (y - s.y) * s.inv_ry;
            const float w = 1 - dx * dx - dy * dy;
            if (w > 0) shade += s.depth * w * w * w;
        }
        // Always darker, never a whole-screen pulse. Hard bound is four levels
        // in graphite, five in pink; bilinear filtering softens the low contrast.
        const int dark = int(std::fmin(shade, 4.f) * palette::paper_depth_scale() + .5f);
        const uint32_t base = palette::paper_color(int(y));
        uint32_t out = 0xff000000u;
        for (unsigned shift = 0; shift < 24; shift += 8) {
            const int c = int((base >> shift) & 255) - dark;
            out |= uint32_t(c < 0 ? 0 : c) << shift;
        }
        return out;
    }
};
constexpr unsigned offset(unsigned x, unsigned y) {
    const unsigned low = (x & 1u) | ((y & 1u) << 1) | ((x & 2u) << 1) |
                         ((y & 2u) << 2) | ((x & 4u) << 2) | ((y & 4u) << 3);
    return (((y >> 3) * (SIZE >> 3) + (x >> 3)) * 64 + low) * 4;
}
inline void paint(uint8_t* data, uint64_t ms) {
    const Field field(ms);
    for (int y = 0; y < SIZE; ++y) for (int x = 0; x < SIZE; ++x) {
        const uint32_t c = field.color((x + .5f) * 400 / SIZE, (y + .5f) * 240 / SIZE);
        uint8_t* p = data + offset(x, y);
        // PICA RGBA8: bytes A,B,G,R. Top-down Morton rows, descending V in draw.
        p[0] = 255; p[1] = uint8_t(c >> 16); p[2] = uint8_t(c >> 8); p[3] = uint8_t(c);
    }
}
// Weak, wide change in ink density, followed by a quiet pause. It never moves
// the glyphs and never adds glitter/noise. One shade per existing native ink run.
inline uint32_t title_ink(uint32_t ink, uint64_t now, uint64_t entered,
                          unsigned label, float x, int width) {
    const unsigned age = now < entered ? 170 : unsigned(now - entered > 170 ? 170 : now - entered);
    const float r = 1 - float(age) / 170;
    float weight = .18f * r * r * r;
    const unsigned phase = unsigned((now + label * 719u) % 19000);
    if (phase < 4800) {
        const float center = -80 + float(phase) / 4800 * (width + 160);
        const float d = std::fabs(x - center) / 80;
        if (d < 1) { const float w = 1 - d * d; weight += .055f * w * w; }
    }
    const uint32_t paper = palette::bg();
    uint32_t out = ink & 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const int a = (ink >> shift) & 255, b = (paper >> shift) & 255;
        out |= uint32_t(std::floor(a + (b - a) * weight + .5f)) << shift;
    }
    return out;
}
} // namespace seance::platform::ui3ds::paper_motion
