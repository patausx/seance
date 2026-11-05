// Included only by ui_sample_library.cpp, inside ui3ds. No standalone exports.
namespace {
bool browser_pending_use = false, browser_audition_owned = false;
constexpr int browser_sources[] = {kLibrary, kProject, kRecordings};
const char* browser_source_name(int mode) {
    switch (mode) {
        case kProject: return "project";
        case kRecordings: return "recordings";
        default: return "library";
    }
}
bool browser_selected_valid() {
    return g_sample_selected >= 0 && g_sample_selected < g_sample_file_count;
}
bool browser_slot_current(const BrowserEntry& entry) {
    return entry.slot >= 0 && entry.slot < trackr::synth::SAMPLE_BANK_SIZE &&
        trackr::synth::SampleBank::instance().generation(entry.slot) == entry.generation;
}

// Exact selection/lifecycle identity shared by touch, confirmation and marquee.
// No borrowed path pointers: relisting or source/owner replacement invalidates it.
struct BrowserIdentity {
    Engine* engine = nullptr;
    platform::AudioOut* audio = nullptr;
    uintptr_t owner = 0;
    int chain = -1, unit = -1, owner_slot = -1, mode = -1, index = -1, slot = -1;
    uint32_t generation = 0;
    uint64_t listing = 0;
    char directory[128] = {}, path[192] = {}, root[192] = {}, name[SAMPLE_NAME_LEN] = {};
};
BrowserIdentity browser_identity() {
    BrowserIdentity id;
    id.engine = g_eng; id.audio = g_audio;
    if (g_eng && g_edit_chain >= 0 && g_edit_chain < NUM_CHAINS && g_edit_slot >= 0 && g_edit_slot < UNITS_PER_CHAIN) {
        id.owner = uintptr_t(edited_unit());
        id.owner_slot = sample_slot_for_unit(edited_unit());
    }
    id.chain = g_edit_chain; id.unit = g_edit_slot; id.mode = g_sample_browser_mode;
    id.index = g_sample_selected; id.listing = browser_listing_revision;
    std::snprintf(id.directory, sizeof id.directory, "%s", g_sample_relative_dir);
    std::snprintf(id.root, sizeof id.root, "%s", browser_root());
    if (browser_selected_valid()) {
        const auto& entry = browser_entries[g_sample_selected];
        id.slot = entry.slot; id.generation = entry.generation;
        std::snprintf(id.path, sizeof id.path, "%s", entry.path);
        std::snprintf(id.name, sizeof id.name, "%s", g_sample_files[g_sample_selected]);
    }
    return id;
}
bool browser_same_identity(const BrowserIdentity& a, const BrowserIdentity& b) {
    return a.engine == b.engine && a.audio == b.audio && a.owner == b.owner &&
        a.chain == b.chain && a.unit == b.unit && a.owner_slot == b.owner_slot && a.mode == b.mode && a.index == b.index &&
        a.slot == b.slot && a.generation == b.generation && a.listing == b.listing &&
        !std::strcmp(a.directory, b.directory) && !std::strcmp(a.root, b.root) &&
        !std::strcmp(a.path, b.path) && !std::strcmp(a.name, b.name);
}
bool browser_visible() {
    return g_view == View::Chains && g_overlay == Overlay::SampleBrowser &&
        !nav_is_open() && browser_owner_current();
}
bool browser_identity_current(const BrowserIdentity& id) {
    return browser_visible() && browser_same_identity(id, browser_identity());
}
struct BrowserNameMotion {
    BrowserIdentity identity;
    uint64_t began = 0;
    int offset = 0;
    bool running = false;
} browser_name_motion;
int browser_name_offset(const char* name, uint64_t elapsed) {
    const int overflow = ui::text_w(name, 2) - 352;
    if (overflow <= 0) return 0;
    const uint64_t travel = (uint64_t(overflow) * 1000 + 23) / 24;
    const uint64_t phase = elapsed % (750 + travel + 1000);
    if (phase <= 750) return 0;
    if (phase >= 750 + travel) return overflow;
    const int moved = int((phase - 750) * 24 / 1000);
    return moved < overflow ? moved : overflow;
}
void browser_name_suspend() { browser_name_motion.running = false; }
int browser_capture_name_offset() {
    if (!browser_visible()) { browser_name_suspend(); return browser_name_motion.offset; }
    const auto id = browser_identity();
    const uint64_t now = osGetTime();
    if (!browser_name_motion.running || !browser_same_identity(id, browser_name_motion.identity) ||
        now < browser_name_motion.began) {
        browser_name_motion.identity = id; browser_name_motion.began = now;
        browser_name_motion.running = true;
    }
    browser_name_motion.offset = browser_name_offset(browser_selected_valid() ? id.name : "", now - browser_name_motion.began);
    return browser_name_motion.offset;
}
struct BrowserDeleteToken {
    BrowserIdentity identity;
    SampleCommitToken sample;
    struct stat file{};
    bool valid = false;
} browser_delete;
uint64_t browser_menu_serial = 0, browser_touch_menu_serial = 0;
BrowserIdentity browser_touch_identity;
bool browser_destructive_touch_cancelled = false;
void browser_cancel_menu() {
    browser_menu = 0; browser_delete = {}; ++browser_menu_serial;
}
} // namespace

static void browser_name_reset() {
    browser_name_motion.identity = browser_identity();
    browser_name_motion.began = osGetTime();
    browser_name_motion.offset = 0;
    browser_name_motion.running = browser_visible();
}

// Internal audio cancellation deliberately does not restart name motion on a
// drag/lift/listen of the same selection. Public cancellation is the main.cpp
// START/navigation/leaving boundary and also destroys any confirmation token.
static void browser_stop_audio() {
    if (browser_pending) browser_message("");
    browser_pending = 0;
    browser_pending_play = browser_pending_use = false;
    if (browser_audition_owned && g_eng && g_audio) {
        platform::AudioOut::Guard guard(*g_audio);
        g_eng->stop_sample_preview();
    }
    browser_audition_owned = false;
    g_sample_slot_previewing = -1;
}

