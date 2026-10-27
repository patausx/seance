#!/usr/bin/env python3
"""Simplify the user's latest shaded threshold, not the earlier binary artwork.

Non-shipping Pillow study; no image generation, install, animation or device IO.
The upload is retained byte-for-byte. Its light body is isolated and cracks closed,
then fitted to a 56x62 bitmap. Reflect the undamaged left half to repair the right
shoulder fracture/shading asymmetry. This is a cleaned adaptation, NOT an exact
recovery of an unknown original grid. One flat inner shade replaces all bevels.
"""
from pathlib import Path
import subprocess
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/user_threshold_simplified_study'
BG = (18,13,26)
FACE = (245,238,240)
SHADE = (156,132,167)
MUTED = (171,160,181)


def cleaned_master():
    with Image.open(OUT/'user_original.png') as original:
        crop = original.convert('L').crop((60,54,480,524))
    mask = crop.point(lambda p:255 if p>=110 else 0)
    # Exclude the HIGH-RES label, which partly intersects this crop's top left.
    ImageDraw.Draw(mask).rectangle((0,0,180,46),fill=0)
    mask = mask.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.MinFilter(9))
    mask = ImageOps.contain(mask.crop(mask.getbbox()),(56,64),Image.Resampling.LANCZOS)
    mask = mask.point(lambda p:255 if p>=128 else 0)
    assert mask.size == (56,62)
    for y in range(mask.height):
        for x in range(mask.width//2):
            mask.putpixel((mask.width-1-x,y),mask.getpixel((x,y)))
    # Close the small artifact above each foot fork, not the exterior fork itself.
    for x in (10,45):
        mask.putpixel((x,56),255)
    return mask


def native_masks(master):
    masks = {}
    for size,extent in ((48,(40,44)),(24,(20,22))):
        art = master.resize(extent,Image.Resampling.BOX).point(lambda p:255 if p>=108 else 0)
        if size == 48:
            # Retain the inner nib tips lost by coverage at the bottom row.
            for x in (14,25):
                art.putpixel((x,43),255)
        else:
            # Recover crown, both levels of hooks and forked feet at 24px.
            # Paired corrections keep the width/mass of the reference intact.
            for x,y in ((9,0),(8,2),(1,4),(1,5),(0,13),(6,21)):
                art.putpixel((x,y),255)
                art.putpixel((art.width-1-x,y),255)
        masks[size] = art
    return masks


def color_art(mask,shade_width):
    image = Image.new('RGB',mask.size,BG)
    image.paste(FACE,(0,0,*mask.size),mask)
    mid = mask.width//2
    for y in range(mask.height*5//6):
        if mask.getpixel((mid,y)):
            continue
        inside = [x for x in range(mid) if mask.getpixel((x,y))]
        if not inside:
            continue
        edge = max(inside)
        start = edge
        while start>0 and mask.getpixel((start-1,y)):
            start-=1
        # Do not shade away the light core of the thinnest small-icon strokes.
        width = min(shade_width,max(0,edge-start-1))
        for x in range(edge-width+1,edge+1):
            image.putpixel((x,y),SHADE)
            image.putpixel((mask.width-1-x,y),SHADE)
    return image


def canvas(image,size):
    result = Image.new(image.mode,(size,size),0 if image.mode=='L' else BG)
    result.paste(image,((size-image.width)//2,(size-image.height)//2))
    return result


def verify(mask,image):
    assert set(mask.tobytes()) == {0,255}
    assert set(image.get_flattened_data()) <= {BG,FACE,SHADE}
    recovered = Image.new('L',mask.size)
    recovered.putdata([0 if p==BG else 255 for p in image.get_flattened_data()])
    assert ImageChops.difference(recovered,mask).getbbox() is None
    assert ImageChops.difference(mask,ImageOps.mirror(mask)).getbbox() is None
    ink = {(x,y) for y in range(mask.height) for x in range(mask.width) if mask.getpixel((x,y))}
    pending = [ink.pop()]
    while pending:
        x,y = pending.pop()
        for dx,dy in ((-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)):
            p=x+dx,y+dy
            if p in ink:
                ink.remove(p)
                pending.append(p)
    assert not ink,'detached tip'
    for y in range(mask.height//2,mask.height):
        assert mask.getpixel((mask.width//2,y))==0,'passage closed'


def main():
    master = cleaned_master()
    art = {72:master,**native_masks(master)}
    images = {}
    for size,mask in art.items():
        image = color_art(mask,2 if size==72 else 1)
        verify(mask,image)
        mask = canvas(mask,size)
        images[size] = canvas(image,size)
        name = 'master' if size==72 else f'icon_{size}'
        mask.save(OUT/f'{name}_mask.png')
        images[size].save(OUT/f'{name}.png')
    images[72].resize((576,576),Image.Resampling.NEAREST).save(OUT/'logo.png')

    fp = subprocess.check_output(['fc-match','-f','%{file}','sans'],text=True).strip()
    title,label,small = [ImageFont.truetype(fp,n) for n in (26,18,14)]
    board = Image.new('RGB',(1120,700),BG)
    draw = ImageDraw.Draw(board)
    draw.text((32,24),'threshold / clean, without stone texture',font=title,fill=FACE)
    draw.text((32,65),'light fill + one inner arch shade',font=label,fill=MUTED)
    draw.line((32,107,1088,107),fill=(57,44,68))
    draw.text((40,124),'main mark / ×6',font=label,fill=MUTED)
    draw.text((520,124),'48×48 / ×6',font=label,fill=MUTED)
    draw.text((864,124),'24×24 / ×8',font=label,fill=MUTED)
    board.paste(images[72].resize((432,432),Image.Resampling.NEAREST),(32,156))
    board.paste(images[48].resize((288,288),Image.Resampling.NEAREST),(512,156))
    board.paste(images[24].resize((192,192),Image.Resampling.NEAREST),(856,156))
    draw.text((520,482),'actual size',font=small,fill=MUTED)
    board.paste(images[48],(520,514))
    draw.text((580,528),'48',font=small,fill=MUTED)
    board.paste(images[24],(672,526))
    draw.text((708,528),'24',font=small,fill=MUTED)
    draw.line((32,618,1088,618),fill=(57,44,68))
    draw.text((32,648),'study from the latest drawing · no glow, cracks or outer stroke',font=small,fill=MUTED)
    board.save(OUT/'comparison.png')
    print('three-color cleanup; native masks connected; open passage; installed branding unchanged')
    print(OUT/'comparison.png')


if __name__ == '__main__':
    main()
