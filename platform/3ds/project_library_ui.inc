// Included only by main.cpp (and the exact-main host harness), inside ui3ds.
// No engine/PCM reads, clocks, filesystem or mutable UI reads in either draw.
enum class PatchPage { List, More, Destination, Name };
static constexpr int kPatchRowsPerPage = 6;
enum PatchKey { kPatchKeyClear = 40, kPatchKeySpace, kPatchKeyDelete, kPatchKeyDone };
static PatchPage g_patch_page = PatchPage::List;
static int g_patch_more_cursor = 0, g_patch_key_cursor = 0;
static int g_patch_name_slot = -1;
static bool g_patch_copy = false, g_patch_rename = false;
static bool g_patch_replace_approved = false, g_patch_name_file_saved = false;
static char g_patch_draft[PROJECT_NAME_BYTES] = {};
static constexpr char kPatchKeys[] = "1234567890qwertyuiopasdfghjkl-zxcvbnm_.'";
static_assert(sizeof(kPatchKeys) == kPatchKeyClear + 1, "four rows of ten keys");

struct ProjectTouch {
    Overlay overlay = Overlay::None;
    PatchPage page = PatchPage::List;
    int target = -1, x = 0, y = 0, slot = -1, name_slot = -1;
    int replace = -1, remove = -1;
    uint32_t revision = 0;
    PendingDestructiveAction pending;
};
static ProjectTouch g_project_touch;
static void project_overlay_cancel_input() { g_project_touch = {}; }

struct ProjectOverlayFrame {
    struct Row { int slot = -1; bool exists = false, valid = false, checked = false; char name[PROJECT_NAME_BYTES] = {}; } rows[kPatchRowsPerPage];
    bool active = false, discard = false, dirty = false, exists = false, valid = false;
    bool copying = false, checked = false, catalog_failed = false;
    int current = -1, selected = 0, first = 0, row_count = 0, saved_count = 0;
    int replace = -1, remove = -1, more_cursor = 0, key_cursor = 0;
    PatchPage page = PatchPage::List;
    PendingDestructiveAction pending;
    char current_name[PROJECT_NAME_BYTES] = {}, selected_name[PROJECT_NAME_BYTES] = {};
    char draft[PROJECT_NAME_BYTES] = {}, saved_at[32] = {}, notice[48] = {};
};
static ProjectOverlayFrame g_project_overlay_frame;
static_assert(sizeof(ProjectOverlayFrame) <= 448, "six visible metadata rows only");

static void patch_library_notice(const char* text) {
    std::snprintf(g_patch_notice, sizeof g_patch_notice, "%s", text);
    g_save_flash = 180;
}

static void patch_library_inspect_selection() {
    // At most one previously unseen file, never a page/all-slot scan. Empty
    // slots already known from the directory need no SD lookup for browsing.
    if (project_slot_valid(g_patch_slot) && !g_patch_checked[g_patch_slot])
        refresh_patch_slot(g_patch_slot);
}

static void patch_library_reset() {
    project_overlay_cancel_input();
    g_patch_page = PatchPage::List;
    g_patch_confirm_slot = g_patch_delete_confirm_slot = -1;
    g_patch_name_slot = -1; g_patch_more_cursor = 0;
    g_patch_copy = g_patch_rename = g_patch_replace_approved = g_patch_name_file_saved = false;
    if (!project_slot_valid(g_patch_slot)) g_patch_slot = 0;
    scan_patch_slots(); // cached after boot, including failure
    patch_library_inspect_selection();
}

static void patch_library_back() {
    project_overlay_cancel_input();
    g_patch_confirm_slot = g_patch_delete_confirm_slot = -1;
    if (g_patch_page == PatchPage::Name && !g_patch_name_file_saved) {
        g_patch_page = g_patch_rename ? PatchPage::More : PatchPage::Destination;
        return;
    }
    if (g_patch_page != PatchPage::List) {
        g_patch_page = PatchPage::List;
        g_project_session.cancel_pending();
        return;
    }
    g_project_session.cancel_pending();
    g_overlay = Overlay::None;
}

