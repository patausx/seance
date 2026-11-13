// seance - day 2 ui.
// bottom screen = interaction: chain cards view (default) / sigil canvas view (Y).
// top screen = visualization: selected sigil, chain meters, master scope, dsp badge.
//
// buttons: A triggers the selected chain, B closes overlays, X deletes a card,
//          Y is always Chains <-> Sigils, START plays/stops the score, SELECT opens GO TO.
// transport: START enables sequencer play/stop; R+START stops and resets.
//            SELECT is only GO TO. Patches and Exit live there.
// design lesson baked in: if ndsp fails (no dspfirm.cdc - e.g. emulator
// without a dump) we DO NOT exit; ui runs with a "NO DSP" badge.
#include <3ds.h>
#include <citro2d.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <utility>
#include <algorithm>
#include <new>
#include <ctime>
#include <cerrno>

#include "../../core/engine/engine.h"
#include "../../core/engine/units.h"
#include "../../core/engine/vca_envelope.h"
#include "../../core/engine/patch_io.h"
#include "../../core/engine/project_session.h"
#include "../../core/engine/project_recovery.h"
#include "../../core/synth/wav_loader.h"
#include "../../core/synth/wav_writer.h"
#include "../../core/ui/draw.h"
#include "audio_out.h"
#include "debug_overlay.h"
#include "mic_in.h"
#include "screenshot_io.h"
#include "ui_state.h"
#include "ui_transition.h"
#include "ui_theme.h"
#include "ui_palette.h"
#include "small_button_motion.h"
#include "stylus_feedback.h"
#include "menu_style.h"
#include "editor_style.h"
#include "ui_chains.h"
#include "ui_play_editor.h"
#include "ui_editor.h"
#include "ui_param_lens.h"
#include "ui_module_rand.h"
#include "fm_context.h"
#include "ui_module_focus_internal.h"
#include "strn_focus.h"
#include "ui_picker.h"
#include "ui_sample_library.h"
#include "sample_ownership.h"
#include "ui_sigils.h"
#include "ui_patchbay.h"
#include "ui_sequencer.h"
#include "sequencer_inspector.h"
#include "ui_performance.h"
#include "ui_nav.h"
#include "nav_ink_motion.h"
#include "ui_bounce.h"
#include "ui_top.h"
#include "ui_workspace.h"
#include "ui_start_menu.h"
#include "startup_input_tail.h"
#include "lifecycle_input.h"
#include "start_menu_store.h"
#include "project_names.h"
#include "project_file_ops.h"
#include "word_menu.h"
#include "sigil_surface.h"
#include "../../core/engine/patch_preview.h"

using namespace seance;
using namespace seance::platform::ui3ds;

// Passive visual observation; never consumes, smooths or delays controller input.
static void capture_stylus_feedback(u32 held) {
    touchPosition touch{};
    if (held & KEY_TOUCH) hidTouchRead(&touch);
    stylus_feedback::capture((held & KEY_TOUCH) != 0, touch.px, touch.py,
                             osGetTime(), palette::spot());
}

static void draw_stylus_feedback() {
    stylus_feedback::draw(); // only frozen geometry; current-destination GPU blend
}

static void install_demo_patch(Engine& e);
static void clear_sample_bank();

static void draw_bottom_surface(View view, Overlay overlay);
static void commit_nav_destination(View from_view, Overlay from_overlay,
                                   View to_view, Overlay to_overlay);

namespace seance::platform::ui3ds {

// ===================== app state (all static - zero per-frame allocs) =====================


Engine*              g_eng   = nullptr;
platform::AudioOut*  g_audio = nullptr;
static bool g_session_started = false;
bool                 g_has_dsp = false;
bool                 g_mic_ok = false;
q15                  g_mic_peak = 0;
EngineUiSnapshot     g_ui_snapshot;

View    g_view    = View::Chains;
Overlay g_overlay = Overlay::None;
int  g_sel_chain = 0, g_sel_slot = 0;    // dpad cursor in the cards grid
int  g_edit_chain = 0, g_edit_slot = 0;  // editor / picker target
int  g_sel_sigil = 0;
Drag g_drag = Drag::None;
int  g_drag_param = 0;                   // param row being dragged
int  g_last_px = -1, g_last_py = 0;      // previous canvas touch (stroke_to)
bool g_smooth_active = false;                 // touch low-pass state for sigil drawing
int  g_smooth_py = 0;
int  g_save_flash = 0;                   // patch action toast frames
char g_patch_notice[48] = {};            // completed result + actual target, never draw-time IO
int  g_loaded_samples = 0;
int  g_sample_flash = 0;
int  g_export_flash = 0;
bool g_export_ok = false;
bool g_export_is_load = false;
int  g_sample_page = 0;
int  g_patch_slot = 0;
ProjectSession g_project_session;
static recovery::Store g_recovery_store("sdmc:/3ds/seance/recovery");
static recovery::Controller g_recovery;
static recovery::Inventory g_recovery_boot;
static bool g_recovered_session = false;
bool g_exit_requested = false;
int  g_patch_confirm_slot = -1;
int g_patch_delete_confirm_slot = -1;
bool g_patch_exists[PROJECT_SLOTS] = {};
bool g_patch_valid[PROJECT_SLOTS] = {};
bool g_patch_checked[PROJECT_SLOTS] = {};
bool g_patch_catalog_ready = false;
bool g_patch_catalog_failed = false;
uint64_t g_patch_saved_at[PROJECT_SLOTS] = {};
ProjectNames g_project_names;
char g_sample_files[MAX_SAMPLE_FILES][SAMPLE_NAME_LEN] = {};
int  g_sample_file_count = 0;
int  g_sample_file_page = 0;
int  g_sample_browser_mode = 0; // library=0, project=1, recordings=3; retired 2 is not exposed

const char* SAMPLE_DIR = "sdmc:/3ds/seance/samples";
const char* RECORDING_DIR = "sdmc:/3ds/seance/recordings";
// numbered pairs in their own dir so successive ZL presses never overwrite
const char* SCREENSHOT_DIR = "sdmc:/3ds/seance/shots";
bool g_screenshot_pending = false;
Unit* g_clip_unit = nullptr;                 // tiny unit clipboard for COPY/PASTE ops
q15 g_sigil_undo[NUM_SIGILS][SIGIL_POINTS] = {};
bool g_sigil_undo_valid[NUM_SIGILS] = {};
q15 g_sigil_clip[SIGIL_POINTS] = {};
bool g_sigil_clip_valid = false;
bool g_sig_canvas_mutated = false;
u32   g_ui_rng = 0x51A7CEu;
u32   g_ui_frame = 0;
int g_seq_lock_flash = 0;

// Presentation only: the destination is already live. No input ownership, heap,
// engine pointers or retained LCD surfaces. Frame is frozen before C3D begins.
struct NavInkTransition {
    u64 started_tick = 0;
    View view = View::Chains;
    Overlay overlay = Overlay::None;
    nav_ink::Frame frame;
};
NavInkTransition g_nav_ink;
static_assert(sizeof(NavInkTransition) <= 40, "bounded navigation presentation");

void mark_project_mutated() {
    if (g_eng) g_eng->invalidate_structural_undo();
    g_project_session.mark_mutated();
}
void mark_project_structural_mutated() { g_project_session.mark_mutated(); }
bool project_is_dirty() { return g_project_session.dirty(); }
int project_active_slot() { return g_project_session.active_slot(); }



// ===================== small helpers =====================

const char* LAST_PATCH_PATH = "sdmc:/3ds/seance/last_patch.bin";
const char* START_MENU_PATH = "sdmc:/3ds/seance/start_menu.bin";
const char* PROJECT_NAMES_PATH = "sdmc:/3ds/seance/project_names.bin";

bool save_project_names(const ProjectNames& proposed) {
    if (!project_names_save(PROJECT_NAMES_PATH, proposed)) return false;
    g_project_names = proposed;
    return true;
}
StartMenuStore g_start_store;
bool g_start_store_write_failed = false;

uint64_t start_timestamp() {
    const time_t now = std::time(nullptr);
    return now > 0 ? (uint64_t)now : 0;
}

void remember_project(int slot) {
    // Successful LOAD only (manual menu/browser or startup resume), never SAVE.
    start_store_touch(g_start_store, slot, start_timestamp());
    g_start_store_write_failed = !start_store_save_preferences(START_MENU_PATH, g_start_store);
}

int load_last_patch_slot() {
    FILE* f = fopen(LAST_PATCH_PATH, "rb");
    if (!f) return -1;
    unsigned char data[5] = {};
    const bool ok = fread(data, 1, sizeof data, f) == sizeof data;
    fclose(f);
    return ok && !std::memcmp(data, "SLOT", 4) && data[4] < PROJECT_SLOTS ? data[4] : -1;
}

void save_last_patch_slot() {
    // NEW/demo projects are deliberately not attached to a persisted slot.
    // Never cast -1 to 0xff and let the next boot pretend slot 0 was active.
    const int active_slot = g_project_session.active_slot();
    if (!project_slot_valid(active_slot)) {
        remove(LAST_PATCH_PATH);
        return;
    }
    (void)last_patch_store_save(LAST_PATCH_PATH, active_slot);
}

static void persist_last_project() {
    // A detached recovered draft must not erase the old manual resume pointer.
    // Explicit Save attaches a slot; successful New/load clears this exception.
    if (!g_recovered_session || project_slot_valid(g_project_session.active_slot()))
        save_last_patch_slot();
}

void patch_path(int slot, char* out, size_t n) {
    if (!project_slot_valid(slot)) { if (n) out[0] = 0; return; }
    snprintf(out, n, "sdmc:/3ds/seance/patch%d.snc", slot);
}

bool transactional_patch_load(const char* path) {
    { platform::AudioOut::Guard guard(*g_audio); if (g_eng->play_editor_busy()) return false; }
    patchio::SampleManifest* manifest = new(std::nothrow) patchio::SampleManifest();
    if (!manifest) return false;
    Engine* staged = new(std::nothrow) Engine();
    WorkspaceLayout* staged_workspace = new(std::nothrow) WorkspaceLayout();
    if (!staged || !staged_workspace || !patchio::load(*staged, path, manifest, staged_workspace)) {
        delete staged; delete staged_workspace; delete manifest; return false;
    }

    // SNC1-SNC4 predate sample provenance. Preserve their historical convention
    // without contaminating modern SNC5+ projects with implicit factory slots.
    if (!manifest->has_sample_manifest) {
        for (int slot = 0; slot < 16; ++slot) {
            auto& entry = manifest->slots[slot];
            entry.origin = (uint8_t)trackr::synth::SampleOrigin::SdWav;
            std::snprintf(entry.path, sizeof entry.path, "%s/s%02d.wav", SAMPLE_DIR, slot);
        }
    }

    // Decode and allocate outside the real-time lock, then atomically replace
    // SampleBank vectors while the audio worker is excluded.
    patchio::RestoredSamples* restored = new(std::nothrow) patchio::RestoredSamples();
    if (!restored) { delete staged; delete staged_workspace; delete manifest; return false; }
    const int restored_count = patchio::decode_samples(*manifest, ENGINE_SR, ENGINE_SR * 20, *restored);
    if (restored_count < 0) { delete restored; delete staged; delete staged_workspace; delete manifest; return false; }
    // Release momentary live controls before taking the patch-swap guard;
    // performance_view_reset() takes that guard itself.
    performance_view_reset();
    relic_focus_release();
    bool commit_ok = false;
    {
        platform::AudioOut::Guard g(*g_audio);
        commit_ok = !g_eng->play_editor_busy() && patchio::commit_restored_samples(*manifest, *restored, staged, g_eng);
        if (commit_ok) {
            for (int c = 0; c < NUM_CHAINS; ++c)
                for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
                    Unit* u = staged->chains[c].units[s];
                    if (u && u->kind == UnitKind::Recorder) {
                        auto* rec = static_cast<RecUnit*>(u);
                        const auto& sample = trackr::synth::SampleBank::instance().slot(rec->sample_slot);
                        if (!sample.empty()) rec->restore_take(sample.playable_frames());
                        // Empty REC slots stay unprepared. Explicit REC preflights a
                        // private workspace before any quantized audio-thread start.
                    }
                }
            g_eng->swap_patch_state(*staged);
            module_rand_rebase();
            g_eng->set_external_sample_slot(sample_ownership::unit_slot(g_clip_unit));
            g_eng->transport_stop();
            g_eng->set_sequencer_enabled(false);
            sequencer_forget_pins();
            g_seq_lock_flash = 0;
        }
    }
    if (!commit_ok) {
        delete restored; delete staged; delete staged_workspace; delete manifest;
        return false;
    }
    transition::forget_launch_tracking(); // never dirty the new project for an old queued bank
    delete restored;
    delete manifest;
    workspace_apply_layout(*staged_workspace);
    delete staged_workspace;
    delete staged; // destroys previous patch units after audio lock is released
    patchbay_forget();
    sigils_forget_undo(); // never apply a previous project's stroke after a successful load
    nav_close(); // a clock draft must never survive an in-place project swap
    g_loaded_samples = restored_count;
    g_sample_flash = 120;
    return true;
}

