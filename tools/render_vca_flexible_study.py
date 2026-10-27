#!/usr/bin/env python3
"""Non-shipping VCA AHD visual study. Not device UI, DSP or audio telemetry.

Existing menu paper + native 6x8 + Seance Note bitmap digits (assets/fonts/README.md).
All motion is an explicitly authored mathematical envelope proposal. No runtime
sources, serializers, builds or deployment files are written by this tool.
"""
from dataclasses import dataclass, asdict, replace
from pathlib import Path
import hashlib
import json
import math
from PIL import Image, ImageDraw
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair
from render_project_clock_study import hero

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/vca_flexible_study'
X0, X1 = 40, 259
Y0, Y1 = 76, 150


@dataclass(frozen=True)
class Envelope:
    attack: int = 240
    hold: int = 180
    decay: int = 780
    gain: int = 80
    floor: int = 20  # relative to gain; minimum multiplier is .80 * .20 = .16
    a_curve: int = 25
    d_curve: int = -55
    duck: bool = False

    @property
    def total(self):
        return self.attack + self.hold + self.decay

    def env(self, t):
        if t < 0 or t >= self.total:
            return 0.0
        if t < self.attack:
            return (t / self.attack) ** (2 ** (self.a_curve / 50))
        if t < self.attack + self.hold:
            return 1.0
        p = (t - self.attack - self.hold) / self.decay
        return (1 - p) ** (2 ** (-self.d_curve / 50))

    def opening(self, t):
        env = self.env(t)
        return self.floor / 100 + (1 - self.floor / 100) * (1 - env if self.duck else env)

    def stage(self, t):
        if t < 0 or t >= self.total:
            return 'idle'
        return 'attack' if t < self.attack else 'hold' if t < self.attack + self.hold else 'decay'

    def point(self, t, box=(X0, Y0, X1, Y1)):
        x0, y0, x1, y1 = box
        return round(x0 + (x1 - x0) * t / self.total), round(y1 - self.opening(t) * (y1 - y0))


BASE = Envelope()


def ring(c, x, y, r=4, color=INK, filled=False):
    box = (x-r, y-r, x+r, y+r)
    assert 0 <= box[0] <= box[2] < c.im.width and 0 <= box[1] <= box[3] < 240
    if not filled:
        mask = Image.new('L', c.im.size)
        ImageDraw.Draw(mask).ellipse(box, fill=255)
        c.im.paste(paper(c.im.width), (0, 0), mask)
    c.d.ellipse(box, fill=color if filled else None, outline=color)


def diamond(c, x, y, color=GHOST):
    # Curve handles have a different silhouette from timing handles.
    pts = [(x, y-3), (x+3, y), (x, y+3), (x-3, y), (x, y-3)]
    c.d.line(pts, fill=color)


def dotted(c, x0, y0, x1, y1, color=BRANCH):
    n = max(abs(x1-x0), abs(y1-y0))
    for i in range(0, n+1, 4):
        c.d.point((round(x0+(x1-x0)*i/max(1, n)), round(y0+(y1-y0)*i/max(1, n))), fill=color)


def envelope_ink(c, e, selected, box=(X0, Y0, X1, Y1), thick=True):
    times = (0, e.attack, e.attack+e.hold, e.total)
    segments = []
    for name, a, b in zip(('attack', 'hold', 'decay'), times, times[1:]):
        pts = [e.point(a+(b-a)*i/80, box) for i in range(81)]
        color = SPOT if selected == name or selected == ('a_curve' if name == 'attack' else 'd_curve' if name == 'decay' else name) else INK
        c.d.line(pts, fill=color, width=2 if thick else 1)
        segments.append(pts)
    return segments


def numeric(c, left, y, name, value, selected, key, box):
    color = SPOT if key == selected else GHOST
    c.text(left, y, name, color)
    c.text(left, y+13, value, SPOT if key == selected else INK)
    if key == selected:
        c.line((left, y+25, left+len(value)*6-1, y+25), SPOT)
    c.hit(key + ' / stable relative value drag', box)


