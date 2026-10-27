#!/usr/bin/env python3
"""Isolated fleur-derived arrow, not a menu mockup or runtime asset.

A long directional blade and two curved scrolls replace the old blocky cross.
Cubic outlines are sampled locally; no model, external renderer or paid API.
"""
from pathlib import Path
import sys
from PIL import Image, ImageDraw
from render_module_geometry_study import Canvas

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/gothic_arrow_study'
SCALE = 8
NATIVE = (112, 72)
INK = (181, 154, 232)
PAPER = (36, 36, 39)

# Coordinates in the final native grid. The first point is the only long tip;
# the short tail terminates flat, so it cannot be read as a second arrowhead.
BLADE = (
    ('M', 4, 36),
    ('C', 17, 35, 26, 23, 36, 25),
    ('C', 43, 26, 44, 32, 51, 33),
    ('L', 105, 33), ('L', 105, 39), ('L', 51, 39),
    ('C', 44, 40, 43, 46, 36, 47),
    ('C', 26, 49, 17, 37, 4, 36),
)
SCROLL = (
    ('M', 53, 35),
    ('C', 62, 32, 58, 18, 65, 10),
    ('C', 73, 0, 91, 3, 96, 13),
    ('C', 103, 27, 90, 34, 81, 28),
    ('C', 77, 25, 79, 19, 84, 19),
    ('C', 88, 19, 89, 23, 87, 25),
    ('C', 93, 25, 95, 19, 91, 14),
    ('C', 86, 8, 77, 10, 73, 15),
    ('C', 67, 23, 72, 33, 62, 37),
    ('C', 58, 38, 55, 37, 53, 35),
)


def points(commands, mirror=False):
    result=[]
    current=(0,0)
    for cmd in commands:
        if cmd[0] in ('M','L'):
            current=cmd[1:]
            result.append(current)
        else:
            p0=current
            p1,p2,p3=cmd[1:3],cmd[3:5],cmd[5:7]
            for n in range(1,33):
                t=n/32; u=1-t
                result.append(tuple(u**3*p0[i]+3*u*u*t*p1[i]+3*u*t*t*p2[i]+t**3*p3[i] for i in (0,1)))
            current=p3
    return [(round(x*SCALE),round((72-y if mirror else y)*SCALE)) for x,y in result]


def mask():
    im=Image.new('L',(NATIVE[0]*SCALE,NATIVE[1]*SCALE))
    d=ImageDraw.Draw(im)
    d.polygon(points(BLADE),fill=255)
    d.polygon(points(SCROLL),fill=255)
    d.polygon(points(SCROLL,True),fill=255)
    # A single fine axial incision makes the reference's divided blade visible
    # without adding a ring, cross, second tip, texture or separate ornament.
    d.line(((17*SCALE,36*SCALE),(100*SCALE,36*SCALE)),fill=0,width=SCALE)
    # Threshold once at native size. Presentation is nearest-neighbour only.
    return im.resize(NATIVE,Image.Resampling.LANCZOS).point(lambda v:255 if v>=128 else 0)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    arrow=mask()
    gradient='--gradient' in sys.argv
    long=gradient or '--long' in sys.argv
    if gradient:
        # Fill exactly the one native-pixel incision; leave every contour intact.
        ImageDraw.Draw(arrow).line(((17,36),(99,36)),fill=255)
    if long:
        # Extend only the straight stem after the scrolls. Every pixel of the
        # original blade/ornament is retained, including the axial incision.
        extended=Image.new('L',(152,72))
        extended.paste(arrow.crop((0,0,100,72)),(0,0))
        extended.paste(arrow.crop((100,0,101,72)).resize((40,72),Image.Resampling.NEAREST),(100,0))
        extended.paste(arrow.crop((100,0,112,72)),(140,0))
        arrow=extended
    suffix='-gradient' if gradient else ('-long' if long else '')
    rgba=Image.new('RGBA',arrow.size,INK+(0,))
    if gradient:
        left,_,right,_=arrow.getbbox()
        pen=ImageDraw.Draw(rgba)
        for x in range(arrow.width):
            t=max(0.,min(1.,(x-left)/(right-left-1)))
            color=tuple(round(a+(b-a)*t) for a,b in zip((244,244,244),INK))
            pen.line((x,0,x,arrow.height-1),fill=color+(255,))
    rgba.putalpha(arrow)
    rgba.save(OUT/f'arrow{suffix}-native.png')
    width=1040 if long else 800
    c=Canvas(width,620,bg=PAPER)
    title='gothic arrow / ink to accent' if gradient else ('gothic arrow / longer shaft' if long else 'gothic arrow / isolated form')
    c.text(32,24,title,(184,180,192))
    big=rgba.resize((arrow.width*6,arrow.height*6),Image.Resampling.NEAREST)
    c.im.paste(big,(64,54),big)
    c.text(32,533,f'native / {arrow.width} x {arrow.height}',(184,180,192))
    c.im.paste(rgba,((width-arrow.width)//2,517),rgba)
    c.text(32,599,'fleur reference / long blade, curved scrolls / no menu changes',(184,180,192))
    path=OUT/f'gothic-arrow{suffix}-6x.png'
    c.im.save(path)
    print(path)


if __name__=='__main__':
    main()
