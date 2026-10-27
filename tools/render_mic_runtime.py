#!/usr/bin/env python3
"""Package four actual C++ MIC frames. Never draws UI or fabricates microphone data."""
import csv
import json
import subprocess
from pathlib import Path
from PIL import Image
from render_fold_runtime import audit_text

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/mic_runtime'
BINARY = Path('/tmp/seance_chorus_ui_test')
NAMES = ('01_open', '02_clip', '03_muted', '04_low_cut')


def main():
    raw = OUT / 'raw'
    raw.mkdir(parents=True, exist_ok=True)
    run = subprocess.run([str(BINARY), '--dump-mic', str(raw)], capture_output=True, text=True, cwd=ROOT)
    (OUT / 'capture.log').write_text(run.stdout + run.stderr)
    run.check_returncode()
    rows = list(csv.DictReader((raw / 'manifest.tsv').open(), delimiter='\t'))
    assert tuple(row['frame'] for row in rows) == NAMES
    sheet = Image.new('RGB', (400*4+12*3, 496), (24, 20, 29))
    for i, row in enumerate(rows):
        name = row['frame']
        dual = Image.new('RGB', (400, 496), (24, 20, 29))
        for screen, width, xy in (('top', 400, (0, 0)), ('bottom', 320, (40, 256))):
            image = Image.open(raw / f'{name}_{screen}.ppm').convert('RGB')
            assert image.size == (width, 240)
            audit_text(raw / f'{name}_{screen}_text.tsv', width)
            dual.paste(image, xy)
        assert 0 < int(row['shared_objects']) <= 8192
        dual.save(OUT / f'{name}_dual.png')
        dual.resize((800, 992), Image.Resampling.NEAREST).save(OUT / f'{name}_dual_2x.png')
        sheet.paste(dual, (i*412, 0))
    sheet.save(OUT / 'comparison.png')
    (OUT / 'verification.json').write_text(json.dumps({
        'origin': 'actual C++ UI software raster, deterministic input injected through real MicUnit ring; not hardware capture',
        'scope': 'compact MIC wave/meters/controls, real low-cut and source-mute processing; local only',
        'draws_ui_in_python': False,
        'wave': '1048 raw samples at nominal32728.498Hz (~32.02ms), +/-1FS fixed, 293 extrema columns; pre-cut/gain/mute',
        'checks': 'make test-mic; four native pairs checked for text bounds/overlap and shared8192 budget',
        'frames': rows,
        'peak_objects': max(int(row['shared_objects']) for row in rows),
    }, indent=2) + '\n')
    print('4 actual C++ MIC duals packaged; no rebuild or extra test run')
    print(OUT / 'comparison.png')


if __name__ == '__main__':
    main()
