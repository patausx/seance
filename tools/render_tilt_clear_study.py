#!/usr/bin/env python3
"""Preview only: Tilt + wet trim + quick zero. No runtime/DSP/patch edits.

Authored native UI, not a C++/hardware capture. Response is a small-signal
proposal: existing one-pole coefficient and shelf LUTs, fractional gain-table
interpolation, proposed post-drive/pre-mix trim. No saturation/PCM simulation.
"""
from dataclasses import dataclass, asdict, replace
from pathlib import Path
import cmath
import hashlib
import json
import math
import re
from PIL import Image
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair
from render_fold_wave_study import numeric, rail, diamond

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/tilt_clear_study'
X0, X1, Y0, Y1 = 34, 306, 93, 151
DB_MAX = 12.0
SOURCE = (ROOT/'core/engine/units.cpp').read_text().split('void TiltUnit::process', 1)[1].split('// ============================ RING', 1)[0]


def lut(name):
    body = re.search(r'\b'+name+r'\[\d+\] = \{(.*?)\};', SOURCE, re.S).group(1)
    return tuple(map(int, re.findall(r'\d+', body)))


COEFF, BOOST, CUT = lut('PIVOT_COEFF'), lut('BOOST_Q16'), lut('CUT_Q16')
assert (len(COEFF), len(BOOST), len(CUT)) == (65, 33, 33)


@dataclass(frozen=True)
class State:
    name: str = '01_air'
    tilt: float = 85.0
    pivot: int = 16384
    range_raw: int = 21846
    drive: int = 0
    trim: float = 0.0  # proposed -24..+6 dB, unity default, wet only
    mix: int = 100
    selected: str = 'tilt'

    @property
    def max_step(self):
        return 8 + (self.range_raw*24 >> 15)

    @property
    def range_db(self):
        return self.max_step*.375

    @property
    def hz(self):
        return 125*2**(6*self.pivot/32768)

    @property
    def drive_gain(self):
        return 1 + self.drive*6/65536


BASE = State()
CASES = (BASE, replace(BASE, name='02_body', tilt=-85),
         replace(BASE, name='03_zero', tilt=0),
         replace(BASE, name='04_trim', drive=10923, trim=-6, selected='trim'))
KEYS = ('tilt', 'pivot', 'range', 'drive', 'trim', 'mix')


def interpolation(table, position):
    i = min(len(table)-2, int(position))
    fraction = position-i
    return table[i] + (table[i+1]-table[i])*fraction


def response(s, hz):
    # Preserve the existing integer pivot interpolation. Only shelf gain selection
    # is proposed continuous. This is not certification of future fixed-point DSP.
    scaled = s.pivot*64
    i, f = scaled >> 15, scaled & 32767
    a = (COEFF[i] + ((COEFF[i+1]-COEFF[i])*f >> 15))/32768
    low = a/(1-(1-a)*cmath.exp(-2j*math.pi*hz/32000))
    step = abs(s.tilt)/100*s.max_step
    boost = interpolation(BOOST, step)/65536
    cut = interpolation(CUT, step)/65536
    gl, gh = (cut, boost) if s.tilt >= 0 else (boost, cut)
    shelf = 1 if s.tilt == 0 else gl*low + gh*(1-low)
    wet = shelf*s.drive_gain*10**(s.trim/20)
    return 20*math.log10(max(1e-12, abs(1-s.mix/100+s.mix/100*wet)))


def pos(s, key):
    return {'tilt': (s.tilt+100)/200, 'pivot': s.pivot/32767,
            'range': s.range_raw/32767, 'drive': s.drive/32767,
            'trim': (s.trim+24)/30, 'mix': s.mix/100}[key]


def value(s, key):
    return {'tilt': (f'{s.tilt:+.0f}' if s.tilt else '0', '%'),
            'pivot': (str(round(s.hz)), 'hz'),
            'range': (f'{s.range_db:.3f}'.rstrip('0').rstrip('.'), 'db'),
            'drive': (f'{s.drive_gain:.2f}', 'x'),
            'trim': (f'{s.trim:+.1f}', 'db'), 'mix': (str(s.mix), '%')}[key]


