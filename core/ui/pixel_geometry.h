#pragma once
#include <cmath>

namespace seance::ui::pixel {
struct Box { int x, y, w, h; };
// Pixel edges use one rounding rule, including negative/off-screen coordinates.
inline int snap(float v) { return int(std::floor(v + 0.5f)); }
inline Box bounds(float x, float y, float w, float h) {
    if (!(w > 0 && h > 0)) return {};
    const int left = snap(x), top = snap(y);
    return {left, top, snap(x + w) - left, snap(y + h) - top};
}
// Inclusive pixel endpoints. Even widths bias toward the negative side; a
// coincident pair is a thickness-square dot. Diagonals keep their GPU backend.
inline bool axis_line(int x0, int y0, int x1, int y1, int thickness, Box& out) {
    if (x0 != x1 && y0 != y1) return false;
    const int t = thickness < 1 ? 1 : thickness, offset = t / 2;
    if (x0 == x1 && y0 == y1) out = {x0 - offset, y0 - offset, t, t};
    else if (y0 == y1) {
        const int left = x0 < x1 ? x0 : x1, right = x0 > x1 ? x0 : x1;
        out = {left, y0 - offset, right - left + 1, t};
    } else {
        const int top = y0 < y1 ? y0 : y1, bottom = y0 > y1 ? y0 : y1;
        out = {x0 - offset, top, t, bottom - top + 1};
    }
    return true;
}
// A pixel belongs to exactly one span, even for 1xN/Nx1 and alpha-colored frames.
// This sink is also used by the sequencer's clipped inverse foreground.
template<class Emit> inline void frame(Box b, Emit emit) {
    if (b.w <= 0 || b.h <= 0) return;
    if (b.w == 1 || b.h == 1) { emit(b); return; }
    emit(Box{b.x, b.y, b.w, 1});
    emit(Box{b.x, b.y + b.h - 1, b.w, 1});
    if (b.h > 2) {
        emit(Box{b.x, b.y + 1, 1, b.h - 2});
        emit(Box{b.x + b.w - 1, b.y + 1, 1, b.h - 2});
    }
}
} // namespace seance::ui::pixel
