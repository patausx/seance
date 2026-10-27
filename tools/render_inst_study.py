#!/usr/bin/env python3
"""One authored INST concept sheet, not runtime UI or measured audio.

Fresh Pillow artwork only: piano / violin / drums, native 400x240 + 320x240.
Current project 6x8 glyphs, existing Seance Note masks and 28ppem menu face.
Font copyright/license: assets/fonts/{README.md,OFL.txt}. No old captures,
engine calls, generated sound, device operations or production-source writes.
"""
from pathlib import Path
import hashlib
import math
import re
from PIL import Image, ImageDraw, PngImagePlugin
from render_module_geometry_study import Canvas
from render_menu_style_study import heading_mask, overlaps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/inst_study'
TARGET = OUT / 'inst-piano-violin-drums-v1-2x.png'
OUTSIDE = (20, 20, 22)
# Exact static graphite roles from ui_palette.h, not the old gray study skin.
COLORS = dict(paper=(36, 36, 36), cell=(48, 48, 48), ink=(244, 244, 244),
              spot=(181, 154, 232), ghost=(184, 184, 184), rail=(112, 112, 112))
FIGURE = (122, 58, 387, 215)
INPUTS = ('core/ui/font.cpp', 'platform/3ds/note_display_data.h',
          'platform/3ds/ui_palette.h', 'assets/fonts/Gossip-MedPixel.otf',
          'tools/render_module_geometry_study.py', 'tools/render_menu_style_study.py')


def input_hashes():
    return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in INPUTS}


