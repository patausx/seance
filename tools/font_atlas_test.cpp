// Real __3DS__ draw.cpp/font.cpp; GPU boundary only, not a PICA emulator.
// No screenshots: compare bounded in-memory rasters against the original bitmap.
#include "../core/ui/draw.h"
#include "../core/ui/font.h"
#include <citro2d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace ui = seance::ui;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "fail font-atlas:%d: %s\n", __LINE__, #x); std::abort(); } } while (0)
static bool guard = false, fail_texture = false;
static unsigned allocations = 0, init_calls = 0, deletes = 0, flushes = 0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void* __real_realloc(void*, size_t);
extern "C" void* __wrap_malloc(size_t n) { if (guard) ++allocations; return __real_malloc(n); }
extern "C" void* __wrap_calloc(size_t n, size_t s) { if (guard) ++allocations; return __real_calloc(n, s); }
extern "C" void* __wrap_realloc(void* p, size_t n) { if (guard) ++allocations; return __real_realloc(p, n); }
void* operator new(size_t n) { if (guard) ++allocations; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

constexpr int kWidth = 128, kHeight = 64, kBytes = kWidth * kHeight * 4;
static C3D_Tex* live_texture = nullptr;
static bool nearest = false, clamped = false;
static std::array<u8, kBytes> immutable{};
struct Rect { float x, y, w, h; u32 color; };
struct ExpectedImage { int x, y, scale, glyph; u32 color; bool tinted; };
static std::array<Rect, 8192> rects{}, expected_rects{};
static std::array<ExpectedImage, 512> expected_images{};
static unsigned rect_count = 0, image_count = 0, line_count = 0;
static unsigned expected_runs = 0, expected_image_count = 0, comparisons = 0;
static C2D_Image last_image{};
using Raster = std::array<u32, 400 * 240>;
static Raster actual{}, reference{};
static int screen_width = 320;

static unsigned channel(u32 color, unsigned shift) { return (color >> shift) & 255; }
// Identical ideal source-over compositor on both paths, NOT hardware blend precision.
static void pixel(Raster& raster, int x, int y, u32 color) {
    if (x < 0 || x >= screen_width || y < 0 || y >= 240) return;
    auto& dst = raster[y * 400 + x];
    const unsigned alpha = color >> 24;
    u32 out = 0xff000000u;
    for (unsigned shift : {0u, 8u, 16u})
        out |= ((channel(color, shift) * alpha + channel(dst, shift) * (255 - alpha) + 127) / 255) << shift;
    dst = out;
}
static void raster_rect(Raster& raster, int x, int y, int w, int h, u32 color) {
    for (int yy = std::max(0, y); yy < std::min(240, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(screen_width, x + w); ++xx)
            pixel(raster, xx, yy, color);
}
static void reset(int width = 320, u32 background = C2D_Color32(226, 190, 206, 255)) {
    screen_width = width; actual.fill(background); reference.fill(background);
    rect_count = image_count = line_count = expected_runs = expected_image_count = 0;
}
static int glyph_index(unsigned char c) { return c >= 32 && c < 128 ? c - 32 : 0; }
static bool glyph_on(int index, int x, int y) {
    return x < 6 && (trackr::ui::font_6x8[index][y] & (1u << (5 - x)));
}
static bool glyph_nonempty(int index) {
    for (int y = 0; y < 8; ++y) if (trackr::ui::font_6x8[index][y] & 63) return true;
    return false;
}
static void bitmap(int index, int x, int y, u32 color, int scale) {
    for (int row = 0; row < 8; ++row) for (int col = 0; col < 6; ++col)
        if (glyph_on(index, col, row)) raster_rect(reference, x + col * scale, y + row * scale, scale, scale, color);
}
static void expect_image(int index, int x, int y, u32 color, int scale, bool tinted = true) {
    CHECK(expected_image_count < expected_images.size());
    expected_images[expected_image_count++] = {x, y, scale, index, color, tinted};
}
// Bitmap raster is independent of the production run merger and atlas UV/packing.
// Retain the old run shape separately for fallback/zero/negative scale API checks.
static void oracle_text(int x, int y, const char* text, u32 color, int scale, bool atlas) {
    int cx = x;
    for (const auto* s = reinterpret_cast<const unsigned char*>(text); *s; ++s) {
        if (*s == '\n') { cx = x; y += 8 * scale; continue; }
        const int index = glyph_index(*s);
        bitmap(index, cx, y, color, scale);
        if (atlas && glyph_nonempty(index)) expect_image(index, cx, y, color, scale);
        for (int row = 0; row < 8; ++row) {
            int run = -1;
            for (int col = 0; col <= 6; ++col) {
                const bool on = col < 6 && glyph_on(index, col, row);
                if (on && run < 0) run = col;
                if (!on && run >= 0) {
                    CHECK(expected_runs < expected_rects.size());
                    expected_rects[expected_runs++] = {float(cx + run * scale), float(y + row * scale),
                        float((col - run) * scale), float(scale), color};
                    run = -1;
                }
            }
        }
        cx += 6 * scale;
    }
}
// Independent PICA tiled-address decoder. Match the project's tex3ds-verified
// top-down storage (splash_raster.h): descending UVs, but no storage y flip.
static u32 texel(int x, int y) {
    CHECK(live_texture && x >= 0 && x < kWidth && y >= 0 && y < kHeight);
    unsigned interleave = 0;
    for (unsigned bit = 0; bit < 3; ++bit) {
        interleave |= ((unsigned(x) >> bit) & 1u) << (bit * 2);
        interleave |= ((unsigned(y) >> bit) & 1u) << (bit * 2 + 1);
    }
    const size_t offset = (size_t(y / 8 * (kWidth / 8) + x / 8) * 64 + interleave) * 4;
    const auto* p = static_cast<const u8*>(live_texture->data) + offset;
    return C2D_Color32(p[3], p[2], p[1], p[0]); // PICA RGBA8 storage is A,B,G,R.
}
static void atlas_bytes() {
    for (int y = 0; y < kHeight; ++y) for (int x = 0; x < kWidth; ++x) {
        const int index = y / 8 * 16 + x / 8;
        const bool on = index < 96 && glyph_on(index, x % 8, y % 8);
        CHECK(texel(x, y) == (on ? 0xffffffffu : 0u));
    }
}

bool C3D_TexInit(C3D_Tex* tex, u16 width, u16 height, GPU_TEXCOLOR format) {
    ++init_calls;
    CHECK(width == kWidth && height == kHeight && format == GPU_RGBA8 && !live_texture);
    if (fail_texture) return false;
    tex->data = std::malloc(kBytes); CHECK(tex->data);
    std::memset(tex->data, 0xa5, kBytes);
    tex->width = width; tex->height = height; live_texture = tex;
    nearest = clamped = false;
    return true;
}
void C3D_TexSetFilter(C3D_Tex* tex, GPU_TEXTURE_FILTER_PARAM mag, GPU_TEXTURE_FILTER_PARAM min) {
    CHECK(tex == live_texture && mag == GPU_NEAREST && min == GPU_NEAREST); nearest = true;
}
void C3D_TexSetWrap(C3D_Tex* tex, GPU_TEXTURE_WRAP_PARAM s, GPU_TEXTURE_WRAP_PARAM t) {
    CHECK(tex == live_texture && s == GPU_CLAMP_TO_EDGE && t == GPU_CLAMP_TO_EDGE); clamped = true;
}
void C3D_TexFlush(C3D_Tex* tex) { CHECK(tex == live_texture); atlas_bytes(); ++flushes; }
void C3D_TexBind(int, C3D_Tex*) { CHECK(false); } // text must not bypass Citro2D's cache
void C3D_TexDelete(C3D_Tex* tex) {
    CHECK(tex == live_texture && tex->data);
    std::free(tex->data); tex->data = nullptr; live_texture = nullptr; ++deletes;
}
bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 color) {
    CHECK(z == 0 && rect_count < rects.size());
    rects[rect_count++] = {x, y, w, h, color};
    raster_rect(actual, int(x), int(y), int(w), int(h), color);
    return true;
}
bool C2D_DrawLine(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float thickness, float depth) {
    CHECK(depth == 0 && thickness == 1 && y0 == y1 && x0 <= x1 && c0 == c1);
    ++line_count;
    for (int x = int(x0); x <= int(x1); ++x) pixel(actual, x, int(y0), c0);
    return true;
}
bool C2D_DrawImageAt(C2D_Image image, float x, float y, float depth, const C2D_ImageTint* tint, float sx, float sy) {
    CHECK(image.tex == live_texture && image.subtex && nearest && clamped && depth == 0);
    CHECK(image_count < expected_image_count);
    const auto& e = expected_images[image_count++];
    CHECK(x == e.x && y == e.y && sx == e.scale && sy == e.scale && sx > 0);
    const auto& uv = *image.subtex;
    const int gx = e.glyph % 16 * 8, gy = e.glyph / 16 * 8;
    CHECK(uv.width == 6 && uv.height == 8);
    CHECK(uv.left == gx / 128.0f && uv.right == (gx + 6) / 128.0f);
    CHECK(uv.top == 1 - gy / 64.0f && uv.bottom == 1 - (gy + 8) / 64.0f);
    CHECK(bool(tint) == e.tinted);
    if (tint) for (const auto& corner : tint->corners) CHECK(corner.color == e.color && corner.blend == 1.0f);
    else CHECK(e.color == 0xffffffffu);
    last_image = image;
    for (int dy = 0; dy < int(uv.height * sy); ++dy) for (int dx = 0; dx < int(uv.width * sx); ++dx) {
        const double u = uv.left + (dx + 0.5) / (uv.width * sx) * (uv.right - uv.left);
        const double v = uv.top + (dy + 0.5) / (uv.height * sy) * (uv.bottom - uv.top);
        const u32 sample = texel(int(std::floor(u * kWidth)), int(std::floor((1 - v) * kHeight)));
        const u32 color = (e.color & 0xffffffu) | ((channel(sample, 24) * channel(e.color, 24) / 255) << 24);
        pixel(actual, int(x) + dx, int(y) + dy, color);
    }
    return true;
}

static void same_raster() {
    for (int y = 0; y < 240; ++y) for (int x = 0; x < screen_width; ++x) {
        if (actual[y * 400 + x] != reference[y * 400 + x]) {
            std::fprintf(stderr, "raster mismatch at %d,%d actual=%08x reference=%08x\n", x, y, actual[y * 400 + x], reference[y * 400 + x]);
            CHECK(false);
        }
    }
    ++comparisons;
}
static void compare_text(const char* s, int scale, int width, int x, int y, u32 color, bool atlas, u32 bg = C2D_Color32(226, 190, 206, 255)) {
    reset(width, bg); oracle_text(x, y, s, color, scale, atlas);
    const unsigned old_init = init_calls, old_flush = flushes, old_delete = deletes;
    guard = true; ui::text(x, y, s, color, scale); guard = false;
    CHECK(allocations == 0 && init_calls == old_init && flushes == old_flush && deletes == old_delete);
    CHECK(ui::text_w(s, scale) == int(std::strlen(s)) * 6 * scale);
    CHECK(image_count == expected_image_count && line_count == 0);
    CHECK(rect_count == (atlas ? 0 : expected_runs));
    if (!atlas) for (unsigned i = 0; i < rect_count; ++i) {
        const auto& a = rects[i]; const auto& b = expected_rects[i];
        CHECK(a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.color == b.color);
    }
    same_raster();
    if (live_texture) CHECK(std::memcmp(immutable.data(), live_texture->data, kBytes) == 0);
}
static void mixed_calls() {
    const u32 a = C2D_Color32(181, 154, 232, 97), b = C2D_Color32(31, 20, 31, 255);
    reset(400, C2D_Color32(36, 36, 36, 255));
    raster_rect(reference, 3, 4, 80, 34, b);
    oracle_text(4, 5, "A", a, 3, true);
    raster_rect(reference, 7, 8, 12, 6, C2D_Color32(226, 190, 206, 128));
    expect_image('A' - 32, 33, 5, 0xffffffffu, 2, false); bitmap('A' - 32, 33, 5, 0xffffffffu, 2);
    for (int x = 4; x <= 90; ++x) pixel(reference, x, 10, b);
    oracle_text(62, 7, "z0", C2D_Color32(242, 216, 226, 255), 2, true);
    guard = true;
    ui::rect(3, 4, 80, 34, b);
    ui::text(4, 5, "A", a, 3);
    ui::rect(7, 8, 12, 6, C2D_Color32(226, 190, 206, 128));
    C2D_DrawImageAt(last_image, 33, 5, 0, nullptr, 2, 2);
    ui::line(4, 10, 90, 10, b);
    ui::text(62, 7, "z0", C2D_Color32(242, 216, 226, 255), 2);
    guard = false;
    CHECK(allocations == 0 && rect_count == 3 && image_count == 4 && line_count == 0);
    CHECK(image_count == expected_image_count && rects[0].color == b);
    CHECK(rects[1].color == C2D_Color32(226, 190, 206, 128));
    same_raster();
}
int main() {
    constexpr u32 pink_ink = C2D_Color32(31, 20, 31, 255);
    constexpr u32 lilac = C2D_Color32(181, 154, 232, 255);
    constexpr u32 graphite = C2D_Color32(36, 36, 36, 255);
    char all[104]{}; size_t n = 0;
    for (int ch = 32; ch < 128; ++ch) {
        all[n++] = char(ch);
        if ((ch - 31) % 16 == 0 && ch != 127) all[n++] = '\n';
    }
    char unsupported[190]{}; size_t u = 0; unsigned chars = 0;
    for (int ch = 1; ch <= 255; ++ch) if ((ch < 32 || ch >= 128) && ch != '\n') {
        unsupported[u++] = char(ch);
        if (++chars % 16 == 0) unsupported[u++] = '\n';
    }
    unsupported[u++] = 'A'; // unsupported input must still advance the pen
    ui::text_shutdown(); ui::text_shutdown(); CHECK(deletes == 0);
    compare_text(all, 1, 320, 2, 3, pink_ink, false);
    compare_text("a\nb\x01\x80\xff", 2, 400, -3, -2, lilac, false);
    fail_texture = true; CHECK(!ui::text_init() && init_calls == 1 && flushes == 0 && !live_texture);
    compare_text(all, 2, 400, 2, 3, lilac, false);
    ui::text_shutdown(); CHECK(deletes == 0);
    fail_texture = false;
    CHECK(ui::text_init() && live_texture && init_calls == 2 && flushes == 1 && nearest && clamped);
    auto* retained = live_texture->data;
    atlas_bytes(); std::memcpy(immutable.data(), retained, kBytes);
    CHECK(ui::text_init() && init_calls == 2 && flushes == 1 && live_texture->data == retained);
    for (int scale : {1, 2, 3}) {
        compare_text(all, scale, 320, 2, 3, scale == 2 ? (pink_ink & 0xffffffu) | 0x80000000u : pink_ink, true);
        compare_text(all, scale, 400, 2, 3, scale == 2 ? (lilac & 0xffffffu) | 0x61000000u : lilac, true, graphite);
        compare_text(unsupported, scale, 320, -3, -2, pink_ink, true);
        compare_text("Aa09\n _~\nlast", scale, scale == 2 ? 400 : 320, -4, -5, lilac, true, graphite);
        compare_text("edge\nend", scale, scale == 2 ? 400 : 320, (scale == 2 ? 400 : 320) - 5, 231, pink_ink, true);
    }
    compare_text("transparent\nAa09", 2, 320, 1, 1, pink_ink & 0xffffffu, true);
    compare_text(" \x7f\x01\x80\xff\n ", 3, 400, 1, 1, lilac, true);
    compare_text("", 1, 320, 0, 0, pink_ink, true);
    compare_text("Az 09\n_", 0, 320, 20, 20, pink_ink, false);
    compare_text("Az 09\n_", -2, 400, 120, 60, lilac, false);
    for (const char* label : {"value 42", "hold 50ms", "save copy"}) {
        compare_text(label, 1, 320, 5, 5, pink_ink, true);
        const unsigned expected = std::strcmp(label, "value 42") == 0 ? 7 : 8;
        CHECK(image_count == expected && expected_runs > image_count);
        std::printf("objects '%s': legacy %u rects -> %u images\n", label, expected_runs, image_count);
    }
    mixed_calls();
    CHECK(flushes == 1 && init_calls == 2 && deletes == 0 && live_texture->data == retained);
    CHECK(std::memcmp(immutable.data(), live_texture->data, kBytes) == 0);
    ui::text_shutdown(); CHECK(!live_texture && deletes == 1);
    ui::text_shutdown(); CHECK(deletes == 1);
    compare_text("after shutdown", 2, 320, 2, 3, pink_ink, false);
    CHECK(ui::text_init() && init_calls == 3 && flushes == 2); atlas_bytes();
    CHECK(std::memcmp(immutable.data(), live_texture->data, kBytes) == 0);
    compare_text("reinit\nAa09", 3, 400, -2, 231, lilac, true);
    ui::text_shutdown(); ui::text_shutdown(); CHECK(deletes == 2 && !live_texture && allocations == 0);
    std::printf("pass font-atlas: %u raster cases; all 96 glyphs + %u unsupported bytes; scales 1/2/3, legacy 0/-2; 320/400 clipping, alpha, mixed calls; 32768-byte Morton/UV contract; failure/reinit/idempotence/immutable/no draw allocations\n", comparisons, chars);
    std::puts("boundary: real __3DS__ renderer + GPU shims, not GPU blending/cache/fence, device pixels, FPS or audio evidence; no exports");
}
