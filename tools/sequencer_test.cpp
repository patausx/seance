// Host regression harness for the REAL sequencer controller and draw path.
// No copied controller, mock UI layout, GPU, audio thread or paid service.
// The real ui_theme, core font and draw.cpp submit to the small raster below.
// Rectangle/font output is native; line coverage is a software approximation
// of Citro2D, not a claim of pixel-identical PICA hardware rendering.
#include "theme_test_mode.h"
#include "../platform/3ds/ui_sequencer.h"
#include "../platform/3ds/ui_play_editor.h"
#include "../core/ui/play_editor_tools.h"
#include "../platform/3ds/ui_performance.h"
#include "../platform/3ds/ui_nav.h"
#include "../platform/3ds/ui_transition.h"
#include "../platform/3ds/ui_sigils.h"
#include "../platform/3ds/ui_param_lens.h"
#include "../platform/3ds/module_inspector.h"
#include "../core/engine/vca_envelope.h"
#include "../platform/3ds/delay_echo_model.h"
#include "../platform/3ds/filter_response_model.h"
#include "../platform/3ds/fold_wave_model.h"
#include "../platform/3ds/bit_reference.h"
#include "../platform/3ds/tilt_response_model.h"
#include "../platform/3ds/ring_reference.h"
#include "../platform/3ds/strain_reference.h"
#include "../core/synth/wav_loader.h"
#include "../platform/3ds/rev_tail_visual.h"
#include "../platform/3ds/rev_readout.h"
#include "../core/engine/delay_time.h"
#include "../platform/3ds/fm_context.h"
#include "../platform/3ds/menu_style.h"
#include "../platform/3ds/ui_module_focus_internal.h"
#include "../platform/3ds/strn_focus.h"
#include "../platform/3ds/sequencer_inspector.h"
#include "../platform/3ds/sequencer_profile.h"
#include "../platform/3ds/sequencer_wave.h"
#include "../platform/3ds/sequencer_lock_format.h"
#include "../platform/3ds/sequencer_slices.h"
#include "../platform/3ds/sequencer_cuts.h"
#include "../core/engine/play_window.h"
#include "../core/engine/patch_io.h"
#include "../platform/3ds/ui_top.h"
#include "../platform/3ds/ui_workspace.h"
#include "../core/ui/font.h"
#include "../platform/3ds/sequencer_layout.h"
#include "../platform/3ds/sequencer_mode_ring.h"
#include "../platform/3ds/note_display.h"
#include "../platform/3ds/ui_state.h"
#include "../platform/3ds/ui_theme.h"
#include <citro2d.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace seance;
using namespace seance::platform::ui3ds;
namespace layout = seance::platform::ui3ds::seq_layout;
namespace m = seance::platform::ui3ds::menu_style;

// Linker instrumentation only: count private Ring reference processing, and
// prove captures/draws never process the engine-owned live RingUnit.
static uint64_t ring_process_calls=0,ring_live_process_calls=0;
static RingUnit* ring_live_unit=nullptr;
extern "C" void __real__ZN6seance8RingUnit7processEPsi(RingUnit*,q15*,int);
extern "C" void __wrap__ZN6seance8RingUnit7processEPsi(RingUnit* unit,q15* data,int frames) {
    ++ring_process_calls;if(unit==ring_live_unit)++ring_live_process_calls;
    __real__ZN6seance8RingUnit7processEPsi(unit,data,frames);
}
// Host-only Strain process instrumentation; optional exact private reference PCM capture.
static uint64_t strain_process_calls=0,strain_process_frames=0,strain_live_process_calls=0;
static StrainUnit* strain_live_unit=nullptr;
static std::array<q15,1024>* strain_probe_input=nullptr;
static std::array<q15,1024>* strain_probe_output=nullptr;
static size_t strain_probe_offset=0;
extern "C" void __real__ZN6seance10StrainUnit7processEPsi(StrainUnit*,q15*,int);
extern "C" void __wrap__ZN6seance10StrainUnit7processEPsi(StrainUnit* unit,q15* data,int frames) {
    ++strain_process_calls;strain_process_frames+=frames;if(unit==strain_live_unit)++strain_live_process_calls;
    if(strain_probe_input&&strain_probe_offset+frames<=1024)std::copy(data,data+frames,strain_probe_input->begin()+strain_probe_offset);
    __real__ZN6seance10StrainUnit7processEPsi(unit,data,frames);
    if(strain_probe_output&&strain_probe_offset+frames<=1024)std::copy(data,data+frames,strain_probe_output->begin()+strain_probe_offset);
    if(strain_probe_input||strain_probe_output)strain_probe_offset+=frames;
}

// Host-only REV counter: neither scalar schematic capture nor draw may process DSP.
static uint64_t rev_process_calls=0;
extern "C" void __real__ZN6seance10ReverbUnit7processEPsi(ReverbUnit*,q15*,int);
extern "C" void __wrap__ZN6seance10ReverbUnit7processEPsi(ReverbUnit* u,q15* data,int frames){
    ++rev_process_calls;__real__ZN6seance10ReverbUnit7processEPsi(u,data,frames);
}

namespace {
int mutations = 0;
circlePosition circle;
// Explicit virtual wall clock for step-focus tests only. Legacy fixtures retain
// their original deterministic frame clock; no real sleeping or host timing.
uint64_t& step_focus_test_ms = host_time_ms;
constexpr int HEIGHT = 240;
int width = 320;
std::vector<u32> pixels(320 * HEIGHT);
int objects = 0, outside_rects = 0, outside_lines = 0;
int maximum_objects = 0, maximum_top_objects = 0, maximum_shared_objects = 0;
struct TextRecord { int x, y, w, h, call_x, call_y, scale; std::string text; };
std::vector<TextRecord> texts;
int text_overlaps = 0;
bool strn_hero_audit = false;
int native_text_depth = 0;
void record_text(int x, int y, const char* text, int scale) {
    int minx = 4096, miny = 4096, maxx = -1, maxy = -1, cx = x;
    for (const char* p = text; *p; ++p, cx += 6 * scale) {
        if (*p == '\n') { cx = x - 6 * scale; y += 8 * scale; continue; }
        for (int row = 0; row < 8; ++row) for (int col = 0; col < 6; ++col)
            if (trackr::ui::glyph_row(*p, row) & (1u << (5 - col))) {
                minx = std::min(minx, cx + col * scale); miny = std::min(miny, y + row * scale);
                maxx = std::max(maxx, cx + (col + 1) * scale); maxy = std::max(maxy, y + (row + 1) * scale);
            }
    }
    if (maxx < 0) return;
    for (const auto& previous : texts)
        if (minx < previous.x + previous.w && maxx > previous.x &&
            miny < previous.y + previous.h && maxy > previous.y) ++text_overlaps;
    texts.push_back({minx, miny, maxx - minx, maxy - miny, x, y, scale, text});
}
void check(bool condition, const char* message, int line) {
    if (!condition) throw std::runtime_error(std::string(message) + " [line " + std::to_string(line) + "]");
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
struct GranLine {int x0,y0,x1,y1;u32 color;};
std::vector<GranLine>* gran_lines=nullptr;
void pixel(int x, int y, u32 color) {
    if (x < 0 || y < 0 || x >= width || y >= HEIGHT) return;
    u32& dst = pixels[y * width + x];
    const unsigned alpha = color >> 24;
    u32 result = 0xff000000u;
    for (unsigned shift : {0u, 8u, 16u}) {
        const unsigned src = (color >> shift) & 255u, old = (dst >> shift) & 255u;
        result |= ((src * alpha + old * (255u - alpha) + 127u) / 255u) << shift;
    }
    dst = result;
}
std::vector<u32> draw(bool top = false) {
    width = top ? 400 : 320;
    objects = outside_rects = outside_lines = text_overlaps = 0; texts.clear();
    pixels.assign(width * HEIGHT, 0xff000000u);
    CHECK(g_view == View::Sequencer);
    sequencer_capture_inspector();
    if (top) top_draw(); else sequencer_view_draw();
    if (top) maximum_top_objects = std::max(maximum_top_objects, objects);
    else maximum_objects = std::max(maximum_objects, objects);
    CHECK(objects > 0);
    CHECK(objects < C2D_DEFAULT_MAX_OBJECTS * 2);
    return pixels;
}
void check_dual_budget() {
    draw(true); const int top_objects = objects;
    CHECK(outside_rects == 0 && outside_lines == 0 && text_overlaps == 0);
    draw();
    CHECK(outside_rects == 0 && outside_lines == 0);
    maximum_shared_objects = std::max(maximum_shared_objects, top_objects + objects);
    CHECK(top_objects + objects < C2D_DEFAULT_MAX_OBJECTS * 2);
}
uint64_t pattern_hash(const Pattern& pattern) {
    uint64_t hash = 1469598103934665603ull;
    auto add = [&](unsigned value) { hash = (hash ^ value) * 1099511628211ull; };
    for (int c = 0; c < NUM_CHAINS; ++c) {
        add(pattern.length[c]); add((unsigned)pattern.direction[c]);
        for (int s = 0; s < SEQ_STEPS; ++s) {
            const SeqStep& step = pattern.steps[c][s];
            add(step.flags); add(step.lock_count); add((uint8_t)pattern.microtiming[c][s]);
            for (const auto& lock : step.locks) { add(lock.slot); add(lock.param); add((uint16_t)lock.value); }
        }
    }
    return hash;
}
}

namespace seance::platform::ui3ds {
Engine* g_eng = nullptr;
Unit* g_clip_unit = nullptr;
const char* RECORDING_DIR = "/tmp/seance-play-ui-unused-export";
bool g_mic_ok = false;
platform::AudioOut* g_audio = nullptr;
EngineUiSnapshot g_ui_snapshot;
// Main-owned shared horizontal mapping adapter; same integer endpoint law.
q15 norm_h(int tx,int x,int w,bool bipolar) {
    int32_t v=(int32_t)(tx-x)*32767/(w-1);v=std::max(0,std::min(32767,(int)v));
    return (q15)(bipolar?v*2-32767:v);
}
Overlay g_overlay = Overlay::None;
Drag g_drag = Drag::None;
int g_edit_chain = 0, g_edit_slot = 0, g_drag_param = 0;
int g_sample_page = 0, g_sample_browser_mode = 0;
// Main-owned sample-slot adapter backed by the real PLAY field; browser/export stay unreachable.
int sample_slot_for_unit(const Unit* u) {
    if (u->kind == UnitKind::SamplePlayer) return static_cast<const PlayUnit*>(u)->sample_slot;
    if (u->kind == UnitKind::Grain) return static_cast<const GrainUnit*>(u)->sample_slot;
    CHECK(u->kind == UnitKind::Osc || u->kind == UnitKind::Fm || u->kind == UnitKind::Ghost || u->kind == UnitKind::Stranular ||
          u->kind == UnitKind::Vca || u->kind == UnitKind::Chorus || u->kind == UnitKind::Reverb || u->kind == UnitKind::Motor || u->kind == UnitKind::MicIn); return -1;
}
// Link-only legacy shell boundary. Modern composed editors must never request it.
uint32_t sample_slot_frames(int) { CHECK(false); return 0; }
bool gran_browser_enabled=false;int gran_browser_scans=0;
void scan_sample_files() { CHECK(gran_browser_enabled);++gran_browser_scans; }
void sample_browser_draw_top() { CHECK(false); } // separate real browser harness owns this route
int g_export_flash=0;bool g_export_ok=false,g_export_is_load=false;
bool rec_export_enabled=false,rec_export_succeeds=false;int rec_export_calls=0,rec_workspace_shifts=0;
#ifndef SEANCE_REAL_EDITOR
Unit* edited_unit() {return g_eng->chains[g_edit_chain].units[g_edit_slot];}
// Link-only legacy shell boundary; focused score suites must never enter REC's
// editor navigation. Real editor integration builds use their native definition.
void editor_cancel_input() { CHECK(false); }
#endif
void workspace_shift_slots_right(int,int,int) {++rec_workspace_shifts;}
// REC UI test boundary only: simulate export outcome; real WAV IO is unchanged
// and is not claimed as tested by this focused UI executable.
void export_current_sample() {
    CHECK(rec_export_enabled);++rec_export_calls;g_export_ok=rec_export_succeeds;
    g_export_is_load=false;g_export_flash=120;
    if(g_export_ok) {
        auto* rec=static_cast<RecUnit*>(edited_unit());
        rec->mark_take_saved(rec->take_generation());
        trackr::synth::SampleBank::instance().set_provenance(rec->sample_slot,trackr::synth::SampleOrigin::Recording,"rec-ui-fixture.wav");
    }
}
void set_sample_slot_for_unit(Unit*, int) { CHECK(false); }
// Main-owned p-lock adapters, backed by real engine storage in these UI tests.
bool seq_record_lock(int slot, int param, q15 value) {
    if (!g_seq_lock_armed) return true;
    if (g_edit_chain != g_seq_lock_chain || g_seq_lock_chain < 0 || g_seq_lock_chain >= NUM_CHAINS ||
        g_seq_lock_step < 0 || g_seq_lock_step >= SEQ_STEPS) { g_seq_lock_flash = 90; return false; }
    const auto& step = g_eng->pattern.steps[g_seq_lock_chain][g_seq_lock_step];
    bool changed = true;
    for (int i=0; i<std::min((int)step.lock_count,SEQ_LOCKS); ++i)
        if (step.locks[i].slot == slot && step.locks[i].param == param) { changed = step.locks[i].value != value; break; }
    const bool ok = g_eng->set_step_lock(g_seq_lock_chain, g_seq_lock_step, slot, param, value);
    if (!ok) g_seq_lock_flash = 90;
    else if (changed) mark_project_mutated();
    return ok;
}
q15 seq_display_value(int slot, int param, q15 fallback) {
    if (!g_seq_lock_armed || g_edit_chain != g_seq_lock_chain) return fallback;
    const auto& step = g_eng->pattern.steps[g_seq_lock_chain][g_seq_lock_step];
    for (int i=0; i<std::min((int)step.lock_count,SEQ_LOCKS); ++i)
        if (step.locks[i].slot == slot && step.locks[i].param == param) return step.locks[i].value;
    return fallback;
}
bool g_seq_lock_armed = false;
int g_seq_lock_chain = 0, g_seq_lock_step = 0;
int g_sel_chain = 0, g_seq_lock_flash = 0;
int g_sel_sigil = 0, g_sel_slot = 0, g_patch_slot = 0;
// Main-owned storage used by the actual Sigils stroke and compatibility undo path.
int g_last_px = -1, g_last_py = 0, g_smooth_py = 0;
bool g_smooth_active = false, g_sig_canvas_mutated = false;
q15 g_sigil_undo[NUM_SIGILS][SIGIL_POINTS] = {}, g_sigil_clip[SIGIL_POINTS] = {};
bool g_sigil_undo_valid[NUM_SIGILS] = {}, g_sigil_clip_valid = false;
u32 g_ui_rng = 0x12345678u;
ModSource sigil_source(int index) {
    return index >= 0 && index < NUM_SIGILS ? sigil_mod_source(index) : ModSource::None;
}
View g_view = View::Sequencer;
u32 g_ui_frame = 0;
void mark_project_mutated() { ++mutations; }
bool project_is_dirty() { return mutations != 0; }
int project_active_slot() { return 0; } // Active session, deliberately not browser g_patch_slot.
// Link-only defaults for the unreachable non-sequencer top branches.
void chains_context_draw_top() {} // real Chains path is exercised by menu_stage1_test.cpp
const WorkspacePoint& workspace_point(int, int) { static const WorkspacePoint point{}; return point; }
int workspace_camera_x(int) { return 0; }
int workspace_camera_y(int) { return 0; }
}
// GNU ld wrapping observes real text calls, then executes the original renderer.
// No text/mock glyph implementation replaces core/ui/draw.cpp or font.cpp.
void real_text(int, int, const char*, uint32_t, int) asm("__real__ZN6seance2ui4textEiiPKcji");
extern "C" void __wrap__ZN6seance2ui4textEiiPKcji(int x, int y, const char* text, uint32_t color, int scale) {
    record_text(x, y, text, scale);
    ++native_text_depth; real_text(x, y, text, color, scale); --native_text_depth;
}
void hidCircleRead(circlePosition* position) { *position = circle; }
bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 color) {
    CHECK(std::isfinite(x) && std::isfinite(y) && std::isfinite(z));
    CHECK(std::isfinite(w) && std::isfinite(h) && w >= 0 && h >= 0);
    CHECK(std::abs(x) < 4096 && std::abs(y) < 4096 && w < 4096 && h < 4096);
    ++objects;
    if (strn_hero_audit && !native_text_depth && y >= 70 && y < 145 && h <= 26 &&
        (color == ink() || color == spot())) CHECK(x >= 14 && x+w < 239);
    // Solid overlays hide covered source labels; they aren't visible collisions.
    if ((color >> 24) == 255 && w >= 200 && h >= 100)
        texts.erase(std::remove_if(texts.begin(), texts.end(), [&](const TextRecord& t) {
            return t.x >= x && t.y >= y && t.x + t.w <= x + w && t.y + t.h <= y + h;
        }), texts.end());
    if (x < 0 || y < 0 || x + w > width || y + h > HEIGHT) ++outside_rects;
    for (int yy = (int)std::ceil(y); yy < (int)std::ceil(y + h); ++yy)
        for (int xx = (int)std::ceil(x); xx < (int)std::ceil(x + w); ++xx) pixel(xx, yy, color);
    return true;
}
bool C2D_DrawLine(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float thickness, float z) {
    CHECK(c0 == c1 && std::isfinite(z) && std::isfinite(thickness) && thickness > 0);
    if(gran_lines)gran_lines->push_back({int(x0),int(y0),int(x1),int(y1),c0});
    CHECK(std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1) && std::isfinite(y1));
    CHECK(std::abs(x0) < 4096 && std::abs(y0) < 4096 && std::abs(x1) < 4096 && std::abs(y1) < 4096);
    ++objects;
    int x = (int)std::lround(x0), y = (int)std::lround(y0);
    const int ex = (int)std::lround(x1), ey = (int)std::lround(y1);
    const int dx = std::abs(ex - x), dy = -std::abs(ey - y), sx = x < ex ? 1 : -1, sy = y < ey ? 1 : -1;
    int error = dx + dy;
    const int line_width = std::max(1, (int)std::lround(thickness));
    if (std::min(x, ex) < 0 || std::min(y, ey) < 0 ||
        std::max(x, ex) + line_width > width || std::max(y, ey) + line_width > HEIGHT) ++outside_lines;
    for (;;) {
        for (int yy = 0; yy < line_width; ++yy) for (int xx = 0; xx < line_width; ++xx) pixel(x + xx, y + yy, c0);
        if (x == ex && y == ey) break;
        const int e2 = 2 * error;
        if (e2 >= dy) { error += dy; x += sx; }
        if (e2 <= dx) { error += dx; y += sy; }
    }
    return true;
}

