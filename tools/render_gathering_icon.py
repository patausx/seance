#!/usr/bin/env python3
"""Carved gathering logo: preserve the supplied silhouette and internal slits.

    python3 tools/render_gathering_icon.py [--install]

Pillow only. --install writes the 48px 3DS icon; exports alone are non-invasive.
The original supplied PNG is retained in assets/branding for reproducibility.
"""
import argparse
from math import exp
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/branding/gathering_reference.png'
OUT = ROOT / 'ui_mockups/gathering_installed'
BG = (24, 20, 29)
WHITE = (245, 238, 240)
PURPLE = (155, 110, 198)
ASCII_PURPLE = (139, 78, 187)
GLYPHS = [
    ('101', '111', '101', '111', '101'),
    ('000', '010', '111', '010', '000'),
    ('000', '010', '000', '010', '000'),
    ('001', '001', '010', '100', '100'),
]


def mix(a, b, t):
    return tuple(round(x+(y-x)*t) for x, y in zip(a, b))


def source_mask():
    with Image.open(SOURCE) as source:
        # Strip the screenshot's bright top border and retain the four cuts.
        mask = source.convert('L').crop((4, 5, 143, 140)).point(lambda v: 255 if v > 180 else 0)
    box = mask.getbbox()
    if box is None:
        raise ValueError('No light emblem found')
    return mask.crop(box)


def render(size, textured=True):
    original = source_mask()
    # 4px breathing room at native 48, proportionately the same at other sizes.
    extent = round(size*5/6)
    ratio = extent/max(original.size)
    shape = original.resize((round(original.width*ratio), round(original.height*ratio)),
                            Image.Resampling.LANCZOS).point(lambda v: 255 if v >= 115 else 0)
    mask = Image.new('L', (size, size), 0)
    mask.paste(shape, ((size-shape.width)//2, (size-shape.height)//2))
    inner = mask.filter(ImageFilter.MinFilter(3))
    ink = Image.new('RGB', (size, size), BG)
    alpha, inside, pixels = mask.load(), inner.load(), ink.load()
    for y in range(size):
        for x in range(size):
            if not alpha[x, y]:
                continue
            xx, yy = x*48/size, y*48/size
            # A restrained lilac tide toward the meeting point, not 3D bevels.
            radius2 = (xx-24)**2 + (yy-24)**2
            amount = .48*exp(-radius2/90)
            pixels[x, y] = mix(WHITE, PURPLE, amount)
    if textured and size >= 48:
        # Explicit 3x5 ASCII accents, clipped inside the symbol; no random noise.
        # Only sparse cells in a narrow band, leaving most of the sign solid.
        step = max(1, size//96)
        cell_w, cell_h = 4*step, 6*step
        for row, y in enumerate(range(step, size, cell_h)):
            for col, x in enumerate(range(step, size, cell_w)):
                if (row+col*2) % 3 != 0:
                    continue
                rows = GLYPHS[(row*3+col) % len(GLYPHS)]
                for gy, bits in enumerate(rows):
                    for gx, bit in enumerate(bits):
                        if bit != '1':
                            continue
                        for dy in range(step):
                            for dx in range(step):
                                px, py = x+gx*step+dx, y+gy*step+dy
                                if px >= size or py >= size or not inside[px, py]:
                                    continue
                                xx, yy = px*48/size, py*48/size
                                if 5 < abs(xx-24)+abs(yy-24) < 17:
                                    pixels[px, py] = mix(pixels[px, py], ASCII_PURPLE, .85)
    return ink, mask


def verify(image, mask):
    assert image.size == mask.size
    colors = {color for _, color in image.getcolors(image.width*image.height)}
    assert BG in colors and len(colors) > 8
    assert image.getpixel((0, 0)) == BG
    # Four separate petals, centre remains negative space, cuts stay unpainted.
    assert mask.getpixel((mask.width//2, mask.height//2)) == 0
    for y in range(mask.height):
        for x in range(mask.width):
            if not mask.getpixel((x, y)):
                assert image.getpixel((x, y)) == BG


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    icons = {}
    for size in (24, 48, 128):
        image, mask = render(size)
        verify(image, mask)
        icons[size] = image
        image.save(OUT/f'gathering_{size}.png')
        mask.save(OUT/f'gathering_{size}_mask.png')
    icons[48].resize((384, 384), Image.Resampling.NEAREST).save(OUT/'gathering_48_preview.png')
    icons[128].resize((1024, 1024), Image.Resampling.NEAREST).save(OUT/'gathering_store_1024.png')
    sheet = Image.new('RGB', (864, 506), (226, 190, 206))
    d = ImageDraw.Draw(sheet)
    from render_logo_directions import font
    d.text((24, 16), 'seance / carved gathering', font=font(26), fill=BG)
    sheet.paste(icons[128].resize((336, 336), Image.Resampling.NEAREST), (24, 66))
    sheet.paste(icons[48].resize((288, 288), Image.Resampling.NEAREST), (452, 66))
    d.text((24, 420), 'large version / ascii texture', font=font(17), fill=BG)
    d.text((452, 372), '3ds icon / ×6', font=font(17), fill=BG)
    sheet.paste(icons[48], (452, 413))
    sheet.paste(icons[24], (556, 426))
    d.text((508, 430), '48', font=font(12, True), fill=BG)
    d.text((588, 430), '24', font=font(12, True), fill=BG)
    sheet.save(OUT/'gathering_proof.png')
    if args.install:
        icons[48].save(ROOT/'assets/icon.png')
        icons[24].save(ROOT/'assets/branding/icon_small.png')
        print('installed assets/icon.png and assets/branding/icon_small.png')
    print(OUT/'gathering_proof.png')


if __name__ == '__main__':
    main()