def load_note_masks():
    source = (ROOT / 'platform/3ds/note_display_data.h').read_text()
    body = source.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', body))
    result = {}
    for char, small, offset, width, height, top in re.findall(
            r"\{'([a-z0-9])', (true|false), (\d+), (\d+), (\d+), (-?\d+)\}", source):
        offset, width, height, top = map(int, (offset, width, height, top))
        mask = Image.new('L', (width, height))
        stride = (width + 7) // 8
        for y in range(height):
            for x in range(width):
                if data[offset + y * stride + x // 8] & (0x80 >> (x & 7)):
                    mask.putpixel((x, y), 255)
        result[char, small == 'true'] = mask, top
    assert len(result) == 28
    return result


GLYPHS = load_note_masks()


class Page(Canvas):
    def __init__(self, width):
        super().__init__(width, 240, COLORS['paper'])
        self.hits = []
        self.art_count = 0

    def txt(self, x, y, text, role='ink', scale=1, right=False):
        self.text(x, y, text, COLORS[role], scale, right)

    def mask(self, x, y, mask, label, role='ink', scale=1):
        if scale != 1:
            mask = mask.resize((mask.width * scale, mask.height * scale), Image.Resampling.NEAREST)
        box = (x, y, x + mask.width, y + mask.height)
        assert 0 <= x < box[2] <= self.im.width and 0 <= y < box[3] <= 240, (label, box)
        assert all(not overlaps(box, old) for old, _ in self.labels), label
        self.labels.append((box, label))
        self.im.paste(COLORS[role], (x, y), mask)
        return box[2]

    def heading(self, x, y, text, role='ink', scale=1):
        return self.mask(x, y, heading_mask(text), text, role, scale)

    def glyph(self, x, baseline, char, small=False, role='ink', scale=1):
        mask, top = GLYPHS[char, small]
        return self.mask(x, baseline + top * scale, mask, char, role, scale)

    def melodic(self, note):
        # Same note/octet relationship as note_display.h, integer 2x only.
        x = self.glyph(16, 116, note[0], scale=2)
        self.glyph(x + 12, 122, note[1], small=True, role='spot', scale=2)

    def numeric(self, value):
        x = 16
        for char in str(value):
            x = self.glyph(x, 213, char) + 2
        self.txt(x + 2, 202, 'ms', 'ghost')

    def hit(self, name, box):
        assert 0 <= box[0] < box[2] <= 320 and 0 <= box[1] < box[3] <= 240
        assert all(not overlaps(box, other) for _, other in self.hits), name
        self.hits.append((name, box))

    def line(self, points, role='rail', width=1):
        self.d.line(points, fill=COLORS[role], width=width)


class Figure:
    """Bounded schematic primitives; counts are Pillow calls, NOT GPU budgets."""
    def __init__(self, page):
        self.c = page

    def points(self, points):
        result = [(round(x), round(y)) for x, y in points]
        x0, y0, x1, y1 = FIGURE
        assert all(x0 <= x < x1 and y0 <= y < y1 for x, y in result), result
        self.c.art_count += 1
        return result

    def line(self, points, role='ink', width=1):
        self.c.d.line(self.points(points), fill=COLORS[role], width=width)

    def polygon(self, points, role='ink', fill='paper'):
        self.c.d.polygon(self.points(points), fill=COLORS[fill], outline=COLORS[role])

    def ellipse(self, box, role='ink', fill=None, width=1):
        corners = self.points(((box[0], box[1]), (box[2], box[3])))
        self.c.d.ellipse((*corners[0], *corners[1]), outline=COLORS[role],
                         fill=COLORS[fill] if fill else None, width=width)

    def rect(self, box, role='ink', fill=None):
        corners = self.points(((box[0], box[1]), (box[2], box[3])))
        self.c.d.rectangle((*corners[0], *corners[1]), outline=COLORS[role],
                           fill=COLORS[fill] if fill else None)


def cubic(a, b, c, d, steps=16):
    points = []
    for i in range(steps + 1):
        t = i / steps
        weights = ((1 - t)**3, 3 * (1 - t)**2 * t, 3 * (1 - t) * t * t, t**3)
        points.append(tuple(sum(p[k] * w for p, w in zip((a, b, c, d), weights)) for k in (0, 1)))
    return points


def piano(c):
    f = Figure(c)
    # Open cutaway: tapered strings, hammer line, two octaves of keys.
    f.line(((134, 67), (376, 112), (376, 139)), 'ghost')
    f.line(((134, 67), (134, 140)), 'ghost')
    for i in range(14):
        x = 148 + i * 17
        y = 73 + round(i * 44 / 13)
        f.ellipse((x - 1, y - 1, x + 1, y + 1), 'rail')
        for dx in (-2, 2):
            f.line(((x + dx, y + 4), (x + dx, 138)), 'rail')
        if i == 7:
            # An authored resonance pose, not PCM or a physical-string solution.
            points = [(x + 2.5 * math.sin(t * math.tau * 2.5) * math.sin(math.pi * t),
                       y + 3 + t * (135 - y)) for t in (j / 24 for j in range(25))]
            f.line(points, 'spot')
            f.rect((x - 4, 133, x + 4, 137), 'spot', 'spot')
            f.line(((x, 138), (x, 145)), 'spot')
        else:
            f.line(((x, y + 3), (x, 138)), 'ghost')
    for i in range(14):
        x = 140 + i * 17
        pressed = i == 7
        f.rect((x, 144 + 2 * pressed, x + 16, 193 + 2 * pressed),
               'spot' if pressed else 'ghost', 'spot' if pressed else 'cell')
        if not pressed:
            f.line(((x + 3, 189), (x + 13, 189)), 'rail')
    for i in range(13):
        if i % 7 in (2, 6):
            continue
        x = 140 + (i + 1) * 17 - 5
        f.rect((x, 144, x + 9, 171), 'ink', 'paper')
        f.line(((x + 2, 168), (x + 7, 168)), 'rail')
    c.txt(140, 204, 'c3', 'ghost')
    c.txt(260, 204, 'c4', 'spot')
    c.txt(378, 204, 'c5', 'ghost', right=True)


def violin(c):
    f = Figure(c)
    cx = 249
    # Mirrored bouts with a narrow waist: original contour, not clip art.
    left = cubic((cx - 10, 99), (cx - 34, 87), (cx - 55, 104), (cx - 40, 126))
    left += cubic((cx - 40, 126), (cx - 33, 133), (cx - 25, 130), (cx - 25, 139))[1:]
    left += cubic((cx - 25, 139), (cx - 26, 147), (cx - 38, 146), (cx - 41, 152))[1:]
    left += cubic((cx - 41, 152), (cx - 61, 183), (cx - 29, 202), (cx, 200))[1:]
    body = left + [(2 * cx - x, y) for x, y in reversed(left)]
    f.polygon(body, 'ink', 'cell')
    f.line([(cx + (x - cx) * .88, 151 + (y - 151) * .91) for x, y in body], 'rail')
    f.polygon(((242, 65), (255, 65), (258, 130), (239, 130)), 'ghost', 'paper')
    f.ellipse((242, 58, 255, 68), 'ink', 'paper')
    f.line(((247, 59), (252, 62), (248, 65)), 'ghost')
    for y, side in ((72, -1), (80, 1), (87, -1), (93, 1)):
        x = cx + side * 9
        f.line(((cx + side * 5, y), (x + side * 4, y)), 'ghost')
        f.ellipse((x - 2, y - 1, x + 2, y + 2), 'ink', 'ink')
    for side in (-1, 1):
        x = cx + side * 24
        path = cubic((x + 3 * side, 140), (x - 10 * side, 143),
                     (x + 10 * side, 157), (x - 4 * side, 170))
        f.line(path, 'ink', 2)
        f.ellipse((x + 3 * side - 2, 137, x + 3 * side + 2, 141), 'ink')
        f.line(((x - 5, 155), (x + 5, 154)), 'ink')
    f.polygon(((239, 178), (259, 178), (253, 193), (245, 193)), 'ghost', 'paper')
    f.line(((231, 156), (266, 156)), 'ink', 2)
    for i in range(4):
        x = 243 + i * 4
        if i == 2:
            f.line([(x + math.sin(j / 31 * math.pi) * math.sin(j / 31 * math.tau * 3),
                     68 + j * 118 / 31) for j in range(32)], 'spot')
        else:
            f.line(((x, 68), (x, 185)), 'ghost')
    # Bow stick + hair; a finite active pose rather than idle decorative motion.
    f.line(cubic((159, 191), (224, 148), (291, 106), (347, 69)), 'ink', 2)
    f.line(((162, 195), (346, 75)), 'spot')
    f.polygon(((158, 190), (168, 183), (173, 189), (163, 197)), 'ink', 'paper')
    f.line(((346, 69), (346, 75)), 'ink')
    f.ellipse((247, 135, 253, 141), 'spot', 'spot')


def drums(c):
    f = Figure(c)
    # Rear stands first; filled heads occlude them, not a wiring/network diagram.
    for x, top, foot in ((168, 76, 192), (353, 91, 193)):
        f.line(((x, top), (x, foot)), 'rail')
        f.line(((x - 13, foot + 8), (x, foot), (x + 13, foot + 8)), 'rail')
    for box in ((138, 70, 198, 83), (324, 84, 382, 98)):
        f.ellipse(box, 'ink', 'paper')
        x = (box[0] + box[2]) // 2
        y = (box[1] + box[3]) // 2
        f.ellipse((x - 7, y - 3, x + 7, y + 3), 'ghost')
        f.ellipse((x - 1, y - 1, x + 1, y + 1), 'ink', 'ink')
    f.line(((324, 101), (382, 101)), 'rail')
    for x, y, r in ((231, 96, 23), (292, 111, 25)):
        f.rect((x - r, y, x + r, y + 23), 'ghost', 'cell')
        f.ellipse((x - r, y + 12, x + r, y + 32), 'ghost', 'cell')
        f.ellipse((x - r, y - 10, x + r, y + 10), 'ink', 'paper')
        f.ellipse((x - r + 5, y - 6, x + r - 5, y + 6), 'rail')
        for dx in (-r + 4, r - 4):
            f.line(((x + dx, y + 11), (x + dx, y + 20)), 'rail')
    f.ellipse((212, 123, 287, 198), 'ink', 'paper')
    f.ellipse((217, 128, 282, 193), 'ghost')
    f.ellipse((254, 168, 266, 180), 'rail')
    for angle in range(0, 360, 45):
        t = math.radians(angle)
        x, y = 249 + 38 * math.cos(t), 160 + 38 * math.sin(t)
        f.line(((x, y), (249 + 34 * math.cos(t), 160 + 34 * math.sin(t))), 'ghost')
    f.line(((221, 190), (213, 204)), 'ghost')
    f.line(((278, 190), (289, 204)), 'ghost')
    f.polygon(((244, 199), (254, 199), (258, 210), (240, 210)), 'rail', 'cell')
    # The illustrated selected hit is the snare, not all pieces at once.
    f.line(((171, 164), (171, 194)), 'ghost')
    f.line(((157, 206), (171, 194), (185, 206)), 'ghost')
    f.rect((140, 135, 202, 156), 'ghost', 'cell')
    f.ellipse((140, 145, 202, 168), 'ghost', 'cell')
    for x in range(146, 199, 9):
        f.line(((x, 150), (x, 159)), 'rail')
    f.ellipse((139, 122, 203, 146), 'spot', 'paper', 2)
    f.ellipse((145, 126, 197, 142), 'spot')
    f.ellipse((154, 129, 188, 139), 'rail')
    f.ellipse((168, 132, 174, 136), 'spot', 'spot')
    f.line(((149, 112), (170, 133)), 'ink', 2)
    f.line(((189, 111), (176, 127)), 'ghost', 2)


DATA = (
    dict(name='piano', number=1, family='hammer + strings', note='c4', state='tail / 01',
         release=640, length='480ms', length_pos=.24, attack='8ms', attack_pos=.016,
         tone=42, level=78, note_pos=.5, figure=piano,
         captions=('key strike / string resonance', 'one voice, illustrated in its tail')),
    dict(name='violin', number=2, family='bowed strings', note='a4', state='sustain / 01',
         release=480, length='1.2s', length_pos=.6, attack='160ms', attack_pos=.32,
         tone=56, level=72, note_pos=.6875, figure=violin,
         captions=('bow pressure / held string', 'same controls, a different gesture')),
    dict(name='drums', number=6, family='acoustic kit', note='snare', state='hit / 02',
         release=180, length='one-shot', length_pos=None, attack='0ms', attack_pos=0,
         tone=68, level=84, note_pos=.25, figure=drums,
         captions=('snare strike / membrane rings', 'one active piece, not a flashing kit')),
)


def upper(s):
    c = Page(400)
    c.heading(16, 12, s['name'])
    c.txt(384, 20, 'inst / ch01 / 01', 'ghost', right=True)
    c.txt(16, 43, s['family'], 'ghost')
    c.txt(16, 59, 'sound' if s['name'] == 'drums' else 'note', 'spot')
    if s['name'] == 'drums':
        c.heading(16, 85, 'snare', scale=2)
    else:
        c.melodic(s['note'])
    c.txt(16, 139, s['state'], 'ghost')
    c.txt(16, 165, 'release', 'spot')
    c.numeric(s['release'])
    s['figure'](c)
    c.txt(16, 231, 'inst / ghost / rev', 'ghost')
    c.txt(384, 231, 'voice view', 'ghost', right=True)
    return c


def control(c, i, name, value, amount):
    x = 16 + (i % 2) * 152
    y = 108 + (i // 2) * 38
    selected, disabled = i == 3, amount is None
    role = 'spot' if selected else 'ghost' if disabled else 'ink'
    c.txt(x, y, name, role)
    c.txt(x + 134, y, value, role, right=True)
    ry, left, right = y + 19, x + 4, x + 130
    c.line((left, ry, right, ry), 'rail')
    for at in (left, right):
        c.line((at, ry - 2, at, ry + 2), 'rail')
    if not disabled:
        at = left + round(amount * (right - left))
        c.d.rectangle((at - 3, ry - 3, at + 3, ry + 3),
                      outline=COLORS[role], fill=COLORS['paper'])
    c.hit(name, (x - 4, y - 4, x + 138, y + 31))


def lower(s):
    c = Page(320)
    c.heading(16, 12, 'inst')
    c.txt(76, 20, 'ch01 / 01', 'ghost')
    c.txt(304, 20, 'byp', 'ghost', right=True)
    c.hit('bypass', (266, 5, 314, 36))
    c.txt(16, 46, 'instrument', 'ghost')
    c.txt(304, 46, f"{s['number']:02} / 06", 'ghost', right=True)
    c.line((25, 69, 20, 74, 25, 79), 'ghost')
    c.heading(42, 64, s['name'], 'spot')
    c.line((295, 69, 300, 74, 295, 79), 'ghost')
    c.hit('previous', (8, 58, 36, 96))
    c.hit('choose instrument', (36, 58, 280, 96))
    c.hit('next', (280, 58, 312, 96))
    fields = (('sound' if s['name'] == 'drums' else 'note', s['note'], s['note_pos']),
              ('length', s['length'], s['length_pos']),
              ('attack', s['attack'], s['attack_pos']),
              ('release', f"{s['release']}ms", s['release'] / 2000),
              ('tone', f"{s['tone']}%", s['tone'] / 100),
              ('level', f"{s['level']}%", s['level'] / 100))
    for i, field in enumerate(fields):
        control(c, i, *field)
    c.txt(16, 231, 'b back', 'ghost')
    c.txt(124, 231, 'a play', 'ghost')
    c.txt(304, 231, 'l: mod >', 'ghost', right=True)
    c.hit('back', (8, 222, 88, 240))
    c.hit('audition', (112, 222, 202, 240))
    c.hit('modulation', (218, 222, 314, 240))
    assert len(c.hits) == 13
    return c


def caption(im, x, y, text, role='ghost', right=False):
    c = Canvas(im.width, im.height, OUTSIDE)
    c.im, c.d = im, ImageDraw.Draw(im)
    c.text(x, y, text, COLORS[role], right=right)


def main():
    if TARGET.exists():
        raise SystemExit('preserve the existing sheet; choose a new version')
    before = input_hashes()
    sheet = Image.new('RGB', (1280, 656), OUTSIDE)
    caption(sheet, 20, 14, 'seance / inst / three instrument voices', 'ink')
    caption(sheet, 1260, 14, 'visual study 01 / graphite', right=True)
    pages = []
    control_boxes = None
    for i, s in enumerate(DATA):
        x = 20 + i * 420
        caption(sheet, x, 40, f"0{i + 1} / {s['name']}", 'spot')
        top, bottom = upper(s), lower(s)
        assert top.im.size == (400, 240) and bottom.im.size == (320, 240)
        assert set(top.im.get_flattened_data()) | set(bottom.im.get_flattened_data()) <= set(COLORS.values())
        boxes = [box for _, box in bottom.hits]
        if control_boxes is not None:
            assert boxes == control_boxes, 'instrument must not move controls'
        control_boxes = boxes
        sheet.paste(top.im, (x, 60))
        sheet.paste(bottom.im, (x + 40, 320))
        for row, line in enumerate(s['captions']):
            caption(sheet, x, 582 + row * 17, line)
        pages.append((top, bottom))
    caption(sheet, 20, 631, 'static concept / illustrative states / native 400x240 + 320x240 at 2x / not runtime ui')
    assert before == input_hashes(), 'reference source changed during rendering'
    result = sheet.resize((2560, 1312), Image.Resampling.NEAREST)
    assert result.resize(sheet.size, Image.Resampling.NEAREST).tobytes() == sheet.tobytes()
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text('description', __doc__)
    metadata.add_text('source_sha256', '\n'.join(f'{key} {value}' for key, value in before.items()))
    OUT.mkdir(parents=True, exist_ok=True)
    result.save(TARGET, pnginfo=metadata)
    with Image.open(TARGET) as decoded:
        assert decoded.size == result.size and decoded.convert('RGB').tobytes() == result.tobytes()
    print(TARGET.relative_to(ROOT))
    print('one sheet; fresh authored figures; no old screenshots; no runtime/audio/device changes')
    print('pass: native sizes, text bounds/nonoverlap, figure bounds, 13 stable proposed targets, graphite roles, lossless nearest2x + decode')
    print('pillow figure primitives, not GPU cost:', [top.art_count for top, _ in pages])
    print('bytes:', TARGET.stat().st_size)


if __name__ == '__main__':
    main()
