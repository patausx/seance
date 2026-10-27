#!/usr/bin/env python3
"""Generate multilingual gothic wordmark distance fields, not a UI typeface.

Original seance is imported byte-for-byte from pixel_wordmark.native_mask().
New glyphs are authored on that wordmark's 26-row, four-cell-stem pixel grid;
no fonts, image tracing, antialiasing, floating point, or external assets.

  --install  explicitly write platform/3ds/wordmark_morph_data.h
  --check    verify that the checked-in header matches this generator
With no arguments, verify artwork/data without writing anything.
"""
import argparse
from pathlib import Path
from PIL import Image
import pixel_wordmark

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'platform/3ds/wordmark_morph_data.h'
WORDS = ('seance', 'сеанс', 'sitzung', 'sesión')
X, Y, WIDTH, HEIGHT = 8, 48, 384, 128
SCALE, BASE_Y = 4, 64

# Literal authored logical pixels. Main vertical stems remain four cells wide.
# i intentionally has a detached diamond dot. g has a shortened upper bowl and
# a descending return, not an o with a decorative spur. Cyrillic н has an open
# top and a true mid-height crossbar: it is not the Latin n bitmap.
GLYPHS = {
    'i': '''
...#...
..###..
.#####.
..###..
...#...
.......
.......
...#...
..###..
.#####.
######.
.####..
.####..
.####..
.####..
.####..
.####..
.####..
.####..
.####..
######.
.#####.
..###..
...##..
....#..
.......
''',
    't': '''
..........
.....#....
....###...
...#####..
...####...
...####...
.########.
##########
.########.
...####...
...####...
...####...
...####...
...####...
...####...
...####...
...####...
...####...
...####...
...#####..
...######.
....######
.....####.
......##..
.......#..
..........
''',
    'z': '''
.............
...#.........
..###......#.
.#####....###
############.
.###########.
..##########.
.......####..
......####...
......####...
.....####....
.....####....
....####.....
...#####.....
...####......
..####.......
..####.......
.####........
.####........
#####.....#..
.#####...###.
.###########.
..###########
...#########.
....##....#..
.............
''',
    'u': '''
..............
...#.....#....
..###...###...
.#####.#####..
.#####..#####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
.######.######
..###########.
...#########..
....###.###...
.....#...#....
..............
''',
    'g': '''
..............
.....#.....#..
....###...###.
...#####.#####
..###########.
.#####...####.
#####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.#####...####.
..###########.
...##########.
....###..####.
.....#...####.
.........####.
..#......####.
.###.....####.
#####...####..
.##########...
..########....
...######.....
....####......
''',
    'o': '''
..............
......#.......
.....###......
....#####.....
...#######....
..#########...
.#####.#####..
#####...#####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.#####..#####.
..##########..
...########...
....######....
.....####.....
..............
''',
    'н': '''
..............
...#.....#....
..###...###...
.#####.#####..
.#####..#####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..###########.
..###########.
..###########.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
..####...####.
.######.######
..####...####.
...##.....##..
...##.....##..
....#......#..
..............
''',
}
# Russian с/е/а intentionally share the Latin shapes of the approved artwork.
ALIASES = {'с': 'c', 'е': 'e', 'а': 'a', 'ó': 'o'}
# A rising-right three-row acute, not a dot. Positions are relative to the ó.
ACUTE = ('..##', '.##.', '##..')
ACUTE_X, ACUTE_Y = 8, -3
# English gaps are canonical. Narrower custom optical spacing keeps sitzung at
# the same native 4x scale and within the safe region; never squeeze a word.
GAPS = {
    'seance': pixel_wordmark.GAPS,
    'сеанс': (2, 2, 2, 2),
    'sitzung': (1, 1, 1, 1, 1, 1),
    'sesión': (2, 2, 1, 1, 2),
}


def glyph_rows(char):
    """Return the 26-row base glyph (the acute is a separate authored mark)."""
    char = ALIASES.get(char, char)
    if char in pixel_wordmark.GLYPHS:
        return pixel_wordmark.glyph_rows(char)
    rows = GLYPHS[char].strip().splitlines()
    assert len(rows) == 26 and len({len(row) for row in rows}) == 1, char
    assert set(''.join(rows)) == {'.', '#'}, char
    return rows


def word_width(word):
    return sum(len(glyph_rows(char)[0]) for char in word) + sum(GAPS[word])


