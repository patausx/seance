#!/usr/bin/env python3
"""Export the approved Pink gate at Seance's native icon and LCD sizes.

24/48px use the existing separately fitted masks, not a resized color preview.
LCDs use integer 4x master pixels; they are standalone composition studies, NOT
runtime splash replacements. Prior previews and shipping resources are read-only.
"""
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED
from PIL import Image, ImageChops, ImageDraw, ImageFont
from render_user_threshold_seance_pink_study import pink_colors, mix
from render_user_threshold_study import verify

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'ui_mockups/user_threshold_study'
APPROVED = ROOT / 'ui_mockups/user_threshold_seance_pink_study/logo.png'
OUT = ROOT / 'ui_mockups/user_threshold_seance_pink_sizes'


def centered(master, size, scale):
    art = master.resize((master.width * scale, master.height * scale),
                        Image.Resampling.NEAREST)
    assert art.width <= size[0] and art.height <= size[1]
    mask = Image.new('L', size, 0)
    mask.paste(art, ((size[0] - art.width) // 2, (size[1] - art.height) // 2))
    return mask


def paint(mask, colors):
    verify(mask)
    left, _, right, _ = mask.getbbox()
    image = Image.new('RGB', mask.size)
    pixels, ink = image.load(), mask.load()
    for x in range(mask.width):
        paper = mix(colors['cell'], colors['bg'], x / (mask.width - 1))
        t = max(0, min(1, (x - left) / (right - left - 1)))
        gate = mix(colors['ink'], colors['spot'], t)
        for y in range(mask.height):
            pixels[x, y] = gate if ink[x, y] else paper
    recovered = image.convert('L').point(lambda p: 255 if p < 128 else 0)
    assert ImageChops.difference(recovered, mask).getbbox() is None
    return image


def contact_sheet(images, colors):
    board = Image.new('RGB', (800, 600), colors['cell'])
    draw = ImageDraw.Draw(board)
    fp = subprocess.check_output(['fc-match', '-f', '%{file}', 'sans'], text=True).strip()
    title, label, small = [ImageFont.truetype(fp, n) for n in (22, 16, 13)]
    draw.text((24, 16), 'seance / pink gate', font=title, fill=colors['ink'])
    draw.text((24, 54), '48×48 / ×4', font=label, fill=colors['ink'])
    draw.text((248, 54), '24×24 / ×8', font=label, fill=colors['ink'])
    for name, pos in (('icon_48.png', (24, 82)), ('icon_24.png', (248, 82))):
        board.paste(images[name].resize((192, 192), Image.Resampling.NEAREST), pos)
    draw.text((480, 54), 'native sizes / 1:1', font=label, fill=colors['ink'])
    board.paste(images['icon_48.png'], (480, 90))
    board.paste(images['icon_24.png'], (556, 114))
    draw.text((480, 150), '48 and 24 masks fitted separately', font=small, fill=colors['ink'])
    draw.text((480, 174), 'no outline smoothing', font=small, fill=colors['ink'])
    draw.line((24, 294, 776, 294), fill=colors['bg'])
    draw.text((24, 306), '400×240 / upper screen / 1:1', font=label, fill=colors['ink'])
    draw.text((456, 306), '320×240 / lower screen / 1:1', font=label, fill=colors['ink'])
    board.paste(images['screen_top_400x240.png'], (24, 334))
    board.paste(images['screen_bottom_320x240.png'], (456, 334))
    return board


def main():
    colors = pink_colors()
    with Image.open(SOURCE / 'user_master_44x49.png') as source:
        master = source.convert('L')
    assert master.size == (44, 49)
    masks = {}
    for size in (48, 24):
        with Image.open(SOURCE / f'icon_{size}_mask.png') as source:
            masks[f'icon_{size}.png'] = source.convert('L')
        assert masks[f'icon_{size}.png'].size == (size, size)
    masks['screen_top_400x240.png'] = centered(master, (400, 240), 4)
    masks['screen_bottom_320x240.png'] = centered(master, (320, 240), 4)
    masks['logo_640.png'] = centered(master, (640, 640), 10)
    images = {name: paint(mask, colors) for name, mask in masks.items()}
    with Image.open(APPROVED) as source:
        assert ImageChops.difference(images['logo_640.png'], source.convert('RGB')).getbbox() is None
    OUT.mkdir(parents=True, exist_ok=True)
    for name, image in images.items():
        image.save(OUT / name)
        with Image.open(OUT / name) as decoded:
            assert decoded.mode == 'RGB' and decoded.size == image.size
            assert decoded.tobytes() == image.tobytes()
        print(f'{name}: {image.width}x{image.height}, RGB; contour preserved')
    contact_sheet(images, colors).save(OUT / 'preview.png')
    with ZipFile(OUT / 'seance_pink_gate_png.zip', 'w', compression=ZIP_DEFLATED) as archive:
        for name in (*images, 'preview.png'):
            archive.write(OUT / name, name)
    print(OUT / 'preview.png')
    print(OUT / 'seance_pink_gate_png.zip')


if __name__ == '__main__':
    main()
