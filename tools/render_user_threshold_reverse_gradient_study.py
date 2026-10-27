#!/usr/bin/env python3
"""One non-shipping still: subtle inverse gradient inside the existing gate.

Reuse the exact earlier master, placement and background. Only foreground RGB
changes; no new contours, bevel, glow, animation, icon installation or device IO.
"""
from pathlib import Path
from PIL import Image, ImageChops
from render_user_threshold_gradient_study import background, FG

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'ui_mockups/user_threshold_study'
BASELINE = ROOT / 'ui_mockups/user_threshold_linear_gradient_study/logo.png'
OUT = ROOT / 'ui_mockups/user_threshold_reverse_gradient_study'
# Background grows lighter toward the right; the gate gently grows darker.
# Keep the right edge pale lavender, rather than turning it into a dark outline.
SHADE = (214, 198, 226)


def main():
    with Image.open(SOURCE / 'user_master_44x49.png') as source:
        master = source.convert('L')
    assert master.size == (44, 49) and set(master.tobytes()) == {0, 255}
    mask = Image.new('L', (640, 640), 0)
    mask.paste(master.resize((440, 490), Image.Resampling.NEAREST), (100, 75))
    image = background(640)
    left, _, right, _ = mask.getbbox()
    for x in range(left, right):
        t = (x - left) / (right - left - 1)
        color = tuple(round(a + (b - a) * t) for a, b in zip(FG, SHADE))
        for y in range(image.height):
            if mask.getpixel((x, y)):
                image.putpixel((x, y), color)

    with Image.open(BASELINE) as source:
        previous = source.convert('RGB')
    difference = ImageChops.difference(image, previous)
    outside = ImageChops.invert(mask)
    assert ImageChops.multiply(difference, outside.convert('RGB')).getbbox() is None
    recovered = image.convert('L').point(lambda p: 255 if p > 128 else 0)
    assert ImageChops.difference(recovered, mask).getbbox() is None
    OUT.mkdir(parents=True, exist_ok=True)
    image.save(OUT / 'logo.png')
    print('original silhouette and background unchanged; inverse light-to-lavender gate only')
    print(OUT / 'logo.png')


if __name__ == '__main__':
    main()
