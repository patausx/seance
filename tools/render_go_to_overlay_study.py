#!/usr/bin/env python3
"""One static go-to overlay proposal; no runtime, animation or hardware capture.

Reuses the approved centered-paper study with illustrative scalar values.
Native 400x240 / 320x240; original Gossip28 / 6x8; nearest-neighbour 2x.
"""
from pathlib import Path
from PIL import Image
from render_main_screen_centered_study import (
    paper, canvas, band, upper, lower, caption, OUTSIDE,
)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/go_to_overlay_study'
VEIL = (6, 5, 10)
DIM = .68


def backdrop():
    bg = paper()
    top = canvas(bg.crop((0, 0, 400, 240)))
    bottom = canvas(bg.crop((40, 256, 360, 496)))
    left = 48
    for a, end, active in ((left, left + 160, True),
                           (left + 160, left + 192, False)):
        band(top, a, end, top=True, active=active)
        band(bottom, a, end, active=active)
    upper(top, left)
    lower(bottom, left)
    return top.im, bottom.im


def dim(image):
    # Only a uniform translucent veil: no blur, replacement paper or opaque card.
    return Image.blend(image, Image.new('RGB', image.size, VEIL), DIM)


def guide(image):
    c = canvas(dim(image))
    c.heading(24, 13, 'go to')
    c.txt(120, 17, '120 bpm', 'ink')
    c.txt(294, 17, 'swing 50%', 'ink', right=True)
    c.line((120, 30, 160, 30), 'rail')
    c.line((240, 30, 293, 30), 'rail')

    # Four workspaces left, performance / files / exit right. Both are one
    # transient navigation layer, not new persistent navigation columns.
    columns = (
        (32, ('patch table', 'sigils', 'sigilbay', 'sequencer')),
        (192, ('perform', 'bounce', 'patches', 'exit')),
    )
    for x, names in columns:
        stem = x - 12
        c.line((stem, 69, stem, 195), 'rail')
        for row, name in enumerate(names):
            y = 58 + row * 42
            selected = name == 'sequencer'
            role = 'spot' if selected else 'ink'
            c.heading(x, y, name, role)
            c.line((stem, y + 11, x - 5, y + 11),
                   'spot' if selected else 'rail')
            if selected:
                c.line((x - 9, y + 8, x - 5, y + 11, x - 9, y + 14), 'spot')
                c.line((x, y + 25, x + 85, y + 25), 'spot')
            elif name == 'patch table':
                # Location and navigation focus are deliberately distinct.
                c.txt(135, y + 10, 'here', 'ghost')

    c.txt(20, 226, 'a open', 'ghost')
    c.txt(104, 226, 'b / select close', 'ghost')
    c.txt(300, 226, 'y theme', 'ghost', right=True)
    return c.im


def main():
    top, bottom = backdrop()
    overlay_top, overlay_bottom = dim(top), guide(bottom)
    assert overlay_top.size == (400, 240)
    assert overlay_bottom.size == (320, 240)
    sheet = Image.new('RGB', (432, 590), OUTSIDE)
    caption(sheet, 16, 12, 'go to / floating guide')
    caption(sheet, 16, 29, 'patch table underneath / sequencer selected')
    sheet.paste(overlay_top, (16, 48))
    sheet.paste(overlay_bottom, (56, 304))
    caption(sheet, 16, 557, 'translucent shade / no blur / no window')
    caption(sheet, 16, 575, 'static concept / native fonts / not runtime')
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'go-to-overlay-2x.png'
    sheet.resize((864, 1180), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
