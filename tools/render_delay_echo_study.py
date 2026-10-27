#!/usr/bin/env python3
"""Preview only: a delay should look like delayed repeats, not an abstract loop.

Exact project fonts/paper. Explicit linear echo reference: y(t) = (1-m)x(t)
+ m sum(f**(n-1) x(t-nD)). This is NOT the existing nonlinear tape DSP, PCM
telemetry, or a hardware screenshot. Existing delay/tape proposal is untouched.
"""
from dataclasses import dataclass, asdict
from pathlib import Path
import json
import math
from PIL import Image
from render_menu_style_study import Page, GHOST, BRANCH, paper
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair
from render_project_clock_study import hero
from render_delay_tape_study import source_hashes

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/delay_echo_study'
X0, X1, BASE_Y, SPAN_MS = 24, 304, 128, 1600


@dataclass(frozen=True)
class Echo:
    name: str
    ms: int
    feedback: int = 58
    mix: int = 50
    selected: str = 'time'


CASES = (Echo('01_short', 180), Echo('02_long', 420),
         Echo('03_feedback', 420, 84, selected='feedback'))


def px(ms):
    return X0 + (X1-X0)*ms/SPAN_MS


def pulse(t):
    # One fixed 72ms transient. Every echo uses exactly the same source/time scale.
    if not 0 <= t <= 72:
        return 0.0
    u = t/72
    return math.sin(6*math.pi*u)*math.exp(-3*u)*min(1, u*24)


NORM = max(abs(pulse(i/16)) for i in range(72*16+1))


