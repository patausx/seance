#!/usr/bin/env python3
"""Non-shipping native-weight threshold study: separately drawn 48/24px masks.

One concept at two actual icon sizes, with exact nearest-neighbor enlargement.
All artwork coordinates below are integer pixel spans, not generated resizing.
Existing branding is read-only. Run to export one sheet and editable PNG masks.
"""
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/gothic_threshold_native_weight_study'
PAPER, INK, MUTED = '#efefeb', '#171717', '#777773'
ICON_BG, ICON_FG = '#18141d', '#f5eef0'

# Left-side inclusive spans: y0, y1, x0, x1. Mirror around the cell center.
# Main stems remain seven pixels at 48px and four pixels at 24px.
BODY_48 = [
    (2,2,23,23), (3,4,22,23), (5,5,21,23), (6,6,20,23),
    (7,7,19,23), (8,8,18,23), (9,9,17,23), (10,10,16,22),
    (11,11,15,21), (12,12,14,20), (13,13,13,19), (14,15,12,18),
    (16,17,11,17), (18,18,10,17), (19,33,10,16), (34,35,9,16),
    (36,37,8,16), (38,38,7,16), (39,39,6,17), (40,40,5,18),
    (41,41,4,16), (42,42,7,15), (43,43,9,14), (44,44,11,13),
    (45,45,12,12),
]
SHOULDERS_48 = [
    (16,16,6,6), (17,17,6,7), (18,18,6,8), (19,19,7,10),
    (20,21,7,11), (22,22,6,10), (23,23,5,10), (24,24,5,7),
]
BODY_24 = [
    (1,1,11,11), (2,2,10,11), (3,3,9,11), (4,4,8,11),
    (5,5,8,10), (6,6,7,9), (7,7,6,9), (8,8,6,8),
    (9,16,5,8), (17,18,4,8), (19,19,3,8), (20,20,2,8),
    (21,21,4,7), (22,22,5,6),
]
SHOULDERS_24 = [
    (7,7,3,3), (8,8,3,4), (9,9,3,5), (10,10,3,6),
    (11,11,2,5), (12,12,3,5),
]


def make_mask(size):
    mask = Image.new('L', (size,size), 0)
    spans = BODY_48 + SHOULDERS_48 if size == 48 else BODY_24 + SHOULDERS_24
    for y0,y1,x0,x1 in spans:
        for y in range(y0,y1+1):
            for x in range(x0,x1+1):
                mask.putpixel((x,y),255)
                mask.putpixel((size-1-x,y),255)
    return mask


def painted(mask):
    image = Image.new('RGB', mask.size, ICON_BG)
    image.paste(ICON_FG, (0,0,*mask.size), mask)
    return image


def old_icon(size):
    with Image.open(ROOT/'ui_mockups/gothic_threshold_pixel_expressive_study/01_grid_mask.png') as source:
        fit = ImageOps.contain(source.convert('L'), (size-4,size-4), Image.Resampling.LANCZOS)
    fit = fit.point(lambda p:255 if p>=128 else 0)
    mask = Image.new('L',(size,size),0)
    mask.paste(fit,((size-fit.width)//2,(size-fit.height)//2))
    return painted(mask)


def verify(mask):
    assert set(mask.tobytes()) == {0,255}
    ink = {(x,y) for y in range(mask.height) for x in range(mask.width)
           if mask.getpixel((x,y))}
    remaining = set(ink)
    todo = [remaining.pop()]
    while todo:
        x,y = todo.pop()
        for dx,dy in ((-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)):
            p = x+dx,y+dy
            if p in remaining:
                remaining.remove(p)
                todo.append(p)
    assert not remaining, 'detached ink'
    size = mask.width
    assert not mask.getpixel((size//2,size-1)), 'passage closed'
    assert not mask.getpixel((size//2,size//2)), 'passage blocked'


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    masks = {size:make_mask(size) for size in (48,24)}
    for size,mask in masks.items():
        verify(mask)
        mask.save(OUT/f'threshold_{size}_mask.png')
        painted(mask).save(OUT/f'threshold_{size}.png')
    font_path = subprocess.check_output(['fc-match','-f','%{file}','sans'],text=True).strip()
    title, label, small = [ImageFont.truetype(font_path,n) for n in (25,18,14)]
    board = Image.new('RGB',(1040,680),PAPER)
    draw = ImageDraw.Draw(board)
    draw.text((36,26),'threshold / bold icon silhouette',font=title,fill=INK)
    draw.text((36,66),'one design · separately drawn pixels at 48 and 24',font=label,fill=MUTED)
    draw.line((36,106,1004,106),fill='#d0d0cb')

    draw.text((36,130),'48×48 / enlarged ×6',font=label,fill=INK)
    board.paste(painted(masks[48]).resize((288,288),Image.Resampling.NEAREST),(36,170))
    draw.text((728,130),'24×24 / enlarged ×8',font=label,fill=INK)
    board.paste(painted(masks[24]).resize((192,192),Image.Resampling.NEAREST),(728,170))
    draw.text((728,381),'not a reduced 48px copy',font=small,fill=MUTED)

    draw.text((400,130),'actual size / 1:1',font=label,fill=INK)
    draw.text((400,175),'new',font=small,fill=MUTED)
    board.paste(painted(masks[48]),(400,204))
    board.paste(painted(masks[24]),(496,216))
    draw.text((400,263),'48',font=small,fill=MUTED)
    draw.text((496,263),'24',font=small,fill=MUTED)
    draw.text((400,323),'previous thin version / 1:1',font=small,fill=MUTED)
    board.paste(old_icon(48),(400,351))
    board.paste(old_icon(24),(496,363))

    draw.line((36,493,1004,493),fill='#d0d0cb')
    with Image.open(ROOT/'assets/branding/gothic_wordmark_pixel_master.png') as source:
        word = source.convert('L').resize((344,104),Image.Resampling.NEAREST)
    board.paste(INK,(262,531,358,627),masks[48].resize((96,96),Image.Resampling.NEAREST))
    board.paste(INK,(386,527,730,631),word)
    draw.text((36,648),'pixel study · wordmark and installed icons unchanged',font=small,fill=MUTED)
    board.save(OUT/'comparison.png')
    print(OUT/'comparison.png')


if __name__ == '__main__':
    main()
