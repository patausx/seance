#include "debug_overlay.h"
#include "audio_profile.h"
#include "frame_profile.h"
#include "../../core/ui/draw.h"
#include <3ds.h>
#include <citro2d.h>
#include <cstdio>
#include <cmath>

namespace seance::platform::debug_overlay {
namespace {
frame_profile::Ring ring;
// Audio side, gathered on this (UI) thread from audio_profile's lock-free storage.
// Unreferenced, and dropped by the compiler, in a normal build.
[[maybe_unused]] audio_profile::Blocks block_window;   // worker stages, one sample per block
[[maybe_unused]] audio_profile::Frames guard_window;   // UI-side guard waits, one sample per frame
[[maybe_unused]] uint32_t dropped_base = 0;
#ifdef SEANCE_STARTUP_INPUT_DIAGNOSTIC
// Temporary device troubleshooting build only; release visibility is unchanged.
bool visible = true;
#else
bool visible = false;
#endif
aptHookCookie hook{};
uint32_t interrupted = 0;
uint64_t requested_us = 0;
// Rows 0-6 frame cycle, 7-11 audio worker stages, 12 audio counters,
// 13-15 what else the worker saw (late blocks and queue, cores and transport,
// load), 16-17 UI side of the audio guard, 18 footer.
constexpr int AudioRow = 7, CountRow = 12, SeenRow = 13, GuardRow = 16, FooterRow = 18, Columns = 28;
#ifdef SEANCE_STARTUP_INPUT_DIAGNOSTIC
constexpr int HidRow = 19, Rows = 20;
#else
constexpr int Rows = 19;
#endif
// The 3 s window holds 192 samples at most, so every count below is three digits.
static_assert(frame_profile::Capacity < 1000, "window counts are printed in three columns");
static_assert(AudioRow + int(audio_profile::StageCount) == CountRow, "one row per worker stage");
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
void fine(char (&out)[8], uint32_t us) {
    // Audio stages are short: hundredths of a millisecond in the same six columns.
    const uint64_t hundredths = (uint64_t(us) + 5) / 10;
    if (hundredths > 99999) std::snprintf(out, sizeof out, ">=1000");
    else std::snprintf(out, sizeof out, "%3u.%02u", unsigned(hundredths / 100), unsigned(hundredths % 100));
}
void whole(char (&out)[8], uint32_t count) {
    std::snprintf(out, sizeof out, "%6u", unsigned(std::min(count, uint32_t(999999))));
}
// One row: name, then avg / p95 / max through `print`, or dashes. The precisions
// state the real widths (5 + 3 x 7 = 26 characters), so the bound is provable.
template <typename Print>
void row(int index, const char* name, bool has, const frame_profile::Stats& stats, Print print) {
    char avg[8], p95[8], max[8];
    print(avg, stats.avg); print(p95, stats.p95); print(max, stats.max);
    if (has) std::snprintf(lines[index], Columns, "%.5s %.6s %.6s %.6s", name, avg, p95, max);
    else std::snprintf(lines[index], Columns, "%.5s     --     --     --", name);
}
void collect(uint64_t now) {
    // if constexpr: a normal build never references the shared audio storage.
    if constexpr (audio_profile::Enabled) {
        constexpr uint32_t TicksPerUs = uint32_t(SYSCLOCK_ARM11 / 1000000u);   // as the worker's own meter
        audio_profile::Block block;
        // Bounded per frame even while the worker keeps producing.
        for (uint32_t n = 0; n < audio_profile::Feed::Capacity && audio_profile::feed.pop(block); ++n)
            audio_profile::add_block(block_window, now, block.lock / TicksPerUs, block.dsp / TicksPerUs,
                                     block.snap / TicksPerUs, block.out / TicksPerUs, block.xruns,
                                     {block.period / TicksPerUs, block.queue, block.state, block.units, block.voices});
        // Everything AudioOut::lock() callers waited since the previous fenced frame.
        uint32_t ticks = 0, entries = 0;
        audio_profile::guard_wait.take(ticks, entries);
        const uint32_t frame[audio_profile::GuardCount] = {ticks / TicksPerUs, entries};
        guard_window.add(now, frame);
    }
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
    std::snprintf(lines[FooterRow], Columns, "n=%u  win/total  zr hide", unsigned(s.samples));
    if constexpr (audio_profile::Enabled) {
        // Per 512-frame block (16 ms budget): the worker waiting for the engine
        // guard, render, snapshot capture, hand-off to NDSP and their per-block
        // sum. Real worker stamps handed over lock-free; nothing here is estimated.
        const auto& a = block_window.summary();
        const char* stages[audio_profile::StageCount] = {"lock ", "dsp  ", "snap ", "out  ", "block"};
        for (size_t m = 0; m < audio_profile::StageCount; ++m)
            row(AudioRow + int(m), stages[m], a.samples, a.metric[m], fine);
        // xrun window/total, blocks in the window, blocks this thread never collected.
        std::snprintf(lines[CountRow], Columns, "xrun %u/%u n=%u d=%u",
            unsigned(std::min(a.counter_window, uint32_t(999))), unsigned(std::min(a.counter_total, uint32_t(99999))),
            unsigned(a.samples), unsigned(std::min(audio_profile::feed.dropped() - dropped_base, uint32_t(999))));
        // The same blocks, read for what the stage times cannot say: how many took
        // longer than the 16 ms they carry, the fewest other buffers NDSP still held
        // at a hand-off (0 = it ran dry) of the queue depth in use, and how many
        // blocks a second the worker really produced (62.5 keeps up).
        const audio_profile::Observed o = audio_profile::observe(block_window);
        if (o.blocks) {
            // Every number is clamped to its printed width: the bound stays provable.
            const unsigned late = unsigned(std::min(o.late, uint32_t(999)));
            const unsigned held = std::min(unsigned(o.queued_min), 9u), depth = std::min(unsigned(o.depth), 9u);
            const unsigned tenths = unsigned(std::min(o.rate_x10, uint32_t(9999)));
            if (tenths) std::snprintf(lines[SeenRow], Columns, "late %u  q %u/%u  %u.%u/s", late, held, depth,
                                      tenths / 10 % 1000, tenths % 10);
            else std::snprintf(lines[SeenRow], Columns, "late %u  q %u/%u  --/s", late, held, depth);
            // Core of the worker / of this thread, then the transport and the
            // sequencer as the newest block's own snapshot published them.
            const unsigned core = o.last.state >> audio_profile::CoreShift;
            const int here = svcGetProcessorID();
            std::snprintf(lines[SeenRow + 1], Columns, "core %c/%c  %s  seq %s",
                core < 10 ? char('0' + core) : '?', here >= 0 && here < 10 ? char('0' + here) : '?',
                (o.last.state & audio_profile::Playing) ? "play" : "stop",
                (o.last.state & audio_profile::Sequencing) ? "on" : "off");
            // Sounding cards, then GRAN heads + RULE voices: newest block / most in the window.
            std::snprintf(lines[SeenRow + 2], Columns, "units %u  voices %u/%u", unsigned(o.last.units),
                unsigned(o.last.voices), unsigned(o.voices_max));
        } else {
            std::snprintf(lines[SeenRow], Columns, "late --  q -/-  --/s");
            std::snprintf(lines[SeenRow + 1], Columns, "core -/-  --  seq --");
            std::snprintf(lines[SeenRow + 2], Columns, "units --  voices --/--");
        }
        // The other side of the same guard, per UI frame: ms spent waiting to
        // enter it, and how many times it was entered (nested entries included).
        const auto& g = guard_window.summary();
        row(GuardRow, "uilk ", g.samples, g.metric[audio_profile::Waited], fine);
        row(GuardRow + 1, "uin  ", g.samples, g.metric[audio_profile::Taken], whole);
    }
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
    if constexpr (audio_profile::Enabled) {
        // Blocks queued and guard waits accumulated while hidden or across a
        // suspend are not this window's.
        block_window.reset();
        guard_window.reset();
        audio_profile::Block stale;
        for (uint32_t n = 0; n < audio_profile::Feed::Capacity && audio_profile::feed.pop(stale); ++n) {}
        dropped_base = audio_profile::feed.dropped();
        uint32_t ticks = 0, entries = 0;
        audio_profile::guard_wait.take(ticks, entries);
    }
    if (visible) format();
}
void input(uint32_t& down, uint32_t& held, uint32_t& up) {
    if (!Available) return;
#ifdef SEANCE_STARTUP_INPUT_DIAGNOSTIC
    // Read raw HID before the startup tail filter; reveal stuck bits even when
    // safely suppressed. Cached integer text, no SD writes or extra HID polls.
    std::snprintf(lines[HidRow], Columns, "hid h:%08x d:%08x", unsigned(held), unsigned(down));
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
    collect(now);
    if (ring.refresh(now)) {
        if constexpr (audio_profile::Enabled) {
            block_window.refresh(now);
            guard_window.refresh(now);
        }
        format();
    }
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