void sample_browser_stop_preview() {
    browser_stop_audio();
    browser_cancel_menu();
    browser_name_suspend();
}

static void browser_close() {
    browser_remember();
    sample_browser_stop_preview();
    sample_browser_reset_motion();
    browser_menu = 0;
    g_overlay = Overlay::Editor;
}

// File decoding and project PCM copies happen on the UI thread, never in draw
// or under the audio guard. Project sources with writers are deliberately busy.
static bool browser_prepare_selected(bool play) {
    if (!browser_owner_current() || !browser_selected_valid()) return false;
    const auto entry = browser_entries[g_sample_selected];
    if (g_sample_file_is_dir[g_sample_selected]) return false;
    browser_clear_wave();
    trackr::synth::Sample staged;
    if (entry.slot >= 0) {
        uint32_t frames = 0; uint8_t channels = 0;
        {
            platform::AudioOut::Guard guard(*g_audio);
            auto& bank = trackr::synth::SampleBank::instance();
            if (!browser_slot_current(entry)) { browser_message("sound changed; reopen project"); return false; }
            const auto refs = g_eng->sample_slot_refs(entry.slot);
            if (refs.rec || refs.bounce) { browser_message("recording sound is busy"); return false; }
            const auto& source = bank.slot(entry.slot);
            frames = source.playable_frames(); channels = source.channels;
        }
        if (!frames || (channels != 1 && channels != 2)) { browser_message("source missing or empty"); return false; }
        if (uint64_t(frames) * channels * sizeof(q15) > trackr::synth::WAV_DECODE_WORKSPACE_BYTES) {
            browser_message("preview exceeds memory"); return false;
        }
        staged.channels = channels; staged.logical_frames = frames;
        staged.data.resize(size_t(frames) * channels);
        const auto& source = trackr::synth::SampleBank::instance().slot(entry.slot);
        // No REC/bounce writer; UI owns structural mutations and is serialized.
        std::memcpy(staged.data.data(), source.data.data(), staged.data.size() * sizeof(q15));
        sample_wave_capture(staged, g_sample_file_wave);
        {
            platform::AudioOut::Guard guard(*g_audio);
            const auto refs = g_eng->sample_slot_refs(entry.slot);
            if (!browser_owner_current() || !browser_slot_current(entry) || refs.rec || refs.bounce) {
                browser_clear_wave(); browser_message("sound busy or changed"); return false;
            }
            if (play) g_eng->swap_sample_preview(staged);
        }
        browser_wave_slot = entry.slot; browser_wave_generation = entry.generation;
        g_sample_slot_previewing = play ? entry.slot : -1;
        browser_message("");
    } else {
        trackr::synth::WavDecodeLimits limits;
        limits.workspace_bytes = trackr::synth::WAV_DECODE_WORKSPACE_BYTES;
        const auto result = trackr::synth::load_wav_to_sample(
            entry.path, staged, ENGINE_SR, ENGINE_SR * 20, limits);
        browser_decode_notice(result);
        if (result != trackr::synth::WavLoadResult::Ok && result != trackr::synth::WavLoadResult::Truncated) return false;
        sample_wave_capture(staged, g_sample_file_wave);
        std::snprintf(g_sample_file_wave_path, sizeof g_sample_file_wave_path, "%s", entry.path);
        browser_truncated = result == trackr::synth::WavLoadResult::Truncated;
        if (play) {
            platform::AudioOut::Guard guard(*g_audio);
            if (!browser_owner_current()) { browser_clear_wave(); return false; }
            g_eng->swap_sample_preview(staged);
        }
    }
    browser_audition_owned = play;
    // staged retires the previous preview, or the non-audition PCM, after guard.
    return true;
}

static void browser_select(int index, bool play) {
    if (index < 0 || index >= g_sample_file_count) return;
    const bool changed = g_sample_selected != index;
    browser_stop_audio(); browser_cancel_menu();
    browser_clear_wave(); browser_message("");
    g_sample_selected = index;
    if (changed) browser_name_reset();
    g_sample_slot_selected = browser_entries[index].slot;
    if (!g_sample_file_is_dir[index]) {
        browser_pending = 2; browser_pending_play = play;
        browser_message("loading preview...");
    }
}

// REC is a writer: never retarget its workspace onto a populated source. Copy
// into a private destination, like loading a WAV, leaving shared source PCM and
// provenance untouched. No allocator or full PCM copy under the audio guard.
static bool browser_copy_to_recorder(const BrowserEntry& entry, RecUnit& rec) {
    using namespace trackr::synth;
    SampleCommitToken token;
    uint32_t frames = 0; uint8_t channels = 0;
    {
        platform::AudioOut::Guard guard(*g_audio);
        if (!browser_slot_current(entry) || !sample_slot_frames(entry.slot)) {
            browser_message("source missing or changed"); return false;
        }
        if (rec.writer_open() || g_eng->play_editor_busy()) { browser_message("recording sound is busy"); return false; }
        if (entry.slot == rec.sample_slot) return true; // already installed; no writes
        const auto refs = g_eng->sample_slot_refs(entry.slot);
        if (refs.rec || refs.bounce) { browser_message("recording sound is busy"); return false; }
        const int target = find_free_sample_slot(&rec);
        if (target < 0) { browser_edit_notice(SampleEditResult::NoFreeSlot); return false; }
        if (!capture_sample_token(&rec, target, token)) { browser_message("sound busy or changed"); return false; }
        const auto& source = SampleBank::instance().slot(entry.slot);
        frames = source.playable_frames(); channels = source.channels;
        if (!SampleBank::instance().can_replace(target, size_t(frames) * channels)) {
            browser_edit_notice(SampleEditResult::MemoryBudget); return false;
        }
    }
    if ((channels != 1 && channels != 2) || uint64_t(frames) * channels * sizeof(q15) > WAV_DECODE_WORKSPACE_BYTES) {
        browser_message("source exceeds copy memory"); return false;
    }
    Sample staged;
    staged.channels = channels; staged.logical_frames = frames;
    staged.data.resize(size_t(frames) * channels);
    const auto& source = SampleBank::instance().slot(entry.slot);
    std::memcpy(staged.data.data(), source.data.data(), staged.data.size() * sizeof(q15));
    {
        platform::AudioOut::Guard guard(*g_audio);
        const auto refs = g_eng->sample_slot_refs(entry.slot);
        if (!browser_owner_current() || !token_is_current(token) || !browser_slot_current(entry) || refs.rec || refs.bounce) {
            browser_message("sound busy or changed"); return false;
        }
        auto& bank = SampleBank::instance();
        const auto result = bank.replace_sample(token.target_slot, std::move(staged),
            SampleOrigin::Recording, bank.provenance(entry.slot).path);
        if (result != SampleEditResult::Ok) { browser_edit_notice(result); return false; }
        rec.sample_slot = token.target_slot;
        rec.adopt_take(frames);
        mark_project_mutated();
    }
    return true;
}

