#!/usr/bin/env python3
"""One native-resolution, locally drawn theme-picker concept; NOT runtime.

Two states on one sheet. The draft changes the upper preview only; the lower
controls retain saved gray. Reuses project 6x8 + Gossip28, nearest-neighbor 2x.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_module_geometry_study import Canvas
from render_menu_style_study import heading_mask, overlaps
from render_universal_sigils_study import caption

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/theme_picker_study'
PINK = ((226, 190, 206), (31, 20, 31), (91, 49, 124))
GRAY = ((36, 36, 36), (244, 244, 244), (181, 154, 232))
GRIMOIRE = ((232, 220, 192), (81, 57, 40), (127, 29, 44))
BLACK = ((39, 35, 31), (223, 205, 164), (236, 105, 85))
CUSTOM = (BLACK[0], BLACK[1], GRAY[2])
SWATCHES = (
    (232,220,192), (223,205,164), (231,206,131), (239,188,153),
    (226,190,206), (240,216,238), (212,223,176), (244,244,244),
    (81,57,40), (39,35,31), (36,36,36), (31,20,31),
    (56,40,71), (53,39,62), (37,59,57), (72,46,54),
    (127,29,44), (162,30,43), (236,105,85), (169,39,80),
    (91,49,124), (181,154,232), (38,61,192), (203,232,121),
)


def mix(a, b, t):
    return tuple(round(x * (1-t) + y * t) for x, y in zip(a, b))


def inverse(color):
    values = [v / 255 for v in color]
    lum = sum((v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4) * w
              for v, w in zip(values, (.2126, .7152, .0722)))
    return (0, 0, 0) if lum > .179 else (255, 255, 255)


class Page(Canvas):
    def __init__(self, width, colors):
        super().__init__(width, bg=colors[0])
        self.paper, self.ink, self.accent = colors
        self.muted = mix(self.paper, self.ink, .64)
        self.rule = mix(self.paper, self.ink, .24)

    def txt(self, x, y, text, color=None, right=False):
        self.text(x, y, text, self.ink if color is None else color, right=right)

    def title(self, x, y, text, color=None):
        mask = heading_mask(text)
        box = (x, y, x + mask.width, y + mask.height)
        assert box[2] <= self.im.width and box[3] <= self.im.height
        assert all(not overlaps(box, other) for other, _ in self.labels)
        self.labels.append((box, text))
        self.im.paste(self.ink if color is None else color, (x, y), mask)

    def line(self, points, color=None):
        self.d.line(points, fill=self.rule if color is None else color)


def palette_seal(c, x, y):
    # Decorative three-paint specimen, not a new sigil or an interactive control.
    c.d.ellipse((x-19, y-19, x+19, y+19), outline=c.rule)
    c.d.polygon(((x, y-24), (x+24, y), (x, y+24), (x-24, y)), outline=c.ink)
    c.d.ellipse((x-13, y-6, x+1, y+8), fill=c.ink)
    c.d.ellipse((x-1, y-6, x+13, y+8), fill=c.accent)
    c.line((x, y-16, x, y-10), c.accent)
    c.line((x, y+12, x, y+17), c.accent)


def rail(c, x, y, width, value, band=False):
    if band:
        c.d.rectangle((x+26, y-2, x+102, y+2), fill=mix(c.paper, c.accent, .23))
    c.line((x, y, x+width-1, y), c.ink)
    for dx in (0, width-1):
        c.line((x+dx, y-2, x+dx, y+2), c.ink)
    k = x + round((width-1)*value)
    c.d.rectangle((k-1, y-4, k+1, y+4), fill=c.accent)


def upper(custom=False):
    c = Page(400, CUSTOM if custom else GRIMOIRE)
    c.txt(40, 16, 'seance / theme', c.muted)
    c.txt(360, 16, 'preview', c.muted, right=True)
    c.title(40, 39, 'custom' if custom else 'old grimoire')
    c.txt(40, 74, 'from black mass' if custom else 'paper, ink & a little blood', c.muted)
    palette_seal(c, 338, 59)
    c.line((40, 97, 360, 97))
    # This is a compact UI specimen, not a live editor or audio readout.
    c.title(40, 112, 'ring', c.accent)
    c.txt(112, 123, 'tone', c.muted)
    c.d.rectangle((264, 116, 307, 136), fill=c.accent)
    c.txt(274, 123, 'edit', inverse(c.accent))
    c.txt(360, 123, 'on', c.muted, right=True)
    for x, name, value, position in ((40, 'freq', '220 hz', .43), (216, 'mix', '50%', .5)):
        c.txt(x, 155, name, c.muted)
        c.txt(x+144, 155, value, right=True)
        rail(c, x, 179, 144, position, band=name == 'freq')
    c.txt(40, 200, 'sigil a', c.accent)
    c.line((89, 204, 112, 204), c.accent)
    c.line((109, 201, 112, 204, 109, 207), c.accent)
    c.txt(122, 200, 'freq', c.muted)
    c.txt(360, 200, '+24%', c.accent, right=True)
    c.line((40, 218, 360, 218))
    c.txt(40, 228, 'draft', c.muted)
    c.txt(360, 228, 'not applied', c.muted, right=True)
    return c.im


def triplet(c, x, y, colors):
    for i, color in enumerate(colors):
        edge = c.rule if sum(abs(a-b) for a, b in zip(color, c.paper)) < 40 else None
        c.d.rectangle((x+i*7, y, x+i*7+5, y+9), fill=color, outline=edge)


def lower(custom=False):
    c = Page(320, GRAY)  # Deliberately NOT the draft colors.
    colors = CUSTOM if custom else GRIMOIRE
    c.title(16, 10, 'themes')
    c.txt(304, 23, 'saved / gray', c.muted, right=True)
    choices = (
        (16, 50, 'pink', PINK), (172, 50, 'gray', GRAY),
        (16, 74, 'old grimoire', GRIMOIRE), (172, 74, 'black mass', BLACK),
        (16, 98, 'custom', CUSTOM if custom else GRIMOIRE),
    )
    active = 'custom' if custom else 'old grimoire'
    for x, y, name, palette in choices:
        triplet(c, x, y-1, palette)
        selected = name == active
        c.txt(x+29, y, name, c.accent if selected else c.ink)
        if selected:
            c.line((x+29, y+12, x+29+len(name)*6-2, y+12), c.accent)
        if selected and not custom:
            # Small cursor = navigation focus, underline = draft selection.
            c.line((x-8, y+1, x-5, y+4, x-8, y+7), c.accent)
    c.txt(304, 98, '3 paints', c.muted, right=True)
    c.line((16, 119, 304, 119))
    for i, name in enumerate(('paper', 'ink', 'accent')):
        x = 16 + 100*i
        edge = c.rule if sum(abs(a-b) for a, b in zip(colors[i], c.paper)) < 40 else None
        c.d.rectangle((x, 129, x+21, 146), fill=colors[i], outline=edge)
        c.txt(x+29, 134, name, c.accent if i == 2 else c.muted)
        if i == 2:
            c.line((x+29, 146, x+63, 146), c.accent)
    for i, color in enumerate(SWATCHES):
        x, y = 18 + (i % 8)*36, 158 + (i//8)*18
        edge = c.rule if sum(abs(a-b) for a, b in zip(color, c.paper)) < 40 else None
        c.d.rectangle((x, y, x+31, y+12), fill=color, outline=edge)
        chosen = color == colors[2]
        if chosen:
            # Filled inner diamond = current paint, exterior corners = focus.
            c.d.polygon(((x+16,y+3), (x+19,y+6), (x+16,y+9), (x+13,y+6)), fill=inverse(color))
        if custom and chosen:
            for sx, sy, dx, dy in ((x-2,y-2,1,1), (x+33,y-2,-1,1),
                                  (x-2,y+14,1,-1), (x+33,y+14,-1,-1)):
                c.line((sx+4*dx,sy,sx,sy,sx,sy+3*dy), c.accent)
    c.txt(16, 223, 'b cancel', c.muted)
    c.txt(304, 223, 'save >', c.accent, right=True)
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (872, 628), (17, 17, 20))
    caption(sheet, 20, 14, 'seance / theme picker / paint & paper')
    caption(sheet, 20, 34, '01 / preset')
    caption(sheet, 452, 34, '02 / custom accent')
    for x, custom in ((20, False), (452, True)):
        sheet.paste(upper(custom), (x, 54))
        sheet.paste(lower(custom), (x+40, 310))
    caption(sheet, 60, 568, 'palette beside every name')
    caption(sheet, 492, 568, 'three paints / 24 shades')
    caption(sheet, 60, 584, 'words, not cards')
    caption(sheet, 492, 584, 'saved controls / draft above')
    caption(sheet, 20, 610, 'static concept / native 400x240 + 320x240 / not runtime')
    output = OUT / 'theme-picker-v1-2x.png'
    sheet.resize((1744, 1256), Image.Resampling.NEAREST).save(output)
    print(output.relative_to(ROOT))


if __name__ == '__main__':
    main()
