#!/usr/bin/env python3
"""Authored Ring UI proposal; waveform data from the unchanged real C++ RingUnit.

Not an actual UI/hardware capture. Same reset 32ms test burst in every fixture;
carrier is reconstructed from the real unit's published samplewise phase.
Only tools, proposal images and a review receipt are written. No shipping edits.
"""
from dataclasses import dataclass, asdict, replace
from pathlib import Path
import csv
import hashlib
import json
import math
import re
import subprocess
from PIL import Image
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair
from render_fold_wave_study import numeric, rail

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'ui_mockups/ring_clear_study'
X0, X1, N = 64, 306, 1024
KEYS = ('freq', 'depth', 'feedback', 'diode', 'mix', 'follow')  # persisted order
SOURCE = (ROOT/'core/engine/units.cpp').read_text()
INC = tuple(map(int, re.findall(r'(\d+)u', re.search(r'RING_INC_LUT\[65\] = \{(.*?)\};', SOURCE, re.S)[1])))
assert len(INC) == 65


@dataclass(frozen=True)
class State:
    name: str = '01_clean'
    freq: int = 19000
    depth: int = 24576
    feedback: int = 0
    diode: int = 0
    mix: int = 24576
    follow: int = 0
    selected: str = 'freq'


BASE = State()
CASES = (BASE, replace(BASE, name='02_bridge', diode=32767, selected='diode'),
         replace(BASE, name='03_feedback', feedback=22937, selected='feedback'),
         replace(BASE, name='04_follow', follow=32767, selected='follow'))
DATA = {}


def increment(raw):
    i, f = raw*64 >> 15, raw*64 & 32767
    return INC[i]+((INC[i+1]-INC[i])*f >> 15)


def hz(raw):
    return increment(raw)*32000/2**32


def rate(raw):
    n = hz(raw)
    return (f'{n/1000:.2f}', 'khz') if n >= 1000 else (f'{n:.2f}'.rstrip('0').rstrip('.'), 'hz')


def value(s, key):
    if key == 'freq':
        return rate(s.freq)
    v = round(getattr(s, key)*100/32767)
    return (f'{v:+d}' if key == 'feedback' and v else str(v), '%')


def text_value(s, key):
    return ''.join(value(s, key))


def position(s, key):
    return (s.feedback+32767)/65534 if key == 'feedback' else getattr(s, key)/32767


