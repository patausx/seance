// Included only by main.cpp (and exact-main host probes), inside ui3ds.
// PATCHES alone owns this controller. Draw consumes the pointer-free frame only.
enum class PatchPage { List, Name };
enum class PatchMenu { None, Row, Delete, Replace, Pending };
static constexpr int kPatchRowsPerPage = 7, kPatchRowY = 50, kPatchRowStep = 22;
enum PatchKey { kPatchKeyClear = 40, kPatchKeySpace, kPatchKeyDelete, kPatchKeyDone };
static constexpr char kPatchKeys[] = "1234567890qwertyuiopasdfghjkl-zxcvbnm_.'";
static_assert(sizeof(kPatchKeys) == kPatchKeyClear + 1, "four native rows of ten keys");
static PatchPage g_patch_page = PatchPage::List;
static PatchMenu g_patch_menu = PatchMenu::None;
static int g_patch_menu_item = 0, g_patch_key_cursor = 0;
// Logical selection/viewport are NOT SNC file slots. A detached working project
// has one synthetic first row (-1); there are never synthetic empty destinations.
static int g_patch_selection = 0, g_patch_first = 0;
static int g_patch_order[PROJECT_SLOTS] = {}, g_patch_order_count = 0;
static uint32_t g_patch_promoted[PROJECT_SLOTS] = {}, g_patch_promotion = 0;
static uint32_t g_patch_ui_epoch = 1, g_patch_menu_identity = 1;
static bool g_patch_order_ready = false, g_patch_cache_dirty = true;
static bool g_patch_menu_opening = false, g_patch_visible_last = false;
static bool g_patch_inspect_pending = false;
static patchio::PatchPreview g_patch_previews[PROJECT_SLOTS];
static bool g_patch_preview_checked[PROJECT_SLOTS] = {};
static word_menu::State g_patch_word_state;
static char g_patch_unsaved_name[PROJECT_NAME_BYTES] = "new project";
static int g_patch_name_slot = -1;
static bool g_patch_copy = false, g_patch_rename = false, g_patch_name_draft_only = false;
static bool g_patch_name_file_saved = false;
static char g_patch_draft[PROJECT_NAME_BYTES] = {};
static project_files::Snapshot g_patch_name_identity;
static bool g_patch_pending_from_library = false;
static PendingDestructiveAction g_patch_pending_context;

