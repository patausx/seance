#!/usr/bin/env python3
"""Static design proposal: the OPEN editor remains a patch-table module sheet.

Not an entry animation. Main stays unchanged in this proposal. Lower filter
plot/icons/labels retain archived native geometry; the upper inspector is
recomposed inside a persistent aligned card. Pillow, not runtime/hardware.
"""
from datetime import datetime, timezone
from functools import lru_cache
import json
import math
from PIL import Image, ImageDraw, ImageChops, PngImagePlugin
import render_module_editor_link_study as base
from render_filter_interaction_study import response, nominal

ROOT, COLORS, OUTSIDE = base.ROOT, base.COLORS, base.OUTSIDE
Surface, Canvas = base.Surface, base.Canvas
OUT = ROOT/'ui_mockups/module_editor_link_study/editor-sheet-v2'
ARCHIVE = base.ARCHIVE
MODES = ('low','high','band','notch','off')
CHAIN = ('play','filt','ghost')
UPPER_BOX, LOWER_BOX = (44,40,355,239), (4,0,315,225)
PLOT = (76,145,256,62)
PARAMS = base.PARAMS
INPUTS = list(dict.fromkeys(['tools/render_module_editor_sheet_study.py', *base.INPUTS,
    'tools/filter_interaction_ui_cases.inc', 'platform/3ds/ui_editor.cpp']))
READY = base.READY
TARGETS = {
    'back / new proposed release action': (8,0,92,27),
    'bypass': (242,0,280,24),
    **{mode:(7+61*i,28,65+61*i,59) for i,mode in enumerate(MODES)},
    'cutoff': (8,61,274,180), 'resonance': (278,61,320,180),
    'drive': (10,182,147,225), 'morph': (168,182,305,225),
}


def inventory(folder):
    return {str(p.relative_to(ROOT)):base.sha(p) for p in sorted(folder.rglob('*')) if p.is_file()}


@lru_cache(None)
def donor(theme, mode, screen):
    path = ARCHIVE/(theme+'_'+mode+'_'+screen+'.ppm')
    receipt = json.loads((ARCHIVE/'capture.json').read_text())
    assert base.sha(path) == receipt['surface_sha256'][path.name]
    with Image.open(path) as image: result = image.convert('RGB')
    assert result.size == (400 if screen=='top' else 320,240)
    return result


def recolor_paper(image, theme):
    result = image.copy()
    source,target = image.load(),result.load()
    for y in range(image.height):
        bg = source[0,y]
        for x in range(image.width):
            if source[x,y] == bg: target[x,y] = base.active_fill(theme)
    return result


def sheet(theme, upper):
    field = base.paper(theme).crop((0,0,400,240) if upper else (40,256,360,496))
    x,y,right,bottom = UPPER_BOX if upper else LOWER_BOX
    halo = Image.new('RGBA',field.size)
    d = ImageDraw.Draw(halo)
    for radius,alpha in reversed(list(enumerate((17,13,10,7,5,3,2,1),1))):
        d.rectangle((x-radius,y-radius,right+radius,bottom+radius),outline=(*COLORS[theme]['accent'],alpha))
    result = base.composite(field,halo)
    d = ImageDraw.Draw(result)
    d.rectangle((x,y,right,bottom),fill=base.active_fill(theme))
    points = [(x,bottom),(x,y),(right,y),(right,bottom)] if upper else [(x,y),(x,bottom),(right,bottom),(right,y)]
    d.line(points,fill=COLORS[theme]['accent'])
    return Surface(result,theme)


def breadcrumb(p, chain, active, right=343, y=55):
    """Passive order context, selected by position, not by repeated display name."""
    assert 1 <= len(chain) <= 6 and 0 <= active < len(chain)
    left=max(0,active-1); end=min(len(chain),active+2)
    def entries():
        tokens = ([('..',False)] if left else [])
        tokens += [(name,i==active) for i,name in enumerate(chain[left:end],left)]
        tokens += [('..',False)] if end<len(chain) else []
        return tokens
    while len(' > '.join(t for t,_ in entries()))*6 > 174:
        if end-1 > active: end-=1
        elif left < active: left+=1
        else: raise AssertionError('selected label needs its own truncation policy')
    tokens=entries(); width=len(' > '.join(t for t,_ in tokens))*6
    x=right-width
    for i,(text,selected) in enumerate(tokens):
        p.txt(x,y,text,'accent' if selected else 'muted'); x+=len(text)*6
        if i+1<len(tokens): p.txt(x,y,' > ','muted'); x+=18
    assert x==right
    return dict(chain=list(chain),active_order=active,visible_start=left,visible_end=end,
                displayed=' > '.join(t for t,_ in tokens),passive=True)