bool sample_browser_assign_selected_slot() {
    if (!browser_owner_current() || g_sample_browser_mode != kProject || !browser_selected_valid()) return false;
    const auto entry = browser_entries[g_sample_selected];
    Unit* owner = edited_unit();
    sample_browser_stop_preview();
    if (owner->kind == UnitKind::Recorder) {
        if (!browser_copy_to_recorder(entry, *static_cast<RecUnit*>(owner))) return false;
    } else {
        platform::AudioOut::Guard guard(*g_audio);
        if (!browser_slot_current(entry) || !sample_slot_frames(entry.slot)) {
            browser_message("source missing or changed"); return false;
        }
        set_sample_slot_for_unit(owner, entry.slot);
    }
    browser_close();
    return true;
}

bool sample_browser_load_selected() {
    if (!browser_owner_current() || !browser_selected_valid()) return false;
    if (g_sample_file_is_dir[g_sample_selected]) return sample_browser_enter(g_sample_selected);
    if (g_sample_browser_mode == kProject) return sample_browser_assign_selected_slot();
    sample_browser_stop_preview();
    g_export_is_load = true;
    g_export_ok = load_sample_file_to_slot(g_sample_selected, edited_unit());
    g_export_flash = 120;
    if (g_export_ok) browser_close();
    return g_export_ok;
}

bool sample_browser_back() {
    if (browser_menu) { browser_cancel_menu(); return true; }
    sample_browser_stop_preview();
    if ((g_sample_browser_mode == kLibrary || g_sample_browser_mode == kRecordings) && g_sample_relative_dir[0]) {
        sample_browser_parent(); return true;
    }
    return false;
}