static void patch_library_move(int amount) {
    g_patch_slot += amount;
    if (g_patch_slot < 0) g_patch_slot = 0;
    if (g_patch_slot >= PROJECT_SLOTS) g_patch_slot = PROJECT_SLOTS - 1;
    patch_library_inspect_selection();
    g_patch_confirm_slot = g_patch_delete_confirm_slot = -1;
}

static void patch_library_destination(bool copy) {
    g_patch_page = PatchPage::Destination; g_patch_copy = copy;
    g_patch_confirm_slot = g_patch_delete_confirm_slot = -1;
    for (int slot = 0; slot < PROJECT_SLOTS; ++slot) {
        if (!g_patch_exists[slot]) { g_patch_slot = slot; return; }
    }
    patch_library_notice(g_patch_catalog_failed ? "library unavailable / sd error" : "library full / choose a place to replace");
}

static void patch_library_name(bool rename) {
    if (!project_slot_valid(g_patch_slot)) return;
    g_patch_name_slot = g_patch_slot;
    g_patch_rename = rename; g_patch_name_file_saved = false;
    g_patch_replace_approved = g_patch_confirm_slot == g_patch_slot;
    const int source = rename ? g_patch_slot : g_project_session.active_slot();
    if (project_slot_valid(source))
        project_name_label(g_project_names, source, g_patch_draft, sizeof g_patch_draft);
    else std::snprintf(g_patch_draft, sizeof g_patch_draft, "new project");
    g_patch_key_cursor = kPatchKeyClear; // naming a new version needs no 23x backspace
    g_patch_confirm_slot = g_patch_delete_confirm_slot = -1;
    g_save_flash = 0;
    g_patch_page = PatchPage::Name;
}

static void patch_library_name_done() {
    const int slot = g_patch_name_slot;
    if (!project_slot_valid(slot)) return;
    ProjectNames proposed = g_project_names;
    if (!project_name_set(proposed, slot, g_patch_draft)) {
        patch_library_notice("enter a name / 1-23 letters or symbols"); return;
    }
    if (!g_patch_rename && !g_patch_name_file_saved) {
        // An FTP/SD change since opening the destination must not turn a free
        // slot into an unconfirmed overwrite while its name is being entered.
        refresh_patch_slot(slot);
        if (g_patch_exists[slot] && !g_patch_replace_approved) {
            g_patch_slot = slot; g_patch_page = PatchPage::Destination;
            g_patch_confirm_slot = slot;
            patch_library_notice("slot now occupied / confirm replace"); return;
        }
        if (!save_current_patch(slot)) { patch_browser_result(false, PatchAction::Save, slot); return; }
        g_patch_name_file_saved = true;
    }
    if (!save_project_names(proposed)) {
        if (g_patch_name_file_saved) refresh_patch_slot(slot);
        patch_library_notice(g_patch_rename ? "name not saved / retry done" : "project saved / name failed / retry done");
        return; // after partial success retry only metadata, never the patch again
    }
    const bool renamed = g_patch_rename;
    g_patch_page = PatchPage::List; g_patch_slot = slot;
    patch_browser_result(true, renamed ? PatchAction::Rename : PatchAction::Save, slot);
    if (!renamed && g_project_session.has_pending())
        execute_destructive(g_project_session.take_pending());
}

static void patch_library_key(int key) {
    const size_t len = std::strlen(g_patch_draft);
    if (key == kPatchKeyClear) g_patch_draft[0] = 0;
    else if (key == kPatchKeyDelete) { if (len) g_patch_draft[len - 1] = 0; }
    else if (key == kPatchKeyDone) { patch_library_name_done(); return; }
    else if (len < sizeof g_patch_draft - 1 && (key == kPatchKeySpace || (key >= 0 && key < kPatchKeyClear))) {
        g_patch_draft[len] = key == kPatchKeySpace ? ' ' : kPatchKeys[key];
        g_patch_draft[len + 1] = 0;
    }
}