struct PatchFileChoice {
    project_files::Snapshot file;
    int slot = -1, logical = -1, current = -1;
    uint32_t revision = 0;
    char name[PROJECT_NAME_BYTES] = {};
};
static PatchFileChoice g_patch_file_choice;
struct ProjectTouch {
    bool owned = false, armed = false, scrolling = false, can_scroll = false;
    Overlay overlay = Overlay::None;
    PatchPage page = PatchPage::List;
    PatchMenu menu = PatchMenu::None;
    int target = -1, x = 0, y = 0, first = 0, slot = -1, selection = -1;
    int name_slot = -1, item = -1, current = -1;
    uint32_t revision = 0, epoch = 0, ink = 0, spot = 0, ghost = 0, branch = 0, paper = 0;
    PendingDestructiveAction pending;
};
static ProjectTouch g_project_touch;
static void project_overlay_cancel_input() { g_project_touch = {}; }
static void patch_library_changed(bool opening = false) {
    project_overlay_cancel_input();
    ++g_patch_ui_epoch; g_patch_cache_dirty = true;
    if (opening) { ++g_patch_menu_identity; g_patch_menu_opening = true; }
}
static void patch_library_notice(const char* text) {
    std::snprintf(g_patch_notice, sizeof g_patch_notice, "%s", text);
    g_save_flash = 180;
}
static bool patch_library_detached() { return !project_slot_valid(g_project_session.active_slot()); }
static int patch_library_row_count() { return g_patch_order_count + (patch_library_detached() ? 1 : 0); }
static int patch_library_row_slot(int logical_row) {
    if (logical_row < 0 || logical_row >= patch_library_row_count()) return -1;
    if (patch_library_detached()) { if (!logical_row) return -1; --logical_row; }
    return g_patch_order[logical_row];
}
static int patch_library_logical_slot(int slot) {
    for (int row = 0; row < patch_library_row_count(); ++row)
        if (patch_library_row_slot(row) == slot) return row;
    return -1;
}
static void patch_library_position(int logical_row, bool reveal = true) {
    const int count = patch_library_row_count();
    g_patch_selection = count ? std::clamp(logical_row, 0, count - 1) : -1;
    g_patch_slot = g_patch_selection >= 0 ? patch_library_row_slot(g_patch_selection) : -1;
    const int last = std::max(0, count - kPatchRowsPerPage);
    g_patch_first = std::clamp(g_patch_first, 0, last);
    if (reveal && g_patch_selection >= 0) {
        if (g_patch_selection < g_patch_first) g_patch_first = g_patch_selection;
        if (g_patch_selection >= g_patch_first + kPatchRowsPerPage)
            g_patch_first = g_patch_selection - kPatchRowsPerPage + 1;
    }
}
static void patch_library_build_order() {
    g_patch_order_count = 0;
    // scan_patch_slots deliberately marks all places occupied on SD failure.
    // That safety sentinel is NOT a list of 128 saved projects.
    if (!g_patch_catalog_failed) for (int slot = 0; slot < PROJECT_SLOTS; ++slot)
        if (g_patch_exists[slot]) g_patch_order[g_patch_order_count++] = slot;
    std::sort(g_patch_order, g_patch_order + g_patch_order_count, [](int a, int b) {
        if (g_patch_saved_at[a] != g_patch_saved_at[b]) return g_patch_saved_at[a] > g_patch_saved_at[b];
        if (g_patch_promoted[a] != g_patch_promoted[b]) return g_patch_promoted[a] > g_patch_promoted[b];
        return a < b;
    });
    g_patch_order_ready = true;
}
static void patch_library_reconcile() {
    if (!g_patch_order_ready) { patch_library_build_order(); return; }
    const int selected_slot = g_patch_slot, old_selection = g_patch_selection;
    bool seen[PROJECT_SLOTS] = {};
    int count = 0;
    if (!g_patch_catalog_failed) {
        // Selection probes can remove a vanished row, but NEVER sort/promote the
        // other rows under a live finger. Unknown additions append in slot order.
        for (int i = 0; i < g_patch_order_count; ++i) {
            const int slot = g_patch_order[i];
            if (g_patch_exists[slot] && !seen[slot]) { g_patch_order[count++] = slot; seen[slot] = true; }
        }
        for (int slot = 0; slot < PROJECT_SLOTS; ++slot)
            if (g_patch_exists[slot] && !seen[slot]) g_patch_order[count++] = slot;
    }
    g_patch_order_count = count;
    const int selected = patch_library_logical_slot(selected_slot);
    patch_library_position(selected >= 0 ? selected : old_selection, false);
}
static void patch_library_invalidate(int slot) {
    if (project_slot_valid(slot)) { g_patch_previews[slot] = {}; g_patch_preview_checked[slot] = false; }
    g_patch_inspect_pending = true; g_patch_cache_dirty = true;
}
static void patch_library_promote(int slot) {
    if (!project_slot_valid(slot) || g_patch_catalog_failed || !g_patch_exists[slot]) return;
    patch_library_reconcile();
    int at = -1;
    for (int i = 0; i < g_patch_order_count; ++i) if (g_patch_order[i] == slot) { at = i; break; }
    if (at < 0) return;
    for (int i = at; i > 0; --i) g_patch_order[i] = g_patch_order[i - 1];
    g_patch_order[0] = slot; g_patch_promoted[slot] = ++g_patch_promotion;
    patch_library_position(patch_library_logical_slot(slot));
    patch_library_changed();
}
static void patch_library_clear_choice() {
    g_patch_file_choice = {};
    g_patch_confirm_slot = g_patch_delete_confirm_slot = -1;
}
static void patch_library_close_menu(bool cancel_pending = false) {
    g_patch_menu = PatchMenu::None; g_patch_menu_item = 0;
    patch_library_clear_choice(); patch_library_changed();
    if (cancel_pending) g_project_session.cancel_pending();
}
static void patch_library_clear_name() {
    g_patch_name_slot = -1;
    g_patch_copy = g_patch_rename = g_patch_name_draft_only = g_patch_name_file_saved = false;
    g_patch_name_identity = {};
}
static void patch_library_interrupt() {
    // Navigation/transport cancels this library's own work only. A partial SNC
    // commit/clone remains on disk and the session's actual saved/dirty state stays.
    patch_library_close_menu(true); patch_library_clear_name();
    g_patch_page = PatchPage::List; g_patch_inspect_pending = true;
}
static void project_overlay_cancel_lifecycle() {
    patch_library_interrupt();
    for (int slot = 0; slot < PROJECT_SLOTS; ++slot) g_patch_preview_checked[slot] = false;
    g_patch_visible_last = false;
}
static void patch_library_inspect_selection() {
    // Commands/tick only, never capture/draw or the audio guard. At most ONE
    // selected path; names/order/dates for all other rows use cached metadata.
    if (g_project_touch.owned) { g_patch_inspect_pending = true; return; }
    g_patch_inspect_pending = false;
    const int slot = g_patch_slot;
    if (g_patch_catalog_failed || !project_slot_valid(slot) || !g_patch_exists[slot]) return;
    if (!g_patch_checked[slot]) refresh_patch_slot(slot);
    if (g_patch_exists[slot] && !g_patch_preview_checked[slot]) {
        char path[96]; patch_path(slot, path, sizeof path);
        (void)patchio::read_preview(path, g_patch_previews[slot]);
        g_patch_preview_checked[slot] = true;
    }
    if (!g_patch_exists[slot]) {
        patch_library_invalidate(slot); patch_library_reconcile(); patch_library_changed();
    }
    g_patch_cache_dirty = true;
}
static void patch_library_reset() {
    patch_library_interrupt();
    const bool new_catalogue = !g_patch_catalog_ready;
    scan_patch_slots(); // the existing single cached directory enumeration
    const int previous = g_patch_slot;
    if (new_catalogue) {
        std::fill(std::begin(g_patch_promoted), std::end(g_patch_promoted), 0);
        g_patch_promotion = 0;
    }
    if (!g_patch_order_ready || new_catalogue) patch_library_build_order();
    else patch_library_reconcile(); // a selected mtime probe does not reorder even on return
    g_patch_first = 0;
    const int selected = patch_library_detached() ? 0 : patch_library_logical_slot(previous);
    const int current = patch_library_logical_slot(g_project_session.active_slot());
    patch_library_position(selected >= 0 ? selected : current >= 0 ? current : 0);
    patch_library_inspect_selection();
    g_patch_visible_last = false;
}
static void patch_library_cancel_pending() {
    const bool was_pending = g_project_session.has_pending() || g_overlay == Overlay::ConfirmDiscard;
    g_project_session.cancel_pending();
    patch_library_close_menu(); patch_library_clear_name(); g_patch_page = PatchPage::List;
    if (was_pending) g_overlay = g_patch_pending_from_library ? Overlay::PatchBrowser : Overlay::None;
    g_patch_pending_context = {};
}
static void patch_library_back() {
    project_overlay_cancel_input();
    if (g_patch_page == PatchPage::Name) {
        if (g_project_session.has_pending()) patch_library_cancel_pending();
        else { patch_library_clear_name(); g_patch_page = PatchPage::List; patch_library_close_menu(); }
        patch_library_reconcile(); return;
    }
    if (g_project_session.has_pending() || g_overlay == Overlay::ConfirmDiscard) { patch_library_cancel_pending(); return; }
    if (g_patch_menu != PatchMenu::None) { patch_library_close_menu(); return; }
    patch_library_interrupt(); g_overlay = Overlay::None;
}
static void patch_library_select(int logical_row) {
    if (logical_row < 0 || logical_row >= patch_library_row_count()) return;
    if (g_project_session.has_pending() || g_overlay == Overlay::ConfirmDiscard) patch_library_cancel_pending();
    patch_library_close_menu();
    g_patch_page = PatchPage::List; patch_library_clear_name();
    // A selection from the SAME original list owns this lift even when it closes
    // a pending branch. It does not also open the selected project.
    if (g_overlay == Overlay::None) g_overlay = Overlay::PatchBrowser;
    patch_library_position(logical_row); patch_library_inspect_selection();
}
static void patch_library_move(int amount) {
    if (!patch_library_row_count()) return;
    const int from = std::max(0, g_patch_selection);
    patch_library_select(std::clamp(from + amount, 0, patch_library_row_count() - 1));
}
static int patch_library_menu_count() {
    if (g_patch_menu == PatchMenu::None) return 0;
    if (g_patch_menu == PatchMenu::Replace) return 2;
    if (g_patch_menu == PatchMenu::Pending) return 3;
    return project_slot_valid(g_patch_slot) ? 4 : 3;
}
static const char* patch_library_menu_label(int item) {
    if (item < 0 || item >= patch_library_menu_count()) return "";
    if (g_patch_menu == PatchMenu::Replace) return item ? "cancel" : "save again";
    if (g_patch_menu == PatchMenu::Pending) {
        const auto action = g_project_session.pending().action;
        if (item == 2) return "cancel";
        if (action == DestructiveAction::LoadProject) return item ? "discard + open" : "save + open";
        if (action == DestructiveAction::ExitApp) return item ? "discard + exit" : "save + exit";
        return item ? "discard + new" : "save + new";
    }
    if (!project_slot_valid(g_patch_slot)) return item == 0 ? "save" : item == 1 ? "name" : "discard";
    if (item == 0) return g_patch_slot == g_project_session.active_slot() ? "save" : "open";
    if (item == 1) return "rename";
    if (item == 2) return g_patch_slot == g_project_session.active_slot() && g_project_session.dirty() ? "copy saved" : "copy";
    return g_patch_menu == PatchMenu::Delete ? "delete again" : "delete";
}
static word_menu::Layout patch_library_menu_layout() {
    const int count = patch_library_menu_count();
    const int row = std::clamp(g_patch_selection - g_patch_first, 0, kPatchRowsPerPage - 1);
    const int anchor_y = kPatchRowY + row * kPatchRowStep;
    int top = anchor_y + 22;
    if (top + (count - 1) * 18 + 8 > 204) top = anchor_y - 22 - (count - 1) * 18;
    return {206, 222, top, 18, 206, 306}; // NO connector from invoking dots
}
static void patch_library_menu_open(PatchMenu menu, int item) {
    g_patch_menu = menu;
    g_patch_menu_item = std::clamp(item, 0, std::max(0, patch_library_menu_count() - 1));
    patch_library_changed(true);
}
static void patch_library_row_menu(int logical_row) {
    patch_library_select(logical_row);
    if (g_patch_selection >= 0) patch_library_menu_open(PatchMenu::Row, 0);
}
static bool patch_library_choice_current() {
    const auto& choice = g_patch_file_choice;
    char name[PROJECT_NAME_BYTES] = {}; project_name_label(g_project_names, g_patch_slot, name, sizeof name);
    return choice.file.ready && choice.slot == g_patch_slot && choice.logical == g_patch_selection &&
        choice.current == g_project_session.active_slot() && choice.revision == g_project_session.revision() &&
        !std::memcmp(choice.name, name, sizeof name);
}
static bool patch_library_choose_file(int slot) {
    patch_library_clear_choice();
    if (!project_slot_valid(slot) || g_patch_catalog_failed || !g_patch_exists[slot]) return false;
    char path[96]; patch_path(slot, path, sizeof path);
    auto& choice = g_patch_file_choice;
    if (!project_files::inspect(path, choice.file)) {
        patch_library_notice("cannot inspect file / action cancelled"); patch_library_invalidate(slot); return false;
    }
    choice.slot = slot; choice.logical = g_patch_selection;
    choice.current = g_project_session.active_slot(); choice.revision = g_project_session.revision();
    project_name_label(g_project_names, slot, choice.name, sizeof choice.name);
    g_patch_saved_at[slot] = choice.file.identity.st_mtime > 0 ? uint64_t(choice.file.identity.st_mtime) : 0;
    patch_library_invalidate(slot); // explicit file inspection also refreshes the saved preview, never the row order
    return true;
}
static int patch_library_free_slot() {
    if (g_patch_catalog_failed) { patch_library_notice("library unavailable / sd error"); return -1; }
    for (int slot = 0; slot < PROJECT_SLOTS; ++slot) if (!g_patch_exists[slot]) return slot;
    patch_library_notice("library full / no free place"); return -1;
}
static void patch_library_show_pending() {
    if (!g_project_session.has_pending()) return;
    const auto pending = g_project_session.pending();
    const int slot = pending.action == DestructiveAction::LoadProject ? pending.slot : g_project_session.active_slot();
    const int row = patch_library_logical_slot(slot);
    patch_library_position(row >= 0 ? row : std::max(0, g_patch_selection));
    patch_library_clear_choice(); patch_library_menu_open(PatchMenu::Pending, 2);
    g_patch_pending_context = pending; g_overlay = Overlay::ConfirmDiscard;
    g_patch_inspect_pending = true;
}
static void patch_library_request_confirmation() {
    // request_destructive has already armed the real session's pending action.
    // Do not call reset() here: it deliberately cancels pending work.
    g_patch_pending_from_library = g_overlay == Overlay::PatchBrowser ||
        (g_overlay == Overlay::ConfirmDiscard && g_patch_pending_from_library);
    project_overlay_cancel_input(); patch_library_clear_name(); g_patch_page = PatchPage::List;
    scan_patch_slots();
    if (!g_patch_order_ready) patch_library_build_order(); else patch_library_reconcile();
    patch_library_show_pending(); patch_library_inspect_selection();
}
static void patch_library_finish_pending() {
    if (!g_project_session.has_pending()) return;
    const auto pending = g_project_session.take_pending();
    patch_library_close_menu(); patch_library_clear_name(); g_patch_page = PatchPage::List;
    g_patch_pending_context = {};
    const bool ok = execute_destructive(pending); // unchanged staged engine/PCM transaction
    if (ok && pending.action == DestructiveAction::NewProject) {
        std::snprintf(g_patch_unsaved_name, sizeof g_patch_unsaved_name, "new project");
        patch_library_reconcile(); patch_library_position(0);
        g_save_flash = 0; patch_library_changed(); // retain NEW's existing return to the patch table
    } else if (!ok) {
        g_overlay = Overlay::PatchBrowser; patch_library_reconcile(); patch_library_changed();
    }
}
static void patch_library_new() {
    patch_library_close_menu(true); patch_library_clear_name(); g_patch_page = PatchPage::List;
    const uint32_t before = g_project_session.revision();
    request_destructive(DestructiveAction::NewProject);
    if (!g_project_session.has_pending() && g_overlay == Overlay::None && patch_library_detached() &&
        g_project_session.revision() != before) {
        std::snprintf(g_patch_unsaved_name, sizeof g_patch_unsaved_name, "new project");
        patch_library_reconcile(); patch_library_position(0);
        g_save_flash = 0; patch_library_changed(); // draft appears first when PATCHES is reopened
    }
}
static void patch_library_name(bool rename) {
    const int source = g_patch_slot;
    const bool draft_only = rename && !project_slot_valid(source);
    const int destination = rename ? source : patch_library_free_slot();
    if (!draft_only && !project_slot_valid(destination)) return;
    project_files::Snapshot identity;
    if (rename && !draft_only) {
        char path[96]; patch_path(source, path, sizeof path);
        if (!project_files::inspect(path, identity)) { patch_library_notice("file changed / rename cancelled"); return; }
    }
    // An unsaved current name is UI metadata only; naming it does not serialize
    // the working revision until the user explicitly chooses save.
    patch_library_close_menu();
    g_patch_name_slot = destination; g_patch_rename = rename; g_patch_copy = false;
    g_patch_name_draft_only = draft_only; g_patch_name_file_saved = false;
    g_patch_name_identity = identity;
    if (rename && !draft_only) project_name_label(g_project_names, source, g_patch_draft, sizeof g_patch_draft);
    else std::snprintf(g_patch_draft, sizeof g_patch_draft, "%s", g_patch_unsaved_name);
    g_patch_key_cursor = kPatchKeyClear; g_patch_page = PatchPage::Name;
    g_save_flash = 0; patch_library_changed();
}
static void patch_library_name_done() {
    if (g_patch_page != PatchPage::Name) return;
    project_overlay_cancel_input();
    int slot = g_patch_name_slot;
    ProjectNames proposed = g_project_names;
    const int validation_slot = project_slot_valid(slot) ? slot : 0;
    if (!project_name_set(proposed, validation_slot, g_patch_draft)) {
        patch_library_notice("enter a name / 1-23 letters or symbols"); return;
    }
    if (g_patch_name_draft_only) {
        std::snprintf(g_patch_unsaved_name, sizeof g_patch_unsaved_name, "%s", proposed.names[validation_slot]);
        patch_library_clear_name(); g_patch_page = PatchPage::List; patch_library_changed(); return;
    }
    if (!project_slot_valid(slot)) return;
    char path[96]; patch_path(slot, path, sizeof path);
    if (g_patch_rename || g_patch_name_file_saved) {
        // Metadata retries never re-save SNC, mark a newer revision clean, or
        // silently rename an external replacement after the keyboard was opened.
        if (!project_files::matches(path, g_patch_name_identity)) {
            patch_library_notice("file changed / name not saved"); patch_library_invalidate(slot); return;
        }
    } else {
        refresh_patch_slot(slot); // mandatory late path check, not cached overwrite authority
        if (g_patch_exists[slot]) {
            patch_library_invalidate(slot); patch_library_reconcile();
            g_patch_name_slot = patch_library_free_slot();
            patch_library_notice(g_patch_catalog_failed ? "library unavailable / sd error" :
                g_patch_name_slot < 0 ? "library full / name not saved" : "place now occupied / retry done");
            patch_library_changed(); return; // KEEP the name; a fresh Done rechecks the new free place
        }
        project_files::VacantTarget reservation;
        const auto reserved = reservation.reserve(path);
        if (reserved != fileio::ReserveResult::Ok) {
            refresh_patch_slot(slot); patch_library_reconcile(); patch_library_invalidate(slot);
            if (reserved == fileio::ReserveResult::Occupied) g_patch_name_slot = patch_library_free_slot();
            patch_library_notice(reserved == fileio::ReserveResult::Occupied ? "place now occupied / retry done" : "cannot reserve place / retry done");
            patch_library_changed(); return;
        }
        // The empty placeholder proved the place free; give the name back so
        // the save publishes onto a vacant name (the SD card refuses rename
        // onto an existing file, even our own empty one).
        if (!reservation.release_for_publication()) {
            refresh_patch_slot(slot); patch_library_reconcile(); patch_library_invalidate(slot);
            patch_library_notice("place now occupied / retry done"); patch_library_changed(); return;
        }
        const bool ok = save_current_patch(slot); // body/REC/late revision checks remain byte-exact
        struct stat written{};
        const bool published = ::stat(path, &written) == 0 && S_ISREG(written.st_mode) && written.st_size > 0;
        refresh_patch_slot(slot); patch_library_invalidate(slot); patch_library_reconcile();
        if (published) { project_name_clear(g_project_names, slot); patch_library_promote(slot); }
        if (!ok) { patch_browser_result(false, PatchAction::Save, slot); return; }
        g_patch_name_file_saved = true;
        // This inspection is only our just-published revision token, not load validation.
        if (!project_files::inspect(path, g_patch_name_identity)) {
            patch_library_notice("project saved / cannot check name target"); return;
        }
        proposed = g_project_names;
        (void)project_name_set(proposed, slot, g_patch_draft);
    }
    if (!save_project_names(proposed)) {
        patch_library_notice(g_patch_rename ? "name not saved / retry done" : "project saved / name failed / retry done");
        patch_library_changed(); return;
    }
    const bool renamed = g_patch_rename, copied = g_patch_copy;
    patch_library_clear_name(); g_patch_page = PatchPage::List;
    patch_library_position(patch_library_logical_slot(slot)); patch_library_changed();
    if (copied) patch_library_notice("copied saved file / current unchanged");
    else patch_browser_result(true, renamed ? PatchAction::Rename : PatchAction::Save, slot);
    if (!renamed && !copied) patch_library_finish_pending();
}
static void patch_library_key(int key) {
    if (g_patch_page != PatchPage::Name) return;
    project_overlay_cancel_input();
    const size_t length = std::strlen(g_patch_draft);
    if (key == kPatchKeyClear) g_patch_draft[0] = 0;
    else if (key == kPatchKeyDelete) { if (length) g_patch_draft[length - 1] = 0; }
    else if (key == kPatchKeyDone) { patch_library_name_done(); return; }
    else if (length < sizeof g_patch_draft - 1 && (key == kPatchKeySpace || (key >= 0 && key < kPatchKeyClear))) {
        g_patch_draft[length] = key == kPatchKeySpace ? ' ' : kPatchKeys[key];
        g_patch_draft[length + 1] = 0;
    }
    patch_library_changed();
}
static void patch_library_save() {
    if (g_patch_page != PatchPage::List) return;
    project_overlay_cancel_input();
    const int slot = g_project_session.active_slot(); // NEVER the selected saved file
    if (g_patch_catalog_failed) { patch_library_notice("library unavailable / sd error"); return; }
    const bool replace = g_patch_menu == PatchMenu::Replace;
    if (replace) {
        char path[96]; patch_path(slot, path, sizeof path);
        if (!project_slot_valid(slot) || !patch_library_choice_current() ||
            !project_files::matches(path, g_patch_file_choice.file)) {
            patch_library_notice("file changed / save cancelled"); patch_library_invalidate(slot);
            if (g_project_session.has_pending()) patch_library_cancel_pending(); else patch_library_close_menu();
            return;
        }
    }
    patch_library_close_menu(); // one explicit command consumes any earlier delete/replace choice
    if (!project_slot_valid(slot)) { patch_library_name(false); return; }
    refresh_patch_slot(slot);
    if (g_patch_exists[slot] && !replace) {
        patch_library_reconcile();
        const int row = patch_library_logical_slot(slot);
        if (row < 0) { patch_library_notice("cannot find current saved file"); return; }
        patch_library_position(row); // truthful replace anchor, even if another file was selected
        if (!patch_library_choose_file(slot)) return;
        g_patch_confirm_slot = slot; patch_library_menu_open(PatchMenu::Replace, 1); return;
    }
    bool ok = false;
    if (!g_patch_exists[slot]) {
        char path[96]; patch_path(slot, path, sizeof path);
        project_files::VacantTarget reservation;
        const auto reserved = reservation.reserve(path);
        if (reserved != fileio::ReserveResult::Ok) {
            refresh_patch_slot(slot); patch_library_reconcile(); patch_library_invalidate(slot);
            patch_library_notice("place changed / save cancelled"); return;
        }
        // Vacant-name publication, as for a named new save.
        if (!reservation.release_for_publication()) {
            refresh_patch_slot(slot); patch_library_reconcile(); patch_library_invalidate(slot);
            patch_library_notice("place changed / save cancelled"); return;
        }
        ok = save_current_patch(slot);
    } else ok = save_current_patch(slot);
    patch_browser_result(ok, PatchAction::Save, slot);
    patch_library_invalidate(slot); patch_library_reconcile();
    if (ok) { patch_library_promote(slot); patch_library_finish_pending(); }
    else if (g_project_session.has_pending()) patch_library_show_pending();
}
static void patch_library_open() {
    project_overlay_cancel_input();
    const int slot = g_patch_slot;
    patch_library_close_menu(true);
    if (g_patch_page != PatchPage::List || !project_slot_valid(slot) || g_patch_catalog_failed || !g_patch_exists[slot]) {
        patch_library_notice(g_patch_catalog_failed ? "library unavailable / sd error" : "no saved file selected"); return;
    }
    request_destructive(DestructiveAction::LoadProject, slot);
}
static void patch_library_copy_saved() {
    const int source = g_patch_slot;
    if (!project_slot_valid(source) || !g_patch_exists[source] || g_patch_catalog_failed) return;
    const int destination = patch_library_free_slot();
    if (destination < 0) return;
    char source_path[96], destination_path[96], title[PROJECT_NAME_BYTES];
    patch_path(source, source_path, sizeof source_path); patch_path(destination, destination_path, sizeof destination_path);
    project_name_label(g_project_names, source, title, sizeof title);
    patch_library_close_menu(true);
    const auto result = project_files::copy_saved(source_path, destination_path);
    if (result != project_files::Result::Ok) {
        refresh_patch_slot(source); refresh_patch_slot(destination);
        patch_library_reconcile(); patch_library_invalidate(destination);
        patch_library_notice(result == project_files::Result::Occupied ? "place now occupied / retry copy" :
            result == project_files::Result::Changed ? "file changed / copy cancelled" : "copy failed / source unchanged");
        return;
    }
    // Clone the disk version, even when source is the dirty current project.
    // No Engine capture/load/session swap, sample export, or source-title change.
    project_name_clear(g_project_names, destination);
    refresh_patch_slot(destination); patch_library_invalidate(destination); patch_library_promote(destination);
    g_patch_name_slot = destination; g_patch_copy = true; g_patch_rename = g_patch_name_draft_only = false;
    g_patch_name_file_saved = true;
    (void)project_files::inspect(destination_path, g_patch_name_identity);
    std::snprintf(g_patch_draft, sizeof g_patch_draft, "%s", title);
    g_patch_key_cursor = kPatchKeyClear; g_patch_page = PatchPage::Name; g_save_flash = 0;
    patch_library_changed();
}
static void patch_library_delete() {
    const int slot = g_patch_slot;
    if (!project_slot_valid(slot) || g_patch_catalog_failed || !g_patch_exists[slot]) return;
    if (g_patch_menu != PatchMenu::Delete) {
        if (!patch_library_choose_file(slot)) return;
        g_patch_delete_confirm_slot = slot; patch_library_menu_open(PatchMenu::Delete, 3); return;
    }
    char path[96]; patch_path(slot, path, sizeof path);
    const auto result = patch_library_choice_current() ? project_files::remove_saved(path, g_patch_file_choice.file) : project_files::Result::Changed;
    if (result == project_files::Result::Ok) {
        if (g_project_session.active_slot() == slot) {
            project_name_label(g_project_names, slot, g_patch_unsaved_name, sizeof g_patch_unsaved_name);
            g_project_session.detach_active_slot();
        }
        start_store_forget(g_start_store, slot);
        g_start_store_write_failed = !start_store_save_preferences(START_MENU_PATH, g_start_store);
        ProjectNames proposed = g_project_names; project_name_clear(proposed, slot);
        if (!save_project_names(proposed)) project_name_clear(g_project_names, slot);
    }
    patch_library_close_menu(true); patch_library_invalidate(slot);
    patch_browser_result(result == project_files::Result::Ok, PatchAction::Delete, slot);
    if (result == project_files::Result::Changed) patch_library_notice("file changed / delete cancelled");
    patch_library_reconcile(); patch_library_changed();
}
static void patch_library_menu_activate(int item) {
    if (item < 0 || item >= patch_library_menu_count()) return;
    project_overlay_cancel_input();
    if (g_patch_menu == PatchMenu::Pending) {
        if (item == 2) { patch_library_cancel_pending(); return; }
        if (item == 1) { patch_library_finish_pending(); return; }
        patch_library_save(); return;
    }
    if (g_patch_menu == PatchMenu::Replace) {
        if (item == 1) {
            if (g_project_session.has_pending()) patch_library_cancel_pending(); else patch_library_close_menu();
        } else patch_library_save();
        return;
    }
    if (!project_slot_valid(g_patch_slot)) {
        if (item == 0) patch_library_save();
        else if (item == 1) patch_library_name(true);
        else patch_library_new(); // existing dirty request, never an unguarded draft discard
        return;
    }
    if (item == 0) {
        if (g_patch_slot == g_project_session.active_slot()) patch_library_save(); else patch_library_open();
    } else if (item == 1) patch_library_name(true);
    else if (item == 2) patch_library_copy_saved();
    else patch_library_delete();
}
static void patch_library_menu_move(int amount) {
    const int count = patch_library_menu_count();
    if (!count) return;
    const int next = (g_patch_menu_item + amount + count) % count;
    if (g_patch_menu == PatchMenu::Delete && next != 3) {
        patch_library_clear_choice(); g_patch_menu = PatchMenu::Row; patch_library_changed(true);
    }
    g_patch_menu_item = next; project_overlay_cancel_input();
}

