#!/usr/bin/env python3
"""Package actual C++ delay captures only; no UI drawing, PCM or response simulation."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('/tmp/seance_chorus_ui_test'))
    parser.add_argument('--output', type=Path, default=ROOT / 'ui_mockups/delay_modern_runtime')
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    raw = output / 'raw'
    raw.mkdir(parents=True, exist_ok=True)
    # Dump mode deliberately does not rerun any suite or trigger a build.
    subprocess.run([str(binary), '--dump-delay', str(raw)], cwd=ROOT, check=True)
    with (raw / 'manifest.tsv').open() as file:
        rows = list(csv.DictReader(file, delimiter='\t'))
    audit = {
        'origin': 'actual C++ UI controllers/captures/native software raster; not hardware/PICA acceptance',
        'scope': 'modern 11-param and legacy6 delay, real DSP scalar diagnostics; response diagram mathematical not PCM; no device build or deployment',
        'model': 'same72ms source; fixed1.6s <=1000ms / fixed6.4s >1000ms, no amplitude renormalization; excludes tone/saturation/hold/crossfade',
        'budget_scope': 'visible top inspector + delay lower/footer (or selected mod/temp); excludes hidden module shell and Chains underlay',
        'budget_limit': 8192,
        'binary': {'path': str(binary), 'sha256': sha(binary)},
        'sources': {}, 'frames': [], 'peak_shared_objects': 0,
    }
    for relative in ('platform/3ds/ui_module_focus_delay.cpp', 'platform/3ds/delay_echo_model.h',
                     'platform/3ds/module_inspector.cpp', 'platform/3ds/ui_param_lens.cpp',
                     'core/engine/units.cpp', 'core/engine/units.h', 'core/engine/delay_time.h',
                     'core/engine/engine.cpp', 'core/engine/patch_io.cpp',
                     'tools/delay_ui_test.h', 'tools/sequencer_test.cpp', 'tools/render_delay_runtime.py'):
        source = ROOT / relative
        if source.is_file():
            audit['sources'][relative] = sha(source)
    bottoms = {}
    for row in rows:
        name = row['frame']
        top_path, bottom_path = raw / f'{name}_top.ppm', raw / f'{name}_bottom.ppm'
        with Image.open(top_path) as file:
            top = file.convert('RGB')
        with Image.open(bottom_path) as file:
            bottom = file.convert('RGB')
        assert top.size == (400, 240) and bottom.size == (320, 240)
        count = int(row['shared_objects'])
        assert count == int(row['top_objects']) + int(row['bottom_objects'])
        assert 0 < count <= audit['budget_limit']
        assert int(row['bottom_clipped']) == int(row['bottom_overlaps']) == 0
        dual = Image.new('RGB', (400, 480), (0, 0, 0))
        dual.paste(top, (0, 0))
        dual.paste(bottom, (40, 240))
        dual.save(output / f'{name}_dual_native.png')
        dual.resize((800, 960), Image.Resampling.NEAREST).save(output / f'{name}_dual_2x.png')
        bottom.save(output / f'{name}_bottom_native.png')
        bottoms[name] = bottom
        audit['peak_shared_objects'] = max(audit['peak_shared_objects'], count)
        audit['frames'].append(dict(row, top_sha256=sha(top_path), bottom_sha256=sha(bottom_path)))
    # Only paste the three untouched native lower surfaces; no explanatory labels.
    comparison = Image.new('RGB', (960, 240), (0, 0, 0))
    for i, name in enumerate(('01_short', '02_long', '03_feedback')):
        comparison.paste(bottoms[name], (320 * i, 0))
    comparison.save(output / 'three_responses_native.png')
    comparison.resize((1920, 480), Image.Resampling.NEAREST).save(output / 'three_responses_2x.png')
    (output / 'capture.json').write_text(json.dumps(audit, indent=2) + '\n')
    print(json.dumps({'output': str(output), 'frames': len(rows),
                      'peak_shared_objects': audit['peak_shared_objects']}))


if __name__ == '__main__':
    main()
