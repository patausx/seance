#!/usr/bin/env python3
"""One still: musical counter above the existing transparent go-to menu.

Illustrative bar 012 / beat 03 in 4/4. No runtime, live clock or animation.
Preserves the prior overlay still; only this separate clock proposal is saved.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_go_to_overlay_study import backdrop, guide
from render_main_screen_centered_study import paper, canvas, caption, OUTSIDE
from render_menu_style_study import FACE, heading_mask

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/go_to_overlay_study'
SCALE = 4


def digit_mask(value):
    # Shared native baseline, fixed 48px slots: the narrow '1' must not shift
    # either the colon or the beat group. Only integer scaling of native ink.
    shared = FACE.getbbox('0123456789')
    box = FACE.getbbox(value)
    mask = Image.new('L', (box[2] - box[0] + 4, shared[3] - shared[1]))
    ImageDraw.Draw(mask).text((2 - box[0], -shared[1]), value, font=FACE, fill=255)
    mask = mask.point(lambda p: 255 if p >= 128 else 0)
    ink = mask.getbbox()
    assert ink
    mask = mask.crop((ink[0], 0, ink[2], mask.height))
    return mask.resize((mask.width * SCALE, mask.height * SCALE), Image.Resampling.NEAREST)


def clock_top():
    # A clean paper field, not another project/status panel. It does not change
    # with navigation focus in this proposal; the lower backdrop stays intact.
    c = canvas(paper().crop((0, 0, 400, 240)))
    c.txt(126 - 9, 57, 'bar', 'ghost')
    c.txt(300 - 12, 57, 'beat', 'ghost')
    for text, origin, role in (('012', 50, 'ink'), ('03', 250, 'spot')):
        for index, value in enumerate(text):
            mask = digit_mask(value)
            assert mask.width <= 48 and mask.height <= 88
            x = origin + index * 52 + (48 - mask.width) // 2
            c.im.paste(c.p[role], (x, 82), mask)
    colon = heading_mask(':')
    colon = colon.resize((colon.width * SCALE, colon.height * SCALE), Image.Resampling.NEAREST)
    c.im.paste(c.p['rail'], (218, 98), colon)

    # Four quiet beat marks; the third is active. Static example, not a claim
    # about transport timing, meter support or a measured device frame.
    for index, center in enumerate((158, 186, 214, 242)):
        if index == 2:
            c.d.rectangle((center - 5, 182, center + 4, 191), fill=c.p['spot'])
        elif index < 2:
            c.d.rectangle((center - 3, 184, center + 2, 189), fill=c.p['ghost'])
        else:
            c.d.rectangle((center - 3, 184, center + 2, 189), outline=c.p['rail'])
    return c.im


def main():
    _, bottom = backdrop()
    top, bottom = clock_top(), guide(bottom)
    assert top.size == (400, 240) and bottom.size == (320, 240)
    sheet = Image.new('RGB', (432, 590), OUTSIDE)
    caption(sheet, 16, 12, 'go to / musical clock')
    caption(sheet, 16, 29, 'one counter above / navigation below')
    sheet.paste(top, (16, 48))
    sheet.paste(bottom, (56, 304))
    caption(sheet, 16, 557, 'bar 012 / beat 03 / illustrative 4/4')
    caption(sheet, 16, 575, 'static concept / no animation / not runtime')
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'go-to-clock-2x.png'
    sheet.resize((864, 1180), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
