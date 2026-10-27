#pragma once
// Static, allocation-free titles for the remaining compact module editors.
// Cropped-ink coordinates match editor_style::heading. No global font fallback,
// engine reads, animation, input ownership or changes to existing menu masks.
#include "module_heading_data.h"
#include "module_frame_layout.h"
#include "../../core/ui/draw.h"
#include <cstring>

namespace seance::platform::ui3ds::module_heading {
// One native-pixel identity anchor on both LCDs, including workflow subpages.
// Keep the cropped glyph masks unchanged; never pass per-editor coordinates.
constexpr int kX = module_frame_layout::kLeft;
constexpr int kY = module_frame_layout::kTopInset;
constexpr int max_height() {
    int height = 0;
    for (const auto& at : module_heading_data::labels)
        if (at.height > height) height = at.height;
    return height;
}
constexpr bool titles_fit() {
    for (const auto& at : module_heading_data::labels)
        if (at.width > module_frame_layout::kTitleMaxWidth ||
            at.height > module_frame_layout::kTitleMaxHeight) return false;
    return true;
}
static_assert(titles_fit(), "module title must fit the fixed chrome envelope");
using module_frame_layout::kRouteY;
using module_frame_layout::kRouteCrossY;
using module_frame_layout::kRouteUnderlineY;
using module_frame_layout::kDetailY;
inline const module_heading_data::Label* label(UnitKind kind) {
    for (const auto& at : module_heading_data::labels) if (at.kind == kind) return &at;
    return nullptr;
}
inline int width(UnitKind kind) {
    const auto* at = label(kind);
    return at ? at->width : 0;
}
inline void draw(int x, int y, UnitKind kind, uint32_t color) {
    const auto* at = label(kind);
    if (!at) return;
    const int stride = (at->width + 7) / 8;
    const auto* bitmap = module_heading_data::bitmap + at->offset;
    for (int row = 0; row < at->height;) {
        const auto* bits = bitmap + row * stride;
        int end = row + 1;
        while (end < at->height && std::memcmp(bits, bitmap + end * stride, stride) == 0) ++end;
        int run = -1;
        for (int column = 0; column <= at->width; ++column) {
            const bool on = column < at->width && (bits[column / 8] & (0x80u >> (column & 7)));
            if (on && run < 0) run = column;
            if (!on && run >= 0) {
                ui::rect(x + run, y + row, column - run, end - row, color);
                run = -1;
            }
        }
        row = end;
    }
}
// Module pages use this fixed entry point; draw remains the low-level raster
// helper for isolated mask tests, not an editor-specific positioning API.
inline void header(UnitKind kind, uint32_t color) {
    draw(kX, kY, kind, color);
}
} // namespace seance::platform::ui3ds::module_heading
