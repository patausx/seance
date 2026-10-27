#!/usr/bin/env python3
"""Seance identity study. Does not replace the splash or shipping app icon.

Run: python3 tools/render_logo_directions.py
PNG proofs and font-independent SVG marks: ui_mockups/logo_directions/
"""
from pathlib import Path
from html import escape
from PIL import Image, ImageDraw, ImageFont
from seance_blackletter import GLYPHS, draw_text, text_width

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups' / 'logo_directions'
PAPER = '#e2bece'
HI = '#f2d8e2'
INK = '#1f141f'
SPOT = '#5b317c'
MUTED = '#79576b'
RULE = '#c79caf'


class Mark:
    """Small shared vector/raster scene in a 48 by 48 coordinate space."""
    def __init__(self, size=192, ink=INK, background=PAPER):
        self.scale = size * 4 / 48
        self.size = size
        self.image = Image.new('RGB', (size * 4, size * 4), background)
        self.draw = ImageDraw.Draw(self.image)
        self.ink = ink
        self.svg = []

    def box(self, x, y, w, h):
        s = self.scale
        self.draw.rectangle((round(x*s), round(y*s), round((x+w)*s)-1,
                             round((y+h)*s)-1), fill=self.ink)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{self.ink}"/>')

    def path(self, commands, width=None):
        points = []
        pieces = []
        for command in commands:
            op, *v = command
            pieces.append(op + ' '.join(str(n) for n in v))
            if op in ('M', 'L'):
                points.append((v[0], v[1]))
            elif op == 'C':
                x, y = points[-1]
                for i in range(1, 49):
                    t = i / 48
                    u = 1-t
                    points.append((u**3*x + 3*u*u*t*v[0] + 3*u*t*t*v[2] + t**3*v[4],
                                   u**3*y + 3*u*u*t*v[1] + 3*u*t*t*v[3] + t**3*v[5]))
            elif op == 'Z':
                points.append(points[0])
            else:
                raise ValueError(op)
        scaled = [(round(x*self.scale), round(y*self.scale)) for x, y in points]
        if width is None:
            self.draw.polygon(scaled, fill=self.ink)
            style = f'fill="{self.ink}"'
        else:
            self.draw.line(scaled, fill=self.ink, width=round(width*self.scale), joint='curve')
            style = f'fill="none" stroke="{self.ink}" stroke-width="{width}" stroke-linejoin="round"'
        self.svg.append(f'<path d="{" ".join(pieces)}" {style}/>')

    def finish(self):
        return self.image.resize((self.size, self.size), Image.Resampling.LANCZOS)


def initial(m):
    # Actual canonical initial, not an unrelated stock gothic face.
    for y, row in enumerate(GLYPHS['s']):
        for x, bit in enumerate(row):
            if bit == '#':
                m.box(10.5 + x*3, 6 + y*3, 3, 3)


def contact(m):
    # A single broad S ribbon cut across its central contact. Both pieces
    # remain recognisable without colour, halos, dots, or an enclosing badge.
    top = [('M',38,8), ('L',17,8), ('C',9,8,6,12,6,18),
           ('C',6,23,9,26,15,28), ('L',24,31), ('L',28,24),
           ('L',17,20), ('C',14,19,14,16,18,16), ('L',38,16), ('Z',)]
    m.path(top)
    # Half-turn symmetry makes the two contacts physically related.
    bottom = []
    for op, *v in top:
        bottom.append(tuple([op] + [48-n for n in v]))
    m.path(bottom)


def thread(m):
    # Two continuous score threads, rather than a generic waveform badge.
    for offset in (-3, 3):
        m.path([('M',35+offset,10),
                ('C',8+offset,1,7+offset,22,22+offset,24),
                ('C',39+offset,27,37+offset,47,10+offset,38)], width=2.2)


