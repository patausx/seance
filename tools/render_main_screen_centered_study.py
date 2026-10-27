#!/usr/bin/env python3
"""One centered sparse-chain still with stronger ambient graphite paper.

Design only: runtime-sized 160/32/32 group, including the existing add slot.
Background is a static procedural proposal, not the shipped paper shader.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_universal_sigils_study import caption, OUTSIDE
from render_main_screen_tiles_study import surface, control, handle, RANGE
from render_main_screen_sparse_study import band
from render_main_screen_accordion_study import center_text

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_sparse_study'


def lattice(x, y):
    n = (x * 374761393 + y * 668265263 + 419) & 0xffffffff
    n = ((n ^ (n >> 13)) * 1274126177) & 0xffffffff
    return ((n ^ (n >> 16)) & 65535) / 32767.5 - 1


def noise(x, y):
    ix, iy = math.floor(x), math.floor(y)
    u, v = x-ix, y-iy
    u, v = u*u*(3-2*u), v*v*(3-2*v)
    a = lattice(ix, iy)*(1-u) + lattice(ix+1, iy)*u
    b = lattice(ix, iy+1)*(1-u) + lattice(ix+1, iy+1)*u
    return a*(1-v) + b*v


def paper():
    # Large, gently warped ink/light fields, without particles or fine noise.
    # One common sheet is cropped into the LCDs to preserve horizontal alignment.
    im = Image.new('RGB', (100, 124))
    for y in range(im.height):
        for x in range(im.width):
            px, py = x*4, y*4
            qx = px + 31*math.sin(py/83) + 14*math.sin(px/71)
            qy = py + 27*math.sin(px/91)
            n = (.65*noise(qx/115+2, qy/115+7)
                 + .25*noise(qx/53+9, qy/53+3)
                 + .10*noise(qx/24+5, qy/24+11))
            light = max(0., min(1., .5 + 1.45*n))
            # Much more visible than the shipped four-level darkening; still
            # well below ghost/ink contrast. A hint of lilac, not neon clouds.
            dark, pale = (17, 18, 22), (66, 61, 75)
            im.putpixel((x, y), tuple(round(a+(b-a)*light) for a, b in zip(dark, pale)))
    return im.resize((400, 496), Image.Resampling.BICUBIC)


def canvas(image):
    c = surface(image.width)
    c.im, c.d = image.copy(), ImageDraw.Draw(image.copy())
    c.d = ImageDraw.Draw(c.im)
    return c


def upper(c, left):
    # Same content hierarchy as ui_chains.cpp; scalar values are examples.
    c.heading(40, 8, 'patch table')
    x, w = 40+left+14, 131
    c.heading(x, 47, 'play', 'spot')
    c.txt(x, 81, 'live control', 'ghost')
    c.txt(x, 101, '58%', 'ink')
    c.txt(x+w, 101, 'speed', 'ghost', right=True)
    c.d.rectangle((x+round(w*.32), 125, x+round(w*.68), 129), fill=RANGE)
    c.line((x, 127, x+w, 127))
    for tx in (x, x+w):
        c.line((tx, 125, tx, 129))
    c.line((x+round(w*.5), 123, x+round(w*.5), 131), 'ghost')
    c.d.rectangle((x+round(w*.58)-1, 123, x+round(w*.58)+1, 131), fill=c.p['spot'])
    c.txt(x, 149, 'target 58%', 'ghost')
    c.txt(x, 163, 'base', 'ink')
    c.line((x, 176, x+w, 176))
    c.txt(x, 185, 'a > speed', 'spot')
    c.line((x, 205, x+w, 205))
    mid, mark = x+w//2, x+round(w*.72)
    c.line((mid, 202, mid, 208))
    c.line((mid, 205, mark, 205), 'spot')
    c.d.rectangle((mark-1, 203, mark+1, 207), fill=c.p['spot'])
    c.txt(x, 222, 'depth +18%', 'spot')
    cx = 40+left+176
    center_text(c, cx, 55, 'mout', 'ink')
    for dx, value in ((-5, .34), (5, .5)):
        c.line((cx+dx, 91, cx+dx, 151))
        yy = 151-round(60*value)
        c.line((cx+dx-2, yy, cx+dx+2, yy), 'ink')
    center_text(c, cx, 222, '02', 'ghost')


def lower(c, left):
    c.txt(left+12, 22, 'edit', 'spot')
    c.line((left+12, 35, left+35, 35), 'spot')
    c.txt(left+54, 22, 'bypass', 'ghost')
    c.txt(left+120, 22, 'more', 'ghost')
    control(c, left+14, 70, 131, 'speed', '50%', .5, gap=24)
    control(c, left+14, 133, 131, 'gain', '75%', .75, gap=24)
    handle(c, left+80, 185, 'spot')
    handle(c, left+176, 185, 'ghost')
    c.txt(left+192+8, 22, '+', 'ghost')
    c.txt(left+192+6, 40, 'add', 'ghost')
    for i, x in enumerate((18, 54, 90, 126), 1):
        c.txt(x, 219, str(i), 'spot' if i == 4 else 'ghost')
        if i == 4:
            c.line((x-5, 233, x+10, 233), 'spot')
    c.txt(304, 219, 'patch02 *', 'ghost', right=True)


def main():
    left = (320-(160+32+32))//2
    assert left == 48 and 320-(left+224) == left
    bg = paper()
    t, b = canvas(bg.crop((0, 0, 400, 240))), canvas(bg.crop((40, 256, 360, 496)))
    for a, end, active in ((left, left+160, True), (left+160, left+192, False)):
        band(t, a, end, top=True, active=active)
        band(b, a, end, active=active)
    upper(t, left)
    lower(b, left)
    sheet = Image.new('RGB', (432, 590), OUTSIDE)
    caption(sheet, 16, 12, '04 / centered group + living paper')
    caption(sheet, 16, 29, 'same tile sizes / stronger ambient background')
    sheet.paste(t.im, (16, 48))
    sheet.paste(b.im, (56, 304))
    caption(sheet, 16, 557, '160 + 32 + add / centered as one group')
    caption(sheet, 16, 575, 'static concept / not runtime or motion capture')
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'centered-paper-2x.png'
    sheet.resize((864, 1180), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
