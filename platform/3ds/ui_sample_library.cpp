#include <dirent.h>
#include "ui_sample_library.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "menu_style.h"
#include "ui_module_focus_internal.h"
#include "audio_out.h"
#include "sample_browser_store.h"
#include "sample_ownership.h"
#include "ui_nav.h"
#include <3ds.h>
#include "../../core/synth/wav_loader.h"
#include "../../core/synth/wav_writer.h"
#include "../../core/ui/draw.h"
#include <cstdio>
#include <cstring>
#include <utility>
#include <type_traits>

namespace seance::platform::ui3ds {

static uint32_t sample_logical_frames(const trackr::synth::Sample& smp) {
    return smp.playable_frames();
}

int sample_slot_for_unit(const Unit* unit) {
    return sample_ownership::unit_slot(unit);
}

void set_sample_slot_for_unit(Unit* unit, int slot) {
    if (!unit || unit->kind == UnitKind::Recorder || (g_eng && g_eng->recording_active_or_pending())) return;
    const int previous = sample_slot_for_unit(unit);
    if (slot < 0) slot = trackr::synth::SAMPLE_BANK_SIZE - 1;
    if (slot >= trackr::synth::SAMPLE_BANK_SIZE) slot = 0;
    if (unit->kind == UnitKind::SamplePlayer) {
        auto* play = static_cast<PlayUnit*>(unit);
        if (slot != previous || !play->edit_source_matches()) {
            // Explicit browser/source choice starts fresh per-card edits; stale
            // capture/drag never gets this authority. Detach uses its own rebind.
            play->restore_edit_state({});
        }
        play->sample_slot = slot;
    } else if (unit->kind == UnitKind::Grain)
        static_cast<GrainUnit*>(unit)->sample_slot = slot;
    if (slot != previous) mark_project_mutated();
}

uint32_t sample_slot_frames(int slot) {
    return trackr::synth::SampleBank::instance().slot(slot).playable_frames();
}

struct SampleFileInfo {
    uint32_t source_frames = 0;
    uint32_t source_rate = 0;
    bool scanned = false;
    bool valid = false;
};

namespace {
constexpr int SAMPLE_WAVE_COLUMNS = 352;
struct SampleWaveView {
    uint32_t frames = 0;
    uint8_t channels = 0;
    int8_t low[SAMPLE_WAVE_COLUMNS] = {}, high[SAMPLE_WAVE_COLUMNS] = {};
};
static SampleWaveView g_sample_file_wave;
static char g_sample_file_wave_path[192] = {};

// Real PCM overview: eight bounded probes per time bin, union of L/R extrema.
// Fixed +/-full-scale; no hash ornament, gain animation, or full-buffer scan.
static void sample_wave_capture(const trackr::synth::Sample& source, SampleWaveView& wave) {
    wave = {};
    const uint32_t frames = source.playable_frames();
    if (!frames || (source.channels != 1 && source.channels != 2)) return;
    wave.frames = frames;
    wave.channels = source.channels;
    auto project = [](int value) {
        return int8_t(value < 0 ? -((-value * 50 + 16384) / 32768) :
                                  (value * 50 + 16384) / 32768);
    };
    for (int x = 0; x < SAMPLE_WAVE_COLUMNS; ++x) {
        uint32_t first = uint64_t(x) * frames / SAMPLE_WAVE_COLUMNS;
        uint32_t end = uint64_t(x + 1) * frames / SAMPLE_WAVE_COLUMNS;
        if (end <= first) end = first + 1;
        int low = 32767, high = -32768;
        for (int probe = 0; probe < 8; ++probe) {
            const uint32_t at = first + uint64_t(probe) * (end - first - 1) / 7;
            for (int ch = 0; ch < source.channels; ++ch) {
                const int value = source.data[size_t(at) * source.channels + ch];
                if (value < low) low = value;
                if (value > high) high = value;
            }
        }
        wave.low[x] = project(low);
        wave.high[x] = project(high);
    }
}
} // namespace

// The legacy mode integer remains ABI-local: library=0, project=1,
// favorites=2, recordings=3. All sources now share one six-row controller.
namespace {
constexpr int kRows = 6, kRowHeight = 24, kListTop = 56, kListBottom = 200;
constexpr int kLibrary = 0, kProject = 1, kFavorites = 2, kRecordings = 3;
struct BrowserEntry {
    char path[192] = {};
    int slot = -1;
    uint32_t generation = 0;
};
struct BrowserLocation {
    int mode = -1, slot = -1, scroll = 0;
    char directory[128] = {}, path[192] = {};
};
BrowserEntry browser_entries[MAX_SAMPLE_FILES];
BrowserLocation browser_locations[12];
int browser_location_next = 0;
char browser_directories[2][128] = {};
sample_browser_store::State browser_store, browser_store_draft;
char browser_store_path[256] = {};
char browser_notice[48] = {};
bool browser_store_loaded = false, browser_limited = false, browser_truncated = false;
int browser_menu = 0, browser_menu_selected = 0; // 1 sources, 2 more, 3 confirm removal
int browser_pending = 0;
bool browser_pending_play = false;
int browser_wave_slot = -1;
uint32_t browser_wave_generation = 0;
uintptr_t browser_owner = 0;
int browser_owner_chain = -1, browser_owner_slot = -1;
// Every control commits on lift in its original target, including stars and use.
int browser_touch_action = 0;
bool browser_touch_inside = false;
}
static SampleFileInfo g_sample_file_info[MAX_SAMPLE_FILES];
static bool g_sample_file_is_dir[MAX_SAMPLE_FILES] = {};
static char g_sample_relative_dir[128] = {};
static int g_sample_scroll = 0, g_sample_scroll_q8 = 0, g_sample_scroll_velocity_q8 = 0;
static int g_sample_selected = -1, g_sample_slot_selected = -1, g_sample_slot_previewing = -1;
static int g_sample_touch_entry = -1, g_sample_touch_x = 0, g_sample_touch_y = 0;
static int g_sample_touch_scroll_q8 = 0;
static bool g_sample_touch_list = false, g_sample_scrollbar_drag = false, g_sample_touch_scrolling = false;

static void browser_message(const char* text) {
    std::snprintf(browser_notice, sizeof browser_notice, "%s", text ? text : "");
}
static bool browser_owner_current() {
    return g_eng && g_audio && browser_owner && g_edit_chain == browser_owner_chain &&
        g_edit_slot == browser_owner_slot && uintptr_t(edited_unit()) == browser_owner;
}
static void browser_clear_wave() {
    g_sample_file_wave = {};
    g_sample_file_wave_path[0] = 0;
    browser_wave_slot = -1;
    browser_wave_generation = 0;
    browser_truncated = false;
}
static int sample_browser_max_scroll_px() {
    return g_sample_file_count > kRows ? (g_sample_file_count - kRows) * kRowHeight : 0;
}
static void sample_browser_set_scroll_q8(int value) {
    const int maximum = sample_browser_max_scroll_px() * 256;
    if (value < 0) value = 0;
    if (value > maximum) value = maximum;
    g_sample_scroll_q8 = value;
    g_sample_scroll = (value / 256) / kRowHeight;
}
static void sample_browser_reset_motion() {
    g_sample_scroll_velocity_q8 = 0;
    g_sample_touch_entry = -1;
    g_sample_touch_list = g_sample_scrollbar_drag = g_sample_touch_scrolling = false;
    browser_touch_action = 0;
    browser_touch_inside = false;
}
static void sample_browser_scrollbar_to(int ty) {
    if (ty < 59) ty = 59;
    if (ty > 196) ty = 196;
    sample_browser_set_scroll_q8(int(int64_t(ty - 59) * sample_browser_max_scroll_px() * 256 / 137));
}
static const char* browser_root() {
    return g_sample_browser_mode == kRecordings ? RECORDING_DIR : SAMPLE_DIR;
}
static bool sample_browser_path(const char* name, char* out, size_t size) {
    const int wrote = g_sample_relative_dir[0]
        ? std::snprintf(out, size, "%s/%s/%s", browser_root(), g_sample_relative_dir, name ? name : "")
        : std::snprintf(out, size, "%s/%s", browser_root(), name ? name : "");
    return wrote >= 0 && wrote < (int)size;
}
static bool sample_browser_directory_path(char* out, size_t size) {
    const int wrote = g_sample_relative_dir[0]
        ? std::snprintf(out, size, "%s/%s", browser_root(), g_sample_relative_dir)
        : std::snprintf(out, size, "%s", browser_root());
    return wrote >= 0 && wrote < (int)size;
}
static void browser_remember() {
    BrowserLocation* found = nullptr;
    for (auto& loc : browser_locations)
        if (loc.mode == g_sample_browser_mode && !std::strcmp(loc.directory, g_sample_relative_dir)) found = &loc;
    if (!found) found = &browser_locations[browser_location_next++ % 12];
    *found = {};
    found->mode = g_sample_browser_mode;
    found->scroll = g_sample_scroll_q8;
    std::snprintf(found->directory, sizeof found->directory, "%s", g_sample_relative_dir);
    if (g_sample_selected >= 0 && g_sample_selected < g_sample_file_count) {
        const auto& entry = browser_entries[g_sample_selected];
        found->slot = entry.slot;
        std::snprintf(found->path, sizeof found->path, "%s", entry.path);
    }
    if (g_sample_browser_mode == kLibrary || g_sample_browser_mode == kRecordings)
        std::snprintf(browser_directories[g_sample_browser_mode == kRecordings], 128, "%s", g_sample_relative_dir);
}

static uint16_t read_le16(const uint8_t* bytes) {
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static uint32_t read_le32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static SampleFileInfo read_wav_duration(const char* path) {
    SampleFileInfo out;
    out.scanned = true;
    FILE* file = fopen(path, "rb");
    if (!file) return out;
    uint8_t riff[12];
    if (fread(riff, 1, sizeof riff, file) != sizeof riff ||
        std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(file); return out;
    }
    uint16_t channels = 0, block_align = 0;
    uint32_t rate = 0, data_bytes = 0;
    for (int chunk = 0; chunk < 64 && (!rate || !data_bytes); ++chunk) {
        uint8_t header[8];
        if (fread(header, 1, sizeof header, file) != sizeof header) break;
        const uint32_t size = read_le32(header + 4);
        if (std::memcmp(header, "fmt ", 4) == 0 && size >= 16) {
            uint8_t fmt[16];
            if (fread(fmt, 1, sizeof fmt, file) != sizeof fmt) break;
            channels = read_le16(fmt + 2);
            rate = read_le32(fmt + 4);
            block_align = read_le16(fmt + 12);
            const long remain = (long)size - 16;
            if (remain > 0 && fseek(file, remain, SEEK_CUR) != 0) break;
        } else if (std::memcmp(header, "data", 4) == 0) {
            data_bytes = size;
            if (fseek(file, (long)size, SEEK_CUR) != 0) break;
        } else if (fseek(file, (long)size, SEEK_CUR) != 0) break;
        if ((size & 1u) && fseek(file, 1, SEEK_CUR) != 0) break;
    }
    fclose(file);
    if (!rate || !channels || !block_align || !data_bytes) return out;
    out.source_rate = rate;
    out.source_frames = data_bytes / block_align;
    out.valid = out.source_frames > 0;
    return out;
}

static void scan_sample_file_page_metadata() {
    const int first = g_sample_scroll;
    int last = first + kRows + 1;
    if (last > g_sample_file_count) last = g_sample_file_count;
    for (int i = first; i < last; ++i)
        if (!g_sample_file_is_dir[i] && !g_sample_file_info[i].scanned)
            g_sample_file_info[i] = read_wav_duration(browser_entries[i].path);
}

static bool unique_recording_path(int patch, int chain, int unit, char* out, size_t size) {
    for (unsigned sequence = 1; sequence <= 9999; ++sequence) {
        const int n = snprintf(out, size, "%s/p%02d_c%d_u%d_%03u.wav",
                               RECORDING_DIR, patch, chain, unit, sequence);
        if (n < 0 || n >= (int)size) return false;
        FILE* existing = fopen(out, "rb");
        if (!existing) return true;
        fclose(existing);
    }
    return false;
}

struct ExportStreamContext {
    int slot = -1;
    uint8_t channels = 0;
    uint32_t frames = 0;
    uint32_t sample_generation = 0;
    RecUnit* recorder = nullptr;
    uint32_t take_generation = 0;
    uint64_t session_token = 0, state_token = 0;
    Engine* engine = nullptr;
    platform::AudioOut* audio = nullptr;
    Unit* unit = nullptr;
    int chain = -1, unit_slot = -1;
};

static bool export_owner_current(const ExportStreamContext& context) {
    // Once IO starts, an unrelated writer cannot invalidate a stable immutable
    // source. Revalidate THIS owner/session on every read and after rename; main
    // project/send controllers separately recheck the global busy boundary.
    if (g_eng != context.engine || g_audio != context.audio || !g_eng ||
        context.chain < 0 || context.chain >= NUM_CHAINS || context.unit_slot < 0 || context.unit_slot >= UNITS_PER_CHAIN ||
        g_eng->chains[context.chain].units[context.unit_slot] != context.unit) return false;
    if (sample_slot_for_unit(context.unit) != context.slot) return false;
    if (!context.recorder) return true;
    const auto& rec = *context.recorder;
    return !rec.writer_open() && rec.session_token() == context.session_token && rec.state_token() == context.state_token &&
        rec.take_generation() == context.take_generation && rec.recorded_frames() == context.frames;
}

static bool read_export_frames(void* opaque, uint32_t first_frame, uint32_t frames,
                               int16_t* interleaved_out) {
    auto& context = *static_cast<ExportStreamContext*>(opaque);
    platform::AudioOut::Guard guard(*g_audio);
    if (!export_owner_current(context)) return false;
    const auto& sample = trackr::synth::SampleBank::instance().slot(context.slot);
    if (sample.channels != context.channels || sample.playable_frames() < context.frames ||
        trackr::synth::SampleBank::instance().generation(context.slot) != context.sample_generation ||
        first_frame > context.frames || frames > context.frames - first_frame) return false;
    const size_t first = (size_t)first_frame * context.channels;
    const size_t count = (size_t)frames * context.channels;
    std::memcpy(interleaved_out, sample.data.data() + first, count * sizeof(int16_t));
    return true;
}

void export_current_sample() {
    Unit* u = edited_unit();
    const int slot = sample_slot_for_unit(u);
    if (!u || slot < 0 ||
        (u->kind == UnitKind::Recorder &&
         (static_cast<RecUnit*>(u)->writer_open() || static_cast<RecUnit*>(u)->recorded_frames() == 0))) {
        g_export_is_load = false; g_export_ok = false; g_export_flash = 120; return;
    }

    ExportStreamContext context;
    {
        platform::AudioOut::Guard g(*g_audio);
        if (g_eng->play_editor_busy()) { g_export_is_load=false;g_export_ok=false;g_export_flash=120;return; }
        context.engine=g_eng;context.audio=g_audio;context.unit=u;context.chain=g_edit_chain;context.unit_slot=g_edit_slot;
        const auto& source = trackr::synth::SampleBank::instance().slot(slot);
        context.frames = sample_logical_frames(source);
        if (!context.frames || (source.channels != 1 && source.channels != 2)) {
            g_export_is_load = false; g_export_ok = false; g_export_flash = 120; return;
        }
        context.slot = slot;
        context.channels = source.channels;
        context.sample_generation = trackr::synth::SampleBank::instance().generation(slot);
        if (u->kind == UnitKind::Recorder) {
            context.recorder = static_cast<RecUnit*>(u);
            context.take_generation = context.recorder->take_generation();
            context.session_token = context.recorder->session_token();
            context.state_token = context.recorder->state_token();
        }
    }

    char path[192];
    if (!unique_recording_path(g_patch_slot, g_edit_chain, g_edit_slot, path, sizeof path)) {
        g_export_is_load = false; g_export_ok = false; g_export_flash = 120; return;
    }
    g_export_is_load = false;
    // 2048 mono frames = 4 KiB. The callback briefly guards only this bounded
    // memcpy; header/data writes, flush and rename all happen with audio running.
    int16_t scratch[2048 * 2];
    g_export_ok = trackr::synth::save_wav_stream(
        path, context.channels, context.frames, ENGINE_SR, scratch, 2048,
        read_export_frames, &context) == trackr::synth::WavSaveResult::Ok;
    if (g_export_ok) {
        platform::AudioOut::Guard g(*g_audio);
        if (!export_owner_current(context) ||
            trackr::synth::SampleBank::instance().generation(context.slot) != context.sample_generation) {
            g_export_ok = false;
        } else {
            trackr::synth::SampleBank::instance().set_provenance(
                slot, trackr::synth::SampleOrigin::Recording, path);
            if (context.recorder) context.recorder->mark_take_saved(context.take_generation);
            mark_project_mutated();
        }
    }
    g_export_flash = 120;
}

struct SampleCommitToken {
    Unit* owner = nullptr;
    UnitKind kind = UnitKind::Empty;
    int original_slot = -1;
    int target_slot = -1;
    uint32_t target_generation = 0;
    SampleSlotRefs target_refs;
    bool target_clipboard = false;
    uint32_t take_generation = 0;
    uint64_t session_token = 0, state_token = 0;
};

static bool same_refs(const SampleSlotRefs& a, const SampleSlotRefs& b) {
    return a.play == b.play && a.grain == b.grain && a.rec == b.rec && a.bounce == b.bounce && a.external == b.external;
}

static bool capture_sample_token(Unit* owner, int target_slot, SampleCommitToken& token) {
    if (!owner || edited_unit() != owner || target_slot < 0 ||
        target_slot >= trackr::synth::SAMPLE_BANK_SIZE) return false;
    const int original = sample_slot_for_unit(owner);
    if (original < 0) return false;
    if (g_eng->play_editor_busy()) return false;
    if (owner->kind == UnitKind::Recorder && static_cast<RecUnit*>(owner)->writer_open()) return false;
    token = {};
    token.owner = owner;
    token.kind = owner->kind;
    token.original_slot = original;
    token.target_slot = target_slot;
    auto& bank = trackr::synth::SampleBank::instance();
    token.target_generation = bank.generation(target_slot);
    token.target_refs = g_eng->sample_slot_refs(target_slot, owner);
    token.target_clipboard = sample_ownership::clipboard_holds(target_slot, g_clip_unit);
    if (owner->kind == UnitKind::Recorder) {
        const auto* rec=static_cast<const RecUnit*>(owner);
        token.take_generation=rec->take_generation();token.session_token=rec->session_token();token.state_token=rec->state_token();
    }
    return true;
}

static bool token_is_current(const SampleCommitToken& token) {
    if (!token.owner || edited_unit() != token.owner || token.owner->kind != token.kind ||
        sample_slot_for_unit(token.owner) != token.original_slot) return false;
    auto& bank = trackr::synth::SampleBank::instance();
    if (bank.generation(token.target_slot) != token.target_generation ||
        sample_ownership::clipboard_holds(token.target_slot, g_clip_unit) != token.target_clipboard ||
        !same_refs(g_eng->sample_slot_refs(token.target_slot, token.owner), token.target_refs)) return false;
    if (g_eng->play_editor_busy()) return false;
    if (token.kind == UnitKind::Recorder) {
        const auto* rec = static_cast<const RecUnit*>(token.owner);
        if (rec->writer_open() || rec->take_generation() != token.take_generation ||
            rec->session_token()!=token.session_token || rec->state_token()!=token.state_token) return false;
    }
    return true;
}

static bool has_wav_extension(const char* name) {
    const size_t n = name ? std::strlen(name) : 0;
    if (n < 4) return false;
    const char* e = name + n - 4;
    return e[0] == '.' && (e[1] == 'w' || e[1] == 'W') &&
           (e[2] == 'a' || e[2] == 'A') && (e[3] == 'v' || e[3] == 'V');
}

static bool sample_entry_less(bool a_dir, const char* a, bool b_dir, const char* b) {
    if (a_dir != b_dir) return a_dir;
    return std::strcmp(a, b) < 0;
}

static const char* browser_basename(const char* path) {
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}
static void browser_append(const char* path, const char* name, bool directory, int slot = -1) {
    if (g_sample_file_count >= MAX_SAMPLE_FILES) { browser_limited = true; return; }
    const int i = g_sample_file_count++;
    std::snprintf(browser_entries[i].path, sizeof browser_entries[i].path, "%s", path);
    browser_entries[i].slot = slot;
    std::snprintf(g_sample_files[i], SAMPLE_NAME_LEN, "%s", name);
    g_sample_file_is_dir[i] = directory;
}
static void browser_rebuild() {
    sample_browser_stop_preview();
    sample_browser_reset_motion();
    browser_clear_wave();
    browser_message("");
    browser_limited = false;
    g_sample_file_count = g_sample_scroll = g_sample_scroll_q8 = 0;
    g_sample_selected = g_sample_slot_selected = -1;
    for (auto& entry : browser_entries) entry = {};
    for (auto& info : g_sample_file_info) info = {};
    std::memset(g_sample_file_is_dir, 0, sizeof g_sample_file_is_dir);
    if (g_sample_browser_mode == kProject) {
        if (!g_eng || !g_audio) return;
        platform::AudioOut::Guard guard(*g_audio);
        auto& bank = trackr::synth::SampleBank::instance();
        for (int slot = 0; slot < trackr::synth::SAMPLE_BANK_SIZE; ++slot) {
            const auto& p = bank.provenance(slot);
            const uint32_t frames = sample_slot_frames(slot);
            if (!frames && !p.missing) continue;
            char generated[32];
            std::snprintf(generated, sizeof generated, "%s %02d",
                p.origin == trackr::synth::SampleOrigin::Recording ? "recording" : "sound", slot + 1);
            const int index = g_sample_file_count;
            browser_append(p.path, p.path[0] ? browser_basename(p.path) : generated, false, slot);
            browser_entries[index].generation = bank.generation(slot);
            g_sample_file_info[index] = {frames, ENGINE_SR, true, frames > 0};
        }
    } else if (g_sample_browser_mode == kFavorites) {
        for (int i = 0; i < browser_store.count; ++i) {
            const auto& e = browser_store.entries[i];
            browser_append(e.path, browser_basename(e.path), e.directory);
        }
    } else {
        char directory[192];
        DIR* dir = sample_browser_directory_path(directory, sizeof directory) ? opendir(directory) : nullptr;
        if (!dir) browser_message("folder unavailable");
        else {
            while (dirent* ent = readdir(dir)) {
                if (ent->d_name[0] == '.') continue;
                if (std::strlen(ent->d_name) >= SAMPLE_NAME_LEN) { browser_limited = true; continue; }
                char path[192];
                if (!sample_browser_path(ent->d_name, path, sizeof path)) { browser_limited = true; continue; }
                bool is_dir = ent->d_type == DT_DIR;
                if (ent->d_type == DT_UNKNOWN) {
                    DIR* probe = opendir(path);
                    if (probe) { is_dir = true; closedir(probe); }
                }
                if (!is_dir && !has_wav_extension(ent->d_name)) continue;
                if (g_sample_file_count == MAX_SAMPLE_FILES) { browser_limited = true; break; }
                browser_append(path, ent->d_name, is_dir);
            }
            closedir(dir);
        }
    }
    if (g_sample_browser_mode != kProject) {
        // Directories first; move path identity along with the display name.
        for (int i = 1; i < g_sample_file_count; ++i) {
            char key[SAMPLE_NAME_LEN]; std::strcpy(key, g_sample_files[i]);
            const bool key_dir = g_sample_file_is_dir[i];
            const auto entry = browser_entries[i];
            int j = i - 1;
            while (j >= 0 && sample_entry_less(key_dir, key, g_sample_file_is_dir[j], g_sample_files[j])) {
                std::strcpy(g_sample_files[j + 1], g_sample_files[j]);
                g_sample_file_is_dir[j + 1] = g_sample_file_is_dir[j];
                browser_entries[j + 1] = browser_entries[j]; --j;
            }
            std::strcpy(g_sample_files[j + 1], key);
            g_sample_file_is_dir[j + 1] = key_dir;
            browser_entries[j + 1] = entry;
        }
    }
    for (const auto& loc : browser_locations) {
        if (loc.mode != g_sample_browser_mode || std::strcmp(loc.directory, g_sample_relative_dir)) continue;
        sample_browser_set_scroll_q8(loc.scroll);
        for (int i = 0; i < g_sample_file_count; ++i) {
            const auto& e = browser_entries[i];
            if ((loc.slot >= 0 && e.slot == loc.slot) ||
                (loc.slot < 0 && loc.path[0] && !std::strcmp(e.path, loc.path))) {
                g_sample_selected = i; g_sample_slot_selected = e.slot;
                if (!g_sample_file_is_dir[i]) { browser_pending = 2; browser_pending_play = false; }
                break;
            }
        }
        break;
    }
    scan_sample_file_page_metadata();
}
void scan_sample_files() {
    if (browser_owner) browser_remember();
    char path[256];
    const int n = std::snprintf(path, sizeof path, "%s/.browser.dat", SAMPLE_DIR);
    if (!browser_store_loaded || std::strcmp(path, browser_store_path)) {
        browser_store = {};
        browser_store_loaded = true;
        std::snprintf(browser_store_path, sizeof browser_store_path, "%s", n > 0 && n < int(sizeof path) ? path : "");
        sample_browser_store::load(browser_store_path, browser_store);
    }
    browser_owner = 0;
    if (g_eng && g_audio && g_edit_chain >= 0 && g_edit_chain < NUM_CHAINS &&
        g_edit_slot >= 0 && g_edit_slot < UNITS_PER_CHAIN) {
        platform::AudioOut::Guard guard(*g_audio);
        if (sample_slot_for_unit(edited_unit()) >= 0) browser_owner = uintptr_t(edited_unit());
    }
    browser_owner_chain = g_edit_chain; browser_owner_slot = g_edit_slot;
    if (g_sample_browser_mode < 0 || g_sample_browser_mode > 3) g_sample_browser_mode = kLibrary;
    browser_menu = 0;
    browser_rebuild();
}
static void browser_switch_source(int mode) {
    browser_remember();
    g_sample_browser_mode = mode;
    std::snprintf(g_sample_relative_dir, sizeof g_sample_relative_dir, "%s",
        mode == kLibrary || mode == kRecordings ? browser_directories[mode == kRecordings] : "");
    browser_menu = 0;
    browser_rebuild();
}
static void sample_browser_parent() {
    if (!g_sample_relative_dir[0]) return;
    browser_remember();
    char* slash = std::strrchr(g_sample_relative_dir, '/');
    if (slash) *slash = 0; else g_sample_relative_dir[0] = 0;
    browser_rebuild();
}
static bool sample_browser_enter(int index) {
    if (index < 0 || index >= g_sample_file_count || !g_sample_file_is_dir[index]) return false;
    const auto entry = browser_entries[index];
    DIR* dir = opendir(entry.path);
    if (!dir) { browser_message("folder unavailable"); return false; }
    closedir(dir);
    int mode = -1; const char* relative = nullptr;
    for (int candidate : {kLibrary, kRecordings}) {
        const char* root = candidate == kLibrary ? SAMPLE_DIR : RECORDING_DIR;
        const size_t n = std::strlen(root);
        if (!std::strncmp(entry.path, root, n) && entry.path[n] == '/') {
            mode = candidate; relative = entry.path + n + 1; break;
        }
    }
    if (!relative || std::strlen(relative) >= sizeof g_sample_relative_dir) {
        browser_message("folder path too long"); return false;
    }
    g_sample_selected = index; // returning to the parent restores the opened folder
    g_sample_slot_selected = -1;
    browser_remember();
    g_sample_browser_mode = mode;
    std::snprintf(g_sample_relative_dir, sizeof g_sample_relative_dir, "%s", relative);
    browser_rebuild();
    return true;
}

static int sample_slot_ref_count(int slot, const Unit* ignore) {
    return sample_ownership::count(*g_eng, slot, g_clip_unit, ignore);
}

static bool delete_sample_slot_for_owner(int slot, Unit* owner) {
    if (slot < 0 || slot >= trackr::synth::SAMPLE_BANK_SIZE) return false;
    trackr::synth::Sample retired;
    {
        platform::AudioOut::Guard guard(*g_audio);
        const int references = sample_slot_ref_count(slot, nullptr);
        const bool owner_uses_slot = owner && sample_slot_for_unit(owner) == slot;
        const bool safe_owner_only = owner_uses_slot && owner->kind != UnitKind::Recorder && references == 1;
        if ((!safe_owner_only && references != 0) ||
            trackr::synth::SampleBank::instance().slot(slot).empty()) return false;
        trackr::synth::SampleBank::instance().swap_sample_unchecked(
            slot, retired, trackr::synth::SampleOrigin::Runtime, "");
    }
    // Retired PCM is released after the audio worker is allowed to continue. A
    // sole PLAY/GRAN owner deliberately remains pointed at the now-empty slot.
    mark_project_mutated();
    return true;
}

static const char* sample_slot_short_name(const trackr::synth::SampleProvenance& provenance) {
    if (provenance.path[0]) {
        const char* slash = std::strrchr(provenance.path, '/');
        return slash ? slash + 1 : provenance.path;
    }
    return provenance.origin == trackr::synth::SampleOrigin::Recording ? "recording" : "runtime";
}

static int find_free_sample_slot(const Unit* owner) {
    const int current = sample_slot_for_unit(owner);
    // Preserve an already-private slot so repeated loads replace only this track.
    if (current >= 0 && sample_slot_ref_count(current, owner) == 0) return current;

    // Prefer genuinely empty, unreferenced slots. Start after the factory-loaded
    // range so legacy s00..s15 assignments and recordings are not overwritten.
    for (int pass = 0; pass < 2; ++pass) {
        const int begin = pass == 0 ? 16 : 0;
        const int end = pass == 0 ? trackr::synth::SAMPLE_BANK_SIZE : 16;
        for (int slot = begin; slot < end; ++slot) {
            if (sample_slot_ref_count(slot, owner) != 0) continue;
            if (sample_slot_frames(slot) == 0) return slot;
        }
    }
    return -1;
}

static void browser_decode_notice(trackr::synth::WavLoadResult result) {
    using R = trackr::synth::WavLoadResult;
    switch (result) {
        case R::Ok: browser_message(""); break;
        case R::Truncated: browser_message("first 20s only"); break;
        case R::FileNotFound: browser_message("file missing or unreadable"); break;
        case R::UnsupportedFormat: browser_message("unsupported wav format"); break;
        case R::UnsupportedBits: browser_message("unsupported wav bit depth"); break;
        case R::UnsupportedChannels: browser_message("mono or stereo only"); break;
        case R::MemoryBudget: browser_message("wav exceeds decode memory"); break;
        default: browser_message("invalid or incomplete wav"); break;
    }
}
static void browser_edit_notice(trackr::synth::SampleEditResult result) {
    using R = trackr::synth::SampleEditResult;
    module_focus_show_sample_result(result);
    switch (result) {
        case R::Ok: browser_message(""); break;
        case R::NoFreeSlot: browser_message("project sample bank full"); break;
        case R::MemoryBudget: browser_message("sample memory full"); break;
        case R::SharedSlot: browser_message("sound is shared"); break;
        default: browser_message("sound busy or changed"); break;
    }
}
static bool load_sample_file_to_slot(int file_index, Unit* owner) {
    if (!browser_owner_current() || !owner || file_index < 0 || file_index >= g_sample_file_count ||
        g_sample_file_is_dir[file_index] || browser_entries[file_index].slot >= 0) return false;
    SampleCommitToken owner_token;
    {
        platform::AudioOut::Guard guard(*g_audio);
        const int original_slot = sample_slot_for_unit(owner);
        if (!capture_sample_token(owner, original_slot, owner_token)) {
            browser_message("sound busy or changed"); return false;
        }
    }

    const char* path = browser_entries[file_index].path;
    trackr::synth::Sample loaded;
    trackr::synth::WavDecodeLimits limits;
    limits.workspace_bytes = trackr::synth::WAV_DECODE_WORKSPACE_BYTES;
    const auto r = trackr::synth::load_wav_to_sample(path, loaded, ENGINE_SR, ENGINE_SR * 20, limits);
    const bool ok = r == trackr::synth::WavLoadResult::Ok || r == trackr::synth::WavLoadResult::Truncated;
    browser_decode_notice(r);
    if (!ok) return false;

    platform::AudioOut::Guard guard(*g_audio);
    if (!token_is_current(owner_token)) {
        browser_edit_notice(trackr::synth::SampleEditResult::Busy);
        return false;
    }
    const int slot = find_free_sample_slot(owner);
    if (slot < 0) {
        browser_edit_notice(trackr::synth::SampleEditResult::NoFreeSlot);
        return false;
    }
    auto& bank = trackr::synth::SampleBank::instance();
    if (!bank.can_replace(slot, loaded.data.capacity())) {
        browser_edit_notice(trackr::synth::SampleEditResult::MemoryBudget);
        return false;
    }
    const auto origin = owner->kind == UnitKind::Recorder ? trackr::synth::SampleOrigin::Recording
                                                          : trackr::synth::SampleOrigin::SdWav;
    const auto result = bank.replace_sample(slot, std::move(loaded), origin, path);
    if (result != trackr::synth::SampleEditResult::Ok) {
        browser_edit_notice(result);
        return false;
    }
    // Commit ownership only after the sample swap cannot fail.
    if (owner->kind == UnitKind::Recorder) {
        auto* rec = static_cast<RecUnit*>(owner);
        rec->sample_slot = slot;
        rec->adopt_take(bank.slot(slot).playable_frames());
    } else {
        set_sample_slot_for_unit(owner, slot);
        if (sample_slot_for_unit(owner) != slot) return false;
    }
    mark_project_mutated();
    return true;
}

// Private controller + frozen two-screen rendering. Kept separate from the
// sample ownership/export helpers above; included in exactly this translation unit.
#include "ui_sample_browser_impl.h"

} // namespace seance::platform::ui3ds