def response_plot(p, mode):
    x,y,w,h=PLOT
    db=[response(PARAMS['raw'],PARAMS['res'],PARAMS['morph'],mode,20*800**(i/116)) for i in range(117)]
    def py(value): return y+int((12-max(-36.,min(12.,value)))*h/48+.5)
    points=[(x+int(i*w/116+.5),py(value)) for i,value in enumerate(db)]
    if mode!='off':
        shade=base.mix(base.active_fill(p.theme),p.c['accent'],16,255)
        p.d.polygon([*points,(x+w,y+h),(x,y+h)],fill=shade)
    zero=y+(h+2)//4
    p.line(x,zero,x+w,zero); p.line(x,y+h,x+w,y+h)
    for hz,label in ((20,'20'),(100,'100'),(1000,'1k'),(16000,'16k')):
        xx=x+int(math.log(hz/20)/math.log(800)*w+.5)
        for yy in range(y,y+h,7): p.d.point((xx,yy),fill=p.c['rule'])
        p.txt(xx,y+h+9,label,'muted','right' if hz==16000 else 'center')
    p.txt(x-6,y-3,'+12','muted','right')
    p.txt(x-6,zero-3,'0','muted','right')
    p.txt(x-6,y+h-5,'-36','muted','right')
    p.d.line(points,fill=p.c['muted' if mode=='off' else 'accent'])
    if mode!='off':
        at=x+int(math.log(nominal(PARAMS['raw'])/20)/math.log(800)*w+.5)
        for yy in range(y-3,y+h+1,5): p.line(at,yy,at,min(yy+1,y+h),'accent')
        p.diamond(at,y-7,'accent')
    if min(db)<-36: p.txt(x+w-8,y+h-16,'v')
    if max(db)>12: p.txt(x+w-8,y+1,'^')


def upper(theme,mode='low',chain=CHAIN,active=1):
    p=sheet(theme,True)
    p.heading(40,8,'patch table')
    p.txt(355,17,'ch01 / '+str(active+1).zfill(2),'muted','right')
    p.heading(58,47,'filter','accent')
    context=breadcrumb(p,chain,active)
    p.txt(58,84,'nominal cutoff')
    box=p.word(58,129,'800',40,'ink')
    p.txt(box[2]+7,119,'hz','muted')
    label=mode+' pass' if mode in ('low','high','band') else mode
    p.word(343,103,label,22,'accent','right')
    p.txt(343,113,'res 32% / drive 18%','muted','right')
    p.txt(343,129,'off / exact dry' if mode=='off' else 'morph 0%','muted','right')
    response_plot(p,mode)
    p.txt(58,232,'off / dry; drive + morph inactive' if mode=='off' else 'post-drive response / not spectrum')
    return p,context


def needle(p, raw, x,y,width,vertical=False):
    pos=(raw*width+16383)//32767
    if vertical:
        p.line(x,y-width,x,y)
        at=y-pos
        p.line(x-4,at,x+4,at,'accent')
        p.d.rectangle((x-1,at-1,x+1,at+1),fill=p.c['accent'])
    else:
        p.line(x,y,x+width,y)
        at=x+pos
        p.line(at,y-4,at,y+4,'accent')
        p.d.rectangle((at-1,y-1,at+1,y+1),fill=p.c['accent'])


