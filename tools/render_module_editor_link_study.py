#!/usr/bin/env python3
"""Design-only card -> filter editor study, not runtime or hardware capture.

New: Gossip edit affordance in existing target, passive miniature response,
shared title/curve/material and dual unfold. Existing filter host-capture ink
is reused at native geometry; ONLY paper/shade and upper context are recolored.
No app, audio, font, save, build or deployment changes. Motion is scripted.
"""
from datetime import datetime, timezone
from functools import lru_cache
import json
import math
from PIL import Image, ImageDraw, ImageChops, ImageStat, PngImagePlugin
from render_bounce_visual_study import COLORS, ROOT, OUTSIDE, glyph, sha, protected, Canvas, Page
from render_filter_interaction_study import response, nominal

OUT = ROOT/'ui_mockups/module_editor_link_study/card-unfold-v1'
ARCHIVE = ROOT/'ui_mockups/filter_interaction_runtime/raw-final'
PARAMS = dict(raw=5142, res=10485, drive=5898, morph=0, mode='low')
INPUTS = [
    'tools/render_module_editor_link_study.py',
    'tools/render_bounce_visual_study.py', 'tools/render_module_geometry_study.py',
    'tools/render_menu_style_study.py', 'tools/generate_start_menu_labels.py',
    'tools/render_filter_interaction_study.py', 'assets/fonts/Gossip-MedPixel.otf',
    'core/ui/font.cpp', 'platform/3ds/ui_chains.cpp', 'platform/3ds/main.cpp',
    'platform/3ds/chain_columns.h', 'platform/3ds/ui_module_focus_filter.cpp',
    'platform/3ds/filter_response_model.h', 'platform/3ds/filter_interaction.h',
    'platform/3ds/patch_paper.h', 'platform/3ds/ui_palette.h',
]
READY = ['release/module-picker/seance.3dsx', 'release/module-picker/build.json']


