#!/usr/bin/env python3
"""Package actual Filter C++ dual-screen captures; no Python-authored UI or response simulation."""
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
    '01_lp', '02_hp', '03_bp', '04_notch', '05_off', '06_resonance',
    '07_morph_mid', '08_morph_end', '09_cut_min', '10_cut_max', '11_lock',
    '12_mod', '13_temp', '14_return',
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
    parser.add_argument('--output', type=Path, default=ROOT / 'ui_mockups/filter_runtime')
    parser.add_argument('--test-log', type=Path, help='Optional already-completed focused test log; never reruns tests')
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    raw = output / 'raw'
    raw.mkdir(parents=True, exist_ok=True)
    # A dump is deliberately not a build, suite rerun, audio render, or deployment.
    run = subprocess.run([str(binary), '--dump-filter', str(raw)], cwd=ROOT, capture_output=True, text=True)
    (output / 'capture.log').write_text(run.stdout + run.stderr)
    run.check_returncode()
    with (raw / 'manifest.tsv').open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    assert len(rows) == len(EXPECTED) and {r['frame'] for r in rows} == EXPECTED
    audit = {
        'origin': 'actual C++ UI controllers/capture/native Citro2D host software raster; not hardware/PICA acceptance',
        'scope': 'filter UI/model only; no DSP/core changes, audio spectrum, ARM build or device deployment',
        'model': 'linearized exact existing Chamberlin update order relative to post-drive input; notch=input-BP; morph=m+(1-2m)*H with m=raw/32768; OFF=unity',
        'exclusions': ['drive nonlinearity', 'state/output saturation', 'fixed-point rounding', 'audio-worker concurrency', 'LCD/PICA timing'],
        'nominal_hz': '32000/pi*asin(clamp(raw,1,27851)/65536); undamped SVF natural-frequency convention, not measured -3dB; existing final15% plateau retained',
        'capture_contract': 'bounded pre-GPU pointer-free C++ frame; engine/audio detached for both draws; different live FilterUnit argument cannot change captured lower',
        'screen_pixels_drawn_in_python': False,
        'budget_scope': 'visible 400x240 inspector + 320x240 Filter/footer or selected mod/temp; excludes hidden shell and Chains underlay',
        'budget_limit': 8192,
        'binary': {'path': str(binary), 'sha256': sha(binary)},
        'manifest_sha256': sha(raw / 'manifest.tsv'),
        'sources': {}, 'frames': [], 'peak_shared_objects': 0,
    }
    for relative in ('platform/3ds/ui_module_focus_filter.cpp', 'platform/3ds/filter_response_model.h',
                     'platform/3ds/ui_module_focus_internal.h', 'platform/3ds/module_inspector.h',
                     'platform/3ds/module_inspector.cpp', 'platform/3ds/ui_param_lens.cpp',
                     'platform/3ds/ui_editor.cpp', 'platform/3ds/ui_top.cpp', 'platform/3ds/menu_style.cpp',
                     'core/engine/units.cpp', 'core/engine/units.h', 'core/dsp/filter.h',
                     'tools/filter_ui_test.h', 'tools/module_inspector_test.h', 'tools/sequencer_test.cpp',
                     'tools/render_filter_runtime.py', 'Makefile'):
        audit['sources'][relative] = sha(ROOT / relative)
    bottoms, duals = {}, {}
    for row in rows:
        name = row['frame']
        count = int(row['shared_objects'])
        assert count == int(row['top_objects']) + int(row['bottom_objects'])
        assert 0 < count <= audit['budget_limit']
        assert int(row['bottom_clipped']) == int(row['bottom_overlaps']) == 0
        surfaces = {}
        images = {}
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
    # Untouched C++ lower surfaces only: equal-scale LP/HP/BP/Notch/OFF and half-input morph.
    comparisons = ('01_lp', '02_hp', '03_bp', '04_notch', '05_off', '07_morph_mid')
    sheet = Image.new('RGB', (960, 480), (0, 0, 0))
    for i, name in enumerate(comparisons):
        sheet.paste(bottoms[name], ((i % 3) * 320, (i // 3) * 240))
    sheet.save(output / 'response_comparison_native.png')
    sheet.resize((1920, 960), Image.Resampling.NEAREST).save(output / 'response_comparison_2x.png')
    sheet = Image.new('RGB', (1600, 480 * ((len(rows) + 3) // 4)), (0, 0, 0))
    for i, row in enumerate(rows):
        sheet.paste(duals[row['frame']], ((i % 4) * 400, (i // 4) * 480))
    sheet.save(output / 'contact_sheet_native.png')
    if args.test_log:
        log = args.test_log.resolve()
        text = log.read_text()
        result = re.search(r'sequencer: (\d+) tests, (\d+) failed; peak bottom (\d+), top (\d+), shared (\d+) / (\d+) objects', text)
        assert result and int(result[1]) == 5 and int(result[2]) == 0 and int(result[5]) <= 8192
        assert len(re.findall(r'^ok: filter ', text, re.MULTILINE)) == 5
        audit['focused_tests'] = {'log': str(log), 'sha256': sha(log), 'passed': 5,
                                  'peak_shared_objects': int(result[5]),
                                  'scope': 'four controls, locks/ownership/lens/temp, 90 frozen extrema pairs, independent linear impulse+DFT response oracle'}
    (output / 'verification.json').write_text(json.dumps(audit, indent=2) + '\n')
    print(json.dumps({'output': str(output), 'frames': len(rows),
                      'peak_shared_objects': audit['peak_shared_objects']}))


if __name__ == '__main__':
    main()
