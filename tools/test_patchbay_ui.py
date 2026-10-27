#!/usr/bin/env python3
"""Focused real patchbay/menu UI ASan/UBSan test. In-memory raster only."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
subprocess.run(['python3', str(ROOT / 'tools/generate_sigilbay_type.py'), '--check'], check=True)
main = (ROOT / 'platform/3ds/main.cpp').read_text()
state = (ROOT / 'platform/3ds/ui_state.h').read_text()
assert re.search(r'enum class View[^;]+\{\s*Chains,\s*Sequencer,\s*Performance,\s*Sigils,\s*Bounce,\s*Patchbay\s*\}', state)
start = main.index('    if (g_view == View::Patchbay && g_overlay == Overlay::None)', main.index('static void handle_touch('))
end = main.index('    if (g_view == View::Bounce', start)
touch = main[start:end]
assert 'patchbay_touch_down' in touch and 'patchbay_touch_hold' in touch and 'patchbay_touch_release' in touch
start = main.index('        // Patchbay owns local buttons;')
end = main.index('        // Bounce owns local actions', start)
buttons = main[start:end]
assert 'patchbay_cancel_input()' in buttons
sigil_buttons = main[main.index('        // Sigil disclosures own'):start]
assert 'sigils_view_buttons(down, held)' in sigil_buttons and 'down &= KEY_ZL' in sigil_buttons
assert 'sigils_close_mode_menu()' in sigil_buttons
assert main.index('    sigils_visual_init();') < main.index('    g_has_dsp = audio.init')
assert main.index('        sigils_gpu_frame();') > main.index('        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;')
assert main.index('        patchbay_capture();') < main.index('        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;')
assert 'case NavChoice::Patchbay:    to_view = View::Patchbay; break;' in main
assert 'if (to_view == View::Patchbay) patchbay_enter();' in main
assert 'if (view == View::Patchbay) { patchbay_draw(); return; }' in main
assert main.count('patchbay_forget();') == 2
assert 'patchbay_cancel_input();' in main[main.index('static void commit_nav_destination'):main.index('// ===================== input dispatch')]

with tempfile.TemporaryDirectory(prefix='seance-patchbay-ui-') as directory:
    tmp = Path(directory)
    (tmp / 'patchbay_touch.inc').write_text(touch)
    (tmp / 'patchbay_buttons.inc').write_text(buttons)
    (tmp / 'sigils_buttons.inc').write_text(sigil_buttons)
    exe = tmp / 'patchbay_ui_test'
    sources = [ROOT / 'tools/patchbay_ui_test.cpp']
    sources += [ROOT / ('platform/3ds/' + p + '.cpp') for p in (
        'ui_patchbay', 'ui_sigils', 'ui_nav', 'ui_picker', 'ui_chains', 'ui_workspace', 'menu_style',
        'project_names', 'ui_chains_top', 'ui_theme', 'ui_transition')]
    sources += [ROOT / 'core/ui/draw.cpp', ROOT / 'core/ui/font.cpp', *sorted((ROOT / 'core/engine').glob('*.cpp'))]
    sources += [ROOT / ('core/' + p) for p in ('synth/sampler.cpp', 'synth/wav_loader.cpp', 'synth/wav_writer.cpp')]
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=gnu++17', '-O0', '-g1', '-Wall', '-Wextra',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-Wl,--wrap=_ZN6seance2ui4textEiiPKcji', '-I'+str(tmp),
        '-I'+str(ROOT / 'tools/menu_stage1_test_stubs'), '-I'+str(ROOT / 'tools/sequencer_test_stubs'),
        '-I'+str(ROOT / 'core'), *map(str, sources), '-o', str(exe)], check=True, cwd=ROOT)
    subprocess.run([str(exe)], check=True, cwd=directory,
        env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
    print('pass exact-main source invariants: appended View, touch ownership, local buttons/START, navigation entry/cancel, pre-GPU capture, new/load forget')
