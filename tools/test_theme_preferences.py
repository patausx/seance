#!/usr/bin/env python3
"""Exact main startup preference commit + real current sidecar, no copied setter."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'platform/3ds/main.cpp').read_text()
helper=re.search(r'^static bool save_start_preferences\([^;{]+\) \{.*?^}',s,re.M|re.S).group()
assert 'NavChoice::Theme' not in s and 'nav_theme_save_result' not in s
assert s.index('palette::set(g_start_store.theme)') < s.index('menu_audio.init(g_start_store.menu_sound)')
assert 'out.theme = g_start_store.theme;' in s
assert 'proposed.theme = settings.theme;' in s
assert 'if (!save_start_preferences(proposed)) {' in s
assert 'start_menu_set_data(startup_data());' in s
startup=s[s.index('static void startup_draw('):s.index('static void startup_screenshot(')]
assert startup.index('palette::capture(now_ms)') < startup.index('start_menu_capture(now_ms)') < startup.index('capture_stylus_feedback(') < startup.index('C3D_FrameBegin(')
assert startup.count('osGetTime()') == 1
assert startup.index('C3D_FrameBegin(C3D_FRAME_SYNCDRAW)') < startup.index('menu_style::ambient_frame(now_ms)') < startup.index('start_menu_draw_top()')
boot=s[s.index('int main() {'):s.index('    while (aptMainLoop()) {', s.index('int main() {'))]
assert boot.count('menu_style::ambient_init()') == 1
assert boot.index('ui::text_init()') < boot.index('menu_style::ambient_init()') < boot.index('startup_choose(top, bot)') < boot.index('audio.init(*g_eng)')
early=boot[boot.index('    if (!project_ready) {\n        menu_audio.shutdown();'):]
assert early.index('menu_audio.shutdown();') < early.index('clear_sample_bank();')
fence=re.search(r'if \(C3D_FrameBegin\(0\)\) \{(.*?)\n        }',early,re.S).group(1)
assert early.count('menu_style::ambient_shutdown()') == 1
assert fence.index('C3D_FrameEnd(0)') < fence.index('menu_style::ambient_shutdown()') < fence.index('ui::text_shutdown()')
menu=(ROOT/'platform/3ds/ui_start_menu.cpp').read_text()
assert 'menu_style::startup_capture(g.page!=Page::Browser,now_ms,g.page==Page::Theme||g.modal!=0);' in menu
assert 'if(g.page==Page::Theme) { draw_theme_preview();return; }' in menu
assert 'menu_style::paper(400);' in menu
frame=s[s.index('        // === draw ==='):]
assert frame.index('palette::capture(') < frame.index('param_lens_capture();') < frame.index('C3D_FrameBegin(') < frame.index('menu_style::ambient_frame(')
assert frame.index('editor_capture_button_motion(') < frame.index('C3D_FrameBegin(')
cpp='''#include "platform/3ds/start_menu_store.h"
#include "platform/3ds/ui_palette.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
using namespace seance::platform::ui3ds;
StartMenuStore g_start_store;
bool g_start_store_write_failed=true;
const char* START_MENU_PATH;
struct SoundSpy {
    bool enabled = true; int calls = 0;
    void set_enabled(bool value) {
        StartMenuStore disk;
        assert(start_store_load(START_MENU_PATH, disk) && disk.menu_sound == value);
        enabled = value; ++calls;
    }
} sound;
SoundSpy* g_menu_audio = &sound;
uint64_t osGetTime() { return 1000; }
'''+helper+'\n'+r'''
int main(int argc,char** argv) {
    assert(argc==2); const std::string path=std::string(argv[1])+"/start_menu.bin";
    START_MENU_PATH=path.c_str();
    assert(palette::current()==palette::Theme::Pink && g_start_store.theme.id==palette::Theme::Pink);
    start_store_touch(g_start_store,7,111);start_store_touch(g_start_store,2,222);
    g_start_store.show_intro=false;g_start_store.show_menu=false;
    g_start_store.theme.custom=theme_config::kBlackMass;
    assert(start_store_save(START_MENU_PATH,g_start_store));
    for(int i=0;i<15;++i) {
        const auto before=g_start_store.theme;
        auto proposed=g_start_store; proposed.theme.id=theme_config::next(before.id);
        assert(save_start_preferences(proposed));
        assert(g_start_store.theme.id==theme_config::next(before.id));
        assert(palette::settings()==g_start_store.theme);
        StartMenuStore disk, prior;
        assert(start_store_load(START_MENU_PATH,disk));
        assert(start_store_load((path+".bak").c_str(),prior));
        assert(disk.theme==g_start_store.theme && prior.theme==before);
        assert(disk.theme.custom==theme_config::kBlackMass); // preset switches retain custom
        assert(disk.count==2 && disk.recent[0].slot==2 && disk.recent[0].used_at==222);
        assert(disk.recent[1].slot==7 && disk.recent[1].used_at==111);
        assert(!disk.show_intro && !disk.show_menu && !g_start_store_write_failed);
        uint8_t actual[START_STORE_BYTES], expected[START_STORE_BYTES];
        assert(start_store_encode(disk,actual));assert(start_store_encode(g_start_store,expected));
        assert(!std::memcmp(actual,expected,sizeof actual));
    }
    assert(mkdir((path+".tmp").c_str(),0700)==0);
    const auto before=palette::settings(); const auto revision=palette::revision();
    auto blocked=g_start_store;blocked.theme.id=theme_config::next(before.id);
    assert(!save_start_preferences(blocked));
    assert(g_start_store.theme==before && palette::settings()==before && palette::revision()==revision);
    StartMenuStore disk;assert(start_store_load(START_MENU_PATH,disk));assert(disk.theme==before);
    // Startup custom save uses the same save-before-apply boundary.
    auto proposed=g_start_store; proposed.theme={palette::Theme::Custom,theme_config::kGrimoire};
    proposed.menu_sound=false;
    const int calls=sound.calls;
    assert(!save_start_preferences(proposed));assert(palette::settings()==before);
    assert(sound.enabled && sound.calls==calls && g_start_store.menu_sound);
    assert(rmdir((path+".tmp").c_str())==0);
    assert(save_start_preferences(proposed));assert(palette::settings()==proposed.theme);
    assert(!sound.enabled && sound.calls==calls+1 && !g_start_store.menu_sound);
    assert(palette::bg()==theme_config::kGrimoire.paper);
    // Updating custom with unchanged id still commits/recolors/invalidate caches.
    proposed.theme.custom.accent=theme_config::swatches[18];
    assert(save_start_preferences(proposed));assert(palette::spot()==theme_config::swatches[18]);
    proposed.show_intro=true;proposed.show_menu=true;
    assert(save_start_preferences(proposed));assert(palette::current()==palette::Theme::Custom);
    assert(start_store_load(START_MENU_PATH,disk));
    palette::set(palette::Theme::Pink);palette::set(disk.theme);
    assert(palette::settings()==proposed.theme && palette::spot()==theme_config::swatches[18]);
    FILE* f=std::fopen(START_MENU_PATH,"wb");assert(f);std::fputs("broken",f);std::fclose(f);
    assert(!start_store_load(START_MENU_PATH,disk));
    assert(start_store_load((path+".bak").c_str(),disk));assert(disk.theme==proposed.theme && disk.count==2);
    assert(!disk.menu_sound && !sound.enabled);
    std::puts("pass theme/sound preferences: exact main startup commit; 15 preset changes; custom retention/update; flags/MRU; blocked-save audio rollback/retry; boot/recovery");
}
'''
with tempfile.TemporaryDirectory(prefix='seance-theme-prefs-') as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(cpp)
    subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT),str(p/'test.cpp'),str(ROOT/'platform/3ds/start_menu_store.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),str(p)],check=True)
