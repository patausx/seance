#!/usr/bin/env python3
"""Five offline chain-workflow mockups, not runtime or LCD captures.

Exact 320x240 screens, existing 6x8 bitmap labels, binary gothic title,
user-specified five inks plus the existing vertical paper wash. Values and
controls are illustrative. No engine, controller, hitbox or device changes.
"""
from pathlib import Path
import hashlib
import json
from PIL import Image, ImageDraw, ImageChops
from render_module_geometry_study import Canvas
from render_menu_style_study import paper, heading_mask

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/chain_workflow_study'
INK = (31, 20, 31)
SPOT = (91, 49, 124)
GHOST = (174, 130, 151)
FAINT = (207, 165, 184)
HI = (242, 216, 226)
BG = paper(320)
INKS = {INK, SPOT, GHOST, FAINT, HI}
TITLE = heading_mask('patch table')


class Page(Canvas):
    def __init__(self):
        super().__init__(320)
        self.im = BG.copy()
        self.d = ImageDraw.Draw(self.im)
        self.geometry = []
        self.im.paste(INK, (16, 12), TITLE)
        self.labels.append(((16, 12, 16 + TITLE.width, 12 + TITLE.height), 'patch table'))
        self.text(304, 22, 'patch01 / saved', GHOST, right=True)
        self.text(16, 48, 'chain', GHOST)
        for i in range(4):
            x = 88 + i * 44
            self.text(x, 48, str(i + 1), SPOT if i == 0 else GHOST)
            if i == 0:
                self.line((x - 2, 59, x + 6, 59), SPOT)

    def line(self, xy, color=GHOST):
        assert len(xy) % 2 == 0
        assert all(0 <= n < (320 if i % 2 == 0 else 240) for i, n in enumerate(xy))
        self.d.line(xy, fill=color, width=1)

    def center(self, x, y, text, color=INK):
        self.text(x - len(text) * 3, y, text, color)

    def footer(self, left, right):
        self.text(16, 228, left, GHOST)
        self.text(304, 228, right, GHOST, right=True)

    def add_strip(self):
        self.text(16, 204, '+ add', GHOST)
        self.center(160, 204, '4 / 6 modules', GHOST)

    def arrow(self, x, y, color=GHOST):
        self.line((x - 3, y - 3, x, y, x - 3, y + 3), color)

    def cell(self, x, y, w, h, name, selected=False, empty=False):
        assert 0 <= x < x+w <= 320 and 0 <= y < y+h <= 240
        color = SPOT if selected else FAINT if empty else GHOST
        if empty:
            for xx in range(x+3, x+w-3, 5):
                for yy in (y, y+h-1):
                    self.line((xx, yy, min(xx+2, x+w-4), yy), color)
            for yy in range(y+3, y+h-3, 5):
                for xx in (x, x+w-1):
                    self.line((xx, yy, xx, min(yy+2, y+h-4)), color)
            self.center(x+w//2, y+(h-8)//2, '+', FAINT)
        else:
            self.line((x+3, y, x+w-4, y, x+w-1, y+3,
                       x+w-1, y+h-4, x+w-4, y+h-1,
                       x+3, y+h-1, x, y+h-4, x, y+3, x+3, y), color)
            assert len(name)*6 <= w-4, (name, w, 'insufficient text padding')
            self.center(x+w//2, y+(h-8)//2, name, SPOT if selected else INK)
        self.geometry.append({'name': name, 'body': [x, y, w, h], 'empty': empty})

    def socket(self, x, y, selected=False):
        self.d.point((x, y), fill=SPOT if selected else GHOST)


def minimal():
    c = Page()
    # 44x22 bodies; orthogonal paths have discrete 2px softened elbows.
    c.line((91, 102, 176, 102))
    c.line((223, 102, 238, 102, 240, 104, 240, 125,
            238, 127, 98, 127, 96, 129, 96, 149, 98, 151, 104, 151))
    c.line((151, 151, 190, 151))
    c.line((237, 151, 271, 151))
    c.arrow(274, 151)
    c.text(282, 147, 'out', GHOST)
    for x, y, label, selected in ((46, 91, 'fm', False), (178, 91, 'strn', True),
                                  (106, 140, 'ghost', False), (192, 140, 'rev', False)):
        c.cell(x, y, 44, 22, label, selected)
        c.socket(x-2, y+11, selected)
        c.socket(x+45, y+11, selected)
        if selected:
            c.line((x+10, y+17, x+33, y+17), SPOT)
    c.add_strip()
    c.footer('a trig / y sigil', 'tap / hold node')
    return c


def ribbon():
    c = Page()
    c.text(4, 116, 'in', GHOST)
    y = 120
    starts = [24 + i*43 for i in range(6)]
    c.line((18, y, starts[0]-1, y), INK)
    for i, x in enumerate(starts):
        selected = i == 1
        c.cell(x, 106 if selected else 110, 36, 28 if selected else 20,
               ('fm', 'strn', 'ghost', 'rev', '', '')[i], selected, i >= 4)
        c.center(x+18, 91, str(i+1), GHOST)
        if i < 5:
            c.line((x+36, y, starts[i+1]-1, y), INK)
    c.line((starts[-1]+36, y, 284, y), INK)
    c.arrow(287, y, INK)
    c.text(295, 116, 'out', GHOST)
    # 'pos 38%' is 42px at 6x8: keep 36px cells, place a selected-only readout
    # beneath the cell, not cropped or squeezed into an impossible width.
    c.line((85, 135, 85, 141), SPOT)
    c.center(85, 145, 'pos 38%', SPOT)
    c.add_strip()
    c.footer('a edit / hold drag order', 'b back')
    return c


def rack():
    c = Page()
    rows = [('fm', 'ratio 2.00x', 'index 42%', .42),
            ('strn', 'pos 38%', 'mix 100%', .38),
            ('ghost', 'root 110hz', 'decay 87%', .87),
            ('rev', 'size 62%', 'mix 31%', .62)]
    for i in range(6):
        y = 70 + 24*i
        c.line((16, y+23, 303, y+23), FAINT)
        if i >= 4:
            c.center(160, y+8, '+ add module', FAINT)
            continue
        name, a, b, value = rows[i]
        selected = i == 1
        color = SPOT if selected else GHOST
        c.text(20, y+8, str(i+1), GHOST)
        c.text(36, y+8, name, SPOT if selected else INK)
        c.text(76, y+8, a, GHOST)
        c.text(150, y+8, b, GHOST)
        c.line((244, y+12, 303, y+12), FAINT)
        c.line((244, y+12, 244+round(value*59), y+12), color)
        if selected:
            c.d.rectangle((10, y+5, 11, y+18), fill=SPOT)
        if i == 2:
            c.arrow(11, y+12, SPOT)
    c.footer('a edit  y sigil', 'hold drag reorder')
    return c


def ribbon_focus():
    c = Page()
    # 30px cannot contain 'ghost' (30px advance) plus outline/padding. Use
    # 36x14 uniformly; all six slots still fit without changing bitmap type.
    starts = [24 + i*43 for i in range(6)]
    c.line((12, 79, 23, 79), INK)
    for i, x in enumerate(starts):
        c.cell(x, 72, 36, 14, ('fm', 'strn', 'ghost', 'rev', '', '')[i], i == 1, i >= 4)
        if i < 5:
            c.line((x+36, 79, starts[i+1]-1, 79), INK)
    c.line((275, 79, 284, 79), INK)
    c.arrow(287, 79, INK)
    c.text(295, 75, 'out', GHOST)
    c.line((85, 86, 85, 99), SPOT)
    c.line((16, 100, 303, 100), FAINT)
    c.text(20, 109, 'strn', SPOT)
    c.text(50, 109, '/ live grains', INK)
    for i, (name, value, amount) in enumerate((('position', '38%', .38), ('size', '720ms', .72),
                                               ('density', '14%', .14), ('mix', '100%', 1))):
        y = 129 + i*22
        c.text(20, y, name, GHOST)
        c.text(300, y, value, INK, right=True)
        c.line((20, y+13, 299, y+13), FAINT)
        x = 20 + round(amount*278)
        c.d.rectangle((x, y+10, x+1, y+15), fill=SPOT)
    c.footer('drag values', 'b chain')
    return c


def compact_text():
    c = Page()
    # Entire workspace cluster, including out, fits within 192x67 pixels.
    c.line((101, 112, 167, 112))
    c.line((198, 112, 205, 112, 207, 114, 207, 130,
            205, 132, 107, 132, 105, 134, 105, 161, 107, 163, 111, 163))
    c.line((148, 163, 179, 163))
    c.line((204, 163, 245, 163))
    c.arrow(248, 163)
    c.text(256, 159, 'out', GHOST)
    for x, y, name, selected in ((86, 108, 'fm', False), (170, 108, 'strn', True),
                                  (114, 159, 'ghost', False), (182, 159, 'rev', False)):
        c.text(x, y, name, SPOT if selected else INK)
        c.socket(x-3, y+4, selected)
        c.socket(x+len(name)*6+3, y+4, selected)
        if selected:
            c.line((x, y+11, x+23, y+11), SPOT)
    c.add_strip()
    c.footer('a trig / y sigil', 'tap / hold node')
    return c


VARIANTS = [('01_minimal', '01 / minimal object', '44x22 / cut contour', minimal),
            ('02_ribbon', '02 / serial ribbon', 'six slots / one signal path', ribbon),
            ('03_rack', '03 / rack list', '24px rows / values in place', rack),
            ('04_focus', '04 / ribbon + focus', 'serial overview / direct values', ribbon_focus),
            ('05_compact', '05 / compact text', 'control / no bodies', compact_text)]


def protected():
    paths = [p for folder in ('core', 'platform', 'assets', 'release')
             for p in (ROOT / folder).rglob('*') if p.is_file()]
    paths += [p for p in ROOT.glob('seance.*') if p.is_file()]
    paths += [ROOT / 'Makefile']
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}


def save(im, name):
    im.save(OUT / f'{name}_native.png')
    big = im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST)
    big.save(OUT / f'{name}_2x.png')
    assert big.resize(im.size, Image.Resampling.NEAREST).tobytes() == im.tobytes()


