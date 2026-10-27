#!/usr/bin/env python3
"""Fit the user's own threshold, not a redesigned or generated substitute.

Pillow only; run directly. All outputs stay in ui_mockups/user_threshold_study.
user_original.png is a byte-exact copy of the upload. Recover its 44x49 logical
pixels from repeated 14/15px cells, checking exact screenshot reconstruction.
48px: remove named repeated rows and paired columns, maintaining thin terminals.
24px: area coverage seed from the 48px artwork, then one paired crown correction.
No generation, grayscale icons, asset installation, builds or device operations.
"""
from pathlib import Path
from statistics import median
import subprocess
from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/user_threshold_study'
PAPER, INK, MUTED, RULE = '#efefeb', '#171717', '#777773', '#d0d0cb'
BG, FG = '#18141d', '#f5eef0'


def recover_axis(image, vertical):
    """Get source sampling and inverse expansion maps from identical scanlines."""
    length = image.height if vertical else image.width
    def line(i):
        box = (0,i,image.width,i+1) if vertical else (i,0,i+1,image.height)
        return image.crop(box).tobytes()
    runs = []
    start, previous = 0, line(0)
    for i in range(1,length+1):
        current = line(i) if i < length else None
        if current != previous:
            runs.append((start,i-start))
            start, previous = i,current
    unit = median(n for _,n in runs if n <= min(n for _,n in runs)*1.2)
    samples, inverse = [], []
    for start,length in runs:
        cells = round(length/unit)
        assert cells > 0 and abs(length/cells-unit) <= 1
        offset = len(samples)
        samples.extend(start+int((j+.5)*length/cells) for j in range(cells))
        inverse.extend(offset+min(cells-1,i*cells//length) for i in range(length))
    return samples,inverse


def extract_master():
    with Image.open(OUT/'user_original.png') as original:
        source = original.convert('L')
    assert set(source.tobytes()) == {0,255}
    crop = source.crop(source.getbbox())
    xs, ix = recover_axis(crop,False)
    ys, iy = recover_axis(crop,True)
    master = Image.new('L',(len(xs),len(ys)))
    for y,sy in enumerate(ys):
        for x,sx in enumerate(xs):
            master.putpixel((x,y),crop.getpixel((sx,sy)))
    # This compares pixels, not just approximate bounds or a resized likeness.
    expanded = Image.new('L',crop.size)
    pixels = master.load()
    expanded.putdata([pixels[x,y] for y in iy for x in ix])
    assert ImageChops.difference(crop,expanded).getbbox() is None
    assert master.size == (44,49)
    return master


def native_artwork(master):
    # A regular nearest-neighbor reduction loses inner foot tips. Use deliberate
    # discrete compression: 49->44 rows and 44->40 columns, no new outline.
    # Dropped rows duplicate adjacent rows in the uploaded master.
    drop_y = (1,5,23,40,42)
    for y in drop_y:
        assert master.crop((0,y,44,y+1)).tobytes() == master.crop((0,y-1,44,y)).tobytes()
    xs = [x for x in range(44) if x not in (10,19,24,33)]
    ys = [y for y in range(49) if y not in drop_y]
    large = Image.new('L',(40,44))
    for y,sy in enumerate(ys):
        for x,sx in enumerate(xs):
            large.putpixel((x,y),master.getpixel((sx,sy)))
    small = large.resize((20,22),Image.Resampling.BOX).point(lambda p:255 if p>=128 else 0)
    # Coverage alone creates a three-row rectangular spike. Broaden the third
    # row by one pixel each side; retain the two-row crown tip and both hooks.
    small.putpixel((8,2),255)
    small.putpixel((11,2),255)
    return {48:large,24:small}


def on_canvas(artwork,size):
    mask = Image.new('L',(size,size),0)
    mask.paste(artwork,((size-artwork.width)//2,(size-artwork.height)//2))
    return mask


def painted(mask):
    image = Image.new('RGB',mask.size,BG)
    image.paste(FG,(0,0,*mask.size),mask)
    return image


def verify(mask):
    assert set(mask.tobytes()) == {0,255}
    assert ImageChops.difference(mask,mask.transpose(Image.Transpose.FLIP_LEFT_RIGHT)).getbbox() is None
    # Diagonal pixel contacts are intentional in the user's hooks and feet.
    remaining = {(x,y) for y in range(mask.height) for x in range(mask.width)
                 if mask.getpixel((x,y))}
    pending = [remaining.pop()]
    while pending:
        x,y = pending.pop()
        for dx,dy in ((-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)):
            p = x+dx,y+dy
            if p in remaining:
                remaining.remove(p)
                pending.append(p)
    assert not remaining, 'detached mark fragment'
    # The central passage remains open all the way down, as in the drawing.
    for y in range(mask.height//2,mask.height):
        assert mask.getpixel((mask.width//2,y)) == 0


def main():
    master = extract_master()
    art = native_artwork(master)
    masks = {size:on_canvas(value,size) for size,value in art.items()}
    for mask in (master,*masks.values()):
        verify(mask)
    master.save(OUT/'user_master_44x49.png')
    rows = [''.join('#' if master.getpixel((x,y)) else '.' for x in range(44)) for y in range(49)]
    (OUT/'user_master_44x49.txt').write_text('\n'.join(rows)+'\n',encoding='ascii')
    for size,mask in masks.items():
        mask.save(OUT/f'icon_{size}_mask.png')
        painted(mask).save(OUT/f'icon_{size}.png')

    fp = subprocess.check_output(['fc-match','-f','%{file}','sans'],text=True).strip()
    title, label, small = [ImageFont.truetype(fp,n) for n in (27,19,15)]
    board = Image.new('RGB',(1090,780),PAPER)
    draw = ImageDraw.Draw(board)
    draw.text((32,23),'threshold / pixel preview',font=title,fill=INK)
    draw.text((32,65),'original drawing preserved · no generation or new decoration',font=small,fill=MUTED)
    draw.line((32,101,1056,101),fill=RULE)
    draw.text((36,121),'original grid 44×49',font=label,fill=INK)
    draw.text((394,121),'48×48 icon',font=label,fill=INK)
    draw.text((752,121),'24×24 icon',font=label,fill=INK)
    board.paste(painted(on_canvas(master,56)).resize((280,280),Image.Resampling.NEAREST),(40,164))
    board.paste(painted(masks[48]).resize((288,288),Image.Resampling.NEAREST),(390,160))
    board.paste(painted(masks[24]).resize((288,288),Image.Resampling.NEAREST),(748,160))
    draw.text((40,461),'×5 / exact outline recovery',font=small,fill=MUTED)
    draw.text((394,461),'×6 / fitted to the grid',font=small,fill=MUTED)
    draw.text((752,461),'×12 / corrected tip',font=small,fill=MUTED)

    draw.text((40,516),'actual size →',font=small,fill=MUTED)
    board.paste(painted(masks[48]),(394,499))
    draw.text((456,516),'48×48',font=small,fill=MUTED)
    board.paste(painted(masks[24]),(752,511))
    draw.text((790,516),'24×24',font=small,fill=MUTED)
    draw.line((32,581,1056,581),fill=RULE)
    with Image.open(ROOT/'assets/branding/gothic_wordmark_pixel_master.png') as source:
        word = source.convert('L').resize((344,104),Image.Resampling.NEAREST)
    mark = masks[48].resize((96,96),Image.Resampling.NEAREST)
    board.paste(INK,(306,621,402,717),mark)
    board.paste(INK,(430,617,774,721),word)
    draw.text((32,749),'preview, not installation · existing wordmark unchanged',font=small,fill=MUTED)
    board.save(OUT/'comparison.png')
    print('44x49 grid recovered; source reconstruction exact; 48/24 masks connected and passage open')
    print(OUT/'comparison.png')


if __name__ == '__main__':
    main()
