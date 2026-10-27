#include "ui_picker.h"
#include "ui_state.h"
#include "ui_chains.h"
#include "ui_nav.h"
#include "ui_palette.h"
#include "picker_motion.h"
#include "picker_scroll.h"
#include "picker_catalogue_data.h"
#include "menu_style.h"
#include "ui_module_focus_internal.h"
#include "audio_out.h"
#include "../../core/ui/draw.h"
#include "../../core/ui/font.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds {
namespace {
namespace cat = picker_catalogue;
constexpr int ROW_Y = 46, ROW_H = 27, VISIBLE = 6, DRAG_THRESHOLD = 5;
constexpr int NONE = -1, CLOSE = -2, SCROLL = -3, FILTER = cat::count;

struct Session { UnitKind focus = UnitKind::Empty; float offset = 0; };
Session g_sessions[cat::filter_count];
int g_filter = 0, g_filter_cursor = 0, g_cursor = 0, g_start = 0, g_count = 0;
int g_view_entries[cat::count];
bool g_filter_column = false, g_button_frame = false;
picker_scroll::State g_scroll;
struct Gesture {
    bool active = false, moved = false, caught = false;
    int target = NONE, x = 0, y = 0;
    float offset = 0;
} g_touch;
// Compare addresses only, never dereference an old pointer. Audio/controller
// access is guarded; the draw DTO contains only static catalogue pointers.
struct Owner {
    Engine* engine = nullptr;
    platform::AudioOut* audio = nullptr;
    View view = View::Chains;
    int chain = -1, slot = -1, sel_chain = -1, sel_slot = -1;
    Unit* units[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    UnitKind kinds[NUM_CHAINS][UNITS_PER_CHAIN] = {};
    bool valid = false, replace = false;
} g_owner;
struct Frame {
    bool valid = false, replace = false, filter_column = false;
    int chain = 0, slot = 0, filter = 0, filter_cursor = 0;
    int count = 0, start = 0, cursor = -1, offset = 0, node_count = 0, marked = -1;
    const cat::Entry* candidate = nullptr;
    const cat::Entry* rows[VISIBLE + 1] = {};
    const char* nodes[UNITS_PER_CHAIN + 1] = {};
} g_frame;
static_assert(std::is_trivially_copyable<Frame>::value && sizeof(Frame) <= 512,
              "picker draw snapshot stays bounded and contains no owned resources");

const cat::Entry* entry_at(int index) {
    return index >= 0 && index < g_count ? &cat::entries[g_view_entries[index]] : nullptr;
}
int last_start() { return std::max(0, g_count - VISIBLE); }
float scroll_limit() { return float(last_start() * ROW_H); }
void remember() {
    const auto* entry = entry_at(g_cursor);
    g_sessions[g_filter] = {entry ? entry->kind : UnitKind::Empty, g_scroll.position};
}
void sync_scroll() {
    g_start = int(g_scroll.position) / ROW_H;
    if (g_count) g_cursor = std::clamp(g_cursor, g_start, std::min(g_count - 1, g_start + VISIBLE - 1));
    remember();
}
void reveal() {
    if (g_cursor < g_start) g_start = g_cursor;
    if (g_cursor >= g_start + VISIBLE) g_start = g_cursor - VISIBLE + 1;
    g_start = std::clamp(g_start, 0, last_start());
    g_scroll.set(float(g_start * ROW_H), scroll_limit(), osGetTime());
    remember();
}
void choose_filter(int filter) {
    if (filter < 0 || filter >= cat::filter_count) return;
    remember();
    g_filter = g_filter_cursor = filter;
    g_count = 0;
    for (int i = 0; i < cat::count; ++i)
        if (filter == 0 || int(cat::entries[i].group) == filter)
            g_view_entries[g_count++] = i;
    const auto saved = g_sessions[filter];
    g_cursor = 0;
    for (int i = 0; i < g_count; ++i)
        if (entry_at(i)->kind == saved.focus) g_cursor = i;
    g_scroll.set(saved.offset, scroll_limit(), osGetTime());
    g_start = int(g_scroll.position) / ROW_H;
    if (g_cursor < g_start || g_cursor >= g_start + VISIBLE) reveal();
    else remember();
}
bool shallow_owner() {
    return g_owner.valid && g_eng == g_owner.engine && g_audio == g_owner.audio &&
        g_eng && g_audio && g_view == g_owner.view && g_edit_chain == g_owner.chain &&
        g_edit_slot == g_owner.slot && g_sel_chain == g_owner.sel_chain && g_sel_slot == g_owner.sel_slot;
}
bool guarded_owner() {
    if (!shallow_owner()) return false;
    for (int c = 0; c < NUM_CHAINS; ++c) for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
        const auto* unit = g_eng->chains[c].units[s];
        if (unit != g_owner.units[c][s] || (unit && unit->kind != g_owner.kinds[c][s])) return false;
    }
    return true;
}
void invalidate() {
    picker_cancel_input();
    g_owner.valid = g_frame.valid = false;
    picker_motion::reset();
    if (g_overlay == Overlay::Picker) g_overlay = Overlay::None;
}
bool validate_owner() {
    if (g_overlay != Overlay::Picker || !shallow_owner()) { invalidate(); return false; }
    bool valid;
    { platform::AudioOut::Guard guard(*g_audio); valid = guarded_owner(); }
    if (!valid) invalidate();
    return valid;
}
bool advance() {
    // Navigation temporarily owns both screens. Do not destroy the still-open
    // picker/session when its frozen surface cache is lent to that modal.
    if (nav_is_open()) { picker_cancel_input(); return false; }
    if (!validate_owner()) return false;
    const auto now = osGetTime();
    if (picker_motion::sample(now)) { invalidate(); return false; }
    if (!g_touch.active && !picker_motion::state.closing) {
        g_scroll.sample(scroll_limit(), now);
        sync_scroll();
    }
    return true;
}
void request_close() {
    picker_cancel_input();
    picker_motion::close(osGetTime());
}
void publish() {
    g_frame.filter = g_filter;
    g_frame.filter_cursor = g_filter_cursor;
    g_frame.filter_column = g_filter_column;
    g_frame.count = g_count;
    g_frame.start = g_start;
    g_frame.offset = int(std::lround(g_scroll.position)) - g_start * ROW_H;
    g_frame.cursor = g_count ? g_cursor - g_start : -1;
    g_frame.candidate = entry_at(g_cursor);
    for (int i = 0; i <= VISIBLE; ++i) g_frame.rows[i] = entry_at(g_start + i);
}
void place(const cat::Entry* entry) {
    if (!entry || !validate_owner() || picker_motion::state.closing) return;
    {platform::AudioOut::Guard guard(*g_audio);if(g_eng->recording_active_or_pending()){
        module_focus_show_sample_result(trackr::synth::SampleEditResult::Busy);return;}}
    Unit* unit = make_unit(entry->kind); // ONLY commit creates a module.
    if (!unit) return;
    if (unit->kind == UnitKind::SamplePlayer)
        static_cast<PlayUnit*>(unit)->set_pitch_mode(PlayUnit::PitchMode::Normal);
    RecWorkspacePlan plan;
    auto result = trackr::synth::SampleEditResult::Ok;
    bool owned = true;
    if (unit->kind == UnitKind::Recorder) {
        {
            platform::AudioOut::Guard guard(*g_audio);
            owned = guarded_owner();
            if (owned) result = g_eng->plan_recorder(*static_cast<RecUnit*>(unit), true, plan);
        }
        if (owned && result == trackr::synth::SampleEditResult::Ok) result = Engine::stage_recorder(plan);
    }
    Unit* displaced = nullptr;
    bool installed = false;
    if (owned && result == trackr::synth::SampleEditResult::Ok && shallow_owner()) {
        platform::AudioOut::Guard guard(*g_audio);
        owned = guarded_owner(); // Revalidate AFTER allocating/staging, before any bank mutation.
        if (owned) {
            if (g_eng->recording_active_or_pending()) result = trackr::synth::SampleEditResult::Busy;
            else if (unit->kind == UnitKind::Recorder) result = g_eng->commit_recorder(plan);
            if (result == trackr::synth::SampleEditResult::Ok) {
                const int chain = g_owner.chain, slot = g_owner.slot;
                displaced = g_eng->replace_unit(chain, slot, unit);
                if (!g_owner.replace) {
                    // Free physical slot is staging, not audible insertion order.
                    // Engine remaps locks/routes; workspace remaps UI identities.
                    const int final_slot = g_eng->move_unit_before(chain, slot, UNITS_PER_CHAIN);
                    if (final_slot >= 0) {
                        workspace_move_slot(chain, slot, final_slot);
                        g_sel_slot = g_edit_slot = final_slot;
                    }
                }
                mark_project_structural_mutated();
                installed = true;
            }
        }
    } else if (!shallow_owner()) owned = false;
    delete displaced;
    if (!installed) delete unit;
    if (!owned) { invalidate(); return; }
    module_focus_show_sample_result(result);
    if (!installed) return; // Failed REC staging leaves catalogue/chain/bank intact.
    // Commit does NOT collapse into a stale pre-edit chain. Cancel alone reverses.
    invalidate();
}
int row_at(int y) { return y >= ROW_Y && y < ROW_Y + VISIBLE * ROW_H ? (y - ROW_Y) / ROW_H : NONE; }
bool list_hit(int x, int y) { return x >= 108 && x < 276 && row_at(y) >= 0; }
bool scroll_hit(int x, int y) { return g_count > VISIBLE && x >= 276 && x < 308 && row_at(y) >= 0; }
bool close_hit(int x, int y) { return x >= 246 && x < 312 && y >= 216 && y < 240; }
int hit(int x, int y) {
    if (close_hit(x, y)) return CLOSE;
    if (scroll_hit(x, y)) return SCROLL;
    const int row = row_at(y);
    if (row < 0) return NONE;
    if (x >= 26 && x < 108 && row < cat::filter_count) return FILTER + row;
    const int index = (y - ROW_Y + int(std::lround(g_scroll.position))) / ROW_H;
    if (list_hit(x, y) && index < g_count) return index;
    return NONE;
}
void scrollbar(int y) {
    g_scroll.set(float(std::clamp(y, 55, 200) - 55) * scroll_limit() / 145.f, scroll_limit(), osGetTime());
    sync_scroll();
}

struct Clip { int left, top, right, bottom; };
float smooth(float v) { v = std::clamp(v, 0.0f, 1.0f); return v * v * (3 - 2 * v); }
u32 opacity(u32 color, float amount) { return (color & 0x00ffffffu) | (u32(float(color >> 24) * amount + .5f) << 24); }
void rectangle(const Clip& clip, int x, int y, int w, int h, u32 color) {
    const int right = std::min(x + w, clip.right), bottom = std::min(y + h, clip.bottom);
    x = std::max(x, clip.left); y = std::max(y, clip.top);
    if (right > x && bottom > y && (color >> 24)) ui::rect(x, y, right - x, bottom - y, color);
}
void native(const Clip& clip, int x, int y, const char* text, u32 color, int align = 0) {
    if (!text || !(color >> 24)) return;
    const int width = int(std::strlen(text)) * 6;
    x -= align == 1 ? width : align == 2 ? width / 2 : 0;
    if (x >= clip.left && x + width <= clip.right && y >= clip.top && y + 8 <= clip.bottom) {
        ui::text(x, y, text, color); return;
    }
    for (int i = 0; text[i]; ++i) for (int row = 0; row < 8; ++row) {
        const auto bits = trackr::ui::glyph_row(text[i], row);
        for (int col = 0; col < 6;) {
            if (!(bits & (1 << (5-col)))) { ++col; continue; }
            const int start = col++;
            while (col < 6 && (bits & (1 << (5-col)))) ++col;
            rectangle(clip, x + i * 6 + start, y + row, col - start, 1, color);
        }
    }
}
void gothic(const Clip& clip, int x, int baseline, const cat::Label& label, u32 color) {
    if (!(color >> 24)) return;
    const int stride = (label.width + 7) / 8;
    for (int row = 0; row < label.height; ++row) {
        const int y = baseline + label.dy + row;
        if (y < clip.top || y >= clip.bottom) continue;
        const auto* bits = &cat::bitmap[label.offset + row * stride];
        for (int col = 0; col < label.width;) {
            if (!(bits[col / 8] & (1 << (7 - col % 8)))) { ++col; continue; }
            const int start = col++;
            while (col < label.width && (bits[col / 8] & (1 << (7 - col % 8)))) ++col;
            rectangle(clip, x + start, y, col - start, 1, color);
        }
    }
}
} // namespace

