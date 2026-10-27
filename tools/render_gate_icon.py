#!/usr/bin/env python3
"""Approved Seance Pink gate: permanent 24/48px icon inputs, no preview dependency.

python3 tools/render_gate_icon.py            # check installed icons
python3 tools/render_gate_icon.py --install  # explicitly replace both PNGs

Only the two icons are installed. The wordmark and animated splash stay unchanged.
"""
import argparse
import hashlib
from pathlib import Path
import re
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
BRANDING = ROOT / 'assets/branding'
MASK_HASHES = {
    'threshold_master_44x49.png': '44483c4309a821ff7b8038bb65fed691df92b86e33c1b3068626d0c7d024ea6e',
    'threshold_icon_48_mask.png': 'aeb30908a09dc69879673eff9c5cc83fdd8344c5be34c40e148dacbd67e59324',
    'threshold_icon_24_mask.png': '8e1082dfa67be5946118e7dbb1dfad26873e58623782cf4263c10607f1ca0c79',
}


def palette():
    source = (ROOT / 'platform/3ds/ui_palette.h').read_text(encoding='utf-8')
    colors = {}
    for name in ('cell', 'bg', 'ink', 'spot'):
        pattern = (rf'inline uint32_t {name}\(\)\s*\{{[^\n]*:\s*'
                   r'rgb\((\d+),\s*(\d+),\s*(\d+)\);\s*\}')
        matches = re.findall(pattern, source)
        assert len(matches) == 1, f'cannot read Pink {name} from ui_palette.h'
        colors[name] = tuple(map(int, matches[0]))
    return colors


def mix(a, b, t):
    return tuple(round(lo + (hi - lo) * t) for lo, hi in zip(a, b))


def verify_sources():
    for name, expected in MASK_HASHES.items():
        assert hashlib.sha256((BRANDING / name).read_bytes()).hexdigest() == expected, name


def render(size):
    assert size in (24, 48), 'use an authored native icon mask'
    with Image.open(BRANDING / f'threshold_icon_{size}_mask.png') as source:
        mask = source.convert('L')
    assert mask.size == (size, size)
    colors = palette()
    left, _, right, _ = mask.getbbox()
    image = Image.new('RGB', mask.size)
    pixels, ink = image.load(), mask.load()
    for x in range(size):
        paper = mix(colors['cell'], colors['bg'], x / (size - 1))
        t = max(0, min(1, (x - left) / (right - left - 1)))
        gate = mix(colors['ink'], colors['spot'], t)
        for y in range(size):
            pixels[x, y] = gate if ink[x, y] else paper
    return image, mask


def verify(image, mask):
    assert image.mode == 'RGB' and image.size == mask.size
    assert set(mask.tobytes()) == {0, 255}
    assert ImageChops.difference(mask, mask.transpose(Image.Transpose.FLIP_LEFT_RIGHT)).getbbox() is None
    recovered = image.convert('L').point(lambda p: 255 if p < 128 else 0)
    assert recovered.tobytes() == mask.tobytes(), 'gate silhouette changed'
    colors = palette()
    assert image.getpixel((0, 0)) == colors['cell']
    assert image.getpixel((image.width - 1, 0)) == colors['bg']
    # The central passage remains open through the foot forks.
    for y in range(mask.height // 2, mask.height):
        assert mask.getpixel((mask.width // 2, y)) == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    verify_sources()
    for size, path in ((48, ROOT / 'assets/icon.png'), (24, BRANDING / 'icon_small.png')):
        image, mask = render(size)
        verify(image, mask)
        if args.install:
            image.save(path)
        with Image.open(path) as actual:
            assert actual.mode == 'RGB' and actual.size == image.size
            assert actual.tobytes() == image.tobytes(), f'stale icon: {path}'
    print('Pink gate: approved permanent masks and both installed icons match; splash unchanged')


if __name__ == '__main__':
    main()