bool C2D_DrawTriangle(float x0,float y0,u32 c0,float x1,float y1,u32 c1,float x2,float y2,u32 c2,float z) {
    CHECK(c0==c1 && c1==c2 && (c0>>24)==255 && std::isfinite(z));
    for(float v:{x0,y0,x1,y1,x2,y2})CHECK(std::isfinite(v) && std::abs(v)<4096);
    ++objects;
    const float left=std::min({x0,x1,x2}),right=std::max({x0,x1,x2});
    const float top=std::min({y0,y1,y2}),bottom=std::max({y0,y1,y2});
    if(left<0 || top<0 || right>width || bottom>HEIGHT)++outside_lines;
    auto edge=[](float ax,float ay,float bx,float by,float px,float py){return (bx-ax)*(py-ay)-(by-ay)*(px-ax);};
    const float area=edge(x0,y0,x1,y1,x2,y2);if(area==0)return true;
    // Pixel-centre triangle approximation only: not a PICA edge-rule oracle.
    // Existing Bresenham host lines remain byte-for-byte unchanged.
    for(int y=(int)std::floor(top);y<(int)std::ceil(bottom);++y)
        for(int x=(int)std::floor(left);x<(int)std::ceil(right);++x) {
            const float a=edge(x0,y0,x1,y1,x+.5f,y+.5f),b=edge(x1,y1,x2,y2,x+.5f,y+.5f),c=edge(x2,y2,x0,y0,x+.5f,y+.5f);
            if((area>0 && a>=0 && b>=0 && c>=0)||(area<0 && a<=0 && b<=0 && c<=0))pixel(x,y,c0);
        }
    return true;
}