// Stable logical targets. All commands commit on lift in the ORIGINAL target;
// leaving its area disarms permanently. A row drag may scroll, never activate.
static constexpr int kPatchTouchNew = 1, kPatchTouchSave = 2, kPatchTouchOutside = 3;
static constexpr int kPatchTouchRow = 10, kPatchTouchDots = 160, kPatchTouchWord = 320, kPatchTouchKey = 400;
static int project_touch_target(int x, int y) {
    if (g_overlay != Overlay::PatchBrowser && g_overlay != Overlay::ConfirmDiscard) return -1;
    if (g_patch_page == PatchPage::Name) {
        for (int i = 0; i < kPatchKeyClear; ++i)
            if (hit(x, y, 20 + i % 10 * 28, 80 + i / 10 * 24, 28, 24)) return kPatchTouchKey + i;
        for (int i = 0; i < 4; ++i)
            if (hit(x, y, 14 + i * 74, 202, 70, 30)) return kPatchTouchKey + kPatchKeyClear + i;
        return -1; // native keyboard, deliberately NO back word/button
    }
    if (g_patch_menu != PatchMenu::None) {
        const auto layout = patch_library_menu_layout();
        for (int i = 0; i < patch_library_menu_count(); ++i)
            if (hit(x, y, layout.left, layout.y - 4 + i * layout.step, layout.right - layout.left, layout.step)) return kPatchTouchWord + i;
    }
    // A pending destructive branch exposes only its three truthful choices.
    // Outside taps close it; they cannot secretly save/new/open on the same lift.
    if (g_patch_menu == PatchMenu::Pending) return hit(x, y, 0, 0, 320, 240) ? kPatchTouchOutside : -1;
    if (hit(x, y, 228, 12, 32, 27)) return kPatchTouchNew;
    if (hit(x, y, 264, 12, 42, 27)) return kPatchTouchSave;
    const int rows = std::min(kPatchRowsPerPage, std::max(0, patch_library_row_count() - g_patch_first));
    for (int row = 0; row < rows; ++row) {
        const int logical = g_patch_first + row;
        if (hit(x, y, 286, kPatchRowY - 6 + row * kPatchRowStep, 22, kPatchRowStep)) return kPatchTouchDots + logical;
        if (hit(x, y, 14, kPatchRowY - 6 + row * kPatchRowStep, 272, kPatchRowStep)) return kPatchTouchRow + logical;
    }
    return g_patch_menu != PatchMenu::None && hit(x, y, 0, 0, 320, 240) ? kPatchTouchOutside : -1;
}
static bool project_touch_current() {
    const auto& touch = g_project_touch;
    const auto pending = g_project_session.pending();
    return touch.owned && touch.overlay == g_overlay && touch.page == g_patch_page && touch.menu == g_patch_menu &&
        touch.slot == g_patch_slot && touch.selection == g_patch_selection && touch.name_slot == g_patch_name_slot &&
        touch.item == g_patch_menu_item && touch.current == g_project_session.active_slot() &&
        touch.revision == g_project_session.revision() && touch.epoch == g_patch_ui_epoch &&
        touch.ink == menu_style::ink() && touch.spot == menu_style::spot() &&
        touch.ghost == menu_style::ghost() && touch.branch == menu_style::branch() && touch.paper == menu_style::paper_color(0) &&
        touch.pending.action == pending.action && touch.pending.slot == pending.slot;
}
static void project_overlay_touch_down(int x, int y) {
    project_overlay_cancel_input();
    const int target = project_touch_target(x, y);
    if (target < 0) return;
    if (target >= kPatchTouchWord && target < kPatchTouchKey) {
        const int item = target - kPatchTouchWord;
        if (g_patch_menu == PatchMenu::Delete && item != 3) {
            patch_library_clear_choice(); g_patch_menu = PatchMenu::Row; patch_library_changed(true);
        }
        g_patch_menu_item = item; // immediate native focus, command still waits for lift
    } else if (target >= kPatchTouchKey) g_patch_key_cursor = target - kPatchTouchKey;
    auto& touch = g_project_touch;
    touch.owned = touch.armed = true; touch.overlay = g_overlay; touch.page = g_patch_page; touch.menu = g_patch_menu;
    touch.target = target; touch.x = x; touch.y = y; touch.first = g_patch_first;
    touch.slot = g_patch_slot; touch.selection = g_patch_selection; touch.name_slot = g_patch_name_slot;
    touch.item = g_patch_menu_item; touch.current = g_project_session.active_slot();
    touch.revision = g_project_session.revision(); touch.epoch = g_patch_ui_epoch; touch.pending = g_project_session.pending();
    touch.ink = menu_style::ink(); touch.spot = menu_style::spot();
    touch.ghost = menu_style::ghost(); touch.branch = menu_style::branch(); touch.paper = menu_style::paper_color(0);
    touch.can_scroll = g_patch_menu == PatchMenu::None && target >= kPatchTouchRow && target < kPatchTouchDots;
}
void patch_browser_touch_down(int x, int y) { project_overlay_touch_down(x, y); }
void confirm_discard_touch_down(int x, int y) { project_overlay_touch_down(x, y); }
static void project_overlay_touch_hold(int x, int y) {
    if (!g_project_touch.owned) return;
    if (!project_touch_current()) { project_overlay_cancel_input(); return; }
    auto& touch = g_project_touch;
    if (project_touch_target(x, y) != touch.target) touch.armed = false;
    if (!touch.can_scroll) return;
    if (!hit(x, y, 14, kPatchRowY - 6, 294, kPatchRowsPerPage * kPatchRowStep)) {
        touch.can_scroll = false; touch.armed = false; return;
    }
    const int delta = touch.y - y;
    if (delta >= 8 || delta <= -8 || touch.scrolling) {
        touch.scrolling = true; touch.armed = false;
        const int next = std::clamp(touch.first + delta / kPatchRowStep, 0, std::max(0, patch_library_row_count() - kPatchRowsPerPage));
        if (g_patch_first != next) { g_patch_first = next; g_patch_cache_dirty = true; }
    }
}
static void project_overlay_touch_release(bool commit) {
    const auto touch = g_project_touch;
    const bool valid = commit && project_touch_current() && touch.armed && !touch.scrolling;
    project_overlay_cancel_input(); // no newly revealed controller can inherit this lift
    if (!valid) return;
    const int target = touch.target;
    if (target >= kPatchTouchKey) patch_library_key(target - kPatchTouchKey);
    else if (target >= kPatchTouchWord) patch_library_menu_activate(target - kPatchTouchWord);
    else if (target >= kPatchTouchDots) patch_library_row_menu(target - kPatchTouchDots);
    else if (target >= kPatchTouchRow) patch_library_select(target - kPatchTouchRow);
    else if (target == kPatchTouchNew) patch_library_new();
    else if (target == kPatchTouchSave) patch_library_save();
    else if (target == kPatchTouchOutside) {
        if (g_project_session.has_pending() || g_overlay == Overlay::ConfirmDiscard) patch_library_cancel_pending();
        else patch_library_close_menu();
    }
}
static bool patch_library_action_chord(u32 down) {
    const u32 actions = down & (KEY_A | KEY_X | KEY_Y);
    return actions && (actions & (actions - 1)); // no A+X save/open/confirm combination
}
void confirm_discard_buttons(u32 down);
void patch_browser_buttons(u32 down) {
    if (g_overlay == Overlay::ConfirmDiscard && g_patch_page == PatchPage::List) { confirm_discard_buttons(down); return; }
    const u32 local = down & ~(KEY_TOUCH | KEY_ZL);
    if (!local) return;
    project_overlay_cancel_input();
    if (down & KEY_START) { patch_library_interrupt(); return; }
    if (down & KEY_SELECT) { patch_library_interrupt(); nav_open(); return; }
    if (down & KEY_B) { patch_library_back(); return; }
    if (patch_library_action_chord(down)) { patch_library_clear_choice(); if (g_patch_menu == PatchMenu::Delete || g_patch_menu == PatchMenu::Replace) patch_library_close_menu(); return; }
    if (g_patch_page == PatchPage::Name) {
        if (down & KEY_X) { patch_library_key(kPatchKeyDelete); return; }
        if (down & KEY_Y) { patch_library_key(kPatchKeySpace); return; }
        int& key = g_patch_key_cursor;
        if (down & KEY_LEFT) key = key >= kPatchKeyClear ? kPatchKeyClear + (key - kPatchKeyClear + 3) % 4 : key / 10 * 10 + (key + 9) % 10;
        else if (down & KEY_RIGHT) key = key >= kPatchKeyClear ? kPatchKeyClear + (key - kPatchKeyClear + 1) % 4 : key / 10 * 10 + (key + 1) % 10;
        else if (down & KEY_UP) {
            if (key >= kPatchKeyClear) key = 30 + (key - kPatchKeyClear) * 3;
            else if (key >= 10) key -= 10;
            else key = kPatchKeyClear + key / 3;
        } else if (down & KEY_DOWN) {
            if (key >= kPatchKeyClear) key = (key - kPatchKeyClear) * 3;
            else if (key < 30) key += 10;
            else key = kPatchKeyClear + (key - 30) / 3;
        } else if (down & KEY_A) patch_library_key(key);
        return;
    }
    if (g_patch_menu != PatchMenu::None) {
        if (down & KEY_UP) patch_library_menu_move(-1);
        else if (down & KEY_DOWN) patch_library_menu_move(1);
        else if (down & KEY_A) patch_library_menu_activate(g_patch_menu_item);
        else if (down & KEY_X) patch_library_save();
        else if (down & KEY_Y) patch_library_back();
        else if (down & (KEY_LEFT | KEY_L)) patch_library_move(-kPatchRowsPerPage);
        else if (down & (KEY_RIGHT | KEY_R)) patch_library_move(kPatchRowsPerPage);
        return;
    }
    if (down & KEY_UP) patch_library_move(-1);
    else if (down & KEY_DOWN) patch_library_move(1);
    else if (down & (KEY_LEFT | KEY_L)) patch_library_move(-kPatchRowsPerPage);
    else if (down & (KEY_RIGHT | KEY_R)) patch_library_move(kPatchRowsPerPage);
    else if (down & KEY_A) { if (!project_slot_valid(g_patch_slot)) patch_library_save(); else patch_library_open(); }
    else if (down & KEY_X) patch_library_save();
    else if (down & KEY_Y) patch_library_row_menu(g_patch_selection);
}
void confirm_discard_buttons(u32 down) {
    if (!(down & ~(KEY_TOUCH | KEY_ZL))) return;
    project_overlay_cancel_input();
    if (down & KEY_SELECT) { patch_library_cancel_pending(); nav_open(); return; }
    if (down & (KEY_B | KEY_START)) { patch_library_cancel_pending(); return; }
    if (patch_library_action_chord(down)) {
        if (g_patch_menu == PatchMenu::Replace) patch_library_cancel_pending();
        return;
    }
    if (g_patch_page == PatchPage::Name) { patch_browser_buttons(down); return; }
    if (down & KEY_UP) patch_library_menu_move(-1);
    else if (down & KEY_DOWN) patch_library_menu_move(1);
    else if (down & KEY_A) patch_library_menu_activate(g_patch_menu_item); // initial Pending/Replace focus is CANCEL
    else if ((down & KEY_X) && g_patch_menu == PatchMenu::Replace) patch_library_save();
    // X on Pending, shoulders and unsupported chords have no destructive shortcut.
}
void patch_browser_route_buttons(u32& down, bool& skip_touch, Overlay input_overlay) {
    if (input_overlay != Overlay::PatchBrowser && g_overlay != Overlay::PatchBrowser) return;
    const u32 local = down & ~(KEY_TOUCH | KEY_ZL);
    if (!local) return;
    project_overlay_cancel_input();
    if (input_overlay == Overlay::PatchBrowser && g_overlay == Overlay::PatchBrowser && !nav_is_open()) patch_browser_buttons(down);
    skip_touch = true; down &= KEY_TOUCH | KEY_ZL; // includes the frame which closes/opens a page
}
static void patch_library_tick() {
    const bool visible = !nav_is_open() && (g_overlay == Overlay::PatchBrowser || g_overlay == Overlay::ConfirmDiscard);
    if (!visible) {
        if (g_patch_visible_last || g_project_touch.owned || g_patch_menu != PatchMenu::None || g_patch_page != PatchPage::List)
            patch_library_interrupt();
        g_patch_visible_last = false; g_patch_cache_dirty = true; return;
    }
    if (!g_patch_visible_last) { g_patch_cache_dirty = true; g_patch_inspect_pending = true; }
    g_patch_visible_last = true;
    if (g_project_touch.owned && !project_touch_current()) project_overlay_cancel_input();
    if ((g_patch_menu == PatchMenu::Delete || g_patch_menu == PatchMenu::Replace) && !patch_library_choice_current()) {
        patch_library_notice("project changed / confirmation cancelled");
        if (g_project_session.has_pending()) patch_library_cancel_pending(); else patch_library_close_menu();
    }
    const auto pending = g_project_session.pending();
    if (g_overlay == Overlay::ConfirmDiscard && (!g_project_session.has_pending() ||
        pending.action != g_patch_pending_context.action || pending.slot != g_patch_pending_context.slot)) patch_library_cancel_pending();
    if (g_overlay == Overlay::ConfirmDiscard && g_patch_page == PatchPage::List && g_patch_menu == PatchMenu::None && g_project_session.has_pending()) patch_library_show_pending();
    if (g_patch_inspect_pending && !g_project_touch.owned) patch_library_inspect_selection();
}

