#!/usr/bin/env python3
"""One static startup composition study, NOT runtime or a console capture.

Native 400x240 / 320x240, graphite, Gossip28 + actual 6x8, nearest 2x.
Only this new PNG is written; imports do not run existing gallery generators.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_universal_sigils_study import Surface, caption, OUTSIDE
from render_main_screen_centered_study import paper

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/start_menu_compact_study'


def surface(image):
    c = Surface(image.width, 'graphite')
    c.im = image.copy()
    c.d = ImageDraw.Draw(c.im)
    return c


def upper(background):
    im = background.crop((0, 0, 400, 240))
    mask = Image.open(ROOT / 'assets/branding/gothic_wordmark_native_mask.png').convert('L')
    assert mask.size == im.size
    im.paste((244, 244, 244), (0, 0), mask)
    return im


def lower(background, expanded):
    c = surface(background.crop((40, 256, 360, 496)))
    c.heading(38, 20, 'new patch')
    c.txt(282, 33, 'start fresh', 'ghost', right=True)

    # Library keeps its anchor and remains a useful compact section when closed.
    c.heading(38, 76, 'library', 'spot')
    c.txt(282, 89, '3 saved', 'ghost', right=True)
    if expanded:
        c.line((108, 88, 111, 91, 114, 88), 'spot')
    else:
        c.line((109, 87, 112, 90, 109, 93), 'spot')

    # One persistent, independently actionable last-project row. Example data
    # follows real slot naming, not unsupported user-assigned patch titles.
    c.line((52, 110, 52, 121, 55, 124, 66, 124))
    c.txt(77, 120, 'patch 03')
    c.txt(282, 120, '12 min', 'ghost', right=True)
    c.txt(77, 133, 'last opened', 'ghost')
    if expanded:
        c.line((52, 121, 52, 177, 55, 180, 66, 180))
        c.line((52, 158, 66, 158))
        for y, name, age in ((154, 'patch 01', '2 hours'), (176, 'patch 06', 'yesterday')):
            c.txt(77, y, name)
            c.txt(282, y, age, 'ghost', right=True)
        c.txt(77, 192, 'all projects >', 'ghost')

    # Service action stays in the same place, not pushed by the accordion.
    c.heading(38, 212, 'settings', 'ghost')
    c.txt(282, 220, 'a collapse' if expanded else 'a expand', 'ghost', right=True)
    return c.im


def main():
    # Still approximation of the existing softer startup field. This is not a
    # capture of the C++ paper renderer; no change of background is proposed.
    bg = Image.blend(Image.new('RGB', (400, 496), (36, 36, 36)), paper(), .4)
    top = upper(bg)
    sheet = Image.new('RGB', (856, 596), OUTSIDE)
    caption(sheet, 16, 12, 'startup / compact library')
    caption(sheet, 16, 29, '01 / closed - last patch stays visible')
    caption(sheet, 440, 29, '02 / open - recent patches + full library')
    for x, expanded in ((16, False), (440, True)):
        sheet.paste(top, (x, 50))
        sheet.paste(lower(bg, expanded), (x + 40, 306))
    caption(sheet, 16, 565, 'same anchors / no panels / settings stays below')
    caption(sheet, 16, 581, 'layout concept / example slots and ages / not a console capture')
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'start-menu-compact-2x.png'
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(path)
    print(path)


if __name__ == '__main__':
    main()
