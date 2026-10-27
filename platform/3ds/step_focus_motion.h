#pragma once
// Variant 03: elapsed-time ink plate. No engine, allocations or retained pixels.
#include <cmath>
#include <cstdint>
#include "sequencer_layout.h"

namespace seance::platform::ui3ds::step_focus {
constexpr unsigned DURATION_MS = 100;
struct Pose { float x, y, w, h; };
struct Box { int x, y, w, h; };
inline float clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline Pose target(int row, int col) {
    return {float(seq_layout::X0 + col * seq_layout::PITCH), float(seq_layout::ROWS[row]),
            float(seq_layout::CELL_W), float(seq_layout::BADGE_H)};
}
inline Pose geometry(Pose a, Pose b, unsigned ms) {
    if (!ms) return a;
    if (ms >= DURATION_MS) return b;
    const float t = float(ms) / DURATION_MS, rem = 1 - t, e = 1 - rem * rem * rem;
    const float pulse = std::sin(3.14159265358979323846f * t) * rem * .7f;
    const float stretch = clamp(std::fabs(b.x - a.x), 0, 12) * pulse, squash = 3 * pulse;
    Pose out{a.x + (b.x - a.x) * e - stretch / 2,
             a.y + (b.y - a.y) * e + squash / 2,
             a.w + (b.w - a.w) * e + stretch,
             a.h + (b.h - a.h) * e - squash};
    // Repeated interruptions must not accumulate unbounded width/squash.
    out.w = clamp(out.w, 3, 30); out.h = clamp(out.h, 12, 18);
    out.x = clamp(out.x, 0, 320 - out.w); out.y = clamp(out.y, 41, 192 - out.h);
    return out;
}
inline Box raster(Pose p) {
    return {int(std::floor(p.x + .5f)), int(std::floor(p.y + .5f)),
            int(std::floor(p.w + .5f)), int(std::floor(p.h + .5f))};
}
inline bool overlaps(Box a, Box b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
inline Box intersect(Box a, Box b) {
    const int x = a.x > b.x ? a.x : b.x, y = a.y > b.y ? a.y : b.y;
    const int r = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    const int d = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    return {x, y, r > x ? r - x : 0, d > y ? d - y : 0};
}
// Disjoint spans: exactly the native two-pixel chamfer, only five GPU fills.
inline Box span(Box b, int i) {
    switch (i) {
        case 0: return {b.x + 2, b.y, b.w - 4, 1};
        case 1: return {b.x + 1, b.y + 1, b.w - 2, 1};
        case 2: return {b.x, b.y + 2, b.w, b.h - 4};
        case 3: return {b.x + 1, b.y + b.h - 2, b.w - 2, 1};
        default: return {b.x + 2, b.y + b.h - 1, b.w - 4, 1};
    }
}
inline bool contains(Box b, int x, int y) {
    for (int i = 0; i < 5; ++i) if (overlaps(span(b, i), {x, y, 1, 1})) return true;
    return false;
}
struct State {
    Pose shown{}, start{}, end{};
    uint64_t began = 0, last = 0;
    int context = -1, cell = -1;
    void reset() { context = cell = -1; }
    Box update(uint64_t now, int next_context, int row, int col) {
        const Pose destination = target(row, col);
        const int next_cell = row * seq_layout::PAGE_STEPS + col;
        if (context != next_context || now < last || now - last > 200) {
            shown = start = end = destination; began = now;
        } else if (cell != next_cell) {
            // Retarget the last actually displayed pose, never the old cell or
            // an extrapolated hidden frame. Logical selection is already live.
            start = shown; end = destination; began = now;
        }
        context = next_context; cell = next_cell; last = now;
        shown = geometry(start, end, unsigned(now - began > DURATION_MS ? DURATION_MS : now - began));
        return raster(shown);
    }
};
static_assert(sizeof(State) <= 80, "bounded scalar focus state");
} // namespace seance::platform::ui3ds::step_focus
