#!/usr/bin/env python3
"""Three non-shipping carved emblem studies; existing branding is read-only.

Run directly with Pillow. Writes one comparison sheet and three editable SVGs
under ui_mockups/carved_mark_study. No install/build/device path.
"""
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/carved_mark_study'
PAPER = '#efefeb'
INK = '#171717'
GREY = '#777773'
LINE = '#d0d0cb'

# Each master is one filled polygon, optionally with a single counter.
# Coordinates are in a native icon-sized 48-unit design space.
MARKS = [
    {
        'name': 'cut',
        'caption': 'open silhouette · hint of s',
        'outer': [(26,3),(41,12),(34,19),(26,13),(19,17),
                  (32,24),(37,29),(37,36),(22,46),(7,36),
                  (14,29),(22,36),(29,32),(14,24),(10,20),(10,12)],
        'counter': [],
    },
    {
        'name': 'seal',
        'caption': 'solid stone · inner break',
        'outer': [(25,3),(40,13),(40,34),(23,46),(8,35),(8,15)],
        'counter': [(25,11),(32,17),(24,23),(31,29),
                    (24,38),(17,32),(23,28),(16,22)],
    },
    {
        'name': 'loop',
        'caption': 'letterform · one counter',
        'outer': [(24,3),(38,15),(25,28),(18,28),(18,33),
                  (25,38),(35,28),(41,34),(25,46),(8,34),(8,15)],
        'counter': [(18,16),(24,12),(29,16),(18,24)],
    },
]


def font(size, mono=False):
    family = 'monospace' if mono else 'sans'
    path = subprocess.check_output(
        ['fc-match', '-f', '%{file}', family], text=True).strip()
    return ImageFont.truetype(path, size)


def mask(mark, size, native=False):
    factor = 1 if native else 4
    n = size * factor
    result = Image.new('L', (n, n), 0)
    draw = ImageDraw.Draw(result)
    def points(key):
        return [(round(x*n/48), round(y*n/48)) for x,y in mark[key]]
    draw.polygon(points('outer'), fill=255)
    if mark['counter']:
        draw.polygon(points('counter'), fill=0)
    return result if native else result.resize((size,size), Image.Resampling.LANCZOS)


def stamp(image, mark, xy, size, color=INK, native=False):
    image.paste(color, (*xy, xy[0]+size, xy[1]+size), mask(mark,size,native))


def svg(mark):
    def contour(points):
        return 'M ' + ' L '.join(f'{x} {y}' for x,y in points) + ' Z'
    path = contour(mark['outer'])
    if mark['counter']:
        path += ' ' + contour(mark['counter'])
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">\n'
            f'  <path fill="currentColor" fill-rule="evenodd" d="{path}"/>\n'
            '</svg>\n')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    with Image.open(ROOT/'assets/branding/gothic_wordmark_pixel_master.png') as source:
        word = source.convert('L').resize((258,78), Image.Resampling.NEAREST)
    board = Image.new('RGB', (1200,850), PAPER)
    draw = ImageDraw.Draw(board)
    small, label, title, heading = font(14), font(17), font(27), font(30)
    mono = font(14, True)
    draw.text((36,24), 'seance / new mark', font=heading, fill=INK)
    draw.text((36,69), 'three studies · wordmark unchanged · no color or texture',
              font=label, fill=GREY)
    draw.line((36,108,1164,108), fill=LINE)
    for i,mark in enumerate(MARKS):
        x = i*400
        if i:
            draw.line((x,132,x,790), fill=LINE)
        draw.text((x+36,132), f'0{i+1}', font=mono, fill=GREY)
        draw.text((x+36,158), mark['name'], font=title, fill=INK)
        stamp(board, mark, (x+80,210), 240)
        draw.text((x+36,478), mark['caption'], font=label, fill=GREY)

        # The retained pixel master at exact integer scale, paired with a
        # compact native-size symbol; this is a proportion study, not UI.
        draw.rectangle((x+20,526,x+379,674), fill=INK)
        stamp(board, mark, (x+35,570), 48, PAPER, native=True)
        board.paste(PAPER, (x+103,559,x+361,637), word)

        draw.text((x+36,704), 'pixel preview / 1:1', font=small, fill=GREY)
        stamp(board, mark, (x+36,739), 48, native=True)
        stamp(board, mark, (x+142,755), 24, native=True)
        draw.text((x+93,757), '48', font=mono, fill=GREY)
        draw.text((x+178,757), '24', font=mono, fill=GREY)
        draw.rectangle((x+251,735,x+306,790), fill=INK)
        stamp(board, mark, (x+255,739), 48, PAPER, native=True)
        (OUT/f'0{i+1}.svg').write_text(svg(mark), encoding='utf-8')
    draw.line((36,809,1164,809), fill=LINE)
    draw.text((36,824), 'studies, not installed icons', font=small, fill=GREY)
    board.save(OUT/'comparison.png')
    print(OUT/'comparison.png')


if __name__ == '__main__':
    main()
