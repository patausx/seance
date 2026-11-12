#!/usr/bin/env python3
"""Verify selected PENT derivatives and losslessly encode two immutable textures.

No drawing, extension, model or network calls. ffmpeg only decodes local PNGs.
The original art/scenes/old generator/header stay untouched. Normal ARM builds
consume the checked-in header and require none of these authoring tools.
"""
import argparse
import copy
from decimal import Decimal
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/artwork/pent'
DERIVED = ART / 'reactive-v1'
OUT = ROOT / 'platform/3ds/pent_reactive_data.h'
# Pinned local originals and delivered derivatives, not latest pointers.
HASHES = {
    'glow-scene.json': '99f0694c956e55015c50ff43612951036faefe9ebca91e4a825e93233a5f2a54',
    'liquid-scene.json': '127ac070587fa870ca1fdc6064af225ed9fbd929890e6c85067c09f1271fc787',
    'pent-glow-particles-v1.png': 'b16bad65924d8775707de75305448b78128bb672da9ee1f0da89daf6fb2f8145',
    'pent-glow-particles-v1.png.frames.json': '4a45972b1d846447bab48f6b3184f9eac724e7e21bd74d997f5cf4b53ea41878',
    'pent-liquid-blood-v1.png': '9765911f20a0a74eb3eacd959574be9f04dec3684d9f9a8074c2ca11a0ad7eeb',
    'pent-liquid-blood-v1.png.frames.json': '2df37ba5bd3971e0adc1b18b6b27c09b0a3610c516f534659a144690d2cafb89',
    'reactive-v1/atlas-scene.json': '8ff379464f45f431bf4d608987504c11e00915db6dd96ad43b325392941a23ab',
    'reactive-v1/atlas.png': 'f19ffb98fd50428ddb0901efe6398345e6b0d5e2a0a116ca242350f15c1fac5e',
    'reactive-v1/fx.png': 'e4e455de70a791b9b1e721050ce8818241e427e7b7f23a5e43d0126869dfd20b',
}
WHITE = bytes([244, 244, 244, 255])
HEAD_COLORS = {'bb1c31ff', 'd5283aff', 'ed435574', 'ed4355ff', 'f89096ff'}
TILE_FACTS = [(164, (13, 2, 26, 26)), (162, (13, 13, 34, 30)),
              (151, (6, 13, 26, 26)), (161, (13, 9, 35, 26)),
              (163, (13, 13, 26, 37))]


def decode(path, width, height):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', f'not PNG: {path}'
    assert struct.unpack('>II', data[16:24]) == (width, height), f'PNG dimensions: {path}'
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-threads', '1', '-i', str(path),
                          '-threads', '1', '-f', 'rawvideo', '-pix_fmt', 'rgba', 'pipe:1'],
                         capture_output=True, check=True, timeout=40).stdout
    assert len(raw) == width * height * 4, f'RGBA dimensions: {path}'
    return raw


def verify_scene():
    original = json.loads((ART / 'liquid-scene.json').read_text())['scene']
    assert (original['width'], original['height'], original['background']) == (256, 192, None)
    source = original['objects']
    assert len(source) == 56
    white = copy.deepcopy(source[10:16])
    assert [o['type'] for o in white] == ['ring'] + ['line'] * 5
    assert all(o['color'] == '#f4f4f4' and 'motion' not in o for o in white)
    expected = white
    for i in range(5):
        # Confirm zero-based first head and its original four polygon layers.
        blob = source[2*i]
        assert blob['type'] == 'blob' and blob['radius'] == 6
        dx = Decimal(i*40+20) - Decimal(str(blob['x']))
        dy = Decimal(228) - Decimal(str(blob['y']))
        for index in [2*i, *range(16+8*i, 20+8*i)]:
            obj = copy.deepcopy(source[index])
            assert obj['motion'] == blob['motion']
            obj.pop('motion')
            if obj['type'] == 'blob':
                obj['x'] = float(Decimal(str(obj['x'])) + dx)
                obj['y'] = float(Decimal(str(obj['y'])) + dy)
                assert (obj['x'], obj['y']) == (i*40+20, 228)
                # Full radius is strictly inside the declared tile.
                assert i*40 < obj['x']-obj['radius'] < obj['x']+obj['radius'] < i*40+40
                assert 208 < obj['y']-obj['radius'] < obj['y']+obj['radius'] < 248
            else:
                assert obj['type'] == 'polygon'
                obj['points'] = [[float(Decimal(str(x))+dx), float(Decimal(str(y))+dy)]
                                 for x, y in obj['points']]
                assert all(i*40 < x < i*40+40 and 208 < y < 248 for x, y in obj['points'])
            expected.append(obj)
    args = json.loads((DERIVED / 'atlas-scene.json').read_text())
    assert args == {'scene': {'width': 256, 'height': 256, 'background': None, 'objects': expected},
                    'frame_count': 1, 'path': 'assets/artwork/pent/reactive-v1/atlas.png',
                    'scale': 1, 'preview': True, 'output_format': 'png'}, 'atlas derivation drift'
    assert len(expected) == 31 and all('motion' not in o for o in expected)