ModSource sigil_source(int sigil) { return sigil_mod_source(sigil); }


static const char* g_patch_save_error = nullptr;

// Caller holds the audio guard. Include both single-REC and chain-REC queued
// commands: a take that has not reached its quantized start is not yet safe.
static bool project_recordings_ready(bool require_saved) {
    if (g_eng->play_editor_busy()) return false;
    if (require_saved) for (const auto& chain : g_eng->chains)
        for (const Unit* unit : chain.units)
            if (unit && unit->kind == UnitKind::Recorder &&
                static_cast<const RecUnit*>(unit)->take_needs_save()) return false;
    return true;
}

static void recovery_idle_tick(bool input_quiet, u32 fresh_down, bool transport_was_playing = true) {
    const uint64_t now = svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000);
    static uint32_t lifecycle_seen = 0;
    const uint32_t lifecycle = g_audio->lifecycle_epoch();
    recovery::State state;
    state.quiet = input_quiet && lifecycle == lifecycle_seen && g_drag == Drag::None &&
        g_overlay == Overlay::None && !nav_is_open() && !g_project_session.has_pending() &&
        !param_lens_is_open() && !chains_context_is_open() && !play_editor_word_modal() &&
        !rot_focus_menu_modal() && !rule_focus_menu_modal() && !pent_focus_menu_modal() &&
        !grain_focus_menu_modal() && g_view != View::Bounce && g_save_flash == 0;
    lifecycle_seen = lifecycle;
    if (g_recovery.disabled() && !g_recovery.unresolved()) {
        // A successful project swap cannot undo a failed durable disposition.
        // Keep its retained fallback visible instead of implying autosave works.
        g_save_flash=180;
        std::snprintf(g_patch_notice,sizeof g_patch_notice,"recovery retained / snapshots disabled");
    }
    {
        platform::AudioOut::Guard guard(*g_audio);
        // START must have reached an actual transport-play route. Modal closing,
        // R+START reset, navigation A and startup/lifecycle tails never authorize.
        if ((fresh_down & KEY_START) && !transport_was_playing && g_eng->transport_playing)
            g_audio->authorize_recovered_output();
        state.stopped = !g_eng->transport_playing;
        state.safe = !g_eng->recording_active_or_pending() && project_recordings_ready(true);
        state.revision = g_project_session.revision();
        state.random_revision = g_eng->module_random_revision();
        state.dirty = g_project_session.dirty();
    }
    if (!g_recovery.due(now,state)) return;
    patchio::PatchDocument document; // heap constructed BEFORE guard
    WorkspaceLayout workspace; workspace_get_layout(workspace);
    bool captured = false;
    if (document.valid()) {
        platform::AudioOut::Guard guard(*g_audio);
        captured = !g_eng->transport_playing && !g_eng->recording_active_or_pending() &&
            project_recordings_ready(true) && state.revision == g_project_session.revision() &&
            state.random_revision == g_eng->module_random_revision() &&
            patchio::capture(*g_eng,document,&workspace);
    }
    recovery::Record committed;
    const bool ok = captured && g_recovery_store.checkpoint(document,g_recovery.epoch(),state.revision,committed);
    // Audio-clocked rand can advance during SD I/O. Its newer bases stay dirty;
    // coverage names only the captured revision, never the subsequently polled one.
    module_rand_poll();
    // Backoff/next cadence begin at completion, even after a long SD stall.
    g_recovery.attempted(svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000),state,ok,committed);
    if (!ok) {
        g_save_flash=180;
        std::snprintf(g_patch_notice,sizeof g_patch_notice,"snapshot failed / previous retained");
    }
}

static bool recovery_retire_covered() {
    if (g_recovery.unresolved() || !g_recovery.covered_generation()) return true;
    const bool ok = g_recovery_store.dispose(g_recovery.covered_generation());
    g_recovery.retired(ok);
    return ok;
}

bool save_current_patch(int slot) {
    g_patch_save_error = nullptr;
    if (!project_slot_valid(slot)) return false;
    {
        platform::AudioOut::Guard guard(*g_audio);
        if (!project_recordings_ready(false)) {
            g_patch_save_error = "stop rec before saving";
            if (!g_project_session.dirty()) g_project_session.mark_mutated();
            return false;
        }
    }
    char path[96]; patch_path(slot, path, sizeof path);
    const int old_edit_chain = g_edit_chain, old_edit_slot = g_edit_slot;
    bool recordings_ok = true;
    for (int c = 0; c < NUM_CHAINS; ++c) {
        for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
            Unit* u = g_eng->chains[c].units[s];
            if (!u || u->kind != UnitKind::Recorder) continue;
            auto* rec = static_cast<RecUnit*>(u);
            bool should_export = false;
            {
                platform::AudioOut::Guard guard(*g_audio);
                Unit* current = g_eng->chains[c].units[s];
                if (current && current->kind == UnitKind::Recorder) {
                    rec = static_cast<RecUnit*>(current);
                    should_export = !rec->writer_open() && rec->take_needs_save();
                }
            }
            if (!should_export) continue;
            g_edit_chain = c; g_edit_slot = s;
            export_current_sample();
            recordings_ok = recordings_ok && g_export_ok;
        }
    }
    g_edit_chain = old_edit_chain; g_edit_slot = old_edit_slot;
    if (!recordings_ok) { g_patch_save_error = "recording export failed / retry save"; return false; }
    patchio::PatchDocument document;
    if (!document.valid()) { g_patch_save_error = "save failed / out of memory"; return false; }
    uint64_t saved_random_revision=0;
    WorkspaceLayout workspace;
    workspace_get_layout(workspace);
    {
        platform::AudioOut::Guard g(*g_audio);
        if (!project_recordings_ready(true)) {
            g_patch_save_error = "finish rec / retry save";
            if (!g_project_session.dirty()) g_project_session.mark_mutated();
            return false;
        }
        if (!patchio::capture(*g_eng, document, &workspace)) { g_patch_save_error = "save failed / capture refused"; return false; }
        saved_random_revision=g_eng->module_random_revision();
    }
    // The document owns every persisted scalar/table/path needed by SNC7. SD
    // serialization/flush/rename stay outside the audio guard (SD can stall UI).
    patchio::SaveFailure failure = patchio::SaveFailure::None;
    const bool ok = patchio::save(document, path, &failure);
    if (!ok) switch (failure) {
        // The failing step, so an SD refusal is not reported like a bad project.
        case patchio::SaveFailure::Invalid: g_patch_save_error = "save failed / project not admitted"; break;
        case patchio::SaveFailure::Create: g_patch_save_error = "save failed / cannot create file on sd"; break;
        case patchio::SaveFailure::Write: g_patch_save_error = "save failed / sd write error"; break;
        case patchio::SaveFailure::Publish: g_patch_save_error = "save failed / cannot place file on sd"; break;
        case patchio::SaveFailure::None: break;
    }
    if (ok) {
        // A new/copy save can replace a different project in this slot. Drop
        // that project's open history, but do not pretend the new file was
        // opened. Do this after the actual write, before post-save REC checks:
        // even if those checks fail, the destination has already changed.
        if (g_project_session.active_slot() != slot) {
            start_store_forget(g_start_store, slot);
            g_start_store_write_failed = !start_store_save_preferences(START_MENU_PATH, g_start_store);
        }
        // Audio continued during SD I/O. Never authorize save-and-leave if a
        // queued/gated REC started meanwhile, even if the SNC write succeeded.
        {
            platform::AudioOut::Guard guard(*g_audio);
            if (!project_recordings_ready(true)) {
                g_patch_save_error = "rec changed / retry save";
                if (!g_project_session.dirty()) g_project_session.mark_mutated();
                return false;
            }
            if (g_eng->module_random_revision()!=saved_random_revision) {
                g_patch_save_error = "rand changed / retry save";
                module_rand_poll();
                return false; // saved snapshot is valid, but newer bases are still dirty
            }
            module_rand_poll(); // acknowledge exactly the revision stored on disk
            g_project_session.mark_saved(slot);
        }
        // Only after the existing REC/rand postcheck. Never under the guard;
        // failed disposition keeps recovery and disables further checkpoints.
        if (!recovery_retire_covered()) g_patch_save_error = "saved / recovery retained";
    }
    return ok;
}

// Directory discovery is not validation. FAT path lookup is costly even for an
// empty slot: never issue 128 stat/open pairs merely to show either project menu.
static int patch_slot_from_name(const char* name) {
    const size_t length = std::strlen(name);
    if (length < 10 || length > 12) return -1;
    char lower[13] = {};
    for (size_t i = 0; i < length; ++i)
        lower[i] = name[i] >= 'A' && name[i] <= 'Z' ? name[i] + ('a' - 'A') : name[i];
    if (std::strncmp(lower, "patch", 5) || std::strcmp(lower + length - 4, ".snc")) return -1;
    if (length > 10 && lower[5] == '0') return -1;
    int slot = 0;
    for (size_t i = 5; i < length - 4; ++i) {
        if (lower[i] < '0' || lower[i] > '9') return -1;
        slot = slot * 10 + lower[i] - '0';
    }
    return project_slot_valid(slot) ? slot : -1;
}

void scan_patch_slots() {
    if (g_patch_catalog_ready) return;
    bool found[PROJECT_SLOTS] = {}, blocked[PROJECT_SLOTS] = {};
    DIR* dir = opendir("sdmc:/3ds/seance");
    bool ok = dir != nullptr;
    if (dir) {
        for (;;) {
            errno = 0;
            const dirent* entry = readdir(dir);
            if (!entry) { if (errno) ok = false; break; }
            const int slot = patch_slot_from_name(entry->d_name);
            if (slot < 0) continue;
            found[slot] = true;
            blocked[slot] = entry->d_type != DT_REG && entry->d_type != DT_UNKNOWN;
        }
        if (closedir(dir) != 0) ok = false;
    }
    // A partial/failed directory read must not invent empty destinations. Cache
    // the error too: retrying a broken SD directory every frame/open would stall.
    for (int i = 0; i < PROJECT_SLOTS; ++i) {
        g_patch_exists[i] = !ok || found[i];
        g_patch_checked[i] = !ok || !found[i] || blocked[i];
        g_patch_valid[i] = false;
        g_patch_saved_at[i] = 0;
    }
    g_patch_catalog_failed = !ok;
    g_patch_catalog_ready = true;
}

