#!/usr/bin/env python3
"""Authored MIC concept, NOT runtime capture, hardware telemetry or DSP proof.

Reuses existing native-font/paper/inspector preview helpers. The incoming wave
is synthetic demo PCM, NOT microphone audio; meter levels are illustrative.
Proposed low-cut stays OFF in every frame.
"""
from dataclasses import dataclass
from pathlib import Path
import math
from PIL import Image
from render_menu_style_study import Page, GHOST, BRANCH
from render_module_geometry_study import INK, SPOT, OUTSIDE, pair
from render_fold_wave_study import numeric, rail, diamond

OUT = Path(__file__).resolve().parents[1] / 'ui_mockups/mic_visual_study'


@dataclass(frozen=True)
class State:
    name: str
    gain: int = 16384
    opened: bool = True
    input_db: float = -9.0

    @property
    def gain_db(self):
        return 20 * math.log10(self.gain / 8192)

    @property
    def output_db(self):
        return min(0.0, self.input_db + self.gain_db) if self.opened else None

    @property
    def clip(self):
        return self.opened and self.input_db + self.gain_db > 0


STATES = (State('01_open'), State('02_clip', gain=32767),
          State('03_muted', opened=False))


def db_text(value):
    return 'silent' if value is None else f'{value:.1f} dbfs'


# A fixed authored 32ms mono fixture. Calibrate once to the stated -9 dBFS;
# the drawing itself has a fixed +/-1FS scale and never normalizes per frame.
_DEMO = tuple((math.sin(2*math.pi*250*i/32000) +
               .3*math.sin(2*math.pi*625*i/32000)) *
              (.35 + .65*math.exp(-((i-400)/260)**2)) for i in range(1024))
_DEMO_SCALE = 10**(-9/20) / max(abs(v) for v in _DEMO)
DEMO_PCM = tuple(round(v*_DEMO_SCALE*32767) for v in _DEMO)


def input_wave(c):
    """Proposed raw-input window: identical at any gain or source mute state."""
    c.text(14, 79, 'input / 32ms', GHOST)
    c.text(306, 79, 'demo / +/-1fs', GHOST, right=True)
    c.line((14, 113, 306, 113), BRANCH)
    for x in (14, 160, 306):
        c.line((x, 89, x, 92), BRANCH)
        c.line((x, 134, x, 137), BRANCH)
    previous = None
    for column in range(293):
        lo, hi = column*1024//293, (column+1)*1024//293
        block = DEMO_PCM[lo:hi]
        top = round(113 - max(block)*24/32767)
        bottom = round(113 - min(block)*24/32767)
        x = 14 + column
        if previous is not None:
            c.line((x-1, previous, x, round((top+bottom)/2)), INK)
        c.line((x, top, x, bottom), INK)
        previous = round((top+bottom)/2)


def meter(c, y, value, color):
    """Compact paired meters on one fixed -60..0 dBFS scale."""
    c.line((50, y, 230, y), BRANCH)
    stop = 50 if value is None else round(50 + 180 * (max(-60, value) + 60) / 60)
    for x in range(50, 231, 4):
        c.line((x, y-3, x, y+3), color if value is not None and x <= stop else BRANCH)
    if value is not None:
        diamond(c, stop, y, color)
    c.line((230, y-5, 230, y+5), GHOST)