static void patch_library_save() {
    if (g_patch_page == PatchPage::Destination) {
        refresh_patch_slot(g_patch_slot);
        if (g_patch_exists[g_patch_slot] && g_patch_confirm_slot != g_patch_slot) {
            g_patch_confirm_slot = g_patch_slot; return;
        }
        patch_library_name(false); return;
    }
    const int slot = g_project_session.active_slot();
    if (!project_slot_valid(slot)) { patch_library_destination(false); return; }
    // SAVE always targets the actual session, independent of the highlighted row.
    refresh_patch_slot(slot); // cached absence is never overwrite authorization
    if (g_patch_exists[slot] && g_patch_confirm_slot != slot) {
        g_patch_confirm_slot = slot; g_patch_delete_confirm_slot = -1; return;
    }
    const bool ok = save_current_patch(slot);
    g_patch_confirm_slot = -1;
    patch_browser_result(ok, PatchAction::Save, slot);
    if (ok && g_project_session.has_pending()) execute_destructive(g_project_session.take_pending());
}

static void patch_library_more(int item) {
    if (item == 3) { patch_library_back(); return; }
    if (item == 0) {
        g_patch_confirm_slot = g_patch_delete_confirm_slot = -1;
        request_destructive(DestructiveAction::NewProject); return;
    }
    if (!g_patch_exists[g_patch_slot]) return;
    if (item == 1) { patch_library_name(true); return; }
    if (g_patch_delete_confirm_slot != g_patch_slot) {
        g_patch_delete_confirm_slot = g_patch_slot; g_patch_confirm_slot = -1; return;
    }
    const int slot = g_patch_slot;
    char path[96]; patch_path(slot, path, sizeof path);
    const bool ok = remove(path) == 0;
    if (ok) {
        if (g_project_session.active_slot() == slot) g_project_session.detach_active_slot();
        start_store_forget(g_start_store, slot);
        g_start_store_write_failed = !start_store_save(START_MENU_PATH, g_start_store);
        ProjectNames proposed = g_project_names;
        project_name_clear(proposed, slot);
        // A failed metadata cleanup cannot resurrect a missing project. New
        // saves always supply a fresh name; scan never shows names for empty slots.
        if (!save_project_names(proposed)) project_name_clear(g_project_names, slot);
    }
    g_patch_delete_confirm_slot = -1;
    patch_browser_result(ok, PatchAction::Delete, slot);
    if (ok) g_patch_page = PatchPage::List;
}

static void confirm_discard_activate(int tx, int ty) {
    if (hit(tx, ty, 20, 154, 86, 34)) {
        patch_library_reset();
        g_overlay = Overlay::PatchBrowser;
        patch_library_save();
    } else if (hit(tx, ty, 117, 154, 86, 34)) {
        execute_destructive(g_project_session.take_pending());
    } else if (hit(tx, ty, 214, 154, 86, 34)) {
        g_project_session.cancel_pending(); g_overlay = Overlay::None;
    }
}

static void patch_library_open() {
    if (!g_patch_exists[g_patch_slot]) {
        patch_browser_result(false, PatchAction::Load, g_patch_slot);
        return;
    }
    g_patch_confirm_slot = g_patch_delete_confirm_slot = -1;
    request_destructive(DestructiveAction::LoadProject, g_patch_slot);
}

