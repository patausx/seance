#!/usr/bin/env python3
"""Requested two-state static composition, not a running UI or device capture.

Native Pink gradient/6x8/baked headings. Bottom scope is an unchanged crop of
an existing host capture; filter diagram and lock values are illustrative.
Revision 02 removes per-module left icons and proposes held-L editing tools.
Only the new v2 board is exported; the previous PNG is preserved. No runtime changes.
"""
from functools import lru_cache
import math
import re

from PIL import Image
from render_module_geometry_study import Canvas
from render_play_editor_study import ROOT, INK, SPOT, CELL, QUIET, RAIL, paper, blend
from render_step_locks_study import cut_corner, marker, dual

OUT = ROOT / 'ui_mockups/step_locks_chain_study'
SCOPE = ROOT / 'ui_mockups/graphite_accent_runtime/ui/pink/sequence/01_live_scope.ppm'
OUTSIDE = (24, 20, 29)
CAPTION = (226, 190, 206)
ROWS = (45, 83, 121, 159)
SELECTED = {4, 5, 6, 7}
FOCUS = 5
# On ch02: pitch+decay on all four; cutoff on 05/06/08; fract mix on 05.
LOCKS = (
    (1, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0),
    (2, 0, 2, 0, 4, 3, 2, 3, 2, 0, 3, 0, 2, 0, 2, 0),
    (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0),
    (1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0),
)