def bottom(e=BASE, selected='decay', t=650):
    c = Page()
    c.text(14, 10, 'vca', SPOT)
    c.line((14, 23, 31, 23), SPOT)
    c.text(49, 10, 'ch01 / 01', GHOST)
    c.text(258, 10, 'byp', GHOST)
    c.text(294, 10, 'x', GHOST)
    c.hit('bypass', (244, 0, 283, 26))
    c.hit('delete / confirmation', (286, 0, 320, 26))

    for name, x, active, box in (
            ('open', 14, not e.duck, (8, 28, 58, 52)),
            ('duck', 72, e.duck, (62, 28, 112, 52))):
        c.text(x, 35, name, SPOT if active else GHOST)
        if active:
            c.line((x, 46, x+23, 46), SPOT)
        c.hit('polarity ' + name, box)
    c.text(306, 35, 'one shot', GHOST, right=True)  # status, not a fake loop button
    c.text(14, 57, 'opening', GHOST)
    c.text(153, 57, f'{e.total / 1000:g}s', GHOST)
    c.text(270, 57, 'gain', SPOT if selected == 'gain' else GHOST)
    c.text(270, 70, f'{e.gain}%', INK)

    floor_y = round(Y1-(Y1-Y0)*e.floor/100)
    dotted(c, X0, floor_y, X1, floor_y, SPOT if selected == 'floor' else BRANCH)
    dotted(c, X0, Y0, X1, Y0)
    c.line((X0, Y1, X1, Y1), BRANCH)
    # Not a waveform fill: sparse vertical strokes indicate the available opening
    # above floor. Shared opening(t) governs the curve, dot and numeric preview.
    for x in range(X0+5, X1, 7):
        tt = (x-X0)/(X1-X0)*e.total
        _, yy = e.point(tt)
        if yy+4 < floor_y-2:
            c.line((x, yy+4, x, floor_y-2), BRANCH)
    envelope_ink(c, e, selected)

    # Three time handles; timing values below are stable secondary targets even
    # if a stage becomes too short to give its handle an independent hit region.
    for key, tt in (('attack', e.attack), ('hold', e.attack+e.hold), ('decay', e.total)):
        x, y = e.point(tt)
        ring(c, x, y, 6 if selected == key else 4, SPOT if selected == key else INK)
        c.hit(key + ' / horizontal relative node drag', (x-11, y-11, x+12, y+12))
    for key, tt in (('a_curve', e.attack*.48), ('d_curve', e.attack+e.hold+e.decay*.48)):
        x, y = e.point(tt)
        diamond(c, x, y, SPOT if selected == key else INK)
        # This fixture only: runtime would need distance-based hit arbitration at
        # collapsed stages, not blindly reuse these static preview boxes.
        c.hit(key + ' / vertical relative diamond drag', (x-8, y-8, x+9, y+9))

    c.line((22, floor_y, X0-4, floor_y), SPOT if selected == 'floor' else GHOST)
    ring(c, 22, floor_y, 4, SPOT if selected == 'floor' else GHOST)
    c.hit('floor / vertical relative node drag', (8, floor_y-11, 34, floor_y+12))
    # Gain is separate from normalized opening; floor 20% of gain 80% = 16%.
    c.line((295, 92, 295, 148), BRANCH)
    for yy in (92, 120, 148):
        c.line((292, yy, 298, yy), BRANCH)
    ring(c, 295, round(148-56*e.gain/100), 5, SPOT if selected == 'gain' else INK, filled=True)
    c.hit('gain / fixed vertical relative drag', (277, 86, 314, 159))
    c.hit('gain / stable value drag', (273, 52, 314, 84))

    if e.stage(t) != 'idle':
        x, y = e.point(t)
        ring(c, x, y, 5, SPOT)
        ring(c, x, y, 1, SPOT, filled=True)
    for i, (key, title, value) in enumerate((
            ('attack', 'attack', f'{e.attack}ms'), ('hold', 'hold', f'{e.hold}ms'),
            ('decay', 'decay', f'{e.decay}ms'), ('floor', 'floor', f'{e.floor}%'))):
        left = 14 + i*74
        numeric(c, left, 166, title, value, selected, key, (8+i*74, 161, 78+i*74, 195))
    for key, x, value, title, box in (
            ('a_curve', 14, e.a_curve, 'a curve', (8, 198, 156, 222)),
            ('d_curve', 174, e.d_curve, 'd curve', (164, 198, 312, 222))):
        c.text(x, 204, f'{title} {value:+d}', SPOT if key == selected else INK)
        c.hit(key + ' / stable relative value drag', box)
    c.line((14, 223, 306, 223), BRANCH)
    c.footer('b back', 'mod / temp >')
    c.text(118, 228, 'a trig', GHOST)
    c.hit('selected mod / temp', (214, 225, 314, 240))
    return c