static void browser_toggle_auto() {
    browser_store_draft = browser_store;
    browser_store_draft.auto_preview = !browser_store.auto_preview;
    if (!sample_browser_store::save(browser_store_path, browser_store_draft)) {
        browser_message("could not save auto preview"); return;
    }
    browser_store = browser_store_draft; browser_message("");
}
static void browser_listen() {
    bool active = false;
    if (browser_audition_owned && g_eng && g_audio) {
        platform::AudioOut::Guard guard(*g_audio); active = g_eng->sample_preview_active();
    }
    if (active || (browser_pending && browser_pending_play)) browser_stop_audio();
    else if (browser_selected_valid()) browser_select(g_sample_selected, true);
}
static void browser_request_use() {
    if (!browser_selected_valid()) return;
    if (g_sample_file_is_dir[g_sample_selected]) { sample_browser_enter(g_sample_selected); return; }
    if (g_sample_browser_mode == kProject) { sample_browser_assign_selected_slot(); return; }
    browser_stop_audio();
    browser_pending = 2; browser_pending_use = true;
    browser_message("loading sound...");
}
// All provenance is protected, even an empty/missing bank placeholder. Engine
// aliases (PLAY/GRAN/SCRT/PAUL/REC, bounce/external/clipboard) share this bank.
// Pending PLAY loader metadata can additionally hold a source before hydration.
// Caller holds Guard; pure path normalization only, never stat/getcwd here.
static bool browser_file_loaded_lexically(const char* path, const char* root) {
    const bool fat = browser_file_safety::fat_path(root);
    auto same = [&](const char* other) {
        return browser_file_safety::same_path(path, other, browser_cwd, fat);
    };
    auto& bank = trackr::synth::SampleBank::instance();
    for (int i = 0; i < trackr::synth::SAMPLE_BANK_SIZE; ++i)
        if (same(bank.provenance(i).path)) return true;
    auto pending = [&](const Unit* unit) {
        if (!unit || unit->kind != UnitKind::SamplePlayer) return false;
        const auto* p = static_cast<const PlayUnit*>(unit)->pending_edit_source();
        return p && same(p->provenance.path);
    };
    for (const auto& chain : g_eng->chains) for (const Unit* unit : chain.units) if (pending(unit)) return true;
    return pending(g_clip_unit);
}
static bool browser_can_remove_locked() {
    if (!browser_owner_current() || !browser_selected_valid() || g_sample_file_is_dir[g_sample_selected] ||
        g_eng->recording_active_or_pending() || g_eng->play_editor_busy()) return false;
    const auto& entry = browser_entries[g_sample_selected];
    if (g_sample_browser_mode != kProject) {
        return (g_sample_browser_mode == kLibrary || g_sample_browser_mode == kRecordings) && entry.slot < 0 &&
            has_wav_extension(entry.path) && browser_file_safety::within(entry.path, browser_root(), browser_cwd) &&
            !browser_file_loaded_lexically(entry.path, browser_root());
    }
    if (!browser_slot_current(entry) || !sample_slot_frames(entry.slot)) return false;
    const int references = sample_slot_ref_count(entry.slot, nullptr);
    Unit* owner = edited_unit();
    return references == 0 || (references == 1 && sample_slot_for_unit(owner) == entry.slot && owner->kind != UnitKind::Recorder);
}
static bool browser_can_remove() {
    if (!browser_visible()) return false;
    platform::AudioOut::Guard guard(*g_audio);
    return browser_can_remove_locked();
}
struct BrowserFileOwners {
    static constexpr int capacity = trackr::synth::SAMPLE_BANK_SIZE + NUM_CHAINS * UNITS_PER_CHAIN + 1;
    char paths[capacity][192] = {};
    uint32_t generations[trackr::synth::SAMPLE_BANK_SIZE] = {};
    int count = trackr::synth::SAMPLE_BANK_SIZE;
};
// Guarded bounded metadata copies; no PCM reads, allocator or filesystem calls.
static void browser_file_owners_capture(BrowserFileOwners& owners) {
    auto& bank = trackr::synth::SampleBank::instance();
    for (int i = 0; i < trackr::synth::SAMPLE_BANK_SIZE; ++i) {
        owners.generations[i] = bank.generation(i);
        std::snprintf(owners.paths[i], sizeof owners.paths[i], "%s", bank.provenance(i).path);
    }
    auto pending = [&](const Unit* unit) {
        if (!unit || unit->kind != UnitKind::SamplePlayer) return;
        const auto* p = static_cast<const PlayUnit*>(unit)->pending_edit_source();
        if (p && owners.count < BrowserFileOwners::capacity)
            std::snprintf(owners.paths[owners.count++], 192, "%s", p->provenance.path);
    };
    for (const auto& chain : g_eng->chains) for (const Unit* unit : chain.units) pending(unit);
    pending(g_clip_unit);
}
static bool browser_file_owners_current(const BrowserFileOwners& owners) {
    auto& bank = trackr::synth::SampleBank::instance();
    for (int i = 0; i < trackr::synth::SAMPLE_BANK_SIZE; ++i)
        if (owners.generations[i] != bank.generation(i) || std::strcmp(owners.paths[i], bank.provenance(i).path)) return false;
    int at = trackr::synth::SAMPLE_BANK_SIZE;
    bool current = true;
    auto pending = [&](const Unit* unit) {
        if (!unit || unit->kind != UnitKind::SamplePlayer) return;
        const auto* p = static_cast<const PlayUnit*>(unit)->pending_edit_source();
        if (p) {
            if (at >= owners.count || std::strcmp(owners.paths[at], p->provenance.path)) current = false;
            ++at;
        }
    };
    for (const auto& chain : g_eng->chains) for (const Unit* unit : chain.units) pending(unit);
    pending(g_clip_unit);
    return current && at == owners.count;
}
static bool browser_file_owner_alias(const BrowserFileOwners& owners, const BrowserDeleteToken& token) {
    // UI thread outside Guard: host hard links/symlink aliases are recognized by
    // dev/inode as well as lexical paths. FAT provenance compares case-folded
    // sdmc:/ and / paths; its stat inode is not treated as a unique file ID.
    for (int i = 0; i < owners.count; ++i) {
        const char* path = owners.paths[i];
        if (!path[0]) continue;
        if (browser_file_safety::same_path(path, token.identity.path, browser_cwd,
                                         browser_file_safety::fat_path(token.identity.root))) return true;
#ifndef __3DS__
        struct stat source{};
        if (::stat(path, &source) == 0 && browser_file_safety::same_object(source, token.file)) return true;
#endif
    }
    return false;
}
static bool browser_delete_current(const BrowserDeleteToken& token) {
    return token.valid && browser_identity_current(token.identity) && browser_selected_valid() &&
        !g_sample_file_is_dir[g_sample_selected];
}
static bool browser_delete_file(const BrowserDeleteToken& token) {
    if (!browser_delete_current(token) || !has_wav_extension(token.identity.path)) {
        browser_message("file selection changed"); return false;
    }
    BrowserFileOwners owners;
    {
        platform::AudioOut::Guard guard(*g_audio);
        if (!token_is_current(token.sample) || !browser_can_remove_locked()) {
            browser_message("file loaded or sound busy"); return false;
        }
        browser_file_owners_capture(owners);
    }
    struct stat current{};
    if (!browser_file_safety::inspect(token.identity.path, token.identity.root, browser_cwd, current) ||
        !browser_file_safety::same_identity(token.file, current)) {
        browser_message("file missing, unsafe or changed"); return false;
    }
    if (browser_file_owner_alias(owners, token)) {
        browser_message("file is loaded in project"); return false;
    }
    {
        platform::AudioOut::Guard guard(*g_audio);
        if (!browser_delete_current(token) || !token_is_current(token.sample) ||
            !browser_can_remove_locked() || !browser_file_owners_current(owners)) {
            browser_message("sound busy or changed"); return false;
        }
    }
    // UI structural mutations are serialized. The worker cannot load files;
    // only this bounded metadata preflight is guarded. There remains an external
    // stat-to-unlink race (and coarse FAT timestamps), not atomic inode removal.
    if (std::remove(token.identity.path) != 0) {
        browser_message("could not delete wav"); return false;
    }
    return true;
}
static void browser_open_delete() {
    browser_stop_audio(); browser_cancel_menu(); sample_browser_reset_motion();
    if (!browser_can_remove()) {
        browser_message(g_sample_browser_mode == kProject ? "sound is shared or busy" : "file loaded, folder or sound busy"); return;
    }
    BrowserDeleteToken token;
    token.identity = browser_identity();
    {
        platform::AudioOut::Guard guard(*g_audio);
        const int slot = g_sample_browser_mode == kProject ? browser_entries[g_sample_selected].slot : sample_slot_for_unit(edited_unit());
        if (!capture_sample_token(edited_unit(), slot, token.sample) || !browser_can_remove_locked()) {
            browser_message("sound busy or changed"); return;
        }
    }
    if (g_sample_browser_mode != kProject) {
        if (!browser_file_safety::inspect(token.identity.path, token.identity.root, browser_cwd, token.file)) {
            browser_message("file missing, unsafe or not wav"); return;
        }
        BrowserFileOwners owners;
        {
            platform::AudioOut::Guard guard(*g_audio);
            browser_file_owners_capture(owners);
        }
        if (browser_file_owner_alias(owners, token)) { browser_message("file is loaded in project"); return; }
    }
    if (!browser_identity_current(token.identity)) { browser_message("sample target changed"); return; }
    token.valid = true; browser_delete = token;
    browser_menu = 3; browser_menu_selected = 1; ++browser_menu_serial;
}
static void browser_menu_open(int menu) {
    if (menu != 1) return; // no intermediate more page or legacy fallback
    browser_stop_audio(); browser_cancel_menu(); sample_browser_reset_motion();
    browser_menu = 1; browser_menu_selected = 0;
    for (int i = 0; i < 3; ++i) if (browser_sources[i] == g_sample_browser_mode) browser_menu_selected = i;
}
static void browser_menu_choose(int row) {
    if (browser_menu == 1 && row >= 0 && row < 3) { browser_switch_source(browser_sources[row]); return; }
    if (browser_menu != 3) return;
    const auto token = browser_delete;
    browser_cancel_menu(); // single-use authority, consumed even on a failed commit
    if (row != 0) return;
    browser_stop_audio(); // owned audition/pending use ends before any filesystem I/O
    if (!browser_delete_current(token)) { browser_message("sample target changed"); return; }
    bool ok = false;
    if (token.identity.mode == kProject)
        ok = delete_sample_slot_for_owner(token.sample.target_slot, edited_unit(), &token.sample);
    else ok = browser_delete_file(token);
    if (!ok) {
        if (token.identity.mode == kProject) browser_message("sound busy or changed");
        return;
    }
    const int adjacent = token.identity.index;
    const int scroll = g_sample_scroll_q8;
    browser_remember(); browser_rebuild();
    // Same folder/source, a sensible adjacent row, no automatic audition or
    // project dirty mark for file removal. The project slot primitive marks itself.
    browser_stop_audio(); browser_clear_wave();
    g_sample_selected = g_sample_file_count ? (adjacent < g_sample_file_count ? adjacent : g_sample_file_count - 1) : -1;
    g_sample_slot_selected = browser_selected_valid() ? browser_entries[g_sample_selected].slot : -1;
    sample_browser_set_scroll_q8(scroll);
    browser_name_reset(); browser_remember();
}

