#!/usr/bin/env python3
"""Three offline motion studies, not runtime or hardware captures.

Writes one comparison PNG and three 8-second GIFs, no frame galleries.
Existing wordmark, Gossip28 and native 6x8; illustrative saved-slot metadata.
"""
from functools import lru_cache
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_menu_style_study import heading_mask
from render_module_geometry_study import FONT
from render_main_screen_centered_study import paper

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/start_menu_concepts_study'
INK, SPOT, GHOST, RAIL = (244,244,244), (181,154,232), (184,184,184), (94,91,102)
OUTSIDE = (20,20,22)
FPS, SECONDS = 20, 8
BG = Image.blend(Image.new('RGB', (400,496), (36,36,36)), paper(), .4)
TOP, BOTTOM = BG.crop((0,0,400,240)), BG.crop((40,256,360,496))
LOGO = Image.open(ROOT / 'assets/branding/gothic_wordmark_native_mask.png').convert('L')
assert LOGO.size == (400,240)


def ease(v):
    v = max(0., min(1., v))
    return v*v*(3-2*v)


def ramp(t, a, b):
    return ease((t-a)/(b-a))


@lru_cache(None)
def glyph(text, gothic=False):
    if gothic:
        return heading_mask(text)
    im = Image.new('L', (len(text)*6,8))
    d = ImageDraw.Draw(im)
    for n, ch in enumerate(text):
        for y, bits in enumerate(FONT[ord(ch)-32]):
            for x in range(6):
                if bits & (1 << (5-x)):
                    d.point((n*6+x,y), fill=255)
    return im


def label(im, x, y, text, color=INK, gothic=False, alpha=1., center=False, right=False):
    mask = glyph(text, gothic)
    if center:
        x -= mask.width/2
    if right:
        x -= mask.width
    if alpha < 1:
        mask = mask.point(lambda v: round(v*max(0,alpha)))
    im.paste(color, (round(x),round(y)), mask)


def line(im, pts, color=RAIL, alpha=1.):
    if alpha >= 1:
        ImageDraw.Draw(im).line([(round(x),round(y)) for x,y in pts], fill=color)
    elif alpha > 0:
        over = im.copy()
        ImageDraw.Draw(over).line([(round(x),round(y)) for x,y in pts], fill=color)
        im.paste(Image.blend(im, over, alpha))


def diamond(im, x, y, color=SPOT, radius=3, alpha=1.):
    line(im, ((x,y-radius),(x+radius,y),(x,y+radius),(x-radius,y),(x,y-radius)), color, alpha)


def touch(im, x, y, alpha=1.):
    over = im.copy()
    ImageDraw.Draw(over).ellipse((round(x)-6,round(y)-6,round(x)+6,round(y)+6), outline=SPOT)
    ImageDraw.Draw(over).point((round(x),round(y)), fill=SPOT)
    im.paste(Image.blend(im, over, max(0.,min(1.,alpha))))


def top(response=0.):
    im = TOP.copy()
    # Same silhouette; the response is confined to a quiet ink reflection.
    ink = Image.new('RGB', im.size, INK)
    d = ImageDraw.Draw(ink)
    if response > 0:
        for x in range(400):
            weight = response*.5*max(0., 1-abs(x-210)/160)
            color = tuple(round(a+(b-a)*weight) for a,b in zip(INK,SPOT))
            d.line((x,0,x,239), fill=color)
    im.paste(ink,(0,0),LOGO)
    return im


