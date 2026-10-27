#!/usr/bin/env python3
"""Compile main.cpp's exact patch functions in the existing menu harness; package 3 native LCDs.
Only SD/transaction boundaries are shimmed by the C++ test, never UI logic or pixels.
"""
import argparse
import csv
import hashlib
import json
import re
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / 'platform/3ds/main.cpp'
GEN = Path('/tmp/seance_patch_menu_native')
OUT = ROOT / 'ui_mockups/project_library_runtime'
LIBRARY = ROOT / 'platform/3ds/project_library_ui.inc'

def source_digest():
    return hashlib.sha256(MAIN.read_bytes() + LIBRARY.read_bytes()).hexdigest()

FUNCTIONS = ('patch_path', 'patch_browser_result', 'request_destructive')
GLOBALS = ('g_patch_notice', 'g_project_session', 'g_patch_confirm_slot',
           'g_patch_delete_confirm_slot', 'g_patch_exists', 'g_patch_valid',
           'g_patch_checked', 'g_patch_catalog_ready', 'g_patch_catalog_failed',
           'g_patch_saved_at', 'g_project_names')


def function(source, name):
    start = re.search(r'^(?:static )?(?:void|bool) ' + name + r'\([^;{]*\)\s*\{', source, re.M)
    assert start, name
    end = re.search(r'^}', source[start.start():], re.M)
    assert end, name
    return source[start.start():start.start() + end.end()] + '\n'


def extract():
    source = MAIN.read_text()
    action = re.search(r'^enum class PatchAction \{[^}]+\};', source, re.M)
    assert action, 'PatchAction'
    snippets = [action.group()]
    for name in GLOBALS:
        lines = re.findall(r'^(?:int|char|bool|uint64_t|ProjectNames|ProjectSession) +'+name+r'\b[^\n]*', source, re.M)
        assert len(lines) == 1, name
        snippets.extend(lines)
    snippets.extend(function(source, name) for name in FUNCTIONS)
    snippets.append(LIBRARY.read_text())
    output = '// Generated from current main.cpp; do not edit or replace with a UI model.\n' + '\n'.join(snippets)
    GEN.mkdir(parents=True, exist_ok=True)
    (GEN / 'patch_menu_main.inc').write_text(output)
    # Normal app dispatcher and opaque surface ownership, verbatim main bodies.
    def between(start, end):
        at = source.index(start)
        return source[at:source.index(end, at)]
    dispatch = function(source, 'draw_bottom_surface')
    dispatch += '\nvoid draw_top() {\n' + between(
        '        if (nav_clock_top_active()) nav_clock_draw_top();',
        '\n\n        C2D_TargetClear(bot') + '\n}\n'
    dispatch += '\nvoid draw_bottom() {\n' + between(
        '        if (nav_is_open()) nav_draw(); // frozen backdrop; no hidden live page rendering',
        '\n        }\n        draw_stylus_feedback();') + '\n}\n'
    (GEN / 'patch_menu_dispatch.inc').write_text(dispatch)
    (GEN / 'source.sha256').write_text(source_digest())
    assert source.index('        project_overlay_capture();') < source.index('        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;', source.index('        project_overlay_capture();'))
    print('extracted exact main project helpers + private library controller/frozen dual draw + LCD dispatch; IO/unrelated views shimmed')


def package():
    assert (GEN / 'source.sha256').read_text() == source_digest(), 'rebuild stale main extraction'
    OUT.mkdir(parents=True, exist_ok=True)
    run = subprocess.run(['/tmp/seance_patch_menu_test', '--dump-patch-menu', str(OUT)], cwd=ROOT, capture_output=True, text=True)
    (OUT / 'capture.log').write_text(run.stdout + run.stderr)
    run.check_returncode()
    rows = list(csv.DictReader((OUT / 'frames.tsv').open(), delimiter='\t'))
    assert len(rows) == 3
    for row in rows:
        image = Image.open(OUT / (row['name'] + '.ppm')).convert('RGB')
        assert image.size == (320, 240) and int(row['out_of_bounds']) == 0
        image.save(OUT / (row['name'] + '.png'))
        image.resize((640, 480), Image.Resampling.NEAREST).save(OUT / (row['name'] + '_2x.png'))
    files = ['platform/3ds/main.cpp', 'platform/3ds/project_library_ui.inc', 'platform/3ds/project_names.cpp', 'platform/3ds/menu_labels_data.h', 'platform/3ds/menu_style.cpp',
             'tools/menu_stage1_test.cpp', 'tools/patch_menu_test.h', 'tools/patch_menu_runtime.py',
             'tools/generate_menu_labels.py', 'Makefile']
    summary = json.loads((OUT / 'summary.json').read_text())
    (OUT / 'verification.json').write_text(json.dumps({
        'origin': '3 actual main.cpp bottom LCDs, existing C++ software raster; no authored preview',
        'limits': 'SD writes/removal/scans and destructive execution are outcome shims; real ProjectSession and names/controller. These optional exports contain lower LCDs only; focused tests also draw the upper. No ARM/hardware proof.',
        'check': 'make test-patch-ui', 'summary': summary, 'frames': rows,
        'source_sha256': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in files},
        'deployment': 'none',
    }, indent=2) + '\n')
    print(f'3 actual native bottom screens packaged: {OUT}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extract', action='store_true')
    args = parser.parse_args()
    extract() if args.extract else package()