// Positive control IDs are disjoint from list rows. Modal touches never reach
// the covered list. Commit on lift only if still inside the original target.
static int browser_touch_target(int x, int y) {
    if (browser_menu) {
        const int count = browser_menu == 1 ? 3 : 2;
        if (x >= 16 && x < 304 && y >= 56 && y < 56 + count * 28) return 20 + (y - 56) / 28;
        return 19; // outside closes the menu, not the browser
    }
    if (hit(x,y,262,0,58,32)) return 1;
    if (hit(x,y,8,32,100,24)) return 2;
    if (hit(x,y,8,202,108,24)) return 4;
    if (hit(x,y,128,202,80,24)) return 5;
    if (hit(x,y,236,202,76,24)) return 6;
    if (hit(x,y,8,226,152,14)) return 7;
    if (hit(x,y,242,226,70,14)) return 8;
    if (y >= kListTop && y < kListBottom && x >= 8 && x < 310) {
        const int row = (y - kListTop + g_sample_scroll_q8 / 256) / kRowHeight;
        if (row < g_sample_file_count) return 100 + row;
    }
    return 0;
}
static void browser_commit_touch(int action) {
    if (browser_menu) { if (action >= 20) browser_menu_choose(action - 20); else browser_cancel_menu(); return; }
    if (action >= 100) {
        const int row = action - 100;
        if (g_sample_file_is_dir[row]) sample_browser_enter(row);
        else {
            browser_select(row, true);
            // An explicit committed row pick is a new selection/audition, even
            // if it names the same row. Mere drag/lift and Y listen are not picks.
            browser_name_reset();
        }
        return;
    }
    switch (action) {
        case 1: browser_close(); break;
        case 2: browser_menu_open(1); break;
        case 4: browser_listen(); break;
        case 5: browser_open_delete(); break;
        case 6: browser_request_use(); break;
        case 7: browser_toggle_auto(); break;
        case 8: if (!sample_browser_back()) browser_close(); break;
        default: break;
    }
}
void sample_browser_touch_down(int x, int y) {
    sample_browser_reset_motion();
    if (!browser_visible()) { sample_browser_stop_preview(); return; }
    browser_touch_identity = browser_identity(); browser_touch_menu_serial = browser_menu_serial;
    browser_destructive_touch_cancelled = false;
    g_sample_touch_x = x; g_sample_touch_y = y; g_sample_touch_scroll_q8 = g_sample_scroll_q8;
    if (!browser_menu && x >= 310 && x < 320 && y >= kListTop && y < kListBottom && sample_browser_max_scroll_px()) {
        browser_stop_audio();
        g_sample_scrollbar_drag = g_sample_touch_scrolling = true;
        sample_browser_scrollbar_to(y); scan_sample_file_page_metadata(); return;
    }
    browser_touch_action = browser_touch_target(x, y);
    browser_touch_inside = browser_touch_action != 0;
    g_sample_touch_list = !browser_menu && browser_touch_action >= 100;
}
void sample_browser_touch_hold(int x, int y) {
    if (!browser_identity_current(browser_touch_identity) || browser_touch_menu_serial != browser_menu_serial) {
        sample_browser_reset_motion(); return;
    }
    if (g_sample_scrollbar_drag) { sample_browser_scrollbar_to(y); scan_sample_file_page_metadata(); return; }
    if (g_sample_touch_list) {
        const int dy = y - g_sample_touch_y, dx = x - g_sample_touch_x;
        if (!g_sample_touch_scrolling && (dy > 8 || dy < -8 || dx > 12 || dx < -12)) {
            g_sample_touch_scrolling = true; browser_stop_audio();
        }
        if (g_sample_touch_scrolling) {
            const int before = g_sample_scroll_q8;
            sample_browser_set_scroll_q8(g_sample_touch_scroll_q8 - dy * 256);
            int velocity = (g_sample_scroll_q8 - before) * 3;
            if (velocity > 1792) velocity = 1792;
            if (velocity < -1792) velocity = -1792;
            g_sample_scroll_velocity_q8 = velocity;
            scan_sample_file_page_metadata(); return;
        }
    }
    browser_touch_inside = browser_touch_action != 0 && browser_touch_target(x, y) == browser_touch_action;
    if (!browser_touch_inside && (browser_menu == 3 || browser_touch_action == 5))
        browser_destructive_touch_cancelled = true; // leaving/returning cannot restore destructive authority
}
void sample_browser_touch_release(bool commit) {
    if (!browser_touch_action && !g_sample_touch_list && !g_sample_scrollbar_drag) {
        if (!commit) g_sample_scroll_velocity_q8 = 0;
        return;
    }
    const int action = browser_touch_action;
    const bool activate = commit && browser_touch_inside && !browser_destructive_touch_cancelled && !g_sample_touch_scrolling &&
        browser_identity_current(browser_touch_identity) && browser_touch_menu_serial == browser_menu_serial;
    const int velocity = commit && g_sample_touch_scrolling && !g_sample_scrollbar_drag ? g_sample_scroll_velocity_q8 : 0;
    sample_browser_reset_motion();
    g_sample_scroll_velocity_q8 = velocity;
    if (activate) browser_commit_touch(action);
}