struct ProjectOverlayFrame {
    struct Row { int slot = -1, logical = -1; bool current = false; char name[PROJECT_NAME_BYTES] = {}, tag[12] = {}; } rows[kPatchRowsPerPage];
    bool active = false, discard = false, dirty = false, exists = false, catalog_failed = false;
    bool copying = false, renaming = false, detached = false;
    int current = -1, selected = -1, selected_slot = -1, first = 0, row_count = 0, saved_count = 0, total_rows = 0;
    int key_cursor = 0, menu_count = 0, menu_item = 0;
    PatchPage page = PatchPage::List;
    PatchMenu menu = PatchMenu::None;
    PendingDestructiveAction pending;
    word_menu::Layout layout{};
    word_menu::Frame motion{};
    patchio::PatchPreview preview;
    uint32_t ink = 0, spot = 0, ghost = 0, branch = 0, paper = 0, paper_bottom = 0;
    char labels[4][24] = {};
    char current_name[PROJECT_NAME_BYTES] = {}, selected_name[PROJECT_NAME_BYTES] = {};
    char draft[PROJECT_NAME_BYTES] = {}, saved_at[32] = {}, notice[48] = {};
};
static ProjectOverlayFrame g_project_overlay_frame, g_patch_cached_background;
static_assert(sizeof(ProjectOverlayFrame) <= 1024 && std::is_trivially_copyable<ProjectOverlayFrame>::value,
              "seven metadata rows and shared word motion, pointer-free / no pixel storage");
