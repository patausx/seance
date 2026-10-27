#!/usr/bin/env python3
"""Two requested static overlay boards, not runtime or device/performance proof.

Reuses unchanged 320x240 host captures. The gesture is illustrative; no input is
replayed. Alpha compositing and 1px rings are done at native size, then nearest x2.
Existing outputs are refused; application, captures and earlier studies untouched.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_module_geometry_study import Canvas

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/stylus_ring_study'
CAPTURE = ROOT / 'ui_mockups/graphite_accent_runtime/ui'
BACKGROUND = 'osc/02_tri_saw_mod_bottom.ppm'
OUTSIDE = (24, 20, 29)
CAPTION = (226, 190, 206)
MUTED = (165, 145, 167)
CONTACT = (174, 148)
RADIUS = 10
FILL = .10
EDGE = .82
VARIANTS = (
    ('01 / clear ring', 'no trail / quiet contact'),
    ('02 / ring echoes', '3 fading rings / proposed 140ms'),
    ('03 / fine thread', '1px fading line / proposed 160ms'),
    ('04 / dotted trail', 'spaced points / proposed 140ms'),
)


def alpha(value):
    return round(255 * value)


def ring(image, center, radius, color, edge=EDGE, fill=FILL):
    layer = Image.new('RGBA', image.size)
    draw = ImageDraw.Draw(layer)
    x, y = center
    draw.ellipse((x-radius, y-radius, x+radius, y+radius),
                 fill=(*color, alpha(fill)), outline=(*color, alpha(edge)), width=1)
    return Image.alpha_composite(image, layer)


def position(t):
    # Same illustrative path and contact in every comparison; ~52px total.
    return (round(CONTACT[0] - 52 * (1-t)),
            round(CONTACT[1] + 9 * math.sin(math.pi * (1-t)) ** 2))


def overlay(source, color, variant):
    image = source.convert('RGBA')
    if variant == 1:
        # Discrete afterimages: no additive bloom, center fill or large ripple.
        for t, radius, opacity in ((.16, 6, .12), (.44, 8, .24), (.71, 9, .42)):
            image = ring(image, position(t), radius, color, opacity, 0)
    elif variant in (2, 3):
        layer = Image.new('RGBA', source.size)
        draw = ImageDraw.Draw(layer)
        if variant == 2:
            previous = position(0)
            # Stop just outside the current ring, keeping its interior clean.
            for i in range(1, 81):
                t = i / 100
                current = position(t)
                draw.line((previous, current), fill=(*color, alpha(.05 + .49*t)), width=1)
                previous = current
        else:
            for t in (.06, .21, .36, .51, .66, .80):
                x, y = position(t)
                # Native 1px dots at the old end, 2px near contact.
                size = 1 if t < .5 else 2
                draw.rectangle((x, y, x+size-1, y+size-1),
                               fill=(*color, alpha(.08 + .53*t)))
        image = Image.alpha_composite(image, layer)
    return ring(image, CONTACT, RADIUS, color).convert('RGB')


def compose(theme, capture_theme, accent):
    source = Image.open(CAPTURE / capture_theme / BACKGROUND).convert('RGB')
    assert source.size == (320, 240)
    board = Canvas(676, 646, OUTSIDE)
    board.text(12, 12, f'seance / stylus / {theme}', CAPTION, scale=2)
    board.text(12, 37, 'same contact / r10 / 1px outline 82% / fill 10%', CAPTION)
    for i, (title, detail) in enumerate(VARIANTS):
        x, y = 12 + (i % 2)*332, 61 + (i // 2)*281
        board.text(x, y, title, CAPTION)
        board.text(x, y+14, detail, MUTED)
        board.im.paste(overlay(source, accent, i), (x, y+28))
    board.text(12, 622, 'native 320x240, shown 2x / existing host background', MUTED)
    board.text(12, 636, 'static overlay proposal / no application changes', MUTED)
    return board.im.resize((1352, 1292), Image.Resampling.NEAREST)


def main():
    targets = (
        ('pink', 'pink', (91, 49, 124)),
        ('graphite', 'gray', (181, 154, 232)),
    )
    paths = [OUT / f'stylus_{theme}_2x.png' for theme, _, _ in targets]
    if any(path.exists() for path in paths):
        raise SystemExit('output exists; use a new output directory to preserve prior renders')
    boards = [compose(*target) for target in targets]
    OUT.mkdir(parents=True, exist_ok=True)
    for path, image in zip(paths, boards):
        image.save(path)
        print(path)


if __name__ == '__main__':
    main()
