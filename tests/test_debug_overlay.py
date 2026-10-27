#!/usr/bin/env python3
"""One narrow sanitizer check: profiler statistics, actual overlay and main seams. No media."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'platform/3ds/main.cpp').read_text()
# Opt-in diagnostic ZR interception precedes every local controller, including undo.
main = source.split('int main() {', 1)[1]
loop = main.split('    while (aptMainLoop()) {', 1)[1]
assert loop.index('debug_overlay::input(') < loop.index('nav_ink_input(') < loop.index('chains_workspace_buttons(')
assert source.count('debug_overlay::init();') == 1
assert source.count('debug_overlay::shutdown();') == 2
startup = source.split('static void startup_draw(', 1)[1].split('\nstatic void startup_screenshot', 1)[0]
for body in (startup, loop):
    assert body.index('debug_overlay::before_frame();') < body.index('if (!C3D_FrameBegin(') < body.index('debug_overlay::after_begin();')
    assert body.index('draw_stylus_feedback();') < body.index('debug_overlay::draw(top);') < body.index('C3D_FrameEnd(0);') < body.index('debug_overlay::after_end();')
print('pass main seams: input consumed before controllers, final top overlay in both render paths', flush=True)

with tempfile.TemporaryDirectory(prefix='seance-profiler-') as tmp:
    tmp = Path(tmp)
    (tmp / '3ds.h').write_text('''#pragma once
#include <cstdint>
constexpr uint64_t SYSCLOCK_ARM11 = 1000000; // synthetic ticks are microseconds
constexpr uint32_t KEY_A=1, KEY_B=2, KEY_TOUCH=1u<<20, KEY_ZR=1u<<15;
struct aptHookCookie {};
enum APT_HookType { APTHOOK_ONSUSPEND, APTHOOK_ONSLEEP, APTHOOK_ONRESTORE, APTHOOK_ONWAKEUP };
uint64_t svcGetSystemTick();
void aptHook(aptHookCookie*, void (*)(APT_HookType, void*), void*);
void aptUnhook(aptHookCookie*);
''')
    (tmp / 'citro2d.h').write_text('''#pragma once
#include <cstdint>
struct C3D_RenderTarget_tag {};
void C2D_SceneBegin(C3D_RenderTarget_tag*);
float C3D_GetDrawingTime();
uint32_t C3D_FrameCounter(int);
''')
    binary = tmp / 'test'
    subprocess.run(['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
        '-DSEANCE_DIAGNOSTIC_OVERLAY',
        '-I'+str(tmp), '-I.', 'tests/debug_overlay_test.cpp', 'platform/3ds/debug_overlay.cpp',
        '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=20,
        env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1',
             'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'})