static void patch_browser_activate(int tx, int ty) {
    if (g_patch_page == PatchPage::Name) {
        if (hit(tx, ty, 250, 10, 54, 28)) { patch_library_back(); return; }
        for (int i = 0; i < kPatchKeyClear; ++i) if (hit(tx, ty, 20 + i % 10 * 28, 80 + i / 10 * 24, 28, 24)) {
            g_patch_key_cursor = i; patch_library_key(i); return;
        }
        for (int i = 0; i < 4; ++i) if (hit(tx, ty, 14 + i * 74, 202, 70, 30)) {
            g_patch_key_cursor = kPatchKeyClear + i; patch_library_key(kPatchKeyClear + i); return;
        }
        return;
    }
    if (g_patch_page == PatchPage::More) {
        if (hit(tx, ty, 250, 10, 54, 28)) { patch_library_back(); return; }
        for (int i = 0; i < 4; ++i) if (hit(tx, ty, 14, 66 + i * 28, 288, 28)) {
            g_patch_more_cursor = i; patch_library_more(i); return;
        }
        return;
    }
    const int first = g_patch_slot / kPatchRowsPerPage * kPatchRowsPerPage;
    for (int row = 0; row < kPatchRowsPerPage && first + row < PROJECT_SLOTS; ++row) {
        if (hit(tx, ty, 14, 44 + row * 22, 288, 22)) {
            g_patch_slot = first + row;
            patch_library_inspect_selection();
            g_patch_confirm_slot = g_patch_delete_confirm_slot = -1; return;
        }
    }
    if (hit(tx, ty, 14, 178, 64, 22)) { patch_library_move(-kPatchRowsPerPage); return; }
    if (hit(tx, ty, 242, 178, 64, 22)) { patch_library_move(kPatchRowsPerPage); return; }
    if (g_patch_page == PatchPage::Destination) {
        if (hit(tx, ty, 14, 202, 76, 30)) patch_library_back();
        else if (hit(tx, ty, 98, 202, 110, 30)) patch_library_save();
        return;
    }
    if (hit(tx, ty, 250, 10, 54, 28)) {
        g_patch_page = PatchPage::More; g_patch_more_cursor = 0;
        g_patch_confirm_slot = g_patch_delete_confirm_slot = -1; return;
    }
    if (hit(tx, ty, 14, 202, 76, 30)) patch_library_open();
    else if (hit(tx, ty, 98, 202, 86, 30)) patch_library_save();
    else if (hit(tx, ty, 192, 202, 110, 30)) patch_library_destination(true);
}

// Stable logical targets, not a second press at the release coordinates. Leaving
// the original hit area permanently cancels this gesture, even after re-entry.
static int project_touch_target(int x, int y) {
    if (g_overlay == Overlay::ConfirmDiscard) {
        for (int i = 0; i < 3; ++i) if (hit(x, y, 20 + i * 97, 154, 86, 34)) return i;
        return -1;
    }
    if (g_overlay != Overlay::PatchBrowser) return -1;
    if (g_patch_page == PatchPage::Name) {
        if (hit(x, y, 250, 10, 54, 28)) return 1;
        for (int i = 0; i < kPatchKeyClear; ++i)
            if (hit(x, y, 20 + i % 10 * 28, 80 + i / 10 * 24, 28, 24)) return 10 + i;
        for (int i = 0; i < 4; ++i) if (hit(x, y, 14 + i * 74, 202, 70, 30)) return 50 + i;
        return -1;
    }
    if (g_patch_page == PatchPage::More) {
        if (hit(x, y, 250, 10, 54, 28)) return 1;
        for (int i = 0; i < 4; ++i) if (hit(x, y, 14, 66 + i * 28, 288, 28)) return 10 + i;
        return -1;
    }
    const int first = g_patch_slot / kPatchRowsPerPage * kPatchRowsPerPage;
    for (int row = 0; row < kPatchRowsPerPage && first + row < PROJECT_SLOTS; ++row)
        if (hit(x, y, 14, 44 + row * 22, 288, 22)) return 10 + row;
    if (hit(x, y, 14, 178, 64, 22)) return 30;
    if (hit(x, y, 242, 178, 64, 22)) return 31;
    if (hit(x, y, 14, 202, 76, 30)) return 40;
    if (hit(x, y, 98, 202, g_patch_page == PatchPage::Destination ? 110 : 86, 30)) return 41;
    if (g_patch_page == PatchPage::List) {
        if (hit(x, y, 250, 10, 54, 28)) return 1;
        if (hit(x, y, 192, 202, 110, 30)) return 42;
    }
    return -1;
}
static bool project_touch_current() {
    const auto& t = g_project_touch;
    const auto pending = g_project_session.pending();
    return t.target >= 0 && t.overlay == g_overlay && t.page == g_patch_page &&
        t.slot == g_patch_slot && t.name_slot == g_patch_name_slot &&
        t.replace == g_patch_confirm_slot && t.remove == g_patch_delete_confirm_slot &&
        t.revision == g_project_session.revision() &&
        t.pending.action == pending.action && t.pending.slot == pending.slot;
}
static void project_overlay_touch_down(int x, int y) {
    project_overlay_cancel_input();
    const int target = project_touch_target(x, y);
    if (target < 0) return;
    auto& t = g_project_touch;
    t.overlay = g_overlay; t.page = g_patch_page; t.target = target; t.x = x; t.y = y;
    t.slot = g_patch_slot; t.name_slot = g_patch_name_slot;
    t.replace = g_patch_confirm_slot; t.remove = g_patch_delete_confirm_slot;
    t.revision = g_project_session.revision(); t.pending = g_project_session.pending();
}
void patch_browser_touch_down(int tx, int ty) { project_overlay_touch_down(tx, ty); }
void confirm_discard_touch_down(int tx, int ty) { project_overlay_touch_down(tx, ty); }
static void project_overlay_touch_hold(int x, int y) {
    if (!project_touch_current() || project_touch_target(x, y) != g_project_touch.target)
        project_overlay_cancel_input();
}
static void project_overlay_touch_release(bool commit) {
    const auto saved = g_project_touch;
    const bool valid = commit && project_touch_current();
    project_overlay_cancel_input(); // action may replace the project or open a different page
    if (!valid) return;
    if (saved.overlay == Overlay::ConfirmDiscard) confirm_discard_activate(saved.x, saved.y);
    else patch_browser_activate(saved.x, saved.y);
}