static void patch_library_invalidate(int slot);
void refresh_patch_slot(int slot) {
    if (!project_slot_valid(slot)) return;
    patch_library_invalidate(slot); // explicit selected-path refresh invalidates its saved preview only
    char path[96]; patch_path(slot, path, sizeof path);
    struct stat st{};
    const int status = stat(path, &st);
    // Only ENOENT proves free. Unreadable files/directories stay occupied.
    g_patch_exists[slot] = status == 0 || errno != ENOENT;
    g_patch_saved_at[slot] = status == 0 && st.st_mtime > 0 ? uint64_t(st.st_mtime) : 0;
    g_patch_checked[slot] = true;
    g_patch_valid[slot] = false;
    if (status != 0 || !S_ISREG(st.st_mode)) return;
    FILE* f = fopen(path, "rb");
    if (!f) return;
    unsigned char h[5] = {};
    const bool recognized = fread(h, 1, sizeof h, f) == sizeof h && !ferror(f) &&
        h[0] == 'S' && h[1] == 'N' && h[2] == 'C' &&
        ((h[3] >= '1' && h[3] <= '9' && h[4] == (unsigned char)(h[3] - '0')) ||
         (h[3] == 'A' && h[4] == 10) || (h[3] == 'B' && h[4] == 11) ||
         (h[3] == 'C' && h[4] == 12) || (h[3] == 'G' && h[4] == 16) || (h[3] == 'H' && h[4] == 17) || (h[3] == 'I' && h[4] == 18) || (h[3] == 'J' && h[4] == 19) || (h[3] == 'K' && h[4] == 20) || (h[3] == 'L' && h[4] == 21) || (h[3] == 'M' && h[4] == 22) || (h[3] == 'E' && h[4] == 14) || (h[3] == 'F' && h[4] == 15) || (h[3] == 'D' && h[4] == 13));
    const bool closed = fclose(f) == 0;
    g_patch_valid[slot] = recognized && closed;
}

enum class PatchAction { Save, Load, Delete, NewProject, Rename };

void patch_browser_result(bool ok, PatchAction action, int slot = -1) {
    g_save_flash = 120;
    const char* verb = "";
    switch (action) {
        case PatchAction::Save: verb = ok ? "saved to" : "save failed for"; break;
        case PatchAction::Load: verb = ok ? "loaded" : "load failed for"; break;
        case PatchAction::Delete: verb = ok ? "deleted" : "delete failed for"; break;
        case PatchAction::Rename: verb = "renamed"; break; // failures use the retry notice
        case PatchAction::NewProject:
            std::snprintf(g_patch_notice, sizeof g_patch_notice, "new project failed");
            return; // successful NEW has no result toast
    }
    const int target = slot >= 0 ? slot : g_patch_slot;
    if (action == PatchAction::Save && g_patch_save_error)
        std::snprintf(g_patch_notice, sizeof g_patch_notice, "%s", g_patch_save_error);
    else std::snprintf(g_patch_notice, sizeof g_patch_notice, "%s p.%03d", verb, target + 1);
    // Only this entry changed. A new project/rename never requires a disk scan.
    if (action == PatchAction::Save || action == PatchAction::Delete) refresh_patch_slot(target);
}

bool execute_destructive(PendingDestructiveAction pending) {
    if (bounce_guard_project_change()) { g_project_session.cancel_pending(); return false; }
    { platform::AudioOut::Guard guard(*g_audio);
      if (g_eng->play_editor_busy()) {
          g_project_session.cancel_pending(); g_save_flash=120;
          std::snprintf(g_patch_notice,sizeof g_patch_notice,"stop rec before leaving project");
          g_overlay=Overlay::PatchBrowser; return false;
      }
    }
    if (pending.action == DestructiveAction::NewProject) {
        Engine* fresh = new(std::nothrow) Engine();
        patchio::RestoredSamples* retired = new(std::nothrow) patchio::RestoredSamples();
        if (!fresh || !retired) { delete fresh; delete retired; patch_browser_result(false, PatchAction::NewProject); return false; }
        install_demo_patch(*fresh);
        if (!fresh->chains[0].units[0] || !fresh->chains[0].units[1] || !fresh->chains[0].units[2]) {
            delete fresh; delete retired; patch_browser_result(false, PatchAction::NewProject); return false;
        }
        fresh->transport_stop();
        performance_view_reset();
        bool ready=false;
        {
            platform::AudioOut::Guard guard(*g_audio);
            ready=!g_eng->play_editor_busy();
            if (ready) {
            relic_focus_release();
            g_eng->retire_relic_sources_all();
            g_eng->swap_patch_state(*fresh);
            module_rand_rebase();
            g_eng->set_external_sample_slot(sample_ownership::unit_slot(g_clip_unit));
            trackr::synth::SampleBank::instance().swap_all(
                retired->decoded, patchio::SampleManifest::SLOT_COUNT);
            for (int slot = 0; slot < patchio::SampleManifest::SLOT_COUNT; ++slot)
                trackr::synth::SampleBank::instance().set_provenance(
                    slot, trackr::synth::SampleOrigin::Runtime, "");
            }
        }
        if (!ready) { delete fresh; delete retired; patch_browser_result(false,PatchAction::NewProject); return false; }
        transition::forget_launch_tracking();
        delete fresh;
        delete retired; // owns/frees the previous bank after releasing the guard
        g_loaded_samples = 0;
        g_sample_flash = 120;
        workspace_reset_all();
        g_recovery.context_changed(svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000), recovery_retire_covered());
        g_project_session.reset_new();
        g_recovered_session = false;
        nav_close();
        sigils_forget_undo();
        patchbay_forget();
        sequencer_forget_pins();
        g_view = View::Chains;
        g_sel_chain = g_edit_chain = 0;
        g_sel_slot = g_edit_slot = 0;
        g_overlay = Overlay::None;
        return true;
    }
    if (pending.action == DestructiveAction::LoadProject) {
        const int slot = pending.slot;
        if (!project_slot_valid(slot)) { patch_browser_result(false, PatchAction::Load, slot); return false; }
        refresh_patch_slot(slot);
        if (!g_patch_valid[slot]) { patch_browser_result(false, PatchAction::Load, slot); return false; }
        char path[96]; patch_path(slot, path, sizeof path);
        const bool ok = transactional_patch_load(path);
        if (ok) {
            g_recovery.context_changed(svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000), recovery_retire_covered());
            g_project_session.reset_loaded(slot);
            g_recovered_session = false;
            remember_project(slot);
        }
        patch_browser_result(ok, PatchAction::Load, slot);
        g_overlay = ok ? Overlay::None : Overlay::PatchBrowser;
        return ok;
    }
    if (pending.action == DestructiveAction::ExitApp) {
        // This is an explicit saved/discarded leave, unlike aptMainLoop false.
        if (!recovery_retire_covered()) {
            g_save_flash = 180;
            std::snprintf(g_patch_notice,sizeof g_patch_notice,"recovery retained / retry exit");
            return false;
        }
        g_exit_requested = true; return true;
    }
    return false;
}

static void patch_library_request_confirmation();
void request_destructive(DestructiveAction action, int slot = -1) {
    if (g_project_session.request(action, slot)) execute_destructive({action, slot});
    else patch_library_request_confirmation();
}

#include "project_library_ui.inc"

// touch position -> q15 value along a horizontal track
q15 norm_h(int tx, int x, int w, bool bipolar) {
    int32_t v = (int32_t)(tx - x) * 32767 / (w - 1);
    if (v < 0) v = 0;
    if (v > 32767) v = 32767;
    if (bipolar) v = v * 2 - 32767;
    return (q15)v;
}

q15 clamp_q15_i32(int32_t v) {
    if (v < -32767) return -32767;
    if (v >  32767) return  32767;
    return (q15)v;
}

bool chain_is_soloed(int chain) {
    for (int c = 0; c < NUM_CHAINS; ++c) {
        bool want_mute = c != chain;
        if (g_eng->chains[c].mute != want_mute) return false;
    }
    return true;
}


} // namespace seance::platform::ui3ds

static void draw_bottom_surface(View view, Overlay overlay) {
    // Both project surfaces are opaque; don't spend the shared object pool on
    // hidden chains, sequencer, performance or sigils underneath them.
    if (overlay == Overlay::PatchBrowser) { patch_browser_draw(); return; }
    if (overlay == Overlay::ConfirmDiscard) { confirm_discard_draw(); return; }
    if (view == View::Bounce) { bounce_draw(); return; }
    if (view == View::Patchbay) { patchbay_draw(); return; }
    if (view == View::Chains) {
        // Opaque sample paper owns this LCD; do not submit hidden chain objects.
        if (overlay == Overlay::SampleBrowser) { sample_browser_draw(); return; }
        if (overlay == Overlay::Editor) {
            if (!chains_editor_covers()) editor_draw();
            chains_editor_draw(false);
        } else if (overlay == Overlay::Picker) picker_draw();
        else chains_view_draw();
    } else if (view == View::Sequencer) {
        sequencer_view_draw();
        if (overlay == Overlay::Editor) editor_draw();
    } else if (view == View::Performance) {
        performance_view_draw();
    } else {
        sigils_view_draw();
    }
}

static void cancel_nav_ink_transition() {
    g_nav_ink = {};
    nav_transition_clear();
}

static void begin_nav_ink_transition(View view, Overlay overlay) {
    using T = nav_ink::Target;
    T target = T::Chains;
    if (overlay == Overlay::PatchBrowser) target = T::Patches;
    else if (overlay != Overlay::None) { cancel_nav_ink_transition(); return; }
    else switch (view) {
        case View::Chains: target = T::Chains; break;
        case View::Sigils: target = T::Sigils; break;
        case View::Sequencer: target = T::Sequencer; break;
        case View::Performance: target = T::Performance; break;
        case View::Bounce: target = T::Bounce; break;
        case View::Patchbay: cancel_nav_ink_transition(); return; // native title, no unrelated cover
    }
    g_nav_ink.started_tick = svcGetSystemTick();
    g_nav_ink.view = view;
    g_nav_ink.overlay = overlay;
    g_nav_ink.frame.target = target;
    g_nav_ink.frame.elapsed_ms = 0;
    g_nav_ink.frame.active = true;
}

static void nav_ink_input(u32 down) {
    // Never eat an action to finish decoration. Fresh touch/buttons reveal the
    // already-live destination BEFORE the unchanged controller sees that edge.
    // The selecting press's held tail remains owned by the old cancellations.
    if (g_nav_ink.frame.active && (down & ~u32(KEY_ZL)))
        cancel_nav_ink_transition();
}

static void capture_nav_ink_transition() {
    if (!g_nav_ink.frame.active) return;
    if (nav_is_open() || g_view != g_nav_ink.view || g_overlay != g_nav_ink.overlay) {
        cancel_nav_ink_transition();
        return;
    }
    const u64 now = svcGetSystemTick();
    if (now < g_nav_ink.started_tick) { cancel_nav_ink_transition(); return; }
    const u64 elapsed = (now - g_nav_ink.started_tick) / (SYSCLOCK_ARM11 / 1000);
    if (elapsed >= nav_ink::kDurationMs) { cancel_nav_ink_transition(); return; }
    g_nav_ink.frame.elapsed_ms = static_cast<unsigned>(elapsed);
}

static void commit_nav_destination(View from_view, Overlay from_overlay,
                                   View to_view, Overlay to_overlay) {
    relic_focus_release();
    stranular_focus_cancel();
    reverb_focus_release();
    inst_focus_release();
    glitch_focus_release();
    arp_focus_release();
    mouth_focus_release();
    comp_focus_release();
    fract_focus_release();
    patchbay_cancel_input();
    picker_cancel_input();
    chains_close_context_branch();
    // A held stylus must not keep ownership after GO TO closes. Cancelling all
    // captures here also makes a still-held touch inert on the destination.
    chains_view_touch_release(false);
    sequencer_view_cancel_input();
    performance_view_touch_release(false);
    g_drag = Drag::None;
    g_last_px = -1;
    g_smooth_active = false;

    // Publish the destination before this frame's capture/draw. nav_had_frame
    // still consumes the selecting edge; cancelled captures cannot inherit it.
    g_view = to_view;
    g_overlay = to_overlay;
    if (from_view == to_view && from_overlay == to_overlay) cancel_nav_ink_transition();
    else begin_nav_ink_transition(to_view, to_overlay);
}

