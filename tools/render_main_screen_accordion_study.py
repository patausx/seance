#!/usr/bin/env python3
"""One static two-state study, not runtime or a hardware/audio capture.

400x240 top / 320x240 bottom, native Gossip28 and 6x8, nearest 2x sheet.
The supplied GIF informs spatial expansion only. Diagrams/values are illustrative.
Font license: assets/fonts/OFL.txt. Existing v1-v3 studies are left untouched.
"""
from pathlib import Path
import math
from PIL import Image
from render_universal_sigils_study import Surface, caption, OUTSIDE
from render_menu_style_study import heading_mask
from render_main_screen_table_study import NODES, chrome, curve, arrow, node

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_accordion_study'
ORDER = tuple(NODES)


def center_text(c, cx, y, text, role='ghost'):
    c.txt(cx - len(text) * 3, y, text, role)


def contour(c, left, right, selected):
    """Open paper brackets, no filled cards, full capsule outlines or shadows."""
    y0, y1 = (67, 207) if selected else (78, 196)
    role = 'spot' if selected else 'rail'
    color = c.p[role]
    for x, direction in ((left + 3, 1), (right - 4, -1)):
        tip = x + direction * 10
        curve(c, ((tip, y0), (x, y0), (x, y0 + 8), (x, y0 + 18)), color)
        c.line((x, y0 + 18, x, y1 - 18), role)
        curve(c, ((x, y1 - 18), (x, y1 - 8), (x, y1), (tip, y1)), color)


def trace(c, x, y, width, height, cycles, phase=0, role='ink', carrier=False):
    # A reference drawing, not measured PCM, spectral data or live DSP output.
    pts = []
    for i in range(width):
        t = i / (width - 1)
        value = math.sin(t * math.tau * cycles + phase)
        if carrier:
            value *= math.sin(t * math.tau * 1.5)
        pts.append((x + i, y - round(value * height)))
    c.d.line(pts, fill=c.p[role])


def miniature(c, name, cx):
    if name == 'gran':
        for i, amp in enumerate((3, 7, 11, 5, 9, 4)):
            x = cx - 15 + i * 6
            c.line((x, 135 - amp, x, 135 + amp), 'ghost')
    elif name == 'ring':
        trace(c, cx - 17, 135, 35, 11, 2, role='ghost', carrier=True)
    elif name == 'chor':
        trace(c, cx - 17, 130, 35, 5, .8, role='ghost')
        trace(c, cx - 17, 140, 35, 5, .8, phase=.8, role='rail')
    else:
        for i, amp in enumerate((12, 8, 5, 2)):
            x = cx - 15 + i * 10
            c.line((x, 147, x, 147 - amp * 2), 'ghost' if i == 0 else 'rail')


def detail(c, name, left, right):
    x, end = left + 16, right - 16
    width = end - x + 1
    if name == 'ring':
        c.txt(x, 108, 'freq', 'ghost')
        c.txt(end, 108, '440 hz', 'ink', right=True)
        # One illustrative ring-modulated reference trace, not an oscilloscope.
        c.line((x, 143, end, 143))
        trace(c, x, 143, width, 19, 5, role='spot', carrier=True)
        c.txt(x, 173, 'a > freq', 'spot')
        c.txt(end, 173, '+24%', 'spot', right=True)
    else:
        c.txt(x, 108, 'depth', 'ghost')
        c.txt(end, 108, '38%', 'ink', right=True)
        # Two offset reference voices, not a promise of engine telemetry.
        c.line((x, 143, end, 143))
        trace(c, x, 139, width, 13, 1.8, role='ghost')
        trace(c, x, 147, width, 13, 1.8, phase=.9, role='spot')
        c.txt(x, 173, 'b > depth', 'spot')
        c.txt(end, 173, '+12%', 'spot', right=True)
    # Display-only range/base/live markers; this is not a touch control up top.
    c.d.rectangle((x + 34, 189, end - 22, 193), fill=(58, 51, 69))
    c.line((x, 191, end, 191))
    c.line((x + 58, 187, x + 58, 195), 'ghost')
    live = x + (86 if name == 'ring' else 69)
    c.d.rectangle((live - 1, 187, live + 1, 195), fill=c.p['spot'])


def upper(selected):
    c = Surface(400, 'graphite')
    c.heading(16, 12, 'chain 4')
    c.txt(384, 23, 'patch02 *', 'ghost', right=True)
    left = 16
    for i, name in enumerate(ORDER):
        chosen = name == selected
        width = 176 if chosen else 64
        right = left + width
        cx = (left + right) // 2
        center_text(c, cx, 51, f'{i + 1:02}', 'spot' if chosen else 'ghost')
        contour(c, left, right, chosen)
        w = heading_mask(name).width
        c.heading(cx - w // 2, 78 if chosen else 92, name,
                  'spot' if chosen else 'ink')
        if chosen:
            detail(c, name, left, right)
        else:
            miniature(c, name, cx)
            center_text(c, cx, 167,
                        {'gran': 'source', 'ring': 'freq', 'chor': 'depth', 'dly': 'echo'}[name])
        left = right
    assert left == 384
    c.line((16, 218, 384, 218))
    c.txt(16, 228, '04 / 06 modules', 'ghost')
    c.txt(384, 228, 'serial > out', 'ghost', right=True)
    return c.im


def lower(selected):
    c = Surface(320, 'graphite')
    chrome(c, 'normal')
    quiet = (99, 99, 99)
    # Exact same v3 nodes and serial wires in both states; selection is not reorder.
    for pts in (((92, 110), (111, 110), (124, 96), (150, 96)),
                ((215, 96), (260, 96), (188, 155), (229, 155)),
                ((295, 155), (317, 155), (309, 204), (258, 204)),
                ((258, 204), (179, 209), (63, 199), (94, 180))):
        curve(c, pts, quiet)
    c.d.line((148, 180, 184, 180), fill=quiet)
    arrow(c, 182, 180, quiet)
    c.txt(193, 176, 'out', 'ghost')
    for name in ORDER:
        node(c, name, selected=name == selected)
    if selected == 'chor':
        x, y = NODES['chor']
        c.txt(x, y + heading_mask('chor').height + 12, 'b > depth', 'spot')
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (864, 614), OUTSIDE)
    caption(sheet, 16, 12, 'seance / v4 / elastic chain + free table')
    for x, selected, title in ((16, 'ring', '01 / ring expanded'),
                               (448, 'chor', '02 / focus moves to chor')):
        caption(sheet, x, 40, title)
        sheet.paste(upper(selected), (x, 60))
        sheet.paste(lower(selected), (x + 40, 324))
    caption(sheet, 16, 581, 'top expands / bottom stays anchored / native 400x240 + 320x240')
    caption(sheet, 16, 599, 'static concept / example values + reference diagrams / not implemented')
    path = OUT / 'main-accordion-v4-2x.png'
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
