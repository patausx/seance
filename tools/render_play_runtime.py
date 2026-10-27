#!/usr/bin/env python3
"""Package four actual PLAY/TRIM C++ dual frames. No authored UI pixels or build."""
import csv
import json
import subprocess
from pathlib import Path
from PIL import Image
from render_fold_runtime import ROOT, sha, audit_text

OUT = ROOT / 'ui_mockups/play_runtime'
SOURCES = [
    'core/engine/units.h', 'core/engine/units.cpp', 'core/engine/play_window.h',
    'core/engine/play_mode.h', 'core/engine/engine.h', 'core/engine/engine.cpp',
    'core/engine/patch_io.cpp', 'platform/3ds/ui_module_focus_play.cpp',
    'platform/3ds/ui_module_focus_internal.h', 'platform/3ds/module_inspector.cpp',
    'platform/3ds/ui_param_lens.cpp', 'platform/3ds/ui_editor.cpp',
    'platform/3ds/ui_sequencer.cpp', 'tools/sequencer_test.cpp',
    'tools/play_ui_test.h', 'tools/play_core_test.cpp', 'tools/module_inspector_test.h',
    'tools/sequencer_cuts_test.h', 'tools/render_play_runtime.py', 'Makefile',
]

def main():
    raw = OUT / 'raw'
    raw.mkdir(parents=True, exist_ok=True)
    binary = Path('/tmp/seance_chorus_ui_test')
    hashes = {p: sha(ROOT/p) for p in SOURCES}
    before = sha(binary)
    run = subprocess.run([str(binary), '--dump-play-ui', str(raw)], cwd=ROOT, capture_output=True, text=True)
    (OUT/'capture.log').write_text(run.stdout + run.stderr)
    run.check_returncode()
    rows = list(csv.DictReader((raw/'manifest.tsv').open(), delimiter='\t'))
    assert [r['frame'] for r in rows] == ['01_play_gate', '02_trim_region', '03_trim_zoom', '04_one_shot_playing']
    receipt = dict(origin='actual native C++ controller/capture/draw host raster, not hardware acceptance',
                   screen_pixels_drawn_in_python=False, sources=hashes, binary_sha256=before,
                   frame_bound_bytes=1024, capture='301 columns x8 probes xup to2channels; pointer-free pre-GPU frame; actual sample envelope and source position',
                   waveform_limits='sampled extrema with visual-only gain, not exhaustive transient peak scan; trim resolution existing q15 units',
                   budget_scope='visible top+bottom, excludes hidden Chains underlay',
                   frames=[], peak_shared_objects=0, arm_or_ftp=False)
    sheet = Image.new('RGB', (640, 480), 'black')
    for i, row in enumerate(rows):
        name = row['frame']; count = int(row['shared_objects'])
        assert 0 < count <= 8192 and count == int(row['top_objects'])+int(row['bottom_objects'])
        assert int(row['bottom_clipped']) == int(row['bottom_overlaps']) == 0
        images, surfaces = {}, {}
        for screen, width in [('top',400), ('bottom',320)]:
            path = raw/f'{name}_{screen}.ppm'; image = Image.open(path).convert('RGB')
            assert image.size == (width,240)
            surfaces[screen] = dict(sha256=sha(path), text=audit_text(raw/f'{name}_{screen}_text.tsv',width))
            images[screen] = image
        dual = Image.new('RGB', (400,480), 'black'); dual.paste(images['top'],(0,0)); dual.paste(images['bottom'],(40,240))
        dual.save(OUT/f'{name}_dual_native.png'); dual.resize((800,960),Image.Resampling.NEAREST).save(OUT/f'{name}_dual_2x.png')
        images['bottom'].resize((640,480),Image.Resampling.NEAREST).save(OUT/f'{name}_bottom_2x.png')
        sheet.paste(images['bottom'],((i%2)*320,(i//2)*240))
        receipt['frames'].append(dict(row,surfaces=surfaces)); receipt['peak_shared_objects']=max(receipt['peak_shared_objects'],count)
    sheet.save(OUT/'modes_native.png'); sheet.resize((1280,960),Image.Resampling.NEAREST).save(OUT/'modes_2x.png')
    assert {p:sha(ROOT/p) for p in SOURCES} == hashes and sha(binary) == before
    receipt['checks']={p.name:sha(p) for p in sorted((OUT/'checks').glob('*.log'))}
    (OUT/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f"{OUT}: four actual C++ duals; peak {receipt['peak_shared_objects']}/8192, no text collisions/bounds")

if __name__ == '__main__':
    main()
