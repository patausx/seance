#!/usr/bin/env python3
"""Non-shipping delay visual proposal. No DSP, runtime UI or hardware capture.

Uses existing menu paper, native 6x8 and Seance Note bitmap numerals (OFL).
All values are authored, internally consistent example states, not telemetry.
The loop is a signal-path schematic, NOT a waveform or a time-linear plot.
"""
from dataclasses import dataclass, asdict
from pathlib import Path
import hashlib
import json
import math
from PIL import Image, ImageDraw
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair
from render_project_clock_study import hero

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/delay_tape_study'


@dataclass(frozen=True)
class State:
    name: str
    sync: bool = False
    hold: bool = False
    style: str = 'tape'
    base_ms: int = 420
    target_ms: float = 450
    slewed_ms: float = 448
    wow_offset_ms: float = .6
    bpm: float = 120
    feedback: int = 62
    mix: int = 42
    tone: int = 34  # proposal: 0 dark, 100 bright; no cutoff mapping claimed
    wow: int = 20
    sat: int = 28
    source: str = 'a'
    depth_ms: int = 60
    source_output: float = .5
    selected: str = 'time'

    @property
    def read_ms(self):
        return self.slewed_ms + self.wow_offset_ms


CASES = (
    State('01_free'),
    State('02_sync', sync=True, style='clean', base_ms=375, target_ms=375,
          slewed_ms=375, wow_offset_ms=0, wow=0, source='none', depth_ms=0,
          source_output=0),
    State('03_hold', hold=True, base_ms=420, target_ms=420, slewed_ms=420,
          wow_offset_ms=.6, source='none', depth_ms=0, source_output=0,
          selected='feedback'),
)


def bead(c, x, y, r=3, color=INK, hollow=False):
    x, y = round(x), round(y)
    box = (x-r, y-r, x+r, y+r)
    assert 0 <= box[0] < box[2] < c.im.width and 0 <= box[1] < box[3] < 240
    if hollow:
        mask = Image.new('L', c.im.size)
        ImageDraw.Draw(mask).ellipse(box, fill=255)
        c.im.paste(paper(c.im.width), (0, 0), mask)
    c.d.ellipse(box, outline=color, fill=None if hollow else color)


def cubic(c, points, color=INK, width=1):
    a, b, d, e = points
    samples = []
    for i in range(49):
        t = i / 48
        u = 1-t
        samples.append((round(u**3*a[0] + 3*u*u*t*b[0] + 3*u*t*t*d[0] + t**3*e[0]),
                        round(u**3*a[1] + 3*u*u*t*b[1] + 3*u*t*t*d[1] + t**3*e[1])))
    assert all(0 <= x < c.im.width and 0 <= y < 240 for x, y in samples)
    c.d.line(samples, fill=color, width=width)


def dash(c, x0, y0, x1, y1, color=BRANCH):
    n = max(abs(x1-x0), abs(y1-y0))
    for i in range(0, n+1, 4):
        c.d.point((round(x0+(x1-x0)*i/max(1, n)),
                   round(y0+(y1-y0)*i/max(1, n))), fill=color)


def switch(c, text, x, active, hit):
    c.text(x, 36, text, SPOT if active else GHOST)
    if active:
        c.line((x, 47, x+len(text)*6-1, 47), SPOT)
    c.hit(text, hit)


def mini_rail(c, x, y0, y1, amount, selected=False):
    c.line((x, y0, x, y1), BRANCH)
    c.line((x-2, y0, x+2, y0), BRANCH)
    c.line((x-2, y1, x+2, y1), BRANCH)
    y = round(y1-(y1-y0)*amount/100)
    c.line((x-3, y, x+3, y), SPOT if selected else INK)


def log_position(ms, x0, x1):
    return round(x0+(x1-x0)*math.log(max(1, min(4000, ms)))/math.log(4000))


