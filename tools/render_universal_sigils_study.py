#!/usr/bin/env python3
"""Static design only: eight universal sigils, not implemented engine/UI.
Native 400x240 + 320x240, nearest 2x; illustrative values, no hardware capture.
Reuses the project's 6x8 glyphs and Gossip heading font (assets/fonts/OFL.txt).
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_module_geometry_study import Canvas
from render_menu_style_study import paper, heading_mask, overlaps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/universal_sigils_study'
OUTSIDE = (20, 20, 22)
THEMES = {
    'pink': dict(ink=(31, 20, 31), spot=(91, 49, 124),
                 ghost=(126, 100, 119), rail=(174, 145, 165)),
    'graphite': dict(ink=(244, 244, 244), spot=(181, 154, 232),
                     ghost=(184, 184, 184), rail=(94, 94, 94)),
}
DRAWN = ((0, -.3), (.08, -.3), (.12, .12), (.23, .52), (.34, .74),
         (.41, .78), (.46, .57), (.50, -.05), (.58, -.66),
         (.67, -.65), (.78, -.27), (.82, .03), (.85, -.08), (1, .38))


class Surface(Canvas):
    def __init__(self, width, theme):
        super().__init__(width)
        self.theme = theme
        self.p = THEMES[theme]
        self.im = paper(width) if theme == 'pink' else Image.new('RGB', (width, 240), (36, 36, 36))
        self.d = ImageDraw.Draw(self.im)

    def txt(self, x, y, s, role='ink', scale=1, right=False):
        self.text(x, y, s, self.p[role], scale, right)

    def line(self, points, role='rail'):
        self.d.line(points, fill=self.p[role])

    def heading(self, x, y, s, role='ink'):
        mask = heading_mask(s)
        box = (x, y, x + mask.width, y + mask.height)
        assert box[2] <= self.im.width and box[3] <= 240
        assert all(not overlaps(box, b) for b, _ in self.labels)
        self.labels.append((box, s))
        self.im.paste(self.p[role], (x, y), mask)

    def chevron(self, x, y, role='ghost'):
        self.line((x - 3, y - 1, x, y + 2, x + 3, y - 1), role)

    def dot(self, x, y, hollow=False):
        box = (x - 3, y - 3, x + 3, y + 3)
        self.d.ellipse(box, outline=self.p['spot'], fill=None if hollow else self.p['spot'])


def value(shape, t):
    if shape == 'sine':
        return math.sin(t * math.tau)
    if shape == 'triangle':
        return 2 / math.pi * math.asin(math.sin(t * math.tau))
    if shape == 'saw':
        return 2 * t - 1
    if shape == 'square':
        return 1 if t < .5 else -1
    if shape == 'random':
        # Icon only, illustrative sample-and-hold, not a periodic oracle.
        return (.5, -.6, .8, -.1, .35, -.85)[min(5, int(t * 6))]
    for (a, av), (b, bv) in zip(DRAWN, DRAWN[1:]):
        if a <= t <= b:
            return av + (bv - av) * (t - a) / (b - a)
    return DRAWN[-1][1]


def wave(c, shape, box, role='ink', cursor=None):
    x, y, w, h = box
    pts = [(x + i, y + round((1 - value(shape, i / (w - 1))) * (h - 1) / 2)) for i in range(w)]
    c.d.line(pts, fill=c.p[role])
    if cursor is not None:
        n = round(cursor * (w - 1))
        c.d.line(pts[max(0, n - 24):n + 1], fill=c.p['spot'])
        c.dot(*pts[n], hollow=True)
    return pts


def upper(theme, shape='drawn', sync=False):
    c = Surface(400, theme)
    c.heading(20, 14, 'sigils')
    c.txt(110, 26, 'a', 'spot', 2)
    c.txt(380, 25, 'source / 01 of 08', 'ghost', right=True)
    timing = 'sync 1/2 beat' if sync else 'free 2.00 hz'
    c.txt(20, 53, f'{shape} / loop / {timing}', 'spot')
    phase = .125
    c.txt(380, 70, f'output {value(shape, phase):+.2f}', right=True)
    c.line((28, 119, 372, 119))
    for x in (28, 114, 200, 286, 372):
        c.line((x, 117, x, 121))
    c.line((71, 84, 71, 152))
    wave(c, shape, (28, 87, 345, 65), cursor=phase)
    c.txt(20, 157, '0', 'ghost')
    c.txt(200, 157, '180', 'ghost')
    c.txt(380, 157, '360', 'ghost', right=True)
    c.line((20, 174, 380, 174))
    c.txt(20, 182, 'destinations / 03', 'ghost')
    c.line((20, 198, 20, 219))
    for y, name, amount in ((197, 'chain 1 > vca / gain', '+42%'),
                            (213, 'chain 1 > filter / cutoff', '-18%')):
        c.line((20, y + 4, 30, y + 4))
        c.txt(39, y, name)
        c.txt(372, y, amount, 'spot', right=True)
    c.txt(20, 230, '+1 more in sigilbay', 'ghost')
    c.txt(380, 230, 'shared source', 'ghost', right=True)
    return c


def lower(theme, shape='drawn', sync=False):
    c = Surface(320, theme)
    c.heading(16, 10, 'sigils')
    c.txt(117, 19, 'a', 'spot', 2)
    c.chevron(145, 25, 'spot')
    c.line((116, 37, 150, 37), 'spot')
    c.txt(304, 23, 'undo', 'ink' if shape == 'drawn' else 'ghost', right=True)
    wave(c, shape, (16, 45, 28, 10), 'spot')
    c.txt(55, 46, shape, 'spot')
    c.chevron(104, 49, 'spot')
    c.txt(204, 46, 'mode', 'ghost')
    c.txt(246, 46, 'loop')
    c.chevron(300, 49)
    # A full-width drawing surface remains primary, not a tiny LFO thumbnail.
    c.line((20, 94, 300, 94))
    for x in (20, 90, 160, 230, 300):
        c.line((x, 120, x, 123))
    wave(c, shape, (20, 64, 281, 56), cursor=.125)
    c.txt(16, 135, 'free', 'ghost' if sync else 'spot')
    c.txt(52, 135, 'sync', 'spot' if sync else 'ghost')
    sx = 52 if sync else 16
    c.line((sx, 147, sx + 23, 147), 'spot')
    c.txt(92, 133, '1/2' if sync else '2.00', 'spot', 2)
    c.txt(143 if sync else 145, 141, 'beat' if sync else 'hz', 'ghost')
    c.txt(208, 134, 'depth', 'ghost')
    c.txt(304, 134, '+100%', 'ink', right=True)
    c.line((208, 150, 300, 150))
    c.line((254, 147, 254, 153))
    c.dot(300, 150)
    c.txt(16, 163, 'phase', 'ghost')
    c.txt(58, 163, '45 deg', 'spot')
    c.line((110, 168, 300, 168))
    for i in range(9):
        x = 110 + round(190 * i / 8)
        c.line((x, 166, x, 170))
    c.line((134, 164, 134, 172), 'spot')
    c.txt(16, 187, 'bipolar')
    c.chevron(71, 190)
    c.txt(126, 187, 'restart', 'ghost')
    c.txt(186, 187, 'transport')
    c.chevron(300, 190)
    c.txt(16, 209, 'rate mod', 'ghost')
    c.txt(80, 209, 'none')
    c.chevron(112, 212)
    c.txt(152, 209, 'paused in sync' if sync else 'amount   +0%', 'ghost')
    c.line((16, 222, 304, 222))
    c.txt(16, 231, 'reset', 'ink')
    c.txt(101, 231, 'sigilbay 03', 'spot')
    c.txt(304, 231, 'b back', 'ghost', right=True)
    return c


def drawer(theme, kind):
    c = lower(theme)
    # An opaque upward sheet in the same paper; no desktop popup boxes.
    cover = (0, 61, 320, 240)
    base = Surface(320, theme).im
    c.im.paste(base.crop(cover), (0, 61))
    c.labels = [(b, s) for b, s in c.labels if not overlaps(b, cover)]
    c.line((16, 61, 304, 61))
    c.line((149, 66, 171, 66), 'ghost')
    c.txt(16, 76, 'choose sigil' if kind == 'sigil' else 'choose shape', 'ghost')
    c.txt(304, 76, 'a - h' if kind == 'sigil' else 'sigil a', 'ghost', right=True)
    if kind == 'sigil':
        shapes = ('drawn', 'sine', 'triangle', 'saw', 'square', 'random', 'drawn', 'sine')
        counts = (3, 2, 1, 4, 2, 1, 0, 0)
        for i, (shape, count) in enumerate(zip(shapes, counts)):
            x, y = 20 + (i % 2) * 150, 96 + (i // 2) * 31
            role = 'spot' if i == 0 else 'ink'
            c.txt(x, y, chr(97 + i), role, 2)
            wave(c, shape, (x + 30, y + 1, 56, 15), role)
            c.txt(x + 105, y + 5, f'{count:02}', 'ghost')
            if i == 0:
                c.line((x, y + 23, x + 118, y + 23), 'spot')
        c.txt(16, 230, 'number = cables', 'ghost')
    else:
        for i, shape in enumerate(('drawn', 'sine', 'triangle', 'saw', 'square', 'random')):
            x, y = 20 + (i % 2) * 150, 101 + (i // 2) * 39
            role = 'spot' if i == 0 else 'ink'
            wave(c, shape, (x, y, 44, 17), role)
            c.txt(x + 54, y + 5, shape, role)
            if i == 0:
                c.line((x, y + 25, x + 119, y + 25), 'spot')
        c.txt(16, 230, 'drawing kept', 'ghost')
    c.txt(304, 230, 'b close', 'ghost', right=True)
    return c


def caption(im, x, y, s):
    c = Canvas(im.width, im.height)
    c.im = im
    c.d = ImageDraw.Draw(im)
    c.text(x, y, s, (184, 180, 192))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    main_sheet = Image.new('RGB', (852, 558), OUTSIDE)
    menu_sheet = Image.new('RGB', (688, 554), OUTSIDE)
    for i, theme in enumerate(THEMES):
        x = 18 + i * 416
        shape, sync = ('drawn', False) if theme == 'pink' else ('sine', True)
        caption(main_sheet, x, 12, f'{theme} / {shape} / ' + ('sync' if sync else 'free'))
        main_sheet.paste(upper(theme, shape, sync).im, (x, 30))
        main_sheet.paste(lower(theme, shape, sync).im, (x + 40, 282))
        for j, kind in enumerate(('sigil', 'shape')):
            mx, my = 16 + j * 336, 12 + i * 266
            caption(menu_sheet, mx, my, theme + ' / choose ' + kind)
            menu_sheet.paste(drawer(theme, kind).im, (mx, my + 16))
    caption(main_sheet, 18, 542, 'static concept / native layout / not implemented')
    caption(menu_sheet, 16, 542, 'upward drawers / static concept / not implemented')
    for name, image in (('editor-2x.png', main_sheet), ('menus-2x.png', menu_sheet)):
        path = OUT / name
        image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST).save(path)
        print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
