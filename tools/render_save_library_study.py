#!/usr/bin/env python3
"""Static save-library proposal, not runtime or device captures.

One two-theme, box-free branch-style board at 2x nearest-neighbour. Shipping
6x8 text, baked patches heading and 7px chevron; illustrative names/times.
No application or project IO. The previous boxed proposal is kept.
"""
from functools import lru_cache
from pathlib import Path
import re

from PIL import Image
from render_module_geometry_study import Canvas

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/save_library_study'
OUTSIDE = (20, 20, 23)
COLORS = {
    'pink': dict(ink=(31, 20, 31), spot=(91, 49, 124),
                 quiet=(126, 100, 119), rail=(174, 145, 165)),
    'graphite': dict(ink=(244, 244, 244), spot=(181, 154, 232),
                     quiet=(184, 184, 184), rail=(255, 255, 255)),
}


@lru_cache(maxsize=1)
def title_mask():
    source = (ROOT / 'platform/3ds/menu_labels_data.h').read_text()
    offset, width, height = map(int, re.search(
        r'\{"patches", (\d+), (\d+), (\d+)\}', source).groups())
    data = bytes(int(v, 16) for v in re.findall(
        r'0x([0-9a-f]{2})', source.split('bitmap[] = {', 1)[1].split('};', 1)[0]))
    mask = Image.new('L', (width, height))
    stride = (width + 7) // 8
    for y in range(height):
        for x in range(width):
            if data[offset + y * stride + x // 8] & (0x80 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


def paper(width, theme):
    c = Canvas(width)
    for y in range(240):
        d, n = 239 ** 3, y * y * (717 - 2 * y)
        color = (36, 36, 36) if theme == 'graphite' else tuple(
            (a * d + (b - a) * n + d // 2) // d
            for a, b in zip((228, 194, 207), (211, 187, 206)))
        c.d.line((0, y, width - 1, y), fill=color)
    return c


def heading(c, p):
    mask = title_mask()
    c.im.paste(p['ink'], (20, 13), mask)
    c.labels.append(((20, 13, 20 + mask.width, 13 + mask.height), 'patches heading'))


def marker(c, x, y, color):
    # Same unfilled 7px chevron as menu_style::marker, not a solid triangle.
    for dy in range(-3, 4):
        c.im.putpixel((x - abs(dy), y + dy), color)


def top(theme):
    p = COLORS[theme]
    c = paper(400, theme)
    heading(c, p)
    c.text(380, 21, 'project library', p['quiet'], right=True)
    c.text(20, 52, 'selected / 003', p['spot'])
    c.text(20, 73, 'glass choir', p['ink'], scale=2)
    c.d.line((20, 112, 20, 130), fill=p['rail'])
    for y in (112, 130):
        c.d.line((20, y, 32, y), fill=p['rail'])
    c.text(44, 108, 'last saved', p['quiet'])
    c.text(160, 108, '26 sep 2026 / 14:32', p['ink'])
    c.text(44, 126, 'on sd card', p['quiet'])
    c.text(160, 126, 'saved file', p['ink'])

    # Independent open-session branch; no panel, frame or tinted background.
    c.text(20, 154, 'currently open', p['quiet'])
    c.d.line((20, 178, 20, 198), fill=p['rail'])
    c.d.line((20, 178, 32, 178), fill=p['rail'])
    c.d.line((20, 198, 34, 198), fill=p['spot'])
    marker(c, 34, 198, p['spot'])
    c.text(44, 174, 'night bus *', p['ink'])
    c.text(380, 174, '001', p['quiet'], right=True)
    c.text(44, 194, 'unsaved changes', p['spot'])
    c.text(380, 194, 'save -> 001', p['quiet'], right=True)
    c.text(20, 225, 'a open selected   x save current', p['quiet'])
    c.text(380, 225, 'b close', p['quiet'], right=True)
    return c.im


def action(c, x, width, label, p, accented=False):
    # Unframed text action, matching the existing patch menu's save underline.
    left = x + (width - len(label) * 6) // 2
    c.text(left, 213, label, p['spot'] if accented else p['ink'])
    if accented:
        c.d.line((left - 3, 226, left + len(label) * 6 + 2, 226), fill=p['spot'])


def bottom(theme):
    p = COLORS[theme]
    c = paper(320, theme)
    heading(c, p)
    c.text(181, 21, '5 saved', p['quiet'])
    c.text(300, 21, 'more >', p['ink'], right=True)
    names = ('night bus', 'soft metal', 'glass choir', 'slow weather', 'broken amen', 'empty')
    c.d.line((20, 56, 20, 166), fill=p['rail'])
    for i, name in enumerate(names):
        y = 44 + 22 * i
        selected = i == 2
        color = p['spot'] if selected else p['quiet'] if i == 5 else p['ink']
        c.d.line((20, y + 12, 36, y + 12), fill=p['spot'] if selected else p['rail'])
        if selected:
            marker(c, 36, y + 12, p['spot'])
        c.text(44, y + 8, f'{i + 1:03}', p['spot'] if selected else p['quiet'])
        c.text(78, y + 8, name, color)
        if i == 0:
            c.text(294, y + 8, 'open *', p['spot'], right=True)
    # Position within one list, not banks. Touch arrows share the L/R action.
    c.text(20, 185, 'l <', p['quiet'])
    c.text(121, 185, '001-006 / 128', p['quiet'])
    c.text(300, 185, '> r', p['ink'], right=True)
    action(c, 14, 76, 'open', p)
    action(c, 98, 86, 'save', p, accented=True)
    action(c, 192, 110, 'save copy', p)
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    board = Canvas(852, 554, bg=OUTSIDE)
    for x, theme in ((18, 'pink'), (434, 'graphite')):
        board.text(x, 12, theme + ' / save library / lines', (196, 190, 204))
        board.im.paste(top(theme), (x, 30))
        board.im.paste(bottom(theme), (x + 40, 282))
    board.text(18, 536, 'static proposal / native 400x240 + 320x240 / illustrative names and dates', (157, 150, 166))
    path = OUT / 'save-library-lines-2x.png'
    board.im.resize((1704, 1108), Image.Resampling.NEAREST).save(path)
    print(path)


if __name__ == '__main__':
    main()