def loop_figure(c, s):
    # A fixed one-head routing diagram. Geometry never doubles as a touch target.
    # The dry line and wet memory are separate; the HOLD gate is only on injection.
    c.line((75, 94, 251, 94), INK)
    c.line((247, 91, 251, 94, 247, 97), INK)
    c.text(76, 82, 'dry', GHOST)
    bead(c, 99, 94, 2)
    c.line((99, 96, 99, 105), GHOST)
    c.line((99, 115, 116, 122), GHOST if s.hold else INK)
    if s.hold:
        c.line((94, 109, 104, 109), SPOT)
        c.line((94, 112, 104, 112), SPOT)
    else:
        c.line((99, 105, 99, 115), INK)
    writer, head = (116, 122), (221, 124)
    cubic(c, (writer, (140, 98), (200, 99), head), SPOT, 2)
    cubic(c, (head, (249, 178), (87, 182), writer), SPOT if s.hold else INK, 2)
    # A quiet companion return expresses feedback amount, not discrete echo count.
    cubic(c, ((217, 139), (206, 165), (125, 166), (120, 140)), BRANCH)
    c.line((160, 164, 155, 162, 159, 159), SPOT if s.hold else GHOST)
    cubic(c, (head, (240, 124), (238, 101), (244, 94)), GHOST)
    bead(c, *writer, 3, SPOT, hollow=True)
    bead(c, *head, 6, SPOT, hollow=True)
    c.line((218, 121, 224, 127), SPOT)
    bead(c, 244, 94, 3, INK, hollow=True)
    c.text(139, 127, 'held loop' if s.hold else 'one head', SPOT if s.hold else GHOST)
    c.text(139, 141, 'evolving' if s.hold else 'return', GHOST)
    # No fake running dots, energy particles or PCM. A future live head-offset
    # marker needs bounded DSP telemetry; this study only shows it on the time rail.


def lower(s):
    c = Page()
    c.text(14, 10, 'dly', SPOT)
    c.line((14, 23, 31, 23), SPOT)
    c.text(49, 10, 'ch01 / 03', GHOST)
    c.text(184, 10, 'hold', SPOT if s.hold else GHOST)
    if s.hold:
        c.line((184, 23, 207, 23), SPOT)
        bead(c, 219, 14, 3, SPOT)
    c.text(258, 10, 'byp', GHOST)
    c.text(294, 10, 'x', GHOST)
    c.hit('hold / toggle; remains an ordinary lockable parameter', (172, 0, 241, 27))
    c.hit('bypass', (245, 0, 282, 27))
    # x is an existing physical-button hint, not a new touch-delete region.
    switch(c, 'free', 14, not s.sync, (8, 28, 52, 52))
    switch(c, 'sync', 62, s.sync, (56, 28, 104, 52))
    if s.sync:
        c.text(120, 36, f'{s.bpm:g}bpm', GHOST)
    switch(c, 'tape', 204, s.style == 'tape', (194, 28, 242, 52))
    switch(c, 'clean', 252, s.style == 'clean', (246, 28, 313, 52))

    # One large stable TIME target. Hollow applied tick cannot steal the gesture.
    color = SPOT if s.selected == 'time' else INK
    c.text(14, 61, 'time', color)
    value = '1/8 d' if s.sync else f'{s.base_ms}ms'
    c.text(60, 61, value, color)
    if s.source != 'none':
        c.text(108, 61, s.source, SPOT)
    c.line((136, 65, 306, 65), BRANCH)
    for x in (136, 178, 221, 263, 306):
        c.line((x, 62, x, 68), BRANCH)
    # Sync ticks are musical divisions (1/32..1/2 at this example tempo),
    # whereas free mode uses the proposed logarithmic 1ms..4s range.
    x = (round(136+170*math.log2(s.base_ms/(60000/s.bpm/8))/4) if s.sync
         else log_position(s.base_ms, 136, 306))
    c.line((x, 60, x, 70), color)
    if abs(s.read_ms-s.base_ms) > .01:
        bead(c, log_position(s.read_ms, 136, 306), 74, 2, SPOT, hollow=True)
    if s.selected == 'time':
        c.line((14, 73, 89, 73), SPOT)
    c.hit('time / fixed horizontal relative drag; sync changes division', (8, 54, 313, 79))

    loop_figure(c, s)
    for key, y, val in (('wow', 101, s.wow), ('sat', 148, s.sat)):
        c.text(14, y, key, GHOST)
        c.text(14, y+14, f'{val}%', INK)
        mini_rail(c, 59, y+1, y+26, val)
        c.hit(key+' / fixed relative drag', (8, y-8, 69, y+34))
    c.text(266, 110, 'tone', GHOST)
    c.text(266, 124, f'{s.tone}%', INK)
    c.text(266, 158, 'dark', GHOST)
    mini_rail(c, 305, 143, 169, s.tone)
    c.hit('tone / fixed relative drag; 0 dark, 100 bright', (259, 94, 318, 177))

    selected = s.selected == 'feedback'
    c.text(14, 186, 'feedback', SPOT if selected else GHOST)
    c.text(14, 201, f'{s.feedback}%', GHOST if s.hold else INK)
    if selected:
        c.line((14, 214, 61, 214), SPOT)
    mini_rail(c, 103, 186, 211, s.feedback, selected)
    c.hit('feedback / fixed relative drag; inactive during hold override', (8, 182, 114, 218))
    c.text(266, 186, 'mix', GHOST)
    c.text(266, 201, f'{s.mix}%', INK)
    mini_rail(c, 305, 185, 211, s.mix)
    c.hit('mix / fixed relative drag; stays live during hold', (256, 182, 318, 218))
    c.text(126, 184, 'input sealed' if s.hold else 'input open', SPOT if s.hold else GHOST)
    c.text(126, 204, 'hold to clear', GHOST)
    c.hit('clear / deliberate long press; no card deletion', (118, 197, 226, 218))
    c.footer('b back', 'mod / temp >')
    c.hit('back', (8, 222, 78, 240))
    c.hit('selected mod / temp', (214, 222, 314, 240))
    return c


