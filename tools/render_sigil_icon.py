#!/usr/bin/env python3
"""Pink sigil launcher icons, permanent native masks and palette only.

Without arguments verify the installed pair; --install writes only 48/24px PNGs.
No dependency on previews, uploads, fonts or generation services.
"""
import argparse
import hashlib
from pathlib import Path
import re
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BRANDING = ROOT / 'assets/branding'
MASK_HASHES = {
    'sigil_icon_48_rim_mask.png': '63f6bb7e6a1b9e1925175eaf1492218212764f38215bb2f9e900ec04e1ab0dc8',
    'sigil_icon_48_core_mask.png': '6a69a9b11e011c0ac867546dc243a732e6fa34a05eac5e868b8ca611542ba91d',
    'sigil_icon_24_rim_mask.png': '5e618cbd2b1c7bff124f53a53c27b3ed5679b5dd784b1e15990200cb15b26923',
    'sigil_icon_24_core_mask.png': '68dbea1a83ce6bcf3f77895f7f6f33132ea67e52ddab962d1be625850fdbbfcf',
}


def palette():
    source = (ROOT / 'platform/3ds/ui_palette.h').read_text(encoding='utf-8')
    colors = {}
    for role in ('bg', 'ink', 'spot'):
        pattern = (rf'inline uint32_t {role}\(\)\s*\{{[^\n]*:\s*'
                   r'rgb\((\d+),\s*(\d+),\s*(\d+)\);\s*\}')
        matches = re.findall(pattern, source)
        assert len(matches) == 1, f'cannot read Pink {role}'
        colors[role] = tuple(map(int, matches[0]))
    return colors


def verify_sources():
    for name, expected in MASK_HASHES.items():
        assert hashlib.sha256((BRANDING / name).read_bytes()).hexdigest() == expected, name


def render(size):
    assert size in (24, 48), 'use the authored native size'
    masks = []
    for layer in ('rim', 'core'):
        with Image.open(BRANDING / f'sigil_icon_{size}_{layer}_mask.png') as source:
            assert source.mode == 'L' and source.size == (size, size)
            masks.append(source.copy())
    colors = palette()
    image = Image.new('RGB', (size, size), colors['bg'])
    image.paste(colors['ink'], (0, 0), masks[0])
    image.paste(colors['spot'], (0, 0), masks[1])
    return image, tuple(masks)


def verify(image, masks):
    rim, core = masks
    assert image.mode == 'RGB' and image.size == core.size == rim.size
    assert core.getbbox() is not None
    if image.width == 24:
        assert rim.getbbox() is None, '24px mark has no frame'
    else:
        assert rim.getbbox() is not None, '48px seal needs its frame'
    colors = palette()
    expected = Image.new('RGB', image.size, colors['bg'])
    expected.paste(colors['ink'], (0, 0), rim)
    expected.paste(colors['spot'], (0, 0), core)
    assert image.tobytes() == expected.tobytes(), 'palette or coverage changed'
    assert image.getpixel((0, 0)) == colors['bg']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    verify_sources()
    for size, path in ((48, ROOT/'assets/icon.png'), (24, BRANDING/'icon_small.png')):
        image, masks = render(size)
        verify(image, masks)
        if args.install:
            image.save(path)
        with Image.open(path) as actual:
            assert actual.mode == image.mode and actual.size == image.size
            assert actual.tobytes() == image.tobytes(), f'stale icon: {path}'
    print('Pink sigil: native 48px reduced seal and 24px micro mark match; splash unchanged')


if __name__ == '__main__':
    main()
