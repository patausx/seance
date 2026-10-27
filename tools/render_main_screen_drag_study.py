#!/usr/bin/env python3
"""One requested v11 drag GIF, not application/runtime code.
Bottom grip only; synchronized column drag, reorder preview then commit on drop.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_universal_sigils_study import caption, OUTSIDE
from render_main_screen_tiles_study import surface, control
from render_main_screen_unified_info_study import upper
from render_main_screen_unified_study import small_graph, ACTIVE
from render_main_screen_accordion_study import center_text

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_tiles_study'
ORDER = ('gran', 'ring', 'chor', 'dly', 'filt', 'vca')
REST = tuple(n for n in ORDER if n != 'ring')
SPOT = (181, 154, 232)
BG = (36, 36, 36)


def grip(c, cx, active=False):
    for dx in (-5, 0, 5):
        c.d.rectangle((cx+dx, 187, cx+dx+1, 188),
                      fill=SPOT if active else c.p['ghost'])


def tiles(name):
    width = 160 if name == 'ring' else 32
    top, bot = surface(width), surface(width)
    chosen = name == 'ring'
    color = SPOT if chosen else top.p['rail']
    for k,c in enumerate((top,bot)):
        y0,y1 = (40,239) if k == 0 else (0,197)
        if chosen:
            c.d.rectangle((2,y0,width-3,y1),fill=ACTIVE)
        c.d.line((2,y0,2,y1),fill=color)
        c.d.line((width-3,y0,width-3,y1),fill=color)
        y = y0 if k == 0 else y1
        c.d.line((2,y,width-3,y),fill=color)
    if chosen:
        # Preserve the v9 example waveform/sigil panel, no invented live data.
        top.im.paste(upper().crop((72,40,232,240)),(0,40))
        bot.txt(12,22,'edit','spot')
        bot.line((12,35,35,35),'spot')
        bot.txt(54,22,'bypass','ghost')
        bot.txt(120,22,'more','ghost')
        control(bot,14,70,132,'freq','440hz',.43,gap=24)
        control(bot,14,133,132,'mix','68%',.68,gap=24)
    else:
        center_text(top,16,55,name,'ink')
        small_graph(top,name,16)
    grip(bot,width//2,chosen)
    return top.im,bot.im


TILES = {n:tiles(n) for n in ORDER}


def positions(gap):
    return {n:i*32+(160 if i >= gap else 0) for i,n in enumerate(REST)}


def smooth(t):
    return t*t*(3-2*t)


def draw(xs,ring_x,gap,label,held=False):
    top,bot = surface(400),surface(320)
    top.heading(40,8,'patch table')
    if held:
        # Empty destination placeholder, same native coordinates on both LCDs.
        for c,ox,y0,y1 in ((top,40,40,239),(bot,0,0,197)):
            a,b = ox+gap*32+2,ox+gap*32+157
            for y in range(y0,y1,8):
                c.line((a,y,a,min(y+3,y1)),'rail')
                c.line((b,y,b,min(y+3,y1)),'rail')
    for name in REST:
        x = round(xs[name])
        top.im.paste(TILES[name][0].crop((0,40,32,240)),(40+x,40))
        bot.im.paste(TILES[name][1].crop((0,0,32,198)),(x,0))
    x = round(ring_x)
    assert 0 <= x <= 160
    # Floating selected column is composited LAST, above shifting neighbours.
    top.im.paste(TILES['ring'][0].crop((0,40,160,240)),(40+x,40))
    bot.im.paste(TILES['ring'][1].crop((0,0,160,198)),(x,0))
    if held:
        # No scale/rotation/shadow/blur. Small insertion caret + stylus contact.
        dest = gap*32+80
        bot.line((dest-4,206,dest,202,dest+4,206),'spot')
        bot.d.ellipse((x+69,177,x+91,199),outline=SPOT)
    for i,cx in enumerate((18,54,90,126),1):
        bot.txt(cx,219,str(i),'spot' if i == 4 else 'ghost')
        if i == 4:bot.line((cx-5,233,cx+10,233),'spot')
    bot.txt(304,219,'patch02 *','ghost',right=True)
    sheet = Image.new('RGB',(432,558),OUTSIDE)
    caption(sheet,16,10,'seance / drag the bottom grip')
    caption(sheet,16,26,label)
    sheet.paste(top.im,(16,44));sheet.paste(bot.im,(56,300))
    caption(sheet,16,547,'order commits on drop / host concept')
    return sheet.resize((864,1116),Image.Resampling.NEAREST)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    frames,durations = [],[]
    def add(xs,x,gap,label,held=False,ms=40):
        frames.append(draw(xs,x,gap,label,held));durations.append(ms)
    xs = positions(1)
    add(xs,32,1,'ring 02 / edit - bypass - more',ms=1100)
    for start,end in ((1,4),(4,1)):
        add(xs,start*32,start,f'hold bottom grip / ring {start+1:02}',True,240)
        for i in range(1,31):
            x = 32*(start+(end-start)*smooth(i/30))
            gap = max(0,min(5,int((x+16)//32)))
            target = positions(gap)
            xs = {n:xs[n]+(target[n]-xs[n])*.5 for n in REST}
            add(xs,x,gap,f'drag / preview {start+1:02} > {gap+1:02}',True)
        # Hold briefly at target so the gap becomes clear before release.
        for i in range(4):
            target = positions(end)
            xs = {n:xs[n]+(target[n]-xs[n])*.5 for n in REST}
            add(xs,end*32,end,f'preview {end+1:02} / release to place',True)
        xs = positions(end)
        final = list(REST);final.insert(end,'ring')
        assert final[end] == 'ring' and sorted(final) == sorted(ORDER)
        add(xs,end*32,end,f'drop / ring now {end+1:02}',ms=1500)
    palette = frames[0].quantize(colors=128,method=Image.Quantize.MEDIANCUT)
    indexed = [im.quantize(palette=palette,dither=Image.Dither.NONE) for im in frames]
    path = OUT/'main-unified-drag-v11.gif'
    indexed[0].save(path,save_all=True,append_images=indexed[1:],duration=durations,
                    loop=0,optimize=False,disposal=1)
    with Image.open(path) as im:
        total=0
        for i in range(im.n_frames):
            im.seek(i);total+=im.info.get('duration',0)
        print(path.relative_to(ROOT),'frames',im.n_frames,'ms',total,'bytes',path.stat().st_size)
    # Temporary inspection of initial/moving/final states, not a second deliverable.
    preview=Image.new('RGB',(1296,558))
    for j,i in enumerate((0,20,36)):
        preview.paste(frames[i].resize((432,558),Image.Resampling.NEAREST),(j*432,0))
    preview.save('/tmp/seance-drag-preview.png')


if __name__ == '__main__':
    main()
