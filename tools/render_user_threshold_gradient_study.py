#!/usr/bin/env python3
"""Linear purple on the black BACKGROUND only; user's pixel contours unchanged.

Exports one large preview and native 48/24px icons to a separate study directory.
No icon installation, new glyph drawing, build or device operations.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'ui_mockups/user_threshold_study'
OUT = ROOT / 'ui_mockups/user_threshold_linear_gradient_study'
# Keep the exact light ink used in the preceding user-artwork comparison.
FG = (245,238,240)
BLACK = (5,4,8)
# Pink-theme splash_deep_violet() from platform/3ds/ui_palette.h, dimmed for
# background use. The splash's tint sweeps along X; this is a static linear
# background adaptation, not a claim that its runtime background is a gradient.
SPLASH_VIOLET = (88,35,127)
PURPLE = tuple(round(a+(b-a)*.70) for a,b in zip(BLACK,SPLASH_VIOLET))


def background(size):
    image = Image.new('RGB',(size,size))
    pixels = image.load()
    for x in range(size):
        amount = x/(size-1)
        color = tuple(round(a+(b-a)*amount) for a,b in zip(BLACK,PURPLE))
        for y in range(size):
            pixels[x,y] = color
    # One linear axis: every vertical column has an identical background color.
    assert all(pixels[x,0] == pixels[x,size//2] == pixels[x,size-1]
               for x in range(size))
    assert pixels[0,0] == BLACK and pixels[size-1,0] == PURPLE
    return image


def paint(mask):
    assert mask.width == mask.height
    assert set(mask.tobytes()) == {0,255}
    image = background(mask.width)
    image.paste(FG,(0,0,*mask.size),mask)
    # Gradient never enters or softens the symbol.
    pixels = image.load()
    actual = bytes(255 if pixels[x,y] == FG else 0
                   for y in range(image.height) for x in range(image.width))
    assert actual == mask.tobytes()
    return image


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    with Image.open(SOURCE/'user_master_44x49.png') as source:
        master = source.convert('L')
    assert master.size == (44,49)
    # Exact integer enlargement of the recovered original, not a new contour.
    mark = master.resize((440,490),Image.Resampling.NEAREST)
    mask = Image.new('L',(640,640),0)
    mask.paste(mark,(100,75))
    paint(mask).save(OUT/'logo.png')
    for size in (48,24):
        with Image.open(SOURCE/f'icon_{size}_mask.png') as source:
            mask = source.convert('L')
        assert mask.size == (size,size)
        paint(mask).save(OUT/f'icon_{size}.png')
    print('background only; original master and 48/24px contours unchanged')
    print(OUT/'logo.png')


if __name__ == '__main__':
    main()
