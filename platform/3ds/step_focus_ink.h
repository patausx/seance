#pragma once
// Inverse-only foreground replay over five disjoint plate spans. Native glyph
// runs stay at their original coordinates. No textures, scissor/flush or pixels.
#include "step_focus_motion.h"
#include "../../core/ui/draw.h"
#include "../../core/ui/pixel_geometry.h"
#include "../../core/ui/font.h"
#include <citro2d.h>

namespace seance::platform::ui3ds::step_focus {
inline void fill(Box b, uint32_t color) {
    for (int i = 0; i < 5; ++i) {
        const Box s = span(b, i);
        ui::rect(s.x, s.y, s.w, s.h, color);
    }
}
inline void clipped_rect(Box mask, Box rect, uint32_t color) {
    if (!overlaps(mask, rect)) return;
    for (int i = 0; i < 5; ++i) {
        const Box b = intersect(span(mask, i), rect);
        if (b.w && b.h) ui::rect(b.x, b.y, b.w, b.h, color);
    }
}
inline void clipped_text(Box mask, int x, int y, const char* text, uint32_t color) {
    for (int cx = x; *text; ++text, cx += trackr::ui::FONT_W) {
        if (*text == '\n') { cx = x - trackr::ui::FONT_W; y += trackr::ui::FONT_H; continue; }
        if (!overlaps(mask, {cx, y, trackr::ui::FONT_W, trackr::ui::FONT_H})) continue;
        for (int row = 0; row < trackr::ui::FONT_H; ++row) {
            const uint8_t bits = trackr::ui::glyph_row(*text, row);
            int run = -1;
            for (int col = 0; col <= trackr::ui::FONT_W; ++col) {
                const bool on = col < trackr::ui::FONT_W && (bits & (1u << (5 - col)));
                if (on && run < 0) run = col;
                if (!on && run >= 0) {
                    clipped_rect(mask, {cx + run, y + row, col - run, 1}, color); run = -1;
                }
            }
        }
    }
}
namespace detail {
struct Point { float x, y; };
inline float distance(Point p, Box b, int edge) {
    switch (edge) {
        case 0: return p.x - b.x;
        case 1: return b.x + b.w - p.x;
        case 2: return p.y - b.y;
        default: return b.y + b.h - p.y;
    }
}
// A convex four-vertex stroke clipped by four planes has at most eight vertices.
// Fixed stack arrays also bound a degenerate/corner intersection.
inline int clip(const Point* input, int count, Point* output, Box b, int edge) {
    int n = 0;
    Point previous = input[count - 1]; float before = distance(previous, b, edge);
    for (int i = 0; i < count; ++i) {
        const Point current = input[i]; const float after = distance(current, b, edge);
        if ((before < 0) != (after < 0)) {
            const float t = before / (before - after);
            output[n++] = {previous.x + (current.x - previous.x) * t,
                           previous.y + (current.y - previous.y) * t};
        }
        if (after >= 0) output[n++] = current;
        previous = current; before = after;
    }
    return n;
}
} // namespace detail
inline void clipped_line(Box mask, int x0, int y0, int x1, int y1, uint32_t color, int thickness) {
    ui::pixel::Box b{};
    if (ui::pixel::axis_line(x0, y0, x1, y1, thickness, b)) {
        clipped_rect(mask, {b.x, b.y, b.w, b.h}, color);
        return;
    }
    if (thickness < 1) thickness = 1;
    // Diagonals reconstruct Citro2D's original butt-ended quad, NOT a Bresenham line or
    // shortened/re-capped segment. Clipping changes coverage only under the plate.
    const int lo_x = x0 < x1 ? x0 : x1, lo_y = y0 < y1 ? y0 : y1;
    const int hi_x = x0 > x1 ? x0 : x1, hi_y = y0 > y1 ? y0 : y1;
    if (!overlaps(mask, {lo_x - thickness, lo_y - thickness,
                        hi_x - lo_x + thickness * 2 + 1, hi_y - lo_y + thickness * 2 + 1})) return;
    const float dx = float(x1 - x0), dy = float(y1 - y0), len = std::sqrt(dx * dx + dy * dy);
    if (len == 0) return;
    const float half = float(thickness < 1 ? 1 : thickness) / 2;
    const float ux = -dy / len * half, uy = dx / len * half;
    for (int i = 0; i < 5; ++i) {
        detail::Point a[12] = {{x0 - ux, y0 - uy}, {x0 + ux, y0 + uy},
                              {x1 + ux, y1 + uy}, {x1 - ux, y1 - uy}}, b[12];
        int count = 4; const Box region = span(mask, i);
        for (int edge = 0; edge < 4 && count; ++edge) {
            count = detail::clip(a, count, b, region, edge);
            for (int j = 0; j < count; ++j) a[j] = b[j];
        }
        for (int j = 1; j + 1 < count; ++j) {
            const float area = (a[j].x - a[0].x) * (a[j+1].y - a[0].y) -
                               (a[j].y - a[0].y) * (a[j+1].x - a[0].x);
            if (std::fabs(area) > 0.00001f)
                C2D_DrawTriangle(a[0].x, a[0].y, color, a[j].x, a[j].y, color,
                                 a[j+1].x, a[j+1].y, color, 0);
        }
    }
}
} // namespace seance::platform::ui3ds::step_focus
