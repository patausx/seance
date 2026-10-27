#!/usr/bin/env python3
"""One seamless ambient-background GIF of the approved centered still.

Presentation only; no runtime edits or simulated parameter/audio activity.
Fixed GIF palette and no dithering keep all UI ink stable between frames.
"""
from pathlib import Path
import math
from PIL import Image
from render_main_screen_centered_study import (
    noise, paper as still_paper, canvas, band, upper, lower, caption, OUTSIDE,
)
from render_main_screen_tiles_study import ACTIVE, RANGE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_sparse_study'
FRAMES = 180
FRAME_MS = 100
DARK, PALE = (17, 18, 22), (66, 61, 75)


def paper(phase):
    angle = math.tau * (phase % 1.)
    s, c = math.sin(angle), math.cos(angle)
    im = Image.new('RGB', (100, 124))
    for y in range(im.height):
        for x in range(im.width):
            px, py = x*4, y*4
            # Periodic, spatially varying deformation, not a whole-screen pulse.
            # At phase zero the field is exactly the preceding approved still.
            qx = (px + 31*math.sin(py/83 + .32*s)
                  + 14*math.sin(px/71 - .24*s) + 18*s)
            qy = py + 27*math.sin(px/91 + .30*s) + 18*(c-1)
            n = (.65*noise(qx/115+2, qy/115+7)
                 + .25*noise(qx/53+9+.12*s, qy/53+3+.10*(c-1))
                 + .10*noise(qx/24+5-.10*s, qy/24+11))
            light = max(0., min(1., .5 + 1.45*n))
            im.putpixel((x, y), tuple(round(a+(b-a)*light) for a, b in zip(DARK, PALE)))
    return im.resize((400, 496), Image.Resampling.BICUBIC)


def palette():
    colors = [tuple(round(a+(b-a)*i/191) for a, b in zip(DARK, PALE)) for i in range(192)]
    colors += list(canvas(Image.new('RGB', (320, 240))).p.values())
    colors += [ACTIVE, RANGE, OUTSIDE, (184, 180, 192)]
    colors = list(dict.fromkeys(colors))
    assert len(colors) <= 256
    colors.extend([OUTSIDE] * (256-len(colors)))
    pal = Image.new('P', (1, 1))
    pal.putpalette([channel for color in colors for channel in color])
    return pal


def compose(bg):
    left = 48
    t = canvas(bg.crop((0, 0, 400, 240)))
    b = canvas(bg.crop((40, 256, 360, 496)))
    for a, end, active in ((left, left+160, True), (left+160, left+192, False)):
        band(t, a, end, top=True, active=active)
        band(b, a, end, active=active)
    upper(t, left)
    lower(b, left)
    sheet = Image.new('RGB', (432, 590), OUTSIDE)
    caption(sheet, 16, 12, '04 / centered group + living paper')
    caption(sheet, 16, 29, 'same tile sizes / only the background moves')
    sheet.paste(t.im, (16, 48))
    sheet.paste(b.im, (56, 304))
    caption(sheet, 16, 557, 'slow ambient drift / seamless 18 second loop')
    caption(sheet, 16, 575, 'motion concept / not a 3ds runtime capture')
    return sheet


def main():
    initial = paper(0)
    assert initial.tobytes() == still_paper().tobytes()
    assert paper(1).tobytes() == initial.tobytes()
    pal = palette()
    frames = []
    # These solid selected-column rectangles, including all their text/rails,
    # must remain byte-identical while the surrounding paper moves.
    frozen_boxes = ((106, 88, 262, 288), (106, 304, 262, 502))
    frozen = None
    for i in range(FRAMES):
        frame = compose(initial if i == 0 else paper(i/FRAMES))
        frame = frame.quantize(palette=pal, dither=Image.Dither.NONE)
        content = [frame.crop(box).tobytes() for box in frozen_boxes]
        if frozen is None:
            frozen = content
        assert content == frozen, 'selected UI must not animate or palette-flicker'
        frames.append(frame.resize((864, 1180), Image.Resampling.NEAREST))
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'centered-paper-motion-2x.gif'
    frames[0].save(path, save_all=True, append_images=frames[1:],
                   duration=FRAME_MS, loop=0, disposal=1, optimize=False)
    with Image.open(path) as result:
        assert result.n_frames == FRAMES and result.info['loop'] == 0
        assert result.info['duration'] == FRAME_MS
    print(f'{path.relative_to(ROOT)} / {FRAMES} frames / 18s / {path.stat().st_size} bytes')


if __name__ == '__main__':
    main()