def upper(s):
    c = Page(400)
    c.text(14, 10, 'dly', SPOT)
    c.line((14, 23, 31, 23), SPOT)
    c.text(74, 10, 'play > dly > out', GHOST)
    c.text(386, 10, 'ch01 / 03', GHOST, right=True)
    title = 'feedback / stored' if s.hold else 'time / sync' if s.sync else 'time / free'
    c.text(14, 43, title, SPOT)
    c.text(14, 61, 'saved, overridden' if s.hold else 'clock duration' if s.sync else 'base', GHOST)
    hero(c, 14, 129, s.feedback if s.hold else s.base_ms, '%' if s.hold else 'ms')
    caption = ('returns after hold' if s.hold else '1/8 dotted @ 120 bpm' if s.sync else 'stored base 420 ms')
    c.text(14, 148, caption, GHOST)
    c.line((14, 177, 222, 177), BRANCH)
    if s.hold:
        for x in (14, 66, 118, 170, 222):
            c.line((x, 174, x, 180), BRANCH)
        x = round(14+208*s.feedback/100)
        c.line((x, 172, x, 182), GHOST)
        bead(c, round(14+208*.995), 177, 3, SPOT, hollow=True)
        c.text(14, 190, '0%', GHOST)
        c.text(222, 190, '100%', GHOST, right=True)
        c.text(14, 209, 'stored |    hold o', GHOST)
    elif s.sync:
        for n in range(5):
            x = 14+52*n
            c.line((x, 173, x, 181), BRANCH)
        x = round(14+208*math.log2(s.base_ms/62.5)/4)
        c.line((x, 171, x, 183), SPOT)
        c.text(14, 190, '1/32', GHOST)
        c.text(104, 190, '1/8', GHOST)
        c.text(222, 190, '1/2', GHOST, right=True)
        c.text(14, 209, 'dotted / triplet available', GHOST)
    else:
        for value in (1, 10, 100, 1000, 4000):
            x = log_position(value, 14, 222)
            c.line((x, 174, x, 180), BRANCH)
        lo = log_position(s.base_ms-s.depth_ms, 14, 222)
        hi = log_position(s.base_ms+s.depth_ms, 14, 222)
        c.line((lo, 170, hi, 170), SPOT)
        x = log_position(s.base_ms, 14, 222)
        c.line((x, 172, x, 182), INK)
        bead(c, log_position(s.read_ms, 14, 222), 185, 2, SPOT, hollow=True)
        c.text(14, 194, '1ms', GHOST)
        c.text(222, 194, '4s', GHOST, right=True)
        c.text(14, 210, 'log time / relative drag', GHOST)

    c.text(252, 43, 'preview state', GHOST)
    if s.hold:
        rows = ((66, 'hold active', SPOT), (82, 'input closed', INK),
                (98, 'loop fb ~99.5%', SPOT), (124, f'tone {s.tone}%', INK),
                (140, f'sat {s.sat}%', INK), (156, 'mod none', GHOST),
                (183, 'via hold override', SPOT), (199, 'evolving, not frozen', GHOST))
    elif s.sync:
        rows = ((66, f'clock {s.bpm:.1f}bpm', INK), (82, f'target {s.target_ms:.1f}ms', INK),
                (98, f'read {s.read_ms:.1f}ms', SPOT), (124, 'sync / 1/8 dotted', INK),
                (140, 'mod none', GHOST), (156, 'range via clock', GHOST),
                (183, 'via applied tempo', SPOT), (199, 'clean / settled', GHOST))
    else:
        rows = ((66, f'target {s.target_ms:.1f}ms', INK), (82, f'read {s.read_ms:.1f}ms', SPOT),
                (98, f'wow {s.wow_offset_ms:+.1f}ms', GHOST),
                (124, f'mod {s.source} +{s.depth_ms}ms', INK), (140, 'source +50%', GHOST),
                (156, f'range {s.base_ms-s.depth_ms}..{s.base_ms+s.depth_ms}ms', GHOST),
                (183, 'via base + a', SPOT), (199, 'tape / input open', GHOST))
    for y, text, color in rows:
        c.text(252, y, text, color)
    c.text(14, 230, 'concept / illustrative state, not audio capture', GHOST)
    return c


