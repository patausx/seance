#!/usr/bin/env python3
"""One focused ASan/UBSan host build for final audio/project audit regressions.
Exact main save/load and project controller, real engine/WAV/temp filesystem;
no full suite, media export, ARM build, deployment or hardware-performance claim.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import patch_menu_runtime

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'platform/3ds/main.cpp').read_text()
patch_menu_runtime.extract()
# App dispatch must route project hold/lift before any underlying page's touch.
touch = source[source.index('static void handle_touch('):source.index('// Modulation panel owns')]
assert touch.index('g_overlay == Overlay::PatchBrowser') < touch.index('g_view == View::Chains && g_overlay == Overlay::SampleBrowser')
assert 'project_overlay_touch_hold(t.px, t.py)' in touch
assert 'project_overlay_touch_release(true)' in touch
assert 'else handle_touch(down, held);' in source
assert 'if (down & ~(KEY_TOUCH | KEY_ZL)) project_overlay_cancel_input();' in source
assert 'if (!editor_delete_confirmation) chains_close_context_branch();' in source
editor = source[source.index('        } else if (g_view == View::Chains && g_overlay == Overlay::Editor)'):source.index('        // While the GO TO menu is open')]
assert 'chains_confirm_editor_delete()' in editor and 'remove_unit_with_undo' not in editor
with tempfile.TemporaryDirectory(prefix='seance-audit-safety-') as directory:
    work = Path(directory)
    (work / 'audit_project_main.inc').write_text('\n'.join(
        patch_menu_runtime.function(source, name) for name in
        ('project_recordings_ready', 'save_current_patch', 'transactional_patch_load')))
    sources = [ROOT / 'tools/audit_safety_test.cpp']
    sources += [ROOT / ('platform/3ds/' + name + '.cpp') for name in (
        'ui_nav', 'ui_picker', 'ui_chains', 'ui_workspace', 'ui_transition',
        'menu_style', 'project_names', 'ui_chains_top', 'ui_theme')]
    sources += [ROOT / 'core/ui/draw.cpp', ROOT / 'core/ui/font.cpp', *sorted((ROOT / 'core/engine').glob('*.cpp'))]
    sources += [ROOT / ('core/' + name) for name in (
        'synth/sampler.cpp', 'synth/wav_loader.cpp', 'synth/wav_writer.cpp')]
    exe = work / 'test'
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=gnu++17', '-O0', '-g1', '-Wall', '-Wextra',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-Wl,--wrap=rename',
        '-Wl,--wrap=_ZN6seance2ui4textEiiPKcji',
        '-I'+str(ROOT / 'tools/menu_stage1_test_stubs'), '-I'+str(ROOT / 'tools/sequencer_test_stubs'),
        '-I'+str(ROOT / 'core'), '-I'+str(work), '-I'+str(patch_menu_runtime.GEN),
        *map(str, sources), '-o', str(exe)], check=True, cwd=ROOT)
    subprocess.run([str(exe)], check=True, cwd=work,
        env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
