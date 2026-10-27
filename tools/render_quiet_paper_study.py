#!/usr/bin/env python3
"""One design-only GIF: current host UI over quieter centered-paper motion.

Capture the two fixtures first with tools/capture_quiet_paper.py. This renderer
changes only the captured paper mask, never cells, glyphs, rails or parameters.
The field follows patch_paper.h, but contrast and period are proposed, not shipped.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/quiet_paper_study'
SOURCE = OUT / 'source'
SIZE = 64
FRAMES, FRAME_MS = 300, 100
DARK, PALE = (28, 29, 30), (48, 46, 52)
OUTSIDE, LABEL = (20, 20, 22), (184, 180, 192)
SCENES = ('sequencer', 'start_menu')
RAMPS = [tuple((a*(255-i)+b*i+127)//255 for a, b in zip(DARK, PALE))
         for i in range(256)]


def lattice(x, y):
    n = (x*374761393 + y*668265263 + 419) & 0xffffffff
    n = ((n ^ (n >> 13))*1274126177) & 0xffffffff
    return ((n ^ (n >> 16)) & 65535)/32767.5 - 1


def noise(x, y):
    ix, iy = math.floor(x), math.floor(y)
    u, v = x-ix, y-iy
    u, v = u*u*(3-2*u), v*v*(3-2*v)
    a = lattice(ix, iy)*(1-u) + lattice(ix+1, iy)*u
    b = lattice(ix, iy+1)*(1-u) + lattice(ix+1, iy+1)*u
    return a*(1-v) + b*v


def field(phase):
    """The same bounded 64x64 field, sampled on one 400x496 sheet."""
    s, c = math.sin(math.tau*(phase % 1)), math.cos(math.tau*(phase % 1))
    xs = [(i+.5)*400/SIZE-2 for i in range(SIZE)]
    ys = [(i+.5)*496/SIZE-2 for i in range(SIZE)]
    xwarp = [x+14*math.sin(x/71-.24*s)+18*s for x in xs]
    ywarp = [27*math.sin(x/91+.30*s)+18*(c-1) for x in xs]
    rowwarp = [31*math.sin(y/83+.32*s) for y in ys]
    pixels = bytearray()
    for y in range(SIZE):
        for x in range(SIZE):
            qx, qy = xwarp[x]+rowwarp[y], ys[y]+ywarp[x]
            n = (.65*noise(qx/115+2, qy/115+7)
                 + .25*noise(qx/53+9+.12*s, qy/53+3+.10*(c-1))
                 + .10*noise(qx/24+5-.10*s, qy/24+11))
            pixels.append(round(max(0, min(1, .5+1.45*n))*255))
    return Image.frombytes('L', (SIZE, SIZE), bytes(pixels)).resize(
        (400, 496), Image.Resampling.BILINEAR)


def indexed(image, colors, palette):
    raw = image.convert('RGB').tobytes()
    lookup = {bytes(c): i for i, c in enumerate(colors)}
    result = Image.frombytes('P', image.size,
                             bytes(lookup[raw[i:i+3]] for i in range(0, len(raw), 3)))
    result.putpalette(palette)
    return result


def main():
    captures, masks = {}, {}
    colors = set(RAMPS + [OUTSIDE, LABEL])
    for scene in SCENES:
        for screen, size in (('top', (400, 240)), ('bottom', (320, 240))):
            key = f'{scene}_{screen}'
            captures[key] = Image.open(SOURCE / f'{key}.ppm').convert('RGB')
            masks[key] = Image.open(SOURCE / f'{key}_paper_mask.pgm').convert('L')
            assert captures[key].size == masks[key].size == size
            assert set(masks[key].get_flattened_data()) <= {0, 255}, 'paper masks must be binary'
            colors.update(captures[key].get_flattened_data())
    colors = sorted(colors)
    assert len(colors) <= 256, f'{len(colors)} colors: do not silently quantize UI'
    palette = [v for color in colors for v in color] + [0]*(768-3*len(colors))
    index = {color: i for i, color in enumerate(colors)}
    lut = [index[color] for color in RAMPS]
    captures = {key: indexed(im, colors, palette) for key, im in captures.items()}
    base = Image.new('P', (856, 576), index[OUTSIDE])
    base.putpalette(palette)
    draw = ImageDraw.Draw(base)
    placements = []
    for column, scene in enumerate(SCENES):
        x = 16+column*424
        draw.text((x, 10), 'sequencer' if column == 0 else 'start menu', fill=index[LABEL])
        draw.text((x, 26), 'quiet paper / same controls and type', fill=index[LABEL])
        for screen, dx, dy, crop in (
                ('top', 0, 48, (0, 0, 400, 240)),
                ('bottom', 40, 304, (40, 256, 360, 496))):
            key = f'{scene}_{screen}'
            base.paste(captures[key], (x+dx, dy))
            placements.append((key, (x+dx, dy), crop))
    draw.text((16, 558), 'background proposal / current host UI / 30s loop / not a 3ds capture',
              fill=index[LABEL])
    assert field(0).tobytes() == field(1).tobytes()
    frames = []
    for i in range(FRAMES):
        gray = field(i/FRAMES)
        paper = gray.point(lut, 'P')
        paper.putpalette(palette)
        frame = base.copy()
        for key, pos, crop in placements:
            frame.paste(paper.crop(crop), pos, masks[key])
            # Verify even bg-coloured inverse glyphs/cell holes remain untouched.
            rendered = frame.crop((pos[0], pos[1], pos[0]+captures[key].width,
                                   pos[1]+captures[key].height)).tobytes()
            before = captures[key].tobytes()
            assert all(a == b for a, b, mask in zip(rendered, before, masks[key].tobytes())
                       if not mask), f'foreground changed: {key}'
        frames.append(frame.resize((1712, 1152), Image.Resampling.NEAREST))
    path = OUT / 'quiet-paper-sequencer-menu-2x.gif'
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=FRAME_MS,
                   loop=0, disposal=1, optimize=True)
    with Image.open(path) as result:
        assert result.n_frames == FRAMES and result.info['loop'] == 0
        assert result.info['duration'] == FRAME_MS
    print(f'{path.relative_to(ROOT)} / 30s / {path.stat().st_size} bytes')


if __name__ == '__main__':
    main()
