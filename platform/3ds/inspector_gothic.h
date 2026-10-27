#pragma once
// Numeric inspector renderer using the existing Seance Note bitmap derivative.
// Units remain in the native UI font; unsupported/non-numeric values fall back whole.
#include "note_display_data.h"
#include "../../core/ui/draw.h"

namespace seance::platform::ui3ds::inspector_gothic {
struct Result {
    int end_x;
    const char* suffix;
    bool drawn;
};

inline const note_display::Glyph* digit(char c) {
    if (c < '0' || c > '9') return nullptr;
    for (const auto& glyph : note_display::glyphs)
        if (glyph.character == c && !glyph.small) return &glyph;
    return nullptr;
}

// At scale 1 the baked 56 px source masks occupy the same large-value height
// as the previous 4x native 6x8 text. Scale 2 is the STRN renderer geometry.
inline Result draw_numeric(int x, int baseline, const char* text, uint32_t ink,
                           uint32_t accent, int scale = 1) {
    const int start = x;
    const char* p = text;
    bool has_digit = false;
    for (; *p; ++p) {
        if (const auto* glyph = digit(*p)) {
            has_digit = true;
            const int stride = (glyph->width + 7) / 8;
            for (int y = 0; y < glyph->height; ++y) {
                int run = -1;
                for (int column = 0; column <= glyph->width; ++column) {
                    const bool on = column < glyph->width &&
                        (note_display::bitmap[glyph->offset + y * stride + column / 8] &
                         (0x80u >> (column & 7)));
                    if (on && run < 0) run = column;
                    if (!on && run >= 0) {
                        ui::rect(x + run * scale, baseline + glyph->top * scale + y * scale,
                                 (column - run) * scale, scale, ink);
                        run = -1;
                    }
                }
            }
            x += glyph->width * scale + 2 * scale;
        } else if (*p == '+' || *p == '-') {
            ui::rect(x, baseline - 14 * scale, 13 * scale, 2 * scale, accent);
            if (*p == '+')
                ui::rect(x + (11 * scale + 1) / 2, baseline - 20 * scale,
                         2 * scale, 13 * scale, accent);
            x += 18 * scale;
        } else if (*p == '.') {
            ui::rect(x, baseline - 3 * scale, 3 * scale, 3 * scale, ink);
            x += 6 * scale;
        } else {
            break;
        }
    }
    if (!has_digit) return {start, text, false};
    return {x, p, true};
}
} // namespace seance::platform::ui3ds::inspector_gothic