def lower(theme,mode='low'):
    p=sheet(theme,False)
    original=recolor_paper(donor(theme,mode,'bottom'),theme)
    # Native plot, all five icons, parameter labels: translation=0, scale=1.
    p.im.paste(original.crop((7,28,315,225)),(7,28))
    p.d=ImageDraw.Draw(p.im)
    p.word(16,21,'back',22,'accent')
    p.line(12,23,48,23,'accent'); p.line(12,23,15,21,'accent'); p.line(12,23,15,25,'accent')
    p.txt(65,10,'b','muted')
    p.txt(242,10,'bypass','muted')
    p.txt(284,10,'x del','muted')
    for box in ((291,90,300,162),(15,206,141,217),(173,206,299,217)):
        p.d.rectangle((box[0],box[1],box[2]-1,box[3]-1),fill=base.active_fill(theme))
    needle(p,PARAMS['res'],295,157,63,True)
    needle(p,PARAMS['drive'],18,211,119)
    needle(p,PARAMS['morph'],176,211,119)
    p.txt(16,231,'patch01 *')
    p.txt(310,231,'l: mod >','muted','right')
    # Check labels/targets we authored; original labels are separately preserved
    # by crop equality, rather than pretending they entered this Python registry.
    for name,box in TARGETS.items(): p.hit(name,box)
    preserved=((7,28,315,60),(7,61,274,181),(278,61,315,86),(10,182,306,202))
    for box in preserved: assert p.im.crop(box).tobytes()==original.crop(box).tobytes(),(theme,mode,box)
    return p,dict(native_preserved_regions=preserved,targets=TARGETS,raw_parameters=PARAMS,
                  plot_same_size=True,geometry_scale=1,new_back_target=True)


def proposed(theme='graphite',mode='low',chain=CHAIN,active=1):
    a,context=upper(theme,mode,chain,active); b,geometry=lower(theme,mode)
    return (a.im,b.im),dict(upper_labels=a.labels,lower_authored_labels=b.labels,
                           upper_context=context,lower=geometry)


def current_editor(theme):
    # Native archived editor for comparison, normalized only to fixture slot02.
    top=donor(theme,'low','top').copy()
    p=Surface(top,theme)
    p.d.rectangle((322,14,383,25),fill=top.getpixel((0,17)))
    p.txt(382,17,'ch01 / 02','muted','right')
    return p.im,donor(theme,'low','bottom')


def pair(pages): return base.pair(pages)


def board(title,subtitle,cells):
    c=Canvas(24+424*len(cells),626,OUTSIDE)
    c.text(24,14,title,COLORS['graphite']['ink'],scale=2)
    c.text(24,43,subtitle,COLORS['graphite']['muted'])
    for i,(label,image) in enumerate(cells):
        x=24+424*i
        c.text(x,76,label,COLORS['graphite']['accent']); c.im.paste(image,(x,96))
    c.text(24,610,'static authored proposal / not runtime or device capture',COLORS['graphite']['muted'])
    return c.im


def save(image,name):
    path=OUT/name; assert not path.exists()
    big=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
    metadata=PngImagePlugin.PngInfo(); metadata.add_text('provenance',__doc__)
    big.save(path,pnginfo=metadata)
    with Image.open(path) as check: assert check.convert('RGB').tobytes()==big.tobytes()
    return dict(path=str(path.relative_to(ROOT)),size=list(big.size),bytes=path.stat().st_size,sha256=base.sha(path))


def validate():
    cases=0
    assert (UPPER_BOX[0],UPPER_BOX[2])==(LOWER_BOX[0]+40,LOWER_BOX[2]+40)
    assert UPPER_BOX[2]-UPPER_BOX[0]+1==LOWER_BOX[2]-LOWER_BOX[0]+1==312
    for theme in COLORS:
        for mode in MODES:
            pages,data=proposed(theme,mode)
            assert [p.size for p in pages]==[(400,240),(320,240)]
            assert data['lower']['targets']==TARGETS; cases+=1
        for count in range(1,7):
            for active in range(count):
                p,context=upper(theme,chain=('strain',)*count,active=active)
                assert context['active_order']==active
                assert context['visible_start']<=active<context['visible_end']; cases+=1
        # The exterior is genuinely the same paper, not a uniformly tinted modal.
        for top in (True,False):
            p=sheet(theme,top)
            field=base.paper(theme).crop((0,0,400,240) if top else (40,256,360,496))
            for point in ((0,100),(399,100)) if top else ((0,237),(319,237)):
                assert p.im.getpixel(point)==field.getpixel(point)
    return dict(authored_layout_cases=cases,native_themes=list(COLORS),mode_names=list(MODES),
                frame_widths=[312,312],aligned_edges=True,upper_plot_proposal=list(PLOT),
                current_upper_plot=[32,110,348,75],lower_plot_unchanged=[32,94,228,63],
                lower_donor_regions_exact_after_background_recolor=True,
                lower_core_targets_unchanged=True,new_action='back84x27 target; release-only proposal',
                fixture_only='no input FSM/drag/lock/lens/performance/device proof')