def echoes(s):
    return [(0, (100-s.mix)/100)] + [(n*s.ms, s.mix/100*(s.feedback/100)**(n-1))
            for n in range(1, SPAN_MS//s.ms+1)]


def response(c, s):
    c.text(14, 77, 'echo model', GHOST)
    c.text(306, 77, 'same scale / 1.6s', GHOST, right=True)
    c.line((X0, BASE_Y, X1, BASE_Y), BRANCH)
    for i, (start, gain) in enumerate(echoes(s)):
        if gain < .008:
            continue
        # Same packet, same linear scale; only gain changes. Per-column min/max
        # preserves a narrow transient instead of aliasing it to an arbitrary dot.
        color = INK if i == 0 else SPOT if gain >= .16 else GHOST if gain >= .045 else BRANCH
        left, right = round(px(start)), min(X1, math.ceil(px(start+72)))
        for x in range(left, right+1):
            ts = [((x-X0+(j/8-.5))/(X1-X0)*SPAN_MS-start) for j in range(9)]
            vals = [pulse(t)/NORM*gain for t in ts]
            a, b = round(BASE_Y-66*max(vals)), round(BASE_Y-66*min(vals))
            c.line((x, a, x, b), color)
        c.line((left, 150, left, 153), GHOST if i == 0 else BRANCH)
    # Distance between starts encodes delay itself, not a moving decorative bead.
    end = round(px(s.ms))
    c.line((X0, 159, end, 159), SPOT)
    c.line((X0, 156, X0, 162), SPOT)
    c.line((end, 156, end, 162), SPOT)
    if end-X0 >= 42:
        c.text(round((X0+end)/2)-15, 165, f'{s.ms}ms', SPOT)
    else:
        c.text(X0, 165, f'{s.ms}ms', SPOT)
    c.text(304, 165, '1.6s', GHOST, right=True)
    # Source vs echoes identified with tiny open ticks above the actual onsets.
    c.line((X0, 91, X0, 96), INK)
    c.line((end, 91, end, 96), SPOT)


def selected_value(c, x, key, value, s, box, rail_end):
    color = SPOT if s.selected == key else GHOST
    c.text(x, 31, key, color)
    c.text(x, 46, value, SPOT if s.selected == key else INK)
    c.line((x, 63, rail_end, 63), BRANCH)
    v = math.log(s.ms)/math.log(4000) if key == 'time' else s.feedback/100
    at = round(x+(rail_end-x)*v)
    c.line((at, 60, at, 66), color)
    if s.selected == key:
        c.line((x, 57, x+len(value)*6-1, 57), SPOT)
    c.hit(key+' / fixed horizontal relative drag; tap selects only', box)


def lower(s):
    c = Page()
    c.text(14, 10, 'dly', SPOT)
    c.line((14, 22, 31, 22), SPOT)
    c.text(50, 10, 'ch01 / 03', GHOST)
    c.text(193, 10, 'hold', GHOST)
    c.text(258, 10, 'byp', GHOST)
    c.text(294, 10, 'x', GHOST)
    c.hit('hold', (184, 0, 240, 25))
    c.hit('bypass', (246, 0, 282, 25))
    selected_value(c, 14, 'time', f'{s.ms}ms', s, (8, 26, 152, 70), 143)
    selected_value(c, 178, 'feedback', f'{s.feedback}%', s, (168, 26, 313, 70), 306)
    response(c, s)
    # The plot stays view-only. Fixed controls below never chase echo positions.
    for i, (key, value) in enumerate((('mix', f'{s.mix}%'), ('tone', 'bright'), ('wow', '0%'), ('sat', '0%'))):
        x = 14+i*76
        c.text(x, 184, key, GHOST)
        c.text(x, 197, value, INK)
        c.hit(key+' / fixed relative value drag', (8+i*76, 180, 78+i*76, 208))
    for text, x, active, box in (
            ('free', 14, True, (8, 209, 52, 224)),
            ('sync', 62, False, (56, 209, 104, 224)),
            ('tape', 154, False, (148, 209, 194, 224)),
            ('clean', 206, True, (200, 209, 247, 224)),
            ('clear', 272, False, (258, 209, 313, 224))):
        c.text(x, 213, text, SPOT if active else GHOST)
        if active:
            c.line((x, 223, x+len(text)*6-1, 223), SPOT)
        c.hit(text+' / clear requires deliberate hold', box)
    c.footer('b back', 'mod / temp >')
    c.hit('back', (8, 225, 90, 240))
    c.hit('selected mod / temp', (210, 225, 314, 240))
    return c


def upper(s):
    c = Page(400)
    c.text(14, 10, 'dly', SPOT)
    c.line((14, 23, 31, 23), SPOT)
    c.text(74, 10, 'play > dly > out', GHOST)
    c.text(386, 10, 'ch01 / 03', GHOST, right=True)
    is_time = s.selected == 'time'
    c.text(14, 43, 'time / free' if is_time else 'feedback / return gain', SPOT)
    c.text(14, 61, 'base', GHOST)
    value = s.ms if is_time else s.feedback
    suffix = 'ms' if is_time else '%'
    hero(c, 14, 129, value, suffix)
    c.text(14, 148, f'stored {value}{suffix}', GHOST)
    c.line((14, 177, 222, 177), BRANCH)
    for n in range(5):
        x = 14+52*n
        c.line((x, 174, x, 180), BRANCH)
    v = math.log(s.ms)/math.log(4000) if is_time else s.feedback/100
    x = round(14+208*v)
    c.line((x, 172, x, 182), INK)
    c.text(14, 191, '1ms' if is_time else '0%', GHOST)
    c.text(222, 191, '4s' if is_time else '100%', GHOST, right=True)
    c.text(14, 211, 'space between repeats' if is_time else 'height of each next repeat', GHOST)
    rows = ((43, 'example state', GHOST),
            (66, f'target {value}{suffix}', INK),
            (82, f'applied {value}{suffix}', SPOT),
            (108, 'mod none', INK), (124, 'range --', GHOST),
            (150, 'via base', SPOT), (166, 'clean / settled', GHOST),
            (193, 'dark = dry', INK), (209, 'violet = echoes', SPOT))
    for y, text, color in rows:
        c.text(252, y, text, color)
    c.text(14, 230, 'concept / linear echo model, not captured audio', GHOST)
    return c


def main():
    before = source_hashes()
    OUT.mkdir(parents=True, exist_ok=True)
    board = Canvas(1232, 532, OUTSIDE)
    surfaces = Canvas(992, 276, OUTSIDE)
    allowed = set(paper(400).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH, OUTSIDE}
    data = {'status': 'concept only; no runtime, DSP or hardware changes',
            'model': 'linear mono echo response to one synthetic 72ms transient; not current nonlinear delay',
            'time_axis': 'fixed 0..1600ms; 280px; 0.175px/ms for every example',
            'gain': 'dry=1-mix, wet_n=mix*feedback**(n-1); not automatic amplitude normalization',
            'tone_wow_sat': 'bright / zero / zero in this idealized linear reference',
            'scope': 'new lower visual proposal; upper preserves selected-value workflow',
            'cases': {}}
    for i, s in enumerate(CASES):
        u, b = upper(s), lower(s)
        for surface, im in (('top', u.im), ('bottom', b.im), ('dual', pair(u.im, b.im))):
            assert set(im.get_flattened_data()) <= allowed
            im.save(OUT/f'{s.name}_{surface}_native.png')
            im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST).save(OUT/f'{s.name}_{surface}_2x.png')
        title = ('01 / short repeats', '02 / more space', '03 / longer tail')[i]
        board.text(14+i*408, 9, title, BRANCH)
        board.im.paste(pair(u.im, b.im), (8+i*408, 28))
        surfaces.text(14+i*328, 9, title, BRANCH)
        surfaces.im.paste(b.im, (8+i*328, 28))
        data['cases'][s.name] = {'settings': asdict(s), 'onsets_and_gains': echoes(s),
                               'touch_regions': b.hits, 'labels': u.labels+b.labels}
        assert all(abs((px(n*s.ms)-px((n-1)*s.ms))-s.ms*.175) < 1e-9
                   for n in range(1, SPAN_MS//s.ms+1))
    for name, im in (('comparison', board.im), ('surfaces', surfaces.im)):
        im.save(OUT/f'{name}_native.png')
        im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST).save(OUT/f'{name}_2x.png')
    assert before == source_hashes(), 'runtime/fonts changed during preview'
    data['checks'] = ['labels bounded and nonoverlapping', 'touch regions nonoverlapping',
                      'exact menu palette', 'echo spacing arithmetic', 'runtime/font hashes unchanged']
    (OUT/'concept.json').write_text(json.dumps(data, indent=2)+'\n')
    print(OUT/'surfaces_2x.png')
    print(OUT/'02_long_dual_2x.png')


if __name__ == '__main__':
    main()