def folio(t):
    im = BOTTOM.copy()
    forward = ramp(t,1.35,2.3)
    back = ramp(t,4.4,5.25)
    p = forward-back
    label(im,38,20,'new patch',gothic=True)
    label(im,282,33,'a clean page',GHOST,right=True)
    label(im,38,70,'library',GHOST)
    label(im,282,70,'02 / 03' if p>.5 else '01 / 03',GHOST,right=True)
    # Only the middle strip moves; the surrounding words are fixed anchors.
    moving = im.copy()
    for i, (name, age) in enumerate((('patch 03','last opened / 12 min'),('patch 01','last opened / 2 hours'))):
        cx = 160+(i-p)*260
        label(moving,cx,106,name,SPOT,gothic=True,center=True)
        label(moving,cx,145,age,GHOST,center=True)
        diamond(moving,cx,94,RAIL,2)
    im.paste(moving.crop((38,87,283,164)),(38,87))
    # Incomplete page-edge strokes, not card borders or a 3D paper curl.
    for x,direction in ((24,1),(296,-1)):
        line(im,((x,105),(x,153),(x+direction*5,158)))
        line(im,((x-direction*5,111),(x-direction*5,148)),RAIL)
    for i in range(3):
        x=148+i*12
        line(im,((x-2,178),(x+2,178)),SPOT if i==(1 if p>.5 else 0) else RAIL)
    label(im,160,192,'swipe to browse / tap to open',GHOST,center=True)
    label(im,38,212,'settings',GHOST,gothic=True)
    if 1.15<t<2.25:
        q=ramp(t,1.35,2.3)
        touch(im,242-154*q,167, min(ramp(t,1.15,1.35),1-ramp(t,2.05,2.25)))
    if 4.2<t<5.2:
        q=ramp(t,4.4,5.25)
        touch(im,88+154*q,167,min(ramp(t,4.2,4.4),1-ramp(t,5.,5.2)))
    return im, math.sin(p*math.pi)*.8


def ritual(t):
    im=BOTTOM.copy()
    p=ramp(t,1.7,2.45)-ramp(t,4.6,5.35)
    reveal=ease((p-.22)/.78)
    label(im,38-20*p,23,'new patch',gothic=True,alpha=1-p)
    label(im,38-20*p,59,'begin again',GHOST,alpha=1-p)
    lx,ly=200-162*p,52-14*p
    px,py=75+3*p,118-24*p
    # One fine living connection links library to the remembered project.
    # Its endpoints follow the words; no simulated audio/network data.
    line(im,((lx+15,ly+32),(lx+15,ly+39),(px+19,py-17),(px+19,py-7)),RAIL)
    label(im,lx,ly,'library',SPOT,gothic=True)
    label(im,282,52,'3 saved',GHOST,right=True,alpha=p)
    label(im,px,py,'patch 03',INK,gothic=True)
    diamond(im,px-14,py+13,SPOT)
    label(im,px,py+34,'last opened / 12 min',GHOST)
    if reveal>0:
        line(im,((px+19,py+48),(px+19,148),(167,148)),RAIL,reveal)
        line(im,((px+19,148),(52,160),(52,177),(66,177)),RAIL,reveal)
        label(im,178,144,'patch 01',INK,alpha=reveal)
        label(im,77,173,'patch 06',INK,alpha=reveal)
        label(im,282,193,'all projects >',GHOST,right=True,alpha=reveal)
    label(im,38,212,'settings',GHOST,gothic=True)
    label(im,282,220,'b return' if p>.5 else 'tap library',GHOST,right=True)
    if 1.3<t<1.8:
        touch(im,235,68,min(ramp(t,1.3,1.45),1-ramp(t,1.6,1.8)))
    return im,p


def long_page(t):
    im=BOTTOM.copy()
    p=ramp(t,1.4,2.6)-ramp(t,4.7,5.7)
    # The handle is attached to the disclosure end, so it follows the stylus.
    label(im,38,20,'new patch',gothic=True)
    label(im,282,33,'start fresh',GHOST,right=True)
    label(im,38,70,'library',SPOT,gothic=True)
    label(im,282,83,'3 saved',GHOST,right=True)
    line(im,((108,83),(111,86),(114,83)),SPOT)
    label(im,77,110,'patch 03')
    label(im,282,110,'12 min',GHOST,right=True)
    label(im,77,123,'last opened',GHOST)
    line(im,((52,101),(52,111),(55,114),(66,114)))
    edge=145+56*p
    contents=im.copy()
    line(contents,((52,112),(52,173),(55,176),(66,176)))
    line(contents,((52,150),(66,150)))
    for y,name,age in ((146,'patch 01','2 hours'),(172,'patch 06','yesterday')):
        label(contents,77,y,name)
        label(contents,282,y,age,GHOST,right=True)
    label(contents,77,191,'all projects >',GHOST)
    # A paper reveal rather than scaling text or fading a full UI panel.
    end=round(edge-2)
    if end>138:
        im.paste(contents.crop((38,138,284,end)),(38,138))
    line(im,((77,edge),(116,edge)),SPOT)
    diamond(im,126,edge,SPOT,2)
    label(im,282,round(edge)-4,'pull' if p<.35 else 'release',GHOST,right=True,alpha=1 if p<.95 else 0)
    label(im,38,212,'settings',GHOST,gothic=True)
    if 1.15<t<2.8:
        touch(im,126,edge,min(ramp(t,1.15,1.35),1-ramp(t,2.6,2.8)))
    if 4.45<t<5.9:
        touch(im,126,edge,min(ramp(t,4.45,4.65),1-ramp(t,5.7,5.9)))
    return im,p*.4


