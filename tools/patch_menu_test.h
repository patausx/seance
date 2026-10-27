// Focused 128-project controller/render regression. Exact main private include;
// only filesystem save/delete/load outcomes are shimmed here. Real names/session.
#include "../core/engine/project_session.h"
#include "../platform/3ds/project_names.h"
#include <ctime>

namespace seance::platform::ui3ds {
void scan_patch_slots();
void refresh_patch_slot(int);
bool save_current_patch(int);
bool execute_destructive(PendingDestructiveAction);
bool save_project_names(const ProjectNames&);
void patch_path(int, char*, size_t);
int remove(const char*);
void start_store_forget(int&, int);
bool start_store_save(const char*, int);
int g_start_store = 0;
bool g_start_store_write_failed = false;
const char* START_MENU_PATH = "host-outcome-shim";
static const char* g_patch_save_error = nullptr;
#include <patch_menu_main.inc>

int patch_test_saves = 0, patch_test_deletes = 0, patch_test_scans = 0, patch_test_execs = 0;
int patch_test_names = 0, patch_test_saved_slot = -1, patch_test_forgotten_slot = -1;
bool patch_test_save_ok = true, patch_test_delete_ok = true, patch_test_name_ok = true;
PendingDestructiveAction patch_test_executed;
int patch_test_refreshes = 0, patch_test_refreshed_slot = -1, patch_test_disk_override_slot = -1;
bool patch_test_disk_exists = false;
void scan_patch_slots() { if (!g_patch_catalog_ready) { ++patch_test_scans; g_patch_catalog_ready = true; } }
void refresh_patch_slot(int slot) {
    CHECK(project_slot_valid(slot));
    ++patch_test_refreshes; patch_test_refreshed_slot = slot; g_patch_checked[slot] = true;
    if (slot == patch_test_disk_override_slot)
        g_patch_exists[slot] = g_patch_valid[slot] = patch_test_disk_exists;
}
bool save_current_patch(int slot) {
    CHECK(project_slot_valid(slot));
    ++patch_test_saves; patch_test_saved_slot = slot;
    if (patch_test_save_ok) {
        g_patch_exists[slot] = g_patch_valid[slot] = true;
        g_project_session.mark_saved(slot);
    }
    return patch_test_save_ok;
}
bool save_project_names(const ProjectNames& names) {
    ++patch_test_names;
    if (patch_test_name_ok) g_project_names = names;
    return patch_test_name_ok;
}
bool execute_destructive(PendingDestructiveAction pending) {
    ++patch_test_execs; patch_test_executed = pending; g_overlay = Overlay::None;
    return true; // actual SNC/PCM transactional load is unchanged, not modelled
}
int remove(const char* path) {
    int slot = -1; CHECK(std::sscanf(path, "sdmc:/3ds/seance/patch%d.snc", &slot) == 1 && project_slot_valid(slot));
    ++patch_test_deletes;
    if (patch_test_delete_ok) g_patch_exists[slot] = g_patch_valid[slot] = false;
    return patch_test_delete_ok ? 0 : -1;
}
void start_store_forget(int&, int slot) { patch_test_forgotten_slot = slot; }
bool start_store_save(const char*, int) { return true; }
} // namespace seance::platform::ui3ds

