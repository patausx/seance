#pragma once
#include "sequencer_layout.h"
#include <cmath>
#include <cstdint>

namespace seance::platform::ui3ds::seq_ring {
namespace layout = seq_layout;
constexpr float CAPTURE_RADIUS = 18, RELEASE_RADIUS = 28, POINTER_RADIUS = 84;
constexpr unsigned MOVE_MS = 90, OPEN_MS = 120, CAPTURE_MS = 120;
inline float square(float x) { return x * x; }
inline float progress(uint64_t now, uint64_t began, unsigned duration) {
    if (now < began) return 1;
    return now - began >= duration ? 1 : float(now - began) / duration;
}
// UI-only scalar state. Selection uses the raw pointer, never its eased picture.
// update/sample/select run in the controller; drawing only consumes these fields.
struct State {
    int candidate = -1;
    bool visible = false, pinned = false, pad_active = false;
    float x = layout::RING_X, y = layout::RING_Y;
    float raw_x = x, raw_y = y, pin_x = x, pin_y = y;
    float from_x = x, from_y = y, target_x = x, target_y = y;
    float opacity = 0, pulse = 0;
    uint64_t moved_at = 0, opened_at = 0, captured_at = 0;

    void reset() { *this = State{}; }
    void update(uint64_t now) {
        const float t = progress(now, moved_at, MOVE_MS);
        const float ease = 1 - (1-t)*(1-t)*(1-t);
        x = from_x + (target_x - from_x) * ease;
        y = from_y + (target_y - from_y) * ease;
        opacity = visible ? .45f + .55f * progress(now, opened_at, OPEN_MS) : 0;
        pulse = candidate >= 0 ? 1 - progress(now, captured_at, CAPTURE_MS) : 0;
    }
    void show(uint64_t now) {
        if (!visible) { visible = true; opened_at = now; }
        update(now);
    }
    void aim(int next, float tx, float ty, uint64_t now) {
        update(now);
        if (next != candidate) captured_at = now;
        candidate = next;
        if (next >= 0) { tx = layout::SPOKE_X[next]; ty = layout::SPOKE_Y[next]; }
        if (tx != target_x || ty != target_y) {
            from_x = x; from_y = y; target_x = tx; target_y = ty; moved_at = now;
        }
        update(now);
    }
    void select(int mode, uint64_t now) {
        if (mode < 0 || mode >= layout::MODE_COUNT) return;
        pinned = true; pin_x = raw_x; pin_y = raw_y;
        aim(mode, 0, 0, now);
        show(now);
    }
    void sample(int dx, int dy, uint64_t now) {
        float px = float(dx), py = -float(dy);
        const float length2 = square(px) + square(py);
        pad_active = length2 >= 30 * 30;
        if (length2 < 18 * 18) px = py = 0;
        // Both nominal ~140 and full libctru travel reach each magnet; diagonals
        // are radially clamped, not squeezed into a square or a nearest sector.
        px *= float(layout::NODE_RADIUS) / 140;
        py *= float(layout::NODE_RADIUS) / 140;
        const float radius2 = square(px) + square(py);
        if (radius2 > square(POINTER_RADIUS)) {
            const float scale = POINTER_RADIUS / std::sqrt(radius2);
            px *= scale; py *= scale;
        }
        raw_x = layout::RING_X + px; raw_y = layout::RING_Y + py;
        // A touch/key choice survives neutral pad sampling. Deliberate pad
        // movement takes ownership back; tiny hardware jitter does not.
        if (pinned && square(raw_x-pin_x) + square(raw_y-pin_y) <= 6*6) {
            update(now); return;
        }
        pinned = false;
        // Keep the last capture through free space and the pad's spring return.
        // Only another captured node replaces it; cancel/reset still clears it.
        int next = candidate;
        if (next < 0 || square(raw_x-layout::SPOKE_X[next]) +
                        square(raw_y-layout::SPOKE_Y[next]) > square(RELEASE_RADIUS)) {
            float best = square(CAPTURE_RADIUS);
            for (int i = 0; i < layout::MODE_COUNT; ++i) {
                const float distance = square(raw_x-layout::SPOKE_X[i]) + square(raw_y-layout::SPOKE_Y[i]);
                if (distance <= best) { best = distance; next = i; }
            }
        }
        aim(next, raw_x, raw_y, now);
    }
};
static_assert(sizeof(State) <= 112, "bounded magnetic chooser state");
} // namespace seance::platform::ui3ds::seq_ring
