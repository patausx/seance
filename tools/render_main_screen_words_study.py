#!/usr/bin/env python3
"""Two lower-screen concepts, one static comparison sheet; NO runtime changes.
Native 320x240, project Gossip28/6x8, nearest 2x presentation.
The user's reference is preserved alongside the render, not a runtime dependency.
Font license: assets/fonts/OFL.txt. No new recorder module or input API implied.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_universal_sigils_study import Surface, caption, OUTSIDE
from render_menu_style_study import heading_mask
from render_main_screen_focus_study import reference_blur

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_words_study'
# Same four modules in both studies: compare spatial language, not engine changes.
LAYOUTS = {
    'horizontal': {'gran': (32, 92), 'ring': (103, 89),
                   'chor': (175, 85), 'dly': (246, 88)},
    'free': {'gran': (27, 123), 'ring': (121, 85),
             'chor': (241, 131), 'dly': (204, 185)},
}


def arrow(c, x, y, direction='right'):
    if direction == 'right':
        c.line((x - 3, y - 2, x, y, x - 3, y + 2))
    elif direction == 'left':
        c.line((x + 3, y - 2, x, y, x + 3, y + 2))


def bezier(c, points):
    result = []
    for i in range(81):
        t, u = i / 80, 1 - i / 80
        result.append(tuple(round(u**3 * points[0][k] + 3*u*u*t*points[1][k]
                                  + 3*u*t*t*points[2][k] + t**3*points[3][k])
                            for k in (0, 1)))
    c.d.line(result, fill=c.p['rail'])


def chrome(c):
    c.heading(16, 12, 'patch table')
    c.txt(304, 25, 'patch02 *', 'ghost', right=True)
    c.txt(20, 54, 'chain', 'ghost')
    for i, x in enumerate((98, 146, 194, 242), 1):
        c.txt(x, 54, str(i), 'spot' if i == 4 else 'ghost')
        if i == 4:
            c.line((x - 2, 67, x + 8, 67), 'spot')
    c.txt(20, 230, '4 / 6 modules', 'ghost')
    c.txt(304, 230, '+ add', 'ghost', right=True)


def word(c, name, point):
    x, y = point
    selected = name == 'ring'
    c.heading(x, y, name, 'spot' if selected else 'ink')
    if selected:
        w, h = heading_mask(name).size
        c.line((x, y + h + 5, x + w - 1, y + h + 5), 'spot')


def closed(kind):
    c = Surface(320, 'graphite')
    chrome(c)
    if kind == 'horizontal':
        c.line((18, 102, 25, 102))
        for left, right in ((79, 96), (147, 168), (220, 239)):
            c.line((left, 102, right, 102))
        c.line((279, 102, 289, 102))
        arrow(c, 287, 102)
        c.txt(294, 98, '+', 'ghost')
        # Deliberately no six-slot placeholders, individual cards or graph grid.
    else:
        # Free positions, simple individual curves clear of all four word masks.
        bezier(c, ((74, 133), (94, 133), (96, 96), (113, 96)))
        arrow(c, 110, 96)
        bezier(c, ((166, 96), (201, 96), (208, 142), (233, 142)))
        arrow(c, 230, 142)
        bezier(c, ((287, 142), (309, 142), (301, 196), (237, 196)))
        arrow(c, 240, 196, 'left')
        bezier(c, ((196, 196), (149, 196), (101, 181), (55, 181)))
        arrow(c, 58, 181, 'left')
        c.txt(27, 177, 'out', 'ghost')
    for name, point in LAYOUTS[kind].items():
        word(c, name, point)
    return c.im


def opened(kind, base):
    c = Surface(320, 'graphite')
    # Shared approved blur recipe; approximation limitations in the v1 helper.
    c.im = reference_blur(base)
    c.d = ImageDraw.Draw(c.im)
    # Only the source word is redrawn sharp, at exactly its original coordinates.
    # Blur surrounding the accent yields a gentle halo; no new glow/shadow filter.
    x, y = LAYOUTS[kind]['ring']
    word(c, 'ring', (x, y))
    spine, text_x = x + 6, x + 23
    c.line((spine, y + heading_mask('ring').height + 9, spine, 222))
    for i, label in enumerate(('edit', 'bypass', 'copy', 'paste', 'delete', 'add')):
        row = 128 + 18*i
        selected = i == 0
        role = 'spot' if selected else 'ghost' if label == 'paste' else 'ink'
        c.line((spine, row + 4, text_x - 7, row + 4), 'spot' if selected else 'rail')
        c.txt(text_x, row, label, role)
        if selected:
            c.line((text_x, row + 12, text_x + len(label)*6 - 1, row + 12), 'spot')
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (688, 576), OUTSIDE)
    for i, (kind, title) in enumerate((('horizontal', '01 / horizontal words'),
                                      ('free', '02 / free word patches'))):
        x = 16 + i*336
        base = closed(kind)
        caption(sheet, x, 12, title)
        sheet.paste(base, (x, 32))
        caption(sheet, x, 284, 'same anchor / actions + blur')
        sheet.paste(opened(kind, base), (x, 300))
    caption(sheet, 16, 558, 'lower-screen studies / native 320x240 / not implemented')
    path = OUT / 'main-words-v2-2x.png'
    sheet.resize((sheet.width*2, sheet.height*2), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