def mix(a, b, amount, total=100):
    return tuple((x*(total-amount)+y*amount+total//2)//total for x,y in zip(a,b))


class Surface(Page):
    def __init__(self, image, theme):
        super().__init__(image.width,theme)
        self.im = image.copy(); self.d = ImageDraw.Draw(self.im)

    def heading(self,x,y,text,role='ink'):
        mask,_ = glyph(text,28)
        box = (x,y,x+mask.width,y+mask.height)
        assert 0 <= x < box[2] <= self.im.width and 0 <= y < box[3] <= 240
        for old,label in self.labels:
            assert min(box[2],old[2]) <= max(box[0],old[0]) or min(box[3],old[3]) <= max(box[1],old[1]), (text,label)
        self.labels.append((box,text)); self.im.paste(self.c[role],(x,y),mask)


@lru_cache(None)
def active_fill(theme):
    return mix(Page(320,theme).im.getpixel((0,120)),COLORS[theme]['accent'],7)


def lattice(x,y):
    n = (x*374761393+y*668265263+419)&0xffffffff
    n = ((n^(n>>13))*1274126177)&0xffffffff
    return ((n^(n>>16))&65535)/32767.5-1


def noise(x,y):
    ix,iy = math.floor(x),math.floor(y)
    u,v = x-ix,y-iy; u,v = u*u*(3-2*u),v*v*(3-2*v)
    return (lattice(ix,iy)*(1-u)+lattice(ix+1,iy)*u)*(1-v)+(lattice(ix,iy+1)*(1-u)+lattice(ix+1,iy+1)*u)*v


@lru_cache(None)
def paper(theme):
    # Phase-zero projection of current patch_paper.h, not a GPU screenshot.
    dark,pale = ((17,18,22),(66,61,75)) if theme=='graphite' else (mix((226,190,206),(0,0,0),64,255),(226,190,206))
    im = Image.new('RGB',(64,64))
    for y in range(64):
        py = (y+.5)*496/64-2
        for x in range(64):
            px = (x+.5)*400/64-2
            qx = px+14*math.sin(px/71)+31*math.sin(py/83)
            qy = py+27*math.sin(px/91)
            n = .65*noise(qx/115+2,qy/115+7)+.25*noise(qx/53+9,qy/53+3)+.10*noise(qx/24+5,qy/24+11)
            light = max(0,min(255,math.floor((.5+1.45*n)*255+.5)))
            im.putpixel((x,y),mix(dark,pale,light,255))
    return im.resize((400,496),Image.Resampling.BILINEAR)


def column_layout(count=3,selected=1):
    assert 1 <= count <= 6 and 0 <= selected < count
    left = (320-160-(count+(count<6)-1)*32)//2
    layout = []; x = left
    for i in range(count):
        w = 160 if i==selected else 32
        layout.append((x,w)); x += w
    return layout,(x,32) if count<6 else None


def rail(p,left,right,y,value,live=False):
    p.line(left,y,right,y)
    at = left+int(value*(right-left))
    p.line(at,y-4,at,y+4,'accent'); p.d.rectangle((at-1,y-1,at+1,y+1),fill=p.c['accent'])


@lru_cache(None)
def patch_pages(theme,count,selected):
    layout,add = column_layout(count,selected)
    sheet = paper(theme)
    pages = []
    names = ('play','filt','ghost','dly','vca','rev')
    for top in (True,False):
        p = Surface(sheet.crop((0,0,400,240) if top else (40,256,360,496)),theme)
        offset = 40 if top else 0
        x = layout[selected][0]+offset
        halo = Image.new('RGBA',p.im.size)
        d = ImageDraw.Draw(halo)
        for radius,alpha in reversed(list(enumerate((17,13,10,7,5,3,2,1),1))):
            d.rectangle((x+2-radius,(40 if top else 0)-radius,x+157+radius,(239 if top else 197)+radius),outline=(*p.c['accent'],alpha))
        if top: ImageDraw.Draw(halo).rectangle((0,0,399,35),fill=(0,0,0,0))
        p.im = Image.alpha_composite(p.im.convert('RGBA'),halo).convert('RGB'); p.d = ImageDraw.Draw(p.im)
        for i,(base,w) in enumerate(layout):
            x = base+offset; y,b = (40,239) if top else (0,197)
            role = 'accent' if i==selected else 'rule'
            if i==selected: p.d.rectangle((x+2,y,x+w-3,b),fill=active_fill(theme))
            p.line(x+2,y,x+2,b,role); p.line(x+w-3,y,x+w-3,b,role)
            p.line(x+2,y if top else b,x+w-3,y if top else b,role)
            if top and i!=selected:
                p.txt(x+w//2,55,names[i][:4],'ink','center')
                for k,value in enumerate((.5,.4)):
                    xx = x+w//2-5+k*10
                    p.line(xx,91,xx,151); p.line(xx-2,151-round(value*60),xx+2,151-round(value*60),'ink')
                p.txt(x+w//2,222,str(i+1).zfill(2),'muted','center')
            if not top:
                for dx in (-5,0,5): p.d.rectangle((x+w//2+dx,187,x+w//2+dx+1,188),fill=p.c[role if i==selected else 'muted'])
        if top: p.heading(40,8,'patch table')
        else:
            if add:
                x,w = add; p.txt(x+w//2,22,'+','muted','center'); p.txt(x+w//2,40,'add','muted','center')
            for i in range(4):
                x = 18+i*36; p.txt(x,219,str(i+1),'accent' if i==0 else 'muted')
                if i==0: p.line(x-5,233,x+10,233,'accent')
            p.txt(304,219,'patch01 *','muted','right')
        pages.append(p)
    return *pages,layout,add


def ease(t):
    t = max(0., min(1., t))
    return t*t*(3-2*t)


def lerp(a, b, t):
    return round(a+(b-a)*t)


def blank(width, theme):
    return Surface(Image.new('RGBA', (width, 240)), theme)


def composite(base, layer, amount=1., clip=None):
    layer = layer.copy()
    alpha = layer.getchannel('A').point(lambda a: round(a*max(0., min(1., amount))))
    if clip is not None:
        mask = Image.new('L', base.size)
        ImageDraw.Draw(mask).rectangle(clip, fill=255)
        alpha = ImageChops.multiply(alpha, mask)
    layer.putalpha(alpha)
    return Image.alpha_composite(base.convert('RGBA'), layer).convert('RGB')


def ink_only(image, theme):
    rgba = image.convert('RGBA')
    delta = ImageChops.difference(image, Image.new('RGB', image.size, active_fill(theme)))
    # RGB max, NOT grayscale conversion (small color differences must survive).
    r, g, b = delta.split()
    mask = ImageChops.lighter(ImageChops.lighter(r, g), b).point(lambda a: 255 if a else 0)
    rgba.putalpha(mask)
    return rgba


DB = tuple(response(PARAMS['raw'], PARAMS['res'], 0, 'low', 20*800**(i/116)) for i in range(117))


def curve(width, theme, box):
    """Same scalar model at every size, never downscale a UI screenshot/text."""
    x, y, w, h = box
    p = blank(width, theme)
    points = [(x+round(i*w/116), y+round((12-max(-36., min(12., db)))*h/48)) for i, db in enumerate(DB)]
    fill = mix(active_fill(theme), COLORS[theme]['accent'], 16, 255)
    p.d.polygon([*points, (x+w, y+h), (x, y+h)], fill=(*fill, 255))
    p.line(x, y+round(h/4), x+w, y+round(h/4), 'rule')
    p.line(x, y+h, x+w, y+h, 'rule')
    p.d.line(points, fill=(*p.c['accent'], 255))
    xx = x+round(math.log(nominal(PARAMS['raw'])/20)/math.log(800)*w)
    for yy in range(y-4, y+h, 5):
        p.d.line((xx, yy, xx, min(yy+1, y+h)), fill=(*p.c['accent'], 255))
    p.diamond(xx, y-7, 'accent', False)
    return p.im


def native_archive(theme, screen):
    path = ARCHIVE/(theme+'_low_'+screen+'.ppm')
    receipt = json.loads((ARCHIVE/'capture.json').read_text())
    assert sha(path) == receipt['surface_sha256'][path.name], path
    im = Image.open(path).convert('RGB')
    assert im.size == (400 if screen == 'top' else 320, 240)
    return im


@lru_cache(None)
def editor(theme):
    pages, proofs = [], []
    for screen in ('top', 'bottom'):
        original = native_archive(theme, screen)
        output = original.copy()
        oldshade = (45,43,48) if theme == 'graphite' else (218,181,201)
        newshade = mix(active_fill(theme), COLORS[theme]['accent'], 16, 255)
        source, dest = original.load(), output.load()
        recolored = 0
        for y in range(240):
            bg = source[0,y]
            for x in range(original.width):
                color = source[x,y]
                if color == bg:
                    dest[x,y] = active_fill(theme); recolored += 1
                elif screen == 'top' and color == oldshade:
                    dest[x,y] = newshade; recolored += 1
                else:
                    assert dest[x,y] == color
        if screen == 'top':
            # Archive used slot01. Our illustrative chain is play > filter > ghost.
            ImageDraw.Draw(output).rectangle((322,14,383,25), fill=active_fill(theme))
            p = Surface(output, theme)
            p.txt(382,17,'ch01 / 02','muted','right')
            output = p.im
        pages.append(output)
        proofs.append(dict(screen=screen,archive=str((ARCHIVE/(theme+'_low_'+screen+'.ppm')).relative_to(ROOT)),
                           recolored_background_or_shade_pixels=recolored,
                           foreground_native_except_upper_context=True))
    return tuple(pages), proofs


def action_targets(x):
    # Existing stable-card routing. Top preview is entirely passive.
    return [('edit', (x,10,x+48,44)), ('bypass', (x+48,10,x+112,44)),
            ('more', (x+112,10,x+160,44)), ('cutoff', (x,58,x+160,110)),
            ('res', (x,121,x+160,173)), ('grip', (x+66,174,x+95,198))]


@lru_cache(None)
def card(theme='graphite', proposed=True, count=3, selected=1):
    """Illustrative fixture: active physical-order entry is filter in every probe."""
    top, bottom, layout, _ = patch_pages(theme, count, selected)
    x, w = layout[selected]
    assert w == 160
    pages = []
    for upper, original in ((True,top),(False,bottom)):
        p = Surface(original.im, theme)
        # Replace only the chosen interior; original smoke/neighbours stay exact.
        left = x+40 if upper else x
        p.d.rectangle((left+3,41 if upper else 0,left+w-4,239 if upper else 196), fill=active_fill(theme))
        if upper:
            p.heading(left+14,47,'filter' if proposed else 'filt','accent')
            if proposed:
                p.txt(left+14,83,'low pass')
                p.txt(left+w-15,83,'800hz','ink','right')
                p.im = composite(p.im, curve(400,theme,(left+14,111,131,43)))
                p.d = ImageDraw.Draw(p.im)
                p.txt(left+14,165,'res 32%')
                p.txt(left+w-15,165,'base','muted','right')
            else:
                p.txt(left+14,81,'live control')
                p.txt(left+14,101,'15%','ink')
                p.txt(left+w-15,101,'cutoff','muted','right')
                rail(p,left+14,left+w-15,127,PARAMS['raw']/32767,True)
                p.txt(left+14,149,'target 15%')
                p.txt(left+14,163,'base','ink')
            p.line(left+14,176,left+w-15,176)
            p.txt(left+14,185,'no modulation')
            p.txt(left+14,222,'base controls below')
        else:
            if proposed:
                box = p.word(x+8,32,'edit',28,'accent')
                assert x <= box[0] < box[2] <= x+48 and 10 <= box[1] < box[3] <= 44
                p.line(x+8,39,x+42,39,'accent')
                p.line(x+39,36,x+42,39,'accent')
                p.line(x+42,39,x+39,42,'accent')
            else:
                p.txt(x+12,22,'edit','accent'); p.line(x+12,35,x+35,35,'accent')
            p.txt(x+54,22,'bypass')
            p.txt(x+120,22,'more')
            for i,(name,value,raw) in enumerate((('cutoff','15%',5142),('res','31%',10485))):
                y = 70+i*63
                p.txt(x+14,y,name); p.txt(x+145,y,value,'ink','right')
                rail(p,x+14,x+145,y+24,raw/32767)
            for dx in (-5,0,5):
                p.d.rectangle((x+80+dx,187,x+81+dx,188), fill=p.c['accent'])
            for name,box in action_targets(x):
                if name != 'grip': p.hit(name,box)
        pages.append(p.im)
    return tuple(pages)


def bounds(t, count=3, selected=1):
    x, _ = column_layout(count, selected)[0][selected]
    e = 1-(1-t)**3
    # Shared virtual 400px sheet; lower LCD clips edges after they leave 320px.
    # The editor is NOT squeezed into the picker's 280px window.
    left, right = lerp(x+2,-40,e), lerp(x+157,359,e)
    return (left+40,lerp(40,0,e),right+40,239), (left,0,right,lerp(197,239,e))


def frame_edges(base, theme, box, top, t):
    d = ImageDraw.Draw(base)
    left,y,right,bottom = box
    edge = Image.new('RGBA', base.size)
    pen = ImageDraw.Draw(edge)
    a = 1-ease((t-.60)/.40)
    points = [(left,bottom),(left,y),(right,y),(right,bottom)] if top else [(left,y),(left,bottom),(right,bottom),(right,y)]
    for radius,alpha in reversed(list(enumerate((17,13,10,7,5,3,2,1),1))):
        pen.rectangle((left-radius,y-radius,right+radius,bottom+radius), outline=(*COLORS[theme]['accent'],round(alpha*a)))
    pen.line(points, fill=(*COLORS[theme]['accent'],round(255*a)))
    return composite(base, edge)


@lru_cache(None)
def scene(theme='graphite', t=0., count=3, selected=1):
    closed = card(theme, True, count, selected)
    opened = editor(theme)[0]
    if t == 0: return closed
    if t == 1: return opened
    assert 0 < t < 1
    x, _ = column_layout(count,selected)[0][selected]
    boxes = bounds(t,count,selected)
    screens = []
    e = 1-(1-t)**3
    for index,(start,target,box) in enumerate(zip(closed,opened,boxes)):
        top = index == 0
        origin_x = x+(40 if top else 0)
        original = start.copy()
        if top:
            erase = ImageDraw.Draw(original)
            erase.rectangle((origin_x+14,47,origin_x+60,65), fill=active_fill(theme))
            erase.rectangle((origin_x+11,101,origin_x+148,155), fill=active_fill(theme))
        field = paper(theme).crop((0,0,400,240) if top else (40,256,360,496))
        result = Image.blend(original, field, ease(t/.42))
        ImageDraw.Draw(result).rectangle(box, fill=active_fill(theme))
        old = ink_only(original,theme)
        result = composite(result,old,1-ease(t/.30),(origin_x+3,41 if top else 0,origin_x+156,239 if top else 196))
        target_ink = ink_only(target,theme)
        if top:
            pen = ImageDraw.Draw(target_ink)
            pen.rectangle((18,16,64,34), fill=(0,0,0,0))
            pen.rectangle((32,100,380,185), fill=(0,0,0,0))
        # New controls are revealed at final native coordinates, not stretched.
        result = composite(result,target_ink,ease((t-.28)/.48),box)
        result = frame_edges(result,theme,box,top,t)
        if top:
            moving = curve(400,theme,(lerp(origin_x+14,32,e),lerp(111,110,e),lerp(131,348,e),lerp(43,75,e)))
            handoff = ease((t-.72)/.28)
            result = composite(result,moving,1-handoff,box)
            final_plot = ink_only(target,theme)
            result = composite(result,final_plot,handoff,(32,100,380,185))
            title = blank(400,theme)
            title.c = dict(title.c,accent=mix(COLORS[theme]['accent'],COLORS[theme]['ink'],round(e*255),255))
            title.heading(lerp(origin_x+14,18,e),lerp(47,16,e),'filter','accent')
            result = composite(result,title.im,1.,box)
        screens.append(result)
    return tuple(screens)


def pair(pages):
    top,bottom = pages
    image = Image.new('RGB',(400,496),OUTSIDE)
    image.paste(top,(0,0)); image.paste(bottom,(40,256))
    return image


def board(title, subtitle, cells):
    c = Canvas(24+424*len(cells),626,OUTSIDE)
    c.text(24,14,title,COLORS['graphite']['ink'],scale=2)
    c.text(24,43,subtitle,COLORS['graphite']['muted'])
    for i,(label,image) in enumerate(cells):
        x = 24+i*424
        c.text(x,76,label,COLORS['graphite']['accent'])
        c.im.paste(image,(x,96))
    c.text(24,610,'authored proposal / archived filter ink / no device or input capture',COLORS['graphite']['muted'])
    return c.im


def item(path, size):
    return dict(path=str(path.relative_to(ROOT)),bytes=path.stat().st_size,size=list(size),sha256=sha(path))


def save_png(image, name):
    path = OUT/name
    assert not path.exists()
    big = image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo(); meta.add_text('provenance',__doc__)
    big.save(path,pnginfo=meta)
    with Image.open(path) as decoded: assert decoded.convert('RGB').tobytes() == big.tobytes()
    return item(path,big.size)


def save_motion():
    # Durations show a proposed 200ms open and 160ms return, plus review dwell.
    timeline = [(0.,900)]+[(i/10,20) for i in range(1,10)]+[(1.,20),(1.,1500)]
    timeline += [(i/8,20) for i in range(7,-1,-1)]+[(0.,700)]
    frames = [pair(scene(t=t)) for t,_ in timeline]
    assert frames[0].tobytes() == frames[-1].tobytes()
    atlas = Image.new('RGB',(400,496*len(frames)))
    for i,im in enumerate(frames): atlas.paste(im,(0,496*i))
    palette = atlas.quantize(colors=248,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    colors = palette.getpalette()[:248*3]
    for c in (*COLORS['graphite'].values(),OUTSIDE,active_fill('graphite'),(36,36,36),(0,0,0)): colors.extend(c)
    palette.putpalette(colors)
    indexed = [im.quantize(palette=palette,dither=Image.Dither.NONE) for im in frames]
    encoded,durations = [],[]
    for im,(_,duration) in zip(indexed,timeline):
        if encoded and encoded[-1].tobytes() == im.tobytes(): durations[-1] += duration
        else: encoded.append(im); durations.append(duration)
    big = [im.resize((800,992),Image.Resampling.NEAREST) for im in encoded]
    path = OUT/'card-to-editor-2x.gif'
    big[0].save(path,save_all=True,append_images=big[1:],duration=durations,loop=0,disposal=1,optimize=False,
                comment=b'authored card/editor proposal; native archived filter ink; no device/input/audio capture')
    with Image.open(path) as decoded:
        assert decoded.n_frames == len(big)
        for i,im in enumerate(big):
            decoded.seek(i)
            assert decoded.convert('RGB').tobytes() == im.convert('RGB').tobytes()
            assert decoded.info['duration'] == durations[i]
    errors = [ImageStat.Stat(ImageChops.difference(a,b.convert('RGB'))) for a,b in zip(frames,indexed)]
    return dict(item(path,big[0].size),source_frames=len(frames),gif_frames=len(big),duration_ms=sum(durations),
                timeline=timeline,decoded_quantized_exact=True,
                max_channel_error=max(max(b for a,b in e.extrema) for e in errors),
                worst_mean_channel_error=max(max(e.mean) for e in errors),limitation='global-palette GIF is lossy vs RGB')


def validate():
    layout_cases = geometry_cases = 0
    for theme in COLORS:
        for count in range(1,7):
            for selected in range(count):
                before = card(theme,False,count,selected)
                closed = card(theme,True,count,selected)
                x,_ = column_layout(count,selected)[0][selected]
                # New edit glyph/arrow only; lower params/actions/grip untouched.
                diff = ImageChops.difference(before[1],closed[1])
                ImageDraw.Draw(diff).rectangle((x,10,x+47,43),fill=(0,0,0))
                assert diff.getbbox() is None
                targets = action_targets(x)
                assert all(0 <= a < c <= 320 and 0 <= b < d <= 240 for _,(a,b,c,d) in targets)
                for i,(_,a) in enumerate(targets):
                    for _,b in targets[i+1:]:
                        assert min(a[2],b[2]) <= max(a[0],b[0]) or min(a[3],b[3]) <= max(a[1],b[1])
                for t in (0.,.3,.7,1.):
                    pages = scene(theme,t,count,selected)
                    assert [p.size for p in pages] == [(400,240),(320,240)]
                    if t == 0: assert all(a.tobytes()==b.tobytes() for a,b in zip(pages,closed))
                    if t == 1: assert all(a.tobytes()==b.tobytes() for a,b in zip(pages,editor(theme)[0]))
                    layout_cases += 1
                for i in range(101):
                    a,b = bounds(i/100,count,selected)
                    assert (a[0],a[2]) == (b[0]+40,b[2]+40)
                    assert 0 <= a[0] < a[2] < 400 and -40 <= b[0] < b[2] <= 359
                    assert 0 <= a[1] <= 40 and a[3] == 239 and b[1] == 0 and 197 <= b[3] <= 239
                    geometry_cases += 1
    assert round(nominal(PARAMS['raw'])) == 800
    return dict(layout_frame_cases=layout_cases,aligned_geometry_samples=geometry_cases,
                themes=list(COLORS),counts=[1,2,3,4,5,6],empty_or_add='no editor target / not applicable',
                lower_changes_only_edit_visual=True,lower_actions_targets_unchanged=True,
                labels_nonoverlapping=True,full_size_editor=[400,320],shared_virtual_width=400,
                card_filter_parameters=PARAMS,upper_preview='display model, not measured audio',
                archive_proof={theme:editor(theme)[1] for theme in COLORS})


def main():
    assert not OUT.exists(), 'preserve prior output; choose a new revision'
    inputs = {p:sha(ROOT/p) for p in INPUTS}
    before = protected(); before['Makefile'] = sha(ROOT/'Makefile')
    ready = {p:sha(ROOT/p) for p in READY}
    archive = {str(p.relative_to(ROOT)):sha(p) for p in ARCHIVE.iterdir() if p.is_file()}
    validation = validate()
    OUT.mkdir(parents=True)
    outputs = []
    for theme in COLORS:
        outputs.append(save_png(board('card / becomes the module','one edit target / one response / full native editor',[
            ('01 / linked card',pair(scene(theme,0.))),
            ('02 / both halves unfold',pair(scene(theme,.30))),
            ('03 / full-size editor',pair(scene(theme,1.)))]),'card-editor-'+theme+'-2x.png'))
    outputs.append(save_png(board('edit / anchored to the card','same lower action targets / faders and grip stay separate',[
        ('current-style fixture',pair(card('graphite',False))),
        ('proposal / edit + response',pair(card('graphite',True)))]),'card-before-after-2x.png'))
    outputs.append(save_png(pair(scene()),'linked-card-graphite-2x.png'))
    outputs.append(save_motion())
    assert before == dict(protected(),Makefile=sha(ROOT/'Makefile'))
    assert all(sha(ROOT/p)==h for p,h in inputs.items())
    assert all(sha(ROOT/p)==h for p,h in ready.items())
    assert all(sha(ROOT/p)==h for p,h in archive.items())
    report = dict(created_at=datetime.now(timezone.utc).isoformat(),origin=__doc__,
        status='design only / awaiting user feedback / no runtime approval',
        source_sha256=inputs,protected_sha256=before,ready_sha256=ready,archive_sha256=archive,
        validation=validation,outputs=outputs,
        behavior_proposal=[
            'first tap on a collapsed card selects only; current stable-card edit48x34 hit stays separate',
            'edit valid tap/lift or X opens; A stays manual trigger, not editor open',
            'no long press, whole-card edit hit, auto-preview, sample load or unit construction',
            'consume opener/held tail; a genuinely fresh touch snaps to final geometry BEFORE hit-testing, then uses existing editor gesture ownership; no mandatory second tap',
            'B reverses from last shown phase while opening; in editor editor_back subpage/lens gets first refusal',
            'X opener never becomes editor delete; A keeps trigger/listen semantics; START transport and Y/navigation retain ownership',
            'only actual chain-card entry owns an origin; sequencer p-lock, sample browser, PLAY subpages and L+dpad module switches retain existing routes',
            'return targets same chain/physical slot/identity and latest parameter values; invalid origin uses safe no-morph fallback',
            'scripted motion200ms open160ms close is a proposal, not measured latency; audio transport unaffected in proposed implementation'],
        limits=[
            'author-created Pillow patch fixture and transition, NOT actual runtime, GPU, hardware, FPS or input proof',
            'filter-only prototype; current chain DTO does not contain filter type/morph needed by real preview',
            'mini response needs a bounded read-only scalar snapshot/shared display model, not a screenshot or extra DSP',
            'existing card raw-percent formatting truncates to15/31 while filter editor rounds res to32; scalars do not change on entry',
            'preview uses fixed raw5142/res10485/drive5898/morph0: no PCM/engine telemetry or sample I/O',
            'native archive filter geometry reused; flat active-tile paper/shade and ch01/02 are PROPOSED recolors',
            'title filter display alias on card is proposed; canonical UnitKind/save names remain unchanged',
            'mini graph is Python display-model approximation, not float/ARM exact or a measured spectrum',
            'virtual aligned400px sheet clips outside lower viewport; full editor is NOT squeezed to picker280px',
            'only geometry expands; font sizes and lower controls stay native, smoke stays in screen space',
            'pink and graphite reviewed; other/custom theme contrast and all-module previews not resolved',
            'GIF lossy global palette; PNG native2x lossless; no app/core/shared fonts/assets/ARM/FTP/CIA/userdata changes'])
    (OUT/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for output in outputs: print(output['path'],output['size'],output['sha256'])
    print('pass:',validation['layout_frame_cases'],'layout/frame cases;',validation['aligned_geometry_samples'],'aligned bounds;',len(before),'protected')


if __name__ == '__main__': main()
