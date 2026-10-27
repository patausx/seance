#!/usr/bin/env python3
"""Package actual Fold C++ dual-screen pixels only; no authored UI, DSP simulation or test rerun."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    '01_fold', '02_wrap', '03_diode', '04_bias', '05_drive_min', '06_drive_max',
    '07_trim_mute', '08_trim_low', '09_mix_dry', '10_mix_half', '11_morph',
    '12_live_mod', '13_lock', '14_mod', '15_temp', '16_bypass', '17_muted',
}
# Taken before this UI task, not learned from the candidate capture. Sound/storage
# remain frozen to the existing filter-clear source baseline.
CORE_BASELINE = {
    'core/engine/units.cpp': 'd70845782e116f89dd760dd17a47b8498a8b3e95645b17925254f4ea94f509d7',
    'core/engine/units.h': 'f5ff6bfb71c97b63368d46735d2726c83a1a7332721e0c02a5af579cda1ab532',
    'core/engine/engine.cpp': '707d2414b972483c3584c5a3bf79481e19b7beb456b8e30bc3a8df28aa68957e',
    'core/engine/engine.h': 'ed4c7aa8ca2719d1ac6c5a84237fb8c9b1d4d4344123db737480d7c474c27a38',
    'core/engine/patch_io.cpp': 'affdf5ad30d4960585a6132b3064e4e3226bf6213fe1ff4893e85f1b69777844',
    'core/engine/patch_io.h': '0fcfae457f0a7c7842850593b5729b7e60245152136a849b79a29a0314a80d69',
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit_text(path, width):
    with path.open() as stream:
        records = list(csv.DictReader(stream, delimiter='\t'))
    boxes = []
    for record in records:
        x, y, w, h = (int(record[key]) for key in ('x', 'y', 'w', 'h'))
        assert 0 <= x < x + w <= width and 0 <= y < y + h <= 240, (path, record)
        for ox, oy, ow, oh in boxes:
            assert not (x < ox + ow and ox < x + w and y < oy + oh and oy < y + h), (path, record)
        boxes.append((x, y, w, h))
    return {'records': len(records), 'bounds_or_intersections': 0, 'sha256': sha(path)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('/tmp/seance_chorus_ui_test'))
    parser.add_argument('--output', type=Path, default=ROOT / 'ui_mockups/fold_runtime')
    parser.add_argument('--test-log', type=Path, help='Optional already-completed focused log; never rerun tests')
    parser.add_argument('--filter-log', type=Path, help='Optional already-completed shared-inspector Filter regression log')
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    for relative, expected in CORE_BASELINE.items():
        assert sha(ROOT / relative) == expected, f'protected sound/storage source changed: {relative}'
    raw = output / 'raw'
    raw.mkdir(parents=True, exist_ok=True)
    run = subprocess.run([str(binary), '--dump-fold', str(raw)], cwd=ROOT, capture_output=True, text=True)
    (output / 'capture.log').write_text(run.stdout + run.stderr)
    run.check_returncode()
    with (raw / 'manifest.tsv').open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    assert len(rows) == len(EXPECTED) and {r['frame'] for r in rows} == EXPECTED
    audit = {
        'origin': 'actual C++ UI capture/controllers/native Citro2D host software raster; not hardware/PICA acceptance',
        'scope': 'Fold UI/model only; no core/DSP/storage changes, ARM build or deployment',
        'model': 'exact existing integer Fold: 8 bounded reflections, signed wrap, continuous mode, existing positive diode quarter branch, bias subtraction, trim, dry/wet and final saturation',
        'reference': 'fixed 0.75FS peak24575 two-cycle test sine; x28..306, center132, +/-36px per1FS; eight UI-only subpixel intervals per column, inclusive shared endpoints; no amplitude normalization',
        'plot_layers': 'effective values normally; armed step-lock draft explicitly labelled; bypass exact dry; muted plot hypothetical',
        'exclusions': ['real input/PCM scope', 'anti-aliasing/oversampling audio', 'DC blocking', 'audio-worker concurrency', 'LCD/PICA timing'],
        'capture_contract': 'bounded pre-GPU pointer-free frame; engine/audio detached for both draws; different live FoldUnit argument does not change captured lower',
        'screen_pixels_drawn_in_python': False,
        'budget_scope': 'visible400x240 upper +320x240 Fold/current footer or selected mod/temp; excludes hidden shell/Chains underlay',
        'budget_limit': 8192,
        'binary': {'path': str(binary), 'sha256': sha(binary)},
        'manifest_sha256': sha(raw / 'manifest.tsv'),
        'protected_core_baseline': CORE_BASELINE,
        'protected_core_unchanged': True,
        'sources': {}, 'frames': [], 'peak_shared_objects': 0,
    }
    # Packaging and source receipt are deliberately independent of the test command.
    package_frames(rows, raw, output, audit)
    if args.test_log:
        log = args.test_log.resolve()
        text = log.read_text()
        result = re.search(r'sequencer: (\d+) tests, (\d+) failed; peak bottom (\d+), top (\d+), shared (\d+) / (\d+) objects', text)
        assert result and int(result[1]) == 5 and int(result[2]) == 0 and int(result[5]) <= 8192
        assert len(re.findall(r'^ok: fold ', text, re.MULTILINE)) == 5
        oracle = re.search(r'fold model oracle: (\d+) exact samples against unchanged FoldUnit::process', text)
        assert oracle and int(oracle[1]) > 1000000
        audit['focused_tests'] = {'log': str(log), 'sha256': sha(log), 'passed': 5,
                                  'peak_shared_objects': int(result[5]), 'exact_dsp_comparisons': int(oracle[1]),
                                  'scope': 'five controls/relative/bipolar, locks/context/lens/temp,150 frozen extrema pairs, exact model vs real unchanged DSP'}
    if args.filter_log:
        log = args.filter_log.resolve()
        text = log.read_text()
        assert 'sequencer: 5 tests, 0 failed;' in text
        assert len(re.findall(r'^ok: filter ', text, re.MULTILINE)) == 5
        audit['shared_filter_check'] = {'log': str(log), 'sha256': sha(log), 'passed': 5,
                                         'scope': 'same already-built binary, shared inspector/lens regression only'}
    audit['peak_tested_shared_objects'] = max(audit['peak_shared_objects'],
                                             audit.get('focused_tests', {}).get('peak_shared_objects', 0))
    for relative, expected in CORE_BASELINE.items():
        assert sha(ROOT / relative) == expected
    (output / 'verification.json').write_text(json.dumps(audit, indent=2) + '\n')
    print(json.dumps({'output': str(output), 'frames': len(rows), 'peak_shared_objects': audit['peak_shared_objects']}))


def package_frames(rows, raw, output, audit):
    for relative in ('platform/3ds/ui_module_focus_fold.cpp', 'platform/3ds/fold_wave_model.h',
                     'platform/3ds/ui_module_focus_internal.h', 'platform/3ds/module_inspector.h',
                     'platform/3ds/module_inspector.cpp', 'platform/3ds/ui_param_lens.cpp',
                     'platform/3ds/ui_editor.cpp', 'platform/3ds/ui_top.cpp', 'platform/3ds/menu_style.cpp',
                     'core/engine/units.cpp', 'core/engine/units.h', 'core/engine/engine.cpp',
                     'core/engine/engine.h', 'core/engine/patch_io.cpp', 'core/engine/patch_io.h',
                     'tools/fold_ui_test.h', 'tools/module_inspector_test.h', 'tools/sequencer_test.cpp',
                     'tools/render_fold_runtime.py', 'Makefile'):
        audit['sources'][relative] = sha(ROOT / relative)
    bottoms, duals = {}, {}
    for row in rows:
        name = row['frame']
        count = int(row['shared_objects'])
        assert count == int(row['top_objects']) + int(row['bottom_objects'])
        assert 0 < count <= audit['budget_limit']
        assert int(row['bottom_clipped']) == int(row['bottom_overlaps']) == 0
        surfaces, images = {}, {}
        for screen, width in (('top', 400), ('bottom', 320)):
            source = raw / f'{name}_{screen}.ppm'
            with Image.open(source) as file:
                image = file.convert('RGB')
            assert image.size == (width, 240)
            surfaces[screen] = {'ppm_sha256': sha(source), 'size': list(image.size),
                               'text': audit_text(raw / f'{name}_{screen}_text.tsv', width)}
            images[screen] = image
        dual = Image.new('RGB', (400, 480), (0, 0, 0))
        dual.paste(images['top'], (0, 0))
        dual.paste(images['bottom'], (40, 240))
        assert dual.crop((0, 0, 400, 240)).tobytes() == images['top'].tobytes()
        assert dual.crop((40, 240, 360, 480)).tobytes() == images['bottom'].tobytes()
        dual.save(output / f'{name}_dual_native.png')
        enlarged = dual.resize((800, 960), Image.Resampling.NEAREST)
        assert enlarged.resize(dual.size, Image.Resampling.NEAREST).tobytes() == dual.tobytes()
        enlarged.save(output / f'{name}_dual_2x.png')
        images['bottom'].save(output / f'{name}_bottom_native.png')
        bottoms[name], duals[name] = images['bottom'], dual
        audit['peak_shared_objects'] = max(audit['peak_shared_objects'], count)
        audit['frames'].append(dict(row, surfaces=surfaces,
                                   dual_native_sha256=sha(output / f'{name}_dual_native.png')))
    # Equal-scale untouched real surfaces; Python adds only black outside screens.
    for filename, names in (
            ('mode_comparison', ('01_fold', '02_wrap', '03_diode', '04_bias')),
            ('level_comparison', ('05_drive_min', '06_drive_max', '07_trim_mute', '08_trim_low', '09_mix_dry', '10_mix_half'))):
        columns = 2 if len(names) == 4 else 3
        sheet = Image.new('RGB', (320 * columns, 240 * ((len(names) + columns - 1) // columns)), (0, 0, 0))
        for i, name in enumerate(names):
            sheet.paste(bottoms[name], ((i % columns) * 320, (i // columns) * 240))
        sheet.save(output / f'{filename}_native.png')
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(output / f'{filename}_2x.png')
    sheet = Image.new('RGB', (1600, 480 * ((len(rows) + 3) // 4)), (0, 0, 0))
    for i, row in enumerate(rows):
        sheet.paste(duals[row['frame']], ((i % 4) * 400, (i // 4) * 480))
    sheet.save(output / 'contact_sheet_native.png')


if __name__ == '__main__':
    main()