static void patch_library_date(uint64_t value, char* out, size_t size, bool short_date) {
    const time_t stamp = time_t(value);
    const auto* date = stamp > 0 ? std::localtime(&stamp) : nullptr;
    if (!date || !std::strftime(out, size, short_date ? "%d %b" : "%d %b %Y / %H:%M", date)) std::snprintf(out, size, "--");
    for (char* p = out; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
}
namespace project_library_drawing {
using Pen = sigil_surface::Painter;
static void marker(const Pen& pen, int x, int y, uint32_t color) {
    if (!pen.cache) menu_style::marker(x, y, color);
    else for (int dy = -3; dy <= 3; ++dy) pen.rect(x - std::abs(dy), y + dy, 1, 1, color);
}
static void dots(const Pen& pen, int y, uint32_t color) {
    for (int i = 0; i < 3; ++i) pen.rect(292 + i * 4, y + 3, 2, 2, color);
}
static void row(const ProjectOverlayFrame& frame, const Pen& pen, int i, bool details = true) {
    const auto& item = frame.rows[i]; const int y = kPatchRowY + i * kPatchRowStep;
    const bool selected = item.logical == frame.selected;
    const uint32_t color = selected ? frame.spot : frame.branch;
    pen.rect(20, y + 4, 17, 1, color);
    if (selected) marker(pen, 36, y + 4, color);
    pen.text(44, y, item.name, selected ? frame.spot : frame.ink);
    if (item.current && frame.dirty) pen.text(48 + ui::text_w(item.name), y, "*", frame.spot);
    if (!details) return;
    pen.right(280, y, item.tag, item.current ? frame.spot : frame.ghost);
    dots(pen, y, frame.menu != PatchMenu::None && selected ? frame.spot : selected ? frame.ink : frame.ghost);
}
static void list(const ProjectOverlayFrame& frame, const Pen& pen, bool backdrop = false) {
    if (!pen.cache) menu_style::page_heading(page_layout::Title::Patches, frame.ink);
    else pen.heading(page_layout::kLeft, page_layout::title_y(page_layout::Title::Patches), "patches", frame.ink);
    pen.text(232, 21, "new", frame.ink); pen.text(268, 21, "save", frame.spot);
    pen.rect(266, 34, ui::text_w("save") + 3, 1, frame.spot);
    if (frame.row_count) pen.rect(20, kPatchRowY + 4, 1, (frame.row_count - 1) * kPatchRowStep + 1, frame.branch);
    for (int i = 0; i < frame.row_count; ++i) {
        if (backdrop && frame.rows[i].logical == frame.selected) continue; // sharp invoking anchor is a separate pass
        row(frame, pen, i, !backdrop);
    }
    if (!frame.row_count) pen.text(44, kPatchRowY, frame.catalog_failed ? "library unavailable / sd error" : "no saved patches", frame.ghost);
    if (frame.catalog_failed && frame.row_count) pen.text(44, 205, "library unavailable / sd error", frame.ghost);
    if (!backdrop && frame.total_rows > kPatchRowsPerPage) {
        const int range = (kPatchRowsPerPage - 1) * kPatchRowStep + 8;
        const int height = std::max(8, range * kPatchRowsPerPage / frame.total_rows);
        const int y = kPatchRowY + (range - height) * frame.first / std::max(1, frame.total_rows - kPatchRowsPerPage);
        pen.rect(311, y, 1, height, frame.ghost); // scroll thumb, not a parameter slider
    }
    if (!backdrop) {
        pen.text(20, 228, "a open / x save", frame.ghost);
        pen.right(300, 228, "b close", frame.ghost);
    }
}
static void paper_cache(sigil_surface::Cache& cache) {
    for (int y = 0; y < sigil_surface::H; ++y) for (int x = 0; x < sigil_surface::W; ++x) {
        uint32_t color = 0xff000000u;
        for (unsigned shift : {0u, 8u, 16u}) {
            unsigned sum = 0;
            for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
                sum += (menu_style::patch_sample(false, float(x * 2 + dx), float(y * 2 + dy)) >> shift) & 255;
            color |= ((sum + 2) / 4) << shift;
        }
        cache.pixels[y * sigil_surface::W + x] = color;
    }
}
static void name(const ProjectOverlayFrame& frame) {
    namespace m = menu_style;
    m::paper(320); m::page_heading(page_layout::Title::Patches, frame.ink);
    ui::text(242, 21, frame.renaming ? "rename" : "name", frame.ghost);
    ui::text(20, 52, frame.draft, frame.ink); ui::rect(20, 65, 228, 1, frame.branch);
    ui::rect(20 + ui::text_w(frame.draft), 62, 5, 1, frame.spot);
    char text[16]; std::snprintf(text, sizeof text, "%u/23", unsigned(std::strlen(frame.draft)));
    ui::text(300 - ui::text_w(text), 52, text, frame.ghost);
    for (int i = 0; i < kPatchKeyClear; ++i) {
        const int x = 30 + i % 10 * 28, y = 88 + i / 10 * 24;
        const char key[] = {kPatchKeys[i], 0};
        ui::text(x, y, key, i == frame.key_cursor ? frame.spot : frame.ink);
        if (i == frame.key_cursor) ui::rect(x - 3, y + 11, 12, 1, frame.spot);
    }
    const char* labels[] = {"clear", "space", "del", "done"};
    for (int i = 0; i < 4; ++i) {
        const int x = 14 + i * 74 + (70 - ui::text_w(labels[i])) / 2;
        ui::text(x, 213, labels[i], i + kPatchKeyClear == frame.key_cursor ? frame.spot : frame.ink);
        if (i + kPatchKeyClear == frame.key_cursor) ui::rect(x - 3, 226, ui::text_w(labels[i]) + 6, 1, frame.spot);
    }
    ui::text(20, 184, frame.notice[0] ? frame.notice : "x delete / y space / b cancel", frame.ghost);
}
} // namespace project_library_drawing
static bool patch_library_background_equal(const ProjectOverlayFrame& a, const ProjectOverlayFrame& b) {
    // Compare actual fields, not indeterminate C++ struct padding. Focus100ms and
    // reveal140ms advance without a cache rebuild; the selected anchor stays sharp.
    if (a.current != b.current || a.dirty != b.dirty || a.selected != b.selected ||
        a.selected_slot != b.selected_slot || a.first != b.first || a.row_count != b.row_count ||
        a.total_rows != b.total_rows || a.catalog_failed != b.catalog_failed || a.menu != b.menu ||
        a.ink != b.ink || a.spot != b.spot || a.ghost != b.ghost || a.branch != b.branch ||
        a.paper != b.paper || a.paper_bottom != b.paper_bottom) return false;
    for (int i = 0; i < a.row_count; ++i) {
        const auto& x = a.rows[i]; const auto& y = b.rows[i];
        if (x.slot != y.slot || x.logical != y.logical || x.current != y.current ||
            std::memcmp(x.name, y.name, sizeof x.name) || std::memcmp(x.tag, y.tag, sizeof x.tag)) return false;
    }
    return true;
}
static void project_overlay_capture() {
    // All clocks, dates, short guarded draft-kind capture and CPU cache work are
    // BEFORE FrameBegin. No SD I/O here: inspection/invalidation lives in tick.
    auto& frame = g_project_overlay_frame; frame = {};
    frame.active = !nav_is_open() && (g_overlay == Overlay::PatchBrowser || g_overlay == Overlay::ConfirmDiscard);
    if (!frame.active) { project_overlay_cancel_input(); return; }
    frame.discard = g_overlay == Overlay::ConfirmDiscard;
    frame.current = g_project_session.active_slot(); frame.dirty = g_project_session.dirty();
    frame.selected = g_patch_selection; frame.selected_slot = g_patch_slot;
    frame.exists = project_slot_valid(g_patch_slot) && g_patch_exists[g_patch_slot] && !g_patch_catalog_failed;
    frame.detached = patch_library_detached(); frame.catalog_failed = g_patch_catalog_failed;
    frame.page = g_patch_page; frame.menu = g_patch_menu; frame.pending = g_project_session.pending();
    frame.copying = g_patch_copy; frame.renaming = g_patch_rename && !g_patch_name_draft_only;
    frame.key_cursor = g_patch_key_cursor; frame.menu_count = patch_library_menu_count(); frame.menu_item = g_patch_menu_item;
    frame.ink = menu_style::ink(); frame.spot = menu_style::spot(); frame.ghost = menu_style::ghost(); frame.branch = menu_style::branch();
    frame.paper = menu_style::paper_color(0); frame.paper_bottom = menu_style::paper_color(239);
    std::snprintf(frame.draft, sizeof frame.draft, "%s", g_patch_draft);
    if (project_slot_valid(frame.current)) project_name_label(g_project_names, frame.current, frame.current_name, sizeof frame.current_name);
    else std::snprintf(frame.current_name, sizeof frame.current_name, "%s", g_patch_unsaved_name);
    frame.first = g_patch_first; frame.saved_count = g_patch_order_count; frame.total_rows = patch_library_row_count();
    for (int row = frame.first; row < frame.first + kPatchRowsPerPage && row < frame.total_rows; ++row) {
        auto& item = frame.rows[frame.row_count++]; item.logical = row; item.slot = patch_library_row_slot(row);
        item.current = item.slot == frame.current;
        if (project_slot_valid(item.slot)) {
            project_name_label(g_project_names, item.slot, item.name, sizeof item.name);
            if (item.current) std::snprintf(item.tag, sizeof item.tag, "open");
            else patch_library_date(g_patch_saved_at[item.slot], item.tag, sizeof item.tag, true);
        } else {
            std::snprintf(item.name, sizeof item.name, "%s", g_patch_unsaved_name);
            std::snprintf(item.tag, sizeof item.tag, "not saved");
        }
    }
    if (frame.exists) {
        project_name_label(g_project_names, frame.selected_slot, frame.selected_name, sizeof frame.selected_name);
        patch_library_date(g_patch_saved_at[frame.selected_slot], frame.saved_at, sizeof frame.saved_at, false);
        frame.preview = g_patch_previews[frame.selected_slot]; // saved FILE, never current-engine fallback
    } else if (frame.selected >= 0 && frame.selected_slot < 0 && frame.detached) {
        std::snprintf(frame.selected_name, sizeof frame.selected_name, "%s", g_patch_unsaved_name);
        std::snprintf(frame.saved_at, sizeof frame.saved_at, "not saved yet");
        if (g_eng && g_audio) {
            { platform::AudioOut::Guard guard(*g_audio);
              for (int c = 0; c < NUM_CHAINS; ++c) for (int s = 0; s < UNITS_PER_CHAIN; ++s) {
                  const Unit* unit = g_eng->chains[c].units[s];
                  frame.preview.kinds[c][s] = unit ? uint8_t(unit->kind) : uint8_t(UnitKind::Empty);
              }
            }
            frame.preview.status = patchio::PreviewStatus::Ready;
        }
    } else {
        std::snprintf(frame.selected_name, sizeof frame.selected_name, "%s", frame.catalog_failed ? "library unavailable" : "no saved patches");
        std::snprintf(frame.saved_at, sizeof frame.saved_at, "--");
    }
    if (g_save_flash > 0) std::snprintf(frame.notice, sizeof frame.notice, "%s", g_patch_notice);
    for (int i = 0; i < frame.menu_count; ++i) std::snprintf(frame.labels[i], sizeof frame.labels[i], "%s", patch_library_menu_label(i));
    frame.layout = patch_library_menu_layout();
    ProjectOverlayFrame background = frame;
    background.motion = {}; background.menu_item = 0;
    // Focus motion does not rebuild/upload a backdrop. Context/palette/name/date,
    // viewport or disclosure-state changes do. GO TO reuses this same cache, so
    // tick/reset force refresh whenever PATCHES regains ownership.
    if (frame.menu != PatchMenu::None && frame.page == PatchPage::List &&
        (g_patch_cache_dirty || !patch_library_background_equal(background, g_patch_cached_background))) {
        auto& cache = sigil_surface::bottom_cache;
        project_library_drawing::paper_cache(cache);
        project_library_drawing::list(frame, sigil_surface::Painter{&cache}, true);
        cache.finish(); g_patch_cached_background = background;
    }
    const uint64_t now = osGetTime();
    frame.motion = word_menu::capture(g_patch_word_state, now,
        frame.menu != PatchMenu::None ? g_patch_menu_identity : 0, frame.menu_item, frame.layout,
        [&](int item) { return frame.labels[std::clamp(item, 0, 3)]; }, g_patch_menu_opening);
    g_patch_menu_opening = g_patch_cache_dirty = false;
}
void patch_browser_draw() {
    const auto& frame = g_project_overlay_frame;
    if (!frame.active) return;
    if (frame.page == PatchPage::Name) { project_library_drawing::name(frame); return; }
    const sigil_surface::Painter pen{};
    if (frame.menu == PatchMenu::None) {
        menu_style::paper(320); project_library_drawing::list(frame, pen); return;
    }
    if (!sigil_surface::bottom_cache.draw()) {
        menu_style::paper(320); project_library_drawing::list(frame, pen, true);
        menu_style::paper_rect(0, 0, 320, 240, 150); // existing material fallback, not a new cache/panel
    }
    for (int i = 0; i < frame.row_count; ++i)
        if (frame.rows[i].logical == frame.selected) project_library_drawing::row(frame, pen, i);
    word_menu::draw(pen, frame.layout, frame.motion, frame.menu_count, frame.menu_item,
        [&](int item) { return frame.labels[item]; }, [](int, int, unsigned, bool) {}, frame.ink, frame.spot, frame.branch);
    pen.text(20, 228, "a choose", frame.ghost); pen.right(300, 228, "b close", frame.ghost);
}
void confirm_discard_draw() { patch_browser_draw(); } // same list/component, NOT an unsaved page
static void patch_library_ellipsis(char* out, size_t size, const char* source, int pixels, int scale = 1) {
    if (!size) return;
    const size_t capacity = std::min(size - 1, size_t(std::max(0, pixels / (6 * scale))));
    const size_t length = std::strlen(source), end = std::min(length, capacity);
    std::memcpy(out, source, end); out[end] = 0;
    if (length <= capacity) return;
    if (end >= 3) out[end - 3] = out[end - 2] = out[end - 1] = '.';
}
static void patch_library_chain_text(const uint8_t* kinds, char* out, size_t size) {
    size_t used = 0; if (size) out[0] = 0;
    for (int slot = 0; slot < UNITS_PER_CHAIN; ++slot) {
        if (kinds[slot] == uint8_t(UnitKind::Empty)) continue;
        const char* name = unit_kind_available(UnitKind(kinds[slot])) ? unit_kind_name(UnitKind(kinds[slot])) : "?";
        const int n = std::snprintf(out + used, size - used, "%s%s", used ? "  " : "", name);
        if (n < 0 || size_t(n) >= size - used) break;
        used += size_t(n);
    }
    if (!used && size) std::snprintf(out, size, "-");
    for (char* p = out; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
}
static bool project_overlay_draw_top() {
    const auto& frame = g_project_overlay_frame;
    if (!frame.active) return false;
    menu_style::paper(400); menu_style::page_heading(page_layout::Title::Patches, frame.ink);
    char text[64];
    patch_library_ellipsis(text, sizeof text, frame.selected_name, 360, 2);
    ui::text(20, 58, text, frame.selected_slot == frame.current ? frame.spot : frame.ink, 2);
    ui::text(20, 84, frame.saved_at, frame.ghost);
    ui::rect(20, 116, 1, 3 * 22 + 1, frame.branch);
    for (int chain = 0; chain < NUM_CHAINS; ++chain) {
        const int y = 112 + chain * 22;
        ui::rect(20, y + 4, 13, 1, frame.branch);
        char number[4]; std::snprintf(number, sizeof number, "%d", chain + 1);
        ui::text(44, y, number, frame.ghost);
        if (frame.preview.status == patchio::PreviewStatus::Ready) {
            char complete[64]; patch_library_chain_text(frame.preview.kinds[chain], complete, sizeof complete);
            patch_library_ellipsis(text, sizeof text, complete, 316);
        } else std::snprintf(text, sizeof text, "--");
        ui::text(64, y, text, text[0] == '-' ? frame.ghost : frame.ink);
    }
    if (frame.notice[0]) ui::text(20, 205, frame.notice, frame.spot);
    else if (frame.catalog_failed) ui::text(20, 205, "library unavailable / sd error", frame.ghost);
    else if (frame.preview.status != patchio::PreviewStatus::Ready)
        ui::text(20, 205, frame.preview.status == patchio::PreviewStatus::Unsupported ? "preview unsupported / check file" :
            frame.preview.status == patchio::PreviewStatus::Invalid ? "preview invalid / check file" : "preview unavailable / check file", frame.ghost);
    if (frame.selected_slot != frame.current || frame.selected < 0)
        std::snprintf(text, sizeof text, "open now / %.23s%s", frame.current_name, frame.dirty ? " *" : "");
    else std::snprintf(text, sizeof text, "%s", frame.detached ? "not saved yet" : frame.dirty ? "open now / unsaved changes" : "open now / saved");
    ui::text(20, 228, text, frame.ghost);
    return true;
}
