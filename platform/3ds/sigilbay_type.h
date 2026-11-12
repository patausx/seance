#pragma once
// Private sigilbay dynamic headings. Native-sized, baseline-aligned baked ink;
// no scaled 6x8 fallback, heap, textures, font loading or shared mask changes.
#include "sigilbay_type_data.h"
#include "../../core/ui/draw.h"

namespace seance::platform::ui3ds::sigilbay_type {
inline const Glyph& glyph(char c, bool small) {
    if(c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    const Glyph* fallback = &glyphs[0];
    for(const auto& g : glyphs) {
        if(g.small != small) continue;
        if(g.character == c) return g;
        if(g.character == '?') fallback = &g;
    }
    return *fallback;
}
inline int advance(char c, bool small) {
    return c == ' ' ? (small ? 5 : 7) : glyph(c, small).width + 1;
}
inline int width(const char* text, bool small = false) {
    int n = 0; for(; *text; ++text) n += advance(*text, small);
    return n;
}
inline void letter(int x, int baseline, const Glyph& g, uint32_t color) {
    const int stride = (g.width + 7) / 8;
    for(int y = 0; y < g.height;) {
        const auto* bits = bitmap + g.offset + y * stride;
        int end = y + 1;
        for(; end < g.height; ++end) {
            bool same = true;
            for(int b = 0; b < stride; ++b)
                if(bits[b] != bitmap[g.offset + end * stride + b]) { same = false; break; }
            if(!same) break;
        }
        int run = -1;
        for(int col = 0; col <= g.width; ++col) {
            const bool on = col < g.width && (bits[col / 8] & (0x80u >> (col & 7)));
            if(on && run < 0) run = col;
            if(!on && run >= 0) {
                ui::rect(x + run, baseline + g.top + y, col - run, end - y, color);
                run = -1;
            }
        }
        y = end;
    }
}
// Prefer 28ppem; long parameter names use a separately baked 22ppem face.
// Unusual future labels truncate with a real ellipsis, never escape the column.
template<class Letter>
inline int draw_with(int x, int baseline, const char* text, int max_width, uint32_t color,Letter emit) {
    const bool small = width(text) > max_width;
    const bool clipped = width(text, small) > max_width;
    const int dots = clipped ? width("...", small) : 0;
    const int limit = x + max_width;
    for(; *text; ++text) {
        const int step = advance(*text, small);
        if(x + step + dots > limit) break;
        if(*text != ' ') emit(x, baseline, glyph(*text, small), color);
        x += step;
    }
    if(clipped) for(int i = 0; i < 3 && x + advance('.', small) <= limit; ++i) {
        emit(x, baseline, glyph('.', small), color); x += advance('.', small);
    }
    return x;
}
inline int draw(int x,int baseline,const char* text,int max_width,uint32_t color){return draw_with(x,baseline,text,max_width,color,letter);}
} // namespace seance::platform::ui3ds::sigilbay_type
