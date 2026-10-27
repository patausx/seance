#!/usr/bin/env python3
"""One sequencer concept sheet; no runtime, audio, animation or device work.

Left: retained native C++ host frame from 2026-09-12, NOT a fresh build.
Right: authored Pillow layout using the same selected time=-23/96 state.
The measured scope and all 64 native 18x18 badges are reused unchanged.
Native 400x240 / 320x240, existing 6x8 and Seance Note digits, nearest 2x.
Font license: assets/fonts/OFL.txt. Run this file, not imported generators.
"""
from pathlib import Path
import re
from PIL import Image, ImageDraw
from render_universal_sigils_study import Surface, caption, OUTSIDE
from render_menu_style_study import overlaps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/sequencer_context_study'
REFERENCE = ROOT / 'ui_mockups/step_focus_runtime/ui/gray'
OLD_ROWS = (45, 83, 121, 159)
NEW_ROWS = (49, 88, 127, 166)
VALUES = tuple((-23, 23, 0)[i % 3] for i in range(16))


def surface(width):
    c = Surface(width, 'graphite')
    c.p = dict(c.p, inverse=(36, 36, 36))
    return c


def native_digits():
    src = (ROOT / 'platform/3ds/note_display_data.h').read_text()
    body = src.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    bitmap = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', body))
    glyphs = {}
    for char, offset, width, height, top in re.findall(
            r"\{'([0-9])', false, (\d+), (\d+), (\d+), (-?\d+)\}", src):
        offset, width, height, top = map(int, (offset, width, height, top))
        mask = Image.new('L', (width, height))
        stride = (width + 7) // 8
        for y in range(height):
            for x in range(width):
                if bitmap[offset + y * stride + x // 8] & (0x80 >> (x & 7)):
                    mask.putpixel((x, y), 255)
        glyphs[char] = (mask, top)
    assert len(glyphs) == 10
    return glyphs


DIGITS = native_digits()


def numeric(c, x, baseline, value):
    """Exact native inspector_gothic spacing/sign geometry, no font scaling."""
    start = x
    boxes = []
    for char in value:
        if char == '-':
            c.d.rectangle((x, baseline - 14, x + 12, baseline - 13), fill=c.p['spot'])
            boxes.append((x, baseline - 14, x + 13, baseline - 12))
            x += 18
        else:
            mask, top = DIGITS[char]
            c.im.paste(c.p['ink'], (x, baseline + top), mask)
            boxes.append((x, baseline + top, x + mask.width, baseline + top + mask.height))
            x += mask.width + 2
    box = (start, min(b[1] for b in boxes), x, max(b[3] for b in boxes))
    assert 0 <= box[0] < box[2] <= c.im.width and 0 <= box[1] < box[3] <= 240
    assert all(not overlaps(box, b) for b, _ in c.labels)
    c.labels.append((box, value))
    return x


def time_profile(c):
    c.txt(164, 65, 'selected track', 'ghost')
    c.txt(384, 65, '01-16', 'ghost', right=True)
    c.txt(162, 118, '0', 'ghost')
    for x in range(176, 384, 5):
        c.line((x, 122, x + 1, 122))
    for i, value in enumerate(VALUES):
        x, y = 178 + i * 13, 122 - round(value * 30 / 23)
        role = 'spot' if i == 0 else 'ink'
        # Discrete stored offsets, not interpolated motion or live event outcomes.
        c.line((x + 3, 122, x + 3, y), 'spot' if i == 0 else 'rail')
        c.line((x, y, x + 7, y), role)
        c.d.rectangle((x + 2, y - 1, x + 4, y + 1), fill=c.p[role])
        if i == 0:
            c.line((x, 166, x + 7, 166), 'spot')
    for group in range(4):
        x = 176 + group * 52
        c.line((x, 175, x + 47, 175))


def extent(c):
    c.txt(164, 184, 'track / 64', 'ghost')
    c.txt(384, 184, 'edit 01', 'spot', right=True)
    # This example is stopped. No fabricated playhead is added to either screen.
    c.d.rectangle((170, 197, 219, 209), fill=(41, 38, 46))
    c.line((170, 197, 219, 197), 'spot')
    c.line((170, 209, 219, 209), 'spot')
    for step in range(64):
        x = 172 + (step // 16) * 54 + (step % 16) * 3
        c.d.rectangle((x, 201, x + 1, 205), fill=c.p['ghost'])
        if step == 0:
            c.d.rectangle((x, 200, x + 1, 206), fill=c.p['spot'])
    for page in range(4):
        c.txt(172 + page * 54, 214, f'{page * 16 + 1:02}', 'spot' if page == 0 else 'ghost')


def upper():
    c = surface(400)
    c.txt(16, 14, 'edit 1 / play 1', 'spot')
    c.txt(384, 14, '120 bpm / 1.1 / stop', 'ghost', right=True)
    c.txt(16, 36, 'chain 01', 'ink')
    c.txt(92, 36, 'fm / out', 'ghost')
    c.txt(384, 36, '64 steps / reverse', 'ghost', right=True)
    c.line((16, 52, 384, 52))
    c.heading(16, 64, 'time', 'spot')
    end = numeric(c, 16, 124, '-23')
    c.txt(end + 5, 122, '/96', 'ghost')
    c.txt(16, 145, 'early / stored', 'ghost')
    c.txt(16, 166, 'a#2 / lev 18', 'ghost')
    c.txt(16, 181, 'on / all / x1', 'ink')
    c.txt(16, 196, 'locks 2/4', 'ghost')
    c.txt(16, 215, 'step 01 / 64', 'spot')
    time_profile(c)
    extent(c)
    c.txt(16, 232, 'l tools / r mode', 'ghost')
    c.txt(384, 232, 'hold b setup', 'ghost', right=True)
    return c.im


def lower(before):
    c = surface(320)
    # Pixel-exact retained measured scope: same full width, height and amplitude.
    c.im.paste(before.crop((0, 0, 320, 40)), (0, 0))
    targets = []
    for chain, (old_y, y) in enumerate(zip(OLD_ROWS, NEW_ROWS)):
        c.txt(8, y - 8, f'chain {chain + 1:02}', 'spot' if chain == 0 else 'ghost')
        if chain == 0:
            c.txt(312, y - 8, '64 / reverse', 'ghost', right=True)
        for step in range(16):
            x = 8 + step * 19
            badge = before.crop((x, old_y, x + 18, old_y + 18))
            c.im.paste(badge, (x, y))
            assert c.im.crop((x, y, x + 18, y + 18)).tobytes() == badge.tobytes()
            c.labels.append(((x, y, x + 18, y + 18), f'badge {chain}:{step}'))
            # All 64 visible trigs are on in the retained host fixture.
            # Replace the repeated miniature timing rails with one event mark.
            c.d.rectangle((x + 7, y + 22, x + 9, y + 23),
                          fill=c.p['spot' if chain == 0 and step == 0 else 'ghost'])
            box = (x, y, x + 19, y + 31)
            assert 0 <= box[0] < box[2] <= 312 and box[3] <= 197
            assert all(not overlaps(box, other) for other in targets)
            targets.append(box)
        # Four-step visual grouping without narrowing the existing 19px pitch.
        for beat in range(4):
            x = 8 + beat * 76
            c.line((x, y + 28, x + 73, y + 28))
    c.txt(8, 201, 'time', 'spot')
    c.txt(64, 201, '1:01', 'ghost')
    c.txt(145, 201, '01-16', 'ghost')
    c.txt(312, 201, '-23/96', 'ink', right=True)
    # Same signed domain; larger travel and an explicit fixed zero.
    # The zero caption is a guide, NOT a newly promised tap-to-reset button.
    c.line((8, 220, 311, 220))
    c.line((8, 220, 160, 220), 'spot')
    c.line((160, 215, 160, 225), 'ghost')
    c.line((8, 217, 8, 223))
    c.line((311, 217, 311, 223))
    c.d.rectangle((6, 216, 10, 224), fill=c.p['spot'])
    c.txt(8, 232, 'early', 'ghost')
    c.txt(157, 232, '0', 'ghost')
    c.txt(312, 232, 'late', 'ghost', right=True)
    assert c.im.crop((0, 0, 320, 40)).tobytes() == before.crop((0, 0, 320, 40)).tobytes()
    assert len(targets) == 64
    return c.im


def main():
    with Image.open(REFERENCE / '0070_layer_5_top.ppm') as im:
        before_top = im.convert('RGB')
    with Image.open(REFERENCE / '0070_layer_5_bottom.ppm') as im:
        before_bottom = im.convert('RGB')
    assert before_top.size == (400, 240) and before_bottom.size == (320, 240)
    after_top, after_bottom = upper(), lower(before_bottom)
    assert after_top.size == (400, 240) and after_bottom.size == (320, 240)
    sheet = Image.new('RGB', (864, 652), OUTSIDE)
    caption(sheet, 16, 12, 'seance / sequencer / context study')
    for x, title, source, top, bottom in (
            (16, '01 / existing layout', 'saved native host / 12 sep', before_top, before_bottom),
            (448, '02 / context + score', 'proposal / same selected step', after_top, after_bottom)):
        caption(sheet, x, 36, title)
        caption(sheet, x, 50, source)
        sheet.paste(top, (x, 64))
        sheet.paste(bottom, (x + 40, 320))
    caption(sheet, 16, 579, 'note remains dominant in the time layer')
    caption(sheet, 16, 595, '64 tiny timing rails / anonymous score rows')
    caption(sheet, 16, 611, 'retained reference, not a new runtime capture')
    caption(sheet, 448, 579, 'time first / clearer profile / whole-track overview')
    caption(sheet, 448, 595, 'chain labels / same badges + scope / signed rail')
    caption(sheet, 448, 611, 'same 16 columns and 19x31 step touch bands')
    caption(sheet, 16, 637, 'static layout proposal / native screens at 2x / no runtime changes')
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'sequence-context-v1-2x.png'
    sheet.resize((1728, 1304), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))
    print('one sheet; text bounds/nonoverlap, 64 touch bands, exact scope + badge preservation passed')


if __name__ == '__main__':
    main()
