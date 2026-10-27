#!/usr/bin/env python3
"""Swap the existing threshold's light/dark roles in one non-shipping still.

Keep the exact master, placement and opposing gradient directions. The earlier
light gate colors become the background, and its dark background colors become
the gate. No new outline, animation, installed asset changes or device IO.
"""
from pathlib import Path
from PIL import Image, ImageChops
from render_user_threshold_gradient_study import BLACK, PURPLE, FG
from render_user_threshold_reverse_gradient_study import SHADE

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'ui_mockups/user_threshold_study/user_master_44x49.png'
OUT = ROOT / 'ui_mockups/user_threshold_light_background_study'
SIZE = 640


def mix(a, b, amount):
    return tuple(round(lo + (hi - lo) * amount) for lo, hi in zip(a, b))


def main():
    with Image.open(SOURCE) as source:
        master = source.convert('L')
    assert master.size == (44, 49) and set(master.tobytes()) == {0, 255}
    mask = Image.new('L', (SIZE, SIZE), 0)
    mask.paste(master.resize((440, 490), Image.Resampling.NEAREST), (100, 75))
    left, _, right, _ = mask.getbbox()
    image = Image.new('RGB', mask.size)
    pixels, ink = image.load(), mask.load()
    for x in range(SIZE):
        # Pale background darkens rightward; the dark gate lightens to violet.
        paper = mix(FG, SHADE, x / (SIZE - 1))
        gate = mix(BLACK, PURPLE, max(0, min(1, (x - left) / (right - left - 1))))
        for y in range(SIZE):
            pixels[x, y] = gate if ink[x, y] else paper

    recovered = image.convert('L').point(lambda p: 255 if p < 128 else 0)
    assert ImageChops.difference(recovered, mask).getbbox() is None
    assert pixels[0, 0] == FG and pixels[SIZE - 1, 0] == SHADE
    OUT.mkdir(parents=True, exist_ok=True)
    image.save(OUT / 'logo.png')
    print('original silhouette unchanged; light milk-lavender background / dark black-violet gate')
    print(OUT / 'logo.png')


if __name__ == '__main__':
    main()
