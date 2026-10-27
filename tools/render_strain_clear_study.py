#!/usr/bin/env python3
"""Authored Strain visual proposal, NOT a runtime UI or hardware capture.

Reference pixels use six traces from the unchanged real C++ StrainUnit.
Same reset state / 32ms window / 250Hz burst / fixed +/-1FS in every example.
Only tools, preview images and a receipt are written; no shipping code edits.
"""
from dataclasses import dataclass, asdict, replace
from pathlib import Path
import csv
import hashlib
import json
import subprocess
from PIL import Image
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair
from render_fold_wave_study import numeric, rail

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'ui_mockups/strain_clear_study'
X0, X1, N = 64, 306, 1024
KEYS = ('drive', 'bias', 'body', 'teeth', 'feedback', 'mix')  # persisted order
DATA = {}


@dataclass(frozen=True)
class State:
    name: str = '01_default'
    drive: int = 15000
    bias: int = 0
    body: int = 15000
    teeth: int = 12000
    feedback: int = 9000
    mix: int = 24500
    selected: str = 'drive'


BASE = State()
CASES = (BASE, replace(BASE, name='02_teeth', teeth=32767, selected='teeth'),
         replace(BASE, name='03_body', body=27852, selected='body'),
         replace(BASE, name='04_feedback', feedback=32767, selected='feedback'),
         replace(BASE, name='05_bias', bias=19660, selected='bias'),
         replace(BASE, name='06_drive', drive=32767))


def value(s, key):
    if key == 'drive':
        return f'{(32767+7*s.drive)/32768:.2f}', 'x'
    v = round(getattr(s, key)*100/32767)
    return (f'{v:+d}' if key == 'bias' and v else str(v)), '%'


def text_value(s, key):
    return ''.join(value(s, key))


def position(s, key):
    return (s.bias+32767)/65534 if key == 'bias' else getattr(s, key)/32767