def top(e=BASE, selected='decay', t=650):
    c = Page(400)
    c.text(14, 10, 'vca', SPOT)
    c.line((14, 23, 31, 23), SPOT)
    c.text(386, 10, 'ch01 / 01', GHOST, right=True)
    info = {
        'attack': ('attack / rise time', e.attack, 'ms', '1ms', '8s', 'rise from floor to gain'),
        'hold': ('hold / plateau time', e.hold, 'ms', '0ms', '8s', 'hold the envelope at its peak'),
        'decay': ('decay / tail time', e.decay, 'ms', '1ms', '8s', 'return to the resting level'),
        'floor': ('floor / minimum opening', e.floor, '%', '0%', '100%', f'{e.floor}% of gain = {e.floor*e.gain/100:g}% input'),
        'gain': ('gain / output ceiling', e.gain, '%', '0%', '100%', 'linear gain, not output loudness'),
        'a_curve': ('attack / curve bend', abs(e.a_curve), 'bend', '-100', '+100', '0 straight / bend the rise'),
        'd_curve': ('decay / curve bend', abs(e.d_curve), 'bend', '-100', '+100', '0 straight / bend the tail'),
    }
    title, value, unit, lo, hi, description = info[selected]
    c.text(14, 42, title, SPOT)
    c.text(14, 58, 'base', GHOST)
    if selected.endswith('curve'):
        c.text(14, 100, '-' if getattr(e, selected) < 0 else '+', INK, scale=2)
        hero(c, 34, 130, value, unit)
    else:
        hero(c, 14, 130, value, unit)
    c.text(14, 149, f'stored {getattr(e, selected):+d}' if selected.endswith('curve') else f'stored base {value}{unit}', GHOST)
    c.line((14, 171, 224, 171), BRANCH)
    for x in range(14, 225, 30):
        c.line((x, 168, x, 174), BRANCH)
    # Proposed 1..8000 ms logarithmic editor; not the old quadratic DSP mapping.
    position = ((getattr(e, selected)+100)/200 if selected.endswith('curve') else value/100 if unit == '%' else math.log(max(1,value))/math.log(8000))
    x = round(14+210*position)
    c.line((x, 165, x, 177), INK)
    c.text(14, 185, lo, GHOST)
    c.text(224, 185, hi, GHOST, right=True)
    c.text(14, 207, description, GHOST)
    c.line((240, 46, 240, 210), BRANCH)
    c.text(254, 46, 'preview / ' + ('duck' if e.duck else 'open'), GHOST)
    c.text(254, 62, 'stage ' + e.stage(t), SPOT)
    c.text(254, 88, f'opening {round(e.opening(t)*100)}%', INK)
    c.text(254, 103, f'vca gain {round(e.opening(t)*e.gain)}%', SPOT)
    c.text(254, 129, 'mod none', INK)
    c.text(254, 144, 'via base', GHOST)
    mini = (254, 173, 384, 202)
    dotted(c, 254, 202, 384, 202)
    envelope_ink(c, e, selected, mini, thick=False)
    if e.stage(t) != 'idle':
        x, y = e.point(t, mini)
        ring(c, x, y, 2, SPOT, filled=True)
    c.text(14, 230, 'concept / envelope model, not an audio waveform', GHOST)
    return c


def validate_model():
    for duck in (False, True):
        e = replace(BASE, duck=duck)
        assert e.env(0) == 0 and e.env(e.attack) == 1 and e.env(e.total) == 0
        assert e.opening(-1) == e.opening(e.total)
        for t in range(e.total+1):
            normal = BASE.opening(t)
            opposite = replace(BASE, duck=True).opening(t)
            assert abs(normal+opposite-(1+BASE.floor/100)) < 1e-12
            assert BASE.floor/100-1e-12 <= e.opening(t) <= 1+1e-12
    for p in (-100, 0, 100):
        e = replace(BASE, a_curve=p, d_curve=p)
        assert e.env(0) == 0 and e.env(e.attack) == 1 and e.env(e.total) == 0
    assert abs(BASE.gain*BASE.floor/10000-.16) < 1e-12


def source_hashes():
    # Lightweight guard: no build, device access or broad test suite for a drawing.
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for root in ('core', 'platform') for p in sorted((ROOT/root).rglob('*')) if p.is_file()}