def main():
    before = protected()
    OUT.mkdir(parents=True, exist_ok=True)
    pages = {}
    metadata = {}
    for key, title, description, draw in VARIANTS:
        c = draw()
        assert c.im.size == (320, 240)
        # Stronger than a set-of-colors check: every non-ink pixel must equal
        # the paper at THIS row. No hidden fills, antialias or second wash.
        assert all(pixel in INKS or pixel == bg for pixel, bg in
                   zip(c.im.get_flattened_data(), BG.get_flattened_data()))
        assert c.im.crop((0, 0, 2, 240)).tobytes() == BG.crop((0, 0, 2, 240)).tobytes()
        save(c.im, key)
        pages[key] = c.im
        metadata[key] = {'caption': title, 'description': description,
                         'labels': c.labels, 'bodies': c.geometry}
    # Captions lie outside the screen. All five native screens also saved alone.
    sheet = Canvas(664, 852, INK)
    for i, (key, title, description, _) in enumerate(VARIANTS):
        x, y = 8+(i%2)*328, 8+(i//2)*280
        sheet.text(x+4, y+2, title, HI)
        sheet.text(x+4, y+16, description, GHOST)
        sheet.im.paste(pages[key], (x, y+32))
    sheet.text(356, 604, 'five workflow studies', HI)
    sheet.text(356, 628, '320x240 / bitmap 6x8', GHOST)
    sheet.text(356, 652, 'illustrative values only', GHOST)
    sheet.text(356, 676, 'no runtime or hitbox edits', GHOST)
    save(sheet.im, 'comparison')
    # A shorter side-by-side proof for the two workflow-first directions.
    pair = Canvas(664, 280, INK)
    for x, key, caption in ((8, '03_rack', '03 / rack list'), (336, '04_focus', '04 / ribbon + focus')):
        pair.text(x+4, 8, caption, HI)
        pair.im.paste(pages[key], (x, 32))
    save(pair.im, 'rack_focus')
    assert protected() == before, 'runtime/assets/releases changed during preview'
    report = {'status': 'offline mockups only; not runtime or hardware capture',
              'native': [320, 240], 'scale': 'nearest 2x',
              'palette': {'ink': INK, 'spot': SPOT, 'ghost': GHOST, 'faint': FAINT, 'highlight': HI},
              'checks': ['all text lowercase / 6x8 except binary gothic title', 'label bounds and no text overlap',
                         'exact per-row background or allowed ink', 'native size / nearest roundtrip',
                         'protected runtime/assets/release hashes unchanged'],
              'fit_adjustments': ['02: pos 38% is 42px wide, shown beneath the selected 36px cell',
                                  '04: equal 36x14 cells instead of 30x14, to fit ghost plus contour'],
              'limits': ['parameter values and bar normalization are illustrative, not engine readings',
                         'rack signal caret is a requested concept marker, not actual serial processing position',
                         'footer actions are proposals, not implemented mappings',
                         'no touch target, GPU, runtime or hardware validation'],
              'variants': metadata, 'protected_inputs': before}
    (OUT / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
    (OUT / 'README.txt').write_text(__doc__+'\nrun: python3 tools/render_chain_workflow_study.py\n'
        'font: unchanged core/ui/font.cpp; gothic: unchanged Gossip Med Pixel, Deborah Khodanovich 2025, SIL OFL 1.1.\n'
        '02: selected value lives below cell; 04: cells widened to 36px for full ghost label.\n'
        'all values, handles and rack caret are illustrative; no runtime behavior implied.\n')
    print(OUT / 'comparison_2x.png')
    print(f'pass: 5 native screens; exact inks/paper, text bounds, nearest scale; {len(before)} protected files unchanged')


if __name__ == '__main__':
    main()
