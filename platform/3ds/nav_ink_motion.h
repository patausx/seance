#pragma once
// Pure 200ms handoff for the current GO TO popup. No title morph or retained draw state.
#include <cstdint>
#include "menu_style.h"

namespace seance::platform::ui3ds::nav_ink {
enum class Target { Chains, Sigils, Sequencer, Performance, Patches, Bounce };
constexpr unsigned kDurationMs = 200;
struct Frame {
    Target target = Target::Chains;
    unsigned elapsed_ms = 0;
    bool active = false;
};

namespace detail {
constexpr int kRevealMs = 110;
constexpr int ease(int elapsed, int duration) {
    if (elapsed <= 0) return 0;
    if (elapsed >= duration) return 255;
    const int64_t t = elapsed, d = duration;
    return int((255 * t * t * (3 * d - 2 * t) + d * d * d / 2) / (d * d * d));
}
constexpr int source_veil(int ms) { return ease(ms, kRevealMs); }
constexpr int destination_alpha(int ms) { return ease(ms - kRevealMs, int(kDurationMs) - kRevealMs); }
struct Box { int x0, y0, x1, y1; };
inline void paper(int width, int alpha, Box b = {0, 0, 0, 240}) {
    if (alpha <= 0) return;
    const int x1 = b.x1 ? b.x1 : width;
    menu_style::paper_rect(b.x0, b.y0, x1 - b.x0, b.y1 - b.y0, alpha);
}
} // namespace detail

inline void draw_top_cover(const Frame& frame) {
    if (!frame.active || frame.elapsed_ms >= kDurationMs) return;
    // Incoming-only upper fade; no old-screen capture/freeze or title/shimmer.
    detail::paper(400, 255 - detail::destination_alpha(int(frame.elapsed_ms)));
}
} // namespace seance::platform::ui3ds::nav_ink