def source_hashes():
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for folder in ('core', 'platform', 'assets/fonts')
            for p in sorted((ROOT/folder).rglob('*')) if p.is_file()}


def main():
    before = source_hashes()
    OUT.mkdir(parents=True, exist_ok=True)
    report = {'status': 'visual proposal only; no new delay features implemented',
              'font_provenance': 'assets/fonts/README.md', 'cases': {},
              'controls': 'fixed relative drags; no hit region on animated diagram',
              'new_proposals': ['free/sync, dotted/triplet', 'up to 4s, memory budget pending',
                                'feedback tone', 'tape/clean transitions', 'smooth hold and protected clear'],
              'diagram': 'one-head feedback path, independent dry and gated input; not a waveform or a linear time plot',
              'hold': 'evolving feedback loop, not lossless freeze; nominal feedback coefficient, not net gain',
              'deferred': ['exact freeze', 'ducking', 'second head', 'reverse', 'stereo ping pong'],
              'runtime_implemented': False, 'hardware_capture': False}
    comparison = Canvas(1232, 532, OUTSIDE)
    palette = set(paper(400).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH, OUTSIDE}
    for i, s in enumerate(CASES):
        if s.sync:
            assert s.base_ms == 60000/s.bpm*.75  # dotted eighth = 3/4 beat
        else:
            assert s.target_ms == s.base_ms+s.depth_ms*s.source_output
        u, b = upper(s), lower(s)
        for name, im in (('top', u.im), ('bottom', b.im), ('dual', pair(u.im, b.im))):
            assert set(im.get_flattened_data()) <= palette
            im.save(OUT/f'{s.name}_{name}_native.png')
            im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST).save(OUT/f'{s.name}_{name}_2x.png')
        x = 8+i*408
        comparison.text(x+6, 9, ('01 / free + tape', '02 / sync + clean', '03 / hold + feedback')[i], BRANCH)
        comparison.im.paste(pair(u.im, b.im), (x, 28))
        report['cases'][s.name] = {'fixture': asdict(s), 'read_ms': s.read_ms,
                                  'labels': u.labels+b.labels, 'touch_regions': b.hits}
    comparison.im.save(OUT/'comparison_native.png')
    comparison.im.resize((2464, 1064), Image.Resampling.NEAREST).save(OUT/'comparison_2x.png')
    assert before == source_hashes(), 'runtime/font source changed during concept rendering'
    report['checks'] = {'native_labels_in_bounds_and_nonoverlapping': True,
                        'fixed_touch_targets_nonoverlapping': True,
                        'exact_menu_palette': True, 'fixture_arithmetic': True,
                        'runtime_and_fonts_unchanged': True}
    (OUT/'concept.json').write_text(json.dumps(report, indent=2)+'\n')
    (OUT/'README.txt').write_text(
        'delay / tape loop visual proposal\n\n'
        'not implemented UI, DSP telemetry, or a hardware capture.\n'
        '01 free: stored 420ms + sigil a (+60ms * 50%) -> target450ms;\n'
        'slewed448ms + illustrative wow0.6ms = read448.6ms.\n'
        '02 sync: 120bpm * dotted eighth -> 375ms; clean transition settled.\n'
        '03 hold: input closed, nominal feedback overridden to ~99.5%;\n'
        'stored62% retained, tone/sat continue shaping the loop. not exact freeze.\n'
        'tone34% is a proposed normalized bright/dark control, not a claimed cutoff.\n'
        'free TIME rail uses a proposed log1ms..4s editor. capacity needs a memory check.\n'
        'drawn loop is a fixed routing schematic, never a measured waveform.\n'
        'no new animation/PCM readout is faked. no actual C++ hit handling is changed.\n'
        'native400x240 + centered320x240; nearest-neighbour2x, exact existing font masks.\n'
        'font license/provenance: assets/fonts/README.md.\n'
        'reproduce: python3 tools/render_delay_tape_study.py\n')
    print('delay concept: 3 duals; layout, palette, arithmetic and source guards passed')
    print(OUT/'comparison_2x.png')


if __name__ == '__main__':
    main()