def _paint_rows(mask, rows, x0, y0):
    for y, row in enumerate(rows):
        for x, value in enumerate(row):
            if value == '#':
                # Exactly four native pixels per logical pixel, no filtering.
                mask.paste(255, (x0 + x*SCALE, y0 + y*SCALE,
                                 x0 + (x+1)*SCALE, y0 + (y+1)*SCALE))


def masks():
    """Return four binary native 400x240 PIL L endpoint masks in WORDS order."""
    result = [pixel_wordmark.native_mask()]
    for word in WORDS[1:]:
        mask = Image.new('L', (400, 240), 0)
        x0 = (400 - word_width(word)*SCALE) // 2
        for index, char in enumerate(word):
            rows = glyph_rows(char)
            _paint_rows(mask, rows, x0, BASE_Y)
            if char == 'ó':
                _paint_rows(mask, ACUTE, x0 + ACUTE_X*SCALE,
                            BASE_Y + ACUTE_Y*SCALE)
            x0 += len(rows[0])*SCALE
            if index < len(word)-1:
                x0 += GAPS[word][index]*SCALE
        result.append(mask)
    return result


def _distances(binary, seed):
    """Two-pass integer chamfer distance: native axis=4, diagonal=6."""
    far = 1 << 20
    distances = [0 if value == seed else far for value in binary]
    for y in range(HEIGHT):
        row = y*WIDTH
        for x in range(WIDTH):
            i = row+x
            value = distances[i]
            if x:
                value = min(value, distances[i-1]+4)
            if y:
                value = min(value, distances[i-WIDTH]+4)
                if x:
                    value = min(value, distances[i-WIDTH-1]+6)
                if x+1 < WIDTH:
                    value = min(value, distances[i-WIDTH+1]+6)
            distances[i] = value
    for y in range(HEIGHT-1, -1, -1):
        row = y*WIDTH
        for x in range(WIDTH-1, -1, -1):
            i = row+x
            value = distances[i]
            if x+1 < WIDTH:
                value = min(value, distances[i+1]+4)
            if y+1 < HEIGHT:
                value = min(value, distances[i+WIDTH]+4)
                if x:
                    value = min(value, distances[i+WIDTH-1]+6)
                if x+1 < WIDTH:
                    value = min(value, distances[i+WIDTH+1]+6)
            distances[i] = value
    return distances


def distance_field(mask):
    """Signed Q4 (quarter native pixel): positive inside; magnitudes >=2."""
    binary = mask.crop((X, Y, X+WIDTH, Y+HEIGHT)).tobytes()
    to_ink = _distances(binary, 255)
    to_paper = _distances(binary, 0)
    return [min(127, to_paper[i]-2) if value else -min(127, to_ink[i]-2)
            for i, value in enumerate(binary)]


def _components(points):
    remaining = set(points)
    result = []
    while remaining:
        component = {remaining.pop()}
        todo = list(component)
        while todo:
            x, y = todo.pop()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    p = x+dx, y+dy
                    if p in remaining:
                        remaining.remove(p)
                        component.add(p)
                        todo.append(p)
        result.append(component)
    return result


