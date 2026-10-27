#!/usr/bin/env python3
"""v8 static composition: one module column across two LCDs, not runtime.
Native Gossip28/6x8, illustrative reference curves, no hardware telemetry.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_universal_sigils_study import caption, OUTSIDE
from render_main_screen_tiles_study import surface, control
from render_main_screen_tiles_menus_study import columns, FULL
from render_main_screen_accordion_study import center_text, trace
from render_main_screen_focus_study import reference_blur

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_tiles_study'
ACTIVE = (41, 38, 46)


def bands(c, top):
    offset = 40 if top else 0
    for name, a, b in columns(FULL):
        a, b = a + offset, b + offset
        selected = name == 'ring'
        color = c.p['spot'] if selected else c.p['rail']
        y0, y1 = (12, 239) if top else (0, 197)
        if selected:
            c.d.rectangle((a + 2, y0, b - 3, y1), fill=ACTIVE)
        # Same edges, ink and paper across the hinge; no closed card at the seam.
        c.d.line((a + 2, y0, a + 2, y1), fill=color)
        c.d.line((b - 3, y0, b - 3, y1), fill=color)
        y = y0 if top else y1
        c.d.line((a + 2, y, b - 3, y), fill=color)


def small_graph(c, name, cx):
    # Minimal illustrative processing marks, NOT measured audio or meter values.
    if name == 'gran':
        for dx, h in ((-8, 3), (-4, 9), (0, 13), (4, 5), (8, 8)):
            c.line((cx + dx, 131 - h, cx + dx, 131 + h), 'ghost')
    elif name == 'chor':
        trace(c, cx - 10, 125, 21, 5, .7, role='ghost')
        trace(c, cx - 10, 137, 21, 5, .7, phase=.7, role='rail')
    elif name == 'dly':
        for dx, h in ((-8, 20), (-2, 13), (4, 8), (9, 4)):
            c.line((cx + dx, 143, cx + dx, 143 - h), 'ghost')
    elif name == 'filt':
        c.line((cx - 10, 123, cx - 2, 123, cx + 2, 127, cx + 9, 143), 'ghost')
    else:
        c.line((cx - 10, 143, cx - 5, 121, cx - 1, 132, cx + 5, 132, cx + 10, 143), 'ghost')


def upper():
    c = surface(400)
    bands(c, True)
    for name, a, b in columns(FULL):
        cx = 40 + (a + b) // 2
        if name != 'ring':
            small_graph(c, name, cx)
    # Two reference factors and their product: no repeated names or numbers.
    x, width, cx = 86, 132, 152
    trace(c, x, 57, width, 17, 1.5, role='ghost')
    c.line((cx - 3, 80, cx + 3, 86), 'spot')
    c.line((cx - 3, 86, cx + 3, 80), 'spot')
    trace(c, x, 108, width, 17, 5, role='spot')
    c.line((cx, 135, cx, 145), 'rail')
    c.line((cx - 3, 142, cx, 145, cx + 3, 142), 'rail')
    c.line((x, 182, x + width - 1, 182), 'rail')
    trace(c, x, 182, width, 31, 5, role='ink', carrier=True)
    return c.im


def title(c, menu=False):
    c.heading(46, 16, 'ring', 'spot')
    c.txt(166, 22, '...', 'spot' if menu else 'ghost')


def lower():
    c = surface()
    bands(c, False)
    for name, a, b in columns(FULL):
        if name != 'ring':
            center_text(c, (a + b) // 2, 26, name, 'ink')
    title(c)
    # Direct editor entry belongs to the tile, not to its context menu.
    c.txt(97, 23, 'edit', 'spot')
    c.line((97, 35, 120, 35), 'spot')
    # Standard power/bypass mark, distinct from the ellipsis.
    c.d.arc((137, 20, 149, 32), 305, 235, fill=c.p['spot'], width=1)
    c.line((143, 18, 143, 26), 'spot')
    control(c, 46, 74, 132, 'freq', '440hz', .43, mod=True, gap=24)
    control(c, 46, 143, 132, 'mix', '68%', .68, gap=24)
    # Only global navigation and patch status live outside the module columns.
    for i, x in enumerate((18, 54, 90, 126), 1):
        c.txt(x, 219, str(i), 'spot' if i == 4 else 'ghost')
        if i == 4:
            c.line((x - 5, 233, x + 10, 233), 'spot')
    c.txt(304, 219, 'patch02 *', 'ghost', right=True)
    return c.im


def actions(base):
    c = surface()
    c.im = reference_blur(base)
    c.d = ImageDraw.Draw(c.im)
    title(c, menu=True)
    c.line((175, 36, 202, 36, 202, 133), 'spot')
    for i, label in enumerate(('replace', 'copy', 'paste', 'delete')):
        y = 53 + i * 25
        role = 'spot' if i == 0 else 'ghost' if label == 'paste' else 'ink'
        c.line((202, y + 4, 212, y + 4), 'spot' if i == 0 else 'rail')
        c.txt(220, y, label, role)
        if i == 0:
            c.line((220, y + 12, 261, y + 12), 'spot')
    c.txt(16, 219, 'b close', 'ghost')
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    top, bottom = upper(), lower()
    sheet = Image.new('RGB', (864, 604), OUTSIDE)
    caption(sheet, 16, 12, 'seance / v8 / one column across both screens')
    for x, title_text, low in ((16, '01 / graphics above / controls below', bottom),
                               (448, '02 / only secondary actions in menu', actions(bottom))):
        caption(sheet, x, 38, title_text)
        sheet.paste(top, (x, 57))
        sheet.paste(low, (x + 40, 313))
    caption(sheet, 16, 569, 'edit + bypass on the tile / chain selection at bottom / no repeated labels')
    caption(sheet, 16, 587, 'native 400x240 + 320x240 / illustrative curves / static concept, not runtime')
    path = OUT / 'main-unified-v8-2x.png'
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
