#!/usr/bin/env python3
"""Three static upper-screen treatments above the same blurred navigation.

One illustrative pose each: four-part seal, pendulum, and a beat-first counter.
No animation, runtime capture, transport implementation or application changes.
"""
from pathlib import Path
import math
from PIL import Image
from render_go_to_sequencer_study import SOURCE
from render_go_to_overlay_study import dim
from render_go_to_unified_study import lower, beat_marks
from render_go_to_clock_study import digit_mask
from render_main_screen_unified_info_study import blur
from render_main_screen_centered_study import canvas, caption, OUTSIDE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/go_to_overlay_study'


def background():
    with Image.open(SOURCE) as source:
        source = source.convert('RGB')
        assert source.size == (400, 488)
        shared = Image.new('RGB', (400, 496), (36, 36, 36))
        shared.paste(source.crop((0, 0, 400, 240)), (0, 0))
        shared.paste(source.crop((40, 248, 360, 488)), (40, 256))
    return dim(blur(shared))


def rotate(point, turn, center=(200, 118)):
    x, y = point
    for _ in range(turn):
        x, y = -y, x
    return center[0] + x, center[1] + y


def seal(image):
    c = canvas(image)
    cx, cy = 200, 118
    # Broken circular rim belongs to the symbol, not a panel or new UI frame.
    for start in (8, 98, 188, 278):
        points = [(round(cx + 88 * math.cos(math.radians(a))),
                   round(cy + 88 * math.sin(math.radians(a))))
                  for a in range(start, start + 75)]
        c.d.line(points, fill=c.p['rail'], width=1)
    # Four pointed, separate leaves. Top/right have passed, bottom is active,
    # left is waiting. Geometry is a pose, not a fabricated motion sequence.
    petal = ((0, -80), (-24, -53), (-11, -32), (0, -43),
             (11, -32), (24, -53), (0, -80))
    for quarter in range(4):
        role = 'spot' if quarter == 2 else 'ink' if quarter < 2 else 'ghost'
        active = quarter == 2
        leaf = [rotate(p, quarter) for p in petal]
        c.d.line(leaf, fill=c.p[role], width=2 if active else 1)
        spine = [rotate(p, quarter) for p in ((0, -80), (0, -62), (0, -43))]
        c.d.line(spine, fill=c.p[role], width=1)
        for side in (-1, 1):
            fork = [rotate(p, quarter) for p in ((side * 24, -53), (side * 10, -57), (0, -62))]
            c.d.line(fork, fill=c.p[role], width=1)
        if active:
            gem = [rotate(p, quarter) for p in ((0, -80), (-5, -70), (0, -63), (5, -70))]
            c.d.polygon(gem, fill=c.p['spot'])
    # Existing seance branding at native resolution, not a modulation source.
    with Image.open(ROOT / 'assets/branding/sigil_icon_48_core_mask.png') as mark:
        mark = mark.convert('L')
        assert mark.size == (48, 48)
        c.im.paste(c.p['ink'], (cx - 24, cy - 24), mark)
    return c.im


def pendulum(image):
    c = canvas(image)
    px, py, length = 200, 40, 151
    def position(angle, radius=length):
        a = math.radians(angle)
        return round(px + radius * math.sin(a)), round(py + radius * math.cos(a))
    # A restrained sweep and center reference; no text/status dashboard.
    arc = [position(a) for a in range(-42, 43)]
    c.d.line(arc, fill=c.p['rail'], width=1)
    for angle in (-40, -20, 0, 20, 40):
        c.d.line((position(angle, length - 3), position(angle, length + 4)),
                 fill=c.p['ghost'] if angle == 0 else c.p['rail'], width=1)
    for y in range(57, 193, 9):
        c.d.line((200, y, 200, y + 2), fill=c.p['rail'])
    angle = 24  # illustrative off-center pose, not a measured engine phase
    a = math.radians(angle)
    ux, uy, vx, vy = math.sin(a), math.cos(a), math.cos(a), -math.sin(a)
    def local(distance, side=0):
        return round(px + ux * distance + vx * side), round(py + uy * distance + vy * side)
    c.d.line((local(-12), local(length - 17)), fill=c.p['ink'], width=2)
    arrow = [local(length + 2), local(length - 21, -9), local(length - 15),
             local(length - 21, 9)]
    c.d.polygon(arrow, fill=c.p['spot'])
    c.d.ellipse((px - 5, py - 5, px + 5, py + 5), outline=c.p['ink'], width=1)
    c.d.rectangle((px - 1, py - 1, px + 1, py + 1), fill=c.p['spot'])
    return c.im


def beat_first(image):
    c = canvas(image)
    mask = digit_mask('3')
    mask = mask.resize((mask.width * 2, mask.height * 2), Image.Resampling.NEAREST)
    assert mask.size == (96, 168)
    c.im.paste(c.p['spot'], ((400 - mask.width) // 2, 29), mask)
    c.txt(268, 106, 'bar', 'ghost')
    c.heading(268, 124, '012', 'ink')
    beat_marks(c, 200, 219)
    return c.im


def main():
    shared = background()
    top_base = shared.crop((0, 0, 400, 240))
    bottom = lower(shared.crop((40, 256, 360, 496)), current='sequencer')
    variants = (
        ('seal', seal, '01 / four-part living seal', 'third part accented / one still pose'),
        ('pendulum', pendulum, '02 / rhythmic pendulum', 'one off-center pose / motion not rendered'),
        ('beat', beat_first, '03 / beat first', 'large beat / small bar / example 4/4'),
    )
    OUT.mkdir(parents=True, exist_ok=True)
    for name, draw, title, note in variants:
        top = draw(top_base)
        assert top.size == (400, 240) and bottom.size == (320, 240)
        sheet = Image.new('RGB', (432, 590), OUTSIDE)
        caption(sheet, 16, 12, title)
        caption(sheet, 16, 29, 'same blurred sequencer / same lower menu')
        sheet.paste(top, (16, 48))
        sheet.paste(bottom, (56, 304))
        caption(sheet, 16, 557, note)
        caption(sheet, 16, 575, 'static concept / not animation or runtime')
        path = OUT / f'go-to-upper-{name}-2x.png'
        sheet.resize((864, 1180), Image.Resampling.NEAREST).save(path)
        print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