def verify():
    """Check artwork, safe bounds, authored mark topology, and exact endpoints."""
    for char in set(''.join(WORDS)) | set(GLYPHS):
        rows = glyph_rows(char)
        points = {(x, y) for y, row in enumerate(rows)
                  for x, value in enumerate(row) if value == '#'}
        assert len(_components(points)) == (2 if char == 'i' else 1), char
    # Four-cell minims and an open top distinguish н from the Latin n.
    cyrillic = glyph_rows('н')
    assert cyrillic != glyph_rows('n')
    assert cyrillic[8][2:6] == cyrillic[8][9:13] == '####'
    assert cyrillic[8][6:9] == '...'
    assert cyrillic[12][2:13] == '###########'
    # Explicit acute slants up to the right, separated from the base o by >=1
    # whole logical row. Its 4x3 bounding box cannot silently become a dot.
    assert len(ACUTE) == 3 and all(len(row) == 4 for row in ACUTE)
    assert [row.index('#') for row in ACUTE] == [2, 1, 0]
    assert all(row.count('#') == 2 for row in ACUTE)
    assert ACUTE_Y + len(ACUTE) == 0
    assert glyph_rows('ó')[0] == '.'*len(glyph_rows('ó')[0])
    acute_points = {(x, y) for y, row in enumerate(ACUTE)
                    for x, value in enumerate(row) if value == '#'}
    assert len(_components(acute_points)) == 1
    accented_points = {(x, y) for y, row in enumerate(glyph_rows('ó'))
                       for x, value in enumerate(row) if value == '#'}
    accented_points |= {(x+ACUTE_X, y+ACUTE_Y) for x, y in acute_points}
    assert len(_components(accented_points)) == 2
    endpoints = masks()
    assert endpoints[0].tobytes() == pixel_wordmark.native_mask().tobytes()
    assert endpoints[0].getbbox() == (28, 64, 372, 168)
    for word, mask in zip(WORDS, endpoints):
        assert len(GAPS[word]) == len(word)-1
        assert word_width(word)*SCALE <= WIDTH
        assert mask.mode == 'L' and mask.size == (400, 240)
        assert set(mask.tobytes()) == {0, 255}, word
        x0, y0, x1, y1 = mask.getbbox()
        assert X <= x0 < x1 <= X+WIDTH and Y <= y0 < y1 <= Y+HEIGHT, word
        # Both the LCD edges and all four distance-domain edges are unpainted.
        assert not any(mask.crop(box).getbbox() for box in
                       ((0, 0, 400, 1), (0, 239, 400, 240),
                        (0, 0, 1, 240), (399, 0, 400, 240)))
        assert not any(mask.crop(box).getbbox() for box in
                       ((X, Y, X+1, Y+HEIGHT),
                        (X, Y, X+WIDTH, Y+1),
                        (X+WIDTH-1, Y, X+WIDTH, Y+HEIGHT),
                        (X, Y+HEIGHT-1, X+WIDTH, Y+HEIGHT)))
        field = distance_field(mask)
        assert len(field) == WIDTH*HEIGHT
        assert all(2 <= abs(value) <= 127 for value in field)
        # The renderer's alpha rule must reproduce all four *binary* endpoints.
        rebuilt = bytes(max(0, min(255, 128+64*value)) for value in field)
        assert rebuilt == mask.crop((X, Y, X+WIDTH, Y+HEIGHT)).tobytes(), word
    return endpoints


def outputs():
    """Return the complete deterministic C++ header; do not write anything."""
    lines = [
        '// Generated by tools/generate_wordmark_morph.py; do not hand edit.',
        '// Custom gothic wordmark artwork; NOT the Gossip UI font.',
        '// Order: seance -> сеанс -> sitzung -> sesión -> seance.',
        '// English source: tools/pixel_wordmark.py native_mask(), unchanged.',
        '// Other glyphs: literal 26-row artwork in the generator; native 4x.',
        '// Signed quarter-native-pixel units (Q4): positive inside the mask.',
        '// Chamfer axis=4, diagonal=6; half-pixel correction=2; clamp +/-127.',
        '// Minimum endpoint magnitude=2. alpha=clamp(128+64*d,0,255) is binary.',
        '// Outside this native LCD region is background. 196608 bytes ROM.',
        '#pragma once', '#include <cstdint>', '',
        'namespace seance { namespace platform { namespace ui3ds {',
        'namespace wordmark_morph_data {',
        f'constexpr int kX = {X};', f'constexpr int kY = {Y};',
        f'constexpr int kWidth = {WIDTH};', f'constexpr int kHeight = {HEIGHT};',
        f'constexpr int kWords = {len(WORDS)};', '',
        'static const std::int8_t kDistance[kWords][kWidth * kHeight] = {',
    ]
    for word, mask in zip(WORDS, masks()):
        lines.append(f'    {{ // {word}')
        field = distance_field(mask)
        for start in range(0, len(field), 32):
            lines.append('        ' + ','.join(str(value) for value in field[start:start+32]) + ',')
        lines.append('    },')
    lines.extend(['};', '', '} // namespace wordmark_morph_data',
                  '}}} // namespace seance::platform::ui3ds', ''])
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--install', action='store_true')
    group.add_argument('--check', action='store_true')
    args = parser.parse_args()
    verify()
    if args.install:
        HEADER.write_text(outputs(), encoding='utf-8')
        print(f'installed {HEADER.relative_to(ROOT)}')
    elif args.check:
        assert HEADER.read_text(encoding='utf-8') == outputs(), 'stale wordmark morph header'
        print('wordmark morph: 4 endpoints, exact English, topology/bounds/Q4/header match')
    else:
        print('wordmark morph: artwork and binary endpoints verified; no files written')


if __name__ == '__main__':
    main()