void patch_browser_buttons(u32 down) {
    if (down) project_overlay_cancel_input();
    if (g_patch_page == PatchPage::Name) {
        if (down & KEY_X) { patch_library_key(kPatchKeyDelete); return; }
        if (down & KEY_Y) { patch_library_key(kPatchKeySpace); return; }
        int& key = g_patch_key_cursor;
        if (down & KEY_LEFT) key = key >= kPatchKeyClear
            ? kPatchKeyClear + (key - kPatchKeyClear + 3) % 4 : key / 10 * 10 + (key + 9) % 10;
        else if (down & KEY_RIGHT) key = key >= kPatchKeyClear
            ? kPatchKeyClear + (key - kPatchKeyClear + 1) % 4 : key / 10 * 10 + (key + 1) % 10;
        else if (down & KEY_UP) {
            if (key >= kPatchKeyClear) key = 30 + (key - kPatchKeyClear) * 3;
            else if (key >= 10) key -= 10;
            else key = kPatchKeyClear + key / 3;
        } else if (down & KEY_DOWN) {
            if (key >= kPatchKeyClear) key = (key - kPatchKeyClear) * 3;
            else if (key < 30) key += 10;
            else key = kPatchKeyClear + (key - 30) / 3;
        }
        else if (down & KEY_A) patch_library_key(key);
        return;
    }
    if (g_patch_page == PatchPage::More) {
        if (down & KEY_UP) g_patch_more_cursor = (g_patch_more_cursor + 3) % 4;
        else if (down & KEY_DOWN) g_patch_more_cursor = (g_patch_more_cursor + 1) % 4;
        else if (down & KEY_A) patch_library_more(g_patch_more_cursor);
        if (down & (KEY_UP | KEY_DOWN)) g_patch_delete_confirm_slot = -1;
        return;
    }
    if (down & KEY_UP) { patch_library_move(-1); return; }
    if (down & KEY_DOWN) { patch_library_move(1); return; }
    if (down & (KEY_LEFT | KEY_L)) { patch_library_move(-kPatchRowsPerPage); return; }
    if (down & (KEY_RIGHT | KEY_R)) { patch_library_move(kPatchRowsPerPage); return; }
    if (down & KEY_A) {
        if (g_patch_page == PatchPage::Destination) patch_library_save();
        else patch_library_open();
        return;
    }
    if (down & KEY_X) patch_library_save();
}

