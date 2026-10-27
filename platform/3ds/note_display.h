#pragma once
// Seance Note, OFL 1.1 bitmap derivative of Gossip Med Pixel.
// Copyright 2025 Deborah Khodanovich; see assets/fonts/OFL.txt and README.md.
#include "note_display_data.h"
#include "../../core/ui/draw.h"

namespace seance::platform::ui3ds::note_display {
inline const Glyph* find(char c, bool small = false) {
    for (const auto& glyph : glyphs)
        if (glyph.character == c && glyph.small == small) return &glyph;
    return nullptr;
}
inline void draw_glyph(int x, int baseline, const Glyph& g, uint32_t color) {
    const int stride = (g.width + 7) / 8;
    for (int y = 0; y < g.height; ++y) {
        int run = -1;
        for (int column = 0; column <= g.width; ++column) {
            const bool on = column < g.width &&
                (bitmap[g.offset + y * stride + column / 8] & (0x80u >> (column & 7)));
            if (on && run < 0) run = column;
            if (!on && run >= 0) {
                ui::rect(x + run, baseline + g.top + y, column - run, 1, color);
                run = -1;
            }
        }
    }
}
inline void sharp(int x, int y, uint32_t color) {
    ui::rect(x + 4, y, 2, 18, color);
    ui::rect(x + 10, y, 2, 18, color);
    ui::rect(x, y + 4, 14, 2, color);
    ui::rect(x, y + 12, 14, 2, color);
}
inline void sign(int x, int y, bool positive, uint32_t color) {
    ui::rect(x, y + 8, 18, 2, color);
    if (positive) ui::rect(x + 8, y, 2, 18, color);
}
// Validate the entire small bounded string before any drawing. No silent .notdef.
// Engine domain: c1..c7, optional sharp. Returns false for ordinary-font fallback.
inline bool melodic(const char* pitch, uint32_t ink, uint32_t accent, int dx = 0, int dy = 0) {
    if (!pitch || pitch[0] < 'a' || pitch[0] > 'g') return false;
    const bool has_sharp = pitch[1] == '#';
    const int digit = has_sharp ? 2 : 1;
    if (pitch[digit] < '0' || pitch[digit] > '9' || pitch[digit + 1]) return false;
    const Glyph* note = find(pitch[0]);
    const Glyph* octave = find(pitch[digit], true);
    if (!note || !octave) return false;
    int x = 10 + dx;
    draw_glyph(x, 96 + dy, *note, ink);
    x += note->width;
    if (has_sharp) { sharp(x + 5, 60 + dy, accent); x += 19; }
    draw_glyph(x + 6, 99 + dy, *octave, accent);
    return true;
}
// PLAY/GRAN have optional r, STRN only signs. Pitch identity remains engine-owned.
// At most 2 digits; worst r-72 stays x10..104, y47..87, above the y91 unit label.
inline bool semitones(const char* pitch, uint32_t ink, uint32_t accent, int dx = 0, int dy = 0) {
    if (!pitch) return false;
    int i = pitch[0] == 'r' ? 1 : 0;
    const bool reverse = i == 1;
    if (pitch[i] != '+' && pitch[i] != '-') return false;
    const bool positive = pitch[i++] == '+';
    const Glyph* digits[2] = {};
    int count = 0;
    while (count < 2 && pitch[i]) {
        if (pitch[i] < '0' || pitch[i] > '9') return false;
        digits[count++] = find(pitch[i++]);
    }
    if (!count || pitch[i]) return false;
    int x = 10 + dx;
    if (reverse) { const Glyph* r = find('r', true); draw_glyph(x, 79 + dy, *r, accent); x += r->width + 5; }
    sign(x, 64 + dy, positive, accent);
    x += 25;
    for (int n = 0; n < count; ++n) {
        draw_glyph(x, 81 + dy, *digits[n], ink);
        x += digits[n]->width + 3;
    }
    return true;
}
} // namespace seance::platform::ui3ds::note_display
