// Frozen dual-screen GO TO popup with the approved two-column word tree.
// Clock drags remain drafts: one guarded setter on lift, not per pixel.
#include "nav_backdrop.h"
#include "nav_ferrofluid.h"
#include "ui_nav.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "menu_style.h"
#include "menu_labels_data.h"
#include "start_menu_ink.h"
#include "ui_palette.h"
#include "inspector_gothic.h"
#include "../../core/ui/draw.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {

namespace {
bool g_open = false;
int  g_cursor = 0;
int g_current = 0, g_transition_current = 0;
uint32_t g_master_serial = 0;
u64 g_master_seen = 0;
bool g_master_progress = false;
bool g_button_frame = false;
int g_clock_focus = -1; // -1 navigation, 0 tempo, 1 swing
bool g_clock_detail = false;
int g_detail_control = 0; // base, source, depth

struct NavItem { const char* label; const char* hint; NavChoice choice; };
const NavItem ITEMS[] = {
    { "patch table", "build",       NavChoice::Chains },
    { "sigils",      "draw",        NavChoice::Sigils },
    { "sigilbay",    "modulation",  NavChoice::Patchbay },
    { "sequencer",   "score",       NavChoice::Sequencer },
    { "perform",     "morph",       NavChoice::Performance },
    { "bounce",      "mix to loop", NavChoice::Bounce },
    { "patches",     "save / load", NavChoice::Patch },
    { "exit",        "hbmenu",      NavChoice::Exit },
};
constexpr int N_ITEMS = (int)(sizeof(ITEMS) / sizeof(ITEMS[0]));
constexpr int ROWS = 4, BTN_Y0 = 54, BTN_H = 34, BTN_SY = 42;
constexpr int COLUMN_X[] = {32, 192};
constexpr int CLOCK_Y = 4, CLOCK_H = 36;
int item_x(int index) { return COLUMN_X[index / ROWS]; }
int item_y(int index) { return BTN_Y0 + (index % ROWS) * BTN_SY; }
int current_item() {
    if (g_overlay == Overlay::PatchBrowser) return 6;
    switch (g_view) {
        case View::Sigils: return 1;
        case View::Patchbay: return 2;
        case View::Sequencer: return 3;
        case View::Performance: return 4;
        case View::Bounce: return 5;
        default: return 0;
    }
}


// Identity is controller-only; never dereferenced after retirement. No pointers
// or engine access in the frozen draw frame or renderer.
struct ClockDrag {
    Engine* engine = nullptr;
    platform::AudioOut* audio = nullptr;
    int bpm = 0, swing = 0;
    int x = 0, y = 0, draft = 0;
    int field = -1;
    int edit = 0; // base, depth, source
    ClockMod mods[2];
} g_clock_drag;
struct ClockFrame {
    int16_t bpm = 0, swing = 0, depth = 0;
    ClockInspector clocks[2];
    int8_t focus = -1, control = 0;
    ModSource source = ModSource::None;
    bool valid = false, pending = false, detail = false;
    uint8_t paper_alpha = 0; // frozen blur -> opaque editor paper, shared by both LCDs
} g_clock_frame;
ClockFrame g_transition_clock_frame;
int g_transition_cursor = 0;
bool g_transition_frame_valid = false;
static_assert(sizeof(ClockFrame) <= 96, "bounded pre-GPU clock frame");
static_assert(std::is_trivially_copyable<ClockFrame>::value, "pointer-free clock frame");

constexpr int kClockPaperMs = 180, kWordShiftMs = 120;
struct PaperMotion {
    u64 started = 0;
    int from = 0, alpha = 0;
    bool editor = false;
} g_paper_motion;
struct MenuMotionFrame {
    uint16_t ink_ms = 0; // captured phase, also retained by the closing handoff
    uint8_t shift[N_ITEMS] = {};
    int8_t pressed = -1;
} g_menu_frame, g_transition_menu_frame;
static_assert(sizeof(MenuMotionFrame) <= 12, "bounded native word feedback and ink phase");
static_assert(std::is_trivially_copyable<MenuMotionFrame>::value, "pointer-free outgoing ink");
u64 g_ink_last = 0;
bool g_ink_running = false;
uint8_t g_shift_from[N_ITEMS] = {};
u64 g_shift_started = 0;
int g_shift_target = -1;

int smooth_weight(u64 elapsed, int duration) {
    if (elapsed >= unsigned(duration)) return 255;
    const int64_t t = int(elapsed), d = duration;
    return int((255*t*t*(3*d-2*t) + d*d*d/2) / (d*d*d));
}
void capture_nav_ink(u64 now) {
    const bool running = g_clock_focus < 0 && !g_clock_detail;
    // Pause on both sides of the clock inspector; hidden/suspended time is not
    // an animation debt. Rollback and long capture gaps only rebase the clock.
    if (running && g_ink_running && now >= g_ink_last && now - g_ink_last <= 1000)
        g_menu_frame.ink_ms = uint16_t((g_menu_frame.ink_ms + (now - g_ink_last)) % cabinet_ink::kLoopMs);
    g_ink_last = now;
    g_ink_running = running;
}
void capture_menu_motion(u64 now, bool editor) {
    // Retarget from the last visible pose, never a freshly advanced/unshown one.
    // Draw uses only these frozen scalars; no input, engine, clock or blur work.
    auto& p = g_paper_motion;
    if (editor != p.editor || now < p.started) {
        p.from = p.alpha; p.started = now; p.editor = editor;
    }
    const int t = smooth_weight(now - p.started, kClockPaperMs);
    p.alpha = (p.from*(255-t) + (editor ? 255 : 0)*t + 127) / 255;
    g_clock_frame.paper_alpha = uint8_t(p.alpha);
    const int target = editor ? -1 : g_cursor;
    if (target != g_shift_target || now < g_shift_started) {
        std::copy(std::begin(g_menu_frame.shift), std::end(g_menu_frame.shift), g_shift_from);
        g_shift_target = target; g_shift_started = now;
    }
    const int shift = smooth_weight(now - g_shift_started, kWordShiftMs);
    for (int i = 0; i < N_ITEMS; ++i)
        g_menu_frame.shift[i] = uint8_t((g_shift_from[i]*(255-shift) + (i == target ? 255 : 0)*shift + 127) / 255);
}
void draw_clock_background(bool top, int alpha) {
    const int width = top ? 400 : 320;
    if (alpha == 255) { menu_style::paper(width); return; }
    nav_backdrop::draw(top); // same cached pair; also establishes the paper LCD crop
    if (alpha) menu_style::paper_rect(0, 0, width, 240, alpha);
}
struct ClockInk { u32 ink, ghost, rail, spot; };
ClockInk clock_ink(int paper_alpha) {
    namespace m = menu_style;
    const u32 light = ui::rgb(244,244,244);
    u32 accent = m::spot();
    const unsigned luma = ((accent & 255)*54 + ((accent >> 8)&255)*183 + ((accent >> 16)&255)*19) / 256;
    if (luma < 150) accent = theme_config::mix(accent, light, (170-luma)*255/(244-luma));
    return {theme_config::mix(light, m::ink(), paper_alpha),
            theme_config::mix(ui::rgb(184,180,192), m::ghost(), paper_alpha),
            theme_config::mix(ui::rgb(94,94,94), m::branch(), paper_alpha),
            theme_config::mix(accent, m::spot(), paper_alpha)};
}

int clamp(int value, int lo, int hi) { return value < lo ? lo : value > hi ? hi : value; }
int clock_limit(int field, int value) { return field == 0 ? clamp(value, 20, 300) : clamp(value, 50, 75); }
void cancel_clock_drag() { g_clock_drag = {}; }
bool has_clock_drag() { return g_clock_drag.field >= 0; }

// Requires the current audio guard. Both persisted values identify the draft;
// even an unrelated clock change invalidates a captured stroke.
bool same_mod(ClockMod a, ClockMod b) { return a.src == b.src && a.depth == b.depth; }
ClockTarget target(int field) { return field == 0 ? ClockTarget::Tempo : ClockTarget::Swing; }
const char* clock_unit(int field) { return field == 0 ? "bpm" : "%"; }
const char* depth_unit(int field) { return field == 0 ? "bpm" : "pp"; }
const char* source_name(ModSource src) {
    static const char* names[] = {"a", "b", "c", "d", "e", "f", "g", "h"};
    const int i=mod_source_sigil(src);return i>=0?names[i]:"none";
}
int depth_limit(int field, int depth) { const int m = field == 0 ? 280 : 25; return clamp(depth, -m, m); }
bool drag_matches() {
    return g_eng && g_audio && g_eng == g_clock_drag.engine &&
        g_audio == g_clock_drag.audio && g_eng->bpm == g_clock_drag.bpm &&
        g_eng->swing_percent == g_clock_drag.swing &&
        same_mod(g_eng->clock_mod(ClockTarget::Tempo), g_clock_drag.mods[0]) &&
        same_mod(g_eng->clock_mod(ClockTarget::Swing), g_clock_drag.mods[1]);
}
void begin_clock_drag(int field, int edit, int tx, int ty, int draft) {
    // Caller holds the audio guard. Only persistent context is compared, never
    // moving effective clock values, so modulation cannot cancel manual edits.
    g_clock_drag.engine = g_eng; g_clock_drag.audio = g_audio;
    g_clock_drag.bpm = g_eng->bpm; g_clock_drag.swing = g_eng->swing_percent;
    g_clock_drag.mods[0] = g_eng->clock_mod(ClockTarget::Tempo);
    g_clock_drag.mods[1] = g_eng->clock_mod(ClockTarget::Swing);
    g_clock_drag.x = tx; g_clock_drag.y = ty; g_clock_drag.field = field;
    g_clock_drag.edit = edit; g_clock_drag.draft = draft;
}
void open_clock_detail() {
    cancel_clock_drag(); g_clock_detail = true; g_detail_control = 0;
}
void back_clock_detail() { cancel_clock_drag(); g_clock_detail = false; }

bool set_clock_value(int field, int value) {
    // Guard is held by the caller; don't dirty or clear runtime on a no-op.
    value = clock_limit(field, value);
    const int before = field == 0 ? g_eng->bpm : g_eng->swing_percent;
    if (value == before) return false;
    if (field == 0) g_eng->set_bpm(value);
    else g_eng->set_swing(value);
    return true;
}

void nudge_clock(int delta) {
    if (!delta || !g_eng || !g_audio) return;
    bool changed = false;
    {
        platform::AudioOut::Guard guard(*g_audio);
        const int current = g_clock_focus == 0 ? g_eng->bpm : g_eng->swing_percent;
        changed = set_clock_value(g_clock_focus, current + delta);
    }
    if (changed) mark_project_mutated();
}
void nudge_clock_mod(int delta) {
    if (!delta || !g_eng || !g_audio) return;
    bool changed = false;
    {
        platform::AudioOut::Guard guard(*g_audio);
        ClockMod mod = g_eng->clock_mod(target(g_clock_focus));
        if (g_detail_control == 1) {
            const int at=mod_source_sigil(mod.src)+1;
            const int next=(at+(delta>0?1:NUM_SIGILS))%(NUM_SIGILS+1);
            mod.src=next?sigil_mod_source(next-1):ModSource::None;
        }
        else mod.depth = (int16_t)depth_limit(g_clock_focus, mod.depth + delta);
        changed = g_eng->set_clock_mod(target(g_clock_focus), mod.src, mod.depth);
    }
    if (changed) mark_project_mutated();
}
constexpr int SOURCE_LEFT[] = {86,114,139,164,189,214,239,264,289};
constexpr int SOURCE_RIGHT[] = {113,138,163,188,213,238,263,288,314};
int source_hit(int tx, int ty) {
    for (int i = 0; i <= NUM_SIGILS; ++i)
        if (hit(tx, ty, SOURCE_LEFT[i], 132, SOURCE_RIGHT[i] - SOURCE_LEFT[i], 30)) return i?int(sigil_mod_source(i-1)):0;
    return -1;
}
}

