#!/usr/bin/env python3
"""Exactly two static unified go-to concepts; no runtime or animation.

Both use the same 400x496 source surface and a single identical dimming pass.
The native LCD crops remain 400x240 and centered 320x240 with a 16px hinge gap.
"""
from pathlib import Path
from PIL import Image
from render_go_to_overlay_study import backdrop, dim
from render_go_to_clock_study import digit_mask
from render_main_screen_centered_study import paper, canvas, caption, OUTSIDE
from render_menu_style_study import heading_mask

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/go_to_overlay_study'


def shared_backdrop():
    # Existing work stays visible across both LCDs, not clean clock paper above
    # a dimmed editor below. Apply the veil ONCE, before either foreground.
    top, bottom = backdrop()
    shared = paper()
    shared.paste(top, (0, 0))
    shared.paste(bottom, (40, 256))
    return dim(shared)


def number(c, text, origin, y, role):
    for index, value in enumerate(text):
        mask = digit_mask(value)
        assert mask.width <= 48 and mask.height <= 88
        x = origin + index * 52 + (48 - mask.width) // 2
        assert 0 <= x and x + mask.width <= 400 and y + mask.height <= 240
        c.im.paste(c.p[role], (x, y), mask)


def beat_marks(c, center, y):
    for index, offset in enumerate((-42, -14, 14, 42)):
        x = center + offset
        if index == 2:
            c.d.rectangle((x - 5, y - 5, x + 4, y + 4), fill=c.p['spot'])
        elif index < 2:
            c.d.rectangle((x - 3, y - 3, x + 2, y + 2), fill=c.p['ghost'])
        else:
            c.d.rectangle((x - 3, y - 3, x + 2, y + 2), outline=c.p['rail'])


def upper(image, kind):
    c = canvas(image)
    if kind == 'center':
        # The whole readout and the beat marks belong to the common center,
        # x200 above / x160 below. Shift towards the hinge, not a new top panel.
        c.txt(126 - 9, 79, 'bar', 'ghost')
        c.txt(300 - 12, 79, 'beat', 'ghost')
        number(c, '012', 50, 104, 'ink')
        number(c, '03', 250, 104, 'spot')
        colon = heading_mask(':')
        colon = colon.resize((colon.width * 4, colon.height * 4), Image.Resampling.NEAREST)
        c.im.paste(c.p['rail'], (218, 120), colon)
        beat_marks(c, 200, 218)
    else:
        # Each 160px lane continues down into one menu branch. No separator,
        # connector or frame. Readout centers x120/x280 map to lower x80/x240.
        number(c, '012', 44, 98, 'ink')
        number(c, '03', 230, 98, 'spot')
        c.txt(120 - 9, 197, 'bar', 'ghost')
        c.txt(280 - 12, 197, 'beat', 'ghost')
        beat_marks(c, 280, 224)
    return c.im


def lower(image, current='patch table'):
    c = canvas(image)
    # Navigation needs no title; keep only the two quick clock controls.
    c.txt(32, 17, '120 bpm', 'ink')
    c.txt(192, 17, 'swing 50%', 'ink')
    c.line((32, 30, 72, 30), 'rail')
    c.line((192, 30, 245, 30), 'rail')
    for x, names in ((32, ('patch table', 'sigils', 'sigilbay', 'sequencer')),
                     (192, ('perform', 'bounce', 'patches', 'exit'))):
        stem = x - 12
        c.line((stem, 69, stem, 195), 'rail')
        for row, name in enumerate(names):
            y = 58 + row * 42
            selected = name == 'sequencer'
            c.heading(x, y, name, 'spot' if selected else 'ink')
            c.line((stem, y + 11, x - 5, y + 11), 'spot' if selected else 'rail')
            if selected:
                c.line((x - 9, y + 8, x - 5, y + 11, x - 9, y + 14), 'spot')
                c.line((x, y + 25, x + 85, y + 25), 'spot')
            if name == current:
                c.txt(135, y + 10, 'here', 'ghost')
    c.txt(20, 226, 'a open', 'ghost')
    c.txt(104, 226, 'b / select close', 'ghost')
    c.txt(300, 226, 'y theme', 'ghost', right=True)
    return c.im


def main():
    shared = shared_backdrop()
    top_base = shared.crop((0, 0, 400, 240))
    bottom = lower(shared.crop((40, 256, 360, 496)))
    assert bottom.size == (320, 240)
    OUT.mkdir(parents=True, exist_ok=True)
    for kind, title, note in (
        ('center', '01 / one shared center', 'clock > beat marks > navigation'),
        ('columns', '02 / two continuous columns', 'bar above left / beat above right'),
    ):
        top = upper(top_base, kind)
        assert top.size == (400, 240)
        sheet = Image.new('RGB', (432, 590), OUTSIDE)
        caption(sheet, 16, 12, title)
        caption(sheet, 16, 29, 'same paper / same shade / both screens')
        sheet.paste(top, (16, 48))
        sheet.paste(bottom, (56, 304))
        caption(sheet, 16, 557, note)
        caption(sheet, 16, 575, 'static concept / example 4/4 / not runtime')
        path = OUT / f'go-to-unified-{kind}-2x.png'
        sheet.resize((864, 1180), Image.Resampling.NEAREST).save(path)
        print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
