#include "screenshot_io.h"
#include <3ds.h>
#include <cstdio>

namespace seance::platform {

static void wr16(FILE* f, u16 v) {
    fputc((int)(v & 0xff), f); fputc((int)((v >> 8) & 0xff), f);
}
static void wr32(FILE* f, u32 v) {
    fputc((int)(v & 0xff), f); fputc((int)((v >> 8) & 0xff), f);
    fputc((int)((v >> 16) & 0xff), f); fputc((int)((v >> 24) & 0xff), f);
}
static bool save_screen_bmp(const char* path, gfxScreen_t screen, int out_w, int out_h) {
    u16 fb_w = 0, fb_h = 0;
    u8* fb = gfxGetFramebuffer(screen, GFX_LEFT, &fb_w, &fb_h);
    (void)fb_w; (void)fb_h;
    if (!fb) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const int row_bytes = (out_w * 3 + 3) & ~3;
    const u32 pixel_bytes = (u32)(row_bytes * out_h);
    fputc('B', f); fputc('M', f); wr32(f, 54 + pixel_bytes); wr16(f, 0); wr16(f, 0);
    wr32(f, 54); wr32(f, 40); wr32(f, (u32)out_w); wr32(f, (u32)out_h);
    wr16(f, 1); wr16(f, 24); wr32(f, 0); wr32(f, pixel_bytes);
    wr32(f, 2835); wr32(f, 2835); wr32(f, 0); wr32(f, 0);
    static const u8 pad[3] = {0, 0, 0};
    const int pad_n = row_bytes - out_w * 3;
    for (int by = out_h - 1; by >= 0; --by) {
        for (int x = 0; x < out_w; ++x) {
            const int off = ((out_h - 1 - by) + x * out_h) * 3;
            fputc(fb[off], f); fputc(fb[off + 1], f); fputc(fb[off + 2], f);
        }
        if (pad_n) fwrite(pad, 1, pad_n, f);
    }
    const bool ok = !ferror(f) && fclose(f) == 0;
    return ok;
}

bool save_screenshots(const char* top_path, const char* bottom_path) {
    const bool top = save_screen_bmp(top_path, GFX_TOP, 400, 240);
    const bool bottom = save_screen_bmp(bottom_path, GFX_BOTTOM, 320, 240);
    return top && bottom;
}

} // namespace seance::platform