def text_value(s, key):
    return ''.join(value(s, key))


def freq_x(hz):
    return round(X0+(X1-X0)*math.log(hz/20)/math.log(800))


def db_y(db):
    return round(Y0+(DB_MAX-max(-DB_MAX, min(DB_MAX, db)))*(Y1-Y0)/(2*DB_MAX))


def plot(c, s):
    c.text(14, 78, 'body', GHOST)
    c.text(132, 78, 'linear eq', GHOST)
    c.text(306, 78, 'air', GHOST, right=True)
    for db, label in ((12, '+12'), (0, '0'), (-12, '-12')):
        y = db_y(db)
        c.text(8, y-4, label, GHOST)
        if db == 0:
            c.line((X0, y, X1, y), BRANCH)
        else:
            for x in range(X0, X1+1, 6):
                c.d.point((x, y), fill=BRANCH)
    px = freq_x(s.hz)
    for y in range(Y0, Y1+1, 4):
        c.d.point((px, y), fill=BRANCH)
    points = [(x, db_y(response(s, 20*800**((x-X0)/(X1-X0))))) for x in range(X0, X1+1)]
    c.d.line(points, fill=SPOT, width=2)
    diamond(c, px, db_y(response(s, s.hz)), SPOT, filled=False)
    for hz, label in ((20, '20'), (100, '100'), (1000, '1k'), (16000, '16k')):
        x = freq_x(hz)
        c.line((x, 154, x, 157), GHOST)
        c.text(x if hz == 20 else x-len(label)*3 if hz != 16000 else x-len(label)*6,
               162, label, GHOST)
    # Explicit visible overflow hints on fixed axes, not auto-normalization.
    dbs = [response(s, 20*800**(i/272)) for i in range(273)]
    for clipped, y, dy in ((max(dbs)>DB_MAX, Y0, 3), (min(dbs)<-DB_MAX, Y1, -3)):
        if clipped:
            c.line((X1-7, y+dy, X1-4, y, X1-1, y+dy), SPOT)


def lower(s):
    c = Page()
    c.text(14, 10, 'tilt', SPOT); c.line((14, 23, 37, 23), SPOT)
    c.text(56, 10, 'ch01 / 02', GHOST)
    c.text(258, 10, 'byp', GHOST); c.text(294, 10, 'x', GHOST)
    c.hit('bypass / blocked during p-lock', (246, 0, 284, 25))
    for key, x, end, box in (
            ('tilt', 14, 132, (8, 27, 142, 73)),
            ('pivot', 202, 304, (196, 27, 312, 73))):
        color = SPOT if s.selected == key else INK
        c.text(x, 32, key, color); c.text(x, 48, text_value(s, key), color)
        rail(c, x, end, 65, pos(s, key), color, bipolar=key == 'tilt')
        c.hit(key+' / fixed relative horizontal drag; tap selects', box)
    c.text(154, 44, 'zero', SPOT if s.tilt == 0 else GHOST)
    c.line((158, 58, 174, 58), SPOT if s.tilt == 0 else BRANCH)
    c.hit('zero / set only tilt to 0; edit armed lock instead of base', (148, 27, 188, 73))
    plot(c, s)
    for i, key in enumerate(('range', 'drive', 'trim', 'mix')):
        x = 14+i*76
        color = SPOT if s.selected == key else INK
        c.text(x, 182, key, color if s.selected == key else GHOST)
        c.text(x, 196, text_value(s, key), color)
        rail(c, x, x+62, 214, pos(s, key), color)
        if key == 'trim':
            # Unity is 80% along the proposed -24..+6dB scale, not its midpoint.
            zero = round(x+62*.8)
            c.line((zero, 210, zero, 218), GHOST)
        c.hit(key+' / fixed relative horizontal drag; tap selects', (8+i*76, 177, 80+i*76, 221))
    c.line((14, 223, 306, 223), BRANCH)
    c.footer('b back', 'mod / temp >')
    c.hit('back', (8, 225, 92, 240))
    c.hit('selected mod / temp; hold-l audition retained', (214, 225, 314, 240))
    return c


