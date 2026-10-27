#!/usr/bin/env python3
"""Narrow real GO TO/clock/master/blur test; ASan+UBSan, no exports/full suite."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
main = (ROOT / 'platform/3ds/main.cpp').read_text()
nav = (ROOT / 'platform/3ds/ui_nav.cpp').read_text()
# Verify actual integration order, not just the standalone CPU backdrop helper.
a = main.index('        nav_capture();')
b = main.index('        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;', a)
c = main.index('        nav_gpu_frame();', b)
d = main.index('        sigils_gpu_frame();', c)
e = main.index('        if (g_nav_ink.frame.active)', d)
f = main.index('        nav_remember_outputs();', e)
g = main.index('        C3D_FrameEnd(0);', f)
assert a < b < c < d < e < f < g
assert 'nav_ink::draw_bottom(' not in main # no obsolete go-to title over new clock
assert 'draw_nav_preview' not in nav and 'capture_sigil_wave' not in nav
assert 'snapshot.scope' not in nav and 'g_eng->scope' not in nav
with tempfile.TemporaryDirectory(prefix='seance-nav-popup-') as directory:
    exe = Path(directory) / 'test'
    sources = [ROOT / 'tests/nav_popup_test.cpp']
    sources += [ROOT / ('platform/3ds/' + p + '.cpp') for p in (
        'ui_nav', 'ui_picker', 'ui_chains', 'ui_workspace', 'menu_style',
        'project_names', 'ui_chains_top', 'ui_theme', 'ui_transition')]
    sources += [ROOT / 'core/ui/draw.cpp', ROOT / 'core/ui/font.cpp', *sorted((ROOT / 'core/engine').glob('*.cpp'))]
    sources += [ROOT / ('core/' + p) for p in ('synth/sampler.cpp', 'synth/wav_loader.cpp', 'synth/wav_writer.cpp')]
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=gnu++17', '-O0', '-g1', '-Wall', '-Wextra',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-Wl,--wrap=_ZN6seance2ui4textEiiPKcji',
        '-I'+str(ROOT / 'tests/menu_stage1_test_stubs'), '-I'+str(ROOT / 'tests/sequencer_test_stubs'),
        '-I'+str(ROOT / 'core'), *map(str, sources), '-o', str(exe)], check=True, cwd=ROOT)
    subprocess.run([str(exe)], check=True, cwd=directory,
        env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