namespace {
namespace patch_dispatch_boundary {
bool clock_open = false, nav_open = false;
bool nav_clock_top_active() { return clock_open; }
bool nav_is_open() { return nav_open; }
void nav_clock_draw_top() { m::paper(400); ui::text(20,20,"clock boundary",m::ink()); }
void nav_draw() { m::paper(320); ui::text(20,20,"nav boundary",m::ink()); }
void bounce_draw() { CHECK(false); }
void patchbay_draw() { CHECK(false); }
void bounce_draw_top() { CHECK(false); }
void bounce_draw_indicator() {}
void top_draw() { CHECK(false && "hidden top must not draw"); }
void chains_view_draw() { CHECK(false); }
void sequencer_view_draw() { CHECK(false); }
void performance_view_draw() { CHECK(false); }
void sigils_view_draw() { CHECK(false); }
void picker_draw() { CHECK(false); }
void editor_draw() { CHECK(false); }
void sample_browser_draw() { CHECK(false); }
#include <patch_menu_dispatch.inc>
}
namespace patch_menu_test {
int peak_objects = 0, render_count = 0, peak_pair_objects = 0, pair_count = 0;
void reset() {
    g_project_session.reset_new(); g_patch_slot = 0; g_overlay = Overlay::PatchBrowser;
    g_project_names = {};
    g_patch_catalog_ready = true; g_patch_catalog_failed = false;
    g_save_flash = 0; g_patch_notice[0] = 0; g_patch_save_error = nullptr;
    std::fill(std::begin(g_patch_exists), std::end(g_patch_exists), false);
    std::fill(std::begin(g_patch_valid), std::end(g_patch_valid), false);
    std::fill(std::begin(g_patch_saved_at), std::end(g_patch_saved_at), 0);
    std::fill(std::begin(g_patch_checked), std::end(g_patch_checked), true);
    patch_library_reset();
    patch_test_refreshes = 0; patch_test_refreshed_slot = patch_test_disk_override_slot = -1;
    patch_test_disk_exists = false;
    patch_test_saves = patch_test_deletes = patch_test_scans = patch_test_execs = patch_test_names = 0;
    patch_test_saved_slot = patch_test_forgotten_slot = -1;
    patch_test_save_ok = patch_test_delete_ok = patch_test_name_ok = true; patch_test_executed = {};
    g_eng = nullptr; g_audio = nullptr;
    patch_dispatch_boundary::clock_open = patch_dispatch_boundary::nav_open = false;
}
void seed() {
    reset();
    for (int slot : {0,2,127}) g_patch_exists[slot] = g_patch_valid[slot] = true;
    CHECK(project_name_set(g_project_names,0,"night bus"));
    CHECK(project_name_set(g_project_names,2,"glass choir"));
    CHECK(project_name_set(g_project_names,127,"last save"));
    g_patch_slot = 2; g_project_session.reset_loaded(0); g_project_session.mark_mutated();
}
Frame frame(bool discard = false) {
    project_overlay_capture();
    auto image = render(320, discard ? confirm_discard_draw : patch_browser_draw);
    palette(image); paper_edges(image);
    for (const auto& t : image.texts) CHECK(t.x >= 0 && t.y >= 0 && t.x+t.w <= 320 && t.y+t.h <= 240);
    peak_objects = std::max(peak_objects, image.objects); ++render_count;
    return image;
}
struct Pair { Frame top, bottom; };
Pair pair() {
    project_overlay_capture();
    const auto frozen = g_project_overlay_frame;
    const auto rev = g_project_session.revision();
    const int scans = patch_test_scans, saves = patch_test_saves, names = patch_test_names;
    const int refreshes = patch_test_refreshes;
    auto top = render(400, patch_dispatch_boundary::draw_top);
    auto bottom = render(320, patch_dispatch_boundary::draw_bottom);
    for (const auto* image : {&top, &bottom}) {
        palette(*image); paper_edges(*image);
        for (const auto& t : image->texts) CHECK(t.x >= 0 && t.y >= 0 && t.x+t.w <= image->width && t.y+t.h <= 240);
    }
    CHECK(scans == patch_test_scans && refreshes == patch_test_refreshes && saves == patch_test_saves && names == patch_test_names);
    CHECK(rev == g_project_session.revision() && !std::memcmp(&frozen,&g_project_overlay_frame,sizeof frozen));
    CHECK(top.objects + bottom.objects < 8192);
    peak_pair_objects = std::max(peak_pair_objects, top.objects + bottom.objects); ++pair_count;
    return {top,bottom};
}
void project_tap(int x, int y) {
    project_overlay_touch_down(x, y);
    project_overlay_touch_release(true);
}
void type_name(const char* text) {
    CHECK(g_patch_page == PatchPage::Name);
    project_tap(30,216); // clear
    for (const char* p = text; *p; ++p) {
        if (*p == ' ') { patch_browser_buttons(KEY_Y); continue; }
        const char* at = std::strchr(kPatchKeys,*p); CHECK(at);
        const int key = int(at-kPatchKeys);
        project_tap(30+key%10*28,90+key/10*24);
    }
    CHECK(!std::strcmp(text,g_patch_draft));
}
void controls() {
    reset(); char path[96]; patch_path(127,path,sizeof path); CHECK(!std::strcmp(path,"sdmc:/3ds/seance/patch127.snc"));
    patch_path(7,path,sizeof path); CHECK(!std::strcmp(path,"sdmc:/3ds/seance/patch7.snc"));
    patch_path(128,path,sizeof path); CHECK(!path[0]);
    for (int page : {0,6,60,126}) for (int row = 0; row < 6 && page+row < PROJECT_SLOTS; ++row) {
        g_patch_slot=page; g_patch_confirm_slot=127;
        project_tap(80,44+row*22);
        CHECK(g_patch_slot==page+row && g_patch_confirm_slot==-1);
    }
    g_patch_slot=127; patch_browser_buttons(KEY_R); CHECK(g_patch_slot==127);
    patch_browser_buttons(KEY_L); CHECK(g_patch_slot==121);
    g_patch_slot=0; patch_browser_buttons(KEY_UP); CHECK(g_patch_slot==0);
    patch_browser_buttons(KEY_DOWN); CHECK(g_patch_slot==1);
    CHECK(patch_test_saves==0 && patch_test_execs==0);
    // A and touch share the open action, without synthesizing screen coordinates.
    for (bool touch : {false, true}) {
        seed();
        if (touch) project_tap(50,216); else patch_browser_buttons(KEY_A);
        CHECK(g_overlay==Overlay::ConfirmDiscard && patch_test_execs==0);
        CHECK(g_project_session.pending().action==DestructiveAction::LoadProject && g_project_session.pending().slot==2);
        reset();
        if (touch) project_tap(50,216); else patch_browser_buttons(KEY_A);
        CHECK(g_overlay==Overlay::PatchBrowser && patch_test_execs==0 && patch_test_refreshes==0);
        CHECK(!std::strcmp(g_patch_notice,"load failed for p.001"));
    }
    // Result actions keep their target, toast duration and single-entry IO policy.
    g_patch_slot=127;
    patch_browser_result(false,PatchAction::Save);
    CHECK(!std::strcmp(g_patch_notice,"save failed for p.128") && g_save_flash==120);
    CHECK(patch_test_refreshes==1 && patch_test_refreshed_slot==127);
    patch_browser_result(true,PatchAction::Rename,2);
    CHECK(!std::strcmp(g_patch_notice,"renamed p.003") && patch_test_refreshes==1);
    patch_browser_result(false,PatchAction::NewProject);
    CHECK(!std::strcmp(g_patch_notice,"new project failed") && patch_test_refreshes==1);
    seed(); patch_browser_buttons(KEY_X); CHECK(g_patch_confirm_slot==0 && patch_test_saves==0 && g_patch_slot==2);
    CHECK(text_has(pair().top,"replace 001? / save again"));
    patch_browser_buttons(KEY_X); CHECK(patch_test_saved_slot==0 && !g_project_session.dirty());
    CHECK(text_has(pair().top,"saved to p.001"));
    seed(); u32 down=KEY_A|KEY_X|KEY_TOUCH; bool skip=false;
    g_project_session.mark_saved(0);
    patch_browser_route_buttons(down,skip,Overlay::PatchBrowser);
    CHECK(skip && down==KEY_TOUCH && patch_test_execs==1 && patch_test_saves==0);
    for (u32 edge : {KEY_B,KEY_START,KEY_SELECT}) {
        seed(); down=edge|KEY_X|KEY_TOUCH; skip=false;
        patch_browser_route_buttons(down,skip,Overlay::PatchBrowser);
        CHECK(skip && down==KEY_TOUCH && patch_test_saves==0);
    }
    seed(); project_tap(270,21); CHECK(g_patch_page==PatchPage::More);
    patch_library_back(); CHECK(g_patch_page==PatchPage::List && g_overlay==Overlay::PatchBrowser);
    patch_library_back(); CHECK(g_overlay==Overlay::None);
}
void naming_and_failures() {
    seed(); project_tap(250,216);
    CHECK(g_patch_page==PatchPage::Destination && g_patch_slot==1 && g_patch_copy);
    patch_browser_buttons(KEY_A); CHECK(g_patch_page==PatchPage::Name && patch_test_saves==0);
    type_name("soft rain"); patch_test_name_ok=false;
    project_tap(270,216);
    CHECK(patch_test_saves==1 && g_project_session.active_slot()==1 && g_patch_page==PatchPage::Name);
    CHECK(g_patch_name_file_saved && !g_project_names.names[1][0]);
    patch_test_name_ok=true; project_tap(270,216);
    CHECK(patch_test_saves==1 && g_patch_page==PatchPage::List && !std::strcmp(g_project_names.names[1],"soft rain"));
    CHECK(!std::strcmp(g_project_names.names[0],"night bus"));
    // Rename is metadata-only, including a rejected write and retry.
    g_patch_slot=127; project_tap(270,21); project_tap(70,108);
    CHECK(g_patch_rename && g_patch_page==PatchPage::Name);
    const auto revision=g_project_session.revision(); type_name("renamed");
    patch_test_name_ok=false; project_tap(270,216);
    CHECK(!std::strcmp(g_project_names.names[127],"last save"));
    patch_test_name_ok=true; project_tap(270,216);
    CHECK(!std::strcmp(g_project_names.names[127],"renamed") && revision==g_project_session.revision() && patch_test_saves==1);
    // Full library proposes no imaginary free slot. Replace needs explicit second action.
    std::fill(std::begin(g_patch_exists),std::end(g_patch_exists),true);
    project_tap(250,216); CHECK(g_patch_page==PatchPage::Destination);
    g_patch_slot=127; patch_browser_buttons(KEY_X); CHECK(g_patch_page==PatchPage::Destination && g_patch_confirm_slot==127);
    patch_browser_buttons(KEY_X); CHECK(g_patch_page==PatchPage::Name && g_patch_replace_approved);
    patch_test_save_ok=false; type_name("replacement"); project_tap(270,216);
    CHECK(g_patch_page==PatchPage::Name && !g_patch_name_file_saved && !std::strcmp(g_project_names.names[127],"renamed"));
    // A slot created while entering a name is not silently overwritten.
    reset(); patch_browser_buttons(KEY_X); patch_browser_buttons(KEY_X);
    CHECK(g_patch_page==PatchPage::Name);
    // Cached empty, occupied on SD: only the mandatory target probe sees it.
    CHECK(!g_patch_exists[0]); patch_test_disk_override_slot=0; patch_test_disk_exists=true; type_name("new");
    project_tap(270,216); CHECK(patch_test_saves==0 && g_patch_page==PatchPage::Destination && g_patch_confirm_slot==0);
    // Bounds/empty names and keyboard navigation stay inside the native 44 keys.
    patch_test_disk_override_slot=-1;
    g_patch_exists[0]=false; patch_browser_buttons(KEY_X); type_name(""); project_tap(270,216);
    CHECK(patch_test_saves==0 && g_patch_page==PatchPage::Name);
    for(int i=0;i<30;++i) patch_browser_buttons(KEY_Y);
    CHECK(std::strlen(g_patch_draft)==23); patch_browser_buttons(KEY_X); CHECK(std::strlen(g_patch_draft)==22);
    for(int key=0;key<44;++key) for(u32 d : {KEY_LEFT,KEY_RIGHT,KEY_UP,KEY_DOWN}) {
        g_patch_key_cursor=key; patch_browser_buttons(d); CHECK(g_patch_key_cursor>=0 && g_patch_key_cursor<44);
    }
}
void pending_and_delete() {
    seed(); request_destructive(DestructiveAction::LoadProject,127);
    CHECK(g_overlay==Overlay::ConfirmDiscard); project_tap(40,170);
    CHECK(g_overlay==Overlay::PatchBrowser && g_patch_confirm_slot==0 && g_project_session.has_pending());
    patch_test_save_ok=false; patch_browser_buttons(KEY_X);
    CHECK(g_project_session.has_pending() && g_project_session.dirty() && patch_test_execs==0);
    patch_test_save_ok=true; patch_browser_buttons(KEY_X); patch_browser_buttons(KEY_X);
    CHECK(patch_test_execs==1 && patch_test_executed.slot==127 && !g_project_session.has_pending());
    reset(); g_project_session.mark_mutated(); request_destructive(DestructiveAction::ExitApp);
    project_tap(40,170); CHECK(g_patch_page==PatchPage::Destination);
    patch_browser_buttons(KEY_X); type_name("new saved"); project_tap(270,216);
    CHECK(patch_test_execs==1 && patch_test_executed.action==DestructiveAction::ExitApp);
    seed(); request_destructive(DestructiveAction::NewProject); project_tap(230,170);
    CHECK(!g_project_session.has_pending() && g_project_session.dirty() && patch_test_execs==0);
    seed(); g_patch_slot=0; project_tap(270,21);
    project_tap(70,136); CHECK(g_patch_delete_confirm_slot==0 && patch_test_deletes==0);
    patch_test_delete_ok=false; project_tap(70,136);
    CHECK(g_project_session.active_slot()==0 && g_patch_exists[0]);
    patch_test_delete_ok=true; project_tap(70,136); project_tap(70,136);
    CHECK(g_project_session.active_slot()==-1 && g_project_session.dirty() && !g_patch_exists[0]);
    CHECK(patch_test_forgotten_slot==0 && !g_project_names.names[0][0]);
}
void release_safety() {
    seed();
    patch_browser_touch_down(140,216);
    CHECK(patch_test_saves==0 && g_patch_confirm_slot==-1);
    project_overlay_touch_hold(190,190); project_overlay_touch_hold(140,216);
    project_overlay_touch_release(true);
    CHECK(patch_test_saves==0 && g_patch_confirm_slot==-1); // leaving is permanent cancel
    project_tap(140,216); CHECK(g_patch_confirm_slot==0 && patch_test_saves==0);
    patch_browser_touch_down(140,216); CHECK(patch_test_saves==0);
    project_overlay_touch_release(false); CHECK(patch_test_saves==0);
    project_tap(140,216); CHECK(patch_test_saves==1 && !g_project_session.dirty());
    project_overlay_touch_release(true); CHECK(patch_test_saves==1); // no replay

    seed(); project_tap(270,21); project_tap(70,136);
    CHECK(g_patch_delete_confirm_slot==2 && patch_test_deletes==0);
    patch_browser_touch_down(70,136); CHECK(patch_test_deletes==0);
    project_overlay_touch_hold(310,136); project_overlay_touch_hold(70,136);
    project_overlay_touch_release(true); CHECK(patch_test_deletes==0 && g_patch_exists[2]);
    patch_browser_touch_down(70,136); patch_browser_buttons(KEY_UP);
    project_overlay_touch_release(true); CHECK(patch_test_deletes==0);
    project_tap(70,136); project_tap(70,136); CHECK(patch_test_deletes==1 && !g_patch_exists[2]);

    for (int x : {40,150,230}) {
        seed(); request_destructive(DestructiveAction::ExitApp);
        confirm_discard_touch_down(x,170);
        CHECK(g_project_session.has_pending() && patch_test_execs==0 && patch_test_saves==0);
        project_overlay_touch_hold(x,150); project_overlay_touch_hold(x,170);
        project_overlay_touch_release(true);
        CHECK(g_overlay==Overlay::ConfirmDiscard && g_project_session.has_pending() && patch_test_execs==0);
        project_tap(x,170);
        if (x==40) CHECK(g_overlay==Overlay::PatchBrowser && g_patch_confirm_slot==0 && patch_test_execs==0);
        if (x==150) CHECK(patch_test_execs==1 && !g_project_session.has_pending());
        if (x==230) CHECK(g_overlay==Overlay::None && !g_project_session.has_pending() && patch_test_execs==0);
    }
    seed(); project_tap(250,216); patch_browser_buttons(KEY_A); type_name("released");
    patch_browser_touch_down(270,216); CHECK(patch_test_saves==0 && patch_test_names==0);
    project_overlay_touch_hold(270,196); project_overlay_touch_release(true);
    CHECK(patch_test_saves==0 && patch_test_names==0);
    project_tap(270,216); CHECK(patch_test_saves==1 && patch_test_names==1);

    seed(); patch_browser_touch_down(140,216); g_patch_slot=127;
    project_overlay_touch_release(true); CHECK(g_patch_confirm_slot==-1 && patch_test_saves==0);
    seed(); patch_browser_touch_down(140,216); g_project_session.mark_mutated();
    project_overlay_touch_release(true); CHECK(g_patch_confirm_slot==-1 && patch_test_saves==0);
    for (u32 key : {KEY_START, KEY_SELECT, KEY_B}) {
        seed(); patch_browser_touch_down(140,216); bool skip=false; u32 down=key;
        patch_browser_route_buttons(down,skip,Overlay::PatchBrowser);
        project_overlay_touch_release(true); CHECK(skip && patch_test_saves==0 && g_patch_confirm_slot==-1);
    }
}
void cache_navigation() {
    seed();
    const int directory_reads=patch_test_scans;
    patch_library_reset(); patch_library_reset();
    CHECK(patch_test_scans==directory_reads && patch_test_refreshes==0);
    // No status means unknown, not corrupt. Only a newly selected file is probed.
    g_patch_exists[126]=g_patch_exists[127]=true;
    g_patch_checked[126]=g_patch_checked[127]=false;
    g_patch_valid[126]=g_patch_valid[127]=false;
    g_patch_slot=126;
    CHECK(text_has(pair().top,"not checked"));
    patch_library_inspect_selection();
    CHECK(patch_test_refreshes==1 && patch_test_refreshed_slot==126 && !g_patch_checked[127]);
    CHECK(text_has(pair().top,"check file"));
    patch_library_reset(); CHECK(patch_test_refreshes==1);
    patch_browser_buttons(KEY_DOWN); CHECK(patch_test_refreshes==2 && patch_test_refreshed_slot==127);
    patch_browser_buttons(KEY_DOWN); CHECK(patch_test_refreshes==2);
    patch_browser_buttons(KEY_UP); CHECK(patch_test_refreshes==2);
    CHECK(patch_test_scans==directory_reads);
    // A changed current destination is also rechecked, not only save-copy/name.
    reset(); g_project_session.reset_loaded(0);
    patch_test_disk_override_slot=0; patch_test_disk_exists=true;
    patch_browser_buttons(KEY_X);
    CHECK(g_patch_confirm_slot==0 && patch_test_saves==0 && patch_test_refreshes==1);
    patch_test_disk_override_slot=-1;
    patch_browser_buttons(KEY_X);
    CHECK(patch_test_saves==1 && patch_test_refreshed_slot==0 && patch_test_scans==0);
    reset(); g_patch_catalog_failed=true;
    CHECK(text_has(pair().bottom,"sd error"));
}
void layout() {
    for (int selected : {2,127}) for (PatchPage page : {PatchPage::List,PatchPage::Destination,PatchPage::More,PatchPage::Name}) {
        seed(); g_patch_slot=selected; g_patch_page=page;
        CHECK(project_name_set(g_project_names,selected,"abcdefghijklmnopqrstuvw"));
        std::snprintf(g_patch_draft,sizeof g_patch_draft,"abcdefghijklmnopqrstuvw");
        const auto p=pair(); CHECK(text_has(p.top,"currently open") && text_has(p.top,"night bus"));
        if(page==PatchPage::List) CHECK(text_has(p.bottom,selected==127 ? "127-128 / 128" : "001-006 / 128"));
    }
    seed(); g_overlay=Overlay::ConfirmDiscard; pair();
    seed(); const auto before=pair();
    g_project_session.reset_new(); g_patch_slot=127; g_patch_page=PatchPage::Name; g_project_names={};
    std::fill(std::begin(g_patch_exists),std::end(g_patch_exists),false);
    CHECK(render(400,patch_dispatch_boundary::draw_top).pixels==before.top.pixels);
    CHECK(render(320,patch_dispatch_boundary::draw_bottom).pixels==before.bottom.pixels);
    g_overlay=Overlay::None; project_overlay_capture(); CHECK(!project_overlay_draw_top());
}
// Optional explicit exports retained for existing tools, never part of tests.
void dump_project(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir); std::ofstream report(dir/"frames.tsv");
    report << "name\ttop_objects\tbottom_objects\tshared_objects\tout_of_bounds\n";
    auto shot = [&](const char* name) {
        const auto p=pair();
        ppm(dir/(std::string(name)+"_top.ppm"),p.top); ppm(dir/(std::string(name)+"_bottom.ppm"),p.bottom);
        report << name << '\t' << p.top.objects << '\t' << p.bottom.objects << '\t'
            << p.top.objects+p.bottom.objects << '\t' << p.top.outside+p.bottom.outside << '\n';
    };
    seed(); shot("01_current_not_selected");
    patch_browser_buttons(KEY_X); shot("02_replace");
    g_patch_confirm_slot=-1; request_destructive(DestructiveAction::LoadProject,2); shot("03_unsaved");
    std::ofstream summary(dir/"summary.json");
    summary << "{\"suites\":4,\"native_pair_calls\":" << pair_count
        << ",\"peak_shared_objects\":" << peak_pair_objects << ",\"frozen_frame_bytes\":" << sizeof(ProjectOverlayFrame)
        << ",\"out_of_bounds\":0,\"engine_available_in_draw\":false}\n";
}
void dump(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir); std::ofstream report(dir/"frames.tsv");
    report << "name\tbottom_objects\tout_of_bounds\n";
    auto shot = [&](const char* name, bool discard=false) {
        const auto im=frame(discard); ppm(dir/(std::string(name)+".ppm"),im);
        report << name << '\t' << im.objects << '\t' << im.outside << '\n';
    };
    seed(); shot("01_slots"); patch_browser_buttons(KEY_X); shot("02_replace");
    g_patch_confirm_slot=-1; request_destructive(DestructiveAction::LoadProject,2); shot("03_unsaved",true);
    std::ofstream summary(dir/"summary.json");
    summary << "{\"suites\":4,\"native_render_calls\":" << render_count
        << ",\"peak_bottom_objects\":" << peak_objects << ",\"out_of_bounds\":0,\"engine_available_in_draw\":false}\n";
}
} // namespace patch_menu_test
void test_patch_menu() {
    patch_menu_test::controls(); std::puts("pass library controls: native rows/pages128, active save target, confirmations and input ownership");
    patch_menu_test::naming_and_failures(); std::puts("pass library names: copy/rename, keyboard, full library, failure/retry and concurrent destination protection");
    patch_menu_test::pending_and_delete(); std::puts("pass library safety: dirty save/discard/cancel, failed saves, named first save, delete/detach");
    patch_menu_test::release_safety(); std::puts("pass project release safety: save/delete/discard/done, leave-return, button/context cancellation and single commit");
    patch_menu_test::cache_navigation(); std::puts("pass library cache: repeat open has no IO, one new selection probe, unknown status and fresh write-target protection");
    patch_menu_test::layout(); std::puts("pass library dual frame: native bounds, six-row snapshot, current vs selected, no draw IO or mutable state reads");
    std::printf("pass library UI 6/6; peak shared objects=%d/8192 (not hardware fps)\n",patch_menu_test::peak_pair_objects);
}
} // namespace
