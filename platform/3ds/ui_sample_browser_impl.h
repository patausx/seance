// Included only by ui_sample_library.cpp, inside ui3ds. No standalone exports.
namespace {
bool browser_pending_use = false, browser_audition_owned = false;
int browser_delete_slot = -1;
uint32_t browser_delete_generation = 0;
constexpr int browser_sources[] = {kLibrary, kFavorites, kProject, kRecordings};
const char* browser_source_name(int mode) {
    switch (mode) {
        case kProject: return "project";
        case kFavorites: return "favorites";
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
} // namespace

void sample_browser_stop_preview() {
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
    sample_browser_stop_preview();
    browser_clear_wave(); browser_message("");
    g_sample_selected = index;
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
    if (browser_menu) { browser_menu = 0; return true; }
    sample_browser_stop_preview();
    if ((g_sample_browser_mode == kLibrary || g_sample_browser_mode == kRecordings) && g_sample_relative_dir[0]) {
        sample_browser_parent(); return true;
    }
    return false;
}

static void browser_toggle_favorite(int index) {
    if (index < 0 || index >= g_sample_file_count) return;
    browser_store_draft = browser_store;
    if (!sample_browser_store::toggle(browser_store_draft, browser_entries[index].path, g_sample_file_is_dir[index])) {
        browser_message(browser_store.count == sample_browser_store::kCapacity ? "favorites full (64)" : "no saved file for this sound"); return;
    }
    if (!sample_browser_store::save(browser_store_path, browser_store_draft)) {
        browser_message("could not save favorites"); return;
    }
    browser_store = browser_store_draft;
    browser_message("");
    if (g_sample_browser_mode == kFavorites) { browser_remember(); browser_rebuild(); }
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
    if (active || (browser_pending && browser_pending_play)) sample_browser_stop_preview();
    else if (browser_selected_valid()) browser_select(g_sample_selected, true);
}
static void browser_request_use() {
    if (!browser_selected_valid()) return;
    if (g_sample_file_is_dir[g_sample_selected]) { sample_browser_enter(g_sample_selected); return; }
    if (g_sample_browser_mode == kProject) { sample_browser_assign_selected_slot(); return; }
    sample_browser_stop_preview();
    browser_pending = 2; browser_pending_use = true;
    browser_message("loading sound...");
}
static bool browser_can_remove() {
    if (!browser_owner_current() || g_sample_browser_mode != kProject || !browser_selected_valid()) return false;
    platform::AudioOut::Guard guard(*g_audio);
    const auto& entry = browser_entries[g_sample_selected];
    if (!browser_slot_current(entry) || !sample_slot_frames(entry.slot)) return false;
    const int references = sample_slot_ref_count(entry.slot, nullptr);
    Unit* owner = edited_unit();
    return references == 0 || (references == 1 && sample_slot_for_unit(owner) == entry.slot && owner->kind != UnitKind::Recorder);
}
static void browser_menu_open(int menu) {
    sample_browser_stop_preview();
    sample_browser_reset_motion();
    browser_menu = menu; browser_menu_selected = 0;
    if (menu == 1) for (int i = 0; i < 4; ++i) if (browser_sources[i] == g_sample_browser_mode) browser_menu_selected = i;
}
static void browser_menu_choose(int row) {
    if (browser_menu == 1 && row >= 0 && row < 4) { browser_switch_source(browser_sources[row]); return; }
    if (browser_menu == 2) {
        if (row != 0) { browser_menu = 0; return; }
        if (!browser_can_remove()) { browser_message("sound is shared or busy"); browser_menu = 0; return; }
        const auto& entry = browser_entries[g_sample_selected];
        browser_delete_slot = entry.slot; browser_delete_generation = entry.generation;
        browser_menu = 3; browser_menu_selected = 1; // destructive confirmation defaults to cancel
        return;
    }
    if (browser_menu == 3) {
        browser_menu = 0;
        if (row != 0) return;
        bool current = false;
        if (browser_owner_current() && browser_selected_valid()) {
            platform::AudioOut::Guard guard(*g_audio);
            const auto& entry = browser_entries[g_sample_selected];
            current = entry.slot == browser_delete_slot && entry.generation == browser_delete_generation && browser_slot_current(entry);
        }
        if (!current || !delete_sample_slot_for_owner(browser_delete_slot, edited_unit())) {
            browser_message("sound busy or changed"); return;
        }
        browser_remember(); browser_rebuild();
    }
}

// Positive control IDs are disjoint from rows/stars. Modal touches never reach
// the covered list. Commit on lift only if still inside the original target.
static int browser_touch_target(int x, int y) {
    if (browser_menu) {
        const int count = browser_menu == 1 ? 4 : 2;
        if (x >= 16 && x < 304 && y >= 56 && y < 56 + count * 28) return 20 + (y - 56) / 28;
        return 19; // outside closes the menu, not the browser
    }
    if (hit(x,y,262,0,58,32)) return 1;
    if (hit(x,y,8,32,100,24)) return 2;
    if (hit(x,y,112,32,198,24)) return 3;
    if (hit(x,y,8,202,108,24)) return 4;
    if (hit(x,y,128,202,80,24) && g_sample_browser_mode == kProject) return 5;
    if (hit(x,y,236,202,76,24)) return 6;
    if (hit(x,y,8,226,152,14)) return 7;
    if (hit(x,y,242,226,70,14)) return 8;
    if (y >= kListTop && y < kListBottom && x >= 8 && x < 310) {
        const int row = (y - kListTop + g_sample_scroll_q8 / 256) / kRowHeight;
        if (row < g_sample_file_count) return (x >= 282 ? 1000 : 100) + row;
    }
    return 0;
}
static void browser_commit_touch(int action) {
    if (browser_menu) { if (action >= 20) browser_menu_choose(action - 20); else browser_menu = 0; return; }
    if (action >= 1000) { browser_toggle_favorite(action - 1000); return; }
    if (action >= 100) {
        const int row = action - 100;
        if (g_sample_file_is_dir[row]) sample_browser_enter(row); else browser_select(row, true);
        return;
    }
    switch (action) {
        case 1: browser_close(); break;
        case 2: browser_menu_open(1); break;
        case 3:
            if ((g_sample_browser_mode == kLibrary || g_sample_browser_mode == kRecordings) && g_sample_relative_dir[0]) sample_browser_parent();
            break;
        case 4: browser_listen(); break;
        case 5: browser_menu_open(2); break;
        case 6: browser_request_use(); break;
        case 7: browser_toggle_auto(); break;
        case 8: if (!sample_browser_back()) browser_close(); break;
        default: break;
    }
}
void sample_browser_touch_down(int x, int y) {
    sample_browser_reset_motion();
    if (!browser_owner_current()) return;
    g_sample_touch_x = x; g_sample_touch_y = y; g_sample_touch_scroll_q8 = g_sample_scroll_q8;
    if (!browser_menu && x >= 310 && x < 320 && y >= kListTop && y < kListBottom && sample_browser_max_scroll_px()) {
        sample_browser_stop_preview();
        g_sample_scrollbar_drag = g_sample_touch_scrolling = true;
        sample_browser_scrollbar_to(y); scan_sample_file_page_metadata(); return;
    }
    browser_touch_action = browser_touch_target(x, y);
    browser_touch_inside = browser_touch_action != 0;
    g_sample_touch_list = !browser_menu && browser_touch_action >= 100;
}
void sample_browser_touch_hold(int x, int y) {
    if (g_sample_scrollbar_drag) { sample_browser_scrollbar_to(y); scan_sample_file_page_metadata(); return; }
    if (g_sample_touch_list) {
        const int dy = y - g_sample_touch_y, dx = x - g_sample_touch_x;
        if (!g_sample_touch_scrolling && (dy > 8 || dy < -8 || dx > 12 || dx < -12)) {
            g_sample_touch_scrolling = true; sample_browser_stop_preview();
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
}
void sample_browser_touch_release(bool commit) {
    if (!browser_touch_action && !g_sample_touch_list && !g_sample_scrollbar_drag) {
        if (!commit) g_sample_scroll_velocity_q8 = 0;
        return;
    }
    const int action = browser_touch_action;
    const bool activate = commit && browser_touch_inside && !g_sample_touch_scrolling && browser_owner_current();
    const int velocity = commit && g_sample_touch_scrolling && !g_sample_scrollbar_drag ? g_sample_scroll_velocity_q8 : 0;
    sample_browser_reset_motion();
    g_sample_scroll_velocity_q8 = velocity;
    if (activate) browser_commit_touch(action);
}

bool sample_browser_buttons(uint32_t down) {
    if (!(down & (KEY_A|KEY_B|KEY_X|KEY_Y|KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT|KEY_L|KEY_R))) return false;
    sample_browser_touch_release(false);
    if (!browser_owner_current()) { sample_browser_stop_preview(); browser_message("sample target changed"); return true; }
    if (down & KEY_B) { if (!sample_browser_back()) browser_close(); return true; }
    if (browser_menu) {
        const int count = browser_menu == 1 ? 4 : 2;
        if (down & KEY_UP) browser_menu_selected = (browser_menu_selected + count - 1) % count;
        if (down & KEY_DOWN) browser_menu_selected = (browser_menu_selected + 1) % count;
        if (down & KEY_A) browser_menu_choose(browser_menu_selected);
        return true;
    }
    if (down & (KEY_L|KEY_R)) { browser_menu_open(1); return true; }
    if (down & KEY_Y) { browser_listen(); return true; }
    if (down & KEY_X) { browser_toggle_favorite(g_sample_selected); return true; }
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
    if (g_view != View::Chains || g_overlay != Overlay::SampleBrowser || nav_is_open() || !browser_owner_current()) {
        if (browser_audition_owned || browser_pending) sample_browser_stop_preview();
        sample_browser_reset_motion(); return;
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
    bool directory = false, favorite = false, valid = false, installed = false;
};
struct SampleBrowserFrame {
    BrowserRowView rows[kRows + 1];
    SampleWaveView wave;
    char directory[128] = {}, name[SAMPLE_NAME_LEN] = {}, installed[SAMPLE_NAME_LEN] = {}, notice[48] = {};
    int mode = 0, chain = 0, assigned = -1, selected = -1, count = 0, scroll = 0, maximum = 0;
    int menu = 0, menu_selected = 0, references = 0;
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
void browser_star(int x, int y, bool favorite) {
    const int dx[] = {0,1,5,2,3,0,-3,-2,-5,-1,0};
    const int dy[] = {-5,-2,-2,1,4,2,4,1,-2,-2,-5};
    const auto color = favorite ? menu_style::spot() : menu_style::branch();
    for (int i = 0; i < 10; ++i) ui::line(x+dx[i],y+dy[i],x+dx[i+1],y+dy[i+1],color);
    if (favorite) ui::rect(x-1,y-1,3,3,color);
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
        row.favorite = sample_browser_store::find(browser_store, browser_entries[i].path, row.directory) >= 0;
        std::snprintf(row.name, sizeof row.name, "%s", g_sample_files[i]);
    }
    if (browser_selected_valid()) {
        std::snprintf(f.name, sizeof f.name, "%s", g_sample_files[f.selected]);
        f.directory_selected = g_sample_file_is_dir[f.selected];
        const auto& e = browser_entries[f.selected];
        if (e.slot < 0 && !f.directory_selected && !std::strcmp(e.path, g_sample_file_wave_path)) f.wave = g_sample_file_wave;
    }
    if (!browser_owner_current()) return;
    // Metadata, preview cursor and at most one bounded 5632-read PCM projection.
    // No allocations, decoder, storage I/O or GPU submissions in this guard.
    platform::AudioOut::Guard guard(*g_audio);
    auto& bank = trackr::synth::SampleBank::instance();
    Unit* owner = edited_unit();
    f.valid = true; f.kind = owner->kind; f.assigned = sample_slot_for_unit(owner);
    if (f.assigned >= 0 && f.assigned < trackr::synth::SAMPLE_BANK_SIZE)
        std::snprintf(f.installed, sizeof f.installed, "%s", sample_slot_frames(f.assigned) ? sample_slot_short_name(bank.provenance(f.assigned)) : "empty");
    for (auto& row : f.rows) if (row.index >= 0 && f.mode == kProject) row.installed = browser_entries[row.index].slot == f.assigned;
    bool wave_matches = f.wave.frames > 0;
    if (browser_selected_valid() && f.mode == kProject) {
        const auto& e = browser_entries[f.selected];
        if (browser_slot_current(e)) {
            f.references = sample_slot_ref_count(e.slot, nullptr);
            sample_wave_capture(bank.slot(e.slot), f.wave);
            f.can_delete = f.wave.frames && (f.references == 0 || (f.references == 1 && e.slot == f.assigned && owner->kind != UnitKind::Recorder));
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
        if (selected) ui::rect(15,top+4,2,16,m::spot());
        if (selected && f.playing) { ui::line(26,y,31,y+3,m::spot()); ui::line(31,y+3,26,y+7,m::spot()); }
        else if (row.directory) ui::text(25,y,">",m::ghost());
        char duration[24], name[64];
        if (row.directory) std::snprintf(duration,sizeof duration,"open >");
        else sample_duration(duration,sizeof duration,row.frames,row.valid?row.rate:0);
        const int duration_x = 276-ui::text_w(duration);
        sample_label(name,sizeof name,row.name,(duration_x-46)/6,true);
        ui::text(40,y,name,selected?m::spot():m::ink());
        if (row.installed) ui::rect(40,y+9,ui::text_w(name),1,m::spot());
        ui::text(duration_x,y,duration,m::ghost()); browser_star(297,y+3,row.favorite);
    }
    // Fractional rows are native, then masked by the exact frozen paper.
    m::paper(320,0,kListTop); m::paper(320,kListBottom,240);
    if (!f.count) {
        ui::text(24,80,f.mode==kProject?"no project sounds":f.mode==kFavorites?"no favorites yet":"no wav files",m::ink());
        ui::text(24,100,f.mode==kFavorites?"star a file or folder in library":"choose another source above",m::ghost());
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
    if (f.mode==kProject || f.mode==kFavorites) std::snprintf(label,sizeof label,"%d sounds",f.count);
    else if (f.directory[0]) {
        const size_t length=std::strlen(f.directory);
        std::snprintf(label,sizeof label,"%s%.27s",length>27?"..":"",f.directory+(length>27?length-25:0));
    } else std::snprintf(label,sizeof label,"%s / root",f.mode==kRecordings?"recordings":"samples");
    ui::text(304-ui::text_w(label),41,label,m::ghost());
    ui::line(16,201,303,201,m::branch());
    ui::text(16,210,f.playing?"y stop":"y listen",f.selected>=0&&!f.directory_selected?m::ink():m::ghost());
    if (f.mode==kProject) ui::text(144,210,"more",m::ghost());
    const char* use=f.directory_selected?"a open >":"a use >";
    ui::text(304-ui::text_w(use),210,use,f.selected>=0?m::spot():m::ghost());
    ui::line(262,221,303,221,m::spot());
    ui::text(16,231,f.automatic?"auto preview on":"auto preview off",m::ghost());
    ui::text(268,231,"b back",m::ghost());
    if (f.menu) {
        m::paper_rect(8,54,304,144);
        const int count=f.menu==1?4:2;
        for (int i=0;i<count;++i) {
            const char* item=f.menu==1?browser_source_name(browser_sources[i]):i==1?"cancel":f.menu==2?"remove from project":"confirm remove sound";
            if (i==f.menu_selected) m::marker(24,64+i*28,m::spot());
            ui::text(40,60+i*28,item,i==f.menu_selected?m::spot():m::ink());
        }
        if (f.menu==3) ui::text(24,132,"sd file is kept",m::ghost());
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
    sample_label(name,sizeof name,f.selected>=0?f.name:"select a sample below",29,true);
    ui::text(24,43,name,m::ink(),2);
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
