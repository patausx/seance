#!/usr/bin/env python3
"""A non-shipping refinement of the user's pixel S. No app assets are replaced."""
from collections import deque
from pathlib import Path
import hashlib
import json
import shutil
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups' / 'pixel_ribbon_study'
UPLOAD = ROOT / '.local-state/uploads/image_b34d2f57.png'
PAPER = '#e2bece'
INK = '#1f141f'
VIOLET = '#5b317c'
PALETTE = {'.': '#00000000', 'i': INK, 't': '#3d234e', 'v': VIOLET, 'h': '#782db0'}
# The original is a 24x28 sprite enlarged with alternating 8/9-pixel steps.
# The authored master retains its S gesture, two white apertures and crossing.
# 4 flat inks replace noisy shading. Top terminal and lower-left spur are pruned.
ROWS = [
    '..........iiiiii........',
    '........tttttiiiiii.....',
    '.....vtvvttiiiiiiiii....',
    '....ttvvvtiiiiiiiiiii...',
    '...ttvvvvvtttiiiiiiii...',
    '..ttvvvvvvvvvttiiiiii...',
    '.htvvvvvh....ttiii.ii...',
    '.vvvvvvv.....iii........',
    '.vvvvvv.....ii.i........',
    '.vvvvvv.....iii.........',
    '.hvvvvv.....ii..........',
    '..vvvvvvh..iii..........',
    '....vvvvvh.ii...........',
    '.....vvvvvii............',
    '.......vvviivv..........',
    '........vivivvvvvv......',
    '........iiiihhvvvvvvvh..',
    '.......ii.i...hhhvvvvvv.',
    '......iiiii.......vvvvvh',
    '......iiii.......vvvvvvv',
    '.i...iiiii.......vvvvvvt',
    '.iiiiiiiiit.....vvvvvvvt',
    '.iiiiiiiitt...vvvvvvvvv.',
    '.iiiiiiiittttttvvvvvv...',
    '..iiiiiiiiiittttvvv.....',
    '...iiiiiiiiiittt........',
    '.....iiiiittv...........',
    '.......iit..............',
]


def font(size, mono=False):
    face = 'IBMPlexMono-Regular.ttf' if mono else 'IBMPlexSans-Regular.ttf'
    return ImageFont.truetype('/usr/share/fonts/TTF/' + face, size)


def raster(rows, palette=PALETTE):
    im = Image.new('RGBA', (32, 32))
    for y, row in enumerate(rows):
        for x, cell in enumerate(row):
            if cell != '.':
                im.putpixel((x + 4, y + 2), Image.new('RGBA', (1, 1), palette[cell]).getpixel((0, 0)))
    return im


def svg(rows, palette, filename):
    content = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" shape-rendering="crispEdges">',
               '<title>seance — pixel ribbon study</title>']
    for cell, color in palette.items():
        if cell == '.':
            continue
        runs = []
        for y, row in enumerate(rows):
            x = 0
            while x < len(row):
                if row[x] != cell:
                    x += 1
                    continue
                end = x + 1
                while end < len(row) and row[end] == cell:
                    end += 1
                runs.append(f'M{x+4} {y+2}h{end-x}v1h-{end-x}z')
                x = end
        content.append(f'<path fill="{color}" d="{"".join(runs)}"/>')
    content.append('</svg>')
    (OUT / filename).write_text('\n'.join(content) + '\n')


def paste_mark(target, mark, x, y, size):
    scaled = mark.resize((size, size), Image.Resampling.NEAREST)
    target.paste(scaled, (x, y), scaled)


def verify(rows):
    assert len(rows) == 28 and all(len(row) == 24 for row in rows)
    assert set(''.join(rows)) <= PALETTE.keys()
    points = {(x, y) for y, row in enumerate(rows) for x, c in enumerate(row) if c != '.'}
    start = next(iter(points))
    todo = deque([start])
    seen = {start}
    while todo:
        x, y = todo.popleft()
        for p in ((x-1,y), (x+1,y), (x,y-1), (x,y+1)):
            if p in points and p not in seen:
                seen.add(p)
                todo.append(p)
    assert seen == points, 'detached pixels in authored silhouette'
    return len(points)