def main():
    assert not OUT.exists(),'keep prior studies; choose a new output directory'
    sources={p:base.sha(ROOT/p) for p in INPUTS}
    protected=base.protected(); protected['Makefile']=base.sha(ROOT/'Makefile')
    ready={p:base.sha(ROOT/p) for p in READY}
    previous=inventory(OUT.parent); archived=inventory(ARCHIVE)
    validation=validate()
    OUT.mkdir(parents=True)
    outputs=[]; layouts={}
    for theme in COLORS:
        pages,layouts[theme]=proposed(theme)
        outputs.append(save(pair(pages),'editor-sheet-'+theme+'-2x.png'))
        outputs.append(save(board('main / open module','same card language after opening / no animation needed',[
            ('main / current-style fixture',pair(base.card(theme,False))),
            ('open filter / proposed sheet',pair(pages))]),'main-and-editor-'+theme+'-2x.png'))
    outputs.append(save(board('editor / before and after','native lower working surface / shared patch-table frame',[
        ('current layout / archived editor',pair(current_editor('graphite'))),
        ('proposal / persistent module sheet',pair(proposed()[0]))]),'editor-before-after-graphite-2x.png'))
    assert sources=={p:base.sha(ROOT/p) for p in INPUTS}
    assert protected==dict(base.protected(),Makefile=base.sha(ROOT/'Makefile'))
    assert all(base.sha(ROOT/p)==h for group in (ready,previous,archived) for p,h in group.items())
    report=dict(created_at=datetime.now(timezone.utc).isoformat(),origin=__doc__,
        status='design only / awaiting user feedback / no runtime or deployment approval',
        source_sha256=sources,protected_sha256=protected,ready_sha256=ready,
        previous_sha256=previous,archive_sha256=archived,validation=validation,
        outputs=outputs,layouts=layouts,
        changed=['persistent aligned312px card with same flat active fill and exterior smoke',
            'patch table heading and passive chain context remain visible in open editor',
            'upper filter inspector intentionally recomposed; graph348x75 becomes256x62, native fonts unchanged',
            'back entry and bypass text in main-like command strip; X delete remains keyboard hint',
            'three existing amount rails use main-like needles at unchanged numeric positions',
            'patch identity in footer; existing L mod keyboard hint retained'],
        preserved=['all lower modes, cutoff graph, parameter labels and numeric values at native source positions',
            'all filter core touch targets including resonance to x319; outline is decorative, not a hit clip',
            'fixed raw5142/res10485/drive5898/morph0; no PCM, engine, project or sample loaded',
            'current main design: no miniature plots, no edit affordance change, no neighbour sidebars in editor',
            'only current Gossip22/28/40 plus core/ui/font.cpp6x8; no new font assets'],
        implementation_notes=[
            'shared sheet/chrome/needle helpers could be reused but DO NOT exist as runtime changes',
            'individual modules own drawing: upper inspector/header must opt into/reflow around the common frame',
            'back header action is NEW and must delegate to existing editor_back before closing; preserve B behavior',
            'no tap handlers exist in this study; no changes to A trigger, X delete confirmation, L mod or parameter laws',
            'physical slot/identity must own future context; duplicate breadcrumb probe only tests authored positioning'],
        limits=['static Pillow proposal; no animation, C++ compilation/capture, hardware, FPS or input tests',
            'archived C++ lower ink reused and paper recolored; main and upper page authored, not device screenshots',
            'five ordinary filter modes only; bypass/mute, p-lock, MOD lens, other themes/modules need design integration',
            'back shell is not a universal drop-in; cannot claim all28 editors solved or pixel-exact current UI',
            'upper response is a Python linearized post-drive display model, not audio/spectrum/ARM exact',
            'v1 transition study and all prior files preserved; no app/core/font/assets/ARM/FTP/CIA/userdata changes'])
    (OUT/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for output in outputs: print(output['path'],output['size'],output['sha256'])
    print('pass:',validation['authored_layout_cases'],'authored cases;',len(protected),'protected;',len(previous),'prior study files')


if __name__=='__main__': main()