bool sample_browser_buttons(uint32_t down) {
    if (!(down & (KEY_A|KEY_B|KEY_X|KEY_Y|KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT|KEY_L|KEY_R))) return false;
    sample_browser_touch_release(false);
    if (!browser_visible()) { sample_browser_stop_preview(); browser_message("sample target changed"); return true; }
    if (down & KEY_B) { if (!sample_browser_back()) browser_close(); return true; }
    if (browser_menu) {
        const int count = browser_menu == 1 ? 3 : 2;
        if (down & KEY_UP) browser_menu_selected = (browser_menu_selected + count - 1) % count;
        if (down & KEY_DOWN) browser_menu_selected = (browser_menu_selected + 1) % count;
        if (down & KEY_A) browser_menu_choose(browser_menu_selected);
        return true;
    }
    if (down & (KEY_L|KEY_R)) { browser_menu_open(1); return true; }
    if (down & KEY_Y) { browser_listen(); return true; }
    if (down & KEY_X) { browser_open_delete(); return true; }
    if (down & KEY_A) { browser_request_use(); return true; }
    if (down & KEY_LEFT) { sample_browser_back(); return true; }
    if (down & KEY_RIGHT) {
        if (browser_selected_valid() && g_sample_file_is_dir[g_sample_selected]) sample_browser_enter(g_sample_selected);
        return true;
    }
    if ((down & (KEY_UP|KEY_DOWN)) && g_sample_file_count) {
        int index = g_sample_selected < 0 ? 0 : g_sample_selected + ((down & KEY_DOWN) ? 1 : -1);
        if (index < 0) index = 0;
        if (index >= g_sample_file_count) index = g_sample_file_count - 1;
        browser_select(index, browser_store.auto_preview);
        const int top = index * kRowHeight * 256, bottom = (index + 1) * kRowHeight * 256;
        if (top < g_sample_scroll_q8) sample_browser_set_scroll_q8(top);
        if (bottom > g_sample_scroll_q8 + kRows * kRowHeight * 256) sample_browser_set_scroll_q8(bottom - kRows * kRowHeight * 256);
        scan_sample_file_page_metadata();
    }
    return true;
}
void sample_browser_tick() {
    if (!browser_visible()) {
        sample_browser_stop_preview();
        sample_browser_reset_motion(); return;
    }
    if (browser_menu == 3 && !browser_delete_current(browser_delete)) {
        browser_cancel_menu(); sample_browser_reset_motion(); browser_message("sample target changed");
    }
    if (!g_sample_touch_scrolling && !g_sample_scrollbar_drag && g_sample_scroll_velocity_q8) {
        const int before = g_sample_scroll_q8;
        sample_browser_set_scroll_q8(before + g_sample_scroll_velocity_q8);
        g_sample_scroll_velocity_q8 = g_sample_scroll_velocity_q8 * 27 / 32;
        if ((g_sample_scroll_velocity_q8 > -24 && g_sample_scroll_velocity_q8 < 24) || before == g_sample_scroll_q8) g_sample_scroll_velocity_q8 = 0;
        scan_sample_file_page_metadata();
    }
    if (browser_pending && --browser_pending == 0) {
        const bool use = browser_pending_use, play = browser_pending_play;
        browser_pending_use = browser_pending_play = false;
        if (use) sample_browser_load_selected(); else browser_prepare_selected(play);
    }
}

namespace {
struct BrowserRowView {
    char name[SAMPLE_NAME_LEN] = {};
    uint32_t frames = 0, rate = 0;
    int index = -1;
    bool directory = false, valid = false, installed = false;
};
struct SampleBrowserFrame {
    BrowserRowView rows[kRows + 1];
    SampleWaveView wave;
    char directory[128] = {}, name[SAMPLE_NAME_LEN] = {}, installed[SAMPLE_NAME_LEN] = {}, notice[48] = {};
    int mode = 0, chain = 0, assigned = -1, selected = -1, count = 0, scroll = 0, maximum = 0;
    int menu = 0, menu_selected = 0, references = 0, name_offset_px = 0;
    uint32_t position = 0;
    UnitKind kind = UnitKind::Empty;
    bool valid = false, playing = false, automatic = true, moving = false, can_delete = false;
    bool directory_selected = false, limited = false, truncated = false, pending = false;
};
SampleBrowserFrame sample_frame;
static_assert(sizeof(SampleBrowserFrame) <= 2048 && std::is_trivially_copyable<SampleBrowserFrame>::value,
              "bounded pointer-free dual-screen sample browser frame");
void sample_label(char* out, int capacity, const char* source, int chars, bool wav_suffix = false) {
    if (chars > capacity - 1) chars = capacity - 1;
    if (chars < 3) { out[0] = 0; return; }
    const int length = int(std::strlen(source));
    if (length <= chars) { std::memcpy(out, source, length + 1); return; }
    const int suffix = wav_suffix && chars >= 8 && has_wav_extension(source) ? 4 : 0;
    const int stem = chars - suffix - 2;
    std::memcpy(out, source, stem); out[stem] = '.'; out[stem+1] = '.';
    if (suffix) std::memcpy(out+stem+2, source+length-suffix, suffix);
    out[chars] = 0;
}
void sample_duration(char* out, int capacity, uint32_t frames, uint32_t rate) {
    if (!rate) { std::snprintf(out, capacity, "--.-s"); return; }
    const uint64_t tenths = uint64_t(frames) * 10 / rate;
    std::snprintf(out, capacity, "%llu.%llus", (unsigned long long)(tenths/10), (unsigned long long)(tenths%10));
}
} // namespace