def monogram(m):
    # S's exterior folds and N's diagonal share a compact octagonal frame.
    # C is the left/open outer contour: intentionally a sigil, not body text.
    m.path([('M',37,9), ('L',15,9), ('L',9,15), ('L',9,24),
            ('L',33,24), ('L',39,30), ('L',39,34), ('L',33,40),
            ('L',11,40)], width=3.5)
    m.path([('M',17,36), ('L',17,16), ('L',31,34), ('L',31,14)], width=3.5)


DIRECTIONS = [
    ('01', 'initial', 'letter seal', 'the original blackletter s; a direct approach.', initial),
    ('02', 'contact', 'contact', 'an open s: two halves of one signal.', contact),
    ('03', 'thread', 'double thread', 's as drawn modulation and its echo.', thread),
    ('04', 'monogram', 'monogram', 'snc combined into one angular sigil.', monogram),
]


def font(size, mono=False):
    name = 'IBMPlexMono-Regular.ttf' if mono else 'IBMPlexSans-Regular.ttf'
    path = Path('/usr/share/fonts/TTF') / name
    if not path.exists():
        path = Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
    return ImageFont.truetype(str(path), size)


def wordmark(image, x, y, cell=5, color=INK):
    draw_text(ImageDraw.Draw(image), x, y, 'seance', cell=cell, spacing=0, color=color)


def render_mark(fn, size, ink=INK, background=PAPER):
    m = Mark(size, ink, background)
    fn(m)
    return m.finish(), m


def write_svg(path, m, name):
    path.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">\n'
                    f'<title>seance / {escape(name)}</title>\n' + '\n'.join(m.svg) + '\n</svg>\n')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (1280, 960), PAPER)
    d = ImageDraw.Draw(sheet)
    d.text((40, 27), 'seance / an instrument mark', font=font(28), fill=INK)
    d.text((40, 68), 'four directions · shared lettering · checked at 3ds size',
           font=font(17), fill=MUTED)
    d.line((40,108,1240,108), fill=RULE)
    for i, (number, slug, title, description, fn) in enumerate(DIRECTIONS):
        x = 40 + (i % 2)*640
        y = 140 + (i // 2)*392
        d.text((x, y), number, font=font(18, True), fill=SPOT)
        d.text((x+42, y-3), title, font=font(24), fill=INK)
        large, scene = render_mark(fn, 168)
        sheet.paste(large, (x-10, y+53))
        wordmark(sheet, x+190, y+103, cell=6)
        d.text((x+192, y+192), 'sampler / synth / sigils', font=font(13, True), fill=MUTED)
        d.text((x, y+246), description, font=font(16), fill=INK)
        d.text((x, y+292), '48 px', font=font(12, True), fill=MUTED)
        d.text((x+105, y+292), '24 px', font=font(12, True), fill=MUTED)
        for size, xx in ((48, x+45), (24, x+152)):
            icon, _ = render_mark(fn, size)
            icon.save(OUT / f'{number}_{slug}_{size}.png')
            sheet.paste(icon, (xx, y+279+(48-size)//2))
        for j, (ink, bg) in enumerate(((PAPER, INK), (HI, SPOT), ('#000000', '#ffffff'))):
            proof, _ = render_mark(fn, 48, ink, bg)
            sheet.paste(proof, (x+306+j*70, y+279))
        d.line((x, y+355, x+560, y+355), fill=RULE)
        large.save(OUT / f'{number}_{slug}_mark.png')
        write_svg(OUT / f'{number}_{slug}.svg', scene, title)
        lockup = Image.new('RGB', (800, 280), PAPER)
        icon, _ = render_mark(fn, 176)
        lockup.paste(icon, (45,52))
        wordmark(lockup, 274, 92, cell=9)
        lockup.save(OUT / f'{number}_{slug}_lockup.png')
    d.text((40, 922), 'studies — current splash and app icon unchanged', font=font(15), fill=MUTED)
    sheet.save(OUT / 'seance_logo_directions.png')
    print(OUT / 'seance_logo_directions.png')


if __name__ == '__main__':
    main()