void picker_open() {
    picker_cancel_input();
    g_button_frame = false;
    g_owner = {}; g_frame = {};
    // Freeze pre-open active-column geometry BEFORE overlay or selection changes.
    chains_picker_begin();
    picker_motion::begin(osGetTime());
    if (!g_eng || !g_audio || g_edit_chain < 0 || g_edit_chain >= NUM_CHAINS ||
        g_edit_slot < 0 || g_edit_slot >= UNITS_PER_CHAIN) return;
    {
        platform::AudioOut::Guard guard(*g_audio);
        g_owner.engine = g_eng; g_owner.audio = g_audio; g_owner.view = g_view;
        g_owner.chain = g_edit_chain; g_owner.slot = g_edit_slot;
        g_owner.sel_chain = g_sel_chain; g_owner.sel_slot = g_sel_slot;
        for (int c = 0; c < NUM_CHAINS; ++c) for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
            const auto* u = g_eng->chains[c].units[s];
            g_owner.units[c][s] = g_eng->chains[c].units[s];
            g_owner.kinds[c][s] = u ? u->kind : UnitKind::Empty;
        }
        g_owner.replace = g_owner.units[g_edit_chain][g_edit_slot] != nullptr;
        g_owner.valid = g_frame.valid = true;
        g_frame.chain = g_edit_chain; g_frame.slot = g_edit_slot; g_frame.replace = g_owner.replace;
        for (int s = 0; s < UNITS_PER_CHAIN; ++s) if (g_owner.units[g_edit_chain][s]) {
            const int at = g_frame.node_count++;
            g_frame.nodes[at] = menu_style::module_name(g_owner.kinds[g_edit_chain][s]);
            if (s == g_edit_slot) g_frame.marked = at;
        }
        if (!g_owner.replace) {
            g_frame.marked = g_frame.node_count++;
            g_frame.nodes[g_frame.marked] = "+";
        }
    }
    choose_filter(g_filter);
    publish();
}
void picker_cancel_input() { g_touch = {}; g_scroll.stop(osGetTime()); }
void picker_capture() {
    if (g_overlay != Overlay::Picker) { invalidate(); return; }
    if (advance()) publish();
}
void picker_buttons(u32 down) {
    g_button_frame = (down & ~u32(KEY_TOUCH)) != 0;
    if (!advance() || !g_button_frame) return;
    picker_cancel_input();
    if (picker_motion::state.closing) return;
    if (down & KEY_B) { request_close(); return; }
    const bool opening = picker_motion::state.progress < 1.0f;
    if (down & (KEY_A | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) picker_motion::snap();
    if (opening && (down & KEY_A)) return; // A first reveals, never invisibly commits.
    if (down & KEY_LEFT) { g_filter_column = true; g_filter_cursor = g_filter; }
    if (down & KEY_RIGHT) g_filter_column = false;
    const int delta = (down & KEY_DOWN) ? 1 : (down & KEY_UP) ? -1 : 0;
    if (g_filter_column) {
        g_filter_cursor = (g_filter_cursor + delta + cat::filter_count) % cat::filter_count;
        if (down & KEY_A) choose_filter(g_filter_cursor);
    } else if (g_count) {
        g_cursor = (g_cursor + delta + g_count) % g_count;
        reveal();
        if (down & KEY_A) place(entry_at(g_cursor));
    }
    publish();
}
void picker_touch_down(int x, int y) {
    if (g_button_frame || g_touch.active || !advance() || picker_motion::state.closing) return;
    const bool caught = g_scroll.moving();
    picker_cancel_input();
    picker_motion::snap(); // Final geometry BEFORE hit testing a fresh physical down.
    const int target = hit(x, y);
    if (target == NONE) return;
    g_touch = {true, target == SCROLL, caught && target >= 0 && target < FILTER, target, x, y, g_scroll.position};
    if (target == SCROLL) { g_filter_column = false; scrollbar(y); }
    else if (target >= FILTER) { g_filter_column = true; g_filter_cursor = target - FILTER; }
    else if (target >= 0 && !g_touch.caught) { g_filter_column = false; g_cursor = target; remember(); }
    publish();
}
void picker_touch_hold(int x, int y) {
    if (!g_touch.active || !advance() || picker_motion::state.closing) return;
    const int dx = x - g_touch.x, delta = g_touch.y - y;
    const bool horizontal = dx >= DRAG_THRESHOLD || dx <= -DRAG_THRESHOLD;
    const bool vertical = delta >= DRAG_THRESHOLD || delta <= -DRAG_THRESHOLD;
    if (g_touch.target == SCROLL) {
        if (!scroll_hit(x, y)) { picker_cancel_input(); return; }
        scrollbar(y);
    } else if (g_touch.target == CLOSE || g_touch.target >= FILTER) {
        if (horizontal || vertical || hit(x, y) != g_touch.target) picker_cancel_input();
    } else {
        if (x < 108 || x >= 276 || y < 0 || y >= 240 ||
            (horizontal && !vertical && !g_touch.moved)) { picker_cancel_input(); return; }
        if (vertical) { g_touch.moved = true; g_touch.target = NONE; }
        if (g_touch.moved) {
            g_scroll.drag(g_touch.offset + float(delta), scroll_limit(), osGetTime());
            sync_scroll();
        } else if (!list_hit(x, y) || hit(x, y) != g_touch.target) g_touch.target = NONE; // sticky boundary crossing
    }
    publish();
}
void picker_touch_release() {
    if (!g_touch.active) return;
    const auto gesture = g_touch;
    const int target = gesture.moved || gesture.caught ? NONE : gesture.target;
    g_touch = {};
    // Only a physical list lift can launch inertia. Every cancellation stops it.
    if (gesture.moved && gesture.target != SCROLL) g_scroll.release(osGetTime());
    else g_scroll.stop(osGetTime());
    if (g_button_frame || !advance() || picker_motion::state.closing) return;
    if (target == CLOSE) request_close();
    else if (target >= FILTER) choose_filter(target - FILTER);
    else if (target >= 0) place(entry_at(target));
    publish();
}

void picker_draw_content(bool top, int left, int upper, int right, int bottom, float progress) {
    if (!g_frame.valid || progress <= 0) return;
    namespace m = menu_style;
    const float a = smooth((progress - .38f) / .42f);
    const u32 ink = opacity(m::ink(), a), spot = opacity(m::spot(), a), muted = opacity(m::ghost(), a);
    const Clip clip{left + 5, top ? upper + 4 : upper, right - 4, top ? 240 : bottom + 1};
    char text[64];
    if (top) {
        const auto* entry = g_frame.candidate;
        if (entry) {
            std::snprintf(text, sizeof text, "category / %s", cat::filters[int(entry->group)]);
            native(clip, 76, 26, text, muted);
            native(clip, 326, 26, "built-in", muted, 1);
            gothic(clip, 76, 92, entry->title, spot);
            native(clip, 76, 115, entry->description[0], ink);
            native(clip, 76, 129, entry->description[1], ink);
        } else {
            native(clip, 76, 26, "catalogue / empty", muted);
            gothic(clip, 76, 92, cat::empty_title, muted);
            native(clip, 76, 115, "choose another filter below.", ink);
        }
        if (g_frame.replace) std::snprintf(text, sizeof text, "ch%02d / replace %02d %s", g_frame.chain + 1,
            g_frame.marked + 1, g_frame.nodes[g_frame.marked]);
        else std::snprintf(text, sizeof text, "ch%02d / add at chain end", g_frame.chain + 1);
        native(clip, 76, 164, text, muted);
        const int pitch = std::min(64, 212 / std::max(1, g_frame.node_count - 1));
        const int first = 200 - (g_frame.node_count - 1) * pitch / 2;
        for (int i = 0; i < g_frame.node_count; ++i) {
            const int x = first + i * pitch;
            const u32 color = i == g_frame.marked ? spot : muted;
            // UI numbers describe heard chain order. Physical storage indices
            // remain in the guarded Owner and are never inferred from a name.
            std::snprintf(text, sizeof text, "%02d", i + 1);
            native(clip, x, 185, text, color, 2);
            native(clip, x, 201, g_frame.nodes[i], color, 2);
            if (i == g_frame.marked) rectangle(clip, x - 18, 215, 37, 1, spot);
            if (i + 1 < g_frame.node_count) native(clip, x + pitch / 2, 201, ">", muted, 2);
        }
        return;
    }
    const u32 header = opacity(m::ghost(), smooth((progress - .20f) / .20f));
    std::snprintf(text, sizeof text, "ch%02d / %s", g_frame.chain + 1, g_frame.replace ? "replace" : "add");
    native(clip, 34 + left - 20, 16, text, header);
    native(clip, 286 + right - 299, 16, g_frame.replace ? "selected slot" : "at chain end", header, 1);
    for (int i = 0; i < cat::filter_count; ++i) {
        const int y = ROW_Y + i * ROW_H;
        const bool active = i == g_frame.filter;
        const bool focused = g_frame.filter_column && i == g_frame.filter_cursor;
        native(clip, 36, y + 9, cat::filters[i], active || focused ? spot : muted);
        if (active) rectangle(clip, 36, y + 20, int(std::strlen(cat::filters[i])) * 6 - 1, 1, spot);
    }
    if (!g_frame.count) {
        native(clip, 120, 82, "no modules", muted);
        native(clip, 120, 100, "choose a filter", muted);
    }
    const Clip list_clip{std::max(clip.left, 108), std::max(clip.top, ROW_Y),
                         std::min(clip.right, 276), std::min(clip.bottom, ROW_Y + VISIBLE * ROW_H)};
    for (int row = 0; row <= VISIBLE; ++row) {
        const auto* entry = g_frame.rows[row];
        if (!entry) continue;
        const int y = ROW_Y + row * ROW_H - g_frame.offset;
        const bool selected = row == g_frame.cursor;
        if (selected) { // exact seven-row tiny study diamond
            constexpr int widths[] = {1, 3, 3, 5, 3, 3, 1};
            for (int i = 0; i < 7; ++i) rectangle(list_clip, 110 - widths[i] / 2, y + 10 + i, widths[i], 1, spot);
        }
        gothic(list_clip, 120, y + 20, entry->row, selected ? spot : ink);
    }
    if (g_frame.count > VISIBLE) {
        rectangle(clip, 287, 53, 1, 150, opacity(palette::shadow(), a));
        const int cy = 55 + int(std::lround(float(g_frame.start * ROW_H + g_frame.offset) * 145.f /
                                                    float((g_frame.count - VISIBLE) * ROW_H)));
        rectangle(clip, 286, cy - 2, 3, 5, spot);
        rectangle(clip, 285, cy - 1, 5, 3, spot);
    }
    const Clip footer{0, 217, 320, 240};
    native(footer, 18, 228, !g_frame.count ? "choose filter" : g_frame.replace ? "tap / a replace" : "tap / a add", muted);
    std::snprintf(text, sizeof text, "%d total", g_frame.count);
    native(footer, 188, 228, text, muted, 1);
    native(footer, 304, 228, "b close", muted, 1);
}
void picker_draw() { chains_draw_surface(false); }
} // namespace seance::platform::ui3ds