void sample_browser_capture() {
    if (g_view != View::Chains || g_overlay != Overlay::SampleBrowser) return;
    sample_frame = {};
    auto& f = sample_frame;
    f.mode = g_sample_browser_mode; f.chain = g_edit_chain; f.selected = g_sample_selected;
    f.count = g_sample_file_count; f.scroll = g_sample_scroll_q8 / 256; f.maximum = sample_browser_max_scroll_px();
    f.menu = browser_menu; f.menu_selected = browser_menu_selected;
    f.automatic = browser_store.auto_preview; f.moving = g_sample_touch_scrolling || g_sample_scroll_velocity_q8;
    f.limited = browser_limited; f.truncated = browser_truncated; f.pending = browser_pending != 0;
    std::snprintf(f.directory, sizeof f.directory, "%s", g_sample_relative_dir);
    std::snprintf(f.notice, sizeof f.notice, "%s", browser_notice);
    const int first = f.scroll / kRowHeight;
    for (int r = 0; r <= kRows && first+r < f.count; ++r) {
        const int i = first+r; auto& row = f.rows[r];
        row.index = i; row.directory = g_sample_file_is_dir[i];
        row.frames = g_sample_file_info[i].source_frames; row.rate = g_sample_file_info[i].source_rate; row.valid = g_sample_file_info[i].valid;
        std::snprintf(row.name, sizeof row.name, "%s", g_sample_files[i]);
    }
    if (browser_selected_valid()) {
        std::snprintf(f.name, sizeof f.name, "%s", g_sample_files[f.selected]);
        f.directory_selected = g_sample_file_is_dir[f.selected];
        const auto& e = browser_entries[f.selected];
        if (e.slot < 0 && !f.directory_selected && !std::strcmp(e.path, g_sample_file_wave_path)) f.wave = g_sample_file_wave;
    }
    f.name_offset_px = browser_capture_name_offset();
    const int overflow = f.selected >= 0 ? ui::text_w(f.name, 2) - 352 : 0;
    if (f.name_offset_px < 0 || overflow <= 0) f.name_offset_px = 0;
    else if (f.name_offset_px > overflow) f.name_offset_px = overflow;
    if (!browser_owner_current()) return;
    // Metadata, preview cursor and at most one bounded 5632-read PCM projection.
    // No allocations, decoder, storage I/O or GPU submissions in this guard.
    platform::AudioOut::Guard guard(*g_audio);
    auto& bank = trackr::synth::SampleBank::instance();
    Unit* owner = edited_unit();
    f.valid = true; f.kind = owner->kind; f.assigned = sample_slot_for_unit(owner);
    f.can_delete = browser_can_remove_locked();
    if (f.assigned >= 0 && f.assigned < trackr::synth::SAMPLE_BANK_SIZE)
        std::snprintf(f.installed, sizeof f.installed, "%s", sample_slot_frames(f.assigned) ? sample_slot_short_name(bank.provenance(f.assigned)) : "empty");
    for (auto& row : f.rows) if (row.index >= 0 && f.mode == kProject) row.installed = browser_entries[row.index].slot == f.assigned;
    bool wave_matches = f.wave.frames > 0;
    if (browser_selected_valid() && f.mode == kProject) {
        const auto& e = browser_entries[f.selected];
        if (browser_slot_current(e)) {
            f.references = sample_slot_ref_count(e.slot, nullptr);
            sample_wave_capture(bank.slot(e.slot), f.wave);
            wave_matches = e.slot == browser_wave_slot && e.generation == browser_wave_generation;
        } else wave_matches = false;
    }
    f.playing = browser_audition_owned && wave_matches && f.wave.frames && g_eng->sample_preview_active();
    if (f.playing) {
        f.position = g_eng->sample_preview_position();
        if (f.position > f.wave.frames) f.position = f.wave.frames;
    }
}

