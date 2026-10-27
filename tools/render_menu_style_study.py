#!/usr/bin/env python3
"""Proposal only: post-splash menu is the shared visual reference, not a new skin.

No runtime edits or hardware capture. Example chain contents/selection only.
Headings: Seance Menu study derivative, Deborah Khodanovich 2025, SIL OFL 1.1;
original Gossip Med Pixel unchanged; see assets/fonts/{OFL.txt,README.md}.
"""
from pathlib import Path
import hashlib
import json
from PIL import Image, ImageDraw, ImageFont, ImageChops
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair
from generate_start_menu_labels import masks

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/menu_style_study'
GHOST = (126, 100, 119)
BRANCH = (174, 145, 165)
FACE = ImageFont.truetype(str(ROOT / 'assets/fonts/Gossip-MedPixel.otf'), 28)


def paper(width):
    im = Image.new('RGB', (width, 240))
    d = 239 ** 3
    for y in range(240):
        n = y * y * (717 - 2 * y)
        color = tuple((a * d + (b - a) * n + d // 2) // d
                      for a, b in zip((228, 194, 207), (211, 187, 206)))
        ImageDraw.Draw(im).line((0, y, width - 1, y), fill=color)
    return im


def heading_mask(text):
    box = FACE.getbbox(text)
    mask = Image.new('L', (box[2] - box[0] + 4, box[3] - box[1] + 4))
    ImageDraw.Draw(mask).text((2 - box[0], 2 - box[1]), text, font=FACE, fill=255)
    mask = mask.point(lambda p: 255 if p >= 128 else 0)
    assert mask.getbbox(), text
    return mask.crop(mask.getbbox())


class Page(Canvas):
    def __init__(self, width=320):
        super().__init__(width)
        self.im = paper(width)
        self.d = ImageDraw.Draw(self.im)
        self.hits = []

    def heading(self, x, y, text, color=INK):
        mask = heading_mask(text)
        box = (x, y, x + mask.width, y + mask.height)
        assert 0 <= x < box[2] <= self.im.width and 0 <= y < box[3] <= 240
        for old, name in self.labels:
            assert not overlaps(box, old), (text, name)
        self.labels.append((box, text))
        self.im.paste(color, (x, y), mask)

    def hit(self, name, box):
        assert 0 <= box[0] < box[2] <= 320 and 0 <= box[1] < box[3] <= 240
        for other, old in self.hits:
            assert not overlaps(box, old), (name, other)
        self.hits.append((name, box))

    def line(self, points, color=BRANCH):
        assert all(0 <= v < (self.im.width if i % 2 == 0 else 240)
                   for i, v in enumerate(points))
        self.d.line(points, fill=color)

    def marker(self, x, y, color=SPOT):
        self.line((x - 3, y - 3, x, y, x - 3, y + 3), color)

    def footer(self, left, right):
        self.text(20, 228, left, GHOST)
        self.text(self.im.width - 20, 228, right, GHOST, right=True)


def overlaps(a, b):
    return min(a[2], b[2]) > max(a[0], b[0]) and min(a[3], b[3]) > max(a[1], b[1])


def navigation():
    c = Page()
    c.heading(38, 20, 'go to')
    labels = ('patch table', 'sigils', 'sequencer', 'perform', 'patches', 'stress', 'exit')
    hints = ('build', 'draw', 'score', 'morph', 'save / load', 'test', 'hbmenu')
    c.line((52, 62, 52, 200))
    for i, (label, hint) in enumerate(zip(labels, hints)):
        y = 64 + i * 22
        color = SPOT if i == 0 else INK
        c.line((52, y + 4, 67, y + 4), SPOT if i == 0 else BRANCH)
        if i == 0:
            c.marker(68, y + 4)
        c.text(77, y, label, color)
        c.text(292, y, hint, GHOST, right=True)
        c.hit(label, (38, y - 6, 300, y + 16))
    c.footer('a open', 'b / select close')
    return c


def chains(context=False):
    c = Page()
    c.heading(24, 14, 'patch table')
    c.text(296, 22, 'patch01 / saved', GHOST, right=True)
    c.text(24, 54, 'chain', GHOST)
    for i in range(4):
        x = 96 + i * 48
        c.text(x, 54, str(i + 1), SPOT if i == 0 else GHOST)
        if i == 0:
            c.line((x - 2, 67, x + 7, 67), SPOT)
        c.hit('chain ' + str(i + 1), (x - 16, 44, x + 24, 74))
    if context:
        # A focused branch, not a pop-up card; background route is suppressed,
        # not painted over with a flat rectangle that would damage the gradient.
        c.text(32, 102, 'fm', GHOST)
        c.line((51, 106, 82, 106))
        c.text(94, 102, 'strn', SPOT)
        c.line((94, 115, 118, 115), SPOT)
        c.line((131, 106, 149, 106, 149, 92, 149, 197))
        actions = ('edit', 'bypass', 'copy', 'paste', 'delete', 'add')
        for i, label in enumerate(actions):
            y = 88 + i * 21
            c.line((149, y + 4, 164, y + 4), SPOT if i == 0 else BRANCH)
            c.text(176, y, label, SPOT if i == 0 else INK)
            if i == 0:
                c.marker(167, y + 4)
            c.hit(label, (160, y - 6, 302, y + 15))
        c.footer('a edit', 'b close branch')
    else:
        # Same serial chain semantics: these curves do not imply arbitrary graph routing.
        c.line((56, 102, 107, 102, 155, 110, 185, 110))
        c.line((233, 110, 269, 110, 269, 145, 61, 145, 61, 176, 83, 176))
        c.line((137, 176, 191, 176))
        c.line((233, 176, 293, 176, 293, 207))
        for x, y in ((177, 110), (75, 176), (183, 176)):
            c.marker(x, y, BRANCH)
        for x, y, name, chosen in ((32, 98, 'fm', False), (197, 106, 'strn', True),
                                   (95, 172, 'ghost', False), (203, 172, 'rev', False)):
            color = SPOT if chosen else INK
            c.text(x, y, name, color)
            w = len(name) * 6
            for sx in (x - 12, x + w + 12):
                c.d.ellipse((sx - 2, y + 2, sx + 2, y + 6), outline=color)
            if chosen:
                c.line((x, y + 13, x + w, y + 13), SPOT)
            c.hit(name + ' body', (x - 5, y - 10, x + w + 6, y + 24))
        c.text(281, 209, 'out', GHOST)
        c.text(24, 200, '+ add', GHOST)
        c.hit('add next free slot', (16, 190, 77, 220))
        c.text(101, 209, '4 / 6 modules', GHOST)
        c.footer('a trig   y sigil', 'tap / hold node')
    return c


def picker():
    c = Page()
    c.heading(38, 20, 'add module')
    c.text(292, 32, '01 / 05', GHOST, right=True)
    # A visible slice of the existing 20-item order, not a reduced module set.
    names = ('osc', 'noise', 'fm', 'motor', 'play', 'gran')
    hints = ('oscillator', 'noise source', 'fm operators', 'impulses', 'sample', 'grains')
    c.line((52, 63, 52, 207))
    for i, (name, hint) in enumerate(zip(names, hints)):
        y = 66 + i * 26
        color = SPOT if name == 'fm' else INK
        c.line((52, y + 4, 67, y + 4), color if name == 'fm' else BRANCH)
        if name == 'fm':
            c.marker(68, y + 4)
        c.text(77, y, name, color)
        c.text(140, y, hint, GHOST)
        c.hit(name, (38, y - 7, 280, y + 19))
    c.line((302, 68, 302, 206))
    c.d.ellipse((300, 68, 304, 72), fill=SPOT)
    c.hit('scroll position', (288, 62, 316, 217))
    c.footer('drag / tap add', 'b close')
    return c


def top():
    c = Page(400)
    c.heading(38, 20, 'chain one')
    c.text(372, 30, 'patch01', GHOST, right=True)
    c.text(38, 75, 'fm', INK)
    c.text(63, 75, '/', GHOST)
    c.text(81, 75, 'strn', SPOT)
    c.text(117, 75, '/', GHOST)
    c.text(135, 75, 'ghost', INK)
    c.text(177, 75, '/', GHOST)
    c.text(195, 75, 'rev', INK)
    c.text(225, 75, '/', GHOST)
    c.text(243, 75, 'out', GHOST)
    c.line((81, 87, 104, 87), SPOT)
    c.heading(38, 112, 'strn', SPOT)
    c.text(38, 154, 'selected / 02 of 06', GHOST)
    c.text(38, 178, 'live grain processor', INK)
    c.text(38, 202, 'fm in / ghost next', GHOST)
    c.footer('chain context', 'select / go to')
    return c


def digest_inputs():
    names = ['platform/3ds/ui_start_menu.cpp', 'platform/3ds/ui_chains.cpp',
             'platform/3ds/ui_nav.cpp', 'platform/3ds/ui_picker.cpp',
             'platform/3ds/ui_theme.cpp', 'platform/3ds/ui_state.h',
             'platform/3ds/start_menu_labels_data.h', 'assets/fonts/Gossip-MedPixel.otf',
             'platform/3ds/wordmark_morph.h', 'core/engine/units.cpp', 'seance.3dsx', 'seance.cia']
    return {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in names}


def main():
    before = digest_inputs()
    OUT.mkdir(parents=True, exist_ok=True)
    for text, mask in masks():
        assert ImageChops.difference(mask, heading_mask(text)).getbbox() is None
    ref = Image.open(ROOT / 'ui_mockups/start_menu_runtime/expanded_bottom_native.png').convert('RGB')
    # Unmarked full-height strips independently verify the actual runtime gradient.
    assert ImageChops.difference(ref.crop((0, 0, 10, 240)), paper(320).crop((0, 0, 10, 240))).getbbox() is None
    pages = {'navigation': navigation(), 'chains': chains(), 'branch': chains(True), 'picker': picker()}
    t = top()
    allowed = set(paper(320).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH}
    for name, c in dict(pages, top=t).items():
        assert set(c.im.get_flattened_data()) <= allowed
        c.im.save(OUT / f'{name}_native.png')
        big = c.im.resize((c.im.width * 2, 480), Image.Resampling.NEAREST)
        assert ImageChops.difference(big.resize(c.im.size, Image.Resampling.NEAREST), c.im).getbbox() is None
        big.save(OUT / f'{name}_2x.png')
    dual = pair(t.im, pages['chains'].im)
    dual.resize((800, 992), Image.Resampling.NEAREST).save(OUT / 'chains_dual_2x.png')
    # Reference plus four new screens, two rows with readable native-derived 2x.
    sheet = Image.new('RGB', (1000, 536), OUTSIDE)
    cap = Canvas(1000, 24, OUTSIDE)
    for x, label in ((8, 'reference / installed menu'), (340, 'proposal / go to'), (672, 'proposal / add module')):
        cap.text(x + 10, 8, label, (228, 194, 207))
    sheet.paste(cap.im, (0, 0))
    for x, im in ((8, ref), (340, pages['navigation'].im), (672, pages['picker'].im)):
        sheet.paste(im, (x, 24))
    cap2 = Canvas(1000, 24, OUTSIDE)
    cap2.text(18, 8, 'proposal / chains', (228, 194, 207))
    cap2.text(350, 8, 'proposal / local actions', (228, 194, 207))
    cap2.text(682, 8, 'same material / less chrome', (228, 194, 207))
    sheet.paste(cap2.im, (0, 272))
    sheet.paste(pages['chains'].im, (8, 296))
    sheet.paste(pages['branch'].im, (340, 296))
    rules = Page()
    rules.heading(38, 20, 'one language')
    for y, text in zip((74, 100, 126, 152, 178), ('soft paper / violet ink', 'gothic headings only', 'plain values and small labels', 'branches instead of boxes', 'module figures stay unique')):
        rules.text(24, y, text, GHOST if y != 152 else SPOT)
    rules.footer('visual proposal', 'not installed')
    sheet.paste(rules.im, (672, 296))
    sheet.resize((2000, 1072), Image.Resampling.NEAREST).save(OUT / 'comparison_2x.png')
    assert before == digest_inputs()
    report = {'status': 'noninteractive proposal; no runtime edits/build/deployment',
              'reference': 'actual post-splash ui_start_menu.cpp and saved C++ expanded frame',
              'font': 'Seance Menu study derivative; original 28ppem threshold128; SIL OFL 1.1',
              'data': 'illustrative fm / strn / ghost / rev chain, not engine telemetry',
              'checks': {'exact_menu_heading_raster': True, 'runtime_gradient_strip_exact': True,
                         'text_bounds_and_overlap': True, 'proposed_hit_bounds_and_overlap': True,
                         'native_palette_and_nearest2x': True, 'protected_inputs_unchanged': before},
              'pages': {name: {'text_count': len(c.labels), 'proposed_hit_regions': c.hits} for name, c in pages.items()},
              'limits': ['not a controller/hardware/GPU test', 'hit regions are design examples, not tested gestures',
                         'all 20 modules remain in existing scroll order; six visible here',
                         'navigation retains all seven existing choices; no new routes',
                         'body move/pan/socket wiring and six-slot capacity must survive implementation',
                         'context retains edit/bypass/copy/paste/delete/add; delete confirmation still required',
                         'sequencer cut cells/radial chooser and per-module figures are not redesigned']}
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    (OUT / 'README.txt').write_text(__doc__ + '\nrun: python3 tools/render_menu_style_study.py\n'
        'reference is saved actual C++ UI; other panels are Python visual proposals.\n'
        'native 320x240 / 400x240, integer nearest2x; labels/hits checked, no runtime testing.\n'
        'no sound, splash, startup menu, source font, binary or device changes.\n')
    print(OUT / 'comparison_2x.png')
    print('pass: menu gradient and headings, text bounds, hit bounds, palette, 2x, protected input hashes')


if __name__ == '__main__':
    main()
