#!/usr/bin/env python3
"""Six color directions; one static sheet, not runtime/hardware telemetry.

Project cell-blackletter wordmark, Gossip headings, native 6x8 body.
Main-inspired geometry with identical illustrative control values per palette.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from seance_blackletter import draw_text
from render_module_geometry_study import Canvas
from render_menu_style_study import heading_mask

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/palette_study'
PALETTES = (
    ('pistachio kiss', 'mint paper / plum ink / sour cherry', '#d4dfb0', '#35273e', '#a92750'),
    ('apricot radio', 'peach paper / cocoa ink / electric blue', '#efbc99', '#482e36', '#263dc0'),
    ('orchid acid', 'violet paper / lilac ink / bitter lime', '#382847', '#f0d8ee', '#cbe879'),
    ('sulfur rite', 'yellow paper / brown ink / blood', '#e7ce83', '#49301f', '#a21e2b'),
    ('old grimoire', 'vellum paper / sepia ink / dried blood', '#e8dcc0', '#513928', '#7f1d2c'),
    ('black mass', 'charred paper / candle ink / hot wax', '#27231f', '#dfcda4', '#ec6955'),
)


def rgb(s):
    return tuple(bytes.fromhex(s.removeprefix('#')))


def mix(a, b, t):
    return tuple(round(x * (1 - t) + y * t) for x, y in zip(a, b))


def colors(spec):
    paper, ink, spot = map(rgb, spec[2:])
    return dict(paper=paper, ink=ink, spot=spot,
                ghost=mix(paper, ink, .72), rail=mix(paper, ink, .31),
                active=mix(paper, spot, .08), band=mix(paper, spot, .23),
                inverse=paper)


class Page(Canvas):
    def __init__(self, width, height, p):
        super().__init__(width, height, p['paper'])
        self.p = p

    def txt(self, x, y, text, role='ink', scale=1, right=False):
        self.text(x, y, text, self.p[role], scale, right)

    def line(self, xy, role='rail'):
        self.d.line(xy, fill=self.p[role])

    def title(self, x, y, text, role='ink'):
        mask = heading_mask(text)
        self.im.paste(self.p[role], (x, y), mask)


def rail(c, x, y, width, amount, mod=False):
    end = x + width - 1
    if mod:
        c.d.rectangle((x + width // 4, y - 3, x + width * 3 // 4, y + 3), fill=c.p['band'])
    c.line((x, y, end, y))
    for tx in (x, end):
        c.line((tx, y - 2, tx, y + 2))
    knob = x + round((width - 1) * amount)
    c.d.rectangle((knob - 1, y - 4, knob + 1, y + 4), fill=c.p['spot'])


def screen(p, top):
    c = Page(400 if top else 320, 240, p)
    offset = 40 if top else 0
    names = ('gran', 'ring', 'chor', 'dly', 'filt', 'vca')
    x = offset
    for i, name in enumerate(names):
        width = 160 if i == 1 else 32
        left, right = x + 2, x + width - 3
        y0, y1 = (40, 239) if top else (0, 197)
        if i == 1:
            c.d.rectangle((left, y0, right, y1), fill=p['active'])
        role = 'spot' if i == 1 else 'rail'
        c.line((left, y0, left, y1), role)
        c.line((right, y0, right, y1), role)
        y = y0 if top else y1
        c.line((left, y, right, y), role)
        if top:
            if i == 1:
                c.title(x + 14, 48, name, 'spot')
            else:
                c.txt(x + (width - len(name) * 6) // 2, 55, name)
                # Two example control positions, not audio level meters.
                for dx, value in ((11, .25 + i * .09), (21, .62 - i * .05)):
                    c.line((x + dx, 104, x + dx, 183))
                    yy = 183 - round(value * 79)
                    c.line((x + dx - 3, yy, x + dx + 3, yy), 'ghost')
        x += width
    if top:
        c.title(40, 9, 'patch table')
        c.txt(360, 19, 'chain 04', 'ghost', right=True)
        c.txt(86, 85, 'live control', 'ghost')
        c.txt(86, 105, 'freq')
        c.txt(218, 105, '512hz', 'spot', right=True)
        rail(c, 86, 128, 132, .59, mod=True)
        c.txt(86, 147, 'target 512hz', 'ghost')
        c.txt(86, 167, 'mix 68%', 'ghost')
        c.line((86, 184, 217, 184))
        c.txt(86, 196, 'a > freq', 'spot')
        c.txt(218, 196, '+24%', 'spot', right=True)
        c.txt(86, 218, 'source +0.38', 'ghost')
    else:
        c.txt(46, 20, 'edit', 'spot')
        c.line((46, 32, 69, 32), 'spot')
        c.d.arc((123, 18, 133, 28), 305, 235, fill=p['ghost'])
        c.line((128, 16, 128, 23), 'ghost')
        c.txt(159, 20, '...', 'ghost')
        for y, name, value, amount in ((75, 'freq', '440hz', .43), (136, 'mix', '68%', .68)):
            c.txt(46, y, name, 'ghost')
            c.txt(178, y, value, right=True)
            rail(c, 46, y + 23, 132, amount)
        for dx in (-5, 0, 5):
            c.d.rectangle((111 + dx, 184, 112 + dx, 185), fill=p['ghost'])
        for i, cx in enumerate((18, 54, 90, 126), 1):
            if i == 4:
                c.d.rectangle((cx - 6, 214, cx + 11, 231), fill=p['spot'])
            c.txt(cx, 219, str(i), 'inverse' if i == 4 else 'ghost')
        c.txt(304, 219, 'patch02 *', 'ghost', right=True)
    return c.im


def seal(c, cx, cy):
    # A print ornament for the sheet, not a replacement launcher icon.
    c.d.ellipse((cx - 19, cy - 19, cx + 19, cy + 19), outline=c.p['spot'])
    c.line((cx, cy - 24, cx, cy + 24), 'spot')
    c.line((cx - 24, cy, cx + 24, cy), 'spot')
    c.line((cx, cy - 13, cx + 13, cy, cx, cy + 13, cx - 13, cy, cx, cy - 13), 'spot')
    c.d.rectangle((cx - 2, cy - 2, cx + 2, cy + 2), fill=c.p['spot'])


def card(index, spec):
    p = colors(spec)
    c = Page(432, 710, p)
    c.txt(20, 20, f'{index:02} / {spec[0]}')
    c.txt(412, 20, 'seance', 'ghost', right=True)
    draw_text(c.d, 20, 48, 'seance', cell=4, color=p['ink'])
    seal(c, 375, 72)
    c.txt(20, 111, spec[1], 'ghost')
    c.im.paste(screen(p, True), (16, 139))
    c.im.paste(screen(p, False), (56, 391))
    c.line((20, 649, 411, 649))
    for i, (role, value) in enumerate(zip(('paper', 'ink', 'spot'), spec[2:])):
        x = 20 + i * 132
        c.d.rectangle((x, 664, x + 15, 679), fill=p[role], outline=p['rail'] if role == 'paper' else p[role])
        c.txt(x + 23, 662, role, 'ghost')
        c.txt(x + 23, 677, value)
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Canvas(1360, 1528, (24, 22, 26))
    sheet.text(20, 18, 'seance / chromatic rituals', (226, 217, 204), scale=2)
    sheet.text(20, 47, '01-03 / candy pigments                         04-06 / paper, blood & candlelight', (158, 150, 153))
    for i, spec in enumerate(PALETTES):
        sheet.im.paste(card(i + 1, spec), (16 + i % 3 * 448, 72 + i // 3 * 726))
    sheet.text(20, 1510, 'palette study / same example controls / native type / not a device capture / no runtime changes', (158, 150, 153))
    path = OUT / 'seance-six-palettes-2x.png'
    sheet.im.resize((2720, 3056), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