static void draw_nav_ink_transition(C3D_RenderTarget* top, C3D_RenderTarget* bot) {
    // Incoming upper only: no stale engine-backed source view is resurrected.
    // Both draw passes consume the same pre-GPU scalar motion frame.
    C2D_TargetClear(top, col_bg());
    C2D_SceneBegin(top);
    if (!project_overlay_draw_top()) {
        if (g_view == View::Bounce) bounce_draw_top();
        else top_draw();
    }
    nav_ink::draw_top_cover(g_nav_ink.frame);
    bounce_draw_indicator(); // recording remains visible throughout navigation

    C2D_TargetClear(bot, col_bg());
    C2D_SceneBegin(bot);
    // The popup has no GO TO heading to morph. Keep the immediate, interruptible
    // 200ms handoff, but fade the exact frozen menu without resurrecting a title.
    const int ms = int(g_nav_ink.frame.elapsed_ms);
    if (ms <= nav_ink::detail::kRevealMs) {
        nav_draw_transition();
        nav_ink::detail::paper(320, nav_ink::detail::source_veil(ms));
    } else {
        draw_bottom_surface(g_view, g_overlay);
        nav_ink::detail::paper(320, 255 - nav_ink::detail::destination_alpha(ms));
    }
}

// ===================== input dispatch =====================

static platform::LifecycleInput lifecycle_input;
static void cancel_lifecycle_input() {
    // UI-thread cancellation only. Never turn an interrupted press into a lift.
    editor_cancel_input();
    param_lens_cancel();
    recorder_focus_cancel_lifecycle();
    sample_browser_touch_release(false);
    sample_browser_stop_preview();
    project_overlay_cancel_lifecycle();
    if (g_overlay == Overlay::ConfirmDiscard) g_overlay = Overlay::None;
    nav_close();
    nav_transition_clear();
    cancel_nav_ink_transition();
    chains_view_touch_release(false);
    chains_close_context_branch();
    chains_editor_cancel_input();
    picker_cancel_input();
    sequencer_view_cancel_input();
    performance_view_leave(); // existing cancel/return endpoint; protected painter untouched
    bounce_cancel_input();
    patchbay_cancel_input();
    sigils_close_mode_menu();
    // Canvas points already written before sleep are data, not a deferred lift.
    if (g_sig_canvas_mutated) { mark_project_mutated(); g_sig_canvas_mutated = false; }
    g_drag = Drag::None; g_last_px = -1; g_smooth_active = false;
}
static u32 poll_lifecycle_input(bool startup = false) {
    if (!g_audio) return 0;
    const u32 epoch = g_audio->lifecycle_epoch();
    if (lifecycle_input.observe(epoch)) {
        if (startup) start_menu_cancel_lifecycle();
        else cancel_lifecycle_input();
    }
    return epoch;
}

static void handle_touch(u32 down, u32 held) {
    // Project actions own press/hold/lift as one gesture on every underlying
    // view. A lift that changes project/page must not reach the revealed editor.
    if (g_overlay == Overlay::PatchBrowser || g_overlay == Overlay::ConfirmDiscard) {
        if (!(held & KEY_TOUCH)) {
            project_overlay_touch_release(true);
            g_drag = Drag::None; g_last_px = -1; g_smooth_active = false;
        } else {
            touchPosition t; hidTouchRead(&t);
            if (down & KEY_TOUCH) project_overlay_touch_down(t.px, t.py);
            else project_overlay_touch_hold(t.px, t.py);
        }
        return;
    }
    // A browser lift may return to the editor. Own that whole edge so the newly
    // revealed editor cannot receive the same release or a stale Drag capture.
    if (g_view == View::Chains && g_overlay == Overlay::SampleBrowser) {
        if (!(held & KEY_TOUCH)) {
            sample_browser_touch_release(true);
            g_drag = Drag::None; g_last_px = -1; g_smooth_active = false;
        } else {
            touchPosition t; hidTouchRead(&t);
            if (down & KEY_TOUCH) sample_browser_touch_down(t.px, t.py);
            else sample_browser_touch_hold(t.px, t.py);
        }
        return;
    }
    // REC's save/discard/cancel owns its press, motion and real lift. None of
    // these edges can edit length, open MOD, or hit a command behind the choice.
    if (recorder_focus_confirming()) {
        touchPosition t{};
        if (held & KEY_TOUCH) hidTouchRead(&t);
        recorder_focus_confirm_touch((down & KEY_TOUCH) != 0, (held & KEY_TOUCH) != 0, t.px, t.py);
        g_drag = Drag::None; g_last_px = -1; g_smooth_active = false;
        return;
    }
    if (g_view == View::Patchbay && g_overlay == Overlay::None) {
        if (!(held & KEY_TOUCH)) patchbay_touch_release();
        else {
            touchPosition t; hidTouchRead(&t);
            if (down & KEY_TOUCH) patchbay_touch_down(t.px, t.py);
            else patchbay_touch_hold(t.px, t.py);
        }
        return;
    }
    if (g_view == View::Bounce && g_overlay == Overlay::None) {
        if (!(held & KEY_TOUCH)) bounce_touch_release();
        else {
            touchPosition t; hidTouchRead(&t);
            if (down & KEY_TOUCH) bounce_touch_down(t.px, t.py);
            else bounce_touch_hold(t.px, t.py);
        }
        return;
    }
    if (!(held & KEY_TOUCH)) {
        // The chain lift may OPEN the editor/picker. It belongs to its original
        // owner only; never synthesize a release on the newly opened controller.
        const Overlay lift_overlay = g_overlay;
        chains_view_touch_release(true);
        if (lift_overlay != g_overlay) {
            g_drag = Drag::None; g_last_px = -1; g_smooth_active = false;
            return;
        }
        sequencer_view_touch_release(true);   // real lift: commit pending step taps
        performance_view_touch_release(true);
        if (g_view == View::Chains && g_overlay == Overlay::Picker)
            picker_touch_release();
        if ((g_view == View::Chains || g_view == View::Sequencer) && g_overlay == Overlay::Editor)
            editor_touch_release();
        sigils_view_touch_release();
        if (g_sig_canvas_mutated) { mark_project_mutated(); g_sig_canvas_mutated = false; }
        g_drag = Drag::None; g_last_px = -1; g_smooth_active = false;
        return;
    }
    touchPosition t;
    hidTouchRead(&t);
    int tx = t.px, ty = t.py;
    // Passive button decoration observes motion even when the logical control
    // was a one-shot toggle (and therefore owns no Drag enum).
    if (!(down & KEY_TOUCH)) small_press::drag(tx, ty);

    if (down & KEY_TOUCH) {
        // touch-down edge: hit test resolves either a tap or a drag capture
        if (g_view == View::Chains) {
            switch (g_overlay) {
                case Overlay::None:   chains_view_touch_down(tx, ty); break;
                case Overlay::Picker: picker_touch_down(tx, ty); break;
                case Overlay::Editor: editor_touch_down(tx, ty); break;
                case Overlay::SampleBrowser: break; // owned above
                case Overlay::PatchBrowser: patch_browser_touch_down(tx, ty); break;
                case Overlay::ConfirmDiscard: confirm_discard_touch_down(tx, ty); break;
            }
        } else if (g_view == View::Sequencer) {
            if (g_overlay == Overlay::ConfirmDiscard) confirm_discard_touch_down(tx, ty);
            else if (g_overlay == Overlay::PatchBrowser) patch_browser_touch_down(tx, ty);
            else if (g_overlay == Overlay::Editor) editor_touch_down(tx, ty);
            else if (g_overlay == Overlay::None) sequencer_view_touch_down(tx, ty);
        } else if (g_view == View::Performance) {
            if (g_overlay == Overlay::ConfirmDiscard) confirm_discard_touch_down(tx, ty);
            else if (g_overlay == Overlay::PatchBrowser) patch_browser_touch_down(tx, ty);
            else if (g_overlay == Overlay::None) performance_view_touch_down(tx, ty);
        } else {
            if (g_overlay == Overlay::ConfirmDiscard) confirm_discard_touch_down(tx, ty);
            else if (g_overlay == Overlay::PatchBrowser) patch_browser_touch_down(tx, ty);
            else if (g_overlay == Overlay::None) sigils_view_touch_down(tx, ty);
        }
        return;
    }

    // Modulation panel owns its bounded touch capture, not a module Drag enum.
    if (g_view == View::Chains && g_overlay == Overlay::Editor && param_lens_is_open()) {
        param_lens_touch_drag(tx, ty);
        return;
    }
    // continued touch: route to the captured widget
    if (g_view == View::Chains && g_overlay == Overlay::None)
        chains_view_touch_hold(tx, ty);
    if (g_view == View::Chains && g_overlay == Overlay::Picker)
        picker_touch_hold(tx, ty);
    if (g_view == View::Sequencer && g_overlay == Overlay::None)
        sequencer_view_touch_hold(tx, ty);
    if (g_view == View::Performance && g_overlay == Overlay::None)
        performance_view_touch_hold(tx, ty);
    switch (g_drag) {
        case Drag::ParamBase:
        case Drag::PlaySpeed:
        case Drag::PlayStart:
        case Drag::PlayLength:
        case Drag::PlayGain:
        case Drag::PlayModDepth:
        case Drag::PlaySliceSeam:
        case Drag::GrainPosition:
        case Drag::OscShape: // modern OSC uses one relative capture for all seven controls
        case Drag::NoiseDensity:
        case Drag::FmPitch:
        case Drag::FmRatio:
        case Drag::FmIndex:
        case Drag::FmFeedback:
        case Drag::FmDecay:
        case Drag::FmGain:
        case Drag::FmIndexFall:
        case Drag::FmAttack:
        case Drag::FmRatioFine:
        case Drag::FmFeedbackFall:
        case Drag::FmModDepth:
        case Drag::MotorRpm:
        case Drag::VcaGain:
        case Drag::VcaAttack:
        case Drag::VcaDecay:
        case Drag::FilterCutoff:
        case Drag::FilterResonance:
        case Drag::FilterDrive:
        case Drag::FilterMorph:
        case Drag::FoldDrive:
        case Drag::FoldTrim:
        case Drag::FoldMode:
        case Drag::FoldBias:
        case Drag::FoldMix:
        case Drag::ReverbSize:
        case Drag::ChorusRate:
        case Drag::ChorusDepth:
        case Drag::ChorusMix:
        case Drag::TiltBalance:
        case Drag::TiltPivot:
        case Drag::TiltDrive:
        case Drag::TiltMix:
        case Drag::TiltRange:
        case Drag::TiltModDepth:
        case Drag::RotParam:
        case Drag::RuleParam:
        case Drag::PentParam:
        case Drag::PaulParam:
        case Drag::ScratchParam:
        case Drag::InstParam:
        case Drag::GlitchParam:
        case Drag::GlitchCell:
        case Drag::ArpParam:
        case Drag::MouthParam:
        case Drag::CompParam:
        case Drag::FractParam:
        case Drag::FractGrab:
        case Drag::BirdParam:
        case Drag::ShiftParam:
        case Drag::RingFreq:
        case Drag::RingDepth:
        case Drag::RingFeedback:
        case Drag::RingDiode:
        case Drag::RingMix:
        case Drag::RingFollow:
        case Drag::GhostTone:
        case Drag::GhostDecay:
        case Drag::GhostSmear:
        case Drag::GhostDrift:
        case Drag::GhostFreeze:
        case Drag::GhostMix:
        case Drag::GhostFine:
        case Drag::GhostRate:
        case Drag::MicGain:
        case Drag::RecLength:
        case Drag::BitBits:
        case Drag::BitRate:
        case Drag::BitMix:
        case Drag::BitJitter:
        case Drag::BitDropout:
        case Drag::BitDrift:
        case Drag::BitComp:
        case Drag::StrainParam:
        case Drag::StrnPitch:
            if ((g_view == View::Chains || g_view == View::Sequencer) && g_overlay == Overlay::Editor)
                editor_touch_drag(tx, ty);
            break;
        case Drag::PerformanceMorph:
            if (g_view == View::Performance)
                performance_view_touch_drag(tx, ty);
            break;
        case Drag::SigCanvas:
        case Drag::SigRate:
        case Drag::SigDepth:
        case Drag::SigModDepth:
            if (g_view == View::Sigils)
                sigils_view_touch_drag(tx, ty);
            break;
        default: break;
    }
}

