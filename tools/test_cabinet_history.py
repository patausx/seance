#!/usr/bin/env python3
"""Scoped cabinet history proof; temporary files only, no SD/ARM/DSP/UI build.

Store tests use actual serialization and filesystem operations. Main integration
uses exact remember/startup/save/name/delete functions and the exact load branch;
SNC transactions, recorder/engine/catalogue/audio boundaries are explicit fakes.
This tests history call ordering, not the underlying patch transaction or UI.
"""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(text, name):
    match = re.search(r"^(?:static )?[\w:*&<>]+ " + re.escape(name)
                      + r"\([^)]*\) \{.*?^}", text, re.M | re.S)
    assert match, name
    return match.group()


def block(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


PREFIX = r'''
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "platform/3ds/start_menu_store.h"
#include "platform/3ds/project_names.h"
#include "core/engine/project_session.h"
using namespace seance;
using namespace seance::platform::ui3ds;

static uint64_t clock_now = 10000;
uint64_t start_timestamp() { return clock_now; }
StartMenuStore g_start_store;
bool g_start_store_write_failed = false;
const char* START_MENU_PATH = "start_menu.bin";
const char* PROJECT_NAMES_PATH = "project_names.bin";
ProjectSession g_project_session;
ProjectNames g_project_names;
bool g_patch_exists[PROJECT_SLOTS] = {};
bool g_patch_valid[PROJECT_SLOTS] = {};
bool g_patch_checked[PROJECT_SLOTS] = {};
uint64_t g_patch_saved_at[PROJECT_SLOTS] = {};
int legacy_slot = -1, scans = 0, refreshes = 0;
void scan_patch_slots() { ++scans; }
int load_last_patch_slot() { return legacy_slot; }
void refresh_patch_slot(int slot) { assert(project_slot_valid(slot)); ++refreshes; }
void patch_path(int slot, char* out, size_t n) { std::snprintf(out, n, "patch%d.snc", slot); }

// Deliberately minimal save/load boundary spies: no engine, DSP or SNC linked.
constexpr int NUM_CHAINS = 1, UNITS_PER_CHAIN = 1;
enum class UnitKind { None, Recorder };
struct Unit { UnitKind kind = UnitKind::None; };
struct RecUnit : Unit { bool is_recording() const { return false; } bool take_needs_save() const { return false; } };
struct Engine { struct Chain { Unit* units[1] = {}; } chains[1]; };
static Engine engine;
Engine* g_eng = &engine;
namespace seance::platform {
struct AudioOut { struct Guard { explicit Guard(AudioOut&) {} }; };
}
static platform::AudioOut audio;
platform::AudioOut* g_audio = &audio;
struct WorkspaceLayout {};
void workspace_get_layout(WorkspaceLayout&) {}
bool ready = true, save_ok = true, capture_ok = true, document_ok = true;
bool fail_ready_after_save = false, load_ok = true;
int patch_writes = 0, load_attempts = 0, exports = 0;
bool project_recordings_ready(bool) { return ready; }
int g_edit_chain = 0, g_edit_slot = 0;
bool g_export_ok = true;
void export_current_sample() { ++exports; }
static const char* g_patch_save_error = nullptr;
namespace patchio {
struct PatchDocument { bool valid() const { return document_ok; } };
bool capture(Engine&, PatchDocument&, WorkspaceLayout*) { return capture_ok; }
bool save(PatchDocument&, const char*) {
    ++patch_writes;
    if (save_ok && fail_ready_after_save) ready = false;
    return save_ok;
}
}
bool transactional_patch_load(const char*) { ++load_attempts; return load_ok; }
enum class PatchAction { Load, Save, Delete, Rename, NewProject };
enum class Overlay { None, PatchBrowser };
Overlay g_overlay = Overlay::None;
struct MenuAudio { int enters = 0; void enter_patch() { ++enters; } };
MenuAudio menu_audio;
MenuAudio* g_menu_audio = &menu_audio;
bool g_session_started = false;
int last_result_slot = -1;
bool last_result_ok = false;
void patch_browser_result(bool ok, PatchAction, int slot = -1) { last_result_ok = ok; last_result_slot = slot; }

// Only these filesystem wrappers fail, never the project's actual files.
static bool fail_commit = false;
extern "C" int __real_rename(const char*, const char*);
extern "C" int __wrap_rename(const char* from, const char* to) {
    const size_t n = std::strlen(from);
    if (fail_commit && n >= 4 && !std::strcmp(from + n - 4, ".tmp")) {
        fail_commit = false; errno = EACCES; return -1;
    }
    return __real_rename(from, to);
}

enum class PatchPage { List, Destination };
PatchPage g_patch_page = PatchPage::List;
int g_patch_slot = 0, g_patch_name_slot = 0;
int g_patch_confirm_slot = -1, g_patch_delete_confirm_slot = -1;
bool g_patch_rename = false, g_patch_name_file_saved = false, g_patch_replace_approved = false;
char g_patch_draft[PROJECT_NAME_BYTES] = {};
void patch_library_back() {}
void patch_library_name(bool) {}
void request_destructive(DestructiveAction) { assert(false); }
void patch_library_notice(const char*) {}
'''

TESTS = r'''
static std::vector<uint8_t> file_bytes(const char* path) {
    std::ifstream file(path, std::ios::binary); assert(file);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void same(const StartMenuStore& a, const StartMenuStore& b) {
    uint8_t x[START_STORE_BYTES], y[START_STORE_BYTES];
    assert(start_store_encode(a, x) && start_store_encode(b, y));
    assert(!std::memcmp(x, y, sizeof x));
}
static void reset() {
    g_start_store = {}; g_start_store_write_failed = false;
    g_project_session = {}; g_project_names = {};
    std::fill_n(g_patch_exists, PROJECT_SLOTS, false);
    std::fill_n(g_patch_valid, PROJECT_SLOTS, false);
    std::fill_n(g_patch_checked, PROJECT_SLOTS, false);
    std::fill_n(g_patch_saved_at, PROJECT_SLOTS, 0);
    scans = refreshes = patch_writes = load_attempts = exports = 0;
    legacy_slot = -1; clock_now = 10000;
    ready = save_ok = capture_ok = document_ok = load_ok = true;
    fail_ready_after_save = false; g_session_started = false;
    g_overlay = Overlay::None; g_patch_slot = g_patch_name_slot = 0;
    g_patch_confirm_slot = g_patch_delete_confirm_slot = -1;
    g_patch_rename = g_patch_name_file_saved = g_patch_replace_approved = false;
    g_patch_draft[0] = 0; g_patch_page = PatchPage::List;
    for (int slot = 1; slot <= 8; ++slot) {
        start_store_touch(g_start_store, slot, 1000 + slot);
        g_patch_exists[slot] = g_patch_valid[slot] = true;
    }
    assert(start_store_save(START_MENU_PATH, g_start_store));
}
static void check_startup() {
    reset();
    g_start_store.recent[0].opened = false; // v1-v3-style, raw nonzero stamp
    g_start_store.recent[1].used_at = clock_now + 1; // future clock
    g_start_store.recent[2].used_at = 0; // successful open, unknown clock
    g_start_store.recent[3].used_at = clock_now - 60;
    g_patch_exists[127] = true; // unknown checkedness is still offered
    for (int slot = 0; slot < PROJECT_SLOTS; ++slot) g_patch_saved_at[slot] = clock_now;
    const auto before = g_start_store;
    const auto data = startup_data();
    const int capacity = int(sizeof data.recent_slots / sizeof data.recent_slots[0]);
    assert(capacity == 5);
    assert(data.recent_count == capacity && data.recent_slots[0] == 8);
    for (int i = 0; i < capacity; ++i) assert(data.recent_slots[i] == 8 - i);
    assert(!std::strcmp(data.projects[8].age, "--"));
    assert(!std::strcmp(data.projects[7].age, "--"));
    assert(!std::strcmp(data.projects[6].age, "--"));
    assert(!std::strcmp(data.projects[5].age, "1 min"));
    assert(!std::strcmp(data.projects[127].age, "--") && data.projects[127].valid);
    same(before, g_start_store);
    for (int i = 0; i < 8; ++i) start_store_forget(g_start_store, i + 1);
    legacy_slot = 127;
    const auto fallback = startup_data();
    assert(fallback.recent_slots[0] == 127 && fallback.recent_slots[1] == 1);
    for (int slot = 0; slot < PROJECT_SLOTS; ++slot) g_patch_saved_at[slot] = UINT64_MAX - slot;
    const auto resaved = startup_data();
    for (int i = 0; i < capacity; ++i) assert(fallback.recent_slots[i] == resaved.recent_slots[i]);
    for (const auto& p : resaved.projects) assert(!std::strcmp(p.age, "--"));
    g_patch_checked[127] = true; g_patch_valid[127] = false;
    const auto invalid = startup_data();
    assert(!invalid.projects[127].valid && invalid.recent_slots[0] == 1);
    std::puts("main startup: actual five-row array, unknown/legacy/future/zero ages '--', no mtime provenance/order, unchecked/invalid filtering passed");
}
static void check_open() {
    reset();
    g_project_session.reset_loaded(2);
    const auto before = g_start_store;
    const auto disk = file_bytes(START_MENU_PATH);
    load_ok = false;
    assert(!execute_destructive({DestructiveAction::LoadProject, 3}));
    assert(load_attempts == 1 && g_project_session.active_slot() == 2);
    same(before, g_start_store); assert(file_bytes(START_MENU_PATH) == disk);
    assert(!execute_destructive({DestructiveAction::LoadProject, -1}));
    g_patch_valid[4] = false;
    assert(!execute_destructive({DestructiveAction::LoadProject, 4}) && load_attempts == 1);
    same(before, g_start_store);
    load_ok = true; g_session_started = true;
    assert(execute_destructive({DestructiveAction::LoadProject, 3}));
    assert(g_start_store.recent[0].slot == 3 && g_start_store.recent[0].opened && g_start_store.recent[0].used_at == clock_now);
    assert(g_project_session.active_slot() == 3 && menu_audio.enters == 1);
    StartMenuStore loaded;
    assert(start_store_load(START_MENU_PATH, loaded)); same(loaded, g_start_store);
    // Exact explicit startup-resume body uses the same successful load path.
    clock_now += 3600; g_session_started = false;
    assert(resume_for_test());
    assert(g_start_store.recent[0].slot == 3 && g_start_store.recent[0].used_at == clock_now);
    const auto resume = g_start_store;
    load_ok = false; clock_now += 100;
    assert(!resume_for_test()); same(resume, g_start_store);
    // A failed sidecar save keeps the real open in memory and reports failure;
    // rollback preserves the preceding primary rather than a partial new file.
    load_ok = true; fail_commit = true;
    const auto prior_disk = file_bytes(START_MENU_PATH);
    assert(execute_destructive({DestructiveAction::LoadProject, 5}));
    assert(g_start_store_write_failed && g_start_store.recent[0].slot == 5);
    assert(file_bytes(START_MENU_PATH) == prior_disk);
    std::puts("main open: failed/invalid loads do not touch; manual and exact startup-resume success touch+persist; failed history commit rollback passed");
}
static void check_saves_and_names() {
    reset(); g_project_session.reset_loaded(3);
    const auto before = g_start_store; const auto disk = file_bytes(START_MENU_PATH);
    clock_now += 999;
    assert(save_current_patch(3));
    same(before, g_start_store); assert(file_bytes(START_MENU_PATH) == disk);
    assert(g_project_session.active_slot() == 3 && patch_writes == 1);
    // Metadata-only rename never calls SNC save or changes raw/trusted history.
    g_patch_name_slot = 3; g_patch_rename = true;
    std::strcpy(g_patch_draft, "renamed"); patch_library_name_done();
    assert(patch_writes == 1 && !std::strcmp(g_project_names.names[3], "renamed"));
    same(before, g_start_store); assert(file_bytes(START_MENU_PATH) == disk);
    // Failure before/at SNC commit must not remove destination history.
    save_ok = false; assert(!save_current_patch(4)); same(before, g_start_store);
    save_ok = true; capture_ok = false; assert(!save_current_patch(4)); same(before, g_start_store);
    capture_ok = true; document_ok = false; assert(!save_current_patch(4)); same(before, g_start_store);
    document_ok = true; ready = false; assert(!save_current_patch(4)); same(before, g_start_store);
    ready = true;
    assert(save_current_patch(4));
    auto expected = before; start_store_forget(expected, 4); same(expected, g_start_store);
    assert(g_project_session.active_slot() == 4);
    assert(save_current_patch(4)); same(expected, g_start_store);
    StartMenuStore loaded; assert(start_store_load(START_MENU_PATH, loaded)); same(expected, loaded);
    // New session writing a formerly used slot has no last-open provenance.
    g_project_session.reset_new(); assert(save_current_patch(5));
    start_store_forget(expected, 5); same(expected, g_start_store);
    // The destination changed even when post-commit REC check rejects leave.
    g_project_session.reset_loaded(3); fail_ready_after_save = true;
    assert(!save_current_patch(6) && g_project_session.active_slot() == 3);
    start_store_forget(expected, 6); same(expected, g_start_store);
    assert(start_store_load(START_MENU_PATH, loaded)); same(expected, loaded);
    assert(!std::strcmp(g_patch_save_error, "rec changed / retry save"));
    std::puts("main save/name: same-slot save+rename byte-exact history, failed save no refresh, copy/new destination forget, post-commit REC rejection forget passed");
}
static void check_delete() {
    reset(); g_project_session.reset_loaded(7); g_patch_slot = 7;
    const auto before = g_start_store;
    std::remove("patch7.snc");
    patch_library_more(2); // confirmation only
    same(before, g_start_store);
    patch_library_more(2); // missing file, failed deletion
    same(before, g_start_store); assert(g_project_session.active_slot() == 7);
    { std::ofstream f("patch7.snc"); f << "fixture-not-an-snc"; }
    patch_library_more(2); patch_library_more(2);
    auto expected = before; start_store_forget(expected, 7); same(expected, g_start_store);
    assert(g_project_session.active_slot() == -1 && !std::filesystem::exists("patch7.snc"));
    StartMenuStore loaded; assert(start_store_load(START_MENU_PATH, loaded)); same(expected, loaded);
    assert(save_current_patch(7)); same(expected, g_start_store);
    std::puts("main deletion: confirmation/failed deletion no touch, successful delete clears+persists and detaches, slot reuse cannot inherit age passed");
}
int main() {
    check_startup(); check_open(); check_saves_and_names(); check_delete();
}
'''


def main():
    runtime = (ROOT / "platform/3ds/main.cpp").read_text()
    library = (ROOT / "platform/3ds/project_library_ui.inc").read_text()
    header = (ROOT / "platform/3ds/ui_start_menu.h").read_text()
    remembered = function(runtime, "remember_project")
    saver = function(runtime, "save_current_patch")
    executor = function(runtime, "execute_destructive")
    load_branch = block(executor, "    if (pending.action == DestructiveAction::LoadProject) {")
    assert len(re.findall(r"\bremember_project\s*\(", runtime)) == 2  # declaration + load only
    assert "remember_project(" not in saver
    assert "start_store_touch(" not in saver
    assert runtime.count("start_store_touch(") == 1
    assert remembered.count("start_store_touch(") == 1
    assert load_branch.index("if (ok)") < load_branch.index("remember_project(slot)")
    assert "transactional_patch_load(path)" in load_branch
    assert executor.index("bounce_guard_project_change()") < executor.index(load_branch)
    assert runtime.count("const bool ok = transactional_patch_load(path);") == 1
    assert saver.index("const bool ok = patchio::save(") < saver.index("start_store_forget(") < saver.index("g_project_session.mark_saved(")
    assert "start_store_" not in function(library, "patch_library_name_done")
    resume = block(runtime, "    } else if (!force_menu && !g_start_store.show_menu) {")
    # block() includes the leading prior '}', so extract only inside its '{'.
    resume = resume[resume.index("{") + 1:-1]
    assert "execute_destructive({DestructiveAction::LoadProject, slot})" in resume
    actual_types = []
    for name in ("StartProject", "StartMenuData"):
        match = re.search(r"^struct " + name + r" \{.*?^};", header, re.M | re.S)
        assert match, name
        actual_types.append(match.group())
    constants = re.findall(r"^constexpr int kStartRecentCount\s*=\s*\d+;", header, re.M)
    pieces = [PREFIX, *constants, *actual_types,
              function(runtime, "save_project_names"), remembered, saver,
              "bool execute_destructive(PendingDestructiveAction pending) {\n" + load_branch + "\nreturn false;\n}",
              function(runtime, "startup_data"),
              "bool resume_for_test() { bool project_ready = false;\n" + resume + "\nreturn project_ready;\n}",
              function(library, "patch_library_name_done"), function(library, "patch_library_more"), TESTS]
    fixture = "\n".join(pieces)
    env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1", "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    with tempfile.TemporaryDirectory(prefix="seance-cabinet-history-") as work:
        work = Path(work)
        source = work / "main-history.cpp"
        source.write_text(fixture)
        flags = ["g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie", "-I", str(ROOT)]
        for name, inputs in (
            ("store", [str(ROOT / "tools/start_menu_store_test.cpp"), str(ROOT / "platform/3ds/start_menu_store.cpp")]),
            ("main-history", [str(source), str(ROOT / "platform/3ds/start_menu_store.cpp"),
                              str(ROOT / "platform/3ds/project_names.cpp"), str(ROOT / "core/engine/project_session.cpp"),
                              "-Wl,--wrap=rename"]),
        ):
            binary = work / name
            subprocess.run([*flags, *inputs, "-o", str(binary)], cwd=work, env=env, check=True)
            subprocess.run([str(binary)], cwd=work, env=env, check=True)
        print("cabinet history: scoped store/main-history ASAN+UBSAN+leaks passed; no engine/DSP/UI/hardware guarantee")
        for path in ("platform/3ds/start_menu_store.h", "platform/3ds/start_menu_store.cpp", "platform/3ds/main.cpp",
                     "platform/3ds/project_library_ui.inc", "platform/3ds/ui_start_menu.h", "tools/start_menu_store_test.cpp"):
            print(hashlib.sha256((ROOT / path).read_bytes()).hexdigest(), path)
        print(hashlib.sha256(fixture.encode()).hexdigest(), "extracted-main-history-fixture")


if __name__ == "__main__":
    main()