bool nav_is_open() { return g_open; }
void nav_open() {
    g_open = true;
    g_transition_frame_valid = false;
    g_button_frame = false;
    g_clock_focus = -1;
    g_clock_detail = false;
    g_detail_control = 0;
    g_current = current_item();
    g_cursor = g_current;
    const u64 now = osGetTime();
    g_paper_motion = {};
    g_paper_motion.started = now;
    g_menu_frame = {};
    g_ink_last = now;
    g_ink_running = true;
    std::fill(std::begin(g_shift_from), std::end(g_shift_from), 0);
    g_menu_frame.shift[g_cursor] = g_shift_from[g_cursor] = 255;
    g_shift_target = g_cursor; g_shift_started = now;
    g_master_serial = g_ui_snapshot.master_serial;
    g_master_seen = now;
    g_master_progress = false;
    nav_backdrop::request();
    nav_ferrofluid::reset(now);
    cancel_clock_drag();
    nav_capture();
}
void nav_close() {
    // A committed main-menu choice closes input immediately, but the transition
    // still needs the exact final GO TO pixels during its closing half.
    if (g_open && !g_clock_detail) {
        g_transition_clock_frame = g_clock_frame;
        g_transition_cursor = g_cursor;
        g_transition_current = g_current;
        g_transition_menu_frame = g_menu_frame;
        g_transition_frame_valid = true;
    }
    g_open = false;
    g_button_frame = false;
    g_clock_focus = -1;
    g_clock_detail = false;
    g_detail_control = 0;
    cancel_clock_drag();
    g_clock_frame = {};
}

