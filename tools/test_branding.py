#!/usr/bin/env python3
"""Verify installed branding and optionally both icon planes in real SMDH files.

python3 tools/test_branding.py [seance.smdh build/seance_cia.smdh]
Checks include unchanged circular launcher icons and the restored gothic morph.
"""
import argparse
import struct
from pathlib import Path
from PIL import Image
from generate_circle_logo import ROOT, REFERENCE, outputs, verify_outputs
from package_release import source_payload
from pixel_wordmark import verify as verify_wordmark
from generate_wordmark_morph import verify as verify_morph, outputs as morph_header
from generate_wordmark_reference import generate as reference_header


def morton(x, y, size):
    return (((y >> 3)*(size >> 3)+(x >> 3)) << 6) + (
        (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
        ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3))


def verify_smdh(path):
    data = path.read_bytes()
    assert len(data) == 0x36c0 and data[:4] == b'SMDH', path
    for size, offset, source in (
        (24, 0x2040, ROOT/'assets/branding/icon_small.png'),
        (48, 0x24c0, ROOT/'assets/icon.png'),
    ):
        with Image.open(source) as image:
            rgb = image.convert('RGB')
        words = struct.unpack_from(f'<{size*size}H', data, offset)
        for y in range(size):
            for x in range(size):
                r, g, b = rgb.getpixel((x, y))
                expected = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                assert words[morton(x, y, size)] == expected, (path, size, x, y)
    print(f'{path}: both RGB565 icon planes match authored 24/48px assets')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('smdh', nargs='*', type=Path)
    args = parser.parse_args()
    images = outputs()
    verify_outputs(images)
    again = outputs()
    assert all(again[path].tobytes() == image.tobytes() for path, image in images.items())
    # The restored active English endpoint remains the exact old gothic master.
    verify_morph()
    assert (ROOT/'platform/3ds/wordmark_morph_data.h').read_text() == morph_header(), 'stale morph header'
    wordmark = verify_wordmark()
    for name, expected in wordmark.items():
        with Image.open(ROOT/'assets/branding'/name) as image:
            assert image.mode == 'L' and image.size == expected.size, name
            assert image.tobytes() == expected.tobytes(), f'pixel wordmark stale: {name}'
    source = source_payload(ROOT)
    for path in (*images, REFERENCE):
        relative = path.relative_to(ROOT).as_posix()
        assert source[f'source/{relative}'] == path.read_bytes(), f'missing release input {relative}'
    for name in ('gathering_reference.png', *wordmark):
        assert f'source/assets/branding/{name}' in source, f'missing release input {name}'
    assert 'source/tools/pixel_wordmark.py' in source, 'missing editable pixel master'
    assert 'source/tools/generate_circle_logo.py' in source, 'missing current launcher icon generator'
    assert (ROOT/'tools/wordmark_reference.h').read_text() == reference_header(), 'stale English endpoint oracle'
    for relative in ('tools/generate_wordmark_morph.py', 'platform/3ds/wordmark_morph_data.h',
                     'platform/3ds/wordmark_morph.h', 'tools/generate_wordmark_reference.py',
                     'tools/wordmark_reference.h'):
        assert source[f'source/{relative}'] == (ROOT/relative).read_bytes(), f'missing morph source {relative}'
    assert 'source/tools/render_sigil_icon.py' in source, 'missing historical sigil renderer'
    assert 'source/tools/render_gate_icon.py' in source, 'missing historical gate renderer'
    for relative in ('assets/fonts/Gossip-MedPixel.otf', 'assets/fonts/OFL.txt', 'assets/fonts/README.md'):
        assert source[f'source/{relative}'] == (ROOT/relative).read_bytes()
    assert b'SIL Open Font License' in source['source/assets/fonts/OFL.txt']
    for path in args.smdh:
        verify_smdh(path)
    print('branding: restored gothic morph + unchanged circular 48/24px icons and release sources OK')


if __name__ == '__main__':
    main()
