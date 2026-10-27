#!/usr/bin/env python3
"""One render-only sheet; native 400x240/320x240, enlarged nearest 2x.
Illustrative values, not live telemetry or a hardware capture. Existing UI untouched.
Uses project Gossip/6x8 typography; font license assets/fonts/OFL.txt.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_universal_sigils_study import Surface, caption, OUTSIDE
from render_menu_style_study import heading_mask

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_focus_study'


def arrow(c, x, y, direction='right', role='rail'):
    if direction == 'right':
        c.line((x - 3, y - 3, x, y, x - 3, y + 3), role)
    else:
        c.line((x - 3, y - 3, x, y, x + 3, y - 3), role)


def plate(c, x, y, name, selected=False):
    fill = c.p['spot'] if selected else (28, 28, 28)
    c.d.rounded_rectangle((x, y, x + 57, y + 25), radius=3, fill=fill)
    c.text(x + (58 - len(name) * 6) // 2, y + 9, name,
           (48, 48, 48) if selected else c.p['ink'])


def upper():
    c = Surface(400, 'graphite')
    c.heading(20, 14, 'ring', 'spot')
    c.txt(380, 25, 'patch02 / unsaved', 'ghost', right=True)
    c.txt(20, 50, 'ring modulation', 'ghost')
    c.txt(380, 50, 'chain 04 / slot 02', 'ghost', right=True)
    c.line((20, 68, 380, 68))

    c.txt(20, 82, 'frequency', 'ghost')
    c.heading(20, 99, '221', 'spot')
    c.txt(25 + heading_mask('221').width, 117, 'hz', 'ghost')
    c.txt(20, 137, 'base 180 hz', 'ghost')
    c.txt(208, 137, 'live', 'spot', right=True)
    c.line((20, 155, 208, 155))
    c.line((20, 152, 20, 158))
    c.line((208, 152, 208, 158))
    c.line((87, 151, 87, 159), 'ghost')
    c.line((87, 155, 130, 155), 'spot')
    c.dot(130, 155)

    c.txt(256, 82, 'mix', 'ghost')
    c.heading(256, 99, '75')
    c.txt(261 + heading_mask('75').width, 117, '%', 'ghost')
    c.txt(256, 137, 'depth 75%', 'ghost')
    c.txt(256, 154, 'feedback 0%', 'ghost')

    c.line((20, 178, 380, 178))
    c.txt(20, 188, 'modulation', 'ghost')
    c.txt(20, 205, 'a', 'spot')
    c.line((32, 209, 51, 209), 'spot')
    arrow(c, 51, 209, role='spot')
    c.txt(61, 205, 'freq')
    c.txt(208, 205, '+49%', 'spot', right=True)
    c.txt(380, 205, 'sigilbay', 'ghost', right=True)
    c.txt(20, 230, 'gran > ring > chor', 'ghost')
    c.txt(380, 230, 'output on', 'ghost', right=True)
    return c.im


def lower():
    c = Surface(320, 'graphite')
    c.heading(16, 12, 'patch table')
    c.txt(304, 25, 'patch02 *', 'ghost', right=True)
    c.txt(20, 54, 'chain', 'ghost')
    for i, x in enumerate((98, 146, 194, 242), 1):
        c.txt(x, 54, str(i), 'spot' if i == 4 else 'ghost')
        if i == 4:
            c.line((x - 2, 67, x + 8, 67), 'spot')

    # Slots retain the real serial order; only their visual layout is proposed.
    # One short turn, no crossing paths, no audio wire through an empty slot.
    for left, right in ((80, 131), (189, 240)):
        c.line((left, 107, right, 107))
        arrow(c, (left + right) // 2 + 2, 107)
    c.line((269, 121, 269, 154))
    arrow(c, 269, 141, 'down')
    c.line((269, 180, 269, 195, 278, 195))
    arrow(c, 278, 195)
    c.txt(304, 191, 'out', 'ghost', right=True)

    for x, y, name, slot in ((22, 95, 'gran', 1), (131, 95, 'ring', 2),
                              (240, 95, 'chor', 3), (240, 154, 'dly', 4)):
        c.txt(x, y - 12, f'{slot:02}', 'spot' if slot == 2 else 'ghost')
        plate(c, x, y, name, slot == 2)
    for x, slot in ((131, 5), (22, 6)):
        c.txt(x, 142, f'{slot:02}', 'ghost')
        for px in range(x, x + 58, 4):
            c.line((px, 180, px + 1, 180))
        c.txt(x + 26, 163, '+', 'ghost')
    c.txt(20, 209, '4 / 6 modules', 'ghost')
    c.txt(20, 230, 'a edit / r actions', 'ghost')
    c.txt(304, 230, 'select go to', 'ghost', right=True)
    return c.im


def reference_blur(image):
    """Host approximation of the approved recipe, NOT GPU pixel equivalence.
    Finished image BOX downsample differs from runtime command-coverage painting.
    Kernel/rounding/clamped edges/24% paper match; upscale is Pillow, not PICA.
    """
    small = image.resize((160, 120), Image.Resampling.BOX)
    for axis in (0, 1):
        src = small.load()
        dest = Image.new('RGB', small.size)
        pixels = dest.load()
        for y in range(120):
            for x in range(160):
                a = src[max(0, x - 1), y] if axis == 0 else src[x, max(0, y - 1)]
                b = src[x, y]
                z = src[min(159, x + 1), y] if axis == 0 else src[x, min(119, y + 1)]
                pixels[x, y] = tuple((a[k] + 2 * b[k] + z[k] + 2) // 4 for k in range(3))
        small = dest
    small = small.point(lambda v: (v * 76 + 36 * 24 + 50) // 100)
    return small.resize((320, 240), Image.Resampling.BILINEAR)


def disclosure(base):
    c = Surface(320, 'graphite')
    c.im = reference_blur(base)
    c.d = ImageDraw.Draw(c.im)
    # Same sharp selected plate, redrawn in place. No rectangular menu backing.
    plate(c, 131, 95, 'ring', True)
    c.line((160, 121, 160, 127, 188, 127, 188, 222))
    for i, text in enumerate(('edit', 'bypass', 'copy', 'paste', 'delete', 'add')):
        y = 128 + i * 18
        role = 'spot' if i == 0 else 'ghost' if text == 'paste' else 'ink'
        c.line((188, y + 4, 196, y + 4), 'spot' if i == 0 else 'rail')
        c.txt(203, y, text, role)
        if i == 0:
            c.line((203, y + 12, 203 + len(text) * 6 - 1, y + 12), 'spot')
    c.txt(20, 230, 'b close', 'ghost')
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (852, 556), OUTSIDE)
    top, bottom = upper(), lower()
    for i, (name, image) in enumerate((('chain / selected ring', bottom),
                                      ('actions / focus + blur', disclosure(bottom)))):
        x = 18 + i * 416
        caption(sheet, x, 12, name)
        sheet.paste(top, (x, 30))
        sheet.paste(image, (x + 40, 282))
    caption(sheet, 18, 540, '01 / graphite / illustrative values / static concept, not implemented')
    path = OUT / 'main-focus-v1-2x.png'
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