static void seed_demo_sigil(Sigil& s) {
    s.clear();
    for (int i = 0; i < SIGIL_POINTS; ++i) {
        int32_t y;
        if (i < 64) y = -32767 + (i * 65534) / 63;
        else        y =  32767 - ((i - 64) * 65534) / 63;
        s.set_point(i, (q15)y);
    }
    s.mode = SigilMode::Loop;
    s.depth = trackr::fx::Q15_ONE;
    s.trigger();
}

static void clear_sample_bank() {
    if(g_eng) g_eng->retire_relic_sources_all();
    auto& bank = trackr::synth::SampleBank::instance();
    for (int slot = 0; slot < trackr::synth::SAMPLE_BANK_SIZE; ++slot) {
        bank.clear_slot(slot);
    }
}

static void install_demo_patch(Engine& e) {
    for (int c = 0; c < NUM_CHAINS; ++c) {
        e.chains[c].volume = trackr::fx::Q15_ONE;
        e.chains[c].pan = 0;
        e.chains[c].mute = false;
        for (int s = 0; s < UNITS_PER_CHAIN; ++s)
            delete e.replace_unit(c, s, nullptr);
    }

    // OSC keeps the first-sound contract independent of SD samples: press A,
    // then draw SG.A and the filter must move immediately.
    auto* osc = static_cast<OscUnit*>(make_unit(UnitKind::Osc));
    auto* filt = static_cast<FilterUnit*>(make_unit(UnitKind::Filter));
    auto* vca = static_cast<VcaUnit*>(make_unit(UnitKind::Vca));
    if (!osc || !filt || !vca) {
        delete osc;
        delete filt;
        delete vca;
        return;
    }
    osc->params[OscUnit::P_GAIN].base = 18000;

    filt->params[FilterUnit::P_CUTOFF].base = 22000;
    filt->params[FilterUnit::P_CUTOFF].src = ModSource::SigilA;
    filt->params[FilterUnit::P_CUTOFF].mod_depth = 9000;
    filt->params[FilterUnit::P_RES].base = 7000;

    // Preserve the demo's approximately 4/93ms articulation with the new map;
    // old quadratic raw values would make its flexible decay only about 16ms.
    vca->params[VcaUnit::P_ATTACK].base = vca_envelope::time_value(4 * 32);
    vca->params[VcaUnit::P_DECAY].base  = vca_envelope::time_value(93 * 32);

    e.replace_unit(0, 0, osc);
    e.replace_unit(0, 1, filt);
    e.replace_unit(0, 2, vca);

    seed_demo_sigil(e.sigils[0]);
    e.set_sigil_rate(0, 10000);
}
// ===================== startup (no engine worker or project edits underneath) =====================

static StartMenuData startup_data() {
    StartMenuData out;
    scan_patch_slots();
    out.active_slot = g_project_session.active_slot();
    const uint64_t now = start_timestamp();
    bool candidates[PROJECT_SLOTS] = {};
    for (int i = 0; i < PROJECT_SLOTS; ++i) {
        auto& p = out.projects[i];
        p.exists = g_patch_exists[i];
        // Unknown files are offered for explicit load, not claimed validated.
        // The selected file is checked immediately before staged SNC/sample load.
        p.valid = candidates[i] = p.exists && (!g_patch_checked[i] || g_patch_valid[i]);
        project_name_label(g_project_names, i, p.name, sizeof p.name);
        uint64_t stamp = 0;
        for (int j = 0; j < g_start_store.count; ++j)
            if (g_start_store.recent[j].slot == i && g_start_store.recent[j].opened) {
                stamp = g_start_store.recent[j].used_at;
                break;
            }
        start_store_age(stamp, now, p.age, sizeof p.age);
    }
    // Unopened fallback entries keep stable slot order. Saved mtime is not
    // last-open provenance and a save/rename must not promote an unopened row.
    static const uint64_t stable_order[PROJECT_SLOTS] = {};
    out.recent_count = start_store_recent(g_start_store, candidates, stable_order,
        g_start_store.count == 0 ? load_last_patch_slot() : -1, out.recent_slots);
    out.recovery_offer = g_recovery_boot.offered() && g_recovery.unresolved();
    out.show_menu = g_start_store.show_menu;
    out.menu_sound = g_start_store.menu_sound;
    out.theme = g_start_store.theme;
    return out;
}

// App-only preference: commit the bounded preference journal before changing
// either screen. No engine mutation, project dirtiness or audio guard / GPU I/O.
static bool save_start_preferences(const StartMenuStore& proposed) {
    if (!start_store_save_preferences(START_MENU_PATH, proposed)) return false;
    g_start_store = proposed;
    g_start_store_write_failed = false;
    palette::animate_to(proposed.theme, osGetTime());
    return true;
}

static void startup_draw(C3D_RenderTarget* top, C3D_RenderTarget* bottom) {
    const uint64_t now_ms = osGetTime();
    palette::capture(now_ms);
    start_menu_capture(now_ms);
    capture_stylus_feedback(hidKeysHeld());
    platform::debug_overlay::before_frame();
    if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) return;
    platform::debug_overlay::after_begin();
    stylus_feedback::upload();
    menu_style::ambient_frame(now_ms);
    C2D_TargetClear(top, col_bg()); C2D_SceneBegin(top);
    start_menu_draw_top();
    start_menu_draw_reveal(400);
    C2D_TargetClear(bottom, col_bg()); C2D_SceneBegin(bottom);
    start_menu_draw_bottom();
    draw_stylus_feedback();
    start_menu_draw_reveal(320); // final veil includes stylus feedback on both LCDs
    platform::debug_overlay::draw(top);
    C3D_FrameEnd(0);
    platform::debug_overlay::after_end();
}

static void startup_screenshot() {
    mkdir(SCREENSHOT_DIR, 0777);
    char top_p[96], bot_p[96];
    for (int n = 0; n < 1000; ++n) {
        std::snprintf(top_p, sizeof top_p, "%s/shot_%03d_top.bmp", SCREENSHOT_DIR, n);
        struct stat st{};
        if (stat(top_p, &st) == 0) continue;
        std::snprintf(bot_p, sizeof bot_p, "%s/shot_%03d_bot.bmp", SCREENSHOT_DIR, n);
        platform::save_screenshots(top_p, bot_p);
        break;
    }
}

static bool recovery_restore() {
    char path[256];
    if (!g_recovery_store.candidate_path(g_recovery_boot,path,sizeof path) ||
        !transactional_patch_load(path)) return false;
    // Recovery is a working draft, never attached to the old manual destination.
    g_project_session.reset_new();
    g_project_session.mark_mutated();
    g_recovered_session = true;
    g_recovery.restored(g_recovery_boot.records[g_recovery_boot.latest],
        g_project_session.revision(),svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000));
    { platform::AudioOut::Guard guard(*g_audio); g_audio->inhibit_recovered_output(); }
    g_save_flash=300;
    std::snprintf(g_patch_notice,sizeof g_patch_notice,"recovered draft / start or chain a for sound");
    return true;
}

static bool startup_choose(C3D_RenderTarget* top, C3D_RenderTarget* bottom) {
    if (!aptMainLoop()) return false;
    platform::debug_overlay::reset();
    start_menu_open(startup_data());
    startup_draw(top, bottom); // first frame fully dark; shared 3s image entrance
    if (g_recovery_boot.blocked) start_menu_error("recovery sd error / snapshots disabled");
    else if (g_patch_catalog_failed) start_menu_error("library unavailable / sd error");
    startup_draw(top, bottom); // first frame fully dark; shared 3s image entrance
    u64 previous_tick = svcGetSystemTick();
    while (aptMainLoop()) {
        poll_lifecycle_input(true);
        const u64 now_tick = svcGetSystemTick();
        const u64 elapsed_ms = (now_tick - previous_tick) / (SYSCLOCK_ARM11 / 1000);
        previous_tick = now_tick;
        start_menu_tick(u32(elapsed_ms > 1000 ? 1000 : elapsed_ms));
        hidScanInput();
        u32 down = hidKeysDown(), held = hidKeysHeld(), up = hidKeysUp();
        poll_lifecycle_input(true);
        lifecycle_input.filter(down, held, up);
        platform::debug_overlay::input(down, held, up);
        touchPosition touch{}; hidTouchRead(&touch);
        const StartMenuResult result = start_menu_input(down, held, touch.px, touch.py);
        if (result.action == StartMenuAction::Exit) return false;
        if (result.action == StartMenuAction::RecoverProject) {
            if (recovery_restore()) {
                return true;
            }
            start_menu_error("recovery failed / retained unchanged");
        }
        if (result.action == StartMenuAction::CancelRecovery)
            start_menu_error("recovery retained / snapshots disabled");
        if (result.action == StartMenuAction::PreferencesChanged) {
            const StartMenuData& settings = start_menu_data();
            StartMenuStore proposed = g_start_store;
            proposed.show_menu = settings.show_menu;
            proposed.menu_sound = settings.menu_sound;
            proposed.theme = settings.theme;
            if (!save_start_preferences(proposed)) {
                start_menu_set_data(startup_data());
                start_menu_error("settings not saved / retry");
            }
        }
        if (result.action == StartMenuAction::NewProject || result.action == StartMenuAction::LoadProject) {
            bool ok = false;
            if (result.action == StartMenuAction::NewProject)
                ok = execute_destructive({DestructiveAction::NewProject, -1});
            else {
                g_patch_slot = result.slot;
                ok = execute_destructive({DestructiveAction::LoadProject, result.slot});
            }
            g_overlay = Overlay::None; // the startup error stays on its own page
            if (ok) return true;
            start_menu_set_data(startup_data());
            start_menu_error(result.action == StartMenuAction::NewProject
                ? "cannot create / out of memory" : "load failed / project unchanged");
        }
        startup_draw(top, bottom);
        if (down & KEY_ZL) startup_screenshot();
    }
    return false;
}

// ===================== main =====================

