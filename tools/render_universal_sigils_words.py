#!/usr/bin/env python3
"""Render-only v2: compact word disclosures, not full-screen drawers.
No runtime changes, animation export or device/performance claims.
Gossip heading font license: assets/fonts/OFL.txt.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter
from render_universal_sigils_study import Surface, wave, caption, OUTSIDE, THEMES

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/universal_sigils_study'


def editor(theme):
    c = Surface(320, theme)
    c.heading(16, 11, 'sigils')
    # No scaled 6x8 display lettering: all emphasis uses native Gossip.
    c.heading(147, 14, 'a', 'spot')
    c.chevron(174, 24, 'spot')
    c.line((144, 36, 178, 36), 'spot')
    c.txt(304, 23, 'undo', 'ghost', right=True)
    c.txt(16, 47, 'shape', 'ghost')
    c.txt(62, 47, 'drawn', 'spot')
    c.chevron(107, 50, 'spot')
    c.txt(204, 47, 'mode', 'ghost')
    c.txt(246, 47, 'loop')
    c.chevron(300, 50)
    for x in (20, 160, 300):
        c.line((x, 93, x, 97))
    wave(c, 'drawn', (20, 64, 281, 57), cursor=.125)
    for x in (20, 90, 160, 230, 300):
        c.line((x, 122, x, 124))
    c.txt(16, 138, 'free', 'spot')
    c.txt(52, 138, 'sync', 'ghost')
    c.line((16, 151, 39, 151), 'spot')
    c.heading(96, 132, '2.00', 'spot')
    c.txt(161, 145, 'hz', 'ghost')
    c.txt(208, 138, 'depth', 'ghost')
    c.txt(304, 138, '+100%', right=True)
    c.line((208, 153, 300, 153))
    c.line((254, 150, 254, 156))
    c.dot(300, 153)
    c.txt(16, 168, 'phase', 'ghost')
    c.txt(58, 168, '45 deg', 'spot')
    c.line((110, 173, 300, 173))
    for i in range(9):
        x = 110 + round(190 * i / 8)
        c.line((x, 171, x, 175))
    c.line((134, 169, 134, 177), 'spot')
    c.txt(16, 190, 'bipolar')
    c.chevron(71, 193)
    c.txt(126, 190, 'restart', 'ghost')
    c.txt(186, 190, 'transport')
    c.chevron(300, 193)
    c.txt(16, 211, 'rate mod', 'ghost')
    c.txt(80, 211, 'none')
    c.chevron(112, 214)
    c.txt(152, 211, 'amount   +0%', 'ghost')
    c.txt(16, 231, 'reset')
    c.txt(101, 231, 'sigilbay 03', 'spot')
    c.txt(304, 231, 'b back', 'ghost', right=True)
    return c.im


def opened(theme, kind):
    underlying = editor(theme)
    c = Surface(320, theme)
    clean_paper = c.im.copy()
    # Illustrative host-only blur, not a proposed 3DS framebuffer algorithm.
    c.im = Image.blend(underlying.filter(ImageFilter.GaussianBlur(.85)), clean_paper, .12)
    c.d = ImageDraw.Draw(c.im)
    if kind == 'sigil':
        box = (130, 41, 242, 208)
        anchor = (138, 8, 183, 38)
        items = tuple('sigil ' + letter for letter in 'abcdefgh')
        text_x, first_y, row_h = 157, 55, 18
        branch_x = 143
    else:
        box = (49, 64, 170, 201)
        anchor = (57, 39, 114, 59)
        items = ('drawn', 'sine', 'triangle', 'saw', 'square', 'random')
        text_x, first_y, row_h = 76, 78, 20
        branch_x = 62
    # Selected word remains attached to its unfolding menu, not a new page.
    c.im.paste(underlying.crop(anchor), anchor[:2])
    c.im.paste(clean_paper.crop(box), box[:2])
    # Quiet square paper edge; no cards, grid, thumbnails, counters or handle.
    border = tuple(round(a * .5 + b * .5) for a, b in
                   zip(c.p['rail'], clean_paper.getpixel(box[:2])))
    c.d.rectangle((box[0], box[1], box[2] - 1, box[3] - 1), outline=border)
    c.line((branch_x, first_y + 4, branch_x, first_y + (len(items) - 1) * row_h + 4))
    for i, item in enumerate(items):
        y = first_y + i * row_h
        role = 'spot' if i == 0 else 'ink'
        c.line((branch_x, y + 4, text_x - 7, y + 4), role if i == 0 else 'rail')
        c.txt(text_x, y, item, role)
        if i == 0:
            c.line((text_x, y + 12, text_x + len(item) * 6 - 1, y + 12), 'spot')
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (1024, 554), OUTSIDE)
    for row, theme in enumerate(THEMES):
        y = 12 + row * 266
        for col, (name, image) in enumerate((
                ('editor', editor(theme)),
                ('sigil / words', opened(theme, 'sigil')),
                ('shape / words', opened(theme, 'shape')))):
            x = 16 + col * 336
            caption(sheet, x, y, theme + ' / ' + name)
            sheet.paste(image, (x, y + 16))
    caption(sheet, 16, 542, 'v2 / local word menus + soft background blur / static concept only')
    path = OUT / 'word-menus-v2-2x.png'
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