void nav_buttons(u32 down, NavChoice& out) {
    out = NavChoice::None;
    // ZL captures without interrupting a stroke. Every other button owns touch.
    g_button_frame = (down & ~(KEY_TOUCH | KEY_ZL)) != 0;
    if (!g_open) return;
    const bool had_drag = has_clock_drag();
    if (g_button_frame) cancel_clock_drag();
    if (down & KEY_SELECT) { nav_close(); return; }
    if (down & KEY_B) {
        if (g_clock_detail) back_clock_detail();
        else if (g_clock_focus >= 0) g_clock_focus = -1;
        else nav_close();
        return;
    }
    if ((down & KEY_A) && g_clock_focus >= 0 && !g_clock_detail) { g_clock_focus = -1; return; }
    // Cancelling a touch must not also edit the persisted value on that edge.
    if (had_drag && g_button_frame) return;
    const u32 supported = KEY_TOUCH | KEY_ZL | KEY_A | KEY_B | KEY_SELECT |
        KEY_X | KEY_Y | KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN;
    if (down & ~supported) return; // modal START/shoulder edges are inert
    if (g_clock_detail) {
        if (down & (KEY_X | KEY_A)) { g_detail_control = (g_detail_control + 1) % 3; return; }
        int delta = 0;
        if (down & KEY_LEFT) --delta;
        if (down & KEY_RIGHT) ++delta;
        if (down & KEY_DOWN) delta -= g_detail_control == 1 ? 1 : 5;
        if (down & KEY_UP) delta += g_detail_control == 1 ? 1 : 5;
        if (g_detail_control == 0) nudge_clock(delta); else nudge_clock_mod(delta);
        return;
    }
    if (down & KEY_Y) {
        if (g_clock_focus >= 0) { if (g_eng && g_audio) open_clock_detail(); }
        return;
    }
    if (down & KEY_X) {
        if (g_eng && g_audio) g_clock_focus = g_clock_focus == 0 ? 1 : 0;
        return;
    }
    if (g_clock_focus >= 0) {
        int delta = 0;
        if (down & KEY_LEFT)  --delta;
        if (down & KEY_RIGHT) ++delta;
        if (down & KEY_DOWN)  delta -= 5;
        if (down & KEY_UP)    delta += 5;
        nudge_clock(delta);
        return;
    }
    const int vertical = int(bool(down & KEY_DOWN)) - int(bool(down & KEY_UP));
    const int horizontal = int(bool(down & KEY_RIGHT)) - int(bool(down & KEY_LEFT));
    if (vertical) g_cursor = (g_cursor / ROWS) * ROWS + (g_cursor % ROWS + ROWS + vertical) % ROWS;
    if (horizontal) g_cursor = (g_cursor + ROWS) % N_ITEMS;
    if (down & KEY_A) {
        // A/tap still commits now. The existing closing handoff carries the
        // one-pixel depression; never hold a destination hostage to animation.
        g_menu_frame.pressed = int8_t(g_cursor);
        out = ITEMS[g_cursor].choice; nav_close();
    }
}

