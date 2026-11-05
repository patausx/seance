#pragma once
// FRACT's approved unframed spring-grab row, not a second fader stencil or style.
// Shared native paper/font/palette/frame and actual lancet geometry are retained.
#include "fract_grab_motion.h"
#include "module_fader.h"
#include "editor_style.h"

namespace fract_grab {
namespace row {
namespace e = seance::platform::ui3ds::editor_style;
namespace f = seance::platform::ui3ds::module_fader;
namespace ui = seance::ui;

struct LargePen {
    int x, y;
    void block(int u, int v, int w, int h, uint32_t color) const {
        // Integer scaling of the ACTUAL hollow 7x11 shared diamond ->14x22.
        // Geometry stays owned by module_fader::detail::diamond, not copied here.
        ui::rect(x+2*u, y+2*v, 2*w, 2*h, color);
    }
};
struct ClearFootprintPen {
    int x, y, thumb;
    void block(int u, int v, int w, int h, uint32_t color) const {
        // Omit rail/end/home/tension footprints BEFORE the hollow outline. All
        // remaining hole pixels are the original textured paper, never an erase.
        const int left = x+u, right = left+w;
        for (int yy=y+v; yy<y+v+h; ++yy) {
            const int dy = yy-y;
            const int row = dy >= 0 ? dy/2 : -((-dy+1)/2);
            if (row < -5 || row > 5) { ui::rect(left, yy, w, 1, color); continue; }
            const int half = f::detail::half_width(row, false);
            const int a = thumb-2*half, b = thumb+2*half+2;
            const int first = std::min(right, a), second = std::max(left, b);
            if (first > left) ui::rect(left, yy, first-left, 1, color);
            if (second < right) ui::rect(second, yy, right-second, 1, color);
        }
    }
};
inline void draw(Pose pose, bool locked, bool bypass, bool mute,
                 bool hold_selected, const char* hold_value) {
    const bool disabled = !pose.enabled || locked || bypass || mute;
    const bool physical = pose.physical && !disabled; // a held fragment is NOT a press
    ui::text(e::kLeft, 91, "grab", disabled ? e::ghost() : physical ? e::spot() : e::ink());
    ui::text(100, 91, locked ? "locked step" : bypass ? "bypassed" :
             mute ? "chain muted" : "pull left", e::ghost());
    const ClearFootprintPen pen{kLeft, kAxisY, pose.x};
    f::detail::track(pen, kRight-kLeft); // SAME thin pointed-ended native rail
    // Home is a return anchor, never a fake solid live/effective diamond.
    for (int v=-3; v<=3; ++v) pen.block(kHome-kLeft, v, 1, 1, e::branch());
    if (!disabled && pose.x != kHome) {
        const int a = std::min(int(pose.x), kHome), b = std::max(int(pose.x), kHome);
        pen.block(a-kLeft, 0, b-a+1, 1, physical ? e::ghost() : e::branch());
    }
    f::detail::diamond(LargePen{pose.x, kAxisY}, 0, false,
                       disabled ? e::ghost() : physical ? e::spot() : e::ink(), false);
    // Existing HOLD wording, value, coordinates, selection and touch ownership;
    // its old visible box is omitted only within this approved row.
    ui::text(248, 98, "hold", hold_selected ? e::spot() : e::ink());
    ui::text(248, 113, hold_value, e::ghost());
}
} // namespace row
} // namespace fract_grab