void sample_browser_draw() {
    namespace m = menu_style;
    const auto& f = sample_frame;
    m::paper(320);
    const int offset = f.scroll % kRowHeight;
    for (int r = 0; r <= kRows; ++r) {
        const auto& row = f.rows[r]; if (row.index < 0) break;
        const int top = kListTop + r*kRowHeight - offset, y = top+8;
        const bool selected = row.index == f.selected;
        if (selected) ui::rect(9,top+4,2,16,m::spot());
        if (selected && f.playing) { ui::line(18,y,23,y+3,m::spot()); ui::line(23,y+3,18,y+7,m::spot()); }
        else if (row.directory) ui::text(17,y,">",m::ghost());
        char duration[24], name[64];
        if (row.directory) std::snprintf(duration,sizeof duration,"open >");
        else sample_duration(duration,sizeof duration,row.frames,row.valid?row.rate:0);
        const int duration_x = 304-ui::text_w(duration);
        sample_label(name,sizeof name,row.name,(duration_x-34)/6,true);
        ui::text(28,y,name,selected?m::spot():m::ink());
        if (row.installed) ui::rect(28,y+9,ui::text_w(name),1,m::spot());
        ui::text(duration_x,y,duration,m::ghost());
    }
    // Fractional rows are native, then masked by the exact frozen paper.
    m::paper(320,0,kListTop); m::paper(320,kListBottom,240);
    // Reviewed 12px smoothstep veil over exact row-dependent captured paper.
    // Scrollbar is deliberately composed afterwards, never faded with the list.
    for (int d=0;d<12;++d) {
        const int n=11-d;
        const int alpha=(255*n*n*(33-2*n)+665)/1331;
        if (f.scroll>0) m::paper_rect(8,kListTop+d,302,1,alpha);
        if (f.scroll<f.maximum) m::paper_rect(8,kListBottom-1-d,302,1,alpha);
    }
    if (!f.count) {
        ui::text(24,80,f.mode==kProject?"no project sounds":"no wav files",m::ink());
        ui::text(24,100,"choose another source above",m::ghost());
    }
    if (f.maximum) {
        int height = 137*kRows/f.count; if (height<10) height=10;
        ui::line(315,59,315,196,m::branch());
        ui::rect(314,59+f.scroll*(137-height)/f.maximum,2,height,f.moving?m::spot():m::ghost());
    }
    m::heading(16,8,"samples",m::ink());
    char label[64];
    if (f.valid) std::snprintf(label,sizeof label,"ch %d / %s",f.chain+1,f.kind==UnitKind::Recorder?"rec":f.kind==UnitKind::Grain?"gran":"play");
    else std::snprintf(label,sizeof label,"no target");
    ui::text(142,19,label,m::ghost()); ui::text(274,19,"close",m::ghost());
    const char* source = browser_source_name(f.mode);
    ui::text(16,41,source,m::spot());
    const int chevron = 16+ui::text_w(source)+8;
    ui::line(chevron,43,chevron+3,46,m::spot()); ui::line(chevron+3,46,chevron+6,43,m::spot());
    ui::text(16,210,f.playing?"y stop":"y listen",f.selected>=0&&!f.directory_selected?m::ink():m::ghost());
    ui::text(144,210,f.mode==kProject?"remove":"delete",f.can_delete?m::ink():m::ghost());
    const char* use=f.directory_selected?"a open >":"a use >";
    ui::text(304-ui::text_w(use),210,use,f.selected>=0?m::spot():m::ghost());
    ui::line(262,221,303,221,m::spot());
    ui::text(16,231,f.automatic?"auto preview on":"auto preview off",m::ghost());
    ui::text(268,231,"b back",m::ghost());
    if (f.menu) {
        m::paper_rect(8,54,304,144);
        const int count=f.menu==1?3:2;
        for (int i=0;i<count;++i) {
            const char* item=f.menu==1?browser_source_name(browser_sources[i]):i==1?"cancel":
                f.mode==kProject?"remove from project":"delete wav";
            if (i==f.menu_selected) m::marker(24,64+i*28,m::spot());
            ui::text(40,60+i*28,item,i==f.menu_selected?m::spot():m::ink());
        }
        if (f.menu==3) {
            char selected_name[64]; sample_label(selected_name,sizeof selected_name,f.name,44,true);
            ui::text(24,120,selected_name,m::ghost());
            ui::text(24,140,f.mode==kProject?"sd file is kept":"deletes wav from sd / cannot undo",m::ghost());
        }
    }
}
void sample_browser_draw_top() {
    namespace m=menu_style;
    const auto& f=sample_frame;
    m::paper(400); ui::text(24,18,"preview",m::ghost());
    char status[48], name[64], duration[24], info[64];
    if (f.playing) { sample_duration(duration,sizeof duration,f.position,ENGINE_SR); std::snprintf(status,sizeof status,"listening / %s",duration); }
    else std::snprintf(status,sizeof status,"%s",f.pending?"loading":"stopped");
    ui::text(376-ui::text_w(status),18,status,f.playing?m::spot():m::ghost());
    // Native glyph submissions stay within x13..388; restore the same frozen
    // paper outside [24,376) x [43,59). No new stencil/atlas or off-LCD glyphs.
    const char* full_name = f.selected>=0?f.name:"select a sample below";
    const int first = f.name_offset_px/12, remainder = f.name_offset_px%12;
    const int title_x = 24-remainder;
    const int visible = (376-title_x+11)/12;
    std::snprintf(name,sizeof name,"%.*s",visible,full_name+first);
    ui::text(title_x,43,name,m::ink(),2);
    m::paper_rect(0,43,24,16); m::paper_rect(376,43,24,16);
    if (f.wave.frames) {
        sample_duration(duration,sizeof duration,f.wave.frames,ENGINE_SR);
        std::snprintf(info,sizeof info,"%s / %s / 32khz%s",duration,f.wave.channels==2?"stereo":"mono",f.truncated?" / first 20s":"");
    } else std::snprintf(info,sizeof info,"%s",f.directory_selected?"folder / a open":f.selected<0?"tap a row to listen":f.pending?"loading preview...":"preview unavailable");
    ui::text(24,70,info,m::ghost());
    ui::line(24,130,375,130,m::branch());
    const int cursor=24+(f.wave.frames?int(uint64_t(f.position)*351/f.wave.frames):0);
    if (f.wave.frames) {
        for (int x=0;x<SAMPLE_WAVE_COLUMNS;++x)
            ui::line(24+x,130-int(f.wave.high[x])*36/50,24+x,130-int(f.wave.low[x])*36/50,f.playing&&24+x<=cursor?m::spot():m::ink());
        if (f.playing) { ui::line(cursor,90,cursor,170,m::spot()); ui::line(cursor-3,87,cursor,90,m::spot()); ui::line(cursor,90,cursor+3,87,m::spot()); }
        ui::text(24,178,"0s",m::ghost()); ui::text(376-ui::text_w(duration),178,duration,m::ghost());
    }
    if (f.notice[0]) sample_label(info,sizeof info,f.notice,58);
    else if (f.limited) std::snprintf(info,sizeof info,"list limited / 128 entries or long paths");
    else if (f.mode==kProject && f.selected>=0) std::snprintf(info,sizeof info,"used by %d module%s / source unchanged",f.references,f.references==1?"":"s");
    else std::snprintf(info,sizeof info,"listen first / use when ready");
    ui::text(24,196,info,m::ghost());
    ui::line(24,213,375,213,m::branch()); ui::text(24,224,"installed",m::ghost());
    sample_label(name,sizeof name,f.valid?f.installed:"no sample target",46,true); ui::text(96,224,name,m::ink());
}