def main():
    before = source_hashes()
    validate_model()
    OUT.mkdir(parents=True, exist_ok=True)
    cases = [
        ('01_open', BASE, 'decay', 650),
        ('02_hold', BASE, 'hold', 330),
        ('03_duck', replace(BASE, duck=True), 'floor', 650),
        ('04_curve', BASE, 'd_curve', 650),
    ]
    report = {'status': 'visual proposal only; no implemented new VCA features or audio capture',
              'fixture': asdict(BASE), 'font_provenance': 'assets/fonts/README.md',
              'mapping': 'x is linear time; y is opening 0..1; output multiplier = gain * opening',
              'duck': 'gain * (floor + (1-floor)*(1-envelope)); same stages, inverted excursion',
              'gesture': 'relative drags, tap only selects; time circles horizontal, curve diamonds vertical; gain and floor vertical; stable numeric fallback always available',
              'timing_range': 'proposed free times up to 8s; hold can be zero; not current DSP',
              'scope': ['AHD', 'independent attack/decay bends', 'floor relative to gain', 'open/duck'],
              'deferred': ['loop', 'tempo sync', 'retrigger selector', 'ADSR/gate-off', 'new DSP and persistence'],
              'cases': {}}
    for name, e, selected, t in cases:
        u, b = top(e, selected, t), bottom(e, selected, t)
        for surface, image in (('top', u.im), ('bottom', b.im), ('dual', pair(u.im, b.im))):
            image.save(OUT/f'{name}_{surface}_native.png')
            image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST).save(OUT/f'{name}_{surface}_2x.png')
        report['cases'][name] = {'selected': selected, 'time_ms': t, 'labels': u.labels+b.labels, 'touch_regions': b.hits}
    comparison = Canvas(664, 512, OUTSIDE)
    for i, (name, e, selected, t) in enumerate(cases):
        x, y = 8+(i%2)*328, 8+(i//2)*252
        comparison.im.paste(bottom(e, selected, t).im, (x, y))
    comparison.im.save(OUT/'surfaces_native.png')
    comparison.im.resize((1328, 1024), Image.Resampling.NEAREST).save(OUT/'surfaces_2x.png')

    colors = sorted(set(paper(400).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH, OUTSIDE})
    assert len(colors) <= 256
    palette = Image.new('P', (1, 1))
    entries = [n for rgb in colors for n in rgb]
    palette.putpalette(entries+[0]*(768-len(entries)))
    frames = []
    # Two separately triggered one-shots, each with 800ms of idle afterwards.
    # GIF repeats as a presentation, not a proposed enabled loop mode.
    for duck in (False, True):
        e = replace(BASE, duck=duck)
        for t in range(0, 2000, 50):
            u, b = top(e, 'decay', t), bottom(e, 'decay', t)
            frame = pair(u.im, b.im).resize((800, 992), Image.Resampling.NEAREST)
            assert set(frame.get_flattened_data()) <= set(colors)
            frames.append(frame.quantize(palette=palette, dither=Image.Dither.NONE))
    frames[0].save(OUT/'vca_open_duck_motion.gif', save_all=True, append_images=frames[1:],
                   duration=50, loop=0, disposal=2, optimize=False)
    assert before == source_hashes(), 'runtime sources changed during preview'
    report['checks'] = {'model_endpoints_bounds_inversion': True, 'native_label_bounds_no_overlap': True,
                        'fixture_touch_regions_no_overlap': True, 'gif_exact_palette': True,
                        'runtime_sources_unchanged': True, 'hardware_checked': False}
    (OUT/'concept.json').write_text(json.dumps(report, indent=2)+'\n')
    (OUT/'README.txt').write_text(
        'vca flexible / visual proposal only\n\n'
        '01 open: decay selected. 02 hold: plateau selected.\n'
        '03 duck: inverted excursion, relative floor selected. 04 curve: decay bend selected.\n'
        'native 400x240 + centered 320x240; 2x nearest, exact project bitmap fonts/menu paper.\n'
        'no new VCA feature has been implemented. no audio, hardware or runtime UI capture.\n'
        'motion: two illustrative one-shots, open then duck, each 1.2s + 0.8s idle.\n'
        'floor is relative to gain: 20% of 80% = 16% minimum input multiplier.\n'
        'curve is normalized opening, not waveform or measured loudness.\n'
        'touch maps only validate these fixtures, not collapsed/extreme geometry.\n'
        'loop/sync/retrigger selection intentionally deferred.\n'
        'reproduce: python3 tools/render_vca_flexible_study.py\n')
    print('vca concept: 4 duals, 80 animation frames; model/layout/palette/source guards passed')
    print(OUT/'01_open_dual_2x.png')
    print(OUT/'vca_open_duck_motion.gif')


if __name__ == '__main__':
    main()