def main():
    count = verify(ROWS)
    OUT.mkdir(parents=True, exist_ok=True)
    source = OUT / 'original.png'
    if not source.exists():
        shutil.copyfile(UPLOAD, source)
    original = Image.open(source).convert('RGB')
    assert original.size == (291, 308)
    original_grid = Image.new('RGBA', (32, 32))
    recolor_grid = Image.new('RGBA', (32, 32))
    color_map = {(0,0,0): INK, (51,33,51): INK, (66,0,66): INK,
                 (128,3,128): PALETTE['t'], (179,2,179): PALETTE['t'],
                 (207,2,207): VIOLET, (232,9,232): VIOLET, (255,0,255): PALETTE['h']}
    for y in range(28):
        for x in range(24):
            rgb = original.getpixel((int(41+(x+.5)*8.5), int(37+(y+.5)*8.5)))
            if rgb != (255,255,255):
                original_grid.putpixel((x+4,y+2), (*rgb,255))
                recolor_grid.putpixel((x+4,y+2), Image.new('RGBA',(1,1),color_map[rgb]).getpixel((0,0)))
    mark = raster(ROWS)
    mark.save(OUT / 'master_32.png')
    svg(ROWS, PALETTE, 'seance_ribbon.svg')
    for size in (24, 32, 48, 64, 256, 512, 1024):
        mark.resize((size, size), Image.Resampling.NEAREST).save(OUT / f'seance_ribbon_{size}.png')
    hero = Image.new('RGB', (768, 768), PAPER)
    paste_mark(hero, mark, 96, 96, 576)
    hero.save(OUT / 'preview.png')
    mono = {key: INK for key in PALETTE}
    svg(ROWS, mono, 'seance_ribbon_mono.svg')
    # Reverse colorway: same geometry, existing pink UI roles as the light inks.
    reverse = {'.':'#00000000', 'i':'#7e6477', 't':'#ae8297', 'v':PAPER, 'h':'#f2d8e2'}
    reversed_mark = raster(ROWS, reverse)
    svg(ROWS, reverse, 'seance_ribbon_reverse.svg')
    reversed_mark.resize((1024,1024), Image.Resampling.NEAREST).save(OUT/'seance_ribbon_reverse_1024.png')
    dark = Image.new('RGB', (768,768), INK)
    paste_mark(dark, reversed_mark, 96,96,576)
    dark.save(OUT/'preview_dark.png')
    # Side-by-side proof: original / palette only / refined, same native scale.
    sheet = Image.new('RGB', (1200, 730), PAPER)
    d = ImageDraw.Draw(sheet)
    d.text((36,24), 'seance / pixel ribbon', font=font(27), fill=INK)
    d.text((36,65), 'color and shape shown separately to make the changes clear', font=font(16), fill=VIOLET)
    d.line((36,107,1164,107), fill='#ae8297')
    for x, label, image, bg in (
        (0, '01 / original', original_grid, '#ffffff'),
        (400, '02 / palette only', recolor_grid, PAPER),
        (800, '03 / refined', mark, PAPER),
    ):
        d.rectangle((x+20,130,x+379,513),fill=bg)
        paste_mark(sheet,image,x+40,158,320)
        d.text((x+36,532),label,font=font(18),fill=INK)
    d.line((36,585,1164,585),fill='#ae8297')
    d.text((36,606),'actual size',font=font(14),fill=VIOLET)
    for size,x in ((24,36),(32,120),(48,220)):
        paste_mark(sheet,mark,x,645,size)
        d.text((x+size+8,654),str(size),font=font(12,True),fill=INK)
    for x,color,label in ((550,INK,'ink'),(730,VIOLET,'violet'),(930,PAPER,'paper')):
        d.rectangle((x,627,x+35,662),fill=color,outline='#ae8297')
        d.text((x+47,626),label,font=font(13),fill=INK)
        d.text((x+47,649),color,font=font(12,True),fill=VIOLET)
    sheet.save(OUT/'comparison.png')
    assert mark.getchannel('A').getextrema() == (0,255)
    assert len(set(mark.get_flattened_data())) == 5
    for file in OUT.glob('seance_ribbon_*.png'):
        with Image.open(file) as exported:
            assert exported.mode == 'RGBA'
            assert set(exported.getchannel('A').get_flattened_data()) <= {0,255}
    # SVG geometry and all transparent exports use exact hard-edged cells.
    receipt = {'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
               'grid':[24,28], 'canvas':[32,32], 'connected_cells':count,
               'palette':PALETTE, 'status':'study only; shipping assets untouched',
               'small_exports':'nearest-neighbour scale proofs, not separately hinted icons'}
    (OUT/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('pixel ribbon: connected silhouette, exact 4 inks, binary alpha; exports ok')
    print(OUT/'comparison.png')


if __name__ == '__main__':
    main()
