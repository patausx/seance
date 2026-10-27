#!/usr/bin/env python3
"""One requested motion GIF: v9 composition, illustrative data, no runtime.
50fps/200ms cubic-out transition, no overshoot, no blur, no centred carousel.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_main_screen_tiles_study import surface, control
from render_main_screen_unified_info_study import upper, lower
from render_main_screen_unified_study import small_graph, ACTIVE
from render_menu_style_study import heading_mask
from render_main_screen_accordion_study import center_text, trace
from render_universal_sigils_study import caption, OUTSIDE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_tiles_study'
NAMES = ('gran', 'ring', 'chor', 'dly', 'filt', 'vca')
BG = (36, 36, 36)
SPOT = (181, 154, 232)


def mix(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def widths(selected):
    return [160 if i == selected else 32 for i in range(6)]


def panels(name):
    if name == 'ring':
        # Exact accepted v9 focused content (borders painted separately).
        return upper().crop((72, 0, 232, 240)), lower().crop((32, 0, 192, 240))
    t, b = surface(160), surface(160)
    for c in (t, b):
        c.im.paste(ACTIVE, (0, 0, 160, 240))
    t.heading(14, 47, name, 'spot')
    if name == 'gran':
        t.txt(14, 81, 'source window', 'ghost')
        pts = []
        for i in range(132):
            value = math.sin(i * .7) * (0.3 + .7 * abs(math.sin(i * .057)))
            pts.append((14 + i, 123 - round(22 * value)))
        t.d.line(pts, fill=t.p['ghost'])
        for x, w in ((36, 24), (71, 32), (94, 22)):
            t.line((x, 151, x, 155, x+w, 155, x+w, 151), 'spot')
        t.txt(14, 178, '3 grains', 'ghost')
        t.txt(14, 208, 'a > position', 'spot')
        params = (('position', '38%', .38), ('size', '80ms', .5))
    elif name == 'chor':
        t.txt(14, 81, 'voice motion', 'ghost')
        for j, role in enumerate(('ghost', 'spot', 'ink')):
            trace(t, 14, 119+j*17, 132, 12, 1.3, phase=j*.7, role=role)
        t.txt(14, 178, 'wet / dry', 'ghost')
        t.txt(14, 208, 'b > depth', 'spot')
        params = (('rate', '0.8hz', .3), ('depth', '38%', .38))
    else:
        t.txt(14, 81, 'envelope', 'ghost')
        t.line((14, 153, 146, 153))
        t.line((14, 153, 32, 101, 59, 121, 113, 121, 146, 153), 'ink')
        t.line((85, 97, 85, 156), 'spot')
        t.txt(14, 178, 'gate on', 'ghost')
        t.txt(14, 208, 'c > gain', 'spot')
        params = (('gain', '75%', .75), ('attack', '12ms', .2))
    b.txt(14, 22, 'edit >', 'spot')
    b.line((14, 35, 49, 35), 'spot')
    b.d.arc((105, 20, 117, 32), 305, 235, fill=SPOT)
    b.line((111, 18, 111, 26), 'spot')
    b.txt(134, 22, '...', 'ghost')
    for y, (label, val, amount) in zip((77, 143), params):
        control(b, 14, y, 132, label, val, amount, gap=24)
    return t.im, b.im


PANELS = {i: panels(NAMES[i]) for i in (0, 1, 2, 5)}
# Remove all panel backgrounds, borders and shared chrome: details only.
DETAILS = {}
for i, pair in PANELS.items():
    result = []
    for k, im in enumerate(pair):
        mask = Image.new('L', im.size)
        src, dst = im.load(), mask.load()
        for y in range(240):
            for x in range(8, 152):
                if ((45 <= y <= 236) if k == 0 else (14 <= y <= 175)):
                    dst[x,y] = 255 if src[x,y] not in (BG, ACTIVE) else 0
        result.append((im, mask))
    DETAILS[i] = result


def smooth(t):
    t = max(0, min(1, t))
    return t*t*(3-2*t)


def draw(current, selected, label, touch=None):
    t, b = surface(400), surface(320)
    t.heading(40, 8, 'patch table')
    edges = [0]
    for w in current:
        edges.append(round(sum(current[:len(edges)])))
    assert edges[-1] == 320
    for i, name in enumerate(NAMES):
        a, end = edges[i], edges[i+1]
        w = end-a
        extent = max(0, min(1, (w-32)/128))
        for k, c in enumerate((t,b)):
            offset = 40 if k == 0 else 0
            x, r = a+offset, end+offset
            y0, y1 = (40,239) if k == 0 else (0,197)
            fill = mix(BG, ACTIVE, extent)
            color = SPOT if i == selected else (94,94,94)
            c.d.rectangle((x+2,y0,r-3,y1), fill=fill)
            c.d.line((x+2,y0,x+2,y1), fill=color)
            c.d.line((r-3,y0,r-3,y1), fill=color)
            y = y0 if k == 0 else y1
            c.d.line((x+2,y,r-3,y), fill=color)
            if i in DETAILS:
                # Fixed native glyphs/curves; only translation, clipping and opacity.
                alpha = smooth((extent-.60)/.40)
                if alpha > 0:
                    im, mask = DETAILS[i][k]
                    clip = min(152,w-5)
                    if clip > 8:
                        cmask = mask.crop((8,0,clip,240)).point(lambda p: round(p*alpha))
                        c.im.paste(im.crop((8,0,clip,240)), (x+8,0), cmask)
        # Compact title/mark fades away before expanded fixed-size text appears.
        alpha = 1-smooth(extent/.55)
        if alpha > 0:
            cx = 40+(a+end)//2
            title_layer = surface(400)
            title_layer.p = {key: mix(BG,value,alpha) for key,value in title_layer.p.items()}
            center_text(title_layer,cx,55,name,'spot' if i == selected else 'ink')
            small_graph(title_layer,name,cx)
            # Compact marks stay inside a 28px-wide native viewport.
            crop = title_layer.im.crop((cx-12,45,cx+12,160))
            mask = crop.convert('RGB').point(lambda p:p)
            m = Image.new('L',crop.size)
            m.putdata([0 if px == BG else 255 for px in mask.getdata()])
            t.im.paste(crop,(cx-12,45),m)
    for num,x in enumerate((18,54,90,126),1):
        b.txt(x,219,str(num),'spot' if num == 4 else 'ghost')
        if num == 4: b.line((x-5,233,x+10,233),'spot')
    b.txt(304,219,'patch02 *','ghost',right=True)
    if touch:
        # Visual touch indicator on lower screen only, not a new UI control.
        x,y,age = touch
        if age <= 100:
            r = 5+round(age/30)
            b.d.ellipse((x-r,y-r,x+r,y+r),outline=SPOT)
    sheet = Image.new('RGB',(432,558),OUTSIDE)
    caption(sheet,16,10,'seance / synchronized module focus')
    caption(sheet,16,26,label)
    sheet.paste(t.im,(16,44))
    sheet.paste(b.im,(56,300))
    caption(sheet,16,547,'200ms / example data / not runtime')
    return sheet.resize((864,1116),Image.Resampling.NEAREST)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    frames, durations = [], []
    current = widths(1)
    frames.append(draw(current,1,'02 ring')); durations.append(1000)
    # Ordinary neighbour switch, then return to first and jump directly to sixth.
    for old,new,label in ((1,2,'02 ring > 03 chor'),(2,0,'03 chor > 01 gran'),
                          (0,5,'01 gran > 06 vca / direct jump'),(5,1,'06 vca > 02 ring')):
        source, target = list(current), widths(new)
        tx = round(sum(source[:new])+source[new]/2)
        for step in range(11):
            p = step/10
            ease = 1-(1-p)**3
            current = [a+(z-a)*ease for a,z in zip(source,target)]
            frames.append(draw(current,new,label,(tx,115,step*20)))
            durations.append(20 if step < 10 else (1400 if new in (0,5) else 1000))
        assert [round(v) for v in current] == target
    # Shared palette avoids GIF palette pumping between frames.
    palette_source = Image.new('RGB',(864,1116*4))
    for j,i in enumerate((0,11,22,33)):
        palette_source.paste(frames[i],(0,j*1116))
    palette = palette_source.quantize(colors=128,method=Image.Quantize.MEDIANCUT)
    indexed = [im.quantize(palette=palette,dither=Image.Dither.NONE) for im in frames]
    path = OUT/'main-unified-motion-v10.gif'
    indexed[0].save(path,save_all=True,append_images=indexed[1:],duration=durations,
                    loop=0,optimize=False,disposal=1)
    with Image.open(path) as check:
        total = 0
        for i in range(check.n_frames):
            check.seek(i);total += check.info.get('duration',0)
        print(path.relative_to(ROOT),'frames',check.n_frames,'duration_ms',total,'bytes',path.stat().st_size)
    # One temporary inspection sheet; not another user-facing render.
    preview = Image.new('RGB',(864*2,1116))
    preview.paste(frames[0],(0,0)); preview.paste(frames[33],(864,0))
    preview.save('/tmp/seance-motion-preview.png')


if __name__ == '__main__':
    main()