void nav_touch_down(int tx, int ty, NavChoice& out) {
    out = NavChoice::None;
    if (!g_open || g_button_frame) return;
    cancel_clock_drag();
    if (g_clock_detail) {
        if (hit(tx, ty, 230, 218, 86, 22)) { back_clock_detail(); return; }
        if (!g_eng || !g_audio) return;
        platform::AudioOut::Guard guard(*g_audio);
        const int src = source_hit(tx, ty);
        const ClockMod mod = g_eng->clock_mod(target(g_clock_focus));
        if (src >= 0) {
            g_detail_control = 1; begin_clock_drag(g_clock_focus, 2, tx, ty, src);
        } else if (hit(tx, ty, 16, 48, 288, 79)) {
            g_detail_control = 0; begin_clock_drag(g_clock_focus, 0, tx, ty,
                g_clock_focus == 0 ? g_eng->bpm : g_eng->swing_percent);
        } else if (hit(tx, ty, 16, 165, 288, 32)) {
            g_detail_control = 2; begin_clock_drag(g_clock_focus, 1, tx, ty, mod.depth);
        }
        return;
    }
    if (g_clock_focus >= 0 && hit(tx, ty, 208, 218, 46, 22)) { open_clock_detail(); return; }
    for (int field = 0; field < 2; ++field) {
        if (!hit(tx, ty, COLUMN_X[field] - 12, CLOCK_Y, 140, CLOCK_H)) continue;
        if (!g_eng || !g_audio) return;
        platform::AudioOut::Guard guard(*g_audio);
        g_clock_focus = field;
        if (tx >= COLUMN_X[field] + 108 && g_eng->clock_mod(target(field)).src != ModSource::None) { open_clock_detail(); return; }
        begin_clock_drag(field, 0, tx, ty, field == 0 ? g_eng->bpm : g_eng->swing_percent);
        return;
    }
    for (int i = 0; i < N_ITEMS; ++i) {
        if (hit(tx, ty, item_x(i) - 12, item_y(i), 136, BTN_H)) {
            g_cursor = i;
            g_menu_frame.pressed = int8_t(i);
            out = ITEMS[i].choice;
            nav_close();
            return;
        }
    }
}

