#!/usr/bin/env python3
"""One isolated-arrow press GIF; offline proposal, no menu/runtime changes.

Uses the approved permanent RGBA arrow without changing its pixel silhouette.
Button down sends a soft accent front from right to left in 280ms. Hold stays
fully accented; release restores the original gradient in 360ms.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_module_geometry_study import Canvas

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/gothic_arrow_study'
SOURCE=Image.open(OUT/'arrow-gradient-native.png').convert('RGBA')
ALPHA=SOURCE.getchannel('A')
LEFT,_,RIGHT,_=ALPHA.getbbox()
LIGHT=(244,244,244)
ACCENT=(181,154,232)
PAPER=(36,36,39)
CAPTION=(184,180,192)
DOWN,FILL,UP,RETURN=800,280,1760,360
DURATION,FRAME_MS=3400,20
assert SOURCE.size==(152,72)


def ease(t):
    t=max(0.,min(1.,t))
    return t*t*(3-2*t)


def color(t):
    return tuple(round(a+(b-a)*t) for a,b in zip(LIGHT,ACCENT))


def arrow_at(ms):
    if ms<=DOWN or ms>=UP+RETURN:
        return SOURCE.copy()
    out=Image.new('RGBA',SOURCE.size)
    draw=ImageDraw.Draw(out)
    fill=ease((ms-DOWN)/FILL)
    front=(RIGHT+12)*(1-fill)+(LEFT-12)*fill
    release=1-ease((ms-UP)/RETURN) if ms>UP else 1
    for x in range(SOURCE.width):
        base=max(0.,min(1.,(x-LEFT)/(RIGHT-LEFT-1)))
        weight=ease((x-front+12)/24)*release
        draw.line((x,0,x,SOURCE.height-1),fill=color(base+(1-base)*weight)+(255,))
    out.putalpha(ALPHA)
    return out


def frame(ms):
    im=arrow_at(ms)
    c=Canvas(1040,620,bg=PAPER)
    c.text(32,24,'gothic arrow / press fills with accent',CAPTION)
    state='button down' if DOWN<=ms<DOWN+FILL else ('held' if DOWN+FILL<=ms<UP else ('release' if UP<=ms<UP+RETURN else 'idle'))
    c.text(1008,24,state,ACCENT if DOWN<=ms<UP else CAPTION,right=True)
    big=im.resize((912,432),Image.Resampling.NEAREST)
    c.im.paste(big,(64,54),big)
    c.text(32,533,'native / 152 x 72',CAPTION)
    c.im.paste(im,(444,517),im)
    c.text(32,599,'280ms fill / held accent / 360ms return / motion study only',CAPTION)
    return c.im


def palette():
    # Every color lies on the same ink->accent line. Reserve original colors
    # too, so the approved idle arrow survives GIF encoding byte for byte.
    colors=[PAPER,CAPTION,ACCENT,LIGHT]
    colors += [rgb[:3] for rgb in SOURCE.get_flattened_data() if rgb[3]]
    colors += [color(i/4096) for i in range(4097)]
    colors=list(dict.fromkeys(colors))
    assert len(colors)<=256
    colors += [PAPER]*(256-len(colors))
    result=Image.new('P',(1,1))
    result.putpalette([v for rgb in colors for v in rgb])
    return result


def main():
    assert arrow_at(0).tobytes()==SOURCE.tobytes()==arrow_at(DURATION).tobytes()
    held=arrow_at(DOWN+FILL)
    assert held.getchannel('A').tobytes()==ALPHA.tobytes()
    assert all(p[:3]==ACCENT for p in held.get_flattened_data() if p[3])
    pal=palette()
    palette_bytes=pal.getpalette()
    lookup={tuple(palette_bytes[i:i+3]):i//3 for i in range(0,768,3)}
    frames=[]
    cache={}
    for ms in range(0,DURATION,FRAME_MS):
        key=0 if ms<DOWN or ms>=UP+RETURN else (DOWN+FILL if DOWN+FILL<=ms<UP else ms)
        if key not in cache:
            rendered=frame(ms)
            # Pillow's supplied-palette quantizer uses an approximate RGB cube
            # lookup. Index directly to keep this narrow gradient exact.
            indexed=Image.new('P',rendered.size)
            indexed.putpalette(palette_bytes)
            indexed.putdata([lookup[p] for p in rendered.get_flattened_data()])
            assert indexed.convert('RGB').tobytes()==rendered.tobytes()
            cache[key]=indexed
        frames.append(cache[key])
    path=OUT/'gothic-arrow-press-6x.gif'
    frames[0].save(path,save_all=True,append_images=frames[1:],duration=FRAME_MS,loop=0,disposal=1,optimize=False)
    with Image.open(path) as check:
        total=0
        for n in range(check.n_frames):
            check.seek(n)
            total+=check.info['duration']
        assert total==DURATION
    print(path)


if __name__=='__main__':
    main()
