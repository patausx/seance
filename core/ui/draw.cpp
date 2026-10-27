#include "draw.h"
#include "font.h"
#include "pixel_geometry.h"
#include <algorithm>
#include <citro2d.h>
#ifdef __3DS__
#include <cstring>
#endif

namespace seance::ui {

#ifdef __3DS__
namespace {
// Six rows of sixteen 8x8 cells. The 6x8 crop preserves the donor's exact
// advance; nearest filtering needs no resampling or inter-glyph bleed guards.
constexpr unsigned ATLAS_W = 128, ATLAS_H = 64, ATLAS_COLS = ATLAS_W / 8;
C3D_Tex text_texture{};
Tex3DS_SubTexture text_glyphs[96]{};
bool text_ink[96]{};
bool text_ready = false;

unsigned text_texel_offset(unsigned x, unsigned y) {
    // PICA/tex3ds top-down 8x8 Morton tiles; descending V in the subtexture.
    // Do NOT flip y: this is the same layout as the existing splash/paper.
    const unsigned low = (x & 1u) | ((y & 1u) << 1) | ((x & 2u) << 1) |
                         ((y & 2u) << 2) | ((x & 4u) << 2) | ((y & 4u) << 3);
    return (((y >> 3) * ATLAS_COLS + (x >> 3)) * 64 + low) * 4;
}
} // namespace
#endif

bool text_init() {
#ifdef __3DS__
    if (text_ready) return true;
    if (!C3D_TexInit(&text_texture, ATLAS_W, ATLAS_H, GPU_RGBA8)) return false;
    C3D_TexSetFilter(&text_texture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&text_texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    auto* data = static_cast<uint8_t*>(text_texture.data);
    std::memset(data, 0, ATLAS_W * ATLAS_H * 4);
    for (unsigned i = 0; i < 96; ++i) {
        const unsigned x = (i % ATLAS_COLS) * 8, y = (i / ATLAS_COLS) * 8;
        text_glyphs[i] = {trackr::ui::FONT_W, trackr::ui::FONT_H,
            float(x) / ATLAS_W, 1.0f - float(y) / ATLAS_H,
            float(x + trackr::ui::FONT_W) / ATLAS_W,
            1.0f - float(y + trackr::ui::FONT_H) / ATLAS_H};
        text_ink[i] = false;
        for (int row = 0; row < trackr::ui::FONT_H; ++row) {
            const uint8_t bits = trackr::ui::font_6x8[i][row];
            for (int col = 0; col < trackr::ui::FONT_W; ++col) {
                if (!(bits & (1 << (5 - col)))) continue;
                // RGBA8 storage is A,B,G,R; white binary mask, tinted at draw.
                std::memset(data + text_texel_offset(x + col, y + row), 255, 4);
                text_ink[i] = true;
            }
        }
    }
    C3D_TexFlush(&text_texture);
    text_ready = true;
    return true;
#else
    return false; // ordinary host harnesses retain the rectangle backend
#endif
}

void text_shutdown() {
#ifdef __3DS__
    // The owner must fence GPU readers and reset texture caches first.
    if (text_ready) C3D_TexDelete(&text_texture);
    text_ready = false;
#endif
}

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return C2D_Color32(r, g, b, 255); }

void rect(float x, float y, float w, float h, uint32_t c) {
    C2D_DrawRectSolid(x, y, 0, w, h, c);
}

void frame(float x, float y, float w, float h, uint32_t c) {
    pixel::frame(pixel::bounds(x, y, w, h), [c](pixel::Box b) {
        rect(b.x, b.y, b.w, b.h, c);
    });
}

void line(int x0, int y0, int x1, int y1, uint32_t c, int thickness) {
    pixel::Box b{};
    if (pixel::axis_line(x0, y0, x1, y1, thickness, b)) {
        // Rails/frames must cover whole LCD pixels, not straddle their edges.
        // One rectangle even for a full-screen line; no per-pixel object loop.
        rect(b.x, b.y, b.w, b.h, c);
        return;
    }
    if (thickness < 1) thickness = 1;
    // Keep diagonal waveform/cable geometry and its one-object budget unchanged.
    C2D_DrawLine((float)x0, (float)y0, c,
                 (float)x1, (float)y1, c,
                 (float)thickness, 0.0f);
}

void circle(float x, float y, float radius, uint32_t c, bool filled) {
    if (radius < 1.0f) radius = 1.0f;
    // Avoid Citro2D's expensive ellipse-mode state transition. Small concentric
    // horizontal spans are cheap, deterministic, and mix safely with line/text.
    const int r = (int)(radius + 0.5f);
    const int cx = (int)(x + 0.5f);
    const int cy = (int)(y + 0.5f);
    for (int dy = -r; dy <= r; ++dy) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) ++dx;
        if (filled) {
            if (dx > 0) rect((float)(cx - dx), (float)(cy + dy), (float)(dx * 2 + 1), 1.0f, c);
        } else {
            rect((float)(cx - dx), (float)(cy + dy), 1.0f, 1.0f, c);
            if (dx > 0) rect((float)(cx + dx), (float)(cy + dy), 1.0f, 1.0f, c);
        }
    }
}