def upper(s):
    c = Page(400)
    c.text(14, 10, 'osc', GHOST); c.text(44, 10, '/', GHOST)
    c.text(62, 10, 'tilt', SPOT); c.line((62, 23, 85, 23), SPOT)
    c.text(98, 10, '/ vca', GHOST)
    c.text(386, 10, 'ch01 / 02', GHOST, right=True)
    titles = {'tilt': 'tilt / body to air', 'pivot': 'pivot / nominal frequency',
              'range': 'range / shelf limit', 'drive': 'drive / before soft clip',
              'trim': 'trim / wet level after drive', 'mix': 'mix / dry and tilted'}
    key = s.selected
    c.text(14, 41, titles[key], SPOT); c.text(14, 56, 'base', GHOST)
    number, suffix = value(s, key)
    numeric(c, number); c.text(14, 150, suffix, GHOST)
    c.text(14, 166, 'stored base '+text_value(s, key), GHOST)
    rail(c, 14, 224, 184, pos(s, key), INK, bipolar=key == 'tilt')
    limits = {'tilt': ('-100%', '+100%'), 'pivot': ('125hz', '8000hz'),
              'range': ('3db', '11.625db'), 'drive': ('1x', '4x'),
              'trim': ('-24db', '+6db'), 'mix': ('0%', '100%')}
    lo, hi = limits[key]
    c.text(14, 199, lo, GHOST); c.text(224, 199, hi, GHOST, right=True)
    c.line((239, 48, 239, 211), BRANCH)
    v = text_value(s, key)
    rows = ((46, 'example state', GHOST), (60, v, SPOT), (78, 'control '+v, INK),
            (92, 'target '+v, GHOST), (113, 'mod off', INK), (127, 'depth +0%', GHOST),
            (148, 'potential range', GHOST), (162, v, INK), (175, v, INK),
            (191, '| base  <> live', GHOST), (205, 'via base', GHOST))
    for y, text, color in rows:
        c.text(254, y, text, color)
    if key == 'trim':
        c.text(14, 217, 'after drive / before mix / dry level untouched', GHOST)
    else:
        amount = s.tilt/100*s.range_db
        c.text(14, 217, f'shelf targets {-amount:+.2f} / {amount:+.2f}db', GHOST)
    c.text(14, 230, 'concept / small-signal curve, not audio', GHOST)
    return c


def source_hashes():
    names = ('core/engine/units.cpp', 'core/engine/units.h', 'core/engine/engine.cpp',
             'core/engine/patch_io.cpp', 'platform/3ds/ui_module_focus_tilt.cpp',
             'platform/3ds/module_inspector.cpp', 'platform/3ds/ui_param_lens.cpp',
             'platform/3ds/ui_editor.cpp', 'platform/3ds/ui_state.h', 'platform/3ds/main.cpp',
             'platform/3ds/note_display_data.h', 'core/ui/font.cpp')
    return {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in names}


def validate():
    hits = lower(BASE).hits
    extremes = {'tilt': (-100, 0, 100), 'pivot': (0, 16384, 32767),
                'range': (1, 8192, 32767), 'drive': (0, 16384, 32767),
                'trim': (-24, 0, 6), 'mix': (0, 50, 100)}
    for key, values in extremes.items():
        for raw in values:
            s = replace(BASE, selected=key, **{'range_raw' if key == 'range' else key: raw})
            assert lower(s).hits == hits
            upper(s)
            for hz in (20, 100, 1000, 16000):
                assert math.isfinite(response(s, hz))
                assert abs(response(replace(s, mix=0), hz)) < 1e-12
    for hz in (20, 100, 1000, 16000):
        assert abs(response(CASES[2], hz)) < 1e-12
        # At full wet trim is a uniform gain offset; it must not change tilt shape.
        assert abs(response(replace(BASE, trim=-6), hz)-response(BASE, hz)+6) < 1e-10
    assert CASES[0].__dict__ | {'name': '03_zero', 'tilt': 0} == CASES[2].__dict__