void nav_touch_hold(int tx, int ty) {
    if (!g_open || g_button_frame || !has_clock_drag()) return;
    if (!g_eng || !g_audio || ty - g_clock_drag.y >= 24 || g_clock_drag.y - ty >= 24) {
        cancel_clock_drag();
        return;
    }
    platform::AudioOut::Guard guard(*g_audio);
    if (!drag_matches()) { cancel_clock_drag(); return; }
    const int field = g_clock_drag.field;
    const int dx = clamp(tx, 0, 319) - g_clock_drag.x;
    if (g_clock_drag.edit == 2) {
        if (source_hit(tx, ty) != g_clock_drag.draft || dx >= 8 || dx <= -8 ||
            ty - g_clock_drag.y >= 8 || g_clock_drag.y - ty >= 8) cancel_clock_drag();
    } else if (g_clock_drag.edit == 1) {
        g_clock_drag.draft = depth_limit(field, g_clock_drag.mods[field].depth + dx / (field == 0 ? 2 : 4));
    } else {
        const int origin = field == 0 ? g_clock_drag.bpm : g_clock_drag.swing;
        g_clock_drag.draft = clock_limit(field, origin + dx / (field == 0 ? 2 : 4));
    }
}

void nav_touch_release() {
    if (!g_open || g_button_frame || !has_clock_drag()) { cancel_clock_drag(); return; }
    bool changed = false;
    if (g_eng && g_audio) {
        platform::AudioOut::Guard guard(*g_audio);
        if (drag_matches()) {
            const int field = g_clock_drag.field;
            if (g_clock_drag.edit == 0) changed = set_clock_value(field, g_clock_drag.draft);
            else {
                const ClockMod mod = g_clock_drag.mods[field];
                changed = g_eng->set_clock_mod(target(field),
                    g_clock_drag.edit == 2 ? (ModSource)g_clock_drag.draft : mod.src,
                    g_clock_drag.edit == 1 ? g_clock_drag.draft : mod.depth);
            }
        }
    }
    cancel_clock_drag();
    if (changed) mark_project_mutated();
}

void nav_capture() {
    g_clock_frame = {};
    if (!g_open) return;

    const u64 now = osGetTime();
    capture_nav_ink(now);
    capture_menu_motion(now, g_eng && g_audio && g_clock_focus >= 0);
    if (now < g_master_seen || now - g_master_seen > 1000) g_master_progress = false;
    if (g_ui_snapshot.master_serial != g_master_serial) {
        g_master_serial = g_ui_snapshot.master_serial;
        g_master_seen = now;
        g_master_progress = true;
    }
    const bool fresh = g_eng && g_audio && g_master_progress && now >= g_master_seen && now - g_master_seen <= 250;
    nav_ferrofluid::capture(now, g_ui_snapshot.master_peak, fresh, menu_style::spot());

    if (!g_eng || !g_audio) { cancel_clock_drag(); return; }
    {
        platform::AudioOut::Guard guard(*g_audio);
        if (has_clock_drag() && !drag_matches()) cancel_clock_drag();
        g_clock_frame.valid = true;
        g_clock_frame.bpm = (int16_t)clock_limit(0, g_eng->bpm);
        g_clock_frame.swing = (int16_t)clock_limit(1, g_eng->swing_percent);
        g_clock_frame.focus = (int8_t)g_clock_focus;
        g_clock_frame.detail = g_clock_detail;
        g_clock_frame.control = (int8_t)g_detail_control;
        for (int i = 0; i < 2; ++i) g_eng->capture_clock_inspector(target(i), g_clock_frame.clocks[i]);
        if (g_clock_focus >= 0) {
            g_clock_frame.source = g_clock_frame.clocks[g_clock_focus].mod.src;
            g_clock_frame.depth = g_clock_frame.clocks[g_clock_focus].mod.depth;
        }
        if (has_clock_drag()) {
            const int field = g_clock_drag.field;
            const int origin = g_clock_drag.edit == 2 ? int(g_clock_drag.mods[field].src) :
                g_clock_drag.edit == 1 ? g_clock_drag.mods[field].depth :
                field == 0 ? g_clock_drag.bpm : g_clock_drag.swing;
            g_clock_frame.pending = g_clock_drag.draft != origin;
            if (g_clock_drag.edit == 2) g_clock_frame.source = (ModSource)g_clock_drag.draft;
            else if (g_clock_drag.edit == 1) g_clock_frame.depth = (int16_t)g_clock_drag.draft;
            else if (field == 0) g_clock_frame.bpm = (int16_t)g_clock_drag.draft;
            else g_clock_frame.swing = (int16_t)g_clock_drag.draft;
        }
    }
}

