#include "debug_overlay.h"
#include "frame_profile.h"
#include "../../core/ui/draw.h"
#include <3ds.h>
#include <citro2d.h>
#include <cstdio>
#include <cmath>

namespace seance::platform::debug_overlay {
namespace {
frame_profile::Ring ring;
#ifdef SEANCE_STARTUP_INPUT_DIAGNOSTIC
// Temporary device troubleshooting build only; release visibility is unchanged.
bool visible = true;
#else
bool visible = false;
#endif
aptHookCookie hook{};
uint32_t interrupted = 0;
uint64_t requested_us = 0;
#ifdef SEANCE_STARTUP_INPUT_DIAGNOSTIC
constexpr int Rows = 9, Columns = 28;
#else
constexpr int Rows = 8, Columns = 28;
#endif
char lines[Rows][Columns]{};

uint64_t clock_us() {
    const uint64_t tick = svcGetSystemTick();
    return (tick / SYSCLOCK_ARM11) * 1000000 +
        ((tick % SYSCLOCK_ARM11) * 1000000) / SYSCLOCK_ARM11;
}
void apt_event(APT_HookType type, void*) {
    if (type == APTHOOK_ONSUSPEND || type == APTHOOK_ONSLEEP ||
        type == APTHOOK_ONRESTORE || type == APTHOOK_ONWAKEUP)
        __atomic_store_n(&interrupted, 1u, __ATOMIC_RELEASE);
}
void cell(char (&out)[8], uint32_t us) {
    // Integer-only formatting: no floating-point printf/dtoa heap path.
    const uint64_t tenths = (uint64_t(us) + 50) / 100;
    if (tenths > 99999) std::snprintf(out, sizeof out, " >9999");
    else std::snprintf(out, sizeof out, "%4u.%u", unsigned(tenths / 10), unsigned(tenths % 10));
}
void format() {
    const auto& s = ring.summary();
    std::snprintf(lines[0], Columns, "ms/3s    avg    p95    max");
    const char* names[] = {"prep ", "ui   ", "gpu  ", "wait ", "frame"};
    for (size_t m = 0; m < frame_profile::MetricCount; ++m) {
        char avg[8], p95[8], max[8];
        cell(avg, s.metric[m].avg); cell(p95, s.metric[m].p95); cell(max, s.metric[m].max);
        if (s.samples) std::snprintf(lines[m + 1], Columns, "%s %s %s %s", names[m], avg, p95, max);
        else std::snprintf(lines[m + 1], Columns, "%s     --     --     --", names[m]);
    }
    // Counts saturate on screen only; totals retain their full 64-bit value.
    std::snprintf(lines[6], Columns, "vb miss %u/%u",
        unsigned(std::min(s.missed_window, uint64_t(9999999))),
        unsigned(std::min(s.missed_total, uint64_t(9999999))));
    std::snprintf(lines[7], Columns, "n=%u  win/total  zr hide", unsigned(s.samples));
}
} // namespace

void init() {
    if (Available) aptHook(&hook, apt_event, nullptr);
}
void shutdown() {
    if (Available) aptUnhook(&hook);
}
void reset() {
    ring.reset();
    requested_us = 0;
    if (visible) format();
}
void input(uint32_t& down, uint32_t& held, uint32_t& up) {
    if (!Available) return;
#ifdef SEANCE_STARTUP_INPUT_DIAGNOSTIC
    // Read raw HID before the startup tail filter; reveal stuck bits even when
    // safely suppressed. Cached integer text, no SD writes or extra HID polls.
    std::snprintf(lines[8], Columns, "hid h:%08x d:%08x", unsigned(held), unsigned(down));
#endif
    if (down & KEY_ZR) {
        visible = !visible;
        reset();
    }
    // Reserve all three edges even while hidden; never also undo a patch or
    // cancel a local gesture merely because the profiler was toggled.
    down &= ~KEY_ZR;
    held &= ~KEY_ZR;
    up &= ~KEY_ZR;
}
void before_frame() {
    if (!Available || !visible) return;
    if (__atomic_exchange_n(&interrupted, 0u, __ATOMIC_ACQ_REL)) reset();
    requested_us = clock_us();
}
void after_begin() {
    if (!Available || !visible) return;
    const uint64_t now = clock_us();
    // Suspend can occur inside FrameBegin's wait too. Do not count lid/HOME
    // time as a multi-second UI stall, nor clobber Citro3D's vblank callbacks.
    if (__atomic_exchange_n(&interrupted, 0u, __ATOMIC_ACQ_REL)) {
        reset();
        requested_us = now;
    }
    const float gpu_ms = C3D_GetDrawingTime(); // previous queue, fenced now
    const uint32_t gpu_us = std::isfinite(gpu_ms) && gpu_ms > 0
        ? frame_profile::duration(uint64_t(std::min(double(gpu_ms) * 1000.0, 4294967295.0))) : 0;
    ring.begin(now, requested_us, C3D_FrameCounter(0), gpu_us);
    if (ring.refresh(now)) format();
}
void after_end() {
    if (Available && visible) ring.end(clock_us());
}
void draw(C3D_RenderTarget_tag* top) {
    if (!Available || !visible) return;
    // Revisit, do NOT clear: remains sharp above nav/reveal/modal blur on top.
    C2D_SceneBegin(top);
    constexpr int X = 230, Y = 4, W = 166, H = Rows * 9 + 6;
    const uint32_t bg = ui::rgb(12, 14, 18), fg = ui::rgb(232, 236, 240);
    ui::rect(X, Y, W, H, bg);
    for (int row = 0; row < Rows; ++row)
        ui::text(X + 4, Y + 3 + row * 9, lines[row], fg);
}
} // namespace seance::platform::debug_overlay
