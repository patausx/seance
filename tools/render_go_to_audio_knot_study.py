#!/usr/bin/env python3
"""One still of a proposed audio-reactive ink knot above navigation.

The open/attack pose is authored, not driven by recorded or live audio.
No animation, audio analysis, new runtime capture or application changes.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_go_to_upper_states_study import background
from render_go_to_unified_study import lower
from render_main_screen_centered_study import caption, OUTSIDE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/go_to_overlay_study'
TAU = math.tau


def filament(t, strand):
    # A folded open-volume knot, not a circular waveform or a level meter.
    # The asymmetric stretch is an illustrative attack pose only.
    swell = 1 + .14 * math.cos(3 * t + .4) + .07 * math.sin(5 * t)
    x = (math.sin(t) + 2 * math.sin(2 * t)) * swell
    y = (math.cos(t) - 2 * math.cos(2 * t)) * .83
    z = -.92 * math.sin(3 * t)
    x += strand * .075 * math.cos(4 * t + .7)
    y += strand * .068 * math.sin(3 * t - .3)
    z += strand * (.058 + .03 * math.cos(5 * t))
    # Tip pulls are localized, rather than scaling a whole pulsating ring.
    pull = math.exp(-((t - .9) / .19) ** 2)
    x += .74 * pull
    y -= .49 * pull
    # Fixed camera for the single pose; no ambient rotation is implied.
    angle = .84
    y, z = y * math.cos(angle) - z * math.sin(angle), y * math.sin(angle) + z * math.cos(angle)
    angle = -.48
    x, z = x * math.cos(angle) + z * math.sin(angle), -x * math.sin(angle) + z * math.cos(angle)
    angle = -.39
    x, y = x * math.cos(angle) - y * math.sin(angle), x * math.sin(angle) + y * math.cos(angle)
    perspective = 1 / (1 + z * .085)
    return x * perspective, y * perspective, z


def ink_knot(image):
    steps = 420
    curves = [(strand, [filament(TAU * i / steps, strand) for i in range(steps + 1)])
              for strand in range(-3, 4)]
    points = [p for _, curve in curves for p in curve]
    xmin, xmax = min(p[0] for p in points), max(p[0] for p in points)
    ymin, ymax = min(p[1] for p in points), max(p[1] for p in points)
    zmin, zmax = min(p[2] for p in points), max(p[2] for p in points)
    scale = min(116 / (xmax - xmin), 116 / (ymax - ymin))
    mx, my = (xmin + xmax) / 2, (ymin + ymax) / 2
    segments = []
    for strand, curve in curves:
        for i, (a, b) in enumerate(zip(curve, curve[1:])):
            depth = (a[2] + b[2]) / 2
            pts = [(round(200 + (p[0] - mx) * scale), round(118 + (p[1] - my) * scale))
                   for p in (a, b)]
            segments.append((depth, strand, i / steps, pts))
    im = image.copy()
    d = ImageDraw.Draw(im)
    # Back fibres first. Different tonal depth keeps the knot legible at native
    # size; the restrained lilac accent is one fold, not an outer glow/halo.
    for depth, strand, phase, pts in sorted(segments):
        front = (depth - zmin) / (zmax - zmin)
        value = round(108 + 133 * front)
        color = (value, value, min(255, value + 7))
        if -.5 <= strand <= 1.5 and .08 < phase < .24:
            color = (181, 154, 232)
        d.line(pts, fill=color, width=1)
    bounds = (round(200 - (xmax - xmin) * scale / 2),
              round(118 - (ymax - ymin) * scale / 2),
              round(200 + (xmax - xmin) * scale / 2),
              round(118 + (ymax - ymin) * scale / 2))
    assert bounds[2] - bounds[0] <= 118 and bounds[3] - bounds[1] <= 118
    return im


def main():
    shared = background()
    top = ink_knot(shared.crop((0, 0, 400, 240)))
    bottom = lower(shared.crop((40, 256, 360, 496)), current='sequencer')
    sheet = Image.new('RGB', (432, 590), OUTSIDE)
    caption(sheet, 16, 12, 'navigation / ink knot')
    caption(sheet, 16, 29, 'small reactive-form proposal / attack pose')
    sheet.paste(top, (16, 48))
    sheet.paste(bottom, (56, 304))
    caption(sheet, 16, 557, 'one form / no clock / same blur and menu')
    caption(sheet, 16, 575, 'static concept / no live audio or animation')
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'go-to-audio-knot-2x.png'
    sheet.resize((864, 1180), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
