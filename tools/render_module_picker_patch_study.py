#!/usr/bin/env python3
"""Picker as a patch-screen disclosure, DESIGN ONLY / authored Pillow.

Right-hand names use existing Gossip28; left filters/metadata use existing 6x8.
Frozen common patch paper, before-open column geometry, small sharp add anchor,
no sidebar slabs, table divider/header/footer rules, inspector, hold or preview.
The background is an illustrative projection of current source, NOT C++/3DS capture.
"""
from functools import lru_cache
from datetime import datetime, timezone
import hashlib
import json
import math

from PIL import Image, ImageDraw, ImageChops, PngImagePlugin
from render_bounce_visual_study import Page, Canvas, COLORS, ROOT, OUTSIDE, sha, protected, glyph, dual
from render_module_quick_picker_study import BUILTINS, CUSTOM, Entry, filters, subset, inside

OUT = ROOT/'ui_mockups/module_picker_study/patch-disclosure-v5'
INPUTS = [
    'tools/render_module_picker_patch_study.py', 'tools/render_module_quick_picker_study.py',
    'tools/render_bounce_visual_study.py', 'tools/render_module_geometry_study.py',
    'tools/render_menu_style_study.py', 'tools/generate_start_menu_labels.py',
    'core/ui/font.cpp', 'core/ui/font.h', 'assets/fonts/Gossip-MedPixel.otf',
    'platform/3ds/ui_chains.cpp', 'platform/3ds/ui_picker.cpp', 'platform/3ds/chain_columns.h',
    'platform/3ds/patch_paper.h', 'platform/3ds/sigil_surface.h',
    'platform/3ds/ui_palette.h', 'platform/3ds/menu_style.cpp', 'platform/3ds/menu_labels_data.h',
]
READY = ('release/module-usability/seance.3dsx', 'release/sample-load-speed/seance.3dsx')
FIXTURE = ('play', 'filt', 'ghost', 'dly', 'vca', 'rev')
VALUES = {
    'play': (('note', 'c4', .5), ('gain', '75%', .75)),
    'filt': (('cutoff', '35%', .35), ('res', '22%', .22)),
    'ghost': (('tone', '50%', .5), ('mix', '40%', .4)),
    'dly': (('time', '40%', .4), ('mix', '30%', .3)),
    'vca': (('gain', '75%', .75), ('attack', '10%', .1)),
    'rev': (('size', '60%', .6), ('mix', '30%', .3)),
}
ROW_Y, ROW_H, VISIBLE = 52, 27, 6