def trace(c, rows, key, color):
    # First/last/extrema per column; no normalizing, smoothing or invented tails.
    buckets = [[] for _ in range(X1-X0+1)]
    for i, row in enumerate(rows):
        buckets[i*(X1-X0)//(N-1)].append(row[key])
    previous = None
    for j, bucket in enumerate(buckets):
        assert bucket
        ys = [round(120-v*24/32768) for v in bucket]
        assert all(96 <= y <= 144 for y in ys)
        x = X0+j
        if previous is not None:
            c.line((x-1, previous, x, ys[0]), color)
        c.line((x, min(ys), x, max(ys)), color)
        previous = ys[-1]


def plot(c, s):
    c.line((14, 82, 24, 82), GHOST); c.text(30, 78, 'test 250hz/.75fs', GHOST)
    c.line((268, 82, 278, 82), SPOT); c.text(286, 78, 'out', SPOT)
    for raw, label in ((32767, '+1'), (0, '0'), (-32768, '-1')):
        y = round(120-raw*24/32768)
        c.text(56, y-4, label, GHOST, right=True)
        if raw == 0:
            c.line((X0, y, X1, y), BRANCH)
        else:
            for x in range(X0, X1+1, 5):
                c.d.point((x, y), fill=BRANCH)
    # Burst ends here. The second half is the actual response to digital silence.
    middle = (X0+X1)//2
    for y in range(95, 146, 4):
        c.d.point((middle, y), fill=BRANCH)
    trace(c, DATA[s.name], 'in', GHOST)
    trace(c, DATA[s.name], 'out', SPOT)
    for x, label in ((X0, '0'), (middle, '16'), (X1, '32ms')):
        c.line((x, 146, x, 148), BRANCH)
        c.text(x if x != X1 else x-24, 151, label, GHOST)
    # Fixed topology annotation, not a seventh control or a made-up waveform.
    c.line((X1, 163, X1, 171, 222, 171), BRANCH)
    c.text(99, 167, '1ms / dc / lp return', GHOST)
    c.line((91, 171, X0, 171, X0, 163), BRANCH)
    c.line((X0-3, 166, X0, 163, X0+3, 166), GHOST)


def lower(s):
    c = Page()
    c.text(14, 10, 'strain', SPOT); c.line((14, 23, 49, 23), SPOT)
    c.text(68, 10, 'ch01 / 02', GHOST)
    c.text(258, 10, 'byp', GHOST); c.text(294, 10, 'x', GHOST)
    c.hit('bypass / blocked during p-lock', (246, 0, 284, 25))
    for i, key in enumerate(('drive', 'teeth')):
        x = 14+160*i
        color = SPOT if s.selected == key else INK
        c.text(x, 32, key, color)
        c.text(x+128, 32, 'gain' if key == 'drive' else 'fold', GHOST, right=True)
        c.text(x, 48, text_value(s, key), color)
        rail(c, x, x+128, 65, position(s, key), color)
        c.hit(key+' / fixed relative horizontal drag; tap selects', (8+160*i, 27, 154+160*i, 73))
    plot(c, s)
    for i, key in enumerate(('body', 'bias', 'feedback', 'mix')):
        x = 14+76*i
        color = SPOT if s.selected == key else INK
        c.text(x, 182, 'fdbk' if key == 'feedback' else key, color if s.selected == key else GHOST)
        c.text(x, 196, text_value(s, key), color)
        rail(c, x, x+62, 214, position(s, key), color, bipolar=key == 'bias')
        c.hit(key+' / fixed relative horizontal drag; tap selects', (8+76*i, 177, 80+76*i, 221))
    c.line((14, 223, 306, 223), BRANCH)
    c.footer('b back', 'mod / temp >')
    c.hit('back', (8, 225, 92, 240))
    c.hit('selected mod/temp; hold-l audition retained', (214, 225, 314, 240))
    return c


def upper(s):
    c = Page(400)
    c.text(14, 10, 'osc', GHOST); c.text(44, 10, '/', GHOST)
    c.text(62, 10, 'strain', SPOT); c.line((62, 23, 97, 23), SPOT)
    c.text(110, 10, '/ vca', GHOST); c.text(386, 10, 'ch01 / 02', GHOST, right=True)
    key = s.selected
    titles = {'drive': 'drive / pre-shaper gain', 'bias': 'bias / asymmetric shaping',
              'body': 'body / moving tone and emphasis', 'teeth': 'teeth / fold and edge',
              'feedback': 'feedback / compensated return', 'mix': 'mix / dry and processed'}
    c.text(14, 41, titles[key], SPOT); c.text(14, 56, 'base', GHOST)
    number, suffix = value(s, key)
    numeric(c, number); c.text(14, 150, suffix, GHOST)
    v = text_value(s, key)
    c.text(14, 166, 'stored base '+v, GHOST)
    rail(c, 14, 224, 184, position(s, key), INK, bipolar=key == 'bias')
    lo, hi = {'drive': ('1.00x', '8.00x'), 'bias': ('-100%', '+100%')}.get(key, ('0%', '100%'))
    c.text(14, 199, lo, GHOST); c.text(224, 199, hi, GHOST, right=True)
    c.line((239, 48, 239, 211), BRANCH)
    for y, text, color in ((46, 'example state', GHOST), (60, v, SPOT), (78, 'control '+v, INK),
                           (92, 'target '+v, GHOST), (113, 'mod off', INK), (127, 'depth +0%', GHOST),
                           (148, 'potential range', GHOST), (162, v, INK), (175, v, INK),
                           (191, '| base  <> live', GHOST), (205, 'via base', GHOST)):
        c.text(254, y, text, color)
    notes = {'drive': 'gain into shaping / not output loudness',
             'bias': 'offset scales with drive / wet dc is blocked',
             'body': 'moving poles + emphasis / not one cutoff in hz',
             'teeth': 'fold drive + blend + edge / 0 = soft saturation',
             'feedback': '1ms loop / gain compensated by drive / not decay time',
             'mix': '0 = exact dry / internal loop still runs'}
    c.text(14, 217, notes[key], GHOST)
    c.text(14, 230, 'concept / reset test burst, not live audio', GHOST)
    return c


def protected():
    # Snapshot identities, not backups: this study must not write any shipping file.
    paths = {p for root in ('core', 'platform', 'assets/fonts') for p in (ROOT/root).rglob('*') if p.is_file()}
    paths |= {ROOT/'Makefile', ROOT/'seance.3dsx'}
    paths |= {p for ext in ('*.3dsx', '*.cia', '*.smdh') for p in (ROOT/'release').rglob(ext)}
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}