void patch_browser_route_buttons(u32& down, bool& skip_touch, Overlay input_overlay) {
    if (input_overlay != Overlay::PatchBrowser && g_overlay != Overlay::PatchBrowser) return;
    if (down & ~(KEY_TOUCH | KEY_ZL)) project_overlay_cancel_input();
    if (input_overlay == Overlay::PatchBrowser && g_overlay == Overlay::PatchBrowser &&
        !(down & (KEY_START | KEY_B | KEY_SELECT))) patch_browser_buttons(down);
    if (down & ~(KEY_TOUCH | KEY_ZL)) {
        skip_touch = true; down &= KEY_TOUCH | KEY_ZL;
    }
}

static void project_overlay_capture() {
    auto& f = g_project_overlay_frame; f = {};
    f.active = g_overlay == Overlay::PatchBrowser || g_overlay == Overlay::ConfirmDiscard;
    if (!f.active) { project_overlay_cancel_input(); return; }
    f.discard = g_overlay == Overlay::ConfirmDiscard;
    f.current = g_project_session.active_slot(); f.dirty = g_project_session.dirty();
    f.selected = project_slot_valid(g_patch_slot) ? g_patch_slot : 0;
    f.exists = g_patch_exists[f.selected]; f.valid = g_patch_valid[f.selected];
    f.checked = g_patch_checked[f.selected]; f.catalog_failed = g_patch_catalog_failed;
    f.replace = g_patch_confirm_slot; f.remove = g_patch_delete_confirm_slot;
    f.pending = g_project_session.pending(); f.page = g_patch_page;
    f.copying = g_patch_copy;
    f.more_cursor = g_patch_more_cursor; f.key_cursor = g_patch_key_cursor;
    std::snprintf(f.draft, sizeof f.draft, "%s", g_patch_draft);
    project_name_label(g_project_names, f.current, f.current_name, sizeof f.current_name);
    if (f.current < 0) std::snprintf(f.current_name, sizeof f.current_name, "new project");
    if (f.exists) project_name_label(g_project_names, f.selected, f.selected_name, sizeof f.selected_name);
    else std::snprintf(f.selected_name, sizeof f.selected_name, "empty");
    f.first = f.selected / kPatchRowsPerPage * kPatchRowsPerPage;
    for (int i = 0; i < PROJECT_SLOTS; ++i) if (g_patch_exists[i]) ++f.saved_count;
    for (int i = f.first; i < f.first + kPatchRowsPerPage && i < PROJECT_SLOTS; ++i) {
        auto& row = f.rows[f.row_count++]; row.slot = i;
        row.exists = g_patch_exists[i]; row.valid = g_patch_valid[i]; row.checked = g_patch_checked[i];
        if (row.exists) project_name_label(g_project_names, i, row.name, sizeof row.name);
        else std::snprintf(row.name, sizeof row.name, "empty");
    }
    const time_t stamp = (time_t)g_patch_saved_at[f.selected];
    const auto* date = stamp > 0 ? std::localtime(&stamp) : nullptr;
    if (!date || !std::strftime(f.saved_at, sizeof f.saved_at, "%d %b %Y / %H:%M", date))
        std::snprintf(f.saved_at, sizeof f.saved_at, "--");
    for (char* p = f.saved_at; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
    if (g_save_flash > 0) std::snprintf(f.notice, sizeof f.notice, "%s", g_patch_notice);
}

static void project_overlay_intent(const ProjectOverlayFrame& f, char* text, size_t size) {
    if (f.pending.action == DestructiveAction::LoadProject)
        std::snprintf(text, size, "next / load %03d", f.pending.slot + 1);
    else std::snprintf(text, size, "next / %s", f.pending.action == DestructiveAction::ExitApp ? "exit to hbmenu" : "new project");
}

void confirm_discard_draw() {
    const auto& f = g_project_overlay_frame;
    namespace m = menu_style;
    m::paper(320); m::page_heading(page_layout::Title::UnsavedChanges, m::ink());
    char text[48];
    ui::text(20, 60, f.current_name, m::spot());
    project_overlay_intent(f, text, sizeof text);
    ui::rect(30, 88, 1, 25, m::branch()); ui::rect(30, 88, 12, 1, m::branch());
    ui::text(50, 84, text, m::ink());
    ui::rect(30, 112, 12, 1, m::branch());
    ui::text(50, 108, "save first or lose changes", m::ghost());
    if (f.notice[0]) ui::text(20, 136, f.notice, m::spot());
    ui::text(51, 167, "save", m::spot()); ui::rect(49, 181, 28, 1, m::spot());
    ui::text(139, 167, "discard", m::ghost()); ui::text(239, 167, "cancel", m::ink());
    m::footer(320, "a / b cancel", "x discard");
}

void patch_browser_draw() {
    const auto& f = g_project_overlay_frame;
    namespace m = menu_style;
    m::paper(320); m::page_heading(page_layout::Title::Patches, m::ink());
    char text[48];
    if (f.page == PatchPage::Name) {
        ui::text(174, 21, "name", m::ghost()); ui::text(276, 21, "back", m::ink());
        ui::text(20, 52, f.draft, m::ink());
        ui::rect(20, 65, 228, 1, m::branch());
        ui::rect(20 + ui::text_w(f.draft), 62, 5, 1, m::spot());
        std::snprintf(text, sizeof text, "%u/23", (unsigned)std::strlen(f.draft));
        ui::text(300 - ui::text_w(text), 52, text, m::ghost());
        for (int i = 0; i < kPatchKeyClear; ++i) {
            const int x = 30 + i % 10 * 28, y = 88 + i / 10 * 24;
            const char key[2] = {kPatchKeys[i], 0};
            ui::text(x, y, key, i == f.key_cursor ? m::spot() : m::ink());
            if (i == f.key_cursor) ui::rect(x - 3, y + 11, 12, 1, m::spot());
        }
        const char* labels[] = {"clear", "space", "del", "done"};
        for (int i = 0; i < 4; ++i) {
            const int x = 14 + i * 74 + (70 - ui::text_w(labels[i])) / 2;
            ui::text(x, 213, labels[i], i + kPatchKeyClear == f.key_cursor ? m::spot() : m::ink());
            if (i + kPatchKeyClear == f.key_cursor) ui::rect(x - 3, 226, ui::text_w(labels[i]) + 6, 1, m::spot());
        }
        ui::text(20, 184, f.notice[0] ? f.notice : "x delete / y space / b back", m::ghost());
        return;
    }
    if (f.page == PatchPage::More) {
        ui::text(276, 21, "back", m::ink());
        std::snprintf(text, sizeof text, "%03d / %.23s", f.selected + 1, f.selected_name);
        ui::text(20, 50, text, m::ghost());
        const char* labels[] = {"new project", "rename", "delete", "back"};
        ui::rect(20, 80, 1, 85, m::branch());
        for (int i = 0; i < 4; ++i) {
            const int y = 76 + i * 28;
            const u32 color = i == f.more_cursor ? m::spot() : m::branch();
            ui::rect(20, y + 4, 17, 1, color);
            if (i == f.more_cursor) m::marker(36, y + 4, color);
            ui::text(48, y, labels[i], !f.exists && (i == 1 || i == 2) ? m::ghost() : i == f.more_cursor ? m::spot() : m::ink());
        }
        if (f.remove >= 0) {
            std::snprintf(text, sizeof text, "delete %03d? / delete again", f.remove + 1);
            ui::text(20, 190, text, m::spot());
        } else if (f.notice[0]) ui::text(20, 190, f.notice, m::spot());
        m::footer(320, "a choose", "b back"); return;
    }
    const bool destination = f.page == PatchPage::Destination;
    if (destination) ui::text(210, 21, f.copying ? "save copy" : "save new", m::spot());
    else {
        if (f.catalog_failed) std::snprintf(text, sizeof text, "sd error");
        else std::snprintf(text, sizeof text, "%d saved", f.saved_count);
        ui::text(176, 21, text, m::ghost()); ui::text(264, 21, "more >", m::ink());
    }
    if (f.row_count) ui::rect(20, 56, 1, (f.row_count - 1) * 22 + 1, m::branch());
    for (int i = 0; i < f.row_count; ++i) {
        const auto& row = f.rows[i]; const int y = 52 + i * 22;
        const bool selected = row.slot == f.selected;
        const u32 color = selected ? m::spot() : m::branch();
        ui::rect(20, y + 4, 17, 1, color);
        if (selected) m::marker(36, y + 4, color);
        std::snprintf(text, sizeof text, "%03d", row.slot + 1);
        ui::text(44, y, text, selected ? m::spot() : m::ghost());
        ui::text(78, y, row.name, selected ? m::spot() : row.exists ? m::ink() : m::ghost());
        const char* tag = row.slot == f.current ? (f.dirty ? "open *" : "open") : row.exists && row.checked && !row.valid ? "check" : "";
        ui::text(294 - ui::text_w(tag), y, tag, m::spot());
    }
    ui::text(20, 185, "l <", f.first ? m::ink() : m::ghost());
    std::snprintf(text, sizeof text, "%03d-%03d / 128", f.first + 1, f.first + f.row_count);
    ui::text(121, 185, text, m::ghost()); ui::text(282, 185, "> r", f.first + kPatchRowsPerPage < PROJECT_SLOTS ? m::ink() : m::ghost());
    ui::text(40, 213, destination ? "back" : "open", !destination && !f.exists ? m::ghost() : m::ink());
    ui::text(destination ? 116 : 128, 213, destination ? "save here" : "save", m::spot());
    ui::rect(destination ? 113 : 125, 226, destination ? 60 : 30, 1, m::spot());
    if (!destination) ui::text(220, 213, "save copy", m::ink());
}

static bool project_overlay_draw_top() {
    const auto& f = g_project_overlay_frame;
    if (!f.active) return false;
    namespace m = menu_style;
    m::paper(400); m::page_heading(f.discard ? page_layout::Title::UnsavedChanges : page_layout::Title::Patches, m::ink());
    ui::text(290, 21, "project library", m::ghost());
    char text[64];
    std::snprintf(text, sizeof text, "selected / %03d", f.selected + 1);
    ui::text(20, 52, text, m::spot()); ui::text(20, 73, f.selected_name, m::ink(), 2);
    ui::rect(20, 112, 1, 19, m::branch()); ui::rect(20, 112, 13, 1, m::branch()); ui::rect(20, 130, 13, 1, m::branch());
    ui::text(44, 108, "last saved", m::ghost()); ui::text(160, 108, f.saved_at, m::ink());
    ui::text(44, 126, "on sd card", m::ghost());
    ui::text(160, 126, !f.exists ? "empty slot" : !f.checked ? "not checked" : f.valid ? "header recognized" : "check file", m::ink());
    ui::text(20, 154, "currently open", m::ghost());
    ui::rect(20, 178, 1, 21, m::branch()); ui::rect(20, 178, 13, 1, m::branch()); ui::rect(20, 198, 15, 1, m::spot());
    m::marker(34, 198, m::spot()); ui::text(44, 174, f.current_name, m::ink());
    if (f.dirty) ui::text(50 + ui::text_w(f.current_name), 174, "*", m::spot());
    if (f.current >= 0) {
        std::snprintf(text, sizeof text, "%03d", f.current + 1); ui::text(362, 174, text, m::ghost());
    }
    ui::text(44, 194, f.dirty ? "unsaved changes" : f.current < 0 ? "not saved yet" : "saved", m::spot());
    if (f.current >= 0) {
        std::snprintf(text, sizeof text, "save -> %03d", f.current + 1); ui::text(380 - ui::text_w(text), 194, text, m::ghost());
    }
    if (f.replace >= 0) {
        std::snprintf(text, sizeof text, "replace %03d? / save again", f.replace + 1); ui::text(20, 225, text, m::spot());
    } else if (f.notice[0]) ui::text(20, 225, f.notice, m::spot());
    else if (f.pending.action != DestructiveAction::None) {
        project_overlay_intent(f, text, sizeof text); ui::text(20, 225, text, m::spot());
    } else if (f.page == PatchPage::Destination) m::footer(400, "choose destination / a save here", "b back");
    else if (f.page == PatchPage::Name) m::footer(400, "name / lowercase letters and symbols", "b back");
    else m::footer(400, "a open selected / x save current", "b close");
    return true;
}