namespace {
void clock_right(int x, int y, const char* text, u32 color) { ui::text(x - ui::text_w(text), y, text, color); }
void clock_number(int x, int baseline, const char* value, const char* unit, u32 color, u32 unit_color) {
    const auto r = inspector_gothic::draw_numeric(x, baseline, value, color, color);
    ui::text(r.end_x + 7, baseline - 6, unit, unit_color);
}
void clock_footer(int width, const char* left, const char* right, u32 color) {
    ui::text(20, 228, left, color);
    clock_right(width - 20, 228, right, color);
}
void q16_text(char* out, size_t size, int32_t value, bool sign = false) {
    const int tenths = value < 0 ? -int((-int64_t(value) * 10 + 32768) / 65536) :
        int((int64_t(value) * 10 + 32768) / 65536);
    const int a = tenths < 0 ? -tenths : tenths;
    std::snprintf(out, size, "%s%d.%d", tenths < 0 ? "-" : sign ? "+" : "", a / 10, a % 10);
}
void draw_clock_detail() {
    namespace m = menu_style;
    const auto colors = clock_ink(g_clock_frame.paper_alpha);
    const int field = g_clock_frame.focus;
    if (!g_clock_frame.valid || field < 0) { ui::text(24, 80, "clock unavailable", colors.ghost); return; }
    m::page_heading(field == 0 ? page_layout::Title::Tempo : page_layout::Title::Swing, colors.ink);
    clock_right(300, 24, "project clock", colors.ghost);
    const u32 base_ink = g_clock_frame.control == 0 ? colors.spot : colors.ink;
    ui::text(24, 57, "base", g_clock_frame.control == 0 ? colors.spot : colors.ghost);
    char text[48];
    std::snprintf(text, sizeof text, "%d", field == 0 ? g_clock_frame.bpm : g_clock_frame.swing);
    clock_number(24, 115, text, clock_unit(field), base_ink, colors.ghost);
    ui::rect(24, 123, 276, 1, colors.rail);
    ui::text(24, 143, "sigil", g_clock_frame.control == 1 ? colors.spot : colors.ghost);
    for (int i = 0; i <= NUM_SIGILS; ++i) {
        const ModSource source=i?sigil_mod_source(i-1):ModSource::None;
        const char* src = source_name(source);
        const int x = (SOURCE_LEFT[i]+SOURCE_RIGHT[i])/2 - ui::text_w(src) / 2;
        const bool selected = g_clock_frame.source == source;
        ui::text(x, 143, src, selected ? colors.spot : colors.ghost);
        if (selected) ui::rect(x - 2, 155, ui::text_w(src) + 3, 1, colors.spot);
    }
    ui::text(24, 178, "depth", g_clock_frame.control == 2 ? colors.spot : colors.ghost);
    std::snprintf(text, sizeof text, "%+d %s", g_clock_frame.depth, depth_unit(field));
    clock_right(300, 178, text, colors.spot);
    ui::rect(219, 190, 81, 1, colors.rail);
    const int base = field == 0 ? g_clock_frame.bpm : g_clock_frame.swing;
    const int depth = g_clock_frame.source == ModSource::None ? 0 :
        g_clock_frame.depth < 0 ? -g_clock_frame.depth : g_clock_frame.depth;
    std::snprintf(text, sizeof text, "range %d .. %d %s", clock_limit(field, base-depth),
        clock_limit(field, base+depth), clock_unit(field));
    ui::text(24, 207, text, colors.ghost);
    if (g_clock_frame.pending) clock_footer(320, "pending / lift apply", "b back", colors.ghost);
    else clock_footer(320, "x field / dpad edit", "b go to", colors.ghost);
}
}


