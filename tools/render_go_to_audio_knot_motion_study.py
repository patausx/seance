#!/usr/bin/env python3
"""One 8s GIF demonstrating the ink knot with a synthetic attack/release envelope.

No real audio, runtime edits, camera rotation, new captures or paid generation.
Static study/source images stay untouched. Only the small upper form moves.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageChops
from render_go_to_audio_knot_study import filament
from render_go_to_upper_states_study import background
from render_go_to_unified_study import lower
from render_main_screen_centered_study import caption, OUTSIDE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/go_to_overlay_study'
FPS, SECONDS, STEPS = 20, 8, 420
FRAME_MS = 1000 // FPS
EVENTS = ((.65, 1., 1.05), (1.43, .48, .65), (2.2, .80, .95),
          (2.63, .40, .6), (3.35, .96, 1.15), (4.73, .66, .8), (5.5, 1., 1.2))


def smooth(value):
    x = max(0., min(1., value))
    return x * x * (3 - 2 * x)


def pulse(age, duration, rise=.12):
    if age <= 0 or age >= duration:
        return 0.
    if age < rise:
        return smooth(age / rise)
    return 1 - smooth((age - rise) / (duration - rise))


def envelope(time):
    level = sum(strength * pulse(time - start, duration)
                for start, strength, duration in EVENTS)
    attack = sum(strength * pulse(time - start, .30, .08)
                 for start, strength, _ in EVENTS)
    return min(1., level), min(1., attack)


CENTERLINE = [filament(math.tau * i / STEPS, 0) for i in range(STEPS + 1)]
CURVES = [(strand, [filament(math.tau * i / STEPS, strand) for i in range(STEPS + 1)])
          for strand in range(-3, 4)]
ALL = [p for _, curve in CURVES for p in curve]
XMIN, XMAX = min(p[0] for p in ALL), max(p[0] for p in ALL)
YMIN, YMAX = min(p[1] for p in ALL), max(p[1] for p in ALL)
ZMIN, ZMAX = min(p[2] for p in ALL), max(p[2] for p in ALL)
SCALE = min(110 / (XMAX - XMIN), 110 / (YMAX - YMIN))
MX, MY = (XMIN + XMAX) / 2, (YMIN + YMAX) / 2


def pose(point, centerline, phase, level, attack):
    spread = .42 + .58 * level
    x, y, z = [c + (p - c) * spread for p, c in zip(point, centerline)]
    x, y = x - MX, y - MY
    # Fixed camera and projection scale: amplitude is not normalized away.
    # Pressure opens folds; local tip displacement follows the shorter onset.
    x *= .60 + .40 * level
    y *= .68 + .32 * level
    fold = math.sin(math.tau * 2 * phase + .4)
    x += attack * .20 * fold
    y += attack * .30 * math.sin(math.tau * phase - .8) * math.sin(math.tau * 3 * phase)
    tip = math.exp(-((phase - .15) / .045) ** 2)
    x += attack * .36 * tip
    y -= attack * .22 * tip
    return 200 + x * SCALE, 118 + y * SCALE, z


def frame_top(image, time):
    level, attack = envelope(time)
    segments = []
    for strand, curve in CURVES:
        projected = [pose(p, CENTERLINE[i], i / STEPS, level, attack)
                     for i, p in enumerate(curve)]
        for i, (a, b) in enumerate(zip(projected, projected[1:])):
            xy = [(round(p[0]), round(p[1])) for p in (a, b)]
            assert all(136 <= x <= 264 and 54 <= y <= 182 for x, y in xy)
            segments.append(((a[2] + b[2]) / 2, strand, i / STEPS, xy))
    im = image.copy()
    d = ImageDraw.Draw(im)
    for depth, strand, phase, xy in sorted(segments):
        front = (depth - ZMIN) / (ZMAX - ZMIN)
        value = round((108 + 133 * front) * (.80 + .20 * level))
        color = (value, value, min(255, value + 7))
        if -.5 <= strand <= 1.5 and .08 < phase < .24:
            amount = .18 + .82 * level
            color = tuple(round(a + (b - a) * amount)
                          for a, b in zip(color, (181, 154, 232)))
        d.line(xy, fill=color, width=1)
    return im


def main():
    shared = background()
    base_top = shared.crop((0, 0, 400, 240))
    bottom = lower(shared.crop((40, 256, 360, 496)), current='sequencer')
    sheet = Image.new('RGB', (432, 590), OUTSIDE)
    caption(sheet, 16, 12, 'navigation / ink knot / motion')
    caption(sheet, 16, 29, 'quiet > attack > release / fixed camera')
    sheet.paste(bottom, (56, 304))
    caption(sheet, 16, 557, '8 second loop / only the small form moves')
    caption(sheet, 16, 575, 'demo envelope / no live audio / not runtime')

    # One palette for the entire GIF, no temporal dithering in the frozen UI.
    atlas = Image.new('RGB', (432, 590 + 4 * 240), OUTSIDE)
    atlas.paste(sheet, (0, 0))
    for i, time in enumerate((0., .73, .85, 1.15)):
        atlas.paste(frame_top(base_top, time), (16, 590 + i * 240))
    quantized = atlas.quantize(colors=240, method=Image.Quantize.MEDIANCUT)
    colors = quantized.getpalette()[:240 * 3]
    fixed = [(244, 244, 244), (181, 154, 232), (184, 184, 184), (94, 94, 94),
             OUTSIDE, (184, 180, 192)]
    fixed += [(v, v, min(255, v + 7)) for v in range(100, 251, 17)]
    fixed = (fixed + [OUTSIDE] * 16)[:16]
    palette = Image.new('P', (1, 1))
    palette.putpalette(colors + [v for color in fixed for v in color])

    frames = []
    for index in range(FPS * SECONDS):
        frame = sheet.copy()
        frame.paste(frame_top(base_top, index / FPS), (16, 48))
        frame = frame.quantize(palette=palette, dither=Image.Dither.NONE)
        frames.append(frame.resize((864, 1180), Image.Resampling.NEAREST))
    assert frames[0].tobytes() == frames[-1].tobytes(), 'quiet seamless loop endpoints'
    path = OUT / 'go-to-audio-knot-motion-2x.gif'
    OUT.mkdir(parents=True, exist_ok=True)
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=FRAME_MS,
                   loop=0, disposal=1, optimize=False)

    # Check the actual encoded artifact: fixed background/menu, changing knot,
    # correct duration, no palette-induced flicker outside the small form.
    with Image.open(path) as result:
        total_ms, changing = 0, False
        first = result.convert('RGB')
        for index in range(result.n_frames):
            result.seek(index)
            total_ms += result.info.get('duration', 0)
            decoded = result.convert('RGB')
            delta = ImageChops.difference(first, decoded).getbbox()
            if delta:
                changing = True
                assert 304 <= delta[0] < delta[2] <= 560
                assert 204 <= delta[1] < delta[3] <= 460
        assert total_ms == SECONDS * 1000 and changing and result.info['loop'] == 0
        count = result.n_frames
    print(f'{path.relative_to(ROOT)} / {total_ms}ms / {count} encoded frames / {path.stat().st_size} bytes')


if __name__ == '__main__':
    main()
