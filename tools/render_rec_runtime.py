#!/usr/bin/env python3
"""Package four actual REC C++ frames. No UI painting, DSP model or rebuild."""
import csv
import json
import subprocess
from pathlib import Path
from PIL import Image
from render_fold_runtime import audit_text

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/rec_runtime'
NAMES = ('01_empty', '02_recording', '03_unsaved', '04_mod')


def main():
    raw = OUT / 'raw'
    raw.mkdir(parents=True, exist_ok=True)
    run = subprocess.run(['/tmp/seance_chorus_ui_test', '--dump-rec', str(raw)],
                         capture_output=True, text=True, cwd=ROOT)
    (OUT / 'capture.log').write_text(run.stdout + run.stderr)
    run.check_returncode()
    rows = list(csv.DictReader((raw / 'manifest.tsv').open(), delimiter='\t'))
    assert tuple(row['frame'] for row in rows) == NAMES
    sheet = Image.new('RGB', (1636, 496), (24, 20, 29))
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
        'origin': 'actual C++ UI software raster; synthetic PCM captured by unchanged RecUnit; not hardware',
        'scope': 'REC controls, upper inspector, sigil mod without TEMP; local only',
        'wave': 'existing 64-point published take trace, recorded-frame timebase, explicit x1..8 display gain',
        'checks': 'make test-rec-ui; WAV export outcome uses a host shim, not filesystem IO proof',
        'frames': rows, 'peak_objects': max(int(row['shared_objects']) for row in rows),
    }, indent=2) + '\n')
    print('4 actual C++ REC duals packaged; no rebuild or extra test run')
    print(OUT / 'comparison.png')


if __name__ == '__main__':
    main()
