#!/usr/bin/env python3
"""Package actual C++ Strain dual frames only; no UI drawing, DSP model or deployment."""
import argparse
import csv
import json
from pathlib import Path
import re
import subprocess
from PIL import Image
from render_fold_runtime import ROOT, audit_text, sha

PARAMS = ('drive', 'bias', 'body', 'teeth', 'feedback', 'mix')
BASE = dict(zip(PARAMS, (15000, 0, 15000, 12000, 9000, 24500)))
MAIN = ('01_default', '02_teeth', '03_body', '04_feedback')
SOURCES = [
    'platform/3ds/ui_module_focus_strain.cpp', 'platform/3ds/strain_reference.h',
    'platform/3ds/strain_reference_data.h',
    'platform/3ds/module_inspector.cpp', 'platform/3ds/module_inspector.h',
    'platform/3ds/ui_module_focus_internal.h', 'platform/3ds/ui_param_lens.cpp',
    'platform/3ds/ui_editor.cpp', 'tools/strain_ui_test.h',
    'tools/module_inspector_test.h', 'tools/sequencer_test.cpp',
    'tools/render_strain_runtime.py', 'tools/generate_strain_reference.py', 'Makefile',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('/tmp/seance_chorus_ui_test'))
    parser.add_argument('--output', type=Path, default=ROOT/'ui_mockups/strain_runtime')
    args = parser.parse_args()
    out = args.output.resolve()
    raw = out/'raw'
    raw.mkdir(parents=True, exist_ok=True)
    preview = json.loads((ROOT/'ui_mockups/strain_clear_study/concept.json').read_text())
    changed = {'platform/3ds/ui_module_focus_strain.cpp', 'platform/3ds/module_inspector.cpp',
               'platform/3ds/ui_module_focus_internal.h', 'platform/3ds/ui_param_lens.cpp',
               'platform/3ds/ui_editor.cpp', 'Makefile'}
    protected = {p: h for p, h in preview['protected_before'].items() if p not in changed}
    for path, expected in protected.items():
        assert sha(ROOT/path) == expected, path
    constants = subprocess.run(['python3', 'tools/generate_strain_reference.py', '--check'],
                               cwd=ROOT, capture_output=True, text=True, check=True)
    sources = {p: sha(ROOT/p) for p in SOURCES}
    binary_sha = sha(args.binary)
    run = subprocess.run([str(args.binary), '--dump-strain', str(raw)], cwd=ROOT,
                         capture_output=True, text=True)
    (out/'capture.log').write_text(run.stdout+run.stderr)
    run.check_returncode()
    rows = list(csv.DictReader((raw/'manifest.tsv').open(), delimiter='\t'))
    assert len(rows) == 18 and len({r['frame'] for r in rows}) == 18
    indexed = {r['frame']: r for r in rows}
    assert tuple(r['frame'] for r in rows[:4]) == MAIN
    for name, param, value in zip(MAIN, ('drive', 'teeth', 'body', 'feedback'), (15000, 32767, 27852, 32767)):
        expected = dict(BASE, **{param: value})
        assert {p: int(indexed[name][p]) for p in PARAMS} == expected
    for name, param, value in [('05_bias', 'bias', 19660), ('06_drive', 'drive', 32767)]:
        assert {p: int(indexed[name][p]) for p in PARAMS} == dict(BASE, **{param: value})
    source = (ROOT/'platform/3ds/ui_module_focus_strain.cpp').read_text()
    bound = re.search(r'sizeof\(Frame\)\s*<=\s*(\d+)', source)
    assert bound and int(bound[1]) <= 2176, 'runtime must assert a fixed bounded frame'
    receipt = {
        'origin': 'actual C++ native host raster; line coverage is software approximation, not PICA/hardware acceptance',
        'scope': 'approved Strain UI only, not Stranular; unchanged core DSP/engine/storage, local Ring/Tilt/Bit/fonts/artifacts preserved; no ARM build/upload',
        'screen_pixels_drawn_in_python': False,
        'reference': 'private unchanged reset StrainUnit; 1024 samples at32khz, pre-block1khz ticks;250hz nominal .75fs burst2ms cosine attack+14ms release then16ms silence; not live audio',
        'axes': 'fixed32ms / +/-1fs,243columns first/last/extrema,no normalization;grey input/violet final mixed output;16ms marker,fixed1ms DC/LP return annotation;not full31.25s body-motion cycle',
        'comparison': 'four approved states share defaults, only teeth/body/fdbk differs; actual chain osc/strain/vca; additional bias+60/drive8 remain single-param changes',
        'capture': {'frame_bound_bytes': int(bound[1]), 'contract': 'pointer-free frozen lower; private cached reference outside guard/before GPU; draws detached from engine+audio'},
        'cache_proof': 'host linker StrainUnit::process instrumentation: no live-unit processing,no DSP cache hits/draw; six effective params independently invalidate; bypass no process,mix0 advances',
        'cost_limits': 'optimized native host compute timing in checks/focused.log only; not Old3DS CPU,wall frame-time or audio budget evidence',
        'focused_reference_coverage': {'plots_including_bypass': 48, 'exact_private_pcm_samples': 24576,
                                       'independent_source_samples': 1024, 'reset_subdivision_silence_dry_checks': 120,
                                       'frozen_extrema_pairs': 36},
        'budget_scope': 'visible upper+lower/lens shared <=8192; excludes hidden main Chains underlay',
        'binary_sha256': binary_sha, 'protected': protected,
        'reference_constants_check': constants.stdout.strip(),
        'sources': sources, 'frames': [], 'peak_shared_objects': 0,
    }
    sheet = Image.new('RGB', (1600, ((len(rows)+3)//4)*480), 'black')
    duals, bottoms = {}, {}
    for i, row in enumerate(rows):
        name = row['frame']
        surfaces, images = {}, {}
        for screen, width in [('top', 400), ('bottom', 320)]:
            path = raw/f'{name}_{screen}.ppm'
            image = Image.open(path).convert('RGB')
            assert image.size == (width, 240)
            images[screen] = image
            surfaces[screen] = {'sha256': sha(path), 'text': audit_text(raw/f'{name}_{screen}_text.tsv', width)}
        count = int(row['shared_objects'])
        assert 0 < count <= 8192 and count == int(row['top_objects'])+int(row['bottom_objects'])
        assert int(row['bottom_clipped']) == int(row['bottom_overlaps']) == 0
        dual = Image.new('RGB', (400, 480), 'black')
        dual.paste(images['top'], (0, 0))
        dual.paste(images['bottom'], (40, 240))
        dual.save(out/f'{name}_dual_native.png')
        dual.resize((800, 960), Image.Resampling.NEAREST).save(out/f'{name}_dual_2x.png')
        sheet.paste(dual, ((i % 4)*400, (i//4)*480))
        duals[name], bottoms[name] = dual, images['bottom']
        receipt['frames'].append(dict(row, surfaces=surfaces))
        receipt['peak_shared_objects'] = max(receipt['peak_shared_objects'], count)
    sheet.save(out/'contact_sheet_native.png')
    comparison = Image.new('RGB', (800, 960), 'black')
    surfaces = Image.new('RGB', (640, 480), 'black')
    for i, name in enumerate(MAIN):
        comparison.paste(duals[name], ((i % 2)*400, (i//2)*480))
        surfaces.paste(bottoms[name], ((i % 2)*320, (i//2)*240))
    for name, image in [('comparison', comparison), ('surfaces', surfaces)]:
        image.save(out/f'{name}_native.png')
        image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST).save(out/f'{name}_2x.png')
    receipt['checks'] = {p.name: {'sha256': sha(p), 'log': str(p.relative_to(out))}
                         for p in sorted((out/'checks').glob('*.log'))}
    receipt['raw_manifest_sha256'] = sha(raw/'manifest.tsv')
    assert sha(args.binary) == binary_sha, 'binary changed during capture'
    assert {p: sha(ROOT/p) for p in SOURCES} == sources, 'source changed during capture'
    for path, expected in protected.items():
        assert sha(ROOT/path) == expected, path
    (out/'verification.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(f"{out}: {len(rows)} actual C++ pairs, peak {receipt['peak_shared_objects']}/8192; strict text/protected identity audits passed")


if __name__ == '__main__':
    main()
