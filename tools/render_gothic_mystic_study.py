#!/usr/bin/env python3
"""Non-shipping gothic/mystical concept sheet from generated raster sketches.

Retains the project's exact pixel wordmark. No tracing, asset installation or
runtime changes. 24/48px thumbnails are only rough silhouette studies, not final
hand-tuned icons. Source image is retained alongside the single comparison PNG.
"""
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/gothic_mystic_study'
PAPER, INK, MUTED, RULE = '#efefeb', '#171717', '#777773', '#d0d0cb'
CONCEPTS = [
    ('threshold', 'between seen and hidden'),
    ('bond', 'interlaced · ritual knot'),
    ('apparition', 'spirit · carved flame'),
]


def main():
    font_path = subprocess.check_output(
        ['fc-match', '-f', '%{file}', 'sans'], text=True).strip()
    small, label, title, heading = [ImageFont.truetype(font_path, size)
                                    for size in (14, 16, 26, 30)]
    with Image.open(OUT/'source.png') as source:
        source = ImageOps.invert(source.convert('L')).point(lambda p: 255 if p > 140 else 0)
    with Image.open(ROOT/'assets/branding/gothic_wordmark_pixel_master.png') as master:
        wordmark = master.convert('L').resize((258,78), Image.Resampling.NEAREST)
    board = Image.new('RGB', (1200,910), PAPER)
    draw = ImageDraw.Draw(board)
    draw.text((36,24), 'seance / gothic and mystic', font=heading, fill=INK)
    draw.text((36,70), 'symbols, not monograms · existing wordmark unchanged',
              font=label, fill=MUTED)
    draw.line((36,108,1164,108), fill=RULE)

    def stamp(mask, xy, box, color=INK, pixel=False):
        fitted = ImageOps.contain(mask, box, Image.Resampling.LANCZOS)
        if pixel:
            fitted = fitted.point(lambda p: 255 if p >= 128 else 0)
        x = xy[0] + (box[0]-fitted.width)//2
        y = xy[1] + (box[1]-fitted.height)//2
        board.paste(color, (x,y,x+fitted.width,y+fitted.height), fitted)

    for i,(name,caption) in enumerate(CONCEPTS):
        x = 400*i
        if i:
            draw.line((x,132,x,848), fill=RULE)
        panel = source.crop((i*source.width//3,0,(i+1)*source.width//3,source.height))
        bounds = panel.getbbox()
        if bounds is None:
            raise ValueError(f'empty concept {i+1}')
        mark = panel.crop(bounds)
        draw.text((x+36,133), f'0{i+1} / {name}', font=title, fill=INK)
        stamp(mark, (x+74,193), (252,334))
        draw.text((x+36,550), caption, font=label, fill=MUTED)
        draw.rectangle((x+20,602,x+379,738), fill=INK)
        stamp(mark, (x+33,633), (54,74), PAPER)
        board.paste(PAPER, (x+103,631,x+361,709), wordmark)
        draw.text((x+36,762), 'rough preview at 48 / 24 px', font=small, fill=MUTED)
        stamp(mark, (x+36,794), (48,48), pixel=True)
        stamp(mark, (x+116,806), (24,24), pixel=True)
        draw.rectangle((x+250,788,x+309,847), fill=INK)
        stamp(mark, (x+256,794), (48,48), PAPER, pixel=True)
    draw.line((36,869,1164,869), fill=RULE)
    draw.text((36,882), 'generated symbol studies · not final icons · not installed in the app',
              font=small, fill=MUTED)
    board.save(OUT/'comparison.png')
    print(OUT/'comparison.png')


if __name__ == '__main__':
    main()
