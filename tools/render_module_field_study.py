#!/usr/bin/env python3
"""Static design sketches, not runtime/device captures.

Uses the current 6x8 glyphs and baked Gossip-derived heading masks (OFL 1.1,
assets/fonts/OFL.txt). Only this script's output directory is written.
"""
from pathlib import Path
import argparse
import re
from PIL import Image, ImageDraw
from render_module_geometry_study import Canvas

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/module_field_study'
BG = (36, 36, 36)
BODY = (28, 28, 28)
INK = (244, 244, 244)
MUTED = (184, 184, 184)
QUIET = (112, 112, 112)
SPOT = (181, 154, 232)
INVERSE = (48, 48, 48)
W, H = 64, 26


def heading(c, name):
    source = (ROOT / 'platform/3ds/menu_labels_data.h').read_text()
    offset, width, height = map(int, re.search(
        r'\{"' + re.escape(name) + r'", (\d+), (\d+), (\d+)\}', source).groups())
    data = bytes(int(v, 16) for v in re.findall(
        r'0x([0-9a-f]{2})', source.split('bitmap[] = {', 1)[1].split('};', 1)[0]))
    stride = (width + 7) // 8
    mask = Image.new('L', (width, height))
    for y in range(height):
        for x in range(width):
            if data[offset + y * stride + x // 8] & (0x80 >> (x % 8)):
                mask.putpixel((x, y), 255)
    c.im.paste(INK, (20, 13 if name == 'patch table' else 16), mask)


def text(c, x, y, value, color=MUTED, right=False):
    c.text(x, y, value, color, right=right)


def body(c, x, y, width, height, shape, color):
    right, bottom = x + width - 1, y + height - 1
    if shape == 'capsule':
        c.d.rounded_rectangle((x, y, right, bottom), radius=height // 2, fill=color)
    else:
        cut = 2
        c.d.polygon(((x + cut, y), (right - cut, y), (right, y + cut),
                     (right, bottom - cut), (right - cut, bottom),
                     (x + cut, bottom), (x, bottom - cut), (x, y + cut)), fill=color)


def node(c, name, x, y, shape, selected=False, bypass=False):
    body(c, x, y, W, H, shape, SPOT if selected else BODY)
    color = INVERSE if selected else QUIET if bypass else INK
    text(c, x + (W - len(name) * 6) // 2, y + (H - 8) // 2, name, color)
    for sx in (x, x + W - 1):
        sy = y + H // 2
        c.d.ellipse((sx - 2, sy - 2, sx + 2, sy + 2), fill=BG,
                    outline=SPOT if selected else QUIET if bypass else MUTED)
    if bypass:
        text(c, x + (W - 18) // 2, y - 12, 'byp', QUIET)


def wire(c, points):
    # Concept-only orthogonal routing with small rounded bends, not an engine route.
    result = [points[0]]
    for i in range(1, len(points) - 1):
        a, b, d = points[i - 1], points[i], points[i + 1]
        la = abs(a[0] - b[0]) + abs(a[1] - b[1])
        ld = abs(d[0] - b[0]) + abs(d[1] - b[1])
        radius = min(6, la / 2, ld / 2)
        p = tuple(b[k] + (a[k] - b[k]) * radius / la for k in (0, 1))
        q = tuple(b[k] + (d[k] - b[k]) * radius / ld for k in (0, 1))
        result.append(tuple(round(v) for v in p))
        for j in range(1, 9):
            t = j / 8
            result.append(tuple(round((1 - t)**2 * p[k] + 2 * (1 - t) * t * b[k] + t*t*q[k])
                                for k in (0, 1)))
    result.append(points[-1])
    c.d.line(result, fill=MUTED)


def workspace(shape):
    c = Canvas(320, bg=BG)
    heading(c, 'patch table')
    text(c, 300, 24, 'patch01 / saved', right=True)
    text(c, 20, 49, 'chain')
    for i in range(4):
        x = 96 + 48 * i
        text(c, x, 49, str(i + 1), SPOT if i == 0 else QUIET)
        if i == 0:
            c.d.line((x - 2, 61, x + 7, 61), fill=SPOT)
    wire(c, [(87, 91), (110, 91), (110, 112), (132, 112)])
    wire(c, [(195, 112), (211, 112), (211, 85), (228, 85)])
    wire(c, [(291, 85), (304, 85), (304, 140), (52, 140), (52, 170), (68, 170)])
    wire(c, [(131, 170), (169, 170), (169, 184), (211, 184)])
    wire(c, [(274, 184), (296, 184)])
    c.d.line((293, 181, 296, 184, 293, 187), fill=MUTED)
    node(c, 'osc', 24, 78, shape)
    node(c, 'filt', 132, 99, shape, selected=True)
    node(c, 'vca', 228, 72, shape, bypass=True)
    node(c, 'dly', 68, 157, shape)
    node(c, 'rev', 211, 171, shape)
    text(c, 20, 208, '+ add', SPOT)
    text(c, 300, 208, '5 / 6 modules', right=True)
    text(c, 20, 228, 'a trig / y sigil')
    text(c, 300, 228, 'tap / hold node', right=True)
    return c.im


def picker():
    c = Canvas(320, bg=BG)
    heading(c, 'add module')
    text(c, 300, 24, 'chain 1', right=True)
    # Proposed fixed two-page order. Page two is not rendered as an extra gallery.
    pages = (('osc', 'noise', 'fm', 'motor', 'play', 'gran', 'mic', 'bird', 'rec', 'filt', 'vca', 'fold'),
             ('bit', 'ring', 'tilt', 'strain', 'dly', 'chor', 'strn', 'rev', 'ghost', 'shift'))
    assert len(set(sum(pages, ()))) == 22
    for i, name in enumerate(pages[0]):
        x, y = 20 + (i % 3) * 96, 55 + (i // 3) * 34
        selected = name == 'filt'
        body(c, x, y, 88, 28, 'flat', SPOT if selected else BODY)
        text(c, x + (88 - len(name) * 6) // 2, y + 10, name, INVERSE if selected else INK)
    text(c, 20, 197, 'filt / filter')
    text(c, 230, 197, '<', QUIET)
    text(c, 246, 197, '1', SPOT)
    text(c, 266, 197, '2', MUTED)
    text(c, 290, 197, '>', MUTED)
    c.d.line((244, 209, 253, 209), fill=SPOT)
    text(c, 20, 228, 'tap / a add')
    text(c, 300, 228, 'b close', right=True)
    return c.im


def picker_columns():
    """All 22 modules, original branch language; one shared description below.

    Fourteen-pixel row pitch is a compact stylus-first proposal, not a validated
    touch target. The original module ordering is read column-first, then right.
    """
    c = Canvas(320, bg=BG)
    heading(c, 'add module')
    text(c, 300, 24, 'ch01 / 06', right=True)
    source = (ROOT / 'platform/3ds/ui_picker.cpp').read_text()
    names_source = (ROOT / 'platform/3ds/menu_style.cpp').read_text()
    kinds = re.findall(r'UnitKind::(\w+)', source.split('PICKER_UNITS[] = {', 1)[1].split('};', 1)[0])
    names = dict(re.findall(r'case UnitKind::(\w+):\s*return "([^"]+)";',
                            names_source.split('const char* module_name', 1)[1].split('const char* module_description', 1)[0]))
    assert len(kinds) == 22 and len(set(kinds)) == 22
    for column in range(2):
        rail_x = 36 + column * 144
        c.d.line((rail_x, 55, rail_x, 197), fill=INK)
        for row, kind in enumerate(kinds[column * 11:(column + 1) * 11]):
            y = 53 + row * 14
            selected = kind == 'Filter'
            color = SPOT if selected else INK
            c.d.line((rail_x, y + 4, rail_x + 16, y + 4), fill=color)
            if selected:
                for dy in range(-3, 4):
                    c.d.point((rail_x + 16 - abs(dy), y + 4 + dy), fill=SPOT)
            text(c, rail_x + 25, y, names[kind], color)
    text(c, 20, 214, 'filt', SPOT)
    text(c, 56, 214, '/ filter')
    text(c, 300, 214, '22 modules', right=True)
    text(c, 20, 228, 'tap / a add')
    text(c, 300, 228, 'b close', right=True)
    return c.im


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--columns', action='store_true', help='render only the two-column line picker')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    frames = ([('04_picker_columns', picker_columns())] if args.columns else
              [('01_flat', workspace('flat')), ('02_capsule', workspace('capsule')),
               ('03_picker', picker())])
    for name, image in frames:
        assert image.size == (320, 240)
        image.save(OUT / f'{name}_native.png')
        image.resize((640, 480), Image.Resampling.NEAREST).save(OUT / f'{name}_2x.png')
        print(OUT / f'{name}_2x.png')


if __name__ == '__main__':
    main()
