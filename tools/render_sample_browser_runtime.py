#!/usr/bin/env python3
"""Package four actual C++ sample-browser LCD pairs; no Python-authored UI or build."""
import csv
import hashlib
import json
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/sample_browser_runtime'
BASE = ROOT / '.local-state/backups/samples-go-to-baseline'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(['/tmp/seance_sample_browser_test', '--dump-samples', str(OUT)],
                            cwd=ROOT, check=True, text=True, capture_output=True)
    (OUT / 'capture.log').write_text(result.stdout + result.stderr)
    rows = list(csv.DictReader((OUT / 'frames.tsv').open(), delimiter='\t'))
    assert len(rows) == 4
    for row in rows:
        name = row['name']
        top = Image.open(OUT / (name + '_top.ppm')).convert('RGB')
        bottom = Image.open(OUT / (name + '_bottom.ppm')).convert('RGB')
        assert top.size == (400, 240) and bottom.size == (320, 240)
        dual = Image.new('RGB', (400, 480), (31, 20, 31))
        dual.paste(top, (0, 0)); dual.paste(bottom, (40, 240))
        for suffix, image in [('top', top), ('bottom', bottom), ('dual', dual)]:
            image.save(OUT / f'{name}_{suffix}.png')
            image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST).save(
                OUT / f'{name}_{suffix}_2x.png')
        assert int(row['out_of_bounds']) == 0
        assert int(row['top_objects']) + int(row['bottom_objects']) < 8192
    preserved = json.loads((BASE / 'source-identity.json').read_text())
    for path, expected in preserved.items():
        assert sha(ROOT / path) == expected, path
    old = (BASE / 'platform/3ds/menu_labels_data.h').read_text()
    new = (ROOT / 'platform/3ds/menu_labels_data.h').read_text()
    # Appended masks cannot move or modify any previous menu label.
    old_bits = old.split('bitmap[] = {', 1)[1].split('};', 1)[0].strip().split(',')
    new_bits = new.split('bitmap[] = {', 1)[1].split('};', 1)[0].strip().split(',')
    old_bits = [x.strip() for x in old_bits if x.strip()]
    new_bits = [x.strip() for x in new_bits if x.strip()]
    assert new_bits[:len(old_bits)] == old_bits
    for label in old.split('labels[] = {', 1)[1].split('};', 1)[0].splitlines():
        if label.strip(): assert label in new
    # Entire preexisting library/controller is unchanged except the additive
    # file-preview waveform cache; all IO/ownership/mutation bodies stay exact.
    original = (BASE / 'platform/3ds/ui_sample_library.cpp').read_text()
    current = (ROOT / 'platform/3ds/ui_sample_library.cpp').read_text()
    original = original[original.index('static SampleFileInfo g_sample_file_info'):original.index('void sample_browser_draw()')]
    current = current[current.index('static SampleFileInfo g_sample_file_info'):current.index('namespace {\nstruct SampleFileView')]
    current = current.replace('    g_sample_file_wave = {};\n    g_sample_file_wave_path[0] = 0;\n', '')
    addition = '''    // Cache only the decoded portion that this preview actually plays. Staged PCM
    // is UI-owned here, outside the audio guard and before the ownership swap.
    sample_wave_capture(staged, g_sample_file_wave);
    std::snprintf(g_sample_file_wave_path, sizeof g_sample_file_wave_path, "%s", path);
'''
    current = current.replace(addition, '')
    assert current == original, 'unexpected sample controller/IO change'
    main_source = (ROOT / 'platform/3ds/main.cpp').read_text()
    assert main_source.index('        sample_browser_capture();') < main_source.index('        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;')
    paths = ['platform/3ds/ui_sample_library.cpp', 'platform/3ds/ui_sample_library.h',
             'platform/3ds/main.cpp', 'platform/3ds/ui_top.cpp', 'platform/3ds/menu_labels_data.h',
             'tools/generate_menu_labels.py', 'tools/menu_stage1_test.cpp', 'tools/sample_browser_test.h',
             'tools/render_sample_browser_runtime.py', 'tools/sequencer_test.cpp', 'Makefile']
    receipt = {
        'origin': '4 real C++ software-raster dual screens; actual browser capture/controller/draw, WAV IO and portable Engine',
        'not_claimed': 'hardware/LCD/audio-thread performance; full main loop, new persistence proof, ARM build or deployment',
        'check': 'make test-sample-browser', 'suites': 4, 'frame_bytes': 2568,
        'max_pcm_reads_per_slot_capture': 352 * 8 * 2, 'wave': 'fixed +/-full scale, 8 probes/bin, L/R extrema union; not exhaustive peaks, no fake playhead',
        'files_wave': 'the actual decoded/resampled/truncated preview portion, cached before preview ownership swap',
        'capture_summary': result.stdout.strip().splitlines()[-1], 'frames': rows,
        'unchanged_prior_heading_masks': 32, 'unchanged_sample_controller_except_additive_wave_cache': True,
        'protected_sha256': preserved, 'source_sha256': {path: sha(ROOT / path) for path in paths},
        'deployment': 'none',
    }
    (OUT / 'verification.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(result.stdout.strip().splitlines()[-1])
    print(OUT / '02_slots_wave_dual_2x.png')


if __name__ == '__main__':
    main()