def main():
    before = source_hashes()
    validate()
    OUT.mkdir(parents=True, exist_ok=True)
    allowed = set(paper(400).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH, OUTSIDE}
    board = Canvas(824, 1056, OUTSIDE)
    surfaces = Canvas(664, 540, OUTSIDE)
    report = {'status': 'authored visual proposal only, not runtime/hardware/PCM',
              'runtime_implemented': False, 'dsp_changed': False, 'deployed': False,
              'new_proposals': ['wet trim -24..+6db, default 0; after drive and before mix',
                                'separate quick zero affects only tilt / armed tilt lock',
                                'fractional shelf-gain LUT interpolation, no extra smooth knob'],
              'response': 'linearized existing one-pole recurrence with current coefficient LUT; proposed fractional shelf LUT interpolation + wet trim; dry/wet sum includes phase; excludes rounding, saturation and transient state',
              'axes': 'fixed log20..16000hz / -12..+12db; edge chevrons for overflow',
              'compatibility': 'fixtures nonzero RANGE only; actual max11.625db retained; legacy range0 must remain explicit/preserved when implemented, not silently remapped to3db',
              'zero': 'not bypass or full reset; drive/trim/mix/range/pivot retained; flat only when other settings are neutral',
              'inspector': 'reused selected-value layout; explicit unmodulated example values, not engine telemetry',
              'cases': {}}
    for i, s in enumerate(CASES):
        u, b = upper(s), lower(s)
        dual = pair(u.im, b.im)
        for name, im in (('top', u.im), ('bottom', b.im), ('dual', dual)):
            assert set(im.get_flattened_data()) <= allowed
            im.save(OUT/f'{s.name}_{name}_native.png')
            im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST).save(OUT/f'{s.name}_{name}_2x.png')
        title = ('01 / air', '02 / body', '03 / zero: tilt only', '04 / drive + wet trim')[i]
        bx, by = 8+(i%2)*408, (i//2)*528
        board.text(bx+6, by+8, title, BRANCH); board.im.paste(dual, (bx, by+26))
        sx, sy = 8+(i%2)*328, (i//2)*270
        surfaces.text(sx+6, sy+8, title, BRANCH); surfaces.im.paste(b.im, (sx, sy+26))
        report['cases'][s.name] = {'state': asdict(s), 'touch_regions': b.hits,
                                  'top_labels': u.labels, 'bottom_labels': b.labels}
    for name, im in (('comparison', board.im), ('surfaces', surfaces.im)):
        im.save(OUT/f'{name}_native.png')
        im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST).save(OUT/f'{name}_2x.png')
    assert before == source_hashes(), 'protected source changed during preview'
    report['protected_sources'] = before
    report['checks'] = ['18 selected extrema text bounds/nonoverlap', 'fixed disjoint hit regions at extrema',
                        'exact menu/font palette; nearest-only scaling', 'model finite/dry/neutral/trim invariants',
                        'protected runtime/font hashes unchanged']
    (OUT/'concept.json').write_text(json.dumps(report, indent=2)+'\n')
    (OUT/'README.txt').write_text('tilt / visual proposal only\n\n'
        'four dual-screen examples: air, body, quick zero, drive + wet trim.\n'
        'same menu paper, native 6x8 and existing gothic inspector masks.\n'
        'linear eq curve is a small-signal design model, not a spectrum/audio/hardware capture.\n'
        'quick zero only changes tilt; trim follows drive and precedes mix.\n'
        'range0 legacy compatibility is not depicted or silently changed.\n'
        'no new feature, runtime UI, DSP, patch format or console artifact changed.\n'
        'reproduce: python3 tools/render_tilt_clear_study.py\n')
    print(f'{OUT}: 4 duals + comparison; preview layout/model/source checks passed')


if __name__ == '__main__':
    main()