@lru_cache(maxsize=None)
def heading_mask(name):
    source = (ROOT / 'platform/3ds/menu_labels_data.h').read_text()
    offset, width, height = map(int, re.search(
        r'\{"' + re.escape(name) + r'", (\d+), (\d+), (\d+)\}', source).groups())
    data = bytes(int(v, 16) for v in re.findall(
        r'0x([0-9a-f]{2})', source.split('bitmap[] = {', 1)[1].split('};', 1)[0]))
    mask = Image.new('L', (width, height))
    stride = (width + 7) // 8
    for y in range(height):
        for x in range(width):
            if data[offset + y * stride + x // 8] & (0x80 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


def heading(c, name, x, y, color=INK):
    mask = heading_mask(name)
    c.im.paste(color, (x, y), mask)
    c.labels.append(((x, y, x + mask.width, y + mask.height), name))


def outline(c, x, y, w, h, color=SPOT):
    c.d.line(((x + 2, y), (x + w - 3, y), (x + w - 1, y + 2),
              (x + w - 1, y + h - 3), (x + w - 3, y + h - 1),
              (x + 2, y + h - 1), (x, y + h - 3), (x, y + 2),
              (x + 2, y)), fill=color)


def chain(c):
    entries = (
        ('osc', 'pitch g2', '4/4'),
        ('filter', 'cutoff / mixed', '3/4'),
        ('vca', 'decay 160ms', '4/4'),
        ('fract', 'mix 35%', '1/4'),
        ('delay', 'base', '--'),
        ('rev', 'base', '--'),
    )
    c.d.line((174, 55, 174, 211), fill=RAIL)
    for i, (name, detail, count) in enumerate(entries):
        y = 57 + i * 26
        chosen = i == 1
        col = SPOT if chosen else INK if count != '--' else QUIET
        if chosen:
            cut_corner(c, 8, y - 3, 159, 26, blend(CELL, SPOT, 5))
            marker(c, 9, y + 1)
        c.text(22, y, f'{i + 1:02} {name}', col)
        c.text(164, y, count, col, right=True)
        c.text(40, y + 12, detail, SPOT if chosen else QUIET)


def frequency_x(hz):
    return 186 + round(math.log(hz / 20) / math.log(1000) * 200)


def response(c, hz, color, dashed=False, width=1):
    """Explanatory low-pass contour, not the shipping filter transfer function."""
    points = []
    for x in range(186, 388):
        f = 20 * 1000 ** ((x - 186) / 200)
        db = -10 * math.log10(1 + (f / hz) ** 4)
        bump = 5 * math.exp(-((math.log(f / hz) + .2) / .3) ** 2)
        y = round(max(102, min(143, 110 - (db + bump) * .8)))
        points.append((x, y))
    if dashed:
        for i in range(0, len(points) - 1, 5):
            c.d.line(points[i:i + 3], fill=color)
    else:
        c.d.line(points, fill=color, width=width)


def filter_detail(c):
    heading(c, 'filter', 186, 55)
    c.text(388, 61, '02 / cutoff', SPOT, right=True)
    c.text(186, 84, '1.80-4.20 khz', SPOT)
    c.text(388, 84, 'mixed', QUIET, right=True)
    # Gray base, thin range edges, stronger focused step 06. No live-value claim.
    xa, xb = frequency_x(1800), frequency_x(4200)
    for x in range(xa, xb + 1, 4):
        c.d.line((x, 102, x, 143), fill=blend(RAIL, CELL, 45))
    c.d.line((186, 144, 387, 144), fill=RAIL)
    response(c, 800, QUIET, dashed=True)
    response(c, 1800, blend(SPOT, CELL, 45))
    response(c, 4200, blend(SPOT, CELL, 45))
    response(c, 2600, SPOT)
    xx = frequency_x(2600)
    c.d.ellipse((xx - 2, 110, xx + 2, 114), fill=SPOT)
    c.text(186, 149, '20hz', QUIET)
    c.text(388, 149, '20khz', QUIET, right=True)
    c.text(186, 166, 'locked', INK)
    c.text(388, 166, '3/4 steps', SPOT, right=True)
    c.text(186, 180, 'base', QUIET)
    c.text(388, 180, '800 hz', QUIET, right=True)
    c.d.line((186, 194, 387, 194), fill=RAIL)
    cut_corner(c, 184, 199, 33, 14, SPOT)
    c.text(192, 202, 'set', CELL)
    c.text(230, 202, 'offset', QUIET)
    c.text(388, 202, '06:2.6k', INK, right=True)


def l_help(c):
    c.text(186, 59, 'l / edit tools', SPOT)
    c.text(186, 76, 'steps 05-08 / 4', QUIET)
    actions = (('x', 'copy'), ('a', 'paste'), ('y', 'undo'),
               ('b', 'clear selection'), ('< >', 'extend range'), ('^ v', 'value'))
    for i, (key, action) in enumerate(actions):
        y = 93 + i * 17
        outline(c, 186, y - 3, 30, 14, RAIL)
        c.text(192 if len(key) == 3 else 198, y, key, SPOT)
        c.text(228, y, action, INK)
    c.d.line((186, 189, 387, 189), fill=RAIL)
    c.text(186, 197, 'l + tap / drag: select', INK)
    c.text(186, 210, 'clip: 4 whole steps', SPOT)


def top(held=False):
    c = paper(400)
    heading(c, 'sequencer', 12, 9)
    c.text(112, 14, 'locks / ch02', SPOT)
    c.text(388, 14, 'edit a / stop', QUIET, right=True)
    c.text(12, 36, 'steps 05-08 / 4 selected', SPOT)
    c.text(388, 36, 'base unchanged', QUIET, right=True)
    c.d.line((12, 49, 387, 49), fill=RAIL)
    chain(c)
    l_help(c) if held else filter_detail(c)
    c.d.line((12, 221, 387, 221), fill=RAIL)
    c.text(12, 229, 'copy keeps timing + all locks' if held else '4 steps / 2-4 locks per step', INK)
    c.text(388, 229, 'release l' if held else 'hold l: edit', SPOT, right=True)
    return c.im


def bottom(held=False):
    c = paper(320)
    # Exact pre-existing scope pixels at their original coordinates, not redrawn.
    with Image.open(SCOPE) as source:
        c.im.paste(source.crop((0, 0, 320, 40)), (0, 0))
    for row, y in enumerate(ROWS):
        for step in range(16):
            x = 8 + step * 19
            count = LOCKS[row][step]
            selected = row == 1 and step in SELECTED
            focused = row == 1 and step == FOCUS
            if count:
                cut_corner(c, x, y, 17, 17, SPOT if focused else CELL)
                c.text(x + 5, y + 5, str(count), CELL if focused else INK)
                for i in range(4):
                    c.d.rectangle((x + 2 + i * 3, y + 21, x + 3 + i * 3, y + 25),
                                  fill=(SPOT if selected else INK) if i < count else RAIL)
            else:
                c.d.rectangle((x + 7, y + 7, x + 9, y + 9), fill=QUIET)
            if selected:
                outline(c, x, y, 17, 17)
            if focused:
                c.d.line((x + 4, y - 2, x + 12, y - 2), fill=SPOT)
    # Selection is a separate small bracket, not a new score layout or playhead.
    left, right = 8 + 4 * 19, 8 + 7 * 19 + 16
    c.d.line((left, 113, left, 116, right, 116, right, 113), fill=SPOT)
    c.text(8, 73, '02', SPOT)
    c.text(311, 73, '05-08 / selected', SPOT, right=True)
    if held:
        c.text(8, 199, 'l held / 4 steps', SPOT)
        c.text(311, 199, 'whole steps', INK, right=True)
        for x, w, label in ((8, 72, 'x copy'), (86, 72, 'a paste'),
                            (164, 72, 'y undo'), (242, 70, 'b clear')):
            outline(c, x, 212, w, 17, RAIL)
            c.text(x + (w - len(label) * 6) // 2, 217, label, INK)
        c.text(8, 232, 'l + touch: select / drag range', QUIET)
    else:
        c.text(8, 199, 'lock / 4 steps', SPOT)
        c.text(311, 199, 'cutoff / mixed', INK, right=True)
        c.text(8, 215, '02 filter >', INK)
        c.d.line((160, 218, 315, 218), fill=RAIL)
        def px(hz):
            return 160 + round(math.log(hz / 20) / math.log(1000) * 155)
        c.d.rectangle((px(1800), 216, px(4200), 220), fill=blend(SPOT, CELL, 60))
        c.d.line((px(800), 214, px(800), 222), fill=QUIET)
        c.d.rectangle((px(2600) - 2, 216, px(2600) + 2, 220), fill=SPOT)
        c.text(8, 232, 'l edit / select', SPOT)
        c.text(132, 232, 'a param', QUIET)
        c.text(311, 232, 'r mode', QUIET, right=True)
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    board = Canvas(824, 532, bg=OUTSIDE)
    board.text(0, 1, '01 / edit selected steps', CAPTION)
    board.text(424, 1, '02 / hold l: edit + clipboard', CAPTION)
    for index, held in enumerate((False, True)):
        board.im.paste(dual(top(held), bottom(held)), (index * 424, 20))
    board.text(0, 519, 'seance / static proposal / native fonts + pink / diagrams are schematic', CAPTION)
    path = OUT / 'locks_chain_l_tools_v2_2x.png'
    board.im.resize((1648, 1064), Image.Resampling.NEAREST).save(path)
    print(path)


if __name__ == '__main__':
    main()
