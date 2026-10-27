#!/usr/bin/env python3
"""One authored REC layout proposal, not a runtime capture or recorded audio.

Uses the existing paper, native labels and inspector numeral helpers only.
The 64-point trace is illustrative; no DSP model or runtime source is changed.
"""
from pathlib import Path
import math
from PIL import Image
from render_menu_style_study import Page, GHOST, BRANCH
from render_module_geometry_study import INK, SPOT, pair
from render_fold_wave_study import numeric, rail

OUT = Path(__file__).resolve().parents[1] / 'ui_mockups/rec_visual_study'


def upper():
    c = Page(400)
    c.text(14, 10, 'rec', SPOT); c.line((14, 23, 31, 23), SPOT)
    c.text(50, 10, '/ mic / out', GHOST)
    c.text(386, 10, 'ch01 / 02', GHOST, right=True)
    c.text(14, 41, 'length / maximum take', SPOT)
    c.text(14, 56, 'base', GHOST)
    numeric(c, '5.0')
    c.text(14, 150, 'seconds', GHOST)
    c.text(14, 166, 'stored base 5.0 s', GHOST)
    rail(c, 14, 184, 184, .25, INK)
    c.text(14, 199, '31 ms', GHOST)
    c.text(184, 199, '20 s', GHOST, right=True)
    c.line((239, 48, 239, 207), BRANCH)
    for y, text, color in (
        (46, 'example state', GHOST), (60, '5.0 s', SPOT),
        (78, 'control 5.0 s', INK), (92, 'target 5.0 s', GHOST),
        (113, 'mod off', INK), (127, 'depth +0%', GHOST),
        (148, 'potential range', GHOST), (162, '5.0 s', INK),
        (175, '5.0 s', INK), (197, 'via base', GHOST)):
        c.text(254, y, text, color)
    c.text(14, 216, 'take 3.20 s / s16 / unsaved', SPOT)
    c.text(14, 230, 'concept / illustrative take / not runtime', GHOST)
    return c


def action(c, x, y, label, box, color=INK):
    c.text(x, y, label, color)
    c.line((x, y+12, x+len(label)*6-1, y+12), color)
    c.hit(label, box)


def lower():
    c = Page()
    c.text(14, 10, 'rec', SPOT); c.line((14, 23, 31, 23), SPOT)
    c.text(56, 10, 'ch01 / 02', GHOST)
    c.text(258, 10, 'byp', GHOST); c.text(294, 10, 'x', GHOST)
    c.hit('bypass', (246, 0, 284, 25)); c.hit('delete', (285, 0, 320, 25))
    c.text(14, 32, 'length', SPOT)
    c.text(184, 32, '5.0 s', SPOT, right=True)
    rail(c, 14, 184, 54, .25, SPOT)
    c.hit('length / select and relative drag', (8, 27, 202, 65))
    c.text(230, 32, 'monitor', GHOST)
    c.text(230, 50, 'on', INK)
    c.line((230, 62, 241, 62), SPOT)
    c.hit('monitor / output only, capture continues', (216, 27, 312, 65))

    c.text(14, 76, 'take / unsaved', SPOT)
    c.text(184, 76, 'x1', GHOST)
    c.text(236, 76, '<', INK); c.text(254, 76, 's16', INK)
    c.text(296, 76, '>', INK)
    c.hit('previous sample slot', (224, 67, 250, 91))
    c.hit('next sample slot', (284, 67, 312, 91))
    # The existing DTO covers recorded_frames, not the planned length. Keep
    # 0..3.20s on this trace; never place a frames/target head halfway through it.
    c.line((14, 118, 306, 118), BRANCH)
    points = []
    for i in range(64):
        t = i/63
        envelope = .12 + .65*math.exp(-((t-.21)/.11)**2) + .43*math.exp(-((t-.66)/.17)**2)
        v = envelope * math.sin(i*2.37)
        points.append((round(14+t*292), round(118-v*25)))
    for a, b in zip(points, points[1:]):
        c.line((*a, *b), INK)
    c.line((14, 142, 14, 145), BRANCH)
    c.line((306, 142, 306, 145), BRANCH)
    c.text(14, 147, '0', GHOST)
    c.text(306, 147, '3.20 s', GHOST, right=True)

    # Commands are separate from parameters, with ordinary full touch cells.
    # In this completed-take example REC/STOP are visibly inactive.
    action(c, 24, 170, 'rec', (8, 161, 100, 190), GHOST)
    action(c, 118, 170, 'stop', (104, 161, 196, 190), GHOST)
    action(c, 224, 170, 'save wav', (202, 161, 312, 190), SPOT)
    action(c, 14, 202, '> play', (8, 194, 100, 221))
    action(c, 118, 202, '> gran', (104, 194, 212, 221))
    action(c, 266, 202, 'clear', (244, 194, 312, 221), GHOST)
    c.line((232, 196, 232, 217), BRANCH)
    c.line((14, 223, 306, 223), BRANCH)
    c.footer('b back', 'mod / temp >')
    c.hit('back', (8, 224, 92, 240))
    c.hit('selected mod / temp proposal', (214, 224, 314, 240))
    return c


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    top, bottom = upper(), lower()
    dual = pair(top.im, bottom.im)
    dual.save(OUT / '01_unsaved_dual.png')
    dual.resize((800, 992), Image.Resampling.NEAREST).save(OUT / '01_unsaved_dual_2x.png')
    print('one authored dual; existing label bounds/overlap and touch-zone assertions passed')
    print('preview only; no native build, runtime edit, DSP or hardware claim')
    print(OUT / '01_unsaved_dual_2x.png')


if __name__ == '__main__':
    main()
