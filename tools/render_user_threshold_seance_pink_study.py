#!/usr/bin/env python3
"""One still using the actual Seance Pink palette, with the existing contour.

Read endpoint RGB values from ui_palette.h rather than approximating them.
This is a horizontal branding study, not a copy of the runtime paper gradient.
Only a new preview is exported; previous studies and shipping assets stay intact.
"""
from pathlib import Path
import re
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'ui_mockups/user_threshold_study/user_master_44x49.png'
PALETTE = ROOT / 'platform/3ds/ui_palette.h'
OUT = ROOT / 'ui_mockups/user_threshold_seance_pink_study'
SIZE = 640


def pink_colors():
    source = PALETTE.read_text(encoding='utf-8')
    colors = {}
    for name in ('cell', 'bg', 'ink', 'spot'):
        # The second ternary branch is Pink. Fail if the source format changes;
        # never silently fall back to guessed or Graphite colors.
        pattern = (rf'inline uint32_t {name}\(\)\s*\{{[^\n]*:\s*'
                   r'rgb\((\d+),\s*(\d+),\s*(\d+)\);\s*\}')
        matches = re.findall(pattern, source)
        assert len(matches) == 1, f'cannot read Pink {name} from {PALETTE}'
        colors[name] = tuple(map(int, matches[0]))
    return colors


def mix(a, b, amount):
    return tuple(round(lo + (hi - lo) * amount) for lo, hi in zip(a, b))


def main():
    colors = pink_colors()
    with Image.open(SOURCE) as source:
        master = source.convert('L')
    assert master.size == (44, 49) and set(master.tobytes()) == {0, 255}
    mask = Image.new('L', (SIZE, SIZE), 0)
    mask.paste(master.resize((440, 490), Image.Resampling.NEAREST), (100, 75))
    left, _, right, _ = mask.getbbox()
    image = Image.new('RGB', mask.size)
    pixels, ink = image.load(), mask.load()
    for x in range(SIZE):
        # Background darkens toward Pink bg; gate lightens toward purple spot.
        paper = mix(colors['cell'], colors['bg'], x / (SIZE - 1))
        t = max(0, min(1, (x - left) / (right - left - 1)))
        gate = mix(colors['ink'], colors['spot'], t)
        for y in range(SIZE):
            pixels[x, y] = gate if ink[x, y] else paper

    recovered = image.convert('L').point(lambda p: 255 if p < 128 else 0)
    assert ImageChops.difference(recovered, mask).getbbox() is None
    assert pixels[0, 0] == colors['cell'] and pixels[SIZE - 1, 0] == colors['bg']
    OUT.mkdir(parents=True, exist_ok=True)
    image.save(OUT / 'logo.png')
    print('original silhouette unchanged; actual Pink palette endpoints:', colors)
    print(OUT / 'logo.png')


if __name__ == '__main__':
    main()
