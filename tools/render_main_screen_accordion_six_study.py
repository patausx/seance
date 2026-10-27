#!/usr/bin/env python3
"""Six-module static stress case for the v4 accordion concept, not runtime.

One sheet, two selections. Native 400x240 / 320x240, nearest 2x.
Uses project Gossip28 / 6x8 (assets/fonts/OFL.txt). Example data only.
"""
from pathlib import Path
from PIL import Image
from render_universal_sigils_study import Surface, caption, OUTSIDE
from render_menu_style_study import heading_mask
from render_main_screen_table_study import curve, port
from render_main_screen_accordion_study import center_text, contour, detail

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_accordion_study'
ORDER = ('gran', 'ring', 'chor', 'dly', 'filt', 'vca')
# Illustrative six-module placement, not a proposed automatic layout algorithm.
# Coordinates are identical in the two selection states.
NODES = {'gran': (34, 99), 'ring': (138, 84), 'chor': (248, 101),
         'dly': (63, 177), 'filt': (161, 164), 'vca': (264, 181)}
PORTS = {name: ((x - 12, y + 11), (x + heading_mask(name).width + 12, y + 11))
         for name, (x, y) in NODES.items()}
WIRES = (
    ((86, 110), (100, 110), (110, 95), (126, 95)),
    ((187, 95), (205, 95), (216, 112), (236, 112)),
    ((298, 112), (316, 112), (315, 150), (303, 150)),
    ((303, 150), (218, 154), (103, 137), (49, 156)),
    ((49, 156), (32, 164), (28, 188), (51, 188)),
    ((101, 188), (120, 188), (130, 175), (149, 175)),
    ((201, 175), (219, 175), (233, 192), (252, 192)),
    ((302, 192), (318, 192), (316, 208), (302, 208)),
)


def upper(selected):
    c = Surface(400, 'graphite')
    c.heading(16, 12, 'chain 4')
    c.txt(384, 23, 'patch02 *', 'ghost', right=True)
    left = 16
    for i, name in enumerate(ORDER):
        chosen = name == selected
        width = 168 if chosen else 40
        right = left + width
        cx = (left + right) // 2
        center_text(c, cx, 51, f'{i + 1:02}', 'spot' if chosen else 'ghost')
        contour(c, left, right, chosen)
        if chosen:
            c.heading(cx - heading_mask(name).width // 2, 78, name, 'spot')
            detail(c, name, left, right)
        else:
            # Fixed native 6x8 text, not a downscaled/deformed heading.
            assert len(name) * 6 <= width - 12
            center_text(c, cx, 130, name, 'ink')
        left = right
    assert left == 384  # 16 + 168 + 5*40, with another 16px margin.
    c.line((16, 218, 384, 218))
    c.txt(16, 228, '06 / 06 modules', 'ghost')
    c.txt(384, 228, 'serial > out', 'ghost', right=True)
    return c.im


def lower(selected):
    c = Surface(320, 'graphite')
    c.heading(16, 10, 'patch table')
    c.txt(304, 23, 'patch02 *', 'ghost', right=True)
    c.txt(20, 51, 'chain', 'ghost')
    for i, x in enumerate((98, 146, 194, 242), 1):
        c.txt(x, 51, str(i), 'spot' if i == 4 else 'ghost')
        if i == 4:
            c.line((x - 2, 64, x + 8, 64), 'spot')
    c.txt(304, 76, '6 / 6', 'ghost', right=True)
    quiet = (99, 99, 99)
    for pts in WIRES:
        curve(c, pts, quiet)
    c.d.line((305, 206, 302, 208, 305, 210), fill=quiet)
    c.txt(276, 204, 'out', 'ghost')
    for i, name in enumerate(ORDER):
        x, y = NODES[name]
        left, right = PORTS[name]
        w, h = heading_mask(name).size
        chosen = name == selected
        role = 'spot' if chosen else 'ink'
        c.line((left[0] + 4, left[1], x - 5, left[1]), 'spot' if chosen else 'rail')
        c.line((x + w + 5, right[1], right[0] - 4, right[1]), 'spot' if chosen else 'rail')
        port(c, left, c.p['spot'] if chosen else c.p['ghost'])
        port(c, right, c.p[role] if chosen else c.p['ghost'], output=True)
        c.heading(x, y, name, role)
        c.txt(x, y - 13, f'{i + 1:02}', 'spot' if chosen else 'ghost')
        if chosen:
            c.line((x, y + h + 5, x + w - 1, y + h + 5), 'spot')
            c.txt(x, y + h + 12, 'a > freq' if name == 'ring' else 'b > depth', 'spot')
    c.line((16, 216, 304, 216))
    c.txt(16, 227, 'hold move / tap actions', 'ghost')
    # Full means full: no active '+ add' affordance for an unavailable seventh slot.
    c.txt(304, 227, 'full', 'ghost', right=True)
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (864, 614), OUTSIDE)
    caption(sheet, 16, 12, 'seance / v5 / six modules / all visible')
    for x, selected, title in ((16, 'ring', '01 / ring expanded'),
                               (448, 'chor', '02 / focus moves to chor')):
        caption(sheet, x, 40, title)
        sheet.paste(upper(selected), (x, 60))
        sheet.paste(lower(selected), (x + 40, 324))
    caption(sheet, 16, 581, 'top: 168 + 5 x 40 px / bottom: same positions in both states')
    caption(sheet, 16, 599, 'static concept / native fonts / example data / not implemented')
    path = OUT / 'main-accordion-six-v5-2x.png'
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
