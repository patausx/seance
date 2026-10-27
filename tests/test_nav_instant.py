#!/usr/bin/env python3
"""Immediate GO TO logic + interruptible ink presentation: exact main sections.
Real ink renderer is linked; controller/HID/GPU boundaries are explicit spies.
Handoff easing/alpha proof lives in test_nav_ink_motion.py, not these raster no-ops.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'platform/3ds/main.cpp').read_text()


def extract(pattern, text=source):
    match = re.search(pattern, text, re.M | re.S)
    assert match, pattern
    return match.group()


def between(start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


# The retired input-locked curtain is gone, not merely disabled. Decoration is
# never an input owner; it advances before GPU submission, not repeated drawing.
for retired in ('g_hinge', 'HingeTransition', 'HINGE_FRAMES', 'blackletter_gate',
                'begin_hinge_transition', 'draw_hinge_transition'):
    assert retired not in source, retired
assert source.count('        nav_ink_input(down);') == 1
assert source.index('        nav_ink_input(down);') < source.index('        const bool param_skip_touch')
assert source.index('        capture_nav_ink_transition();') < source.index(
    '        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;', source.index('        bounce_capture();'))
assert 'g_nav_ink' not in between('        const bool param_skip_touch', '        if (g_exit_requested)')
# Compile the actual pre-lens routing below, not a handwritten ordering model.
# Real PLAY/lens/engine behavior is covered by the narrow --cuts-only harness.
play_route = between('        const bool rec_skip_touch =',
                     '        const bool discard_modal_had_frame =')
assert source.index(play_route) < source.index('        if ((down & KEY_START)')
select_route = between('        // SELECT has one meaning everywhere:',
                       '\n\n        // Sigil disclosures own')
assert 'nav_open();' in select_route
assert 'transport_play' not in select_route and 'transport_stop' not in select_route
assert 'transport_reset' not in select_route and 'held & KEY_' not in select_route
start_route = between('        // START is the sequencer transport everywhere.',
                      '\n\n        if ((down & KEY_Y)')
assert 'set_sequencer_enabled(true)' in start_route
assert 'transport_play()' in start_route and start_route.count('transport_stop()') >= 2
assert 'transport_reset()' in start_route
assert 'held & KEY_R' in start_route and 'held & KEY_L' in start_route
assert 'Overlay::PatchBrowser' not in start_route and 'patch_library_reset()' not in start_route
assert 'View::Sequencer' in start_route and 'View::Bounce' in start_route
assert 'down &= KEY_ZL' in start_route
assert 'splash_play(top, bot)' not in source
assert source.count('    draw_stylus_feedback();') == 2 # startup + final runtime lower pass
startup = between('static void startup_draw(', '\nstatic void startup_screenshot')
assert startup.index('capture_stylus_feedback(') < startup.index('C3D_FrameBegin(')
assert startup.index('start_menu_draw_bottom();') < startup.index('draw_stylus_feedback();')
assert startup.index('start_menu_draw_top();') < startup.index('start_menu_draw_reveal(400);') < startup.index('C2D_SceneBegin(bottom)')
assert startup.index('draw_stylus_feedback();') < startup.index('start_menu_draw_reveal(320);') < startup.index('C3D_FrameEnd(0)')
assert source.index('        capture_stylus_feedback(held);') < source.index(
    '        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;')
body = extract(r'^static void draw_stylus_feedback\([^)]*\) \{.*?^}')
assert 'hid' not in body and 'osGetTime' not in body
for body in (startup, source[source.index('        // === draw ==='):]):
    assert body.index('C3D_FrameBegin(') < body.index('stylus_feedback::upload()') < body.index('C2D_TargetClear(')
assert source.count('stylus_feedback::init();') == 1
assert source.count('stylus_feedback::shutdown();') == 2
assert source.index('stylus_feedback::init();') < source.index('menu_audio.init(')
state = (ROOT / 'platform/3ds/ui_state.h').read_text()
nav = (ROOT / 'platform/3ds/ui_nav.h').read_text()
# CPU stress mode is deleted, not hidden; hardware detection/buffering stays.
audio = (ROOT / 'platform/3ds/audio_out.cpp').read_text()
audio_header = (ROOT / 'platform/3ds/audio_out.h').read_text()
for text in (source, nav, (ROOT / 'platform/3ds/ui_nav.cpp').read_text(), audio, audio_header):
    assert 'NavChoice::Stress' not in text and 'stress_mode' not in text
assert 'Stress' not in extract(r'enum class NavChoice\b[^;]+;', nav)
assert re.findall(r'osSetSpeedupEnable\s*\(([^)]*)\)', audio) == ['true']
assert 'APT_CheckNew3DS(&is_new);' in audio
assert 'num_buffers_ = is_new ? 4 : NUM_BUFFERS;' in audio
assert re.search(r'FRAMES_PER_BUF\s*=\s*512\s*;', audio_header)
assert re.search(r'NUM_BUFFERS\s*=\s*8\s*;', audio_header)
enums = '\n'.join(extract(r'enum class ' + name + r'\b[^;]+;', text)
                  for name, text in [('View', state), ('Overlay', state), ('Drag', state), ('NavChoice', nav)])
cpp = r'''#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <cstring>
#include <citro2d.h>
#include "core/ui/draw.h"
#include "platform/3ds/ui_palette.h"
#include "platform/3ds/nav_ink_motion.h"
using namespace seance;
using namespace seance::platform::ui3ds;
using C3D_RenderTarget = int;
// Profiler is a passive final pass here; real draw/timing tested in test_debug_overlay.py.
namespace seance::platform::debug_overlay { void draw(C3D_RenderTarget*) {} }
constexpr u32 KEY_ZL = 1u << 14; // libctru key absent from the older host shim
constexpr u64 SYSCLOCK_ARM11 = 268123480;
u64 tick=0;
u64 svcGetSystemTick() { return tick; }
struct touchPosition { u16 px=0, py=0; } touch;
int touch_reads=0;
void hidTouchRead(touchPosition* out) { ++touch_reads; *out=touch; }
// Actual liquid model/GPU adapter have their narrow sanitizer tests. Here the
// seam is explicit: compile main's real passive wrappers against adapter spies.
namespace seance::platform::ui3ds::stylus_feedback {
int captures=0, draws=0, x=0, y=0; bool held=false; u64 now=0; u32 accent=0;
void capture(bool h,int px,int py,u64 ms,u32 color) {
    ++captures; held=h; x=px; y=py; now=ms; accent=color;
}
void draw() { ++draws; }
}
''' + enums + '\n'
cpp += between('static void capture_stylus_feedback(', '\nstatic void install_demo_patch') + '\n'
cpp += extract(r'^struct NavInkTransition \{.*?^static_assert[^\n]+') + r'''
View g_view; Overlay g_overlay; Drag g_drag;
int g_last_px, g_seq_lock_flash, g_patch_confirm_slot, g_patch_delete_confirm_slot;
bool g_smooth_active, g_seq_lock_armed, menu_open=false;
int cancels[6], clears, scans, leaves, enters, resets, touches, tops, bottoms, old_menus, badges;
int objects=0;
View drawn_view; Overlay drawn_overlay;
void stranular_focus_cancel() { ++cancels[0]; }
void reverb_focus_release() {} // real ownership/release exercised by --rev-dense-only
void inst_focus_release() {} // real ownership/release exercised by --inst-only
void glitch_focus_release() {} // real ownership/release exercised by --glitch-only
void arp_focus_release() {} // real ownership/release exercised by --arp-only
void mouth_focus_release() {} // real ownership/release exercised by --mouth-only
void comp_focus_release() {} // real ownership/release exercised by --comp-only
void fract_focus_release() {} // real ownership/release exercised by --fract-only
void picker_cancel_input() { ++cancels[1]; }
void chains_close_context_branch() { ++cancels[2]; }
void chains_view_touch_release(bool commit) { assert(!commit); ++cancels[3]; }
void sequencer_view_cancel_input() { ++cancels[4]; }
void performance_view_touch_release(bool commit) { assert(!commit); ++cancels[5]; }
void nav_transition_clear() { ++clears; }
void scan_patch_slots() { ++scans; }
void patch_library_reset() { g_patch_confirm_slot = g_patch_delete_confirm_slot = -1; } // library-only harness covers state
void performance_view_leave() { ++leaves; }
void sequencer_view_enter() { ++enters; }
void performance_view_reset() { ++resets; }
void bounce_enter() {}
void patchbay_enter() {}
void patchbay_cancel_input() {}
void bounce_draw_top() { ++tops; drawn_view=g_view; drawn_overlay=g_overlay; }
void bounce_draw_indicator() { ++badges; }
bool nav_is_open() { return menu_open; }
void handle_touch(u32, u32) { ++touches; }
bool nav_clock_top_active() { return false; }
void nav_clock_draw_top() { assert(false); }
void nav_draw() { assert(false); }
void nav_draw_transition() { ++old_menus; }
void nav_remember_outputs() {} // only stores LCD output addresses; GPU seam has its own test
void confirm_discard_draw() { assert(false); }
void C2D_TargetClear(C3D_RenderTarget*, int) {}
void C2D_SceneBegin(C3D_RenderTarget*) {}
int col_bg() { return 0; }
void top_draw() { ++tops; drawn_view=g_view; drawn_overlay=g_overlay; }
// Editor reveal is inactive at GO TO destinations. Its real controller/raster
// are linked by test_editor_transition_ui.py, not these navigation-only spies.
bool chains_editor_covers() { return false; }
void chains_editor_draw(bool) {}
bool project_overlay_draw_top() {
    if (g_overlay != Overlay::PatchBrowser && g_overlay != Overlay::ConfirmDiscard) return false;
    top_draw(); return true;
}
void draw_bottom_surface(View v, Overlay o) { ++bottoms; assert(v==drawn_view && o==drawn_overlay); }
bool C2D_DrawRectSolid(float,float,float,float,float,u32) { ++objects; return true; }
bool C2D_DrawLine(float,float,u32,float,float,u32,float,float) { ++objects; return true; }
'''
for name in ('cancel_nav_ink_transition', 'begin_nav_ink_transition', 'nav_ink_input',
             'capture_nav_ink_transition', 'commit_nav_destination', 'draw_nav_ink_transition'):
    cpp += extract(r'^static void ' + name + r'\([^;{]+\) \{.*?^}') + '\n' if name not in (
        'cancel_nav_ink_transition', 'capture_nav_ink_transition') else extract(
        r'^static void ' + name + r'\(\) \{.*?^}') + '\n'
cpp += 'void choose(NavChoice pick) {\n' + between(
    '            if (pick != NavChoice::None && pick != NavChoice::Exit',
    '\n        }\n        // Every view') + '\n}\n'
cpp += '''void touch_gate(bool nav_had_frame) {
const bool stage_skip_touch=false, param_skip_touch=false, discard_modal_had_frame=false;
const u32 down=1, held=1;
''' + between('        if (!stage_skip_touch && !param_skip_touch', '\n\n        if (g_exit_requested)') + '\n}\n'
cpp += 'void draw() { C3D_RenderTarget *top=nullptr, *bot=nullptr;\n' + between(
    '        if (g_nav_ink.frame.active) {\n            draw_nav_ink_transition', '\n        C3D_FrameEnd') + '\n}\n'
cpp += r'''
// Explicit controller spies: only test main's ordering/masking/short-circuit.
bool play_context=false, lens_open=false;
int play_calls=0, lens_calls=0;
u32 lens_down=0;
u32 play_editor_context_keys(u32 down, u32 held) {
    ++play_calls;
    if (!(play_context && !lens_open && g_overlay==Overlay::Editor)) return 0;
    u32 owned=down&(KEY_X|KEY_Y|KEY_R);
    if ((down&KEY_START) && !(held&KEY_L)) owned|=KEY_START;
    return owned;
}
bool recorder_focus_confirming() { return false; } // real REC modal: --module-audit-only
bool recorder_focus_input(u32&,u32,bool) { return false; }
bool param_lens_is_open() { return lens_open; }
bool param_lens_input(u32& down, u32, u32, bool blocked) {
    ++lens_calls; lens_down=down;
    if (blocked) lens_open=false;
    else if ((down&KEY_L) && !(down&(KEY_B|KEY_SELECT|KEY_START|KEY_Y))) { lens_open=true; down=0; }
    return lens_open;
}
bool play_route_frame(u32& down, u32 held, u32 up=0) {
''' + play_route + r'''
    assert(param_owned_frame==lens_open);
    return param_skip_touch;
}
void test_play_route() {
    menu_open=false; g_overlay=Overlay::Editor; play_context=true;
    for (u32 key : {KEY_X,KEY_Y,KEY_R}) {
        lens_open=false; play_calls=lens_calls=0;
        u32 down=key|KEY_L|KEY_TOUCH;
        assert(play_route_frame(down,down));
        assert(down==(KEY_L|KEY_TOUCH) && play_calls==1 && lens_calls==0 && !lens_open);
    }
    lens_open=false; play_calls=lens_calls=0;
    u32 down=KEY_START|KEY_TOUCH;
    assert(play_route_frame(down,down));
    assert(down==KEY_TOUCH && play_calls==1 && lens_calls==0 && !lens_open);
    // L+START is deliberately unassigned: PLAY does not claim it and the lens
    // refuses a mixed START edge rather than interpreting it as fresh held-L.
    lens_open=false; play_calls=lens_calls=0; down=KEY_L|KEY_START;
    assert(!play_route_frame(down,down) && down==(KEY_L|KEY_START) && !lens_open);
    // No owned edge: fresh L reaches the lens; held L alone cannot be replayed.
    lens_open=false; play_calls=lens_calls=0;
    down=KEY_L;
    assert(play_route_frame(down,KEY_L) && lens_open && down==0 && lens_calls==1);
    lens_open=false; down=0;
    assert(!play_route_frame(down,KEY_L) && !lens_open);
    // GO TO bypasses PLAY, forwards unchanged keys to the blocked lens boundary.
    menu_open=true; play_calls=lens_calls=0; down=KEY_L|KEY_X;
    assert(!play_route_frame(down,down));
    assert(play_calls==0 && lens_calls==1 && lens_down==(KEY_L|KEY_X) && down==lens_down);
    menu_open=false; g_overlay=Overlay::ConfirmDiscard; down=KEY_X;
    assert(!play_route_frame(down,down) && down==KEY_X);
    // Ordinary PLAY and an already-open lens are not intercepted by this router.
    g_overlay=Overlay::Editor; play_context=false; lens_calls=0; down=KEY_L|KEY_X;
    assert(play_route_frame(down,down) && lens_open && lens_calls==1 && down==0);
    play_context=true; lens_calls=0; down=KEY_X;
    assert(play_route_frame(down,KEY_L|KEY_X) && lens_calls==1 && lens_down==KEY_X);
    lens_open=false; play_context=false;
    std::puts("pass play-input-route: exact main pre-lens block; X/Y/R ownership, bare/R+START masking, free L+START, fresh L+X short-circuit, navigation/modal/lens boundaries (controller spies)");
}
void test_stylus_seam() {
    for (auto theme : {palette::Theme::Pink, palette::Theme::Graphite}) {
        palette::set(theme); host_time_ms=100; touch={174,148};
        const int reads=touch_reads, captures=stylus_feedback::captures;
        capture_stylus_feedback(KEY_TOUCH|KEY_L);
        assert(touch_reads==reads+1 && stylus_feedback::captures==captures+1);
        assert(stylus_feedback::held && stylus_feedback::x==174 && stylus_feedback::y==148);
        assert(stylus_feedback::now==100 && stylus_feedback::accent==palette::spot());
        const u32 frozen=stylus_feedback::accent;
        host_time_ms=9999; touch={0,239};
        palette::set(theme==palette::Theme::Pink?palette::Theme::Graphite:palette::Theme::Pink);
        const int draws=stylus_feedback::draws;
        draw_stylus_feedback(); draw_stylus_feedback();
        assert(touch_reads==reads+1 && stylus_feedback::draws==draws+2);
        assert(stylus_feedback::captures==captures+1 && stylus_feedback::accent==frozen);
        capture_stylus_feedback(KEY_L);
        assert(!stylus_feedback::held && touch_reads==reads+1);
    }
    std::puts("pass stylus seam: exact main passive capture/draw, frozen time/theme/HID, lift without touch read; liquid raster/GPU covered separately");
}
void reset_draw() { tops=bottoms=old_menus=badges=objects=0; }
int main() {
    int cases=0, frames=0, interruptions=0;
    for (View from : {View::Chains, View::Sequencer, View::Performance, View::Sigils, View::Bounce, View::Patchbay})
    for (Overlay overlay : {Overlay::None, Overlay::Editor, Overlay::PatchBrowser})
    for (NavChoice pick : {NavChoice::Chains, NavChoice::Sequencer, NavChoice::Performance,
                          NavChoice::Sigils, NavChoice::Patch, NavChoice::Bounce, NavChoice::Patchbay}) {
        g_view=from; g_overlay=overlay; g_drag=Drag::SigCanvas; g_last_px=99; g_smooth_active=true;
        g_nav_ink={}; g_seq_lock_armed=true; g_seq_lock_flash=9; tick=1000;
        for (int &v : cancels) v=0;
        clears=scans=leaves=enters=resets=touches=0; reset_draw();
        g_patch_confirm_slot=g_patch_delete_confirm_slot=3;
        const View target=pick==NavChoice::Chains?View::Chains:pick==NavChoice::Sequencer?View::Sequencer:
            pick==NavChoice::Performance?View::Performance:pick==NavChoice::Sigils?View::Sigils:
            pick==NavChoice::Bounce?View::Bounce:pick==NavChoice::Patchbay?View::Patchbay:from;
        const Overlay target_overlay=pick==NavChoice::Patch?Overlay::PatchBrowser:Overlay::None;
        const bool motion=(from!=target || overlay!=target_overlay) &&
            (target!=View::Patchbay || target_overlay==Overlay::PatchBrowser);
        choose(pick);
        assert(g_view==target && g_overlay==target_overlay);
        assert(g_nav_ink.frame.active==motion);
        assert(g_drag==Drag::None && g_last_px==-1 && !g_smooth_active && clears==!motion);
        for (int v : cancels) assert(v==1);
        assert(leaves==int(from!=target && from==View::Performance));
        assert(enters==int(from!=target && target==View::Sequencer));
        assert(resets==int(from!=target && target==View::Performance));
        assert(g_seq_lock_armed==!(from!=target && from==View::Sequencer));
        assert(scans==0); // opening a view never performs an all-slot scan
        if (pick==NavChoice::Patch) assert(g_patch_confirm_slot==-1 && g_patch_delete_confirm_slot==-1);
        if (motion) {
            using T=nav_ink::Target;
            const T expected=pick==NavChoice::Chains?T::Chains:pick==NavChoice::Sigils?T::Sigils:
                pick==NavChoice::Sequencer?T::Sequencer:pick==NavChoice::Performance?T::Performance:
                pick==NavChoice::Patch?T::Patches:T::Bounce;
            assert(g_nav_ink.frame.target==expected);
        }
        touch_gate(true); assert(touches==0); // selecting edge stays menu-owned
        capture_nav_ink_transition(); draw();
        assert(tops==1 && badges==1 && drawn_view==target && drawn_overlay==target_overlay);
        assert(bottoms==!motion && old_menus==motion);
        // Holding/releasing selection does not prolong/restart motion; a fresh
        // touch/button is not delayed. The old controller cancellation is exact.
        nav_ink_input(0); assert(g_nav_ink.frame.active==motion);
        touch_gate(false); assert(touches==1); // no added next-frame input owner
        for (unsigned ms : {1u,16u,76u,109u,110u,150u,199u}) {
            tick=1000+u64(ms)*(SYSCLOCK_ARM11/1000);
            capture_nav_ink_transition();
            const auto before=g_nav_ink;
            reset_draw(); draw();
            assert(tops==1 && badges==1);
            assert(bottoms+old_menus<=1); // never composite two whole pages
            if (ms>=150) assert(bottoms==1 && old_menus==0);
            assert(g_nav_ink.frame.elapsed_ms==before.frame.elapsed_ms &&
                g_nav_ink.frame.active==before.frame.active && g_nav_ink.started_tick==before.started_tick);
            draw(); assert(g_nav_ink.frame.elapsed_ms==before.frame.elapsed_ms);
            assert(g_view==target && g_overlay==target_overlay);
            ++frames;
        }
        tick=1000+u64(nav_ink::kDurationMs)*(SYSCLOCK_ARM11/1000);
        capture_nav_ink_transition(); assert(!g_nav_ink.frame.active && clears==1);
        reset_draw(); draw(); assert(tops==1 && bottoms==1 && !old_menus);
        ++cases;
    }
    for (unsigned bit=0;bit<32;++bit) {
        g_view=View::Sigils; g_overlay=Overlay::None; tick=90000000; clears=0;
        begin_nav_ink_transition(g_view,g_overlay);
        nav_ink_input(u32(1)<<bit);
        assert(g_nav_ink.frame.active==(bit==14)); // ZL only is noninterrupting
        assert(clears==int(bit!=14)); ++interruptions;
    }
    // Real opening/overlay change or an app suspension invalidates the frozen
    // outgoing menu. Capture never restores an obsolete view or replays hooks.
    for (int reason=0;reason<5;++reason) {
        g_view=View::Sigils; g_overlay=Overlay::None; tick=90000000; clears=0;
        begin_nav_ink_transition(g_view,g_overlay);
        if(reason==0) menu_open=true;
        if(reason==1) g_view=View::Sequencer;
        if(reason==2) g_overlay=Overlay::Editor;
        if(reason==3) tick=std::numeric_limits<u64>::max();
        if(reason==4) tick=0;
        const View v=g_view; const Overlay o=g_overlay;
        capture_nav_ink_transition();
        assert(!g_nav_ink.frame.active && clears==1 && g_view==v && g_overlay==o);
        menu_open=false; ++interruptions;
    }
    // Restart uses the last destination and timestamp, not queued animations.
    g_view=View::Chains; g_overlay=Overlay::None; tick=5000;
    begin_nav_ink_transition(g_view,g_overlay); tick=9000; g_view=View::Bounce;
    begin_nav_ink_transition(g_view,g_overlay); capture_nav_ink_transition();
    assert(g_nav_ink.frame.active && g_nav_ink.frame.target==nav_ink::Target::Bounce &&
           g_nav_ink.started_tick==9000 && !g_nav_ink.frame.elapsed_ms);
    nav_ink_input(KEY_A|KEY_ZL); assert(!g_nav_ink.frame.active);
    constexpr NavChoice non_navigation[]={NavChoice::None, NavChoice::Exit};
    for (NavChoice pick : non_navigation) {
        g_view=View::Sigils; g_overlay=Overlay::Editor; clears=0;
        choose(pick); assert(g_view==View::Sigils && g_overlay==Overlay::Editor && clears==0);
    }
    std::printf("pass nav-instant: %d source/overlay/destination cases; %d timed frozen dual dispatches; "
        "%d interruptions; immediate destination/lifecycle/selecting-edge isolation; "
        "same-page bypass; last-choice restart; %zu non-navigation choices unchanged; state %zu bytes\n",
        cases,frames,interruptions,sizeof(non_navigation)/sizeof(non_navigation[0]),sizeof(NavInkTransition));
    test_play_route();
    test_stylus_seam();
}
'''
with tempfile.TemporaryDirectory(prefix='seance-nav-instant-') as tmp:
    unit = Path(tmp) / 'test.cpp'
    binary = Path(tmp) / 'test'
    unit.write_text(cpp)
    subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT), '-I'+str(ROOT/'tests/sequencer_test_stubs'), str(unit),
                    str(ROOT/'platform/3ds/menu_style.cpp'), str(ROOT/'core/ui/draw.cpp'),
                    str(ROOT/'core/ui/font.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
