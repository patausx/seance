#!/usr/bin/env python3
"""Single gothic-pointer startup study; no runtime changes or hardware capture.

One states PNG + one 8s GIF. The user reference informs a hand-cleaned 9x29
arrow bitmap; its graph-paper grid is deliberately not part of the artwork.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_start_menu_concepts_study import (
    ROOT, BOTTOM, INK, SPOT, GHOST, RAIL, OUTSIDE, label, line, top,
    touch, caption, ramp, make_palette,
)

OUT = ROOT / 'ui_mockups/start_menu_compass_study'
# Clean single flanking arrow from the user's triple-arrow reference. Keep its
# stepped crown, narrow stem and small cross tail, without the isolated stray
# left-edge marks in the photo. This is a study mask, not a shipped UI asset.
ARROW = (
    '....#....', '....#....', '...###...', '...###...',
    '#..###..#', '##.###.##', '#########', '..#####..',
    '...###...', '...###...', '..#####..', '#########',
    '##..#..##', '#..###..#', '...###...', '...###...',
    '...###...', '...###...', '...###...', '...###...',
    '...###...', '...###...', '...###...', '....#....',
    '....#....', '....#....', '...###...', '....#....',
    '....#....',
)
BASE = Image.new('L', (37,37))
HEAD = Image.new('L', (37,37))
for y,row in enumerate(ARROW):
    for x,ch in enumerate(row):
        if ch=='#':
            BASE.putpixel((x+14,y+4),255)
            if y<14:
                HEAD.putpixel((x+14,y+4),255)
CX,CY=253,119
TARGETS=((84,33),(84,89),(84,223))
ANGLES=tuple(math.degrees(math.atan2(CX-x,CY-y)) for x,y in TARGETS)


def blend(a,b,p):
    return tuple(round(x+(y-x)*p) for x,y in zip(a,b))


def pointer(im,angle,press=0):
    # Integer x3 silhouette, then native-pixel nearest rotation. Keeping the
    # rotation on the final 1px grid preserves the thin gothic notches; no blur,
    # antialiasing or elastic scaling is applied.
    angle=round(angle/3)*3
    dx=-round(2*press*math.sin(math.radians(angle)))
    dy=-round(2*press*math.cos(math.radians(angle)))
    at=(CX-55+dx,CY-55+dy)
    body=BASE.resize((111,111),Image.Resampling.NEAREST).rotate(angle,Image.Resampling.NEAREST)
    head=HEAD.resize((111,111),Image.Resampling.NEAREST).rotate(angle,Image.Resampling.NEAREST)
    im.paste(blend((132,128,141),SPOT,press),at,body)
    im.paste(blend(SPOT,INK,press),at,head)


def lower(selected=1,angle=None,press=0,expanded=0):
    im=BOTTOM.copy()
    for i,(text,y) in enumerate((('new patch',20),('library',76),('settings',212))):
        label(im,38,y,text,SPOT if i==selected else (GHOST if i==2 else INK),gothic=True)
    label(im,181,89,'3 saved',GHOST,right=True)
    if expanded>.5:
        line(im,((108,88),(111,91),(114,88)),SPOT if selected==1 else GHOST)
    else:
        line(im,((109,87),(112,90),(109,93)),SPOT if selected==1 else GHOST)
    line(im,((52,110),(52,121),(55,124),(66,124)))
    label(im,77,120,'patch 03')
    label(im,77,133,'last / 12 min',GHOST)
    if expanded>0:
        # This illustration fades the additional rows; no actual controller or
        # loading is implemented. The arrow keeps pointing at the library root.
        line(im,((52,121),(52,177),(55,180),(66,180)),RAIL,expanded)
        line(im,((52,158),(66,158)),RAIL,expanded)
        label(im,77,154,'patch 01',alpha=expanded)
        label(im,77,176,'patch 06',alpha=expanded)
        label(im,77,192,'all projects >',GHOST,alpha=expanded)
    pointer(im,ANGLES[selected] if angle is None else angle,press)
    return im


def state(t):
    # Focus only until the explicit library tap near the end. Home actions are
    # not accidentally activated during this demonstration of navigation.
    transitions=((1.1,0),(2.5,1),(3.9,2),(5.1,1))
    angle=ANGLES[1]
    selected=1
    for start,target in transitions:
        if t>=start:
            p=ramp(t,start,start+.2)
            angle=angle+(ANGLES[target]-angle)*p
            selected=target
    pulse=ramp(t,5.8,5.9)*(1-ramp(t,5.9,6.1))
    opened=ramp(t,5.95,6.15)-ramp(t,7.05,7.25)
    return selected,angle,pulse,opened


def frame(t):
    selected,angle,pulse,opened=state(t)
    bottom=lower(selected,angle,pulse,opened)
    if 5.65<t<6.1:
        touch(bottom,83,89,min(ramp(t,5.65,5.8),1-ramp(t,5.95,6.1)))
    im=Image.new('RGB',(424,588),OUTSIDE)
    caption(im,12,12,'04 / gothic compass')
    caption(im,12,29,'words on the left / one responsive sign')
    im.paste(top(),(12,48))
    im.paste(bottom,(52,304))
    caption(im,12,558,'select > turn / press > brief ink response')
    caption(im,12,574,'motion study / example slots / not runtime')
    return im


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    sheet=Image.new('RGB',(1000,290),OUTSIDE)
    for i,(title,selection) in enumerate((('01 / new patch',0),('02 / library',1),('03 / settings',2))):
        x=8+332*i
        caption(sheet,x,10,title)
        sheet.paste(lower(selection),(x,28))
    caption(sheet,8,278,'same sign / native pixel turns / layout concept, not a console capture')
    sheet.resize((2000,580),Image.Resampling.NEAREST).save(OUT/'compass-states-2x.png')
    palette=make_palette()
    frames=[frame(n/20).quantize(palette=palette,dither=Image.Dither.NONE)
            .resize((848,1176),Image.Resampling.NEAREST) for n in range(160)]
    assert frame(0).tobytes()==frame(8).tobytes()
    path=OUT/'gothic-compass-2x.gif'
    frames[0].save(path,save_all=True,append_images=frames[1:],duration=50,loop=0,disposal=1,optimize=False)
    with Image.open(path) as check:
        duration=0
        for i in range(check.n_frames):
            check.seek(i)
            duration+=check.info['duration']
        assert duration==8000
    print(OUT/'compass-states-2x.png')
    print(path)


if __name__=='__main__':
    main()