def probe():
    cmd = ['g++', '-O2', '-std=gnu++17', '-Wall', '-Wextra', '-ffunction-sections', '-fdata-sections',
           '-Icore', 'tools/strain_review.cpp', 'core/engine/units.cpp', '-Wl,--gc-sections', '-o', '/tmp/seance_strain_review']
    (OUT/'build_command.txt').write_text(' '.join(cmd)+'\n')
    with (OUT/'build.log').open('w') as log:
        subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (OUT/'probe.log').open('w') as log:
        subprocess.run(['/tmp/seance_strain_review', str(OUT/'raw')], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    summary = {r['case']: r for r in csv.DictReader((OUT/'raw/summary.tsv').open(), delimiter='\t')}
    for s in CASES:
        assert all(int(summary[s.name][key]) == getattr(s, key) for key in KEYS)
        DATA[s.name] = [{key: int(v) for key, v in row.items()}
                        for row in csv.DictReader((OUT/f'raw/{s.name}.tsv').open(), delimiter='\t')]
        assert len(DATA[s.name]) == N
        assert all(r['sample'] == i and -32768 <= r['out'] <= 32767 for i, r in enumerate(DATA[s.name]))
        assert all(r['in'] == 0 for r in DATA[s.name][512:])
    assert all([r['in'] for r in DATA[s.name]] == [r['in'] for r in DATA[BASE.name]] for s in CASES)
    for s in CASES[1:]:
        assert sum(getattr(s, k) != getattr(BASE, k) for k in KEYS) == 1
        assert any(a['out'] != b['out'] for a, b in zip(DATA[s.name], DATA[BASE.name]))
    return summary


def main():
    before = protected()
    OUT.mkdir(parents=True, exist_ok=True)
    summary = probe()
    hits = lower(BASE).hits
    for key in KEYS:
        for raw in ((-32767, 0, 32767) if key == 'bias' else (0, 16384, 32767)):
            # Layout-only extrema use the base trace; not exported as audio evidence.
            s = replace(BASE, selected=key, **{key: raw})
            assert lower(s).hits == hits
            upper(s)
    allowed = set(paper(400).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH, OUTSIDE}
    board = Canvas(824, 1056, OUTSIDE)
    surfaces = Canvas(664, 540, OUTSIDE)
    report = {'status': 'authored preview UI; real DSP reference data; not runtime/hardware capture',
              'runtime_implemented': False, 'dsp_changed': False, 'deployed': False,
              'reference': '1024 samples / 32kHz / reset + pre-block 1kHz ticks; same nominal .75FS 250Hz burst, 2ms cosine attack + 14ms decay then 16ms silence',
              'plot': 'fixed 32ms / +/-1FS; grey input, violet final mixed output; column first/last/extrema; dotted 16ms burst end; fixed 1ms DC/LP return topology annotation',
              'limits': 'not live PCM, spectrum or static memoryless transfer; not a full 31.25s autonomous body-motion cycle; no 3ds CPU/GPU or listening claim; inspector is an unmodulated example',
              'controls': 'proposal: six fixed disjoint relative-x regions; shared selected inspector/mod-temp; retain hold-l and lock ownership/refusal/removal in future implementation',
              'protected_before': before, 'cases': {}}
    titles = ('01 / current defaults', '02 / teeth at 100%', '03 / body at 85%',
              '04 / feedback at 100%', '05 / bias at +60%', '06 / drive at 8.00x')
    for i, s in enumerate(CASES):
        u, b = upper(s), lower(s)
        dual = pair(u.im, b.im)
        for name, im in (('top', u.im), ('bottom', b.im), ('dual', dual)):
            assert set(im.get_flattened_data()) <= allowed
            im.save(OUT/f'{s.name}_{name}_native.png')
            im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST).save(OUT/f'{s.name}_{name}_2x.png')
        if i < 4:
            bx, by = 8+i%2*408, i//2*528
            board.text(bx+6, by+8, titles[i], BRANCH); board.im.paste(dual, (bx, by+26))
            sx, sy = 8+i%2*328, i//2*270
            surfaces.text(sx+6, sy+8, titles[i], BRANCH); surfaces.im.paste(b.im, (sx, sy+26))
        report['cases'][s.name] = {'state': asdict(s), 'touch_regions': b.hits,
                                  'labels': u.labels+b.labels, 'dsp_probe': summary[s.name]}
    for name, im in (('comparison', board.im), ('surfaces', surfaces.im)):
        im.save(OUT/f'{name}_native.png')
        im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST).save(OUT/f'{name}_2x.png')
    assert before == protected(), 'shipping source or console artifact changed'
    report['checks'] = ['33 real DSP reset/dry/silence/subdivision checks', 'six identical-input real DSP fixtures; five single-parameter changes',
                        '18 selected extrema layout bounds/nonoverlap + fixed disjoint hit regions',
                        'six exported duals exact palette / nearest-only scaling', 'all protected source/artifact hashes unchanged']
    report['files'] = {str(p.relative_to(OUT)): hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in sorted((OUT/'raw').glob('*.tsv'))}
    report['probe_sources'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in (ROOT/'tools/strain_review.cpp', Path(__file__))}
    (OUT/'concept.json').write_text(json.dumps(report, indent=2)+'\n')
    (OUT/'README.txt').write_text(__doc__+'\nreproduce: python3 tools/render_strain_clear_study.py\n'
        'four-state comparison: defaults / teeth100 / body85 / feedback100. additional bias+60 / drive8 duals.\n'
        'each changes exactly one DSP parameter from defaults. percent feedback is a control, not literal loop gain.\n'
        'body is a moving multi-stage tone control, not a single cutoff. no runtime migration or upload.\n')
    print('strain preview: six duals; DSP + layout + palette + shipping identity guards passed')
    print(OUT/'01_default_dual_2x.png')
    print(OUT/'surfaces_2x.png')


if __name__ == '__main__':
    main()
