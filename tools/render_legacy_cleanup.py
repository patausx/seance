#!/usr/bin/env python3
"""Package five actual composed C++ duals and preservation receipts; no authored UI pixels."""
import csv
import hashlib
import json
import subprocess
from pathlib import Path
from PIL import Image
from render_fold_runtime import audit_text

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/legacy_cleanup_runtime'
BIN = Path('/tmp/seance_osc_input_test')
ALLOWED = {'platform/3ds/main.cpp', 'platform/3ds/ui_top.cpp',
           'platform/3ds/ui_editor.cpp', 'platform/3ds/ui_param_lens.cpp'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    baseline = json.loads((OUT / 'baseline.json').read_text())
    before = {p: sha(ROOT / p) for p in baseline}
    changed = {p for p in baseline if before[p] != baseline[p]}
    assert changed <= ALLOWED, changed - ALLOWED
    raw = OUT / 'raw'
    raw.mkdir(exist_ok=True)
    binary = sha(BIN)
    run = subprocess.run([str(BIN), '--dump-legacy-cleanup', str(raw)], cwd=ROOT, capture_output=True, text=True)
    (OUT / 'capture.log').write_text(run.stdout + run.stderr)
    run.check_returncode()
    rows = list(csv.DictReader((raw / 'manifest.tsv').open(), delimiter='\t'))
    assert len(rows) == 5
    sheet = Image.new('RGB', (400 * len(rows), 480), 'black')
    frames = []
    for i, row in enumerate(rows):
        name = row['frame']
        assert int(row['shared']) == int(row['top']) + int(row['bottom']) <= 8192
        assert int(row['clipped']) == int(row['overlaps']) == 0
        dual = Image.new('RGB', (400, 480), 'black')
        surfaces = {}
        for screen, width, x, y in [('top', 400, 0, 0), ('bottom', 320, 40, 240)]:
            path = raw / f'{name}_{screen}.ppm'
            image = Image.open(path).convert('RGB')
            assert image.size == (width, 240)
            surfaces[screen] = {'sha256': sha(path), 'text': audit_text(raw / f'{name}_{screen}_text.tsv', width)}
            dual.paste(image, (x, y))
        dual.save(OUT / f'{name}_dual_native.png')
        dual.resize((800, 960), Image.Resampling.NEAREST).save(OUT / f'{name}_dual_2x.png')
        sheet.paste(dual, (i * 400, 0))
        frames.append({'counts': row, 'surfaces': surfaces})
    sheet.save(OUT / 'comparison_native.png')
    assert {p: sha(ROOT / p) for p in baseline} == before and sha(BIN) == binary
    (OUT / 'verification.json').write_text(json.dumps({
        'origin': 'actual real-editor/top dispatcher and native host C++ raster; software line coverage, not PICA/device acceptance',
        'draws_ui_in_python': False, 'binary_sha256': binary, 'frames': frames,
        'peak_shared_objects': max(int(row['shared']) for row in rows),
        'budget_scope': 'actual top_draw + editor_draw (normal and modulation), or top_draw + sequencer_view_draw; excludes hidden main Chains underlay',
        'project_overlay_proof': '../legacy_project_runtime/verification.json',
        'source_sha256': before, 'changed_runtime_files': sorted(changed),
        'unchanged_core_platform_count': len(baseline) - len(changed),
        'baseline': 'baseline.json',
        'limits': ['no ARM build or FTP', 'no hardware/audio acceptance', 'focused cleanup and shared lens checks, not a full suite'],
    }, indent=2) + '\n')
    print(f'five actual composed duals, peak {max(int(r["shared"]) for r in rows)}/8192; {len(baseline)-len(changed)} protected sources exact')


if __name__ == '__main__':
    main()