def lower(s):
    c = Page()
    c.text(14, 10, 'mic', SPOT); c.line((14, 23, 31, 23), SPOT)
    c.text(56, 10, 'ch01 / 01', GHOST)
    c.text(258, 10, 'byp', GHOST); c.text(294, 10, 'x', GHOST)
    c.hit('bypass', (246, 0, 284, 25)); c.hit('delete', (285, 0, 320, 25))
    c.text(14, 32, 'gain', SPOT)
    c.text(152, 32, f'{s.gain_db:+.1f} db', SPOT, right=True)
    rail(c, 14, 152, 53, s.gain / 32767, SPOT)
    unity = round(14 + 138 * 8192 / 32767)
    c.line((unity, 49, unity, 57), GHOST)
    c.text(unity-6, 62, '0', GHOST)
    c.hit('gain / tap selects; relative horizontal drag', (8, 27, 178, 74))
    c.text(230, 32, 'input', GHOST)
    c.text(230, 50, 'open' if s.opened else 'mute', SPOT if s.opened else INK)
    c.line((230, 64, 253, 64), SPOT if s.opened else GHOST)
    c.hit('source open / mute, not monitor-only', (216, 27, 312, 78))
    input_wave(c)
    c.text(14, 147, 'in', GHOST)
    c.text(306, 147, db_text(s.input_db), INK, right=True)
    meter(c, 151, s.input_db, INK)
    c.text(14, 170, 'out', GHOST)
    c.text(306, 170, db_text(s.output_db), SPOT if s.opened else GHOST, right=True)
    meter(c, 174, s.output_db, SPOT)
    for value in (-60, -36, -12, 0):
        x = round(50 + 180 * (value + 60) / 60)
        c.text(x if value == -60 else x-9 if value else x,
               185, str(value), GHOST)
    c.text(14, 206, 'low-cut*', GHOST)
    for x, word in ((92, 'off'), (132, '80'), (171, '160')):
        c.text(x, 206, word, SPOT if word == 'off' else GHOST)
        c.hit('proposed low-cut '+word, (x-6, 191, x+32, 222))
    c.line((92, 218, 109, 218), SPOT)
    c.text(203, 206, 'hz', GHOST)
    c.text(258, 206, 'clip', SPOT if s.clip else GHOST)
    if s.clip:
        diamond(c, 296, 209, SPOT)
        c.line((258, 218, 281, 218), SPOT)
    c.line((14, 223, 306, 223), BRANCH)
    c.footer('b back', 'mod / temp >')
    c.hit('back', (8, 224, 92, 240))
    c.hit('selected mod / temp; existing hold-l retained', (214, 224, 314, 240))
    return c


def upper(s):
    c = Page(400)
    c.text(14, 10, 'mic', SPOT); c.line((14, 23, 31, 23), SPOT)
    c.text(50, 10, '/ rec / play', GHOST)
    c.text(386, 10, 'ch01 / 01', GHOST, right=True)
    c.text(14, 41, 'gain / digital input level', SPOT)
    c.text(14, 56, 'base', GHOST)
    value = f'{s.gain_db:+.1f}'
    numeric(c, value)
    c.text(14, 150, 'db', GHOST)
    c.text(14, 166, 'stored base '+value+' db', GHOST)
    rail(c, 14, 184, 184, s.gain/32767, INK)
    unity = round(14+170*8192/32767)
    c.line((unity, 180, unity, 188), GHOST)
    c.text(14, 199, 'silent', GHOST)
    c.text(unity, 199, '0', GHOST)
    c.text(184, 199, '+12.0 db', GHOST, right=True)
    c.line((239, 48, 239, 207), BRANCH)
    for y, text, color in (
        (46, 'example state', GHOST), (60, value+' db', SPOT),
        (78, 'control '+value+' db', INK), (92, 'target '+value+' db', GHOST),
        (113, 'mod off', INK), (127, 'depth +0%', GHOST),
        (148, 'potential range', GHOST), (162, value+' db', INK),
        (175, value+' db', INK), (197, 'via base', GHOST)):
        c.text(254, y, text, color)
    c.text(14, 216, 'source open / record on rec' if s.opened else
           'source muted / rec receives silence', GHOST)
    c.text(14, 230, 'concept / demo wave+meters / *low-cut proposed', GHOST)
    return c


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (400*3+24, 496), OUTSIDE)
    hit_map = None
    for i, s in enumerate(STATES):
        top, bottom = upper(s), lower(s)
        # Existing helpers assert label bounds/overlaps and disjoint touch zones.
        if hit_map is None:
            hit_map = bottom.hits
        assert bottom.hits == hit_map
        dual = pair(top.im, bottom.im)
        dual.save(OUT / (s.name+'_dual.png'))
        dual.resize((800, 992), Image.Resampling.NEAREST).save(OUT / (s.name+'_dual_2x.png'))
        sheet.paste(dual, (i*412, 0))
    sheet.save(OUT / 'comparison.png')
    sheet.resize((2448, 992), Image.Resampling.NEAREST).save(OUT / 'comparison_2x.png')
    assert not STATES[0].clip and STATES[1].clip and STATES[2].output_db is None
    assert abs(20*math.log10(max(abs(v) for v in DEMO_PCM)/32767) + 9) < .001
    waves = [lower(s).im.crop((14, 89, 307, 138)).tobytes() for s in STATES]
    assert waves[0] == waves[1] == waves[2]
    print('3 authored duals: open / clip / muted; layout and fixed input-wave assertions passed')
    print('preview only; low-cut off; no runtime, build, DSP or hardware claims')
    print(OUT / 'comparison.png')


if __name__ == '__main__':
    main()