void text(int x, int y, const char* s, uint32_t c, int scale) {
    using trackr::ui::glyph_row;
    using trackr::ui::FONT_W;
    using trackr::ui::FONT_H;
#ifdef __3DS__
    // No lazy allocation/upload or global tint changes during frame submission.
    const bool atlas = text_ready && scale > 0;
    C2D_ImageTint tint;
    if (atlas) C2D_PlainImageTint(&tint, c, 1.0f);
#endif
    int cx = x;
    for (; *s; ++s) {
        if (*s == '\n') { cx = x; y += FONT_H * scale; continue; }
#ifdef __3DS__
        if (atlas) {
            unsigned index = static_cast<unsigned char>(*s) - 0x20u;
            if (index >= 96) index = 0; // same unsupported-byte mapping as glyph_row
            if (text_ink[index]) {
                const C2D_Image image{&text_texture, &text_glyphs[index]};
                C2D_DrawImageAt(image, float(cx), float(y), 0, &tint,
                                float(scale), float(scale));
            }
            cx += FONT_W * scale;
            continue;
        }
#endif
        // Exact original run renderer: allocation-failure/uninitialized fallback.
        for (int row = 0; row < FONT_H; ++row) {
            uint8_t bits = glyph_row(*s, row);
            if (!bits) continue;
            // merge horizontal runs into one rect per run (fewer draw calls)
            int run = -1;
            for (int col = 0; col <= FONT_W; ++col) {
                bool on = col < FONT_W && (bits & (1 << (5 - col)));
                if (on && run < 0) run = col;
                if (!on && run >= 0) {
                    C2D_DrawRectSolid((float)(cx + run * scale), (float)(y + row * scale), 0,
                                      (float)((col - run) * scale), (float)scale, c);
                    run = -1;
                }
            }
        }
        cx += FONT_W * scale;
    }
}

int text_w(const char* s, int scale) {
    int n = 0;
    while (*s++) ++n;
    return n * trackr::ui::FONT_W * scale;
}

void hslider(float x, float y, float w, float h, int32_t norm, bool bipolar,
             uint32_t track, uint32_t fill) {
    // Snap this pixel widget, not rect(): paper/large animated fills keep floats.
    const auto box = pixel::bounds(x, y, w, h);
    if (box.w <= 0 || box.h <= 0) return;
    x = box.x; y = box.y; w = box.w; h = box.h;
    const int rail_y = pixel::snap(y + h * 0.5f);
    rect(x, rail_y, w, 1, track);
    const float center = bipolar ? pixel::snap(x + w * 0.5f) : x;
    float value_x = bipolar
                  ? x + ((float)norm + 32767.f) * w / 65534.f
                  : x + (float)norm * w / 32767.f;
    if (value_x < x) value_x = x;
    if (value_x > x + w) value_x = x + w;
    value_x = pixel::snap(value_x);
    const float start = value_x < center ? value_x : center;
    const float extent = value_x < center ? center - value_x : value_x - center;
    if (extent > 0) rect(start, rail_y - 2, extent, 5, fill);
    if (bipolar) rect(center, y, 1, h, track);
    rect(value_x - 1, y - 1, 3, h + 2, fill);
}

} // namespace seance::ui