// Historical entry-point names retained: the popup owns both LCDs; clock focus
// temporarily opens the existing applied/base inspector, not a destination preview.
bool nav_clock_top_active() { return g_open; }
void nav_gpu_frame() { if (g_open) nav_backdrop::gpu_frame(); }
void nav_remember_outputs() { nav_backdrop::remember_outputs(); }
void nav_clock_draw_top() {
    namespace m = menu_style;
    if (!g_open) return;
    if (!g_clock_frame.valid || g_clock_frame.focus < 0) {
        draw_clock_background(true, g_clock_frame.paper_alpha);
        nav_ferrofluid::draw();
        return;
    }
    draw_clock_background(true, g_clock_frame.paper_alpha);
    const auto colors = clock_ink(g_clock_frame.paper_alpha);
    const int field = g_clock_frame.focus;
    const ClockInspector& c = g_clock_frame.clocks[field];
    char text[64], number[24];
    m::page_heading(field == 0 ? page_layout::Title::Tempo : page_layout::Title::Swing, colors.ink);
    ui::text(page_layout::kLeft, 48, "project clock", colors.ghost);
    clock_right(400 - page_layout::kRightInset, 48, c.limited ? "target / limited" : "base + sigil", colors.ghost);
    ui::text(24, 80, "applied", colors.spot);
    q16_text(number, sizeof number, c.applied_q16);
    clock_number(24, 144, number, clock_unit(field), colors.ink, colors.ghost);
    std::snprintf(text, sizeof text, "base %d %s", c.base, clock_unit(field));
    ui::text(24, 164, text, colors.ghost);
    q16_text(number, sizeof number, c.applied_q16 - c.base * 65536, true);
    std::snprintf(text, sizeof text, "change %s %s", number, depth_unit(field));
    ui::text(24, 184, text, colors.spot);
    ui::text(224, 80, "modulation", colors.ghost);
    std::snprintf(text, sizeof text, "source  %s", source_name(c.mod.src));
    ui::text(224, 105, text, colors.ink);
    const int magnitude = c.source < 0 ? -int(c.source) : int(c.source);
    const int output = (magnitude * 100 + 16383) / 32767 * (c.source < 0 ? -1 : 1);
    if (c.mod.src == ModSource::None) std::snprintf(text, sizeof text, "output  --");
    else std::snprintf(text, sizeof text, "output  %+d%%", output);
    ui::text(224, 126, text, colors.ghost);
    std::snprintf(text, sizeof text, "depth   %+d %s", c.mod.depth, depth_unit(field));
    ui::text(224, 147, text, c.mod.src == ModSource::None ? colors.ghost : colors.ink);
    q16_text(number, sizeof number, c.target_q16);
    std::snprintf(text, sizeof text, "target  %s", number);
    ui::text(224, 184, text, colors.ghost);
    ui::text(24, 207, field == 1 ? "pp = percentage points" : "base stays fixed / clock moves", colors.ghost);
    clock_footer(400, g_clock_frame.pending ? "draft below / not applied" : "edit below", "applied / target", colors.ghost);
}