namespace {
struct Fixture {
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    platform::AudioOut audio;
    Fixture() {
        g_eng = engine.get(); g_audio = &audio; circle = {}; mutations = 0; g_sel_chain = 0; g_ui_frame = 0;
        g_view = View::Sequencer; g_patch_slot = g_sel_sigil = g_sel_slot = g_seq_lock_flash = 0;
        g_seq_lock_armed = false; g_seq_lock_chain = g_seq_lock_step = 0;
        stranular_focus_cancel();
        // Isolate generic lens tests from persistent FM inspector selection.
        // FM-specific fixtures explicitly select INDEX or another tested parameter.
        fm_focus_release();fm_focus_select(FmUnit::P_PITCH);
        engine->chains[0].units[0] = new FmUnit;
        engine->chains[1].units[0] = new OscUnit;
        engine->chains[1].units[1] = new VcaUnit;
        engine->chains[2].units[0] = new PlayUnit;
        engine->chains[3].units[0] = new OscUnit;
        sequencer_forget_pins();
        refresh(); sequencer_view_enter();
    }
    ~Fixture() { stranular_focus_cancel(); param_lens_cancel(); g_overlay = Overlay::None; play_focus_capture(); g_eng = nullptr; g_audio = nullptr; }
    void refresh() { engine->capture_ui_snapshot(g_ui_snapshot); }
    void frame(u32 down = 0, u32 held = 0, u32 up = 0) {
        ++g_ui_frame; sequencer_view_buttons(down, held, up); refresh();
    }
    void tap(u32 key) { frame(key, key); frame(0, 0, key); }
    void r_hold(int frames = 14) { frame(KEY_R, KEY_R); for (int i = 1; i < frames; ++i) frame(0, KEY_R); }
    void chord(u32 key) { frame(KEY_R | key, KEY_R | key); frame(0, 0, KEY_R | key); }
    void mode(int target) { for (int i = 0; sequencer_selected_layer() != target && i < 8; ++i) tap(KEY_R); CHECK(sequencer_selected_layer() == target); frame(); }
    void down(int chain, int step) { frame(); sequencer_view_touch_down(layout::X0 + step * layout::PITCH + layout::CELL_W / 2, layout::ROWS[chain] + layout::CELL_H / 2); refresh(); }
    void hold(int chain, int step) { sequencer_view_touch_hold(layout::X0 + step * layout::PITCH + layout::CELL_W / 2, layout::ROWS[chain] + layout::CELL_H / 2); refresh(); }
    void release(bool commit = true) { sequencer_view_touch_release(commit); refresh(); }
    void touch(int chain, int step) { down(chain, step); release(); }
    uint64_t hash() const { return pattern_hash(engine->pattern); }
    bool trig(int chain, int step) const { return (engine->pattern.steps[chain][step].flags & SeqStep::Trig) != 0; }
    void dense() {
        for (int c = 0; c < NUM_CHAINS; ++c) {
            engine->pattern.length[c] = 64;
            for (int s = 0; s < SEQ_STEPS; ++s) {
                SeqStep& step = engine->pattern.steps[c][s];
                step.flags = SeqStep::Trig;
                step.set_retrig_count((uint8_t)(s % 4 + 1));
                step.set_condition((SeqStep::Condition)(s % 8));
                engine->pattern.microtiming[c][s] = (int8_t)((s % 3 == 0) ? -23 : (s % 3 == 1) ? 23 : 0);
                const int pitch = g_ui_snapshot.sequence_pitch_param[c], value = g_ui_snapshot.sequence_value_param[c];
                const int pitch_slot = g_ui_snapshot.sequence_pitch_slot[c], value_slot = g_ui_snapshot.sequence_value_slot[c];
                CHECK(pitch >= 0 && pitch_slot >= 0 && value >= 0 && value_slot >= 0);
                // Includes a#2 / b2 / c3 neighbors and long reverse r-24 label.
                const q15 note = c == 2 ? (q15)-2048 : (q15)((22 + s % 3) * 32767 / 72);
                CHECK(engine->set_step_lock(c, s, pitch_slot, pitch, note));
                CHECK(engine->set_step_lock(c, s, value_slot, value, (q15)((s % 2) ? 32767 : 6000)));
            }
        }
        engine->pattern.direction[0] = SequenceDirection::Reverse;
        refresh();
    }
};
#include "param_lens_cases.h"
void test_r_tap_hold() {
    Fixture f;
    const uint64_t original = f.hash();
    const char* modes[] = {"trig", "note", "level", "retrig", "when", "time", "lock", "slice"};
    for (int i = 0; i < 16; ++i) {
        CHECK(std::string(sequencer_selected_layer_name()) == modes[i % 8]);
        const auto baseline = draw();
        f.frame(KEY_R, KEY_R);
        CHECK(draw() == baseline); // no flashing modal on a short tap
        f.frame(0, 0, KEY_R);
        CHECK(sequencer_selected_layer() == (i + 1) % 8);
    }
    f.mode(3);
    const auto baseline = draw();
    f.r_hold(); CHECK(draw() != baseline);
    f.frame(0, 0, KEY_R);
    CHECK(sequencer_selected_layer() == 3); CHECK(draw() == baseline);
    CHECK(f.hash() == original && mutations == 0);
}
void test_r_chords() {
    const u32 keys[] = {KEY_DUP, KEY_DRIGHT, KEY_DDOWN, KEY_DLEFT, KEY_X, KEY_Y, KEY_A, KEY_L};
    const int modes[] = {1, 2, 3, 5, 0, 4, 6, 7};
    for (int i = 0; i < 8; ++i) {
        Fixture f; f.dense(); f.mode(3);
        const uint64_t original = f.hash(); const int dirty = mutations;
        f.chord(keys[i]);
        CHECK(sequencer_selected_layer() == modes[i]);
        CHECK(sequencer_selected_step() == 0 && sequencer_selected_chain() == 0);
        CHECK(f.hash() == original && mutations == dirty);
        // Also exercise a chord after R was already held for a frame.
        f.mode(3); f.frame(KEY_R, KEY_R); f.frame(keys[i], KEY_R | keys[i]); f.frame(0, 0, KEY_R | keys[i]);
        CHECK(sequencer_selected_layer() == modes[i]); CHECK(f.hash() == original);
    }
    // R+Y must not prime the copy buffer. Normal Y still copies, not pastes.
    Fixture f; f.dense(); f.chord(KEY_Y); f.tap(KEY_DRIGHT);
    const uint64_t original = f.hash(); f.tap(KEY_Y); CHECK(f.hash() == original);
}
void test_cancel() {
    Fixture f; f.dense(); f.mode(2);
    const uint64_t original = f.hash(); const auto baseline = draw();
    f.r_hold(); f.frame(KEY_DUP, KEY_R | KEY_DUP); f.frame(KEY_B, KEY_R | KEY_B); f.frame(0, 0, KEY_R | KEY_B);
    CHECK(sequencer_selected_layer() == 2); CHECK(draw() == baseline);
    f.r_hold(); f.frame(KEY_DUP, KEY_R | KEY_DUP); f.frame(KEY_SELECT, KEY_R | KEY_SELECT); f.frame(0, 0, KEY_R | KEY_SELECT);
    CHECK(sequencer_selected_layer() == 2); CHECK(draw() == baseline); CHECK(f.hash() == original);
    f.r_hold(); f.frame(KEY_DUP, KEY_R | KEY_DUP); sequencer_view_cancel_input(); f.frame(0, 0, KEY_R);
    CHECK(sequencer_selected_layer() == 2); CHECK(draw() == baseline);
    f.mode(0); f.down(0, 3); sequencer_view_cancel_input(); f.release(); CHECK(f.hash() == original);
    // Cancelling an already-mutated editor drag prevents subsequent movement.
    f.mode(2); sequencer_view_touch_down(200, 215); f.refresh(); const auto after_down = f.hash();
    sequencer_view_cancel_input(); sequencer_view_touch_hold(310, 215); f.release(); CHECK(f.hash() == after_down);
    // An R-release + touch-down in one HID frame belongs entirely to the chord.
    f.mode(0); const auto before_release = f.hash();
    f.frame(KEY_R | KEY_X, KEY_R | KEY_X); f.frame(0, 0, KEY_R | KEY_X);
    sequencer_view_touch_down(layout::X0 + 4, layout::ROWS[0] + 4); f.release();
    CHECK(f.hash() == before_release);
}
void test_circle() {
    Fixture f;
    const circlePosition positions[] = {{-100,100},{0,140},{140,0},{100,-100},{100,100},{-140,0},{-100,-100},{0,-140}};
    const auto original = f.hash();
    for (int mode = 0; mode < 8; ++mode) {
        circle = positions[mode]; f.r_hold();
        // libctru also publishes CPAD direction bits; they are not D-pad chords.
        const u32 bits = (circle.dx > 30 ? KEY_CPAD_RIGHT : circle.dx < -30 ? KEY_CPAD_LEFT : 0u) |
                         (circle.dy > 30 ? KEY_CPAD_UP : circle.dy < -30 ? KEY_CPAD_DOWN : 0u);
        f.frame(bits, KEY_R | bits); f.frame(0, 0, KEY_R | bits); circle = {};
        CHECK(sequencer_selected_layer() == mode);
        CHECK(sequencer_selected_step() == 0 && sequencer_selected_chain() == 0);
    }
    // Free space (including a return on the release frame) never commits the
    // last magnet. Near-centre motion is not an implicit nearest-sector choice.
    f.mode(3); circle={0,140}; f.r_hold(); circle={}; f.frame(0,0,KEY_R);
    CHECK(sequencer_selected_layer()==3);
    circle={40,0}; f.r_hold(); f.frame(0,0,KEY_R); circle={};
    CHECK(sequencer_selected_layer()==3);
    // Explicit choices survive idle frames, not just immediate release.
    f.r_hold(); f.frame(KEY_L,KEY_R|KEY_L); f.frame(0,KEY_R); f.frame(0,0,KEY_R);
    CHECK(sequencer_selected_layer()==7);
    CHECK(f.hash() == original);
}
void test_chooser_touch() {
    for (int mode = 0; mode < 8; ++mode) {
        Fixture f; f.dense(); f.mode(2);
        const auto original = f.hash(); const int dirty = mutations;
        f.r_hold();
        const int x = layout::MODE_X[mode] + layout::MODE_W / 2;
        const int y = layout::MODE_Y[mode] + layout::MODE_H / 2;
        sequencer_view_touch_down(x, y); f.refresh();
        sequencer_view_touch_hold(x, y); f.release();
        f.frame(0, 0, KEY_R);
        CHECK(sequencer_selected_layer() == mode);
        CHECK(sequencer_selected_chain() == 0 && sequencer_selected_step() == 0);
        CHECK(f.hash() == original && mutations == dirty);
    }
    Fixture f; f.dense(); f.mode(2); const auto original = f.hash();
    f.r_hold();
    sequencer_view_touch_down(310, 216); f.release(); f.frame(0, 0, KEY_R);
    CHECK(sequencer_selected_layer() == 2); CHECK(f.hash() == original);
    f.r_hold();
    sequencer_view_touch_down(layout::MODE_X[1] + 4, layout::MODE_Y[1] + 4);
    sequencer_view_cancel_input(); f.release(); f.frame(0, 0, KEY_R);
    CHECK(sequencer_selected_layer() == 2); CHECK(f.hash() == original);
}
void test_navigation() {
    Fixture f;
    f.tap(KEY_DLEFT); CHECK(sequencer_selected_step() == 63 && sequencer_current_page() == 3);
    f.tap(KEY_DRIGHT); CHECK(sequencer_selected_step() == 0 && sequencer_current_page() == 0);
    f.tap(KEY_DUP); CHECK(sequencer_selected_chain() == 3);
    f.tap(KEY_DDOWN); CHECK(sequencer_selected_chain() == 0);
    f.mode(1); f.touch(0, 5); const auto original = f.hash();
    for (int page : {1, 2, 3, 0}) { f.tap(KEY_L | KEY_DRIGHT); CHECK(sequencer_current_page() == page); CHECK(sequencer_selected_step() == page * 16 + 5); }
    f.tap(KEY_L | KEY_DLEFT); CHECK(sequencer_current_page() == 3 && sequencer_selected_step() == 53);
    CHECK(f.hash() == original);
    const q15 pitch = g_ui_snapshot.sequence_pitch[0][53];
    f.tap(KEY_L | KEY_DUP); CHECK(g_ui_snapshot.sequence_pitch[0][53] > pitch);
    CHECK(sequencer_selected_chain() == 0 && sequencer_selected_step() == 53);
    f.tap(KEY_L | KEY_DDOWN); CHECK(g_ui_snapshot.sequence_pitch[0][53] <= pitch);
    CHECK(sequencer_selected_chain() == 0 && sequencer_selected_step() == 53);
}
void test_touch_selection() {
    Fixture f; f.dense();
    for (int mode = 1; mode < 8; ++mode) {
        f.mode(mode); const auto original = f.hash(); const int dirty = mutations;
        for (int chain = 0; chain < NUM_CHAINS; ++chain) for (int step = 0; step < 16; ++step) {
            f.touch(chain, step);
            CHECK(sequencer_selected_chain() == chain && sequencer_selected_step() == step);
            CHECK(f.hash() == original && mutations == dirty);
        }
        f.down(0, 1); f.hold(0, 7); f.release(); CHECK(f.hash() == original);
    }
}
void test_touch_trig() {
    Fixture f;
    f.touch(0, 0); CHECK(f.trig(0, 0)); f.touch(0, 0); CHECK(!f.trig(0, 0));
    f.down(0, 1); f.hold(0, 2); f.hold(0, 3); f.release();
    CHECK(f.trig(0, 1) && f.trig(0, 2) && f.trig(0, 3));
    f.down(0, 1); f.hold(0, 2); f.hold(0, 3); f.release();
    CHECK(!f.trig(0, 1) && !f.trig(0, 2) && !f.trig(0, 3));
    f.down(1, 1); f.hold(1, 8); f.release();
    for (int step = 1; step <= 8; ++step) CHECK(f.trig(1, step));
    // The whole fast stroke is filled in both directions, with no missed cells.
    f.down(1, 8); f.hold(1, 1); f.release();
    for (int step = 1; step <= 8; ++step) CHECK(!f.trig(1, step));
    const auto original = f.hash(); f.down(0, 5); f.release(false); CHECK(f.hash() == original);
    f.down(0, 5); f.frame(KEY_R, KEY_R); f.release(); f.frame(0, 0, KEY_R); CHECK(f.hash() == original);
}
void test_apply_clear_copy() {
    Fixture f;
    f.tap(KEY_A); CHECK(f.trig(0, 0));
    for (int mode = 1; mode < 6; ++mode) {
        f.mode(mode); const auto original = f.hash(); f.tap(KEY_A); CHECK(f.hash() != original);
    }
    const SeqStep source = f.engine->pattern.steps[0][0];
    const int timing = f.engine->pattern.microtiming[0][0];
    const auto original = f.hash(); f.tap(KEY_Y); CHECK(f.hash() == original);
    f.tap(KEY_DRIGHT); f.tap(KEY_Y);
    const SeqStep& pasted = f.engine->pattern.steps[0][1];
    CHECK(pasted.flags == source.flags && pasted.lock_count == source.lock_count);
    CHECK(f.engine->pattern.microtiming[0][1] == timing);
    for (int i = 0; i < source.lock_count; ++i) CHECK(pasted.locks[i].slot == source.locks[i].slot && pasted.locks[i].param == source.locks[i].param && pasted.locks[i].value == source.locks[i].value);
    f.tap(KEY_X); CHECK(pasted.flags == 0 && pasted.lock_count == 0 && f.engine->pattern.microtiming[0][1] == 0);
}
void test_editor_controls() {
    Fixture f;
    f.mode(4);
    for (int condition = 0; condition < (int)SeqStep::Condition::Count; ++condition) {
        sequencer_view_touch_down(70 + condition * 30 + 15, 215); f.release();
        CHECK((int)f.engine->pattern.steps[0][0].condition() == condition);
    }
    for (int mode = 0; mode < 7; ++mode) {
        f.mode(mode); const auto original = f.hash();
        // Labels/help and the mode heading cannot operate hidden sliders.
        sequencer_view_touch_down(10, 215); f.release();
        sequencer_view_touch_down(200, 235); f.release();
        sequencer_view_touch_down(200, 201); f.release();
        CHECK(f.hash() == original);
    }
    f.mode(6); sequencer_view_touch_down(270, 215); f.release();
    CHECK(f.engine->pattern.steps[0][0].lock_count == 1);
    f.tap(KEY_START); CHECK(f.engine->transport_playing && f.engine->sequencer_enabled);
    f.tap(KEY_START); CHECK(!f.engine->transport_playing);
}
std::string selected_pitch() {
    char text[16] = {}; sequencer_selected_pitch_text(text, sizeof text); return text;
}
bool has_text(const std::string& expected) {
    for (const auto& text : texts) if (text.text == expected) return true;
    return false;
}
bool has_text_at(const std::string& expected,int x,int y) {
    for (const auto& text : texts) if (text.text == expected&&text.x==x&&text.y==y) return true;
    return false;
}
void check_inspector_gothic_mask(const char* value,int baseline=109) {
    CHECK(width==400);const int x0=14,y0=baseline-39,y1=baseline+8;
    std::vector<u32> expected((size_t)width*HEIGHT);
    for(int y=0;y<HEIGHT;++y)std::fill(expected.begin()+(size_t)y*width,expected.begin()+(size_t)(y+1)*width,m::paper_color(y));
    int x=x0;bool digit_seen=false;
    for(const char* p=value;*p;++p) {
        if(*p>='0'&&*p<='9') {
            const auto* g=note_display::find(*p);CHECK(g&&!g->small);digit_seen=true;
            const int stride=(g->width+7)/8;
            for(int y=0;y<g->height;++y)for(int col=0;col<g->width;++col)
                if(note_display::bitmap[g->offset+y*stride+col/8]&(0x80u>>(col&7)))
                    expected[(baseline+g->top+y)*width+x+col]=m::ink();
            x+=g->width+2;
        } else if(*p=='+'||*p=='-') {
            for(int yy=baseline-14;yy<baseline-12;++yy)for(int xx=x;xx<x+13;++xx)expected[yy*width+xx]=m::spot();
            if(*p=='+')for(int yy=baseline-20;yy<baseline-7;++yy)for(int xx=x+6;xx<x+8;++xx)expected[yy*width+xx]=m::spot();
            x+=18;
        } else if(*p=='.') {
            for(int yy=baseline-3;yy<baseline;++yy)for(int xx=x;xx<x+3;++xx)expected[yy*width+xx]=m::ink();
            x+=6;
        } else break;
    }
    CHECK(digit_seen&&x<239);
    int expected_minx=x,expected_miny=y1,expected_maxx=x0-1,expected_maxy=y0-1;
    int actual_minx=x,actual_miny=y1,actual_maxx=x0-1,actual_maxy=y0-1;
    for(int y=y0;y<=y1;++y)for(int xx=x0;xx<x;++xx) {
        CHECK(pixels[y*width+xx]==expected[y*width+xx]);
        if(expected[y*width+xx]!=m::paper_color(y)){expected_minx=std::min(expected_minx,xx);expected_miny=std::min(expected_miny,y);expected_maxx=std::max(expected_maxx,xx);expected_maxy=std::max(expected_maxy,y);}
        if(pixels[y*width+xx]!=m::paper_color(y)){actual_minx=std::min(actual_minx,xx);actual_miny=std::min(actual_miny,y);actual_maxx=std::max(actual_maxx,xx);actual_maxy=std::max(actual_maxy,y);}
    }
    CHECK(std::tie(actual_minx,actual_miny,actual_maxx,actual_maxy)==std::tie(expected_minx,expected_miny,expected_maxx,expected_maxy));
    const char* suffix=value;while((*suffix>='0'&&*suffix<='9')||*suffix=='+'||*suffix=='-'||*suffix=='.')++suffix;
    if(*suffix){const int suffix_scale=x>187?1:2;bool found=false;for(const auto&t:texts)if(t.text==suffix&&t.call_x==x+4&&t.call_y==baseline-10&&t.scale==suffix_scale){CHECK(t.h<=8*suffix_scale);found=true;}CHECK(found);}
}
void check_hero(const std::string& expected, bool melodic) {
    // Verify the actual upper UI called the baked renderer with the exact engine label
    // at the approved menu position.
    const auto screen = draw(true);
    CHECK(outside_rects == 0 && text_overlaps == 0);
    width = 400; pixels.assign(400 * HEIGHT, 0); objects = 0; texts.clear();
    m::paper(400);
    CHECK(melodic ? note_display::melodic(expected.c_str(), m::ink(), m::spot(), 14, 18) :
                    note_display::semitones(expected.c_str(), m::ink(), m::spot(), 14, 18));
    CHECK(objects > 0 && objects < 700);
    int ink = 0;
    for (int y = 0; y < HEIGHT; ++y) for (int x = 0; x < 400; ++x) {
        if (pixels[y * 400 + x] == m::paper_color(y)) continue;
        CHECK(x >= 24 && x < 121 && y >= (melodic ? 62 : 65) && y < (melodic ? 129 : 107));
        CHECK(screen[y * 400 + x] == pixels[y * 400 + x]);
        ++ink;
    }
    CHECK(ink > 30);
}
void set_pitch(Fixture& f, int chain, q15 value) {
    CHECK(f.engine->set_step_lock(chain, 0, g_ui_snapshot.sequence_pitch_slot[chain],
                                 g_ui_snapshot.sequence_pitch_param[chain], value));
    f.refresh();
}
void test_pitch_labels_and_quantization() {
    Fixture f; f.mode(1);
    static const char* names[] = {"c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"};
    for (int chain : {0, 1}) {
        f.touch(chain, 0);
        for (int semitone = 0; semitone <= 72; ++semitone) {
            set_pitch(f, chain, (q15)(semitone * 32767 / 72));
            const int midi = 24 + semitone;
            const std::string expected = std::string(names[midi % 12]) + std::to_string(midi / 12 - 1);
            CHECK(selected_pitch() == expected);
            draw(); CHECK(has_text(expected)); // complete editor label, not truncated cell glyph
            check_hero(expected, true);
        }
        for (int x = layout::VALUE_X0; x <= layout::VALUE_X1; ++x) {
            f.frame(); sequencer_view_touch_down(x, 215); f.release();
            const int semi = ((x - layout::VALUE_X0) * 72 + layout::VALUE_TRAVEL / 2) / layout::VALUE_TRAVEL;
            CHECK(g_ui_snapshot.sequence_pitch[chain][0] == semi * 32767 / 72);
        }
    }
    char guarded[5] = {'x','x','x','x','x'};
    sequencer_selected_pitch_text(guarded + 1, 2);
    CHECK(guarded[0] == 'x' && guarded[2] == 0 && guarded[3] == 'x');
    sequencer_selected_pitch_text(nullptr, 0);
}
void test_sample_pitch_clamp() {
    Fixture f; f.mode(1); f.touch(2, 0);
    for (UnitKind kind : {UnitKind::SamplePlayer, UnitKind::Grain}) {
        delete f.engine->replace_unit(2, 0, kind == UnitKind::SamplePlayer ? (Unit*)new PlayUnit : (Unit*)new GrainUnit);
        f.refresh();
        for (int sign : {-1, 1}) {
            set_pitch(f, 2, (q15)(sign * 32767));
            for (int i = 0; i < 5; ++i) {
                f.tap(KEY_L | KEY_DUP);
                CHECK(g_ui_snapshot.sequence_pitch[2][0] == sign * 32767);
                CHECK(selected_pitch() == (sign < 0 ? "r+24" : "+24"));
                check_hero(selected_pitch(), false);
            }
        }
    }
}
void test_stranular_pitch() {
    Fixture f; delete f.engine->replace_unit(3, 0, new StranularUnit); f.refresh();
    f.mode(1); f.touch(3, 0); set_pitch(f, 3, 0); CHECK(selected_pitch() == "+0");
    set_pitch(f, 3, -32767); CHECK(selected_pitch() == "-24");
    for (int semi = -24; semi <= 24; ++semi) {
        char expected[12]; std::snprintf(expected, sizeof expected, "%+d", semi);
        CHECK(selected_pitch() == expected);
        draw(); CHECK(has_text(expected));
        draw(true); CHECK(has_text("semitones"));
        CHECK(!has_text("semitones / r = reverse"));
        CHECK(outside_rects == 0 && text_overlaps == 0);
        check_hero(expected, false);
        if (semi < 24) f.tap(KEY_L | KEY_DUP);
    }
    f.tap(KEY_L | KEY_DUP); CHECK(selected_pitch() == "+24");
    for (int semi = 24; semi >= -24; --semi) {
        char expected[12]; std::snprintf(expected, sizeof expected, "%+d", semi);
        CHECK(selected_pitch() == expected);
        if (semi > -24) f.tap(KEY_L | KEY_DDOWN);
    }
    f.tap(KEY_L | KEY_DDOWN); CHECK(selected_pitch() == "-24");
    for (int x = layout::VALUE_X0; x <= layout::VALUE_X1; ++x) {
        f.frame(); sequencer_view_touch_down(x, 215); f.release();
        const int semi = -24 + ((x - layout::VALUE_X0) * 48 + layout::VALUE_TRAVEL / 2) / layout::VALUE_TRAVEL;
        CHECK(g_ui_snapshot.sequence_pitch[3][0] == semi * 32767 / 24);
        char expected[12]; std::snprintf(expected, sizeof expected, "%+d", semi);
        CHECK(selected_pitch() == expected);
    }
}
void test_lock_target_bounds() {
    Fixture f; f.mode(1); f.touch(1, 0); f.mode(6);
    std::vector<std::pair<int,int>> valid;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot)
        if (const Unit* unit = f.engine->chains[1].units[slot])
            for (int param = 0; param < unit->num_params; ++param) valid.push_back({slot,param});
    CHECK(valid.size() > 1);
    for (size_t i = 0; i < valid.size() * 3; ++i) {
        f.engine->clear_step_locks(1, 0); f.refresh();
        f.frame(); sequencer_view_touch_down(270, 215); f.release();
        const auto& step = f.engine->pattern.steps[1][0]; CHECK(step.lock_count == 1);
        const auto expected = valid[i % valid.size()];
        CHECK(step.locks[0].slot == expected.first && step.locks[0].param == expected.second);
        CHECK(step.locks[0].param < f.engine->chains[1].units[step.locks[0].slot]->num_params);
        f.tap(KEY_A);
    }
}
void check_plain_cell(const std::vector<u32>& image, int chain, int step, const std::string& label,
                      bool selected, bool in_length = true) {
    // Full current 18x31 native cell, including outside-badge details. This
    // replaces the obsolete 17x17 selected plate oracle, not a pixel waiver.
    std::array<u32, 18 * 31> expected;
    for (int y=0;y<31;++y)for(int x=0;x<18;++x)
        expected[y*18+x]=theme_test::paper(layout::ROWS[chain]+y);
    const u32 ink=in_length?theme_test::ink():theme_test::branch();
    for(int y=0;y<18;++y){const int cut=y==0||y==17?2:y==1||y==16?1:0;
        for(int x=cut;x<18-cut;++x)expected[y*18+x]=col_cell();}
    auto text=[&](const std::string& value,int y0,int height){
        std::vector<std::pair<int,int>> dots;
        int left=100,right=-1,top=8,bottom=-1;
        for(size_t c=0;c<value.size();++c)for(int y=0;y<8;++y)for(int x=0;x<6;++x)
            if(trackr::ui::glyph_row(value[c],y)&(1u<<(5-x))){const int xx=int(c)*6+x;
                dots.emplace_back(xx,y);left=std::min(left,xx);right=std::max(right,xx);top=std::min(top,y);bottom=std::max(bottom,y);}
        CHECK(!dots.empty()&&right-left+1<=18);
        for(const auto dot:dots){const int x=(18-(right-left+1))/2+dot.first-left;
            const int y=y0+(height-(bottom-top+1))/2+dot.second-top;
            CHECK(x>=0&&x<18&&y>=0&&y<31);expected[y*18+x]=ink;}
    };
    const size_t split=label.find_first_of("0123456789");
    text(split==std::string::npos?label:label.substr(0,split),0,18);
    if(split!=std::string::npos)text(label.substr(split),23,8);
    if(selected)for(int y=0;y<18;++y)for(int x=0;x<18;++x)
        if(((y==0||y==17)&&(x<4||x>=14))||((x==0||x==17)&&(y<4||y>=14)))expected[y*18+x]=theme_test::spot();
    const int x0=layout::X0+step*layout::PITCH,y0=layout::ROWS[chain];
    for(int y=0;y<31;++y)for(int x=0;x<18;++x){
        if(image[(y0+y)*320+x0+x]!=expected[y*18+x])
            std::fprintf(stderr,"cell %d:%d %s at %d,%d got %08x expected %08x\n",chain,step,label.c_str(),x,y,image[(y0+y)*320+x0+x],expected[y*18+x]);
        CHECK(image[(y0+y)*320+x0+x]==expected[y*18+x]);
    }
}
void test_cut_square_cells() {
    Fixture f; f.mode(1);
    for (int semi = 0; semi <= 72; ++semi) {
        set_pitch(f, 0, (q15)(semi * 32767 / 72));
        check_plain_cell(draw(), 0, 0, selected_pitch(), true);
    }
    const std::string note = selected_pitch();
    f.touch(1, 0); check_plain_cell(draw(), 0, 0, note, false);
    f.touch(2, 0);
    for (int value : {-32767, -8192, -1, 0, 1, 8192, 32767}) {
        set_pitch(f, 2, (q15)value);
        check_plain_cell(draw(), 2, 0, selected_pitch(), true);
        check_hero(selected_pitch(), false);
    }
    // The external octave and clipped corners do not shrink the 29px touch band.
    const auto original = f.hash();
    for (int chain = 0; chain < 4; ++chain) for (int step : {0, 7, 15})
        for (int dy : {0, 16, 20, 28}) {
            f.frame(); sequencer_view_touch_down(layout::X0 + step * layout::PITCH, layout::ROWS[chain] + dy);
            f.release(); CHECK(sequencer_selected_chain() == chain && sequencer_selected_step() == step);
        }
    CHECK(f.hash() == original);
    f.mode(0);
    f.frame(); sequencer_view_touch_down(layout::X0 + 3 * layout::PITCH, layout::ROWS[0] + 28);
    f.release(); CHECK(f.trig(0, 3));
}
void test_note_font_data() {
    for (const auto& glyph : note_display::glyphs) {
        CHECK(glyph.width && glyph.width <= 32 && glyph.height && glyph.height <= 50);
        CHECK((size_t)(glyph.offset + ((glyph.width + 7) / 8) * glyph.height) <= sizeof(note_display::bitmap));
    }
    width = 400;
    auto clear = [&] { pixels.assign(400 * HEIGHT, col_bg()); objects = 0; texts.clear(); };
    for (char letter = 'a'; letter <= 'g'; ++letter) for (char octave = '0'; octave <= '9'; ++octave)
        for (bool sharp : {false, true}) {
            std::string label(1, letter); if (sharp) label += '#'; label += octave;
            clear(); CHECK(note_display::melodic(label.c_str(), col_text(), col_on()));
            CHECK(objects > 20 && objects < 200);
            for (int y = 0; y < HEIGHT; ++y) for (int x = 0; x < 400; ++x)
                if (pixels[y*400+x] != col_bg()) CHECK(x >= 8 && x < 107 && y >= 44 && y < 111);
        }
    for (int semitone = -72; semitone <= 24; ++semitone) for (bool reverse : {false,true}) {
        char label[20]; std::snprintf(label, sizeof label, "%s%+d", reverse ? "r" : "", semitone);
        clear(); CHECK(note_display::semitones(label,col_text(),col_on()));
        CHECK(objects > 0 && objects < 300);
        for (int y=0;y<HEIGHT;++y) for(int x=0;x<400;++x)
            if(pixels[y*400+x]!=col_bg()) CHECK(x>=8&&x<107&&y>=47&&y<89);
    }
    for (const char* invalid : {"", "h3", "a#", "a", "a123", "--", "+", "r", "r+123", "r-a", "+0x"}) {
        clear(); CHECK(!note_display::melodic(invalid,col_text(),col_on())); CHECK(objects==0);
        CHECK(!note_display::semitones(invalid,col_text(),col_on())); CHECK(objects==0);
    }
    CHECK(!note_display::melodic(nullptr,0,0)); CHECK(!note_display::semitones(nullptr,0,0));
}
void test_magnetic_pointer() {
    seq_ring::State s;
    s.show(100); s.sample(0,0,100);
    CHECK(s.candidate==-1 && s.x==layout::RING_X && s.y==layout::RING_Y);
    s.sample(100,0,110); CHECK(s.candidate==-1); // 20.6px outside capture
    s.sample(110,0,120); CHECK(s.candidate==2 && s.x!=layout::SPOKE_X[2]);
    s.sample(100,0,130); CHECK(s.candidate==2); // held by 28px exit radius
    s.update(240); CHECK(s.x==layout::SPOKE_X[2] && s.y==layout::SPOKE_Y[2]);
    CHECK(s.opacity==1 && s.pulse==0); // settles once, never loops
    s.sample(80,0,250); CHECK(s.candidate==-1);
    s.sample(100,0,260); CHECK(s.candidate==-1); // no boundary chatter
    s.sample(0,0,270); s.select(7,270); s.sample(2,-2,280);
    CHECK(s.candidate==7 && s.pinned);
    s.sample(40,0,290); CHECK(s.candidate==-1 && !s.pinned);
    s.sample(32767,-32768,300); s.update(400);
    CHECK(seq_ring::square(s.x-layout::RING_X)+seq_ring::square(s.y-layout::RING_Y)<=84*84+.1f);
    s.reset(); CHECK(!s.visible && !s.pinned && s.candidate==-1 && s.opacity==0);
    // Full cardinal and diagonal hardware travel still reaches all eight nodes.
    for(int mode=0;mode<8;++mode) {
        const int x=layout::SPOKE_X[mode]-layout::RING_X, y=layout::RING_Y-layout::SPOKE_Y[mode];
        s.reset(); s.sample(x==0?0:x>0?156:-156,y==0?0:y>0?156:-156,500);
        CHECK(s.candidate==mode);
    }
}
void test_radial_layout() {
    struct Restore {
        palette::Theme theme=palette::current(); uint64_t now=host_time_ms;
        ~Restore(){palette::set(theme);host_time_ms=now;}
    } restore;
    for(int i=0;i<8;++i) {
        const int dx=layout::SPOKE_X[i]-layout::RING_X,dy=layout::SPOKE_Y[i]-layout::RING_Y;
        CHECK(std::abs(dx*dx+dy*dy-72*72)<=18);
        for(int j=i+1;j<8;++j) CHECK(
            layout::MODE_X[i]+layout::MODE_W<=layout::MODE_X[j] ||
            layout::MODE_X[j]+layout::MODE_W<=layout::MODE_X[i] ||
            layout::MODE_Y[i]+layout::MODE_H<=layout::MODE_Y[j] ||
            layout::MODE_Y[j]+layout::MODE_H<=layout::MODE_Y[i]);
    }
    // In-memory bounds/palette checks only; no image files or render campaign.
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}) {
        palette::set(theme); Fixture f; f.dense(); f.mode(1);
        for (int mode=0;mode<8;++mode) {
            f.r_hold();
            sequencer_view_touch_down(layout::SPOKE_X[mode], layout::SPOKE_Y[mode]);
            host_time_ms+=160; f.frame(0,KEY_R); // idle pad cannot erase touch
            const auto image=draw();
            CHECK(has_text("circle pad / release r") && !has_text("edit mode"));
            CHECK(outside_rects==0 && outside_lines==0 && text_overlaps==0);
            const char* names[]={"trig","note","level","retrig","when","time","lock","slice"};
            const int fill_x=layout::SPOKE_X[mode]-ui::text_w(names[mode])/2-3;
            CHECK(image[(layout::mode_label_y(mode)-2)*320+fill_x]==theme_test::spot());
            CHECK(image[layout::SPOKE_Y[mode]*320+layout::SPOKE_X[mode]]==theme_test::spot());
            check_dual_budget();
            f.release(); f.frame(0,0,KEY_R); CHECK(sequencer_selected_layer()==mode);
        }
        const auto original=f.hash(); f.r_hold();
        sequencer_view_touch_down(layout::RING_X,layout::RING_Y); f.release(); f.frame(0,0,KEY_R);
        CHECK(sequencer_selected_layer()==7 && f.hash()==original);
    }
}
void test_render() {
    Fixture f; f.dense();
    CHECK((SequenceLifecycle)g_ui_snapshot.sequence_lifecycle[0] == SequenceLifecycle::Strike);
    CHECK((SequenceLifecycle)g_ui_snapshot.sequence_lifecycle[2] == SequenceLifecycle::Loop);
    CHECK((SequenceLifecycle)g_ui_snapshot.sequence_lifecycle[3] == SequenceLifecycle::Flow);
    f.mode(1); f.touch(2, 0); char label[16]; sequencer_selected_pitch_text(label, sizeof label); CHECK(std::string(label) == "r-24");
    const auto original = f.hash();
    for (int mode = 0; mode < 7; ++mode) {
        f.mode(mode); const auto first = draw(); CHECK(draw() == first); CHECK(outside_rects == 0);
        check_dual_budget();
        // All modes must expose changing flow data, not reuse a fixed empty lane.
        const auto before = draw();
        f.engine->set_step_lock(3, 4, 0, OscUnit::P_GAIN, 12345); f.refresh();
        if (mode == 0 || mode == 2) CHECK(draw() != before);
        f.engine->set_step_lock(3, 4, 0, OscUnit::P_GAIN, 6000); f.refresh();
    }
    CHECK(f.hash() == original);
}
void test_menu_paper_and_distinct_modes() {
    Fixture f; f.dense();
    std::array<uint64_t, 7> signatures{};
    for (int mode = 0; mode < 7; ++mode) {
        f.mode(mode);
        const auto bottom = draw();
        for (int y = 0; y < 240; ++y) CHECK(bottom[y * 320 + 319] == m::paper_color(y));
        uint64_t hash = 1469598103934665603ull;
        for (u32 pixel : bottom) hash = (hash ^ pixel) * 1099511628211ull;
        signatures[mode] = hash;
        const auto top = draw(true);
        for (int y = 0; y < 240; ++y) CHECK(top[y * 400 + 399] == m::paper_color(y));
        CHECK(has_text(sequencer_selected_layer_name()));
        CHECK(has_text("patch01") && has_text("120 bpm / 1.1 / stop"));
    }
    for (int a = 0; a < 7; ++a) for (int b = a + 1; b < 7; ++b)
        CHECK(signatures[a] != signatures[b]);
    f.r_hold(); draw(); CHECK(has_text("circle pad / release r"));
    static const char* names[] = {"trig","note","level","retrig","when","time","lock"};
    for (int i = 0; i < 7; ++i) CHECK(has_text(names[i]));
}
void test_silent_step_metadata() {
    Fixture f;
    // Editing silent steps prepares events; their values must not disappear.
    for (int mode : {3,4,5}) {
        f.mode(mode);
        const auto before = draw();
        if (mode==3) f.engine->pattern.steps[0][4].set_retrig_count(4);
        if (mode==4) f.engine->pattern.steps[0][4].set_condition(SeqStep::Condition::NotFill);
        if (mode==5) f.engine->pattern.microtiming[0][4]=23;
        f.refresh();
        CHECK(!f.trig(0,4)); CHECK(draw()!=before); check_dual_budget();
    }
}
void test_note_audition_controls() {
    Fixture f; f.mode(1);
    auto& pitch = f.engine->chains[0].units[0]->params[FmUnit::P_PITCH];
    const q15 base = pitch.base;
    const auto flags = f.engine->pattern.steps[0][0].flags;
    f.tap(KEY_A);
    CHECK(pitch.audition_override);
    CHECK(pitch.audition == f.engine->pattern.steps[0][0].locks[0].value);
    CHECK(pitch.base == base && !f.engine->transport_playing);
    CHECK(f.engine->pattern.steps[0][0].flags == flags);
    // Audible FM output, not merely a UI marker or a dirty base parameter.
    q15 audio[1024] = {}; f.engine->render(audio, 512);
    CHECK(std::any_of(std::begin(audio), std::end(audio), [](q15 v) { return v != 0; }));
    // Multiple button edits before a worker snapshot must all advance.
    const q15 previous = pitch.audition;
    sequencer_view_buttons(KEY_A, KEY_A, 0);
    sequencer_view_buttons(KEY_A, KEY_A, 0);
    CHECK(pitch.audition > previous + 450);
    f.refresh(); f.frame();
    sequencer_view_touch_down(180, 215);
    const q15 at_touch = pitch.audition;
    sequencer_view_touch_hold(230, 215);
    CHECK(pitch.audition > at_touch);
    const int changes = mutations;
    const auto hash = f.hash();
    sequencer_view_touch_hold(230, 215);
    CHECK(mutations == changes && f.hash() == hash);
    f.release(); CHECK(pitch.base == base);
    sequencer_view_cancel_input(); CHECK(!pitch.audition_override);
    f.frame(); f.tap(KEY_A); CHECK(pitch.audition_override);
    f.mode(2); CHECK(!pitch.audition_override);
    f.mode(1); f.tap(KEY_A); CHECK(pitch.audition_override);
    f.tap(KEY_Y); CHECK(pitch.audition_override); // copying is read-only
    f.tap(KEY_Y); CHECK(!pitch.audition_override); // pasting cancels stale preview
    f.tap(KEY_A); CHECK(pitch.audition_override);
    f.tap(KEY_X); CHECK(!pitch.audition_override);
    // Selection stays silent; existing four locks must never be stolen for pitch.
    f.engine->clear_step_locks(0, 0);
    for (int p : {FmUnit::P_RATIO, FmUnit::P_INDEX, FmUnit::P_FEEDBACK, FmUnit::P_GAIN})
        CHECK(f.engine->set_step_lock(0, 0, 0, p, 5000));
    f.refresh(); f.mode(1);
    const auto full = f.hash(); f.tap(KEY_A);
    CHECK(f.hash() == full && !pitch.audition_override && g_seq_lock_flash > 0);
    f.touch(0, 2); CHECK(!pitch.audition_override);
    f.engine->chains[0].mute = true; f.tap(KEY_A);
    CHECK(!pitch.audition_override && !f.engine->transport_playing);
}
void test_lock_browser_and_snapshot() {
    Fixture f;
    f.engine->chains[0].units[2] = new FilterUnit;
    f.engine->chains[0].units[5] = new StrainUnit;
    f.refresh(); f.mode(6);
    const auto original = f.hash();
    f.tap(KEY_L | KEY_A); sequencer_capture_inspector();
    CHECK(sequencer_inspector().slot == 2 && sequencer_inspector().param == 0);
    f.tap(KEY_L | KEY_B); sequencer_capture_inspector(); CHECK(sequencer_inspector().slot == 0);
    // Previous parameter on touch stays in this module, wraps its real count.
    f.frame(); sequencer_view_touch_down(80, 215); f.release(); sequencer_capture_inspector();
    CHECK(sequencer_inspector().slot == 0);
    CHECK(sequencer_inspector().param == f.engine->chains[0].units[0]->num_params - 1);
    CHECK(f.hash() == original); check_dual_budget();
    f.frame(); sequencer_view_touch_down(130, 215); f.release();
    sequencer_capture_inspector(); CHECK(sequencer_inspector().param == 0);
    CHECK(f.engine->set_step_lock(0, 0, 2, FilterUnit::P_CUTOFF, 17000));
    CHECK(f.engine->set_step_lock(0, 0, 5, StrainUnit::P_BIAS, -12000));
    f.refresh(); const auto locked = f.hash();
    f.tap(KEY_L | KEY_Y); sequencer_capture_inspector();
    CHECK(sequencer_inspector().slot == 2 && sequencer_inspector().param == FilterUnit::P_CUTOFF);
    f.tap(KEY_L | KEY_Y); sequencer_capture_inspector();
    CHECK(sequencer_inspector().slot == 5 && sequencer_inspector().param == StrainUnit::P_BIAS);
    CHECK(f.hash() == locked);
    f.tap(KEY_L | KEY_X);
    CHECK(f.engine->pattern.steps[0][0].lock_count == 1);
    CHECK(f.engine->pattern.steps[0][0].locks[0].slot == 2);
    CHECK(f.engine->pattern.steps[0][0].flags == 0);
    f.tap(KEY_L | KEY_X); CHECK(f.engine->pattern.steps[0][0].lock_count == 1);
    f.tap(KEY_L | KEY_Y); sequencer_capture_inspector(); CHECK(sequencer_inspector().slot == 2);
    // Same-frame browser chords own touch, so no accidental value write.
    const auto before = f.hash(); f.frame(KEY_L | KEY_A, KEY_L | KEY_A);
    sequencer_view_touch_down(300, 215); f.release(); CHECK(f.hash() == before);
    f.frame(0, 0, KEY_L | KEY_A);
    draw(true); CHECK(has_text("fm") && has_text("out"));
    CHECK(text_overlaps == 0 && outside_rects == 0);
    // Frozen controller DTO renders without live engine access or mutation.
    Engine* engine = g_eng; g_eng = nullptr; top_draw(); sequencer_view_draw(); g_eng = engine;
    // Stale/deleted targets normalize safely, no dangling pointers in the DTO.
    delete f.engine->chains[0].units[5]; f.engine->chains[0].units[5] = nullptr;
    f.refresh(); sequencer_capture_inspector(); CHECK(sequencer_inspector().slot == 0);
    check_dual_budget();
}
void test_level_targets_and_inspector_density() {
    Fixture f;
    delete f.engine->chains[3].units[0]; f.engine->chains[3].units[0] = new GrainUnit;
    f.refresh(); f.mode(2); f.touch(3, 0);
    CHECK(g_ui_snapshot.sequence_value_param[3] == GrainUnit::P_DENS);
    const q15 base = f.engine->chains[3].units[0]->params[GrainUnit::P_DENS].base;
    f.frame(); sequencer_view_touch_down(315, 215); f.release();
    CHECK(f.engine->pattern.steps[3][0].locks[0].param == GrainUnit::P_DENS);
    CHECK(f.engine->chains[3].units[0]->params[GrainUnit::P_DENS].base == base);
    draw(true); CHECK(has_text("level > 1 GRN / dens"));
    draw(); CHECK(has_text("100%")); check_dual_budget();
    // Worst route width, two parameter pages, maximum signs and all four locks.
    for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
        delete f.engine->chains[3].units[s]; f.engine->chains[3].units[s] = new StrainUnit;
        f.engine->chains[3].units[s]->bypass = s % 2;
    }
    f.engine->clear_step_locks(3, 0);
    for (int s = 0; s < SEQ_LOCKS; ++s) CHECK(f.engine->set_step_lock(3, 0, s, StrainUnit::P_BIAS, -32767));
    f.refresh(); f.mode(6); check_dual_budget();
    for (int i = 0; i < 40; ++i) { f.tap(KEY_A); check_dual_budget(); }
    for (int s = 0; s < UNITS_PER_CHAIN; ++s) { delete f.engine->chains[3].units[s]; f.engine->chains[3].units[s] = nullptr; }
    f.refresh(); draw(true); CHECK(has_text("no module parameters")); check_dual_budget();
}
void test_hold_l_companion() {
    Fixture f;
    for (int mode = 0; mode < 7; ++mode) {
        f.mode(mode); const auto before = f.hash(); const int dirty = mutations;
        const auto lower = draw(); const auto upper = draw(true);
        f.frame(KEY_L, KEY_L); auto with_help = draw(true);
        CHECK(has_text("l / hold shortcuts") && has_text("play / stop"));
        CHECK(has_text(mode == 1 ? "pitch / hear" : "value"));
        CHECK(has_text("remove this lock") == (mode == 6));
        // Help owns the approved right-hand context region; the profile and its
        // captions beneath are replaced, while route/header/step identity stay.
        for (int y = 0; y < 240; ++y) for (int x = 0; x < 400; ++x)
            if (!(x >= 204 && x < 394 && y >= 47 && y < 215))
                CHECK(upper[y * 400 + x] == with_help[y * 400 + x]);
        const auto bottom = draw();
        for (int y = 0; y < 228; ++y) for (int x = 0; x < 320; ++x)
            CHECK(bottom[y * 320 + x] == lower[y * 320 + x]);
        CHECK(f.hash() == before && mutations == dirty);
        check_dual_budget();
        f.frame(KEY_DRIGHT, KEY_L | KEY_DRIGHT); CHECK(sequencer_selected_step() == 16);
        f.frame(KEY_DLEFT, KEY_L | KEY_DLEFT); CHECK(sequencer_selected_step() == 0);
        if (mode == 1) {
            f.frame(KEY_DUP, KEY_L | KEY_DUP);
            CHECK(f.engine->chains[0].units[0]->params[FmUnit::P_PITCH].audition_override);
        }
        f.frame(0, 0, KEY_L); draw(true); CHECK(!has_text("l / hold shortcuts"));
        f.frame(KEY_L, KEY_L); sequencer_view_cancel_input(); draw(true);
        CHECK(!has_text("l / hold shortcuts")); f.frame(0, 0, KEY_L);
        f.frame(KEY_L | KEY_R, KEY_L | KEY_R); draw(true);
        CHECK(!has_text("l / hold shortcuts"));
        f.frame(KEY_B, KEY_L | KEY_R | KEY_B); f.frame(0, 0, KEY_L | KEY_R | KEY_B);
    }
    f.mode(6); f.engine->chains[0].units[2] = new FilterUnit;
    f.refresh(); f.frame(KEY_L, KEY_L); f.frame(KEY_A, KEY_L | KEY_A);
    sequencer_capture_inspector(); CHECK(sequencer_inspector().slot == 2 && sequencer_inspector().show_l_help);
    draw(true); CHECK(has_text("l / hold shortcuts"));
    f.frame(0, 0, KEY_L | KEY_A); draw(true); CHECK(has_text("3 FLT / 1-4"));
}
void test_profile_stored_flow() {
    Fixture f; f.mode(1); f.touch(3, 3);
    auto* osc = f.engine->chains[3].units[0];
    osc->params[OscUnit::P_PITCH].base = 8192;
    CHECK(f.engine->set_step_lock(3, 3, 0, OscUnit::P_PITCH, 24575));
    CHECK(f.engine->set_step_lock(3, 5, 0, OscUnit::P_PITCH, 16384));
    f.engine->pattern.steps[3][5].set_condition(SeqStep::Condition::Half);
    f.refresh(); sequencer_capture_inspector();
    auto profile = seq_profile::capture(g_ui_snapshot, sequencer_inspector(), 1);
    CHECK(profile.pitch && profile.available && profile.playhead == -1);
    CHECK(profile.selected == 3 && profile.first == 0);
    CHECK(profile.points[3].locked && !profile.points[3].trigger);
    CHECK(profile.points[3].y < profile.base_y);
    CHECK(!profile.points[4].locked && profile.points[4].y == profile.base_y);
    CHECK(profile.points[5].conditional && profile.points[5].locked);
    // Stored profile ignores the currently sounding/modulated pitch.
    osc->params[OscUnit::P_PITCH].effective = 32767;
    sequencer_capture_inspector();
    const auto unchanged = seq_profile::capture(g_ui_snapshot, sequencer_inspector(), 1);
    CHECK(unchanged.points[3].y == profile.points[3].y && unchanged.base_y == profile.base_y);
    draw(true); CHECK(has_text("one step / then base")); check_dual_budget();
    // The visible lock mark exists only on the edited flow cell.
    auto image = draw();
    CHECK(image[(layout::ROWS[3] + 2) * 320 + layout::X0 + 3 * layout::PITCH + 11] == m::ink());
    CHECK(image[(layout::ROWS[3] + 2) * 320 + layout::X0 + 4 * layout::PITCH + 11] == col_cell());
    // Real scheduler: a lock-only note changes pitch, no held-until-next-point semantics.
    f.engine->set_bpm(120); f.engine->set_sequencer_enabled(true); f.engine->transport_reset(); f.engine->transport_play();
    std::array<q15, 512> audio{};
    for (int n = 0; n < 64; ++n) { f.engine->render(audio.data(), 256); f.refresh();
        if (g_ui_snapshot.sequence_step[3] == 3) break;
    }
    CHECK(osc->params[OscUnit::P_PITCH].step_lock_override);
    CHECK(osc->params[OscUnit::P_PITCH].step_lock == 24575);
    for (int n = 0; n < 32; ++n) { f.engine->render(audio.data(), 256); f.refresh();
        if (g_ui_snapshot.sequence_step[3] == 4) break;
    }
    CHECK(!osc->params[OscUnit::P_PITCH].step_lock_override);
    CHECK(osc->params[OscUnit::P_PITCH].control_source() == 8192);
    CHECK(!f.trig(3, 3));
    sequencer_capture_inspector(); profile = seq_profile::capture(g_ui_snapshot, sequencer_inspector(), 1);
    CHECK(profile.playhead == 4 && profile.selected == 3);
    f.engine->transport_stop(); f.engine->set_sequence_length(3, 4); f.refresh();
    sequencer_capture_inspector(); profile = seq_profile::capture(g_ui_snapshot, sequencer_inspector(), 1);
    CHECK(profile.points[3].active && !profile.points[4].active && profile.playhead == -1);
    f.tap(KEY_L | KEY_DRIGHT); sequencer_capture_inspector();
    profile = seq_profile::capture(g_ui_snapshot, sequencer_inspector(), 1);
    CHECK(profile.first == 16 && profile.selected == 3 && !profile.points[0].active);
    delete f.engine->replace_unit(3, 0, new NoiseUnit); f.refresh();
    draw(true); CHECK(has_text("no pitch parameter")); CHECK(!has_text("note mode / edit to hear"));
    check_dual_budget();
}
void test_diagonal_when_and_local_play_length() {
    Fixture f; f.mode(4);
    for (int c = 0; c < NUM_CHAINS; ++c) {
        for (int i = 0; i < 3; ++i) {
            const auto cond = i == 0 ? SeqStep::Condition::Always : i == 1 ? SeqStep::Condition::FirstOfTwo : SeqStep::Condition::SecondOfTwo;
            f.engine->pattern.steps[c][i].flags = SeqStep::Trig;
            f.engine->pattern.steps[c][i].set_condition(cond);
        }
    }
    f.refresh(); const auto image = draw();
    for (int c = 0; c < NUM_CHAINS; ++c) for (int i = 0; i < 3; ++i) {
        const int x = layout::X0 + layout::PITCH * i, y = layout::ROWS[c];
        const u32 ink = col_text(); // selection is corners, never inverse native glyphs
        for (int yy = 0; yy < 7; ++yy) for (int xx = 0; xx < 5; ++xx) {
            if (trackr::ui::glyph_row(i == 2 ? '2' : '1', yy) & (1 << (5 - xx)))
                CHECK(image[(y + 1 + yy) * 320 + x + 2 + xx] == ink);
            if (trackr::ui::glyph_row(i == 0 ? '1' : '2', yy) & (1 << (5 - xx)))
                CHECK(image[(y + 9 + yy) * 320 + x + 10 + xx] == ink);
        }
        CHECK(image[(y + 5) * 320 + x + 10] == ink);
        CHECK(image[(y + 11) * 320 + x + 6] == ink);
    }
    check_dual_budget();
    f.mode(0); f.touch(0, 10);
    for (int s = 0; s < 16; ++s) f.engine->pattern.steps[2][s].flags = SeqStep::Trig;
    for (int length : {0, 16384, 32767}) {
        f.engine->chains[2].units[0]->params[PlayUnit::P_LEN].base = (q15)length; f.refresh();
        const auto img = draw();
        for (int s = 0; s < 16; ++s) {
            const int x = layout::X0 + layout::PITCH * s, y = layout::ROWS[2];
            const int end = x + 4 + g_ui_snapshot.sequence_duration[2][s] * 10 / 255;
            CHECK(img[(y + 21) * 320 + x + 2] == col_text());
            CHECK(img[(y + 25) * 320 + end] == col_text());
            for (int xx = x; xx < x + 19 && xx < 320; ++xx) CHECK(img[(y + 32) * 320 + xx] == m::paper_color(y + 32));
        }
        check_dual_budget();
    }
}
struct SampleFixture {
    trackr::synth::Sample saved;
    trackr::synth::Sample& sample = trackr::synth::SampleBank::instance().slot(63);
    SampleFixture() { std::swap(saved, sample); sample.data.assign(32000, 1000); }
    ~SampleFixture() { std::swap(saved, sample); }
};
void test_play_companion_projection() {
    Fixture f; SampleFixture sf; auto* play = static_cast<PlayUnit*>(f.engine->chains[2].units[0]);
    play->sample_slot = 63; f.refresh(); f.mode(1); f.touch(2, 0);
    auto inspect = [&]() { f.refresh(); sequencer_capture_inspector(); return sequencer_inspector().play; };
    auto p = inspect(); CHECK(p.present && p.valid && p.milliseconds == 1000 && p.start == 0 && p.end == 32767);
    CHECK(p.loop && p.increment == 65536);
    play->params[PlayUnit::P_LOOP].base = 16384; CHECK(inspect().loop);
    play->params[PlayUnit::P_LOOP].base = 16383; CHECK(!inspect().loop);
    play->params[PlayUnit::P_LOOP].base = 32767;
    auto lock = [&](int param, q15 value) { CHECK(f.engine->set_step_lock(2, 0, 0, param, value)); };
    lock(PlayUnit::P_SPEED, -16384); p = inspect(); CHECK(p.increment == -131072 && p.milliseconds == 500);
    lock(PlayUnit::P_START, 16384); lock(PlayUnit::P_LEN, 8192); lock(PlayUnit::P_LOOP, 0);
    p = inspect(); CHECK(!p.loop && p.milliseconds == 125 && p.start > 16000 && p.end < 25000);
    lock(PlayUnit::P_START, -32767); lock(PlayUnit::P_LEN, -32767);
    p = inspect(); CHECK(p.valid && p.start == 0 && p.milliseconds == 0);
    f.engine->clear_step_locks(2, 0);
    sf.sample.chops[0] = 8000; sf.sample.chops[1] = 16000; sf.sample.slice_reverse_mask = 1;
    lock(PlayUnit::P_SLICE, 2000); p = inspect();
    CHECK(p.slice == 1 && p.increment == -65536 && p.milliseconds == 250);
    play->params[PlayUnit::P_SPEED].src = ModSource::SigilB; play->params[PlayUnit::P_SPEED].mod_depth = 17000;
    p = inspect(); CHECK(p.modulated); draw(true); CHECK(has_text("stored / mod may change"));
    play->params[PlayUnit::P_SPEED].src = ModSource::None;
    p = inspect(); const auto unchanged = p;
    play->params[PlayUnit::P_SPEED].effective = 32767; // live voice must not change stored estimate
    p = inspect(); CHECK(p.increment == unchanged.increment && p.milliseconds == unchanged.milliseconds);
    play->pitch_quantize = true; lock(PlayUnit::P_SPEED, 12000); p = inspect();
    CHECK(p.increment == play_window(sf.sample, 12000, 0, 32767, 2000, true).increment);
    check_dual_budget();
    f.frame(KEY_L, KEY_L); draw(true); CHECK(has_text("l / hold shortcuts") && !has_text("stored window / not gate"));
    f.frame(0, 0, KEY_L);
    sf.sample.logical_frames = 0; p = inspect(); CHECK(p.present && !p.valid);
    draw(true); CHECK(has_text("play / no sample")); check_dual_budget();
    play->sample_slot = 0;
}
void dump_surface(const std::filesystem::path& directory, const std::string& name) {
    std::ofstream out(directory / (name + ".ppm"), std::ios::binary);
    CHECK(out.good()); out << "P6\n" << width << " 240\n255\n";
    for (const u32 p : pixels) { const char rgb[] = {(char)p, (char)(p >> 8), (char)(p >> 16)}; out.write(rgb, 3); }
    CHECK(out.good());
    std::ofstream geometry(directory / (name + "_text.tsv"));
    geometry << "x\ty\tw\th\ttext\n";
    for (const auto& text : texts) geometry << text.x << '\t' << text.y << '\t' << text.w << '\t' << text.h << '\t' << text.text << '\n';
}
void dump_frame(const std::filesystem::path& directory, const std::string& name, std::ofstream& manifest) {
    draw(true); dump_surface(directory, name + "_top");
    const int top_objects = objects, top_clipped = outside_rects + outside_lines, top_overlap = text_overlaps;
    draw(); dump_surface(directory, name);
    maximum_shared_objects = std::max(maximum_shared_objects, top_objects + objects);
    CHECK(top_objects + objects < C2D_DEFAULT_MAX_OBJECTS * 2);
    manifest << name << '\t' << objects << '\t' << top_objects << '\t' << objects + top_objects
             << '\t' << outside_rects + outside_lines << '\t' << top_clipped
             << '\t' << text_overlaps << '\t' << top_overlap << '\n';
}
void dump_lens(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    Fixture f;
    delete f.engine->replace_unit(0, 0, new FilterUnit);
    open_lens(f);
    auto& p = f.engine->chains[0].units[0]->params[0];
    std::array<q15, 1024> out{};
    auto save = [&](const char* name) {
        draw_lens(true); dump_surface(directory, std::string(name) + "_top");
        draw_lens(false); dump_surface(directory, name);
    };
    p.base = 20315; p.src = ModSource::None; p.mod_depth = 0;
    f.engine->render(out.data(), 512); save("01_base");
    p.src = ModSource::SigilB; p.mod_depth = -24000;
    for (int i=0; i<SIGIL_POINTS; ++i) f.engine->sigils[1].table[i] = (q15)(i*512-32767);
    f.engine->sigils[1].drawn = true;
    f.engine->render(out.data(), 512); save("02_mod_clipped");
    p.src = ModSource::EnvFollow;
    f.engine->set_env_follow_source(1); f.engine->set_env_follow_response(EnvResponse::Body);
    for (int i=0; i<12; ++i) f.engine->render(out.data(), 512);
    save("03_env");
    lens_quick_begin();param_lens_touch_down(150,129);param_lens_touch_drag(126,129);
    const q15 edited=p.base;CHECK(!f.engine->param_temp_active());
    f.engine->render(out.data(),512);save("04_temp");
    param_lens_touch_release();lens_frame(0,0,KEY_L);CHECK(!param_lens_is_open()&&p.base==edited);
    CHECK(param_lens_touch_down(250,232));param_lens_touch_release();
    f.engine->render(out.data(),512);save("05_return");
    param_lens_cancel();
}
void dump(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    std::ofstream manifest(directory / "manifest.tsv");
    manifest << "frame\tobjects\ttop_objects\tshared_objects\tclipped_primitives\ttop_clipped_primitives\ttext_overlaps\ttop_text_overlaps\n";
    Fixture f; f.dense();
    for (int mode = 0; mode < 7; ++mode) {
        f.mode(mode); dump_frame(directory, std::to_string(mode) + "_" + sequencer_selected_layer_name(), manifest);
    }
    f.mode(1); f.touch(2, 0); dump_frame(directory, "7_reverse_pitch", manifest);
    f.mode(4); f.touch(0, 7); dump_frame(directory, "8_not_fill", manifest);
    f.r_hold(); dump_frame(directory, "9_mode_chooser", manifest); f.frame(0, 0, KEY_R);
    f.mode(1); f.tap(KEY_L | KEY_DLEFT); dump_frame(directory, "10_last_page", manifest);
    // Same selected flow chain with two actual engine snapshots, not hand-drawn curves.
    f.tap(KEY_L | KEY_DRIGHT); f.touch(3, 4); f.mode(2); dump_frame(directory, "11_flow_before", manifest);
    f.engine->set_step_lock(3, 4, 0, OscUnit::P_GAIN, 32767); f.refresh(); dump_frame(directory, "12_flow_after", manifest);
    f.mode(0); f.touch(0, 0); f.tap(KEY_START);
    std::array<q15, 1024> audio{}; f.engine->render(audio.data(), 512); f.refresh();
    dump_frame(directory, "13_reverse_playing", manifest); f.tap(KEY_START);
    for (int c = 0; c < NUM_CHAINS; ++c) f.engine->clear_track(c);
    f.refresh(); f.mode(1); dump_frame(directory, "14_empty_and_free_flow", manifest);
    delete f.engine->replace_unit(3, 0, new StranularUnit); f.refresh(); f.touch(3, 0);
    set_pitch(f, 3, 0); dump_frame(directory, "15_strn_zero", manifest);
    set_pitch(f, 3, -32767); dump_frame(directory, "16_strn_minus24", manifest);
    set_pitch(f, 3, -16383); dump_frame(directory, "17_strn_minus12", manifest);
    set_pitch(f, 3, 16383); dump_frame(directory, "18_strn_plus12", manifest);
    f.engine->set_step_lock(0, 0, 0, FmUnit::P_PITCH, (q15)(22 * 32767 / 72)); f.refresh(); f.touch(0, 0);
    dump_frame(directory, "19_sharp_note", manifest);
    // Normal musical density, in addition to the fully populated stress score.
    delete f.engine->replace_unit(3, 0, new OscUnit);
    for (int c=0;c<NUM_CHAINS;++c) {
        f.engine->clear_track(c); f.engine->set_sequence_length(c,16);
    }
    f.refresh();
    const int steps[3][6]={{0,3,6,9,12,14},{0,4,7,8,12,-1},{0,4,8,12,-1,-1}};
    for (int c=0;c<3;++c) for (int i=0;i<6 && steps[c][i]>=0;++i) {
        const int s=steps[c][i]; auto& event=f.engine->pattern.steps[c][s];
        event.flags=SeqStep::Trig;
        if (i==3) {event.set_retrig_count(3);event.set_condition(SeqStep::Condition::Half);}
        if (i==1) f.engine->pattern.microtiming[c][s]=-5;
        const int pitch=(c==2?8192:(24+i*2)*32767/72);
        f.engine->set_step_lock(c,s,g_ui_snapshot.sequence_pitch_slot[c],g_ui_snapshot.sequence_pitch_param[c],(q15)pitch);
        f.engine->set_step_lock(c,s,g_ui_snapshot.sequence_value_slot[c],g_ui_snapshot.sequence_value_param[c],(q15)(12000+i*3000));
    }
    for (int s=0;s<16;++s) f.engine->set_step_lock(3,s,0,OscUnit::P_GAIN,(q15)(7000+(s<8?s:15-s)*2600));
    f.refresh(); f.mode(1);f.touch(0,9);
    for (int mode=0;mode<7;++mode) {
        f.mode(mode);dump_frame(directory,"20_sparse_"+std::string(sequencer_selected_layer_name()),manifest);
    }
    f.engine->chains[0].units[1] = new FilterUnit;
    f.engine->chains[0].units[2] = new StrainUnit;
    auto& bias = f.engine->chains[0].units[2]->params[StrainUnit::P_BIAS];
    bias.src = ModSource::SigilA; bias.mod_depth = -16384;
    f.engine->set_step_lock(0, 9, 1, FilterUnit::P_CUTOFF, 21000);
    f.engine->set_step_lock(0, 9, 2, StrainUnit::P_BIAS, -12000);
    f.refresh(); f.mode(6); f.tap(KEY_L | KEY_Y); f.tap(KEY_L | KEY_Y);
    dump_frame(directory, "30_lock_filter", manifest);
    f.tap(KEY_L | KEY_Y); dump_frame(directory, "31_lock_strain", manifest);
    f.tap(KEY_L | KEY_A); for (int i = 0; i < 7; ++i) f.tap(KEY_A);
    dump_frame(directory, "32_lock_second_page", manifest);
    delete f.engine->chains[3].units[0]; f.engine->chains[3].units[0] = new NoiseUnit;
    f.refresh(); f.mode(0); dump_frame(directory, "33_noise_flow", manifest);
    for (int mode = 0; mode < 7; ++mode) {
        f.mode(mode); f.frame(KEY_L, KEY_L);
        dump_frame(directory, "40_hold_l_" + std::string(sequencer_selected_layer_name()), manifest);
        f.frame(0, 0, KEY_L);
    }
    // Actual stored lock-only flow pitches, including explicit base-return gaps.
    delete f.engine->replace_unit(3, 0, new OscUnit);
    f.engine->clear_track(3); f.engine->set_sequence_length(3, 16);
    f.engine->chains[3].units[0]->params[OscUnit::P_PITCH].base = (q15)(22 * 32767 / 72);
    for (int s : {2, 3, 6, 10, 14})
        f.engine->set_step_lock(3, s, 0, OscUnit::P_PITCH, (q15)((30 + s) * 32767 / 72));
    f.engine->pattern.steps[3][10].set_condition(SeqStep::Condition::Half);
    f.refresh(); f.mode(1); f.touch(3, 6);
    dump_frame(directory, "50_flow_pitch_points", manifest);
    f.tap(KEY_START); for (int i = 0; i < 32; ++i) f.engine->render(audio.data(), 512);
    f.refresh(); dump_frame(directory, "51_flow_pitch_playing", manifest); f.tap(KEY_START);
    SampleFixture sample;
    auto* play = static_cast<PlayUnit*>(f.engine->chains[2].units[0]); play->sample_slot = 63;
    f.engine->clear_step_locks(2, 0);
    f.engine->set_step_lock(2, 0, 0, PlayUnit::P_SPEED, -8192);
    f.engine->set_step_lock(2, 0, 0, PlayUnit::P_START, 8192);
    f.engine->set_step_lock(2, 0, 0, PlayUnit::P_LEN, 16384);
    f.refresh(); f.mode(0); f.touch(2, 0); dump_frame(directory, "52_play_window", manifest);
    sample.sample.chops[0] = 8000; sample.sample.chops[1] = 16000; sample.sample.slice_reverse_mask = 1;
    f.engine->set_step_lock(2, 0, 0, PlayUnit::P_SLICE, 2000);
    f.refresh(); f.mode(1); dump_frame(directory, "53_play_slice_reverse", manifest);
    f.frame(KEY_L, KEY_L); dump_frame(directory, "54_play_hold_l", manifest); f.frame(0, 0, KEY_L);
    std::printf("runtime frames: %s (software Citro2D backend; not hardware captures)\n", directory.string().c_str());
}
void test_ghost_controls() {
    Fixture f; delete f.engine->replace_unit(0,0,new GhostUnit);
    auto& g=*static_cast<GhostUnit*>(f.engine->chains[0].units[0]);
    g_edit_chain=g_edit_slot=0;g_seq_lock_armed=false;g_ui_snapshot.effect_focus_valid=false;
    g_view=View::Chains;g_overlay=Overlay::Editor;ghost_focus_cancel();
    auto tap=[&](int x,int y){g_drag=Drag::None;CHECK(ghost_focus_touch_body(g,x,y));};
    CHECK(g.long_tail);
    const int mode_before=mutations;
    const auto old_tone=g.params[0].base;
    tap(260,10);CHECK(!g.long_tail&&mutations==mode_before+1&&g_drag==Drag::None&&g.params[0].base==old_tone);
    tap(260,10);CHECK(g.long_tail);
    g_seq_lock_armed=true;tap(260,10);CHECK(g.long_tail);g_seq_lock_armed=false;
    g.params[4].base=32767;tap(260,10);CHECK(g.long_tail);g.params[4].base=0;
    g.params[4].effective=32767;tap(260,10);CHECK(g.long_tail);g.params[4].effective=0;
    auto& fine=g.params[GhostUnit::P_FINE];fine.base=123;
    const int before=mutations;tap(150,76);ghost_focus_drag(g,150,76);
    CHECK(fine.base==123&&mutations==before);
    ghost_focus_drag(g,151,76);CHECK(fine.base==187);
    ghost_focus_drag(g,150,76);CHECK(fine.base==123);
    tap(225,70);CHECK(fine.base==0);
    tap(100,220);ghost_focus_drag(g,286,220);CHECK(g.params[7].base==32767);
    tap(180,30);const q15 tone=g.params[0].base;
    const double n=69+12*std::log2(GhostUnit::root_millihz(tone,0)/440000.0);
    CHECK(std::abs(n-std::round(n))<0.003);
    tap(150,10);CHECK(g.params[0].base==tone); // free preference, no sound mutation
    const int tx=82+(uint32_t)tone*150/32767;
    tap(tx,54);ghost_focus_drag(g,tx+1,54);CHECK(g.params[0].base==tone+32767/150);
    tap(150,10); // restore note mode
    CHECK(f.engine->set_step_lock(0,0,0,GhostUnit::P_FINE,1000));
    g_seq_lock_armed=true;const q15 base=fine.base;
    tap(150,76);ghost_focus_drag(g,151,76);
    CHECK(fine.base==base&&seq_display_value(0,6,0)==1064);
    f.engine->clear_step_locks(0,0);
    for(int p=0;p<4;++p)CHECK(f.engine->set_step_lock(0,0,0,p,12000));
    const auto locks=pattern_hash(f.engine->pattern);
    tap(150,76);ghost_focus_drag(g,151,76);CHECK(pattern_hash(f.engine->pattern)==locks);
    g_seq_lock_armed=false;g.params[4].base=32767;
    for(auto xy:{std::pair<int,int>{150,76},{110,220},{180,30},{225,70}}){tap(xy.first,xy.second);CHECK(g_drag==Drag::None);}
    CHECK(fine.base==base);
    g.params[4].base=0;tap(150,76);g.params[4].base=32767;ghost_focus_drag(g,170,76);CHECK(fine.base==base);
    g.params[4].base=0;
    // Captured DTO must remain drawable after the engine/audio owners disappear.
    g_eng->capture_ui_snapshot(g_ui_snapshot,0,0);ghost_focus_capture();g_eng=nullptr;g_audio=nullptr;
    width=400;objects=0;texts.clear();pixels.assign(400*HEIGHT,0xff000000u);CHECK(ghost_focus_draw_top()&&objects>0);
    width=320;objects=0;texts.clear();pixels.assign(320*HEIGHT,0xff000000u);ghost_focus_draw(g);CHECK(objects>0);
    g_eng=f.engine.get();g_audio=&f.audio;
    // Invalid slot, wrong kind, removed unit, and lens ownership invalidate capture safely.
    g_edit_slot=7;ghost_focus_capture();CHECK(!ghost_focus_draw_top());g_edit_slot=0;
    Unit* old=f.engine->replace_unit(0,0,new ReverbUnit);ghost_focus_capture();CHECK(!ghost_focus_draw_top());delete f.engine->replace_unit(0,0,old);
    Unit* removed=f.engine->replace_unit(0,0,nullptr);ghost_focus_capture();CHECK(!ghost_focus_draw_top());delete f.engine->replace_unit(0,0,removed);
}
void test_ghost_menu_contract() {
    Fixture f;delete f.engine->replace_unit(0,0,new GhostUnit);auto& g=*static_cast<GhostUnit*>(f.engine->chains[0].units[0]);
    g_edit_chain=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;g_seq_lock_armed=false;ghost_focus_cancel();
    auto touch=[&](int x,int y){g_drag=Drag::None;return ghost_focus_touch_body(g,x,y);};
    // Exact unchanged header domains and adjacent gaps.
    CHECK(!touch(199,10));CHECK(touch(200,3)&&g.bypass);CHECK(touch(239,20)&&!g.bypass);CHECK(!touch(240,10));
    CHECK(!touch(125,10));CHECK(touch(126,0));CHECK(touch(186,21));CHECK(!touch(187,10));
    const bool tail=g.long_tail;CHECK(!touch(242,10));CHECK(touch(243,0)&&g.long_tail!=tail);CHECK(touch(280,21)&&g.long_tail==tail);CHECK(!touch(281,10));
    // The four memory wells and their visible contours are passive.
    const auto passive_hash=pattern_hash(f.engine->pattern);const int passive_dirty=mutations;
    for(int x:{106,142,178,214})CHECK(!touch(x,143));
    CHECK(pattern_hash(f.engine->pattern)==passive_hash&&mutations==passive_dirty);
    // Every real parameter target selects only that parameter; note/free and zero retain selection.
    auto at=[&](int p){const q15 v=g.params[p].base;int x=160,y=117;switch(p){
        case 0:x=82+(uint32_t)(uint16_t)v*150/32767;y=54;break;case 1:x=61;y=174-(uint32_t)(uint16_t)v*82/32767;break;
        case 2:x=111+(uint32_t)(uint16_t)v*114/32767;y=183;break;case 3:x=254;y=111-(uint32_t)(uint16_t)v*52/32767;break;
        case 5:x=294;y=181-(uint32_t)(uint16_t)v*65/32767;break;case 6:x=150+(int32_t)v*60/32767;y=76;break;
        case 7:x=64+(uint32_t)(uint16_t)v*186/32767;y=220;break;}return std::pair<int,int>{x,y};};
    for(int p=0;p<8;++p){auto xy=at(p);CHECK(touch(xy.first,xy.second));CHECK(ghost_focus_selected()==p);ghost_focus_release();}
    g.params[4].base=0;g.params[4].effective=0;
    auto xy=at(3);touch(xy.first,xy.second);ghost_focus_release();touch(126,10);CHECK(ghost_focus_selected()==3);g.params[6].base=1234;touch(213,62);CHECK(g.params[6].base==0&&ghost_focus_selected()==3);
    // Inline routing keeps A-D, signed depth and armed refusal semantics.
    xy=at(6);touch(xy.first,xy.second);ghost_focus_release();touch(274,198);
    for(int i=0;i<4;++i){CHECK(touch(10,43+i*14));CHECK(g.params[6].src==(ModSource)((int)ModSource::SigilA+i));}
    touch(157,220);ghost_focus_drag(g,64,220);CHECK(g.params[6].mod_depth==-32767);ghost_focus_release();touch(157,220);ghost_focus_drag(g,250,220);CHECK(g.params[6].mod_depth==32767);ghost_focus_release();
    const auto source=g.params[6].src;const q15 depth=g.params[6].mod_depth;g_seq_lock_armed=true;touch(10,43);touch(157,220);ghost_focus_drag(g,64,220);CHECK(g.params[6].src==source&&g.params[6].mod_depth==depth);g_seq_lock_armed=false;touch(274,198);
    // Requested -> transition -> full -> release -> off telemetry, with MIX still editable.
    std::array<q15,512> audio{};g.params[4].base=32767;g.params[4].snap();g.process(audio.data(),1);CHECK(g.visual_freeze()>0&&g.visual_freeze()<32767);g.process(audio.data(),512);CHECK(g.visual_freeze()==32767);
    const q15 decay=g.params[1].base;xy=at(1);touch(xy.first,xy.second);ghost_focus_drag(g,xy.first+40,xy.second);CHECK(g.params[1].base==decay);ghost_focus_release();
    g.params[5].base=16000;const q15 mix=g.params[5].base;xy=at(5);touch(xy.first,xy.second);CHECK(ghost_focus_selected()==5&&g_drag==Drag::GhostMix);ghost_focus_drag(g,xy.first,xy.second+20);CHECK(g.params[5].base!=mix);ghost_focus_release();
    g.params[4].base=0;g.params[4].snap();g.process(audio.data(),1);CHECK(g.visual_freeze()>0&&g.visual_freeze()<32767);g.process(audio.data(),512);CHECK(g.visual_freeze()==0);
    // Stored-origin lock, external replacement, return and full-bank refusal.
    g.params[6].base=500;CHECK(f.engine->set_step_lock(0,0,0,6,1000));g_seq_lock_armed=true;g_seq_lock_step=0;xy=at(6);touch(xy.first,xy.second);const auto stationary=pattern_hash(f.engine->pattern);ghost_focus_drag(g,xy.first,xy.second);CHECK(pattern_hash(f.engine->pattern)==stationary);ghost_focus_drag(g,xy.first+1,xy.second);CHECK(seq_display_value(0,6,0)==1064);CHECK(f.engine->set_step_lock(0,0,0,6,2000));const auto external=pattern_hash(f.engine->pattern);ghost_focus_drag(g,xy.first+1,xy.second);CHECK(pattern_hash(f.engine->pattern)==external);ghost_focus_drag(g,xy.first,xy.second);CHECK(seq_display_value(0,6,0)==1000);ghost_focus_release();
    f.engine->clear_step_locks(0,0);for(int p=0;p<4;++p)CHECK(f.engine->set_step_lock(0,0,0,p,1000));const auto full=pattern_hash(f.engine->pattern);touch(xy.first,xy.second);ghost_focus_drag(g,xy.first+1,xy.second);CHECK(pattern_hash(f.engine->pattern)==full&&g_seq_lock_flash>0);ghost_focus_release();g_seq_lock_armed=false;
    // Formatter authority remains the existing physical-unit mapping.
    g_eng->capture_ui_snapshot(g_ui_snapshot,0,0);ghost_focus_capture();char value[32];ghost_focus_value_text(value,sizeof value,6,32767);CHECK(std::string(value)=="+50.00c");ghost_focus_value_text(value,sizeof value,6,-32767);CHECK(std::string(value)=="-50.00c");ghost_focus_value_text(value,sizeof value,7,0);CHECK(std::string(value)=="2.5s");ghost_focus_value_text(value,sizeof value,7,32767);CHECK(std::string(value)=="120.0s");
    // Full six-slot path, invalid chain and open-lens ownership are bounded/cancelled.
    for(int s=1;s<UNITS_PER_CHAIN;++s)f.engine->chains[0].units[s]=make_unit((UnitKind)(1+s));
    g_eng->capture_ui_snapshot(g_ui_snapshot,0,0);ghost_focus_capture();width=400;objects=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);CHECK(ghost_focus_draw_top());
    g_edit_chain=NUM_CHAINS;ghost_focus_capture();CHECK(!ghost_focus_draw_top());g_edit_chain=0;CHECK(param_lens_touch_down(250,232));param_lens_touch_release();ghost_focus_capture();CHECK(!ghost_focus_draw_top());param_lens_cancel();
}
void ghost_menu_palette_and_paper();
void test_ghost_menu_lens_temp() {
    Fixture f;delete f.engine->replace_unit(0,0,new GhostUnit);auto& g=*static_cast<GhostUnit*>(f.engine->chains[0].units[0]);
    g_edit_chain=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;g_seq_lock_armed=false;ghost_focus_cancel();
    // Select DRIFT in the module, then prove lens ownership transfers that exact parameter.
    CHECK(ghost_focus_touch_body(g,254,85));ghost_focus_release();CHECK(ghost_focus_selected()==GhostUnit::P_DRIFT);
    CHECK(param_lens_touch_down(250,232));param_lens_touch_release();
    const q15 tone=g.params[0].base;
    param_lens_touch_down(280,185);param_lens_touch_release();CHECK(g.params[3].src==ModSource::EnvFollow&&g.params[0].src==ModSource::None);
    param_lens_touch_down(24,166);param_lens_touch_release();CHECK(g.params[3].mod_depth==-32767);
    for(int c=0;c<4;++c){param_lens_touch_down(85+c*32,212);param_lens_touch_release();CHECK(f.engine->env_follow_source()==c);}
    param_lens_capture();draw_lens(true);const int top=objects;ghost_menu_palette_and_paper();draw_lens(false);const int bottom=objects;ghost_menu_palette_and_paper();maximum_shared_objects=std::max(maximum_shared_objects,top+bottom);CHECK(top+bottom<8192);
    // Held modulation edits DRIFT permanently without corrupting TONE or its route.
    lens_permanent_probe(g,3,false);CHECK(ghost_focus_selected()==3&&g.params[0].base==tone);
    lens_quick_begin();param_lens_capture();draw_lens(true);ghost_menu_palette_and_paper();draw_lens(false);ghost_menu_palette_and_paper();
    lens_frame(0,0,KEY_L);CHECK(!param_lens_is_open());
    f.engine->chains[0].mute=true;CHECK(param_lens_touch_down(250,232));param_lens_touch_release();lens_permanent_probe(g,3,false);f.engine->chains[0].mute=false;
    // Replacement closes instead of silently retargeting the new module.
    lens_quick_begin();Unit* old=f.engine->replace_unit(0,0,new ReverbUnit);delete old;param_lens_capture();CHECK(!param_lens_is_open()&&!param_lens_draw_top());
    lens_frame(0,KEY_L);CHECK(!param_lens_is_open());lens_frame(0,0,KEY_L);
    CHECK(param_lens_touch_down(250,232));param_lens_touch_release();
    Unit* removed=f.engine->replace_unit(0,0,nullptr);delete removed;param_lens_capture();CHECK(!param_lens_is_open()&&!param_lens_draw_top());
}
void ghost_menu_palette_and_paper() {
    auto paper_row=[](int y) { return theme_test::paper(y); };
    const std::array<u32,4> inks={theme_test::ink(),theme_test::spot(),theme_test::ghost(),theme_test::branch()};
    CHECK(pixels.size()==(size_t)width*HEIGHT);
    for(int yy=0;yy<HEIGHT;++yy)for(int xx=0;xx<width;++xx) {
        const u32 px=pixels[yy*width+xx];
        CHECK(px==paper_row(yy)||std::find(inks.begin(),inks.end(),px)!=inks.end());
    }
    for(int yy=0;yy<HEIGHT;++yy)for(int xx=0;xx<2;++xx)CHECK(pixels[yy*width+xx]==paper_row(yy));
}
void ghost_render(GhostUnit& g, const std::filesystem::path* directory=nullptr, const char* name=nullptr,
                  std::ostream* manifest=nullptr) {
    g_view=View::Chains;g_overlay=Overlay::Editor;
    g_eng->capture_ui_snapshot(g_ui_snapshot,0,0);ghost_focus_capture();
    Engine* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
    width=400;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(400*HEIGHT,0xff000000u);
    CHECK(ghost_focus_draw_top());const int top_objects=objects;
    CHECK(objects>0&&objects<8192);CHECK(outside_rects==0&&outside_lines==0&&text_overlaps==0);ghost_menu_palette_and_paper();
    if(directory&&name)dump_surface(*directory,std::string(name)+"_top");
    width=320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(320*HEIGHT,0xff000000u);
    ghost_focus_draw(g);const int bottom_objects=objects;
    CHECK(objects>0&&objects<8192);CHECK(outside_rects==0&&outside_lines==0&&text_overlaps==0);ghost_menu_palette_and_paper();CHECK(top_objects+bottom_objects<8192);
    maximum_shared_objects=std::max(maximum_shared_objects,top_objects+bottom_objects);maximum_top_objects=std::max(maximum_top_objects,top_objects);maximum_objects=std::max(maximum_objects,bottom_objects);
    if(directory&&name)dump_surface(*directory,std::string(name)+"_bottom");
    if(manifest&&name)(*manifest)<<name<<'\t'<<ghost_focus_selected()<<'\t'<<top_objects<<'\t'<<bottom_objects<<'\t'<<top_objects+bottom_objects<<'\n';
    g_eng=engine;g_audio=audio;
}
void ghost_gothic_audit(GhostUnit& g) {
    g_view=View::Chains;g_overlay=Overlay::Editor;g_eng->capture_ui_snapshot(g_ui_snapshot,0,0);ghost_focus_capture();
    char hero[28];ghost_focus_value_text(hero,sizeof hero,ghost_focus_selected(),g.params[ghost_focus_selected()].base);
    Engine* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
    width=400;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(400*HEIGHT,0xff000000u);
    CHECK(ghost_focus_draw_top());CHECK(objects>0&&objects<C2D_DEFAULT_MAX_OBJECTS*2&&outside_rects==0&&outside_lines==0);
    check_inspector_gothic_mask(hero);CHECK(!has_text_at(hero,14,79));
    g_eng=engine;g_audio=audio;
}
void test_ghost_draw() {
    Fixture f;delete f.engine->replace_unit(0,0,new GhostUnit);
    auto& g=*static_cast<GhostUnit*>(f.engine->chains[0].units[0]);g_edit_chain=g_edit_slot=0;g_seq_lock_armed=false;
    for(bool modern:{false,true})for(int extreme:{0,32767})for(bool freeze:{false,true}) {
        g.long_tail=modern;
        for(auto& p:g.params){p.base=(q15)extreme;p.snap();}
        g.params[6].base=(q15)-extreme;g.params[6].snap();
        g.params[4].base=freeze?32767:0;g.params[4].snap();
        ghost_render(g);
        if(!freeze)ghost_gothic_audit(g);
    }
    g.params[4].base=0;g.params[4].snap();
    ghost_focus_touch_body(g,225,183);ghost_render(g);ghost_gothic_audit(g); // smear meaning in existing footer
    for(int param:{6,7}) {
        g_drag=Drag::None;ghost_focus_touch_body(g,param==6?150:100,param==6?76:207);
        ghost_focus_touch_body(g,285,211);ghost_focus_touch_body(g,10,43);
        CHECK(g.params[param].src==ModSource::SigilA);
        for(int depth:{-32767,0,32767}){g.params[param].mod_depth=(q15)depth;ghost_render(g);ghost_gothic_audit(g);}
        ghost_focus_touch_body(g,157,220);ghost_focus_drag(g,64,220);CHECK(g.params[param].mod_depth==-32767);
        ghost_focus_touch_body(g,230,207);CHECK(g.params[param].src==ModSource::None);
        ghost_focus_touch_body(g,285,211);
    }
    g_drag=Drag::None;
}
void dump_ghost(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    Fixture f;delete f.engine->replace_unit(0,0,new GhostUnit);
    auto& g=*static_cast<GhostUnit*>(f.engine->chains[0].units[0]);g_edit_chain=g_edit_slot=0;g_seq_lock_armed=false;
    std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\tparam\ttop_objects\tbottom_objects\tshared_objects\n";
    std::array<q15,512> audio{};for(int block=0;block<20;++block){for(int i=0;i<512;++i)audio[i]=(q15)(std::sin((block*512+i)*0.027)*9000);g.process(audio.data(),512);}
    auto shot=[&](const char*name){for(auto&p:g.params)p.snap();ghost_render(g,&directory,name,&manifest);};
    for(int p=0;p<8;++p){
        g.params[p].base=(q15)(3500+p*3300);g.params[p].snap();int x=160,y=117;const q15 v=g.params[p].base;
        switch(p){case 0:x=82+(uint32_t)(uint16_t)v*150/32767;y=54;break;case 1:x=61;y=174-(uint32_t)(uint16_t)v*82/32767;break;
            case 2:x=111+(uint32_t)(uint16_t)v*114/32767;y=183;break;case 3:x=254;y=111-(uint32_t)(uint16_t)v*52/32767;break;
            case 5:x=294;y=181-(uint32_t)(uint16_t)v*65/32767;break;case 6:x=150+(int32_t)v*60/32767;y=76;break;
            case 7:x=64+(uint32_t)(uint16_t)v*186/32767;y=220;break;}
        ghost_focus_touch_body(g,x,y);ghost_focus_release();shot(("param_"+std::to_string(p)).c_str());
    }
    ghost_focus_touch_body(g,150,10);shot("tuning_free");ghost_focus_touch_body(g,150,10);shot("tuning_note");
    g.params[6].base=0;g.params[6].snap();ghost_focus_touch_body(g,150,76);ghost_focus_drag(g,174,76);ghost_focus_release();shot("fine");
    g.params[3].base=32767;g.params[3].snap();g.params[7].base=32767;g.params[7].snap();ghost_focus_touch_body(g,100,220);ghost_focus_release();shot("drift_rate_slow");g.params[3].base=0;g.params[3].snap();g.params[7].base=0;g.params[7].snap();shot("drift_rate_fast");
    g.long_tail=false;shot("tail_old");g.long_tail=true;shot("tail_long");
    g.params[4].base=32767;g.params[4].snap();g.process(audio.data(),32);shot("freeze_transition");g.process(audio.data(),512);shot("freeze_full");
    g.params[4].base=0;g.params[4].snap();g.process(audio.data(),32);shot("freeze_release");g.process(audio.data(),512);shot("freeze_off");
    ghost_focus_touch_body(g,61,133);ghost_focus_release();g.params[1].src=ModSource::SigilA;g.params[1].mod_depth=-18000;shot("mod_signed");
    g.params[1].src=ModSource::EnvFollow;g.params[1].mod_depth=22000;shot("mod_env");
    for(auto&p:g.params){p.src=ModSource::None;p.base=0;p.snap();}shot("extrema_low");for(auto&p:g.params){p.base=32767;p.snap();}g.params[6].base=-32767;g.params[6].snap();shot("extrema_high");
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,GhostUnit::P_MIX,6000));ghost_focus_touch_body(g,294,148);ghost_focus_release();shot("p_lock");g_seq_lock_armed=false;
    CHECK(param_lens_touch_down(250,232));param_lens_touch_release();g.params[5].src=ModSource::SigilB;g.params[5].mod_depth=20000;param_lens_capture();
    width=400;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);CHECK(param_lens_draw_top());const int mt=objects;ghost_menu_palette_and_paper();dump_surface(directory,"lens_mod_top");
    width=320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);param_lens_draw_bottom();const int mb=objects;ghost_menu_palette_and_paper();dump_surface(directory,"lens_mod_bottom");manifest<<"lens_mod\t5\t"<<mt<<'\t'<<mb<<'\t'<<mt+mb<<'\n';
    lens_quick_begin();param_lens_touch_down(260,129);param_lens_capture();
    width=400;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);CHECK(param_lens_draw_top());const int tt=objects;ghost_menu_palette_and_paper();dump_surface(directory,"lens_temp_top");
    width=320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);param_lens_draw_bottom();const int tb=objects;ghost_menu_palette_and_paper();dump_surface(directory,"lens_temp_bottom");manifest<<"lens_temp\t5\t"<<tt<<'\t'<<tb<<'\t'<<tt+tb<<'\n';
    maximum_shared_objects=std::max(maximum_shared_objects,std::max(mt+mb,tt+tb));
    lens_frame(0,KEY_TOUCH,KEY_L);param_lens_touch_release();param_lens_cancel();
}
void test_fm_controls() {
    Fixture f;
    auto& fm = *static_cast<FmUnit*>(f.engine->chains[0].units[0]);
    g_edit_chain = g_edit_slot = 0; g_seq_lock_armed = false;
    g_view=View::Chains;g_overlay=Overlay::Editor;
    auto tap = [&](int x,int y) { g_drag = Drag::None; CHECK(fm_focus_touch_body(fm,x,y)); };
    tap(40,35); // voice page, irrespective of previous module visits
    CHECK(!fm.drone);
    const int before=mutations;
    tap(150,10); CHECK(fm.drone && mutations==before+1 && g_drag==Drag::None);
    f.refresh();
    CHECK(g_ui_snapshot.sequence_lifecycle[0]==(uint8_t)SequenceLifecycle::Flow);
    CHECK(g_ui_snapshot.sequence_value_param[0]==FmUnit::P_GAIN);
    g_view=View::Sequencer;g_overlay=Overlay::None;
    f.mode(2); f.touch(0,0); f.tap(KEY_A);
    CHECK(f.engine->pattern.steps[0][0].lock_count>0);
    CHECK(f.engine->pattern.steps[0][0].locks[0].param==FmUnit::P_GAIN);
    f.engine->clear_step_locks(0,0);
    g_view=View::Chains;g_overlay=Overlay::Editor;

    tap(140,35); // motion page
    for (auto xy : {std::pair<int,int>{50,155},{154,155},{50,195},{154,195},{258,155},{258,195}}) {
        const int m=mutations; tap(xy.first,xy.second); CHECK(g_drag==Drag::None && mutations==m);
    }
    tap(250,35);const bool reset=fm.phase_reset;tap(70,204);CHECK(fm.phase_reset==reset); // held in drone
    tap(40,35);
    auto& fine=fm.params[FmUnit::P_RATIO_FINE]; fine.base=123;
    tap(258,155); CHECK(g_drag==Drag::FmRatioFine && fine.base==123);
    fm_focus_drag(fm,258,155); CHECK(fine.base==123);
    fm_focus_drag(fm,259,155); CHECK(fine.base==123+32767/336);
    fine.base=8000;const int dirty=mutations;fm_focus_drag(fm,259,155);CHECK(fine.base==8000&&mutations==dirty);
    fm_focus_drag(fm,258,155);CHECK(fine.base==123);fm_focus_release();

    auto& ratio=fm.params[FmUnit::P_RATIO];ratio.base=8192;
    tap(154,155);fm_focus_drag(fm,168,155);CHECK(ratio.base==8192+14*32767/128);fm_focus_release();
    CHECK(f.engine->set_step_lock(0,0,0,FmUnit::P_RATIO_FINE,1000));
    const q15 base=fine.base; g_seq_lock_armed=true;g_view=View::Sequencer;
    tap(258,155);fm_focus_drag(fm,268,155);
    CHECK(fine.base==base && seq_display_value(0,FmUnit::P_RATIO_FINE,0)==1000+10*32767/336);
    f.engine->clear_step_locks(0,0);
    for (int p : {FmUnit::P_PITCH,FmUnit::P_RATIO,FmUnit::P_INDEX,FmUnit::P_GAIN})
        CHECK(f.engine->set_step_lock(0,0,0,p,12000));
    const auto locks=pattern_hash(f.engine->pattern);
    tap(258,155); fm_focus_drag(fm,268,155);
    CHECK(fine.base==base && pattern_hash(f.engine->pattern)==locks && g_seq_lock_flash>0);
    g_seq_lock_armed=false;g_view=View::Chains;
    tap(150,10); CHECK(!fm.drone);
    tap(140,35);tap(50,155);CHECK(g_drag==Drag::FmAttack);fm_focus_release();
    tap(40,35); g_drag=Drag::None;
}
void fm_render(FmUnit& fm) {
    width=320; objects=outside_rects=outside_lines=text_overlaps=0; texts.clear();
    pixels.assign(width*HEIGHT,0xff000000u);
    g_eng->capture_ui_snapshot(g_ui_snapshot,0,0);
    fm_focus_capture();
    fm_focus_draw(fm);
    CHECK(objects>0 && objects<8192);
    CHECK(outside_rects==0 && outside_lines==0 && text_overlaps==0);
    // FM now owns its plain contextual footer; no legacy lens-strip overdraw.
    CHECK(outside_rects==0 && outside_lines==0 && text_overlaps==0);
}
void test_fm_draw() {
    Fixture f; auto& fm=*static_cast<FmUnit*>(f.engine->chains[0].units[0]);
    g_edit_chain=g_edit_slot=0; g_seq_lock_armed=false;
    for (bool drone : {false,true}) for (int page=0;page<2;++page) for (int extreme : {0,32767}) {
        fm.drone=drone;
        for (auto& p: fm.params) {p.base=(q15)extreme; p.snap();}
        fm.params[FmUnit::P_RATIO_FINE].base=(q15)-extreme;
        fm.params[FmUnit::P_RATIO_FINE].snap();
        fm_focus_touch_body(fm,page?140:40,35);
        fm.control_tick(); fm.trigger(); std::array<q15,1024> audio{}; fm.process(audio.data(),1024);
        fm_render(fm);
        const int bot=objects;
        width=400; objects=outside_rects=outside_lines=text_overlaps=0; texts.clear();
        pixels.assign(width*HEIGHT,0xff000000u);
        // Same explicit pre-draw controller capture as main.
        g_view=View::Chains; g_overlay=Overlay::Editor;
        fm_context_capture(fm_focus_selected()); top_draw();
        CHECK(outside_rects==0 && outside_lines==0);
        g_view=View::Sequencer; g_overlay=Overlay::None;
        maximum_shared_objects=std::max(maximum_shared_objects,bot+objects);
        CHECK(bot+objects<8192);
    }
    fm_focus_touch_body(fm,40,35); g_drag=Drag::None;
}
void dump_fm(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    Fixture f; auto& fm=*static_cast<FmUnit*>(f.engine->chains[0].units[0]);
    g_edit_chain=g_edit_slot=0; g_seq_lock_armed=false;
    for (bool drone : {false,true}) for (int page=0;page<2;++page) {
        fm.drone=drone; fm_focus_touch_body(fm,page?140:40,35);
        fm.control_tick(); fm.trigger(); std::array<q15,1024> audio{}; fm.process(audio.data(),1024);
        fm_render(fm);
        std::ofstream out(directory/(std::string(drone?"drone":"strike")+"_"+std::to_string(page)+".ppm"),std::ios::binary);
        out<<"P6\n320 240\n255\n";
        for (u32 px:pixels) { char rgb[3]={(char)px,(char)(px>>8),(char)(px>>16)}; out.write(rgb,3); }
    }
}
#include "fm_menu_test.h"
#include "fm_extended_ui_test.h"
#include "strn_focus_test.h"
#include "reverb_controls_test.h"
#include "rev_visual_test.h"
#include "sigils_menu_test.h"
#include "sequencer_slice_test.h"
#include "sequencer_cuts_test.h"
#include "module_inspector_test.h"
#include "chorus_ui_test.h"
#include "vca_ui_test.h"
#include "delay_ui_test.h"
#include "filter_ui_test.h"
#include "fold_ui_test.h"
#include "bit_ui_test.h"
#include "tilt_ui_test.h"
#include "ring_ui_test.h"
#include "strain_ui_test.h"
#include "gran_ui_test.h"
#include "osc_morph_ui_test.h"
#include "play_ui_test.h"
#include "mic_ui_test.h"
#include "rec_ui_test.h"
#include "noise_core_test.h"
#include "noise_ui_test.h"
#include "motor_ui_test.h"
#include "performance_mixer_test.h"
#include "transition_core_test.h"
#include "transition_ui_test.h"
#include "transition_fx_core_test.h"
#include "transition_fx_ui_test.h"
#include "sequence_extent_test.h"
#include "sequence_polish_test.h"
#include "sequence_bank_core_test.h"
#include "step_focus_ui_test.h"
#include "playhead_press_ui_test.h"
}
int main(int argc, char** argv) {
    theme_test::select();
    if (argc == 2 && std::string(argv[1]) == "--playhead-press-only") return playhead_press_cases::run();
    if (argc == 3 && std::string(argv[1]) == "--dump-playhead-press") {
        try { playhead_press_cases::dump(argv[2]); return 0; }
        catch (const std::exception& error) { std::fprintf(stderr,"FAIL: playhead press dump: %s\n",error.what()); return 1; }
    }
    if (argc == 2 && std::string(argv[1]) == "--step-focus-only") return step_focus_cases::run();
    if (argc == 3 && std::string(argv[1]) == "--dump-step-focus") {
        try { step_focus_cases::dump(argv[2]); return 0; }
        catch (const std::exception& error) { std::fprintf(stderr,"FAIL: step focus dump: %s\n",error.what()); return 1; }
    }
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"r tap / hold", test_r_tap_hold}, {"r chord ownership", test_r_chords},
        {"cancel / stale touch", test_cancel}, {"circle chooser", test_circle},
        {"chooser touch ownership", test_chooser_touch},
        {"dpad / l navigation", test_navigation}, {"non-trig selection", test_touch_selection},
        {"trig tap / paint", test_touch_trig}, {"apply / clear / copy", test_apply_clear_copy},
        {"editor hit zones / conditions", test_editor_controls},
        {"full pitch labels / slider quantization", test_pitch_labels_and_quantization},
        {"sample upper pitch clamp", test_sample_pitch_clamp},
        {"stranular signed semitone pitch", test_stranular_pitch},
        {"lock target num_params bounds", test_lock_target_bounds},
        {"cut squares / exact plain labels / full touch bands", test_cut_square_cells},
        {"baked note font / signs / bounded inputs", test_note_font_data},
        {"magnetic pointer / capture / hysteresis / easing", test_magnetic_pointer},
        {"eight-node radial layout / both themes / touches", test_radial_layout},
        {"real draw / lifecycle / bounds", test_render},
        {"menu paper / real top identity / seven distinct layers", test_menu_paper_and_distinct_modes},
        {"silent-step metadata stays visible", test_silent_step_metadata},
        {"note audition / slider dedup / full locks", test_note_audition_controls},
        {"lock browser / single removal / frozen DTO", test_lock_browser_and_snapshot},
        {"level exact target / inspector worst density", test_level_targets_and_inspector_density},
        {"hold-l contextual helper / live chords / no occlusion", test_hold_l_companion},
        {"stored flow profile / pitch lock return to base", test_profile_stored_flow},
        {"diagonal fractions / local play brackets", test_diagonal_when_and_local_play_length},
        {"selected-step play window / speed / slice / missing", test_play_companion_projection},
        {"slice ring / chords / eight-node geometry", test_slice_chooser},
        {"slice all centers / sparse identities / valid rail", test_slice_centers_sparse},
        {"slice silent selection / no-op / full locks / isolated removal", test_slice_locks},
        {"slice fresh source precedence / masked PLAY / stale snapshot", test_slice_source_revalidation},
        {"slice audition / duration / cancellation / muted", test_slice_audition},
        {"slice immutable PCM capture / refresh / bounds / budget", test_slice_capture},
        {"slice dense four-PLAY extrema / existing patch roundtrip", test_slice_persistence_dense},
        {"slice actual scheduler / two PCM intervals / return to base", test_slice_dispatch},
        {"cuts shared editor navigation / score identity / return", test_cuts_navigation},
        {"cuts seq-origin keys / transport / edit-bank SLCE / lens ownership", test_cuts_context_keys},
        {"cuts stable insertion / holes / no-op / full sixteen", test_cuts_insert},
        {"cuts geometric drag / clamp / cancel / replacement ownership", test_cuts_drag},
        {"cuts merge / per-ID reverse / shared sample identities", test_cuts_merge_reverse},
        {"cuts even replacement / preview confirmation / clear", test_cuts_auto_clear},
        {"cuts fresh last-source / missing / recording refusal", test_cuts_source_recording},
        {"cuts frozen DTO / adaptive real PCM / stereo / bounds", test_cuts_capture},
        {"cuts confirmed distribution / explicit navigation only", test_cuts_play_bridge},
        {"parameter lens / env / held mod / input ownership", test_param_lens_controls},
        {"parameter lens / all modules / DTO render / bounds", test_param_lens_draw},
        {"ghost note / free / fine / rate / freeze / locks",test_ghost_controls},
        {"ghost exact zones / passive wells / routing / freeze / lock contract",test_ghost_menu_contract},
        {"ghost selected lens / env / held mod / identity",test_ghost_menu_lens_temp},
        {"ghost real draw / bounds",test_ghost_draw},
        {"FM extended parameters / waves / harmonic edit / legacy upgrade", test_fm_extended_controls},
        {"FM extended twelve live rails / real Param slew", test_fm_extended_markers},
        {"FM extended signed pitch preview / duration / held drone", test_fm_extended_pitch_preview},
        {"FM extended actual reset DSP / frozen waves / isolated phase", test_fm_extended_reference_frames},
        {"FM exact two-operator reset reference / samplewise DSP", test_fm_exact_reference},
        {"FM frozen voice/motion render / detached draw / budget", test_fm_frozen_render},
        {"FM selected parameters / page locks / stored gestures / dormant", test_fm_menu_selection_locks},
        {"FM contextual values / sources / range / paper / DTO / bounds", test_fm_menu_context},
        {"FM selected lens / env / held mod / ownership", test_fm_menu_lens_temp},
        {"FM relative voice/motion controls / lifecycle / locks", test_fm_controls},
        {"FM real draw / both modes and pages / extremes / bounds", test_fm_draw},
        {"strn silent touch / pitch precision / modes / zero", test_strn_touch_precision},
        {"strn all params / continuous reverse / sync / windows", test_strn_all_controls},
        {"strn locks / stored origin / full bank / context ownership", test_strn_locks_context},
        {"strn old hold dry fallback / freeze forces wet", test_strn_old_hold_freeze},
        {"strn menu paper / palette / unchanged hit geometry", test_strn_menu_style_geometry},
        {"strn selected lens / env / held mod / input ownership", test_strn_lens_routing},
        {"strn render / extrema / immutable DTO / bounds", test_strn_draw},
        {"rev visual bounded space / real energy projection / freeze states", test_rev_visual_model},
        {"rev visual passive center / requested effective freeze / live mix", test_rev_visual_passive_freeze},
        {"rev visual frozen duals / null engine / zero DSP / bounds", test_rev_visual_frozen},
        {"rev gestures / passive figure / freeze / ownership", test_rev_controls},
        {"rev stored locks / full bank / selected held mod", test_rev_locks_lens},
        {"rev menu paper / palette / exact touch geometry / null", test_rev_menu_style_geometry},
        {"rev all params / extrema / telemetry inspector / bounds", test_rev_draw},
        {"rev menu held mod top+bottom / palette / bounds", test_rev_menu_lens_draw},
        {"sigils tabs / six modes / legal source cycling / half-open zones", test_sigils_modes_sources},
        {"sigils free/sync / six divisions / relative labels / absolute rails", test_sigils_timing},
        {"sigils bipolar depth / mod depth / zero / paused modulation", test_sigils_bipolar},
        {"sigils draw / smoothing / bidirectional interpolation / clamp / undo / saved flags", test_sigils_strokes},
        {"sigils primary routing / empty no-op / trigger / real PLAY follow", test_sigils_routing_follow},
        {"sigils release / cancel / context ownership / project undo invalidation", test_sigils_ownership},
        {"sigils ink columns / depth ghost / step buckets / truthful markers", test_sigils_thread},
        {"module inspector module handles / absolute and relative gestures", test_module_inspector_handles},
        {"module inspector chorus fixed controls / p-lock / ownership", test_module_inspector_chorus_controls},
        {"module inspector chorus actual delays / frozen lower / mix / state", test_module_inspector_chorus_frame},
        {"module inspector selected held mod / immediate arrows / return", test_module_inspector_lens},
        {"module inspector held mod ownership / persistent edits / context invalidation", test_module_inspector_ownership},
        {"module inspector physical units / extrema / bounded format", test_module_inspector_units},
        {"module inspector lock edit / base / live / full bank", test_module_inspector_locks},
        {"module inspector frozen capture / upper routing / bounds / shared budget", test_module_inspector_render},
        {"vca flexible all controls / relative origin / context ownership", test_vca_flexible_controls},
        {"vca flexible locks / full bank / selected lens / held mod", test_vca_flexible_locks_lens},
        {"vca flexible physical units / frozen frames / all extrema / telemetry", test_vca_flexible_units_frames},
        {"play ui relative controls / independent trim / zoom / fine / undo", test_play_ui_controls},
        {"play ui locks / full-pair preflight / lifecycle / legacy opt-in", test_play_ui_locks},
        {"play ui selected lens / held mod / source-only preview", test_play_ui_preview_lens},
        {"play ui cut commit / source undo / cut rates / scratch and position mod", test_play_finish_ui},
        {"play ui hold / stationary sustain / live style / cancel / stored locks", test_play_hold_ui},
        {"play ui frozen PCM / physical values / dual budget", test_play_ui_frames},
        {"gran fixed pages / all11 relative controls / browser and clock", test_gran_controls},
        {"gran locks / full bank / gesture ownership / selected held mod", test_gran_locks_lens},
        {"gran real PCM / exact source projection / latched grain fades", test_gran_wave},
        {"gran frozen duals / physical mappings / boundaries and budget", test_gran_frames},
        {"osc morph relative controls / legacy no-jump conversion / isolation", test_osc_controls_conversion},
        {"osc morph locks / selected modulation / held mod / ownership", test_osc_locks_lens},
        {"osc morph exact reset model / width relevance / frozen dual budget", test_osc_model_frames},
        {"strain fixed controls / relative drag / isolation", test_strain_controls},
        {"strain locks / name removal / full bank / context ownership", test_strain_locks},
        {"strain selected inspector / lens / routing / held mod", test_strain_lens},
        {"strain exact real reference / independent source+PCM / units / host cost", test_strain_reference},
        {"strain cached frozen duals / extrema / detached draw / budget", test_strain_frames},
        {"ring fixed controls / relative drag / isolation", test_ring_controls},
        {"ring locks / name removal / full bank / context ownership", test_ring_locks},
        {"ring selected inspector / lens / routing / held mod", test_ring_lens},
        {"ring real reference / independent source+phase / units / host cost", test_ring_reference},
        {"ring cached frozen duals / extrema / detached draw / budget", test_ring_frames},
        {"tilt fixed controls / zero / legacy trim isolation", test_tilt_controls},
        {"tilt locks / zero lock / full bank / ownership", test_tilt_locks},
        {"tilt selected lens / routing / held mod", test_tilt_lens},
        {"tilt linear model / independent recurrence DFT / units", test_tilt_model},
        {"tilt frozen dual frames / legacy+modern extrema / budget", test_tilt_frames},
        {"bit fixed controls / relative drag / isolation", test_bit_controls},
        {"bit locks / name removal / full bank / ownership", test_bit_locks},
        {"bit selected lens / routing / held mod", test_bit_lens},
        {"bit fixed reference / samplewise DSP / units", test_bit_reference},
        {"bit frozen dual frames / extrema / budget", test_bit_frames},
        {"fold fixed controls / relative drag / bipolar / inert background", test_fold_controls},
        {"fold locks / name removal / full bank / context ownership", test_fold_locks},
        {"fold selected lens / bipolar units / permanent held edits", test_fold_lens_temp},
        {"fold exact model / real unchanged integer DSP", test_fold_wave_model},
        {"fold immutable dual frames / extrema / fixed plot / budget", test_fold_frames},
        {"filter fixed controls / no jump / independent / context", test_filter_controls},
        {"filter stored locks / deletion / full bank / type+bypass refusal", test_filter_locks},
        {"filter selected held mod / relative origin / ownership / return", test_filter_lens_temp},
        {"filter nominal coefficient / plateau / linear recurrence response", test_filter_response_model},
        {"filter immutable dual frames / all types+extrema / geometry / budget", test_filter_frames},
        {"delay fixed controls / no jump / passive model / ownership", test_delay_controls},
        {"delay stored locks / full bank / release", test_delay_locks},
        {"delay selected lens / relative base+depth / permanent held edits", test_delay_lens_temp},
        {"delay modern controls / sync tone clean hold clear / long scale", test_delay_modern},
        {"delay physical units / frozen dual frames / extrema / budget", test_delay_units_frames},
        {"transition core / tuple scheduler / phase / bank persistence", test_transition_core},
        {"transition fx core / master audio / ramps / tails / persistence", test_transition_fx_core},
        {"transition fx ui / temporary pads / relative input / lifecycle", test_transition_fx_ui},
        {"transition swipes / staging / relative rail / contribution", test_transition_swipes},
        {"transition modes / sound / mixer / lifecycle", test_transition_modes_lifecycle},
        {"transition frozen frames / bounds / native banks", test_transition_frames},
        {"performance faders / relative ownership / mute", test_performance_faders},
        {"performance scenes / capture / mixer-preserving morph", test_performance_morph},
        {"performance gain / exact audio ramp / partition / swap", test_performance_gain_ramp},
        {"performance frames / capture feedback / peak hold-fall / frozen bounds", test_performance_frames},
        {"sequence bank core / snapshot / PCM / persistence / audition", test_sequence_bank_core},
        {"performance sequence extent / controls / retained steps", test_sequence_extent_controls},
        {"performance sequence extent / directions / continuation", test_sequence_extent_direction},
        {"performance sequence extent / ownership", test_sequence_extent_ownership},
        {"sequence extent / published scope + frozen draw", test_sequence_scope_frozen},
        {"sequence extent / signal-reactive scope ink", sequence_polish_cases::reactive_scope},
        {"sequence extent / centered value rails", sequence_polish_cases::centered_rails},
        {"sequence extent / retained upper + compact context", sequence_polish_cases::upper_context},
        {"motor exact reset reference / unchanged samplewise DSP", test_motor_exact_reference},
        {"motor controls / locks / selected inspector ownership", test_motor_controls_locks},
        {"motor frozen reset model / source states / dual budget", test_motor_frozen_frames},
        {"motor clear presentation / physical extrema / conditional live+layer / locks", test_motor_clear_presentation},
        {"motor gothic source / discrete masks / edit-live separation", test_motor_gothic_source},
        {"motor live rails / actual slew / frozen positions / locks", test_motor_live_rails},
        {"motor truthful graph / exact extrema / nonzero strokes / telemetry isolation", test_motor_truthful_graph},
        {"noise core legacy PCM / attack / lifecycle / persistence", run_noise_core_tests},
        {"noise controls / modes / locks / ownership", test_noise_controls_locks},
        {"noise inspector / held mod / published scalars / frozen frames", test_noise_lens_frames},
        {"rec controls / locks / ownership", test_rec_controls},
        {"rec sigil modulation / permanent held edits / length latch", test_rec_mod},
        {"rec commands / pending / take protection / frozen UI", test_rec_commands_frames},
        {"mic compact controls / source-cut / locks / ownership", test_mic_controls_locks},
        {"mic selected lens / held mod / physical inspector", test_mic_lens_inspector},
        {"mic actual input wave / clip / frozen frames / silence", test_mic_live_frames},
        {"sigils immutable capture / real output / metadata / extrema / budget", test_sigils_capture_extrema}
    };
    int failed = 0, executed = 0;
    const bool mode_ring_only=(argc==2&&std::string(argv[1])=="--mode-ring-only");
    const bool fm_only=(argc==2&&std::string(argv[1])=="--fm-only") ||
                       (argc==3&&std::string(argv[1])=="--dump-fm-menu");
    const bool ghost_only=(argc==2&&(std::string(argv[1])=="--ghost-only"||std::string(argv[1])=="--ghost-menu-only"));
    const bool strn_only=(argc==2&&std::string(argv[1])=="--strn-only") ||
                         (argc==3&&std::string(argv[1])=="--dump-strn");
    const bool rev_visual_only=(argc==2&&std::string(argv[1])=="--rev-visual-only");
    const bool rev_only=rev_visual_only||(argc==2&&std::string(argv[1])=="--rev-only") ||
                        (argc==3&&std::string(argv[1])=="--dump-rev");
    const bool sequencer_only=(argc==2&&std::string(argv[1])=="--sequencer-only") ||
                              (argc==3&&std::string(argv[1])=="--dump");
    const bool sigils_only=(argc==2&&std::string(argv[1])=="--sigils-only") ||
                           (argc==3&&std::string(argv[1])=="--dump-sigils-menu");
    const bool slice_only=(argc==2&&std::string(argv[1])=="--slice-only") ||
                          (argc==3&&std::string(argv[1])=="--dump-slices");
    const bool cuts_only=(argc==2&&std::string(argv[1])=="--cuts-only") ||
                         (argc==3&&std::string(argv[1])=="--dump-cuts");
    const bool module_inspector_only=(argc==2&&std::string(argv[1])=="--module-inspector-only") ||
                                    (argc==3&&std::string(argv[1])=="--dump-module-inspector") ||
                                     (argc==3&&std::string(argv[1])=="--dump-chorus");
    const bool vca_only=(argc==2&&std::string(argv[1])=="--vca-only") || (argc==3&&std::string(argv[1])=="--dump-vca");
    const bool delay_only=(argc==2&&std::string(argv[1])=="--delay-only");
    const bool filter_only=(argc==2&&std::string(argv[1])=="--filter-only");
    const bool fold_only=(argc==2&&std::string(argv[1])=="--fold-only");
    const bool performance_only=(argc==2&&std::string(argv[1])=="--performance-only");
    const bool motor_only=(argc==2&&std::string(argv[1])=="--motor-only");
    const bool noise_only=(argc==2&&std::string(argv[1])=="--noise-only");
    const bool rec_only=(argc==2&&std::string(argv[1])=="--rec-only");
    const bool mic_only=(argc==2&&std::string(argv[1])=="--mic-only");
    const bool play_ui_only=(argc==2&&std::string(argv[1])=="--play-ui-only");
    const bool gran_only=(argc==2&&std::string(argv[1])=="--gran-only");
    const bool osc_only=(argc==2&&std::string(argv[1])=="--osc-only");
    const bool strain_only=(argc==2&&std::string(argv[1])=="--strain-only");
    const bool ring_only=(argc==2&&std::string(argv[1])=="--ring-only");
    const bool tilt_only=(argc==2&&std::string(argv[1])=="--tilt-only");
    const bool bit_only=(argc==2&&std::string(argv[1])=="--bit-only");
    const bool transition_only=(argc==2&&std::string(argv[1])=="--transition-only");
    const bool banks_only=(argc==2&&std::string(argv[1])=="--sequence-banks-only");
    const bool dump_mode=argc==3;
    for (const auto& test : tests) {
        if(dump_mode)break;
        const auto fn = test.second.target<void(*)()>();
        if(banks_only && std::getenv("SEANCE_SEQUENCE_POLISH_ONLY") && (!fn ||
           (*fn!=test_sequence_scope_frozen && *fn!=sequence_polish_cases::reactive_scope &&
            *fn!=sequence_polish_cases::centered_rails && *fn!=sequence_polish_cases::upper_context)))continue;
        if(mode_ring_only && (!fn ||
           (*fn!=test_r_tap_hold && *fn!=test_r_chords && *fn!=test_cancel &&
            *fn!=test_circle && *fn!=test_chooser_touch && *fn!=test_magnetic_pointer &&
            *fn!=test_radial_layout && *fn!=test_slice_chooser)))continue;
        if(transition_only && std::string(test.first).find("transition ")!=0)continue;
        if(!transition_only && argc!=1 && std::string(test.first).find("transition ")==0)continue;
        if(banks_only && std::string(test.first).find("sequence extent")==std::string::npos &&
           std::string(test.first).find("sequence bank core")==std::string::npos &&
           std::string(test.first)!="r chord ownership" && std::string(test.first)!="dpad / l navigation" &&
           std::string(test.first)!="trig tap / paint" && std::string(test.first)!="apply / clear / copy")continue;
        if(performance_only&&std::string(test.first).find("performance ")!=0)continue;
        // UI-only mixer changes need not migrate legacy sequencer gesture fixtures.
        // The normal --performance-only selection still includes all seven entries.
        if(performance_only&&std::getenv("SEANCE_PERFORMANCE_MIXER_ONLY")&&
           std::string(test.first).find("performance sequence ")==0)continue;
        if(!performance_only&&!banks_only&&argc!=1&&std::string(test.first).find("performance ")==0)continue;
        if(motor_only&&std::string(test.first).find("motor ")!=0)continue;
        if(!motor_only&&argc!=1&&std::string(test.first).find("motor ")==0)continue;
        if(noise_only&&std::string(test.first).find("noise ")!=0)continue;
        if(!noise_only&&argc!=1&&std::string(test.first).find("noise ")==0)continue;
        if(rec_only&&std::string(test.first).find("rec ")!=0)continue;
        if(!rec_only&&argc!=1&&std::string(test.first).find("rec ")==0)continue;
        if(mic_only&&std::string(test.first).find("mic ")!=0)continue;
        if(!mic_only&&argc!=1&&std::string(test.first).find("mic ")==0)continue;
        if(!rev_visual_only&&argc!=1&&std::string(test.first).find("rev visual ")==0)continue;
        if(play_ui_only&&std::string(test.first).find("play ui ")!=0)continue;
        if(!play_ui_only&&argc!=1&&std::string(test.first).find("play ui ")==0)continue;
        if(gran_only&&std::string(test.first).find("gran ")!=0)continue;
        if(!gran_only&&argc!=1&&std::string(test.first).find("gran ")==0)continue;
        if(osc_only&&std::string(test.first).find("osc morph ")!=0)continue;
        if(!osc_only&&argc!=1&&std::string(test.first).find("osc morph ")==0)continue;
        if(strain_only&&std::string(test.first).find("strain ")!=0)continue;
        if(!strain_only&&argc!=1&&std::string(test.first).find("strain ")==0)continue;
        if(ring_only&&std::string(test.first).find("ring ")!=0)continue;
        if(!ring_only&&argc!=1&&std::string(test.first).find("ring ")==0)continue;
        if(tilt_only&&std::string(test.first).find("tilt ")!=0)continue;
        if(!tilt_only&&argc!=1&&std::string(test.first).find("tilt ")==0)continue;
        if(bit_only&&std::string(test.first).find("bit ")!=0)continue;
        if(!bit_only&&argc!=1&&std::string(test.first).find("bit ")==0)continue;
        if(fold_only&&std::string(test.first).find("fold ")!=0)continue;
        if(!fold_only&&argc!=1&&std::string(test.first).find("fold ")==0)continue;
        if(filter_only&&std::string(test.first).find("filter ")!=0)continue;
        if(!filter_only&&argc!=1&&std::string(test.first).find("filter ")==0)continue;
        if(delay_only&&std::string(test.first).find("delay ")!=0)continue;
        if(!delay_only&&argc!=1&&std::string(test.first).find("delay ")==0)continue;
        if(sequencer_only&&(std::string(test.first).find("FM ")==0 || std::string(test.first).find("ghost")==0 ||
                            std::string(test.first).find("strn ")==0 || std::string(test.first).find("rev ")==0 ||
                            std::string(test.first).find("module inspector ")==0 ||
                            std::string(test.first).find("parameter lens")==0 || std::string(test.first).find("sigils ")==0))continue;
        if(vca_only&&std::string(test.first).find("vca flexible ")!=0&&std::string(test.first).find("module inspector ")!=0)continue;
        if(module_inspector_only&&std::string(test.first).find("module inspector ")!=0&&std::string(test.first).find("vca flexible ")!=0)continue;
        if(slice_only&&std::string(test.first).find("slice ")!=0)continue;
        if(cuts_only&&std::string(test.first).find("cuts ")!=0)continue;
        if(sigils_only&&std::string(test.first).find("sigils ")!=0)continue;
        if(fm_only&&std::string(test.first).find("FM ")!=0)continue;
        if(rev_only&&std::string(test.first).find("rev ")!=0)continue;
        if(ghost_only&&std::string(test.first).find("ghost")!=0)continue;
        if(strn_only&&std::string(test.first).find("strn ")!=0)continue;
        ++executed;
        try { test.second(); std::printf("ok: %s\n", test.first); }
        catch (const std::exception& error) { ++failed; std::fprintf(stderr, "FAIL: %s: %s\n", test.first, error.what()); }
    }
    if (argc == 3 && std::string(argv[1]) == "--dump") {
        try { dump(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr, "FAIL: dump: %s\n", error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-cuts") {
        try { dump_cuts(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: cuts dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-slices") {
        try { dump_slices(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: slice dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-lens") {
        try { dump_lens(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr, "FAIL: dump-lens: %s\n", error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-sigils-menu") {
        try { dump_sigils_menu(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: sigils menu dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-fm-menu") {
        try { dump_fm_menu(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: FM menu dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-fm") {
        try { dump_fm(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: FM dump: %s\n",error.what()); }
    } else if (argc == 3 && (std::string(argv[1]) == "--dump-ghost" || std::string(argv[1]) == "--dump-ghost-menu")) {
        try { dump_ghost(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: ghost dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-strn") {
        try { dump_strn(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: strn dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-rev-edges") {
        try { dump_rev_edges(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: rev edges dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-rev-visual") {
        try { dump_rev_visual(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: rev visual dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-rev") {
        try { dump_rev(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: rev dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-transition-fx") {
        try { dump_transition_fx(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: transition fx dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-transition") {
        try { dump_transition(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: transition dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-performance-feedback") {
        try { dump_performance_feedback(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: performance feedback dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-performance") {
        try { dump_performance_mixer(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: performance dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-sequence-extent") {
        try { dump_sequence_extent(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: sequence extent dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-motor") {
        try { dump_motor_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: motor dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-noise") {
        try { dump_noise_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: noise dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-rec") {
        try { dump_rec_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: rec dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-mic") {
        try { dump_mic_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: mic dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-play-ui") {
        try { dump_play_ui_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: play dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-gran") {
        try { dump_gran_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: gran dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-osc") {
        try { dump_osc_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: osc dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-strain") {
        try { dump_strain_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: strain dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-ring") {
        try { dump_ring_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: ring dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-tilt") {
        try { dump_tilt_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: tilt dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-bit") {
        try { dump_bit_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: bit dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-fold") {
        try { dump_fold_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: fold dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-filter") {
        try { dump_filter_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: filter dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-delay") {
        try { dump_delay_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: delay dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-vca") {
        try { dump_vca_runtime(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: vca dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-chorus") {
        try { dump_chorus_motion(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: chorus dump: %s\n",error.what()); }
    } else if (argc == 3 && std::string(argv[1]) == "--dump-module-inspector") {
        try { dump_module_inspector(argv[2]); } catch (const std::exception& error) { ++failed; std::fprintf(stderr,"FAIL: module inspector dump: %s\n",error.what()); }
    } else if (argc != 1&&!mode_ring_only&&!osc_only&&!gran_only&&!play_ui_only&&!transition_only&&!banks_only&&!performance_only&&!motor_only&&!noise_only&&!rec_only&&!mic_only&&!strain_only&&!ring_only&&!tilt_only&&!bit_only&&!fold_only&&!filter_only&&!delay_only&&!vca_only&&!module_inspector_only&&!cuts_only&&!slice_only&&!sequencer_only&&!sigils_only&&!fm_only&&!ghost_only&&!strn_only&&!rev_only) { std::fprintf(stderr, "usage: %s [--motor-only|--dump-motor directory|--osc-only|--dump-osc directory|--noise-only|--dump-noise directory|--strain-only|--dump-strain directory|--ring-only|--dump-ring directory|--tilt-only|--dump-tilt directory|--bit-only|--dump-bit directory|--fold-only|--dump-fold directory|--filter-only|--dump-filter directory|--delay-only|--dump-delay directory|--module-inspector-only|--dump-module-inspector directory|--cuts-only|--dump-cuts directory|--slice-only|--dump-slices directory|--sigils-only|--dump-sigils-menu directory|--sequencer-only|--strn-only|--ghost-only|--ghost-menu-only|--dump-strn directory|--dump|--dump-lens|--dump-fm|--dump-ghost directory|--dump-ghost-menu directory]\n", argv[0]); return 2; }
    std::printf("sequencer: %d tests, %d failed; peak bottom %d, top %d, shared %d / %d objects\n",
                executed, failed, maximum_objects, maximum_top_objects, maximum_shared_objects, C2D_DEFAULT_MAX_OBJECTS * 2);
    return failed ? 1 : 0;
}
