#!/usr/bin/env python3
"""Seance wordmark redrawn on a literal pixel grid, not resampled artwork.

The five glyphs below retain the approved gothic lettering's structure, but use
shared vertical stems, intentional step rhythms, connected serifs and identical
e's. This is a wordmark, NOT a general-purpose typeface. No blur, thresholding,
vector tracing or fractional resize occurs in any shipping asset path.

python3 tools/pixel_wordmark.py             # non-shipping before/after studies
python3 tools/pixel_wordmark.py --install   # explicitly write 2x header + 4x splash
python3 tools/pixel_wordmark.py --check     # compare assets with this master
"""
import argparse
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/branding'
OUT = ROOT / 'ui_mockups/pixel_wordmark'
PAPER = (226, 190, 206)
INK = (31, 20, 31)
MUTED = (114, 77, 99)

# One character is one indivisible logical pixel. 26 rows; 4-cell main stems.
# S is intentionally taller; lowercase shares the same cap/baseline rhythm.
GLYPHS = {
    's': '''
......#.......
.....###......
....#####.....
.#########..#.
#####.#######.
.####..#####..
.####...###...
.####.........
.####.........
.####....#....
.####...###...
.####..#####..
#######.######
.#####...####.
..###....####.
...#.....####.
.........####.
.........####.
....#....####.
...###...####.
..#####..####.
...##########.
....########..
.....######...
......####....
.......##.....
''',
    'e': '''
............
.....#......
....###.....
...#####....
..#######...
.####.####..
#####..####.
.####...####
.####...###.
.####..###..
.####.###...
.#######....
.######.....
.#####......
.####.......
.####.......
.####.......
.####.......
.####.......
.#####.....#
.######...#.
..######.#..
...######...
....####....
.....##.....
............
''',
    'a': '''
..............
.....#........
....###.......
...#####......
..#######...#.
..#..#######..
..#...#####...
..#....####...
..#......####.
...#.....####.
...##....####.
..####..#####.
.####.#######.
#####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.####....####.
.#####...#####
..#####.#####.
...#########..
....###.###...
.....#...#....
..............
''',
    'n': '''
..............
...#.....#....
..###...###...
.#####.#####..
#############.
..#####.######
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
..####...####.
...##.....##..
...##.....##..
....#......#..
..............
''',
    'c': '''
............
.....#......
....###.....
...#####....
..########..
.##########.
#####.####..
.####..##...
.####.......
.####.......
.####.......
.####.......
.####.......
.####.......
.####.......
.####.......
.####.......
.####.......
.####.......
.#####.....#
.######...#.
..######.#..
...######...
....####....
.....##.....
............
''',
}
# Optical gaps, in logical pixels, independent of output resolution.
GAPS = (2, 1, 2, 1, 2)


def glyph_rows(char):
    rows = GLYPHS[char].strip().splitlines()
    assert len(rows) == 26 and len({len(row) for row in rows}) == 1, char
    assert set(''.join(rows)) == {'.', '#'}, char
    return rows


def master_mask():
    widths = [len(glyph_rows(char)[0]) for char in 'seance']
    width = sum(widths) + sum(GAPS)
    mask = Image.new('L', (width, 26), 0)
    x0 = 0
    for index, char in enumerate('seance'):
        for y, row in enumerate(glyph_rows(char)):
            for x, value in enumerate(row):
                if value == '#':
                    mask.putpixel((x0+x, y), 255)
        x0 += widths[index] + (GAPS[index] if index < 5 else 0)
    assert mask.size == (86, 26)
    return mask


def scaled_mask(scale):
    if not isinstance(scale, int) or scale < 1:
        raise ValueError('wordmark supports only positive integer pixel scales')
    mask = master_mask()
    return mask.resize((mask.width*scale, mask.height*scale), Image.Resampling.NEAREST)


def native_mask():
    out = Image.new('L', (400, 240), 0)
    # Whole logical pixels at (28,64); 344x104, optically above screen centre.
    out.paste(scaled_mask(4), (28, 64))
    return out


def assets():
    return {
        'gothic_wordmark_pixel_master.png': master_mask(),
        'gothic_wordmark_header_mask.png': scaled_mask(2),
        'gothic_wordmark_native_mask.png': native_mask(),
    }