namespace {
// Exact equal-color span batching from the approved startup ink renderer. This
// is only command coalescing: shade math remains the shared cabinet_ink helper,
// and every native mask pixel is blended once, without new geometry or texture.
struct NavInkRows {
    struct Run { u32 color; uint8_t x, y, w, h; };
    Run previous[128], pending[128];
    int x, y, previous_count = 0, count = 0;
    NavInkRows(int xx, int yy) : x(xx), y(yy) {}
    void add(int xx, int w, u32 color) {
        if (color >> 24) pending[count++] = {color, uint8_t(xx), 0, uint8_t(w), 1};
    }
    void emit(const Run& r) const { ui::rect(x + r.x, y + r.y, r.w, r.h, r.color); }
    void finish(int row) {
        bool used[128]{}; int p = 0;
        for (int i = 0; i < count; ++i) {
            auto& r = pending[i]; r.y = uint8_t(row);
            while (p < previous_count && previous[p].x < r.x) ++p;
            if (p < previous_count && previous[p].x == r.x && previous[p].w == r.w && previous[p].color == r.color) {
                r.y = previous[p].y; r.h = uint8_t(previous[p].h + 1); used[p] = true;
            }
        }
        for (int i = 0; i < previous_count; ++i) if (!used[i]) emit(previous[i]);
        std::memcpy(previous, pending, sizeof(Run) * count); previous_count = count; count = 0;
    }
    void flush() const { for (int i = 0; i < previous_count; ++i) emit(previous[i]); }
};
void draw_nav_heading(int index, int x, int y, bool selected, u32 ink, u32 spot, uint16_t phase) {
    const menu_labels::Label* label = nullptr;
    for (const auto& candidate : menu_labels::labels)
        if (!std::strcmp(candidate.text, ITEMS[index].label)) { label = &candidate; break; }
    // Immutable current masks all fit. A future oversized/unknown label falls
    // back to the shared static heading rather than overflowing a span buffer.
    if (!label || label->width > 128 || label->height > 254) {
        menu_style::heading(x, y, ITEMS[index].label, selected ? spot : ink);
        return;
    }
    const int stride = (label->width + 7) / 8;
    const auto* bits = menu_labels::bitmap + label->offset;
    const auto on = [&](int xx, int yy) {
        return xx >= 0 && xx < label->width && yy >= 0 && yy < label->height &&
            (bits[yy * stride + xx / 8] & (1u << (7 - xx % 8)));
    };
    cabinet_ink::Clock clock; clock.ms = phase;
    NavInkRows rows(x, y);
    for (int row = 0; row < label->height; ++row) {
        int run = -1; u32 previous = 0;
        for (int col = 0; col <= label->width; ++col) {
            u32 color = 0;
            if (on(col, row)) {
                const bool edge = !(on(col-1, row) && on(col+1, row) && on(col, row-1) && on(col, row+1));
                // The approved preview cycles startup's three authored phases.
                // Its three pulse envelopes are never indexed by eight items;
                // no startup opening pulse or input wait is added to GO TO.
                const auto shade = cabinet_ink::heading(clock, index % 3, col, row, edge, selected ? 255 : 0);
                color = (theme_config::mix(ink, spot, shade.tint) & 0x00ffffffu) | (u32(shade.alpha) << 24);
            }
            if (run >= 0 && color != previous) { rows.add(run, col - run, previous); run = -1; }
            if (color && run < 0) run = col;
            previous = color;
        }
        rows.finish(row);
    }
    rows.flush();
}
void draw_nav_main(const ClockFrame& clock, int cursor, int current, const MenuMotionFrame& motion) {
    namespace m = menu_style;
    // Fade foreground palette roles with the background too: light themes must
    // not put dark ink on the still-dark blur during the first editor frames.
    const auto colors = clock_ink(clock.paper_alpha);
    const u32 ink = colors.ink, ghost = colors.ghost, rail = colors.rail, spot = colors.spot;
    for (int field = 0; field < 2; ++field) {
        const int x = COLUMN_X[field];
        const bool selected = clock.valid && clock.focus == field;
        char value[20];
        if (!clock.valid) std::snprintf(value, sizeof value, field == 0 ? "-- bpm" : "swing --");
        else std::snprintf(value, sizeof value, field == 0 ? "%d bpm" : "swing %d%%",
            field == 0 ? clock.bpm : clock.swing);
        ui::text(x, 17, value, selected ? spot : ink);
        ui::line(x, 30, x + (field == 0 ? 40 : 53), 30, selected ? spot : rail);
        if (clock.valid && clock.clocks[field].mod.src != ModSource::None)
            ui::text(x + 110, 17, source_name(clock.clocks[field].mod.src), spot);
    }
    for (int x : COLUMN_X) ui::line(x - 12, 69, x - 12, 195, rail);
    for (int i = 0; i < N_ITEMS; ++i) {
        const int x = item_x(i), y = item_y(i) + 4;
        const bool selected = clock.focus < 0 && i == cursor;
        const int width = m::heading_width(ITEMS[i].label);
        const int dx = (2 * motion.shift[i] + 127) / 255;
        const int dy = motion.pressed == i ? 1 : 0;
        draw_nav_heading(i, x + dx, y + dy, selected, ink, spot, motion.ink_ms);
        ui::line(x - 12, y + 11, x - 5, y + 11, selected ? spot : rail);
        if (selected) {
            ui::line(x - 9, y + 8, x - 5, y + 11, spot);
            ui::line(x - 5, y + 11, x - 9, y + 14, spot);
            ui::line(x + dx, y + 25 + dy, x + dx + width - 1, y + 25 + dy, spot);
        }
        // Current destination is NOT keyboard focus: this quiet dot stays put.
        if (i == current) ui::rect(x + width + 6, y + 10, 3, 3, ghost);
    }
    if (clock.pending) clock_footer(320, "pending / lift apply", "b cancel", ghost);
    else if (clock.focus >= 0) clock_footer(320, "dpad +/-1 / +/-5", "y mod / b back", ghost);
    else {
        ui::text(20,226,"a open",ghost);
        ui::text(104,226,"b / select close",ghost);
    }
}
}

void nav_draw() {
    if (!g_open) return;
    draw_clock_background(false, g_clock_frame.paper_alpha);
    if (g_clock_frame.detail) { draw_clock_detail(); return; }
    draw_nav_main(g_clock_frame, g_cursor, g_current, g_menu_frame);
}

void nav_draw_transition() {
    if (!g_transition_frame_valid) return;
    draw_clock_background(false, g_transition_clock_frame.paper_alpha);
    draw_nav_main(g_transition_clock_frame, g_transition_cursor, g_transition_current, g_transition_menu_frame);
}

void nav_transition_clear() {
    g_transition_frame_valid = false;
    g_transition_clock_frame = {};
}

} // namespace seance::platform::ui3ds
