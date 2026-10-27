#!/usr/bin/env python3
"""Focused ASan/UBSan main-screen test, no exports or full suite.

SEANCE_PAPER_ROUTING_ONLY=1 selects only shared performance/chain paper routing.
"""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
main = (ROOT / 'platform/3ds/main.cpp').read_text()
assert main.index('        chains_capture();') < main.index('        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;')
assert main.count('        chains_capture();') == 1
assert main.index('        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;') < main.index('        menu_style::ambient_frame(osGetTime());')
chains_buttons = main[main.index('            chains_workspace_buttons(down);'):main.index('        } else if (g_view == View::Chains && g_overlay == Overlay::Editor)')]
assert 'g_sel_slot  =' not in chains_buttons
assert main.index('    sigils_visual_init();') < main.index('    g_has_dsp = audio.init')
with tempfile.TemporaryDirectory(prefix='seance-main-columns-') as directory:
    exe = Path(directory) / 'test'
    sources = [ROOT / 'tools/main_columns_test.cpp']
    sources += [ROOT / ('platform/3ds/' + p + '.cpp') for p in (
        'ui_nav', 'ui_picker', 'ui_chains', 'ui_workspace', 'menu_style',
        'project_names', 'ui_chains_top', 'ui_theme', 'ui_transition')]
    sources += [ROOT / 'core/ui/draw.cpp', ROOT / 'core/ui/font.cpp', *sorted((ROOT / 'core/engine').glob('*.cpp'))]
    sources += [ROOT / ('core/' + p) for p in ('synth/sampler.cpp', 'synth/wav_loader.cpp', 'synth/wav_writer.cpp')]
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=gnu++17', '-O0', '-g1', '-Wall', '-Wextra',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-Wl,--wrap=_ZN6seance2ui4textEiiPKcji',
        '-I'+str(ROOT / 'tools/menu_stage1_test_stubs'), '-I'+str(ROOT / 'tools/sequencer_test_stubs'),
        '-I'+str(ROOT / 'core'), *map(str, sources), '-o', str(exe)], check=True, cwd=ROOT)
    subprocess.run([str(exe)], check=True, cwd=directory,
        env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
