#!/usr/bin/env python3
"""Reproduce the approved circular 24/48px launcher icons, not an in-app logo.

Without arguments check installed outputs; --install regenerates only the two
icons. The pinned approved reference is never regenerated. Normal 3DS builds use
the existing PNGs and do not need Pillow. Retired 192px spans live in the archive.
"""
import argparse
import hashlib
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageOps

ROOT = Path(__file__).resolve().parents[1]
BRANDING = ROOT / 'assets/branding'
REFERENCE = BRANDING / 'circle_logo_reference.png'
REFERENCE_SHA = '4049e58aa074934f12cf86aed64e36758153fd2319d95d5261badf321573c54d'


def masters():
    assert hashlib.sha256(REFERENCE.read_bytes()).hexdigest() == REFERENCE_SHA, 'approved reference changed'
    with Image.open(REFERENCE) as source:
        assert source.size == (1024, 1024)
        ink = ImageOps.invert(source.convert('L')).point(lambda v: 255 if v >= 128 else 0)
    assert ink.getbbox() == (108, 100, 919, 906)
    # Square crop, without stretching the approved silhouette or retyping words.
    ink = ink.crop((108, 97, 919, 908))
    disk = Image.new('L', ink.size)
    draw = ImageDraw.Draw(disk)
    for y in range(ink.height):
        bounds = ink.crop((0, y, ink.width, y + 1)).getbbox()
        if bounds:
            draw.line((bounds[0], y, bounds[2] - 1, y), fill=255)
    letters = ImageChops.subtract(disk, ink)
    assert letters.getbbox() == (104, 143, 722, 667)
    return disk, letters


def coverage(master, size):
    # Area coverage, then 16 levels. No ringing, sharpen halo or runtime scaling.
    return master.resize((size, size), Image.Resampling.BOX).point(lambda v: ((v + 8) // 17) * 17)


def outputs():
    disk, letters = masters()
    images = {}
    # Independent reductions from the master, never 24px from the 48px icon.
    # Micro text is necessarily an identity texture at 24px, not readable copy.
    for size, path in ((48, ROOT / 'assets/icon.png'), (24, BRANDING / 'icon_small.png')):
        d, t = coverage(disk, size - 4), coverage(letters, size - 4)
        image = Image.new('RGB', (size, size), 'white')
        image.paste('black', (2, 2, size - 2, size - 2), d)
        image.paste('white', (2, 2, size - 2, size - 2), t)
        images[path] = image
    return images


def verify_outputs(images):
    for path, expected in images.items():
        with Image.open(path) as actual:
            assert actual.mode == expected.mode and actual.size == expected.size, path
            assert actual.tobytes() == expected.tobytes(), f'stale logo asset: {path}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    assert not (args.install and args.check)
    images = outputs()
    if args.install:
        for path, image in images.items():
            image.save(path)
    verify_outputs(images)
    print('launcher icons: pinned circular reference and unchanged 48/24px outputs match')


if __name__ == '__main__':
    main()
