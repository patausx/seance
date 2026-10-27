#!/usr/bin/env python3
"""Package native mixer/sequence duals; four inset/live-scope duals with --sequence, no builds."""
import csv
import json
import subprocess
import sys
from pathlib import Path
from PIL import Image
from render_fold_runtime import audit_text

ROOT = Path(__file__).resolve().parents[1]
TRANSITION_FX = sys.argv[1:] == ['--transition-fx']
TRANSITION = TRANSITION_FX or sys.argv[1:] == ['--transition']
SEQUENCE = sys.argv[1:] == ['--sequence']
FEEDBACK = sys.argv[1:] == ['--feedback']
OUT = ROOT / ('ui_mockups/transition_fx_runtime' if TRANSITION_FX else 'ui_mockups/transition_runtime' if TRANSITION else 'ui_mockups/sequence_scope_runtime' if SEQUENCE else 'ui_mockups/performance_feedback_runtime' if FEEDBACK else 'ui_mockups/performance_runtime')


def main():
    rows = []
    jobs = (('transition', '--dump-transition-fx' if TRANSITION_FX else '--dump-transition'),) if TRANSITION else (('sequence', '--dump-sequence-extent'),) if SEQUENCE else (('mixer', '--dump-performance-feedback'),) if FEEDBACK else (('mixer', '--dump-performance'), ('sequence', '--dump-sequence-extent'))
    for group, option in jobs:
        raw = OUT / group
        raw.mkdir(parents=True, exist_ok=True)
        run = subprocess.run(['/tmp/seance_chorus_ui_test', option, str(raw)], cwd=ROOT, capture_output=True, text=True)
        (raw / 'capture.log').write_text(run.stdout + run.stderr)
        run.check_returncode()
        frames = list(csv.DictReader((raw / 'manifest.tsv').open(), delimiter='\t'))
        assert len(frames) == (4 if group in ('sequence','transition') else 3 if FEEDBACK else 2)
        for row in frames:
            name = row['frame']
            dual = Image.new('RGB', (400, 496), (24, 20, 29))
            for screen, width, xy in (('top', 400, (0, 0)), ('bottom', 320, (40, 256))):
                stem = name + ('_top' if screen == 'top' else '_bottom' if group != 'sequence' else '')
                image = Image.open(raw / f'{stem}.ppm').convert('RGB')
                assert image.size == (width, 240)
                audit_text(raw / f'{stem}_text.tsv', width)
                dual.paste(image, xy)
                if (SEQUENCE or TRANSITION) and screen == 'bottom':
                    image.resize((640,480),Image.Resampling.NEAREST).save(OUT / f'{name}_bottom_2x.png')
            assert 0 < int(row['shared_objects']) < 8192
            for key in ('clipped_primitives', 'top_clipped_primitives', 'text_overlaps', 'top_text_overlaps'):
                assert int(row.get(key, 0)) == 0
            dual.save(OUT / f'{group}_{name}_dual.png')
            dual.resize((800, 992), Image.Resampling.NEAREST).save(OUT / f'{group}_{name}_dual_2x.png')
            rows.append(dict(group=group, **row))
    (OUT / 'verification.json').write_text(json.dumps({
        'origin': f'{len(rows)} actual C++ software-raster duals; not hardware screenshots',
        'scope': 'temporary master filter / echo / roll: held relative pads, real requested/wait/release/tail states; preserved rhythm/sound/both rail and keep-bank' if TRANSITION_FX else 'banks-only transition page: vertical staged swipe, relative rhythm/sound/both rail, lengths/directions and explicit keep bank; no FX pads yet' if TRANSITION else '8px side margins, centered 18px cut squares, no end brackets, real master-left scope and unchanged held-B/edit-bank workflow' if SEQUENCE else 'UI-only a/b capture fill + result, published peak hold/fall' if FEEDBACK else 'four faders + mixer-preserving A/B morph; per-chain 1..64 length and existing three directions',
        'check': 'make test-transition: 6 focused entries' if TRANSITION_FX else 'make test-transition: 4 focused entries' if TRANSITION else 'make test-sequence-banks: 8 focused entries' if SEQUENCE else 'make test-performance: 7 focused entries',
        'wave': 'actual worker-published master-left output after FX/limiter; 200 points sampled every 8 frames (~50ms); fixed +/-1FS; 200B pre-GPU coordinates, frozen under held B; no ornamental animation or AGC' if SEQUENCE else None,
        'peak_objects': max(int(r['shared_objects']) for r in rows), 'frames': rows,
        'deployment': 'none; no ARM build or console audio acceptance',
    }, indent=2) + '\n')
    print(f'{len(rows)} actual C++ duals: text/bounds and object budgets passed')
    print(OUT)


if __name__ == '__main__':
    main()