int main() {
    gfxInitDefault();
    // The startup UI needs the same New 3DS CPU mode as the patch UI.
    // AudioOut::init runs only AFTER startup_choose; enabling it there alone
    // leaves the animated menu at 268 MHz instead of 804 MHz on New 3DS.
    bool startup_is_new = false;
    if (R_SUCCEEDED(APT_CheckNew3DS(&startup_is_new)) && startup_is_new)
        osSetSpeedupEnable(true);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2);
    C2D_Prepare();

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/seance", 0777);
    mkdir(SAMPLE_DIR, 0777);
    mkdir(RECORDING_DIR, 0777);
    (void)start_store_load_preferences(START_MENU_PATH, g_start_store);
    if (!project_names_load(PROJECT_NAMES_PATH, g_project_names))
        project_names_load("sdmc:/3ds/seance/project_names.bin.bak", g_project_names);
    palette::set(g_start_store.theme);
    hidScanInput();
    // R is a recovery entry even if startup was set to last project.
    const u32 boot_keys = hidKeysHeld();
    // Boot proceeds directly to the menu's continuously morphing wordmark.

    // engine and AudioOut live before project restore so boot can use the exact
    // same staged transaction as runtime LOAD. The AudioOut lock is valid before
    // NDSP init; there is simply no worker to contend with yet.
    g_eng = new(std::nothrow) Engine();
    if (!g_eng) {
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return 1;
    }
    // Optional immutable donor-font atlas; all startup/runtime text shares it.
    // Before audio starts; allocation failure preserves the original renderer.
    ui::text_init();
    platform::debug_overlay::init();
    // Shared startup/runtime paper texture; allocation precedes audio and menu.
    // Failure keeps the software paper fallback, with no input/audio changes.
    menu_style::ambient_init();
    editor_style::init(); // optional16KiB; static phase-zero grid before audio starts
    stylus_feedback::init(); // optional 16KiB, no allocations during touch/draw
    relic_material_init(); // retired allocation-free lifecycle hook
    // The three-snapshot mailbox must not occupy the 32KiB main stack while
    // nested project-name readback or REC/WAV export uses its own workspace.
    // Construct once here (before startup/APT); every exit still joins/unhooks
    // explicitly through shutdown(), with no heap allocation or audio change.
    static platform::AudioOut audio;
    g_audio = &audio;
    audio.attach_lifecycle(); // startup and NO DSP mode still receive APT authority edges

    hidScanInput();
    const u32 boot_held = boot_keys | hidKeysHeld();
    const bool force_menu = (boot_held & KEY_R) != 0;
    const bool force_demo = !force_menu && (boot_held & KEY_L) != 0;
    bool project_ready = false;
    mkdir("sdmc:/3ds/seance/recovery", 0777);
    g_recovery_boot = g_recovery_store.inspect(); // once, BEFORE last/demo bypass
    g_recovery.boot(g_recovery_boot, svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000));
    scan_patch_slots(); // one directory enumeration at boot, zero patch file reads
    if (force_demo && !g_recovery.unresolved()) {
        project_ready = execute_destructive({DestructiveAction::NewProject, -1});
        if (project_ready) g_overlay = Overlay::Editor;
    } else if (!force_menu && !g_start_store.show_menu && !g_recovery.unresolved()) {
        // An explicit preference, never an automatic load behind the menu.
        StartMenuData data = startup_data();
        const int slot = data.recent_count ? data.recent_slots[0] : -1;
        if (slot >= 0) {
            g_patch_slot = slot;
            project_ready = execute_destructive({DestructiveAction::LoadProject, slot});
        }
    }
    if (!project_ready) {
        g_overlay = Overlay::None;
        project_ready = startup_choose(top, bot);
    }
    // Own each activation key's held/release tail, including A+touch chords,
    // without waiting for global neutrality or blocking redraw on a stuck key.
    StartupInputTail startup_input_tail;
    if (project_ready) {
        hidScanInput();
        startup_input_tail.capture(0, hidKeysHeld());
    }
    if (!project_ready) {
        clear_sample_bank();
        audio.shutdown();
        delete g_eng; g_eng = nullptr; g_audio = nullptr;
        // Leaving the menu alone must never delete/replace legacy last_patch.bin.
        if (C3D_FrameBegin(0)) {
            C2D_Prepare();
            C3D_TexBind(0, nullptr);
            C3D_FrameEnd(0);
            menu_style::ambient_shutdown();
            editor_style::shutdown();
            stylus_feedback::shutdown();
            relic_material_shutdown();
            ui::text_shutdown();
        } // Failed fence: retain the textures until process teardown.
        platform::debug_overlay::shutdown();
        C2D_Fini(); C3D_Fini(); gfxExit();
        return 0;
    }
    // Loaded transport/sequencer state is data, not permission to make noise.
    g_eng->transport_stop();
    g_eng->set_sequencer_enabled(false);
    // transactional_patch_load already restores/adopts REC samples. Demo REC
    // units (if added later) are prepared by their creation workflow.

    sigils_visual_init();
    g_has_dsp = audio.init(*g_eng);
    g_session_started = true;

    platform::MicIn mic;
    g_mic_ok = mic.init();
    uint64_t mic_last_arrival = osGetTime();
    {
        platform::AudioOut::Guard guard(audio);
        MicUnit::reset_input();
    }
    g_mic_peak = 0;
    static q15 micbuf[1200];    // ~546 frames/frame @60fps, headroom for drops
    platform::debug_overlay::reset(); // do not bridge startup/activation/audio init

    while (aptMainLoop()) {
        poll_lifecycle_input();
        // Audio-clocked rand writes bases; acknowledge on UI thread before save,
        // discard and navigation handlers inspect project dirty state.
        module_rand_poll();
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();
        u32 up = hidKeysUp();
        const u32 recovery_input_epoch = poll_lifecycle_input();
        lifecycle_input.filter(down, held, up);
        platform::debug_overlay::input(down, held, up); // diagnostic opt-in only; normal ZR is undo
        startup_input_tail.filter(down, held, up);
        const u32 recovery_fresh_down = down;
        const bool recovery_quiet_input = (down | held | up) == 0;
        bool recovery_transport_was_playing = false;
        { platform::AudioOut::Guard guard(*g_audio);
          g_audio->begin_recovery_input(recovery_input_epoch);
          recovery_transport_was_playing = g_eng->transport_playing; }
        nav_ink_input(down);
        // Decorative entry never delays a fresh editor action. Closing owns its
        // local input/tails; global navigation and transport remain interruptible.
        const bool editor_motion_skip_touch = chains_editor_input(down, held, up);
        const View input_view = g_view;
        const Overlay input_overlay = g_overlay;
        const int input_chain = g_sel_chain, input_slot = g_sel_slot;
        const bool stage_modal_frame = g_view == View::Chains &&
            (g_overlay == Overlay::Picker || (g_overlay == Overlay::None && chains_context_is_open()));
        bool stage_skip_touch = editor_motion_skip_touch;
        bool editor_delete_confirmation = false;
        if (down & ~(KEY_TOUCH | KEY_ZL)) project_overlay_cancel_input();

        // GO TO owns input for the whole frame, so a navigation touch cannot
        // leak into the view it just opened. ZL remains a stable global capture.
        if (down & KEY_ZL) {
            g_screenshot_pending = true;
        }
        // Held MOD owns entry/release touch edges and action buttons. Evaluate
        // ownership after input: a quick L+arrow exit belongs to old navigation.
        // Seq-origin cuts own destructive keys before a fresh held-L can open
        // the module lens (notably the existing L+X slice-lock removal chord).
        mic_focus_input(down, stage_modal_frame || nav_is_open());
        const bool rot_word_skip_touch = rot_focus_menu_input(down, held, up,
            stage_modal_frame || nav_is_open() || g_overlay == Overlay::ConfirmDiscard);
        if (rot_focus_menu_modal()) {
            touchPosition t{};
            if (held & KEY_TOUCH) hidTouchRead(&t);
            rot_focus_menu_touch(down, held, t.px, t.py);
        }
        const bool rule_word_skip_touch = rule_focus_menu_input(down, held, up,
            stage_modal_frame || nav_is_open() || g_overlay == Overlay::ConfirmDiscard);
        if (rule_focus_menu_modal()) {
            touchPosition t{};
            if (held & KEY_TOUCH) hidTouchRead(&t);
            rule_focus_menu_touch(down, held, t.px, t.py);
        }
        const bool pent_word_skip_touch = pent_focus_menu_input(down, held, up,
            stage_modal_frame || nav_is_open() || g_overlay == Overlay::ConfirmDiscard);
        if (pent_focus_menu_modal()) {
            touchPosition t{};
            if (held & KEY_TOUCH) hidTouchRead(&t);
            pent_focus_menu_touch(down, held, t.px, t.py);
        }
        const bool gran_word_skip_touch = grain_focus_menu_input(down, held, up,
            stage_modal_frame || nav_is_open() || g_overlay == Overlay::ConfirmDiscard);
        if (grain_focus_menu_modal()) {
            touchPosition t{};
            if (held & KEY_TOUCH) hidTouchRead(&t);
            grain_focus_menu_touch(down, held, t.px, t.py);
        }
        grain_focus_input(down, held, stage_modal_frame || nav_is_open());
        relic_focus_input(down, held, stage_modal_frame || nav_is_open() || g_overlay == Overlay::ConfirmDiscard);
        paul_focus_input(down, held, stage_modal_frame || nav_is_open() || g_overlay == Overlay::ConfirmDiscard);
        scratch_focus_input(down, held, stage_modal_frame || nav_is_open() || g_overlay == Overlay::ConfirmDiscard);
        reverb_focus_input(down, held, stage_modal_frame || nav_is_open());
        strain_focus_input(down, held, stage_modal_frame || nav_is_open());
        vca_focus_input(down, held, stage_modal_frame || nav_is_open());
        ring_focus_input(down, held, stage_modal_frame || nav_is_open());
        fold_focus_input(down, held, stage_modal_frame || nav_is_open());
        tilt_focus_input(down, held, stage_modal_frame || nav_is_open());
        inst_focus_input(down, stage_modal_frame || nav_is_open());
        const bool rec_skip_touch = recorder_focus_input(down, held,
            nav_is_open() || g_overlay == Overlay::ConfirmDiscard);
        const bool play_word_skip_touch = play_editor_word_input(down, held, up,
            stage_modal_frame || nav_is_open() || g_overlay == Overlay::ConfirmDiscard);
        const u32 play_owned_keys = nav_is_open() || play_editor_word_modal() ? 0 : play_editor_context_keys(down, held);
        down &= ~play_owned_keys;
        const bool param_skip_touch = (play_word_skip_touch || rec_skip_touch || play_owned_keys != 0 || param_lens_input(down, held, up,
            rot_focus_menu_modal() || rule_focus_menu_modal() || pent_focus_menu_modal() || grain_focus_menu_modal() || play_editor_word_modal() || nav_is_open() || g_overlay == Overlay::ConfirmDiscard || recorder_focus_confirming())) || gran_word_skip_touch || pent_word_skip_touch || rule_word_skip_touch || rot_word_skip_touch;
        const bool param_owned_frame = rot_focus_menu_modal() || rule_focus_menu_modal() || pent_focus_menu_modal() || grain_focus_menu_modal() || play_editor_word_modal() || param_lens_is_open() || recorder_focus_confirming();
        const bool discard_modal_had_frame = g_overlay == Overlay::ConfirmDiscard;
        const bool nav_had_frame = nav_is_open();   // consumed input this frame,
                                                    // even if a pick just closed it
        if (nav_had_frame) {
            if (g_view == View::Sequencer) sequencer_view_cancel_input();
            if (g_sig_canvas_mutated) { mark_project_mutated(); g_sig_canvas_mutated = false; }
            NavChoice pick = NavChoice::None;
            nav_buttons(down, pick);
            if (pick == NavChoice::None && nav_is_open()) {
                if (held & KEY_TOUCH) {
                    touchPosition t; hidTouchRead(&t);
                    if (down & KEY_TOUCH) nav_touch_down(t.px, t.py, pick);
                    else nav_touch_hold(t.px, t.py);
                } else {
                    nav_touch_release(); // commit a clock draft only on its lift
                }
            }
            if (pick == NavChoice::Exit) request_destructive(DestructiveAction::ExitApp);
            if (pick != NavChoice::None && pick != NavChoice::Exit) {
                const View from_view = g_view;
                const Overlay from_overlay = g_overlay;
                View to_view = g_view;
                Overlay to_overlay = Overlay::None;
                switch (pick) {
                    case NavChoice::Chains:      to_view = View::Chains; break;
                    case NavChoice::Sequencer:   to_view = View::Sequencer; break;
                    case NavChoice::Performance: to_view = View::Performance; break;
                    case NavChoice::Sigils:      to_view = View::Sigils; break;
                    case NavChoice::Patchbay:    to_view = View::Patchbay; break;
                    case NavChoice::Bounce:      to_view = View::Bounce; break;
                    case NavChoice::Patch:
                        patch_library_reset();
                        to_overlay = Overlay::PatchBrowser;
                        break;
                    default: break;
                }
                // Re-selecting the current page merely closes GO TO. Entry hooks
                // are real lifecycle work, not decoration, so never replay them.
                if (from_view != to_view) {
                    if (from_view == View::Performance) performance_view_leave();
                    if (to_view == View::Sequencer) sequencer_view_enter();
                    if (to_view == View::Performance) performance_view_reset();
                    if (to_view == View::Bounce) bounce_enter();
                    if (to_view == View::Patchbay) patchbay_enter();
                }
                commit_nav_destination(from_view, from_overlay, to_view, to_overlay);
            }
        }
        // Every view now obeys the same application controls. Performance is a
        // surface inside seance, not a hostile input layer that steals START/Y.
        else {
        // A discard modal owns the entire frame, including the frame where a
        // choice closes it. Its A/X edge must not leak into the revealed view.
        if (discard_modal_had_frame) {
            // Physical buttons remain explicit modal actions. Any button edge
            // cancels a held stylus command and owns this entire frame.
            if (down & ~(KEY_TOUCH | KEY_ZL)) {
                confirm_discard_buttons(down);
            } else handle_touch(down, held);
        } else {
        // SELECT has one meaning everywhere: open/close GO TO. Shoulder chords
        // deliberately do not change transport or create a second control layer.
        if ((down & KEY_SELECT) && g_overlay != Overlay::ConfirmDiscard) {
            if (g_overlay == Overlay::PatchBrowser) patch_library_interrupt();
            sequencer_view_cancel_input();
            picker_cancel_input();
            patchbay_cancel_input();
            chains_close_context_branch();
            if (g_overlay == Overlay::SampleBrowser) {
                sample_browser_touch_release(false);
                sample_browser_stop_preview();
            }
            stage_skip_touch = true;
            nav_open();
            down &= KEY_ZL; // SELECT owns the opening frame
        }

        // The unified browser owns selection/listen/use/back and every local
        // button chord. Never leak its Y/A or L+arrows to the underlying patch.
        // START remains transport, but first cancels pending browser work/touch.
        if (g_view == View::Chains && g_overlay == Overlay::SampleBrowser && !nav_is_open()) {
            const u32 local = down & ~(KEY_TOUCH | KEY_ZL);
            if (local) {
                sample_browser_touch_release(false);
                stage_skip_touch = true;
                if (down & KEY_START) {
                    sample_browser_stop_preview();
                    down &= KEY_START | KEY_ZL;
                } else {
                    sample_browser_buttons(local);
                    down &= KEY_TOUCH | KEY_ZL;
                }
            }
        }

        // Sigil disclosures own B/A/Y and the entire local frame, not the
        // underlying transport/chain actions. START closes the disclosure before
        // the global sequencer transport edge is applied below.
        if (g_view == View::Sigils && g_overlay == Overlay::None && !nav_is_open()) {
            if (down & KEY_START) { sigils_close_mode_menu(); stage_skip_touch = true; down &= KEY_START | KEY_ZL; }
            else if (sigils_view_buttons(down, held)) { stage_skip_touch = true; down &= KEY_ZL; }
        }
        // Patchbay owns local buttons; START cancels an unfinished cable gesture
        // before the global sequencer transport edge is applied below.
        if (g_view == View::Patchbay && g_overlay == Overlay::None && !nav_is_open()) {
            if (down & KEY_START) {
                patchbay_cancel_input(); stage_skip_touch = true;
                down &= KEY_START | KEY_ZL;
            }
            else {
                patchbay_buttons(down, held);
                if (down & ~(KEY_TOUCH | KEY_ZL)) stage_skip_touch = true;
                down &= KEY_TOUCH | KEY_ZL;
            }
        }
        // Bounce owns local actions, including the edge opening a confirmation.
        // SELECT navigation above remains global; no A/Y/L chord leaks
        // into the old chain/Sigil actions underneath this global page.
        if (g_view == View::Bounce && g_overlay == Overlay::None && !nav_is_open()) {
            bounce_buttons(down, held);
            if (down & ~(KEY_TOUCH | KEY_ZL)) stage_skip_touch = true;
            down &= KEY_TOUCH | KEY_ZL;
        }

        // START/Y on a local modal cannot also confirm or edit its underlying
        // surface with the same A/X/touch edge.
        if (stage_modal_frame && (down & (KEY_START | KEY_Y))) {
            if (input_overlay == Overlay::Picker) picker_cancel_input();
            stage_skip_touch = true;
            down &= (down & KEY_START) ? (KEY_START | KEY_ZL) : (KEY_Y | KEY_ZL);
        }
        // Local disclosures own the whole button frame, including a placement,
        // close or editor entry. A must never add/edit and trigger the chain too.
        if (stage_modal_frame && !nav_is_open() && !(down & (KEY_SELECT | KEY_START | KEY_Y))) {
            const u32 local = down & ~(KEY_TOUCH | KEY_ZL);
            if (input_overlay == Overlay::Picker) {
                if (local) picker_cancel_input();
                picker_buttons(local);
            }
            else chains_context_buttons(local);
            if (local) { stage_skip_touch = true; down &= KEY_TOUCH | KEY_ZL; }
        }

        // PATCHES drops its own unfinished command before the unchanged global
        // transport route. The START chord cannot also save/confirm or name it.
        if ((down & KEY_START) && input_overlay == Overlay::PatchBrowser &&
            g_overlay == Overlay::PatchBrowser && !nav_is_open()) patch_library_interrupt();
        // Old-3DS global navigation works from every view and overlay.
        // L belongs to sequencer editing / temporary performance faders on their bare pages.
        if (!stage_modal_frame && !nav_is_open() && !param_owned_frame && g_overlay != Overlay::ConfirmDiscard && g_overlay != Overlay::PatchBrowser && (held & KEY_L) && !(held & KEY_R) &&
            !((g_view == View::Sequencer || g_view == View::Performance) && g_overlay == Overlay::None)) {
            if (down & KEY_UP)    g_sel_chain = (g_sel_chain + NUM_CHAINS - 1) % NUM_CHAINS;
            if (down & KEY_DOWN)  g_sel_chain = (g_sel_chain + 1) % NUM_CHAINS;
            if (down & KEY_LEFT)  g_sel_slot = (g_sel_slot + UNITS_PER_CHAIN - 1) % UNITS_PER_CHAIN;
            if (down & KEY_RIGHT) g_sel_slot = (g_sel_slot + 1) % UNITS_PER_CHAIN;
            if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
                g_edit_chain = g_sel_chain;
                g_edit_slot = g_sel_slot;
                if (g_overlay == Overlay::Editor && !edited_unit()) { picker_open(); g_overlay = Overlay::Picker; }
                else if (g_overlay == Overlay::Picker && edited_unit()) g_overlay = Overlay::Editor;
            }
        }
        // START is the sequencer transport everywhere. Folio, Bounce and a
        // seq-origin PLAY editor own the bare edge locally; shoulder chords are
        // normalized here before Folio's R-mode ring can see them. Bare START
        // enables and toggles the score, R+START stops and resets to the beginning,
        // and L+START stays unassigned. Patches live only in SELECT > GO TO.
        const bool bare_start_owned_locally = !(held & (KEY_L | KEY_R)) &&
            ((g_view == View::Sequencer && g_overlay == Overlay::None) ||
             (g_view == View::Bounce && g_overlay == Overlay::None));
        if ((down & KEY_START) && g_overlay != Overlay::ConfirmDiscard &&
            !bare_start_owned_locally) {
            sequencer_view_cancel_input();
            if (!(held & KEY_L)) {
                platform::AudioOut::Guard g(audio);
                if (held & KEY_R) {
                    g_eng->transport_stop();
                    g_eng->transport_reset();
                } else if (g_eng->transport_playing) g_eng->transport_stop();
                else {
                    g_eng->set_sequencer_enabled(true);
                    g_eng->transport_play();
                }
            }
            if (g_view == View::Performance && g_overlay == Overlay::None)
                performance_view_buttons(KEY_START, held, up); // cancel private gesture ownership
            stage_skip_touch = true;
            down &= KEY_ZL; // transport chord owns its whole frame
        }

        if ((down & KEY_Y) && !(held & (KEY_L | KEY_R)) &&
            !(g_view == View::Sequencer && g_overlay == Overlay::None) &&
            g_overlay != Overlay::ConfirmDiscard && g_overlay != Overlay::PatchBrowser) {
            if (g_sig_canvas_mutated) { mark_project_mutated(); g_sig_canvas_mutated = false; }
            if (g_overlay == Overlay::PatchBrowser && g_project_session.has_pending())
                g_project_session.cancel_pending();
            chains_view_touch_release(false);
            sequencer_view_cancel_input();
            if (g_view == View::Performance) performance_view_leave();
            // Primary muscle-memory loop: build sound <-> draw control.
            // SEQ/PERFORM remain reachable directly through SELECT > GO TO.
            g_view = (g_view == View::Chains) ? View::Sigils : View::Chains;
            g_overlay = Overlay::None;
            g_drag = Drag::None;
            stage_skip_touch = true;
            down &= KEY_ZL; // bare Y owns the switch frame
        }
        if ((down & KEY_B) && g_overlay == Overlay::PatchBrowser) {
            patch_library_back();
            stage_skip_touch = true;
            down &= KEY_TOUCH | KEY_ZL; // B owns the entire page/close edge
        }
        if ((down & KEY_B) && !(g_view == View::Sequencer && g_overlay == Overlay::None)) {
            const bool overlay_owned_b = g_overlay != Overlay::None;
            sequencer_view_cancel_input();
            chains_close_context_branch();
            sigils_close_mode_menu();
            if (g_overlay == Overlay::PatchBrowser && g_project_session.has_pending())
                g_project_session.cancel_pending();
            bool consumed = false;
            if (g_overlay == Overlay::Editor) consumed = editor_back();
            if (!consumed && g_overlay == Overlay::Editor && chains_editor_back()) {
                editor_cancel_input();
                consumed = true;
                stage_skip_touch = true;
                down &= KEY_ZL; // back owns simultaneous A/X/touch too
            }
            if (!consumed) g_overlay = Overlay::None;
            g_patch_confirm_slot = g_patch_delete_confirm_slot = -1;
            g_drag = Drag::None;
            if (!overlay_owned_b && g_view == View::Performance) {
                performance_view_leave();
                g_view = View::Chains;
                down &= ~KEY_B;
            }
            // The B edge that closed an overlay must not also collapse the
            // underlying Folio Event/Chain surface in this same frame.
            if (overlay_owned_b) down &= ~KEY_B;
        }

        patch_browser_route_buttons(down, stage_skip_touch, input_overlay);

        if (!stage_modal_frame && !nav_is_open() && play_editor_button(down)) {
            down &= ~KEY_A; stage_skip_touch = true;
        }
        if (!stage_modal_frame && !nav_is_open() && (down & KEY_A) && g_overlay != Overlay::SampleBrowser && g_overlay != Overlay::Picker && g_overlay != Overlay::PatchBrowser && g_overlay != Overlay::ConfirmDiscard &&
            g_view != View::Sequencer && g_view != View::Performance &&
            !(g_view == View::Chains && g_overlay == Overlay::None && (down & KEY_R) && !(held & KEY_L))) {
            platform::AudioOut::Guard g(audio);
            Sigil& selected = g_eng->sigils[g_sel_sigil];
            if (g_view == View::Sigils && g_overlay == Overlay::None &&
                selected.mode == SigilMode::Follow)
                { audio.authorize_recovered_output();
                  g_eng->request_chain_trigger(g_eng->step_dest[g_sel_sigil], g_eng->launch_quantize); }
            else if (g_view == View::Sigils && g_overlay == Overlay::None)
                g_eng->reset_sigil(g_sel_sigil);
            else {
                audio.authorize_recovered_output(); // actual fresh A chain-audition route
                g_eng->request_chain_trigger(g_sel_chain, g_eng->launch_quantize);
                pent_focus_chain_trigger_requested(g_sel_chain);
            }
        }

        if (g_view == View::Sequencer && g_overlay == Overlay::None && !nav_is_open())
            sequencer_view_buttons(down, held, up);
        if (g_view == View::Performance && g_overlay == Overlay::None)
            performance_view_buttons(down, held, up);

        // Global manual tempo belongs to SELECT > GO TO, not the Sigils canvas.
        // Sigils retain their own free/sync/division and curve controls.

        if (!stage_modal_frame && !nav_is_open() && g_view == View::Chains && g_overlay == Overlay::None &&
            (down & KEY_R) && !(held & KEY_L)) {
            chains_view_touch_release(false);
            chains_workspace_buttons(KEY_R);
            stage_skip_touch = true;
            down &= ~(KEY_R | KEY_A | KEY_X);
        }
        if (!stage_modal_frame && !nav_is_open() && g_view == View::Chains && g_overlay == Overlay::None && !(held & (KEY_L | KEY_R))) {
            chains_workspace_buttons(down);
            if (down & (KEY_A | KEY_B | KEY_X | KEY_ZR | KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN))
                stage_skip_touch = true; // button edge owns this frame, including simultaneous touch
            // Column controller owns installed-slot/+add navigation, including
            // sparse old patches. A still auditions; X opens the selected editor.
            if (((down & KEY_L) && (held & KEY_R)) || ((down & KEY_R) && (held & KEY_L))) {
                platform::AudioOut::Guard g(audio);
                g_eng->chains[g_sel_chain].pan = 0;
                mark_project_mutated();
            } else if (down & KEY_L) {
                platform::AudioOut::Guard g(audio);
                g_eng->chains[g_sel_chain].pan = clamp_q15_i32((int32_t)g_eng->chains[g_sel_chain].pan - 4096);
                mark_project_mutated();
            } else if (down & KEY_R) {
                platform::AudioOut::Guard g(audio);
                g_eng->chains[g_sel_chain].pan = clamp_q15_i32((int32_t)g_eng->chains[g_sel_chain].pan + 4096);
                mark_project_mutated();
            }
            // X / inline edit opens the editor; every deletion uses the same
            // confirmed more branch, never a second unguarded X edge.
        } else if (g_view == View::Chains && g_overlay == Overlay::Editor) {
            if (down & KEY_X) {
                editor_delete_confirmation = chains_confirm_editor_delete();
                stage_skip_touch = true;
            } else if ((down & KEY_R) && sample_slot_for_unit(edited_unit()) >= 0)
                export_current_sample();
        }
        }
        }

        if (input_view != g_view || input_overlay != g_overlay || input_chain != g_sel_chain ||
            input_slot != g_sel_slot || nav_is_open()) {
            relic_focus_release();
            project_overlay_cancel_input();
            if (input_overlay == Overlay::SampleBrowser) {
                sample_browser_touch_release(false);
                sample_browser_stop_preview();
            }
            picker_cancel_input();
            bounce_cancel_input();
            patchbay_cancel_input();
            sigils_view_touch_release();
            chains_view_touch_release(false);
            if (!editor_delete_confirmation) chains_close_context_branch();
            if (input_view == View::Chains || input_view == View::Sigils || input_view == View::Patchbay) stage_skip_touch = true;
        }

        // While the GO TO menu is open it owns the touchscreen exclusively
        // (including the frame where a pick just closed it - the same touch
        // edge must not leak into the freshly opened view).
        if (!stage_skip_touch && !param_skip_touch && !nav_had_frame && !nav_is_open() && !discard_modal_had_frame)
            handle_touch(down, held);

        if (g_exit_requested) break;

        // Destruction of a deleted module (potentially carrying a large delay or
        // STRN buffer) is deferred until no AudioOut guard is held.
        g_eng->reclaim_structural_undo();
        if (g_eng->structural_undo_available() &&
            !(g_view == View::Chains && (g_overlay == Overlay::None || g_overlay == Overlay::Editor)))
            g_eng->invalidate_structural_undo();

        // drain mic into the MicUnit ring (guarded: ring indices are shared)
        if (g_mic_ok) {
            int n = mic.poll(micbuf, 1200);
            if (n > 0) {
                mic_last_arrival=osGetTime();
                q15 peak = 0;
                for (int i = 0; i < n; ++i) {
                    const int32_t value = micbuf[i] < 0 ? -(int32_t)micbuf[i] : micbuf[i];
                    if (value > peak) peak = (q15)(value > 32767 ? 32767 : value);
                }
                g_mic_peak = peak > g_mic_peak ? peak : (q15)(g_mic_peak - (g_mic_peak >> 4));
                platform::AudioOut::Guard g(audio);
                MicUnit::push(micbuf, n);
            } else if (g_mic_peak) {
                g_mic_peak = (q15)(g_mic_peak - ((g_mic_peak >> 4) ? (g_mic_peak >> 4) : 1));
            }
            if (osGetTime()-mic_last_arrival>1000) {
                // Conservative device-loss policy: preserve the prefix as a stopped
                // unsaved take, including fresh starts attempted before input returns.
                platform::AudioOut::Guard guard(audio);
                if (g_eng->recording_active_or_pending()) g_eng->interrupt_recorders();
            }
        }

        // Transient UI timers advance once per app frame. Renderers may run more
        // than once during page-tape composition and must stay side-effect-free.
        if (g_save_flash > 0) --g_save_flash;
        if (g_export_flash > 0) --g_export_flash;
        if (g_sample_flash > 0) --g_sample_flash;
        if (g_seq_lock_flash > 0) --g_seq_lock_flash;
        editor_ui_tick();
        // Includes inactive/navigation cancellation; pending decode/use stays
        // outside GPU work and may expose its loading notice for one frame.
        sample_browser_tick();
        patch_library_tick(); // selected SD inspection/controller invalidation, outside audio guard
        performance_view_tick();
        recovery_idle_tick(recovery_quiet_input, recovery_fresh_down, recovery_transport_was_playing);

        // Tell the audio publisher which single unit editor is visible. Capture
        // validates PLAY/REC kind before publishing focused-only telemetry.
        const bool unit_focus_visible =
            g_view == View::Chains && g_overlay == Overlay::Editor;
        const int focus_chain = unit_focus_visible ? g_edit_chain : -1;
        const int focus_slot = unit_focus_visible ? g_edit_slot : -1;
        const int focus_sigil = g_view == View::Sigils && g_overlay == Overlay::None ? g_sel_sigil : -1;
        if (g_has_dsp) {
            audio.set_ui_focus(focus_chain, focus_slot, focus_sigil);
            // No new audio block: keep the last whole frame, with no payload copy.
            (void)audio.read_ui_snapshot(g_ui_snapshot);
        } else {
            g_eng->capture_ui_snapshot(g_ui_snapshot, focus_chain, focus_slot, focus_sigil);
        }

        // === draw ===
        palette::capture(osGetTime()); // same frozen color step for both LCDs/DTOs
        editor_style::capture(); // palette-only LUT update outside controller/audio guards
        ++g_ui_frame;
        // Bounded controller DTO copied before GPU drawing, never under a draw call.
        if (g_view == View::Sequencer) sequencer_capture_inspector();
        editor_capture_button_motion(unit_focus_visible && !nav_is_open() && !param_lens_is_open());
        param_lens_capture();
        fm_context_capture(fm_focus_selected());
        fm_focus_capture();
        stranular_focus_capture();
        play_focus_capture();
        sigils_capture();
        performance_view_capture();
        picker_capture(); // advance/cancel after input; both LCDs share one progress
        chains_editor_capture(); // same phase/ownership for both editor LCDs
        chains_capture();
        patchbay_capture();
        sample_browser_capture();
        nav_capture();
        project_overlay_capture();
        bounce_capture();
        capture_nav_ink_transition();
        capture_stylus_feedback(held);
        platform::debug_overlay::before_frame();
        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;
        platform::debug_overlay::after_begin();
        stylus_feedback::upload();
        // The single ambient texture may only be written after its last GPU
        // reader is fenced. Both displays/crops then share this frozen time.
        menu_style::ambient_frame(osGetTime());
        editor_style::frame(); // separate static material, same synchronized upload fence
        relic_material_upload(); // retired no-op; no Relic texture exists
        nav_gpu_frame(); // previous LCD pair is fenced; one freeze on popup entry
        sigils_gpu_frame(); // includes the shared GO TO blur pair
        rot_focus_gpu_frame(true); // owns ROT upload/retirement after successful SYNCDRAW
        pent_focus_gpu_frame(); // exact art uploads only after the same GPU fence

        if (g_nav_ink.frame.active) {
            draw_nav_ink_transition(top, bot);
        } else {
        C2D_TargetClear(top, col_bg());
        C2D_SceneBegin(top);
        if (nav_clock_top_active()) nav_clock_draw_top();
        else if (!project_overlay_draw_top()) {
            if (g_view == View::Bounce) bounce_draw_top();
            else {
                if (!chains_editor_covers()) top_draw();
                chains_editor_draw(true);
            }
        }
        bounce_draw_indicator();

        C2D_TargetClear(bot, col_bg());
        C2D_SceneBegin(bot);
        if (nav_is_open()) nav_draw(); // frozen backdrop; no hidden live page rendering
        else draw_bottom_surface(g_view, g_overlay);
        if (nav_is_open() && g_overlay == Overlay::ConfirmDiscard) confirm_discard_draw();
        }
        draw_stylus_feedback(); // final lower-screen overlay, including navigation
        platform::debug_overlay::draw(top); // final sharp top pass, also during transitions

        nav_remember_outputs(); // addresses receiving this frame's LCD transfer
        C3D_FrameEnd(0);
        platform::debug_overlay::after_end();
        if (g_screenshot_pending) {
            g_screenshot_pending = false;
            mkdir(SCREENSHOT_DIR, 0777);
            // first free shot_NNN pair wins; cheap stat probe, ZL is a rare event
            char top_p[96], bot_p[96];
            for (int n = 0; n < 1000; ++n) {
                snprintf(top_p, sizeof top_p, "%s/shot_%03d_top.bmp", SCREENSHOT_DIR, n);
                struct stat st;
                if (stat(top_p, &st) != 0) {
                    snprintf(bot_p, sizeof bot_p, "%s/shot_%03d_bot.bmp", SCREENSHOT_DIR, n);
                    platform::save_screenshots(top_p, bot_p);
                    break;
                }
            }
        }
    }

    relic_focus_release();
    sample_browser_touch_release(false);
    sample_browser_stop_preview();
    performance_release_all();
    mic.shutdown();
    g_session_started = false;
    audio.shutdown();
    g_audio = nullptr;
    delete g_clip_unit;
    g_clip_unit = nullptr;
    persist_last_project();
    delete g_eng;

    if (C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) {
        rot_focus_shutdown(true); // final last-reader fence; never in a destructor
        C2D_Prepare();
        C3D_TexBind(0, nullptr);
        C3D_FrameEnd(0);
        menu_style::ambient_shutdown();
        editor_style::shutdown();
        stylus_feedback::shutdown();
        relic_material_shutdown();
        pent_focus_shutdown();
        sigils_visual_shutdown();
        ui::text_shutdown();
    } // If the final fence fails, keep the textures until process teardown.
    platform::debug_overlay::shutdown();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
