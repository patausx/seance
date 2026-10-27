#!/usr/bin/env python3
"""Non-shipping infinity versus gothic-initial logo study. Pillow only."""
from pathlib import Path
import math
from PIL import Image, ImageDraw, ImageFont
from pixel_wordmark import GLYPHS

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/ribbon_gothic_study'
PAPER, INK, VIOLET, TURN = '#e2bece', '#1f141f', '#5b317c', '#3d234e'
NEAREST = Image.Resampling.NEAREST


def font(size, mono=False):
    name = 'IBMPlexMono-Regular.ttf' if mono else 'IBMPlexSans-Regular.ttf'
    return ImageFont.truetype('/usr/share/fonts/TTF/' + name, size)


def glyph(char):
    rows = GLYPHS[char].strip().splitlines()
    mask = Image.new('L', (max(map(len, rows)), len(rows)))
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c == '#':
                mask.putpixel((x,y),255)
    return mask


def paint(mask, color=INK):
    image = Image.new('RGBA', mask.size, color)
    image.putalpha(mask)
    return image


def infinity():
    # Continuous, closed ribbon: two spacious counters and a strong diagonal
    # crossing, rather than merely turning the open original on its side.
    im = Image.new('RGBA',(44,30))
    d = ImageDraw.Draw(im)
    def point(t):
        s, c = math.sin(t), math.cos(t)
        return (round(21.5+17*c/(1+s*s)),round(14.5+24*s*c/(1+s*s)))
    for start,end,color in ((0,math.pi/2,INK), (math.pi/2,math.pi,VIOLET),
                            (math.pi,math.pi*1.5,VIOLET), (math.pi*1.5,math.pi*2,INK)):
        points=[point(start+(end-start)*i/300) for i in range(301)]
        d.line(points,fill=color,width=5)
    # Ink goes underneath; violet diagonal bridges across the shared contact.
    center=[point(math.pi/2-0.24+0.48*i/80) for i in range(81)]
    d.line(center,fill=TURN,width=7)
    d.line(center,fill=VIOLET,width=5)
    return im


def centered(canvas, mark, center, maxsize, integer=True):
    ratio=min(maxsize[0]/mark.width,maxsize[1]/mark.height)
    if integer:
        ratio=max(1,int(ratio))
    size=(round(mark.width*ratio),round(mark.height*ratio))
    image=mark.resize(size,NEAREST)
    canvas.paste(image,(round(center[0]-size[0]/2),round(center[1]-size[1]/2)),image)


def wordmark(canvas, center, scale=3):
    marks=[glyph(c) for c in 'seance']
    width=sum(m.width for m in marks)+len(marks)-1
    mask=Image.new('L',(width,26))
    x=0
    for m in marks:
        mask.paste(m,(x,0));x+=m.width+1
    centered(canvas,paint(mask),center,(width*scale,26*scale))


def export_svg(mark, file):
    colors={rgba for rgba in mark.get_flattened_data() if rgba[3]}
    paths=[]
    for rgba in sorted(colors):
        commands=[]
        for y in range(mark.height):
            x=0
            while x<mark.width:
                if mark.getpixel((x,y))!=rgba:
                    x+=1;continue
                end=x+1
                while end<mark.width and mark.getpixel((end,y))==rgba:
                    end+=1
                commands.append(f'M{x} {y}h{end-x}v1h-{end-x}z')
                x=end
        color='#'+''.join(f'{v:02x}' for v in rgba[:3])
        paths.append(f'<path fill="{color}" d="{"".join(commands)}"/>')
    file.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {mark.width} {mark.height}" shape-rendering="crispEdges">\n'
                    '<title>seance / logo study</title>\n'+'\n'.join(paths)+'\n</svg>\n')


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    original=Image.open(ROOT/'ui_mockups/pixel_ribbon_study/master_32.png').convert('RGBA')
    rotated=original.transpose(Image.Transpose.ROTATE_90)
    rotated=rotated.crop(rotated.getbbox())
    loop=infinity()
    loop=loop.crop(loop.getbbox())
    gothic=paint(glyph('s'))
    marks=[('01 / rotation','ribbon on its side', '01_rotated',rotated),
           ('02 / infinity','closed loops', '02_infinity',loop),
           ('03 / gothic s','letter from the existing wordmark', '03_gothic',gothic)]
    sheet=Image.new('RGB',(1440,850),PAPER)
    d=ImageDraw.Draw(sheet)
    d.text((48,29),'seance / symbol or letter?',font=font(30),fill=INK)
    d.text((48,78),'three studies · one palette · current logo unchanged',font=font(17),fill=VIOLET)
    d.line((48,119,1392,119),fill='#ae8297')
    for i,(title,caption,slug,mark) in enumerate(marks):
        cx=i*480+240
        if i:
            d.line((i*480,152,i*480,784),fill='#c79caf')
        d.text((i*480+48,151),title,font=font(20),fill=INK)
        centered(sheet,mark,(cx,347),(330,286))
        wordmark(sheet,(cx,572),3)
        d.text((i*480+48,660),caption,font=font(16),fill=VIOLET)
        for j,size in enumerate((24,32,48)):
            icon=Image.new('RGBA',(size,size))
            centered(icon,mark,(size/2,size/2),(size-4,size-4),integer=False)
            sheet.paste(icon,(i*480+48+j*118,714),icon)
            d.text((i*480+48+j*118,773),f'{size} px',font=font(12,True),fill=VIOLET)
            icon.save(OUT/f'{slug}_{size}.png')
        mark.save(OUT/f'{slug}_master.png')
        export_svg(mark,OUT/f'{slug}.svg')
        hero=Image.new('RGB',(768,768),PAPER)
        centered(hero,mark,(384,318),(450,400))
        wordmark(hero,(384,632),4)
        hero.save(OUT/f'{slug}_preview.png')
        assert set(mark.getchannel('A').get_flattened_data()) <= {0,255}
    sheet.save(OUT/'comparison.png')
    (OUT/'README.md').write_text('''# infinity / gothic initial study

preview only. no shipping icon, splash, runtime or release changed.

- 01: exact previous ribbon master, rotated 90 degrees counterclockwise.
- 02: newly drawn closed infinity ribbon; ink under violet crossing.
- 03: exact s cell geometry from tools/pixel_wordmark.py, not a new font.
- all wordmark lockups use the same existing six-character pixel master.
- transparent native masters, svg cell geometry and 24/32/48 png proofs included.
- small proofs use nearest-neighbour fitting, not hand-hinted app icons.

reproduce: python3 tools/render_ribbon_gothic_study.py
all shapes are hard-edged, with binary alpha. large marks use integer scaling.
''')
    print('study generated; binary alpha verified; application assets unchanged')
    print(OUT/'comparison.png')


if __name__=='__main__':
    main()
