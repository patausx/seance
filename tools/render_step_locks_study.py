#!/usr/bin/env python3
"""Two requested static step-lock proposals, NOT runtime/controller captures.

Native 400x240 / 320x240 canvases; existing Pink paper, 6x8 glyphs, baked
sequencer heading and note digits. Illustrative stored values, not live audio.
Only this study's three PNGs are written. No sample/runtime/release changes.
"""
from functools import lru_cache
from pathlib import Path
import math
import re

from PIL import Image
from render_module_geometry_study import Canvas
from render_play_editor_study import ROOT, INK, SPOT, CELL, QUIET, RAIL, paper
from render_play_editor_final import digit

OUT = ROOT / 'ui_mockups/step_locks_study'
OUTSIDE = (24, 20, 29)
CAPTION = (226, 190, 206)
SELECTED = 4  # human step 05; chain02 is osc -> filter -> vca
BASE_HZ = 800
CUTOFF_LOCKS = {2: 1800, 4: 4200, 7: 2600, 10: 1200, 14: 6000}
TRIGGERS = (
    {0, 4, 8, 12},
    {0, 2, 4, 7, 8, 10, 12, 14},
    {4, 12},
    {0, 2, 4, 6, 8, 10, 12, 14},
)


@lru_cache(maxsize=1)
def title_mask():
    source = (ROOT / 'platform/3ds/menu_labels_data.h').read_text()
    off, width, height = map(int, re.search(
        r'\{"sequencer", (\d+), (\d+), (\d+)\}', source).groups())
    data = bytes(int(v, 16) for v in re.findall(
        r'0x([0-9a-f]{2})', source.split('bitmap[] = {', 1)[1].split('};', 1)[0]))
    mask = Image.new('L', (width, height))
    stride = (width + 7) // 8
    for y in range(height):
        for x in range(width):
            if data[off + y * stride + x // 8] & (0x80 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


def title(c, x, y):
    mask = title_mask()
    c.labels.append(((x, y, x + mask.width, y + mask.height), 'native sequencer heading'))
    c.im.paste(INK, (x, y), mask)


def cut_corner(c, x, y, w, h, color):
    c.d.polygon(((x + 2, y), (x + w - 3, y), (x + w - 1, y + 2),
                 (x + w - 1, y + h - 3), (x + w - 3, y + h - 1),
                 (x + 2, y + h - 1), (x, y + h - 3), (x, y + 2)), fill=color)


def marker(c, x, y, color=SPOT):
    c.d.polygon(((x, y), (x + 3, y + 3), (x, y + 6)), fill=color)


def cutoff_position(hz):
    # Explanatory logarithmic frequency scale, not an Engine/DSP prediction.
    return math.log(hz / 20) / math.log(20000 / 20)


def top():
    c = paper(400)
    title(c, 20, 13)
    c.text(380, 21, 'bank a / ch02 / step05', QUIET, right=True)
    c.text(20, 46, '02 filter / cutoff', INK)
    c.text(380, 46, 'stopped / page 1:4', QUIET, right=True)
    c.d.line((20, 61, 379, 61), fill=RAIL)

    c.text(20, 73, 'step lock', SPOT)
    x = 20
    x += digit(c, '4', x, 137) + 5
    c.d.rectangle((x, 132, x + 3, 136), fill=INK)
    x += 11
    x += digit(c, '2', x, 137)
    c.text(x + 10, 126, 'khz', INK)
    c.text(20, 152, 'base 800 hz', QUIET)
    c.text(20, 170, 'step 06 -> base', QUIET)
    c.text(20, 192, 'trigger on / always', INK)

    c.d.line((200, 73, 200, 201), fill=RAIL)
    c.text(218, 73, 'written on step 05', QUIET)
    entries = ((96, '01 osc / pitch', 'g2', False),
               (130, '02 filter / cutoff', '4.20 khz', True),
               (164, '03 vca / decay', '160 ms', False))
    for y, label, value, selected in entries:
        if selected:
            marker(c, 209, y)
        c.text(218, y, label, SPOT if selected else INK)
        c.text(380, y + 12, value, SPOT if selected else QUIET, right=True)
    c.text(218, 193, '1 free lock slot', QUIET)
    c.d.line((20, 213, 379, 213), fill=RAIL)
    c.text(20, 225, 'one step / base unchanged', SPOT)
    c.text(380, 225, 'locks 3/4', QUIET, right=True)
    return c.im


def lower_header(c, chooser=False):
    title(c, 8, 7)
    c.text(110, 10, 'choose lock target' if chooser else '02 filter / cutoff >', INK)
    c.text(110, 27, 'a / ch02 / step05', QUIET)
    if chooser:
        c.text(311, 10, 'back', SPOT, right=True)
    else:
        c.text(311, 27, 'lock 4.2k', SPOT, right=True)
    c.d.line((8, 40, 311, 40), fill=RAIL)


def score(c):
    # Keep the current 4x16 score pitch and row positions. Only the selected
    # chain becomes a stored-cutoff lane; other chains retain their trigs.
    for chain, y in enumerate((45, 83, 121, 159)):
        for step in range(16):
            x = 8 + step * 19
            on = step in TRIGGERS[chain]
            selected = chain == 1 and step == SELECTED
            if chain == 1:
                written = step in CUTOFF_LOCKS
                hz = CUTOFF_LOCKS.get(step, BASE_HZ)
                if selected:
                    cut_corner(c, x, y - 1, 18, 32, SPOT)
                color = CELL if selected else SPOT if written else QUIET
                c.text(x + 3, y + 1, f'{step + 1:02}', color)
                height = max(1, round(cutoff_position(hz) * 13))
                bottom = y + 24
                c.d.line((x + 3, bottom, x + 14, bottom), fill=CELL if selected else RAIL)
                if written:
                    c.d.rectangle((x + 5, bottom - height, x + 12, bottom), fill=color)
                else:
                    # A thin cap, rather than a filled bar, means inherited base.
                    c.d.line((x + 5, bottom - height, x + 12, bottom - height), fill=QUIET)
                if on:
                    c.d.rectangle((x + 8, y + 27, x + 10, y + 28), fill=color)
            else:
                if on:
                    cut_corner(c, x, y, 18, 18, INK)
                else:
                    c.d.line((x + 3, y + 17, x + 14, y + 17), fill=RAIL)
                c.text(x + 3, y + 5, f'{step + 1:02}', CELL if on else QUIET)
        if chain != 1:
            labels = {0: '01 / drums', 2: '03 / snare', 3: '04 / hats'}
            c.text(8, y + 22, labels[chain], QUIET)
            if chain == 0:
                c.text(311, y + 22, '02 below: cutoff', SPOT, right=True)
            elif chain == 3:
                c.text(311, y + 22, 'bar lock / line base', QUIET, right=True)


def editor():
    c = paper(320)
    lower_header(c)
    score(c)
    c.d.line((8, 194, 311, 194), fill=RAIL)
    c.text(8, 198, 'cutoff', INK)
    c.text(311, 198, '4.20 khz / lock', SPOT, right=True)
    c.d.line((16, 214, 303, 214), fill=RAIL)
    base_x = 16 + round(cutoff_position(BASE_HZ) * 287)
    current_x = 16 + round(cutoff_position(CUTOFF_LOCKS[SELECTED]) * 287)
    c.d.line((base_x, 210, base_x, 218), fill=QUIET)
    c.d.line((16, 214, current_x, 214), fill=SPOT)
    c.d.ellipse((current_x - 3, 211, current_x + 3, 217), fill=SPOT)
    marker(c, 8, 230)
    c.text(17, 229, 'hear', SPOT)
    c.text(62, 229, 'remove', INK)
    c.text(128, 229, 'copy', INK)
    c.text(184, 229, 'undo', QUIET)
    c.text(311, 229, 'r mode', QUIET, right=True)
    return c.im


def chooser():
    c = paper(320)
    lower_header(c, chooser=True)
    c.text(12, 54, 'module', QUIET)
    c.text(134, 54, 'parameter', QUIET)
    c.text(311, 54, 'value', QUIET, right=True)
    c.d.line((118, 53, 118, 201), fill=RAIL)
    for y, label, selected in ((81, '01 osc', False), (113, '02 filter', True), (145, '03 vca', False)):
        if selected:
            marker(c, 5, y)
            c.d.line((12, y + 12, 93, y + 12), fill=SPOT)
        c.text(12, y, label, SPOT if selected else INK)
    entries = ((81, 'cutoff', 'lock 4.2k', True),
               (113, 'resonance', 'base 13%', False),
               (145, 'drive', 'base 0%', False),
               (177, 'morph', 'base 0%', False))
    for y, label, value, selected in entries:
        if selected:
            marker(c, 126, y)
        c.text(134, y, label, SPOT if selected else INK)
        c.text(311, y, value, SPOT if selected else QUIET, right=True)
    c.d.line((8, 213, 311, 213), fill=RAIL)
    c.text(12, 225, 'tap parameter -> steps', SPOT)
    c.text(311, 225, 'b cancel', QUIET, right=True)
    return c.im


def dual(upper, lower):
    assert upper.size == (400, 240) and lower.size == (320, 240)
    image = Image.new('RGB', (400, 488), OUTSIDE)
    image.paste(upper, (0, 0))
    image.paste(lower, (40, 248))
    return image


def main():
    upper = top()
    states = [('edit', editor()), ('choose', chooser())]
    OUT.mkdir(parents=True, exist_ok=True)
    board = Canvas(824, 524, bg=OUTSIDE)
    board.text(0, 1, '01 / edit one step', CAPTION)
    board.text(424, 1, '02 / choose module + parameter', CAPTION)
    for index, (name, lower) in enumerate(states):
        image = dual(upper, lower)
        image.resize((800, 976), Image.Resampling.NEAREST).save(OUT / f'locks_{name}_2x.png')
        board.im.paste(image, (index * 424, 20))
    board.text(0, 516, 'static proposal / native fonts + pink / not running ui', CAPTION)
    board.im.resize((1648, 1048), Image.Resampling.NEAREST).save(OUT / 'locks_workflow_2x.png')
    print('two static states; native sizes and text bounds/non-overlap checked by Canvas')
    print(OUT / 'locks_workflow_2x.png')


if __name__ == '__main__':
    main()