def verify():
    original = master_mask()
    for scale in (1, 2, 4):
        mask = scaled_mask(scale)
        assert set(mask.tobytes()) == {0, 255}
        for y in range(mask.height):
            for x in range(mask.width):
                assert mask.getpixel((x, y)) == original.getpixel((x//scale, y//scale))
    # e is genuinely the same bitmap in both positions, not two noisy extractions.
    assert original.crop((16,0,28,26)).tobytes() == original.crop((74,0,86,26)).tobytes()
    assert native_mask().getbbox() == (28, 64, 372, 168)
    for char in 'seanc':
        rows = glyph_rows(char)
        ink = {(x,y) for y,row in enumerate(rows) for x,c in enumerate(row) if c == '#'}
        # No isolated specks or disconnected ornament; diagonal joins are intentional.
        remaining = set(ink)
        todo = [remaining.pop()]
        while todo:
            x,y = todo.pop()
            for dx,dy in ((-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)):
                p = x+dx,y+dy
                if p in remaining:
                    remaining.remove(p)
                    todo.append(p)
        assert not remaining, (char, sorted(remaining))
    return assets()


def painted(mask):
    im = Image.new('RGB', mask.size, PAPER)
    im.paste(INK, (0, 0), mask)
    return im


def preview():
    OUT.mkdir(parents=True, exist_ok=True)
    from render_blackletter_splash import tx
    with Image.open(ASSETS/'gothic_wordmark_source_mask.png') as source:
        source = source.convert('L')
        # Reproduce OLD fractional rasterization only for the comparison.
        before = source.resize((344,103), Image.Resampling.LANCZOS).point(lambda p:255 if p>=128 else 0)
    old = Image.new('L',(400,240),0)
    old.paste(before,(28,65))
    new = native_mask()
    board = Image.new('RGB',(816,298),PAPER)
    board.paste(painted(old),(0,20))
    board.paste(painted(new),(416,20))
    d = ImageDraw.Draw(board)
    tx(d,14,8,'before / uneven raster',MUTED)
    tx(d,430,8,'after / aligned pixel master',MUTED)
    # Header: old 143px fraction vs authored 172px integer 2x. Not claimed same size.
    old_small = before.resize((143,43),Image.Resampling.LANCZOS).point(lambda p:255 if p>=116 else 0)
    board.paste(painted(old_small),(28,250))
    board.paste(painted(scaled_mask(2)),(444,242))
    board.resize((1632,596),Image.Resampling.NEAREST).save(OUT/'wordmark_before_after_2x.png')
    painted(new).resize((800,480),Image.Resampling.NEAREST).save(OUT/'splash_pixel_static_2x.png')
    painted(scaled_mask(2)).resize((688,208),Image.Resampling.NEAREST).save(OUT/'header_pixel_detail_4x.png')
    # Grid proof: the actual master, enlarged only with integer nearest pixels.
    grid = painted(original := master_mask()).resize((original.width*8,original.height*8),Image.Resampling.NEAREST)
    gd = ImageDraw.Draw(grid)
    for x in range(0,grid.width,8): gd.line((x,0,x,grid.height-1),fill=(174,130,151))
    for y in range(0,grid.height,8): gd.line((0,y,grid.width-1,y),fill=(174,130,151))
    grid.save(OUT/'pixel_master_grid.png')
    print(OUT/'wordmark_before_after_2x.png')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--install', action='store_true')
    group.add_argument('--check', action='store_true')
    args = parser.parse_args()
    rendered = verify()
    if args.check:
        for name, expected in rendered.items():
            with Image.open(ASSETS/name) as image:
                assert image.size == expected.size and image.mode == 'L', name
                assert image.tobytes() == expected.tobytes(), f'{name}: stale'
        print('pixel wordmark: master + 2x header + 4x splash match; no partial cells')
    elif args.install:
        for name, image in rendered.items():
            image.save(ASSETS/name)
        print('installed integer-only masks; regenerate wordmark_morph_data.h and tools/wordmark_reference.h next')
    else:
        preview()


if __name__ == '__main__':
    main()