def trace(c, rows, key, center, amplitude, color):
    # Per-column extrema retain dense high-rate excursions; no normalization.
    buckets = [[] for _ in range(X1-X0+1)]
    for i, row in enumerate(rows):
        buckets[i*(X1-X0)//(N-1)].append(row[key])
    previous = None
    for j, bucket in enumerate(buckets):
        assert bucket
        ys = [round(center-v*amplitude/32768) for v in bucket]
        assert all(center-amplitude <= y <= center+amplitude for y in ys)
        x = X0+j
        if previous is not None:
            c.line((x-1, previous, x, ys[0]), color)
        c.line((x, min(ys), x, max(ys)), color)
        previous = ys[-1]


def plot(c, s):
    c.line((14, 82, 24, 82), GHOST); c.text(30, 78, 'test 250hz/.75fs', GHOST)
    c.line((268, 82, 278, 82), SPOT); c.text(286, 78, 'out', SPOT)
    for raw, label in ((32767, '+1'), (0, '0'), (-32768, '-1')):
        y = round(117-raw*21/32768)
        c.text(56, y-4, label, GHOST, right=True)
        if raw == 0:
            c.line((X0, y, X1, y), BRANCH)
        else:
            for x in range(X0, X1+1, 5):
                c.d.point((x, y), fill=BRANCH)
    rows = DATA[s.name]
    trace(c, rows, 'in', 117, 21, GHOST)
    trace(c, rows, 'out', 117, 21, SPOT)
    c.text(14, 150, 'carrier', GHOST)
    c.line((X0, 154, X1, 154), BRANCH)
    trace(c, rows, 'carrier', 154, 6, INK)
    for x, label in ((X0, '0'), ((X0+X1)//2, '16'), (X1, '32ms')):
        c.line((x, 162, x, 164), BRANCH)
        c.text(x if x != X1 else x-24, 167, label, GHOST)


def lower(s):
    c = Page()
    c.text(14, 10, 'ring', SPOT); c.line((14, 23, 37, 23), SPOT)
    c.text(56, 10, 'ch01 / 02', GHOST)
    c.text(258, 10, 'byp', GHOST); c.text(294, 10, 'x', GHOST)
    c.hit('bypass / blocked during p-lock', (246, 0, 284, 25))
    for i, key in enumerate(('freq', 'depth')):
        x = 14+160*i
        color = SPOT if s.selected == key else INK
        c.text(x, 32, key, color)
        c.text(x+128, 32, 'base' if key == 'freq' else 'core', GHOST, right=True)
        c.text(x, 48, text_value(s, key), color)
        rail(c, x, x+128, 65, position(s, key), color)
        c.hit(key+' / fixed relative horizontal drag; tap selects', (8+160*i, 27, 154+160*i, 73))
    plot(c, s)
    for i, key in enumerate(('feedback', 'diode', 'follow', 'mix')):
        x = 14+76*i
        color = SPOT if s.selected == key else INK
        c.text(x, 182, 'fdbk' if key == 'feedback' else key, color if s.selected == key else GHOST)
        c.text(x, 196, text_value(s, key), color)
        rail(c, x, x+62, 214, position(s, key), color, bipolar=key == 'feedback')
        c.hit(key+' / fixed relative horizontal drag; tap selects', (8+76*i, 177, 80+76*i, 221))
    c.line((14, 223, 306, 223), BRANCH)
    c.footer('b back', 'mod / temp >')
    c.hit('back', (8, 225, 92, 240))
    c.hit('selected mod/temp; hold-l audition retained', (214, 225, 314, 240))
    return c


def upper(s):
    c = Page(400)
    c.text(14, 10, 'osc', GHOST); c.text(44, 10, '/', GHOST)
    c.text(62, 10, 'ring', SPOT); c.line((62, 23, 85, 23), SPOT)
    c.text(98, 10, '/ vca', GHOST); c.text(386, 10, 'ch01 / 02', GHOST, right=True)
    key = s.selected
    titles = {'freq': 'freq / base carrier', 'depth': 'depth / modulation core',
              'feedback': 'feedback / signed return', 'diode': 'diode / multiplier to bridge',
              'mix': 'mix / dry and processed', 'follow': 'follow / input level to carrier rate'}
    c.text(14, 41, titles[key], SPOT); c.text(14, 56, 'base', GHOST)
    number, suffix = value(s, key)
    numeric(c, number); c.text(14, 150, suffix, GHOST)
    v = text_value(s, key)
    c.text(14, 166, 'stored base '+v, GHOST)
    rail(c, 14, 224, 184, position(s, key), INK, bipolar=key == 'feedback')
    lo, hi = {'freq': ('0.5hz', '8.19khz'), 'feedback': ('-100%', '+100%')}.get(key, ('0%', '100%'))
    c.text(14, 199, lo, GHOST); c.text(224, 199, hi, GHOST, right=True)
    c.line((239, 48, 239, 211), BRANCH)
    for y, text, color in ((46, 'example state', GHOST), (60, v, SPOT), (78, 'control '+v, INK),
                           (92, 'target '+v, GHOST), (113, 'mod off', INK), (127, 'depth +0%', GHOST),
                           (148, 'potential range', GHOST), (162, v, INK), (175, v, INK),
                           (191, '| base  <> live', GHOST), (205, 'via base', GHOST)):
        c.text(254, y, text, color)
    notes = {'freq': 'follow can raise rate / capped at 12khz',
             'depth': '0 = exact dry / 100% = full core before mix',
             'feedback': 'loop gain limited to +/-0.85 x depth',
             'diode': '0 = multiply / 100% = diode bridge',
             'mix': 'dry bypasses bridge / limiter / dc blocker',
             'follow': 'input level, not pitch tracking / up to 8x'}
    c.text(14, 217, notes[key], GHOST)
    c.text(14, 230, 'concept / reset test burst, not live audio', GHOST)
    return c


def protected():
    names = ('core/engine/units.cpp', 'core/engine/units.h', 'core/engine/unit.h',
             'core/engine/engine.cpp', 'core/engine/patch_io.cpp', 'core/audio/fixed.h',
             'platform/3ds/ui_module_focus_ring.cpp', 'platform/3ds/module_inspector.cpp',
             'platform/3ds/ui_param_lens.cpp', 'platform/3ds/ui_editor.cpp', 'platform/3ds/ui_state.h',
             'platform/3ds/main.cpp', 'platform/3ds/note_display_data.h', 'core/ui/font.cpp',
             'platform/3ds/ui_module_focus_bit.cpp', 'platform/3ds/ui_module_focus_tilt.cpp',
             'release/fold-wave/seance.3dsx', 'seance.3dsx')
    return {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in names}


def probe():
    cmd = ['g++', '-O2', '-std=gnu++17', '-Wall', '-Wextra', '-ffunction-sections', '-fdata-sections',
           '-Icore', 'tools/ring_review.cpp', 'core/engine/units.cpp', '-Wl,--gc-sections', '-o', '/tmp/seance_ring_review']
    (OUT/'build_command.txt').write_text(' '.join(cmd)+'\n')
    with (OUT/'build.log').open('w') as log:
        subprocess.run(cmd, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    with (OUT/'probe.log').open('w') as log:
        subprocess.run(['/tmp/seance_ring_review', str(OUT/'raw')], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    summaries = {row['case']: row for row in csv.DictReader((OUT/'raw/summary.tsv').open(), delimiter='\t')}
    for s in CASES:
        assert all(int(summaries[s.name][key]) == getattr(s, key) for key in KEYS)
        DATA[s.name] = [{key: int(v) for key, v in row.items()}
                        for row in csv.DictReader((OUT/f'raw/{s.name}.tsv').open(), delimiter='\t')]
        assert len(DATA[s.name]) == N
    assert all([r['in'] for r in DATA[s.name]] == [r['in'] for r in DATA[BASE.name]] for s in CASES)
    # Frequency/phase reconstruction independently checks the display LUT mapping,
    # with only the low 16 unexposed phase bits as quantization uncertainty.
    for s in CASES:
        phase = 0
        for row in DATA[s.name]:
            amount = row['envelope']*s.follow >> 15
            inc = min(1610612736, increment(s.freq)+(increment(s.freq)*amount*7 >> 15))
            phase = (phase+inc) & 0xffffffff
            assert phase >> 16 == row['phase']
    return summaries


def validate():
    hits = lower(BASE).hits
    for key in KEYS:
        for v in ((-32767, 0, 32767) if key == 'feedback' else (0, 16384, 32767)):
            # Geometry check only: the fixture waveform stays BASE, deliberately
            # not exported/claimed to be the audio for these unrendered extrema.
            s = replace(BASE, selected=key, **{key: v})
            assert lower(s).hits == hits
            upper(s)
    assert abs(hz(0)-.5) < .00001 and 8189 < hz(32767) < 8192
    assert all(increment(i) < increment(i+1) for i in range(32767))


def main():
    before = protected()
    OUT.mkdir(parents=True, exist_ok=True)
    summaries = probe()
    validate()
    allowed = set(paper(400).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH, OUTSIDE}
    board = Canvas(824, 1056, OUTSIDE)
    surfaces = Canvas(664, 540, OUTSIDE)
    report = {'status': 'authored preview UI, not actual runtime/hardware capture',
              'runtime_implemented': False, 'dsp_changed': False, 'deployed': False,
              'reference': 'unchanged real RingUnit::process, reset phase/state, 1024 samples at 32khz; 250hz burst with 4ms raised-cosine attack + 28ms decay, nominal .75fs peak',
              'plot': 'fixed 32ms / +/-1fs, no normalization; grey input, violet final mixed output; thin separate actual polynomial carrier derived from published phase, +/-1 on its own smaller strip',
              'limits': 'not source PCM or spectrum; does not show a whole slow tremolo cycle; no 3ds CPU/GPU measurement; inspector values are unmodulated examples, not engine telemetry',
              'gestures': 'proposal: six fixed disjoint relative drags, selected inspector/mod/temp, retained hold-l; lock-aware origins/removal/refusal/context ownership to reuse from Tilt/Bit, not yet implemented',
              'freq_endpoints_hz': [hz(0), hz(32767)], 'default_base_hz': hz(BASE.freq),
              'protected_before': before, 'cases': {}}
    titles = ('01 / current defaults', '02 / diode bridge', '03 / positive feedback', '04 / envelope follows input')
    for i, s in enumerate(CASES):
        u, b = upper(s), lower(s)
        dual = pair(u.im, b.im)
        for name, im in (('top', u.im), ('bottom', b.im), ('dual', dual)):
            assert set(im.get_flattened_data()) <= allowed
            im.save(OUT/f'{s.name}_{name}_native.png')
            im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST).save(OUT/f'{s.name}_{name}_2x.png')
        bx, by = 8+i%2*408, i//2*528
        board.text(bx+6, by+8, titles[i], BRANCH); board.im.paste(dual, (bx, by+26))
        sx, sy = 8+i%2*328, i//2*270
        surfaces.text(sx+6, sy+8, titles[i], BRANCH); surfaces.im.paste(b.im, (sx, sy+26))
        report['cases'][s.name] = {'state': asdict(s), 'touch_regions': b.hits, 'labels': u.labels+b.labels,
                                  'dsp_probe': summaries[s.name]}
    for name, im in (('comparison', board.im), ('surfaces', surfaces.im)):
        im.save(OUT/f'{name}_native.png')
        im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST).save(OUT/f'{name}_2x.png')
    assert before == protected(), 'protected source or console artifact changed'
    report['checks'] = ['24 real DSP dry/silence/reset/subdivision probes', '4096 sample phase checks against exact current frequency/follow mapping',
                        '18 selected extrema text bounds/nonoverlap + fixed disjoint hits', '32768-position frequency monotonicity',
                        'four exported states exact palette / nearest-only scaling', 'protected source/artifact hashes unchanged']
    report['files'] = {str(p.relative_to(OUT)): hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in sorted((OUT/'raw').glob('*.tsv'))}
    report['probe_sources'] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                               for p in (ROOT/'tools/ring_review.cpp', Path(__file__))}
    (OUT/'concept.json').write_text(json.dumps(report, indent=2)+'\n')
    (OUT/'README.txt').write_text(__doc__+'\nreproduce: python3 tools/render_ring_clear_study.py\n'
        'four fixtures share defaults; only diode, feedback or follow changes in cases 2..4.\n'
        'the 32ms window cannot depict a complete 0.5hz tremolo cycle; no auto time scaling.\n'
        'carrier strip is a reference, not live phase animation. runtime budget remains untested.\n')
    print('ring preview: four duals; real DSP probes + layout/phase/palette/source guards passed')
    print(OUT/'01_clean_dual_2x.png')
    print(OUT/'surfaces_2x.png')


if __name__ == '__main__':
    main()