CONCEPTS=(
    ('01-folio','01 / living folio','swipe between remembered patches',folio,0.),
    ('02-ritual-table','02 / ritual table','words unfold into a spatial archive',ritual,3.),
    ('03-long-page','03 / one long page','pull the library into view',long_page,3.),
)


def caption(im,x,y,text):
    label(im,x,y,text,(184,180,192))


def pair(title,subtitle,draw,t):
    bottom,response=draw(t)
    im=Image.new('RGB',(424,588),OUTSIDE)
    caption(im,12,12,title)
    caption(im,12,29,subtitle)
    im.paste(top(response),(12,48))
    im.paste(bottom,(52,304))
    caption(im,12,558,'motion concept / example saved slots')
    caption(im,12,574,'touch ring = gesture demo / not runtime')
    return im


def make_palette():
    # Shared fixed palette, no per-frame quantization or dithering shimmer.
    colors=list(dict.fromkeys(TOP.get_flattened_data()))
    bg=Image.new('RGB',(len(colors),1))
    bg.putdata(colors)
    reduced=bg.quantize(colors=48)
    colors=[tuple(reduced.getpalette()[i*3:i*3+3]) for i in range(48)]
    colors.extend((INK,SPOT,GHOST,RAIL,OUTSIDE,(184,180,192)))
    for goal in (INK,SPOT,GHOST,RAIL):
        colors.extend(tuple(round(a+(b-a)*i/24) for a,b in zip((36,36,36),goal)) for i in range(25))
    colors.extend(tuple(round(a+(b-a)*i/24) for a,b in zip(INK,SPOT)) for i in range(25))
    colors=list(dict.fromkeys(colors))
    assert len(colors)<=256
    colors += [OUTSIDE]*(256-len(colors))
    pal=Image.new('P',(1,1))
    pal.putpalette([v for c in colors for v in c])
    return pal


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    pal=make_palette()
    sheet=Image.new('RGB',(1288,588),OUTSIDE)
    for i,(slug,title,subtitle,draw,still) in enumerate(CONCEPTS):
        sheet.paste(pair(title,subtitle,draw,still),(i*432,0))
        frames=[]
        first=pair(title,subtitle,draw,0)
        assert first.tobytes()==pair(title,subtitle,draw,SECONDS).tobytes(), slug
        for f in range(FPS*SECONDS):
            im=pair(title,subtitle,draw,f/FPS).quantize(palette=pal,dither=Image.Dither.NONE)
            frames.append(im.resize((848,1176),Image.Resampling.NEAREST))
        path=OUT/f'{slug}-2x.gif'
        frames[0].save(path,save_all=True,append_images=frames[1:],duration=1000//FPS,loop=0,disposal=1,optimize=False)
        with Image.open(path) as check:
            total=0
            for n in range(check.n_frames):
                check.seek(n)
                total+=check.info['duration']
            assert total==SECONDS*1000,(slug,total)
        print(path.relative_to(ROOT),path.stat().st_size,'bytes')
    path=OUT/'comparison-2x.png'
    sheet.resize((2576,1176),Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__=='__main__':
    main()
