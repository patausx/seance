#include "platform/3ds/frame_profile.h"
#include "platform/3ds/debug_overlay.h"
#include <3ds.h>
#include <citro2d.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

using namespace seance::platform;
static bool forbid_allocations = false;
void* operator new(std::size_t n) { assert(!forbid_allocations); if (void* p = std::malloc(n)) return p; std::abort(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static uint64_t tick;
static uint32_t vb;
static unsigned reads, scenes, texts, rects;
static char drawn[8][28];
static void (*apt_cb)(APT_HookType, void*);
static C3D_RenderTarget_tag top;
uint64_t svcGetSystemTick() { ++reads; return tick; }
void aptHook(aptHookCookie*, void (*cb)(APT_HookType, void*), void*) { apt_cb = cb; }
void aptUnhook(aptHookCookie*) { apt_cb = nullptr; }
float C3D_GetDrawingTime() { ++reads; return 2.5f; }
uint32_t C3D_FrameCounter(int id) { assert(id == 0); ++reads; return vb; }
void C2D_SceneBegin(C3D_RenderTarget_tag* target) { assert(target == &top); ++scenes; }
namespace seance::ui {
uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return r | (g << 8) | (b << 16) | 0xff000000u; }
void rect(float x, float y, float w, float h, uint32_t) {
    assert(x >= 0 && y >= 0 && x + w <= 400 && y + h <= 240); ++rects;
}
void text(int x, int y, const char* s, uint32_t, int scale) {
    assert(scale == 1 && x >= 234 && y >= 7 && x + std::strlen(s) * 6 <= 396 && y + 8 <= 82);
    assert(texts < 8); std::snprintf(drawn[texts++], 28, "%s", s);
}
}
static void draw() { texts = rects = scenes = 0; debug_overlay::draw(&top); }
static void frame(uint64_t request, uint64_t begin, uint64_t end, uint32_t vblank) {
    tick = request; debug_overlay::before_frame();
    tick = begin; vb = vblank; debug_overlay::after_begin();
    tick = end; debug_overlay::after_end();
}
static void toggle() {
    uint32_t down = KEY_ZR | KEY_A, held = KEY_ZR | KEY_TOUCH, up = KEY_ZR | KEY_B;
    debug_overlay::input(down, held, up);
    assert(down == KEY_A && held == KEY_TOUCH && up == KEY_B);
}
int main() {
    forbid_allocations = true;
    {
        frame_profile::Ring r;
        r.begin(1000, 0, 0xfffffffeu, 123);
        r.end(4000);
        r.begin(17667, 5667, 1, 2500);
        assert(r.refresh(17667));
        const auto& s = r.summary();
        assert(s.samples == 1 && s.missed_total == 2 && s.missed_window == 2);
        assert(s.metric[frame_profile::Prep].avg == 1667);
        assert(s.metric[frame_profile::Ui].avg == 3000);
        assert(s.metric[frame_profile::Wait].avg == 12000);
        assert(s.metric[frame_profile::Frame].avg == 16667);
        assert(s.metric[frame_profile::Gpu].avg == 2500);
        assert(!r.refresh(17668));
        r.reset(); r.begin(1000, 0, 1, 0);
        for (unsigned i = 1; i <= 20; ++i) {
            r.end(i * 10000 - 9000 + i * 100);
            r.begin((i + 1) * 10000 - 9000, i * 10000 - 4000, i + 1, 0);
        }
        assert(r.refresh(201000));
        assert(r.summary().samples == 20);
        const auto st = r.summary().metric[frame_profile::Ui];
        assert(st.avg == 1050 && st.p95 == 1900 && st.max == 2000);
        assert(!r.refresh(450999) && r.refresh(451000));
        assert(r.refresh(4000000) && r.summary().samples == 0);
    }
    std::puts("pass exact cycle phases, nearest-rank p95, cadence wrap, 4 Hz and time expiry");
    {
        frame_profile::Ring r;
        r.begin(0, 0, 0, 0);
        for (unsigned i = 1; i <= 400; ++i) {
            r.end((i - 1) * 16667 + 3000);
            r.begin(i * 16667, i * 16667 - 8000, i, 2000);
        }
        r.refresh(400 * 16667);
        assert(r.summary().samples == 180 && r.summary().missed_total == 0);
        r.reset(); r.begin(0, 0, 0, 0);
        for (unsigned i = 1; i <= 400; ++i) {
            r.end((i - 1) * 1000 + 100);
            r.begin(i * 1000, i * 1000 - 200, i, 0);
        }
        r.refresh(400000);
        assert(r.summary().samples == frame_profile::Capacity);
        r.reset(); r.begin(0, 0, 0, 0); r.end(100);
        r.begin(9000000, 8999900, 540, 0); r.refresh(9000000);
        assert(r.summary().missed_total == 539 && r.summary().metric[frame_profile::Frame].max == 9000000);
        r.refresh(13000000);
        assert(!r.summary().samples && !r.summary().missed_window && r.summary().missed_total == 539);
        // Missing end (failed/skipped submission) must not manufacture a sample.
        r.reset(); r.begin(0, 0, 0, 0); r.begin(16667, 8000, 1, 0); r.refresh(16667);
        assert(!r.summary().samples);
    }
    std::puts("pass fixed capacity, rolling 3s, no jitter-based false drops, long real stall and reset");
    debug_overlay::init(); assert(apt_cb);
    frame(0, 1000, 4000, 1); draw(); assert(!texts && !reads && !scenes);
    toggle(); draw(); assert(texts == 8 && scenes == 1 && rects == 1 && std::strstr(drawn[1], "--"));
    frame(0, 1000, 4000, 1);
    for (unsigned i = 1; i <= 20; ++i) frame(i * 16667 - 7000, i * 16667 + 1000, i * 16667 + 4000, i + 1);
    draw(); assert(!std::strstr(drawn[1], "--") && std::strstr(drawn[6], "0/0"));
    assert(std::strstr(drawn[3], "2.5")); // fenced previous GPU queue
    char cached[sizeof drawn]; std::memcpy(cached, drawn, sizeof drawn);
    uint32_t down = 0, held = KEY_ZR, up = 0;
    debug_overlay::input(down, held, up); assert(!held); // hold never toggles
    draw(); assert(!std::memcmp(cached, drawn, sizeof drawn));
    // Resume before a frame, then a suspend during FrameBegin's wait.
    apt_cb(APTHOOK_ONWAKEUP, nullptr);
    frame(100000000, 100001000, 100004000, 5000);
    draw(); assert(std::strstr(drawn[1], "--") && std::strstr(drawn[6], "0/0"));
    tick += 5000; debug_overlay::before_frame(); apt_cb(APTHOOK_ONRESTORE, nullptr);
    tick += 100000000; vb += 6000; debug_overlay::after_begin(); debug_overlay::after_end();
    draw(); assert(std::strstr(drawn[1], "--") && std::strstr(drawn[6], "0/0"));
    toggle(); const auto before = reads;
    frame(300000000, 300001000, 300004000, 20000); draw();
    assert(!texts && !scenes && reads == before);
    toggle(); draw(); assert(std::strstr(drawn[1], "--"));
    debug_overlay::shutdown(); assert(!apt_cb);
    forbid_allocations = false;
    std::puts("pass actual overlay: ZR edges, cached top-only bounds, no hidden timing/draw, suspend and no C++ heap");
}