def mix(a, b, weight, total=100):
    return tuple((x*(total-weight)+y*weight+total//2)//total for x, y in zip(a, b))


def lattice(x, y):
    n = (x*374761393+y*668265263+419) & 0xffffffff
    n = ((n^(n>>13))*1274126177) & 0xffffffff
    return ((n^(n>>16)) & 65535)/32767.5-1


def noise(x, y):
    ix, iy = math.floor(x), math.floor(y)
    u, v = x-ix, y-iy
    u, v = u*u*(3-2*u), v*v*(3-2*v)
    a = lattice(ix, iy)*(1-u)+lattice(ix+1, iy)*u
    b = lattice(ix, iy+1)*(1-u)+lattice(ix+1, iy+1)*u
    return a*(1-v)+b*v


@lru_cache(None)
def paper(theme, phase=0):
    """Current 64x64/400x496 design law projected with Python floats/Pillow.

    Float precision and final resampling are not a GPU/ARM pixel equivalence test.
    No new paper recipe: same endpoints, lattice, warp, quantized light and phase.
    """
    angle = math.tau*(phase % 18000)/18000
    s, c = math.sin(angle), math.cos(angle)
    dark, pale = ((17,18,22), (66,61,75)) if theme == 'graphite' else (mix((226,190,206), (0,0,0), 64, 255), (226,190,206))
    image = Image.new('RGB', (64,64))
    for y in range(64):
        py = (y+.5)*496/64-2
        rowwarp = 31*math.sin(py/83+.32*s)
        for x in range(64):
            px = (x+.5)*400/64-2
            qx = px+14*math.sin(px/71-.24*s)+18*s+rowwarp
            qy = py+27*math.sin(px/91+.30*s)+18*(c-1)
            n = (.65*noise(qx/115+2, qy/115+7)
                 +.25*noise(qx/53+9+.12*s, qy/53+3+.10*(c-1))
                 +.10*noise(qx/24+5-.10*s, qy/24+11))
            light = max(0, min(255, math.floor((.5+1.45*n)*255+.5)))
            image.putpixel((x,y), mix(dark,pale,light,255))
    return image.resize((400,496), Image.Resampling.BILINEAR)


class Surface(Page):
    def __init__(self, image, theme):
        super().__init__(image.width, theme)
        assert image.height == 240
        self.im = image.copy()
        self.d = ImageDraw.Draw(self.im)

    def heading(self, x, y, text, role='ink'):
        mask, _ = glyph(text,28)
        box = (x,y,x+mask.width,y+mask.height)
        assert inside(box,(0,0,self.im.width,240)), (text,box)
        for old, name in self.labels:
            assert min(box[2],old[2]) <= max(box[0],old[0]) or min(box[3],old[3]) <= max(box[1],old[1]), (text,name)
        self.labels.append((box,text))
        self.im.paste(self.c[role],box[:2],mask)


def column_layout(count=3, selected=1):
    assert 0 <= count <= 6
    items = count+(count<6)
    left = (320-160-(items-1)*32)//2
    x = left; result = []
    for i in range(count):
        w = 160 if i == selected else 32
        result.append((x,w)); x += w
    add = (x,160 if selected is None else 32) if count < 6 else None
    assert (add[0]+add[1] if add else x) == 320-left
    return result, add


def glow(p, x, top):
    # Existing eight one-pixel falloff shells around the selected strip.
    overlay = Image.new('RGBA',p.im.size)
    d = ImageDraw.Draw(overlay)
    y, h = (40,200) if top else (0,198)
    for radius, alpha in reversed(list(enumerate((17,13,10,7,5,3,2,1),1))):
        d.rectangle((x+2-radius,y-radius,x+157+radius,y+h-1+radius), outline=(*p.c['accent'],alpha))
    # Same conceptual top/bottom clipping; this is authored raster coverage.
    clip = Image.new('L',p.im.size)
    ImageDraw.Draw(clip).rectangle((0,36 if top else 0,p.im.width-1,239 if top else 205),fill=255)
    overlay.putalpha(ImageChops.multiply(overlay.getchannel('A'),clip))
    p.im = Image.alpha_composite(p.im.convert('RGBA'),overlay).convert('RGB')
    p.d = ImageDraw.Draw(p.im)


def rail(p,left,right,y,value,live=False):
    p.line(left,y,right,y)
    at = left+round(value*(right-left))
    if live:
        p.line(at,y-4,at,y+4,'muted')
    p.line(at,y-4,at,y+4,'accent')
    p.d.rectangle((at-1,y-1,at+1,y+1),fill=p.c['accent'])


def sharp_anchor(p, layout, add, selected, replace=False):
    if replace:
        x,w = layout[selected]
        p.txt(x+w-40,22,'more','accent')
        return x+w-28,32
    assert add
    center = add[0]+add[1]//2
    p.txt(center,22,'+','accent','center')
    p.txt(center,40,'add','accent','center')
    return center,48


@lru_cache(None)
def patch_pages(theme='graphite',count=3,selected=1,phase=0):
    if count == 0:
        selected = None
    layout, add = column_layout(count,selected)
    sheet = paper(theme,phase)
    top = Surface(sheet.crop((0,0,400,240)),theme)
    bottom = Surface(sheet.crop((40,256,360,496)),theme)
    static = Page(320,theme).im
    active_color = mix(static.getpixel((0,120)),COLORS[theme]['accent'],7)
    for screen,p in ((True,top),(False,bottom)):
        offset = 40 if screen else 0
        if selected is not None:
            glow(p,layout[selected][0]+offset,screen)
        for i,(bx,w) in enumerate(layout):
            x,right = bx+offset,bx+offset+w-3
            y0,y1 = (40,239) if screen else (0,197)
            chosen = i == selected
            role = 'accent' if chosen else 'rule'
            if chosen:
                p.d.rectangle((x+2,y0,right,y1),fill=active_color)
            p.line(x+2,y0,x+2,y1,role)
            p.line(right,y0,right,y1,role)
            p.line(x+2,y0 if screen else y1,right,y0 if screen else y1,role)
            name = FIXTURE[i]
            values = VALUES[name]
            if screen:
                if chosen:
                    p.heading(x+14,47,name,'accent')
                    param,value,v = values[0]
                    p.txt(x+14,81,'live control')
                    p.txt(x+14,101,value,'ink')
                    p.txt(right-12,101,param,'muted','right')
                    rail(p,x+14,right-12,127,v,True)
                    p.txt(x+14,149,'target '+value)
                    p.txt(x+14,163,'base','ink')
                    p.line(x+14,176,right-12,176)
                    p.txt(x+14,185,'no modulation')
                    p.txt(x+14,222,'base controls below')
                else:
                    p.txt(x+w//2,55,name[:4],'ink','center')
                    for k,(_,_,v) in enumerate(values):
                        xx = x+w//2-5+k*10
                        p.line(xx,91,xx,151)
                        yy = 151-round(v*60)
                        p.line(xx-2,yy,xx+2,yy,'ink')
                    p.txt(x+w//2,222,str(i+1).zfill(2),'muted','center')
            else:
                if chosen:
                    p.txt(x+12,22,'edit','accent')
                    p.line(x+12,35,x+35,35,'accent')
                    p.txt(x+54,22,'bypass')
                    p.txt(x+w-40,22,'more')
                    for k,(label,value,v) in enumerate(values):
                        y = 70+k*63
                        p.txt(x+14,y,label)
                        p.txt(right-12,y,value,'ink','right')
                        rail(p,x+14,right-12,y+24,v)
                for dx in (-5,0,5):
                    p.d.rectangle((x+w//2+dx,187,x+w//2+dx+1,188),fill=p.c['accent' if chosen else 'muted'])
    top.heading(40,8,'patch table')
    if not count:
        top.txt(200,136,'empty chain / add a module below','muted','center')
    if add:
        center=add[0]+add[1]//2
        bottom.txt(center,22,'+','muted','center')
        bottom.txt(center,40,'add','muted','center')
    for i in range(4):
        x = 18+i*36
        bottom.txt(x,219,str(i+1),'accent' if i == 0 else 'muted')
        if i == 0:bottom.line(x-5,233,x+10,233,'accent')
    bottom.txt(304,219,'patch01 *','muted','right')
    return top,bottom,layout,add


@lru_cache(None)
def blurred_patch(theme='graphite',count=3,selected=1,phase=0):
    top,bottom,_,_ = patch_pages(theme,count,selected,phase)
    # A transparent wash of the SAME frozen paper quiets old labels under the
    # catalogue. No rectangle edge, extra blur pass, new paper phase or texture.
    # This 42% lower-only veil is proposed styling, not existing runtime behavior.
    lower = Image.blend(blur(bottom.im,theme),paper(theme,phase).crop((40,256,360,496)),.42)
    return blur(top.im,theme),lower


def blur(image,theme):
    """Same clamped separable kernel/24% paper as sigil_surface.h.

    BOX-downsampling a finished authored page is an approximation of native
    command-coverage accumulation; Pillow bilinear is not PICA filtering.
    """
    small = image.resize((image.width//2,120),Image.Resampling.BOX)
    width=small.width
    for axis in (0,1):
        src=small.load(); out=Image.new('RGB',small.size)
        dst=out.load()
        for y in range(120):
            for x in range(width):
                a=src[max(0,x-1),y] if axis == 0 else src[x,max(0,y-1)]
                b=src[x,y]
                c=src[min(width-1,x+1),y] if axis == 0 else src[x,min(119,y+1)]
                dst[x,y]=tuple((aa+2*bb+cc+2)//4 for aa,bb,cc in zip(a,b,c))
        small=out
    base=Page(image.width,theme).im
    for y in range(120):
        color=base.getpixel((0,y*2))
        for x in range(width):
            small.putpixel((x,y),mix(small.getpixel((x,y)),color,24))
    return small.resize(image.size,Image.Resampling.BILINEAR)


def fit_gothic(text,width=158):
    if glyph(text,28)[0].width <= width:return text
    while text and glyph(text+'...',28)[0].width > width:text=text[:-1]
    assert text
    return text.rstrip()+'...'


def clipped_native(text,width):
    max_chars=width//6
    return text if len(text)<=max_chars else text[:max_chars-3]+'...'


def picker(theme='graphite',group='all',entries=BUILTINS,selected_id='builtin.Filter',
           count=3,selected_slot=1,replace=False,module_start=None,filter_start=None):
    if not count:selected_slot=None
    assert replace or count < 6, 'full chain cannot append'
    _,_,layout,add=patch_pages(theme,count,selected_slot)
    frozen_top,frozen_bottom=blurred_patch(theme,count,selected_slot)
    p=Surface(frozen_bottom,theme)
    ax,ay=sharp_anchor(p,layout,add,selected_slot,replace)
    p.txt(18,24,'ch01 / '+('replace' if replace else 'add'))
    choices=filters(entries)
    assert group in choices
    view=subset(entries,group)
    ids=[e.id for e in view]
    assert len(ids)==len(set(ids))
    cursor=ids.index(selected_id) if selected_id in ids else 0
    if module_start is None:module_start=max(0,min(cursor-3,len(view)-VISIBLE))
    module_start=max(0,min(module_start,max(0,len(view)-VISIBLE)))
    cat_index=choices.index(group)
    if filter_start is None:filter_start=max(0,min(cat_index-3,len(choices)-VISIBLE))
    filter_start=max(0,min(filter_start,max(0,len(choices)-VISIBLE)))
    filters_out=[];rows_out=[]
    # Quiet left words, no panel, selection slab or vertical table divider.
    for row,name in enumerate(choices[filter_start:filter_start+VISIBLE]):
        y=ROW_Y+row*ROW_H
        chosen=name==group
        label=clipped_native(name,66)
        p.txt(20,y+9,label,'accent' if chosen else 'muted')
        if chosen:p.line(20,y+20,20+len(label)*6-2,y+20,'accent')
        box=(8,y,96,y+ROW_H)
        assert inside(p.labels[-1][0],box)
        p.hit('filter:'+name,box);filters_out.append((name,box))
    if len(choices)>VISIBLE:
        p.line(94,56,94,208)
        fy=58+round(filter_start*147/max(1,len(choices)-VISIBLE))
        p.line(93,fy,95,fy,'muted')
    # Open branch from the existing sharp +add/more anchor, not a new window title.
    p.line(ax,ay,ax,50,'accent')
    p.line(110,50,ax,50,'rule')
    p.line(110,50,110,204,'rule')
    if not view:
        p.txt(126,85,'no modules','muted')
        p.txt(126,103,'choose a filter','muted')
    for row,entry in enumerate(view[module_start:module_start+VISIBLE]):
        y=ROW_Y+row*ROW_H
        chosen=module_start+row==cursor
        p.line(110,y+13,118,y+13,'accent' if chosen else 'rule')
        if chosen:p.diamond(118,y+13,'accent',True)
        label=p.word(128,y+20,fit_gothic(entry.name,156),28,'accent' if chosen else 'ink')
        box=(116,y,286,y+ROW_H)
        assert inside(label,box),(entry,label,box)
        p.hit(entry.id,box);rows_out.append((entry.id,box))
    if len(view)>VISIBLE:
        p.line(303,59,303,207)
        cy=61+round(module_start*144/(len(view)-VISIBLE))
        # Existing five-pixel menu bead, not the browser's heavy thumb.
        p.d.rectangle((302,cy-2,304,cy+2),fill=p.c['accent'])
        p.d.rectangle((301,cy-1,305,cy+1),fill=p.c['accent'])
        p.hit('module scrollbar',(288,52,320,214))
    p.txt(18,228,'tap / a '+('replace' if replace else 'add') if view else 'choose filter')
    p.txt(188,228,str(len(view))+' total','muted','right')
    p.txt(304,228,'b close','muted','right')
    p.hit('close',(246,216,312,240))
    top=Surface(frozen_top,theme)
    # No inspector: same top image for every catalogue filter and focus.
    assert top.im.tobytes()==frozen_top.tobytes()
    return top,p,dict(labels=p.labels,targets=p.hits,module_rows=rows_out,filter_rows=filters_out,
        count=len(view),group=group,module_start=module_start,filter_start=filter_start,
        selected_id=ids[cursor] if ids else None,anchor=[ax,ay],replace=replace,
        upper_sha256=hashlib.sha256(top.im.tobytes()).hexdigest(),
        backdrop_sha256=hashlib.sha256(frozen_bottom.tobytes()).hexdigest())


def board(title,subtitle,cells,dual_screen=True):
    width,height=(400,496) if dual_screen else (320,240)
    c=Canvas(24+len(cells)*(width+24),height+130,OUTSIDE)
    c.text(24,14,title,COLORS['graphite']['ink'],scale=2)
    c.text(24,43,subtitle,COLORS['graphite']['muted'])
    for i,(label,image) in enumerate(cells):
        x=24+i*(width+24)
        c.text(x,76,label,COLORS['graphite']['accent'])
        c.im.paste(image,(x,96))
    c.text(24,height+114,'authored study / same frozen patch / not runtime capture',COLORS['graphite']['muted'])
    return c.im


def save(image,name):
    path=OUT/name;assert not path.exists()
    big=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
    meta=PngImagePlugin.PngInfo()
    meta.add_text('provenance',__doc__)
    meta.add_text('backdrop','Authored current-geometry fixture, Python approximation of current paper/blur; before-open columns intentionally retained. Not device, C++ capture, project data or DSP.')
    big.save(path,pnginfo=meta)
    with Image.open(path) as im:
        assert im.convert('RGB').tobytes()==big.tobytes()
        assert im.resize(image.size,Image.Resampling.NEAREST).tobytes()==image.tobytes()
    return dict(path=str(path.relative_to(ROOT)),size=list(big.size),bytes=path.stat().st_size,sha256=sha(path))


def main():
    assert not OUT.exists(),'preserve prior media: use a new revision'
    source={s:sha(ROOT/s) for s in INPUTS}
    before=protected();before['Makefile']=sha(ROOT/'Makefile')
    ready={s:sha(ROOT/s) for s in READY}
    previous={str(p.relative_to(ROOT)):sha(p) for p in OUT.parent.rglob('*') if p.is_file()}
    cases=0;layouts={};top_hashes={}
    for theme in COLORS:
        for entry in BUILTINS:
            for group in ('all',entry.group):
                top,bottom,layout=picker(theme,group,selected_id=entry.id)
                assert entry.id in dict(layout['module_rows'])
                if theme in top_hashes:assert top_hashes[theme]==layout['upper_sha256']
                top_hashes[theme]=layout['upper_sha256'];cases+=1
        for entry in (e for e in CUSTOM if e.user):
            picker(theme,'user',CUSTOM,entry.id);cases+=1
        picker(theme,'user');cases+=1
        for count in (0,1,2,3,5):
            picker(theme,count=count,selected_slot=0 if count else None);cases+=1
        for selected in range(6):
            picker(theme,count=6,selected_slot=selected,replace=True);cases+=1
    size_cases=[]
    for n in (0,1,6,7,28,31,150,300):
        data=[Entry('probe.'+str(i),'module '+str(i),'module '+str(i),'group '+str(i%12),i%2==0) for i in range(n)]
        for at in sorted({0,n//2,max(0,n-1)}):
            e=data[at] if data else None
            for group in ('all',e.group if e else 'user'):
                _,_,layout=picker(entries=data,selected_id=e.id if e else None,group=group)
                assert len(layout['module_rows'])<=6 and len(layout['filter_rows'])<=6
                if e:assert e.id in dict(layout['module_rows'])
                size_cases.append([n,at,group]);cases+=1
    OUT.mkdir(parents=True)
    outputs=[]
    for theme in COLORS:
        closed_top,closed_bottom,_,_=patch_pages(theme)
        top,bottom,layouts[theme+'/all']=picker(theme)
        outputs.append(save(dual(top,bottom),'patch-picker-'+theme+'-dual-2x.png'))
        outputs.append(save(board('picker / part of the patch','same paper and columns / no separate browser panel',[
            ('patch screen / before add',dual(closed_top,closed_bottom)),
            ('tap add / catalogue opens',dual(top,bottom))]),'patch-to-picker-'+theme+'-2x.png'))
        filtered_top,filtered,layouts[theme+'/time']=picker(theme,'time',selected_id='builtin.Ghost')
        assert filtered_top.im.tobytes()==top.im.tobytes()
        if theme=='graphite':
            outputs.append(save(board('gothic names / quiet filters','tap a category / tap a module / no preview or hold',[
                ('all / scrollable catalogue',bottom.im),('time / same frozen backdrop',filtered.im)],False),
                'picker-filters-graphite-2x.png'))
    _,user,layouts['user']=picker('graphite','user',CUSTOM,'fiction.velvet')
    _,empty,layouts['empty']=picker('graphite','user')
    _,replacement,layouts['replace']=picker('graphite',replace=True)
    outputs.append(save(board('same catalogue / other states','fictional user names / empty view / replace in a patch',[
        ('user / fictional examples',user.im),('empty / no module added',empty.im),('replace / anchored to more',replacement.im)],False),
        'picker-context-cases-2x.png'))
    after=protected();after['Makefile']=sha(ROOT/'Makefile')
    assert before==after
    assert source=={s:sha(ROOT/s) for s in INPUTS}
    assert ready=={s:sha(ROOT/s) for s in READY}
    assert all(sha(ROOT/s)==h for s,h in previous.items())
    report=dict(created_at=datetime.now(timezone.utc).isoformat(),origin=__doc__,
        status='unapproved visual proposal; runtime/ARM/FTP not authorized',
        source_sha256=source,protected_sha256=before,ready_sha256=ready,previous_sha256=previous,
        outputs=outputs,layouts=layouts,fonts='only current Gossip28 + core/ui/font.cpp 6x8',
        fixture=dict(names=FIXTURE[:3],selected='filt',chain=1,patch='patch01 *',phase_ms=0,
                     parameters='illustrative scalars only; no engine/PCM/project loaded'),
        intentional_changes=[
            'Gossip28 right-hand names, native6x8 left-hand filters; same family as patch module names',
            'remove sidebar fill, full column divider, header/footer separator lines and extra page title',
            'sharp existing +add/more anchor with a fine branch into an expandable list',
            'lower-only 42% veil of the SAME frozen paper after existing blur recipe; quiet old text without a popup panel',
            'six 27px rows instead of seven 24px rows; catalogue and filter counts remain unbounded by geometry',
            'same pre-open patch geometry and paper phase remain behind picker; current runtime instead selects Add and snaps geometry BEFORE caching',
        ],
        validation=dict(native_layout_cases=cases,size_cases=size_cases,labels_targets_bounded_nonoverlapping=True,
                        glyph_labels_inside_targets=True,all_filter_focus_upper_exact=True,nearest2x_decode=True,
                        module_target=[170,27],filter_target=[88,27],unchanged_production_makefile=len(before)),
        behavior=dict(filter='one valid tap updates list; remember session filter, scroll and stable-id focus',
            add='one valid tap/lift or A commits then closes; no hold, preview, extra confirmation or auto-editor',
            cancel='drag/out-of-target/capture loss cancels; B closes; old patch controls never receive overlay taps',
            no_work_on_focus='no unit construction, sample I/O, thumbnail load, DSP or recomputation of backdrop',
            upper='same frozen chain for every filter and candidate, not a live inspector'),
        limits=[
            'Pillow-authored current geometry and formula-based paper; not C++/GPU/device pixel equivalence',
            'Python float projection and BOX downsample of finished images approximate current cache command coverage',
            'pre-open geometry preservation and extra lower paper veil are proposed, not current open_picker behavior',
            'static phase only; not an animation/timing/input/performance or LCD-readability test',
            'abbreviations vs full display aliases are inherited proposal data; no canonical names/UnitKind changes',
            'user entries fictional; registry/loader/DSP/plugin ABI not implemented',
            'long/duplicate/Unicode metadata policy still needs completion before runtime integration',
            'background/header underlying labels are intentionally separate from nonoverlapping foreground label checks',
            'no production font/assets/core/app changes, ARM, C++ capture, FTP, CIA, audio export or user data changes',
        ])
    (OUT/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for item in outputs:print(item['path'],item['size'],item['sha256'])
    print('pass:',cases,'native cases;',len(before),'production/assets/Makefile unchanged;',len(previous),'previous proposal files unchanged')


if __name__=='__main__':main()