def verify_assets():
    for name, expected in HASHES.items():
        assert hashlib.sha256((ART / name).read_bytes()).hexdigest() == expected, f'asset drift: {name}'
    verify_scene()
    for name, height in [('pent-glow-particles-v1.png', 200), ('pent-liquid-blood-v1.png', 192)]:
        meta = json.loads((ART / (name + '.frames.json')).read_text())
        assert meta['frames'] == 24 and meta['scale'] == 1 and meta['columns'] == 6
        assert meta['dimensions'] == {'width': 1536, 'height': height*4}
        assert meta['frame_dimensions'] == {'width': 256, 'height': height}
    meta = json.loads((DERIVED / 'fx.png.prepare.json').read_text())
    assert meta['source_crop'] == [768, 400, 256, 200]
    assert meta['source_oriented_dimensions'] == {'width': 1536, 'height': 800}
    assert meta['texture_dimensions'] == {'width': 256, 'height': 200}
    assert meta['pixel_scale'] == 1 and meta['color_mode'] == 'keep' and meta['resample'] == 'nearest'
    fx = decode(DERIVED / 'fx.png', 256, 200)
    old_fx = decode(ART / 'pent-glow-particles-v1.png', 1536, 800)
    crop = b''.join(old_fx[((400+y)*1536+768)*4:((400+y)*1536+1024)*4] for y in range(200))
    assert fx == crop, 'frame15 RGBA must remain byte-exact, including transparent RGB'
    atlas = decode(DERIVED / 'atlas.png', 256, 256)
    old_liquid = decode(ART / 'pent-liquid-blood-v1.png', 1536, 768)
    old_white = set()
    for frame in range(24):
        for y in range(192):
            at = ((frame//6*192+y)*1536+frame%6*256)*4
            row = old_liquid[at:at+256*4]
            old_white.update((x, y) for x in range(256) if row[x*4:x*4+4] == WHITE)
    new_white = set()
    tiles = [[] for _ in range(5)]
    tile_colors = [set() for _ in range(5)]
    for y in range(256):
        for x in range(256):
            c = atlas[(y*256+x)*4:(y*256+x)*4+4]
            if not c[3]:
                continue
            if y < 192:
                assert c == WHITE, 'only original white scaffold in upper atlas'
                new_white.add((x, y))
            else:
                assert 208 <= y < 248 and x < 200, 'pixel outside disjoint atlas crops'
                tile = x//40
                assert c.hex() in HEAD_COLORS, 'tile palette/white contamination'
                tiles[tile].append((x-tile*40, y-208))
                tile_colors[tile].add(c.hex())
    assert new_white == old_white and len(new_white) == 2415, 'original white pixel coverage drift'
    for i, pixels in enumerate(tiles):
        bbox = (min(x for x, y in pixels), min(y for x, y in pixels),
                max(x for x, y in pixels), max(y for x, y in pixels))
        assert (len(pixels), bbox) == TILE_FACTS[i], f'tile {i} raster drift'
        assert all(0 < x < 39 and 0 < y < 39 for x, y in pixels), f'tile {i} edge clipping'
        assert tile_colors[i] == HEAD_COLORS
    # Texture0 padding is transparent, never scaled up to a 256px height.
    return [fx + bytes(256*56*4), atlas]


def generate():
    textures = verify_assets()
    palette, indices = [0], {0: 0}
    streams, heights, offsets, counts = [], [], [], []
    for raw in textures:
        offsets.append(len(streams))
        rectangles, previous = [], {}
        normalized = bytearray(len(raw))
        for y in range(256):
            row = []
            for x in range(256):
                at = (y*256+x)*4
                r, g, b, a = raw[at:at+4]
                color = r | g << 8 | b << 16 | a << 24 if a else 0
                if a:
                    normalized[at:at+4] = bytes([r, g, b, a])
                if color not in indices:
                    indices[color] = len(palette)
                    palette.append(color)
                row.append(indices[color])
            x, current = 0, {}
            while x < 256:
                end = x+1
                while end < 256 and row[end] == row[x]:
                    end += 1
                if row[x]:
                    key = (x, end-x, row[x])
                    if key in previous:
                        index = previous[key]
                        rectangles[index][1] += 1
                    else:
                        index = len(rectangles)
                        rectangles.append([x | y << 8 | (end-x-1) << 16 | row[x] << 24, 1])
                    current[key] = index
                x = end
            previous = current
        assert all(0 < h < 256 for r, h in rectangles)
        # Prove lossless runtime rectangle coverage of every visible RGBA byte.
        restored = bytearray(len(raw))
        for run, height in rectangles:
            x, y, width, c = run & 255, (run >> 8) & 255, ((run >> 16) & 255)+1, palette[run >> 24]
            pixel = bytes([c & 255, (c >> 8) & 255, (c >> 16) & 255, c >> 24])
            for yy in range(y, y+height):
                at = (yy*256+x)*4
                restored[at:at+width*4] = pixel * width
        assert restored == normalized, 'RGBA run encoding drift'
        streams.extend(r for r, h in rectangles)
        heights.extend(h for r, h in rectangles)
        counts.append(len(rectangles))
    offsets.append(len(streams))
    assert len(palette) <= 256 and len(offsets) == 3
    lines = ['// Generated by tools/generate_pent_reactive.py; exact local derivatives.',
             '// Immutable texture0: FX frame15 + transparent bottom; texture1: white + 5 wet tiles.',
             '// Two whole 256x256 RGBA textures, no animation-frame compatibility stream.']
    lines += ['// assets/artwork/pent/' + name + ' sha256 ' + digest for name, digest in HASHES.items()]
    lines += ['#pragma once', '#include <cstdint>',
              'namespace seance::platform::ui3ds::pent_reactive_data {',
              'inline constexpr uint32_t colors[] = {']
    lines += ['    ' + ','.join('0x%08xu' % c for c in palette[i:i+8]) + ',' for i in range(0, len(palette), 8)]
    lines += ['};', 'inline constexpr uint32_t offsets[] = {',
              '    ' + ','.join(str(x) for x in offsets), '};', 'inline constexpr uint32_t runs[] = {']
    lines += ['    ' + ','.join('0x%08xu' % c for c in streams[i:i+12]) + ',' for i in range(0, len(streams), 12)]
    lines += ['};', 'inline constexpr uint8_t heights[] = {']
    lines += ['    ' + ','.join(str(h) for h in heights[i:i+32]) + ',' for i in range(0, len(heights), 32)]
    lines += ['};', 'inline constexpr uint8_t ring_x[] = {',
              '    ' + ','.join(str(math.isqrt(92*92-y*y)) for y in range(-92, 93)), '};',
              'inline constexpr unsigned textures=2, width=256, height=256;',
              'inline constexpr unsigned max_runs=' + str(max(counts)) + ';',
              '} // namespace seance::platform::ui3ds::pent_reactive_data', '']
    text = '\n'.join(lines)
    assert len(text.encode()) < 1884576, 'must remain smaller than original large atlas header'
    return text, counts, len(palette)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    text, counts, colors = generate()
    if args.check:
        assert OUT.read_text() == text, 'PENT immutable reactive header drift'
    else:
        OUT.write_text(text)
    print('pent reactive: ' + ('verified' if args.check else 'encoded') +
          f' two 256x256 textures; runs={counts}, colors={colors}, header_bytes={len(text.encode())}; '
          'fx frame15 exact, white2415 exact, five unclipped/disjoint original-palette tiles')


if __name__ == '__main__':
    main()
