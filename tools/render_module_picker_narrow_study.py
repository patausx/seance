#!/usr/bin/env python3
"""v8 width refinement, design only: lower frame 304 -> 280px, upper UNCHANGED.

No rescaled glyphs: existing Gossip28 and native6x8 runs move closer together.
Same active-tile fill, smoke outside, six rows, passive upper info and one-tap
proposal. Authored Pillow mockup, not application/input/device capture.
"""
from datetime import datetime, timezone
import json

from PIL import Image, ImageDraw, ImageChops, ImageStat, PngImagePlugin
import render_module_picker_dual_study as v7
from render_module_picker_patch_study import Surface, fit_gothic, clipped_native
from render_module_quick_picker_study import BUILTINS, CUSTOM, Entry, filters, inside
from render_bounce_visual_study import Canvas, COLORS, ROOT, OUTSIDE, dual, sha, protected

v6 = v7.v6
OUT = ROOT/'ui_mockups/module_picker_study/narrow-lower-v8'
INPUTS = ['tools/render_module_picker_narrow_study.py', *v7.INPUTS]
LEFT, RIGHT = 20, 299


def catalogue(theme='graphite', group='all', entries=BUILTINS, candidate_id='builtin.Filter',
              replace=False, module_start=None):
    old,state = v6.catalogue(theme,group,entries,candidate_id,replace,module_start)
    p = Surface(Image.new('RGBA',(320,240)),theme)
    p.txt(34,16,'ch01 / '+('replace' if replace else 'add'))
    p.txt(286,16,'selected slot' if replace else 'at chain end','muted','right')
    filter_rows, module_rows = [],[]
    for name,old_box in state['filter_rows']:
        y=old_box[1]; label=clipped_native(name,66); active=name==group
        p.txt(36,y+9,label,'accent' if active else 'muted')
        if active: p.line(36,y+20,36+len(label)*6-2,y+20,'accent')
        box=(26,y,108,y+27)
        assert inside(p.labels[-1][0],box)
        p.hit('filter:'+name,box); filter_rows.append((name,box))
    choices=filters(entries)
    if len(choices)>6:
        p.line(106,51,106,202)
        fy=53+round(state['filter_start']*146/max(1,len(choices)-6))
        p.line(105,fy,107,fy,'muted')
    if not state['count']:
        p.txt(120,82,'no modules')
        p.txt(120,100,'choose a filter')
    by_id={e.id:e for e in entries}
    for identity,old_box in state['module_rows']:
        y=old_box[1]; e=by_id[identity]; active=identity==state['selected_id']
        if active: p.diamond(110,y+13,'accent',True)
        label=p.word(120,y+20,fit_gothic(e.name,156),28,'accent' if active else 'ink')
        box=(108,y,276,y+27)
        assert inside(label,box)
        p.hit(identity,box); module_rows.append((identity,box))
    if state['count']>6:
        p.line(287,53,287,202)
        cy=55+round(state['module_start']*145/(state['count']-6))
        p.d.rectangle((286,cy-2,288,cy+2),fill=(*p.c['accent'],255))
        p.d.rectangle((285,cy-1,289,cy+1),fill=(*p.c['accent'],255))
        # Like v7, the 32px scroll capture extends 9px past the visible border.
        # It stays exclusive to this modal; the underlying patch is inert.
        p.hit('module scrollbar',(276,46,308,208))
    p.txt(18,228,'tap / a '+('replace' if replace else 'add') if state['count'] else 'choose filter')
    p.txt(188,228,str(state['count'])+' total','muted','right')
    p.txt(304,228,'b close','muted','right')
    p.hit('close',(246,216,312,240))
    assert p.im.crop((0,217,320,240)).tobytes()==old.im.crop((0,217,320,240)).tobytes()
    # Every word has exactly the same donor glyphs, font size and text as v7.
    assert [s for _,s in p.labels]==[s for _,s in old.labels]
    return p,dict(state,labels=p.labels,targets=p.hits,filter_rows=filter_rows,module_rows=module_rows)


def bounds(count,selected,t):
    a=v6.bounds(count,selected,0.)
    f=1-(1-t)**3
    width=round((a[2]-a[0]+1)+(280-(a[2]-a[0]+1))*f)
    center=(a[0]+a[2])/2+((LEFT+RIGHT)/2-(a[0]+a[2])/2)*f
    left=round(center-(width-1)/2)
    return left,round(a[1]*(1-f)),left+width-1,round(a[3]+(216-a[3])*f)


def lower(theme,t,count,selected,foreground):
    _,closed,_,frozen,field,old_mask,_=v6.origin(theme,count,selected)
    if t==0: return Surface(closed,theme)
    box=bounds(count,selected,t); left,y,right,_=box
    result=Image.blend(closed,frozen,v6.smooth(t/.8))
    ImageDraw.Draw(result).rectangle(box,fill=v7.active_fill(theme))
    result.paste(Image.blend(result.crop((0,217,320,240)),field.crop((0,217,320,240)),v6.smooth(t/.8)),(0,217))
    has_active=count>0 and selected is not None
    fade=1-v6.smooth(t/.42)
    if has_active and fade>0:
        result=Image.composite(closed,result,old_mask.point(lambda a:round(a*fade)))
    result=Image.alpha_composite(result.convert('RGBA'),v7.edge_layer(theme,t,box,False,has_active))
    ink=foreground.im.copy()
    ImageDraw.Draw(ink).rectangle((0,0,319,39),fill=(0,0,0,0))
    clip=Image.new('L',(320,240)); d=ImageDraw.Draw(clip)
    d.rectangle((left+5,y,right-5,216),fill=255)
    # Footer is outside the frame and keeps its full-size original positions.
    d.rectangle((0,217,319,239),fill=255)
    ink.putalpha(ImageChops.multiply(ink.getchannel('A').point(lambda a:round(a*v6.smooth((t-.38)/.42))),clip))
    header=Image.new('RGBA',(320,240))
    header.paste(foreground.im.crop((0,0,160,40)),(left-LEFT,0))
    header.paste(foreground.im.crop((160,0,320,40)),(160+right-RIGHT,0))
    header.putalpha(ImageChops.multiply(header.getchannel('A').point(lambda a:round(a*v6.smooth((t-.20)/.20))),clip))
    return Surface(Image.alpha_composite(Image.alpha_composite(result,header),ink).convert('RGB'),theme)


def scene(theme='graphite',progress=1.,group='all',candidate_id='builtin.Filter',entries=BUILTINS,
          count=3,selected=1,replace=False,module_start=None):
    if not count: selected=None
    # Return the original upper image directly, with the original info/layout.
    top,_,original=v7.scene(theme,progress,group,candidate_id,entries,count,selected,replace,module_start)
    foreground,state=catalogue(theme,group,entries,candidate_id,replace,module_start)
    for key in ('selected_id','group','count','module_start','filter_start','action'):
        assert state[key]==original['lower'][key]
    assert state['selected_id']==original['upper']['candidate_id']
    return top,lower(theme,progress,count,selected,foreground),dict(upper=original['upper'],lower=state)


def composite(*args,**kwargs):
    top,bottom,_=scene(*args,**kwargs)
    return dual(top,bottom)


def save_png(image,name):
    path=OUT/name; assert not path.exists()
    big=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
    info=PngImagePlugin.PngInfo(); info.add_text('provenance',__doc__)
    big.save(path,pnginfo=info)
    with Image.open(path) as check: assert check.convert('RGB').tobytes()==big.tobytes()
    return v6.item(path,big)


def comparison():
    c=Canvas(712,370,OUTSIDE)
    c.text(24,14,'lower window / a little narrower',COLORS['graphite']['ink'],scale=2)
    c.text(24,43,'same type size / same tile fill / upper screen unchanged',COLORS['graphite']['muted'])
    for i,(label,image) in enumerate((('before / 304px / 8px margins',v7.scene()[1].im),
                                      ('now / 280px / 20px margins',scene()[1].im))):
        c.text(24+i*344,76,label,COLORS['graphite']['accent'])
        c.im.paste(image,(24+i*344,96))
    c.text(24,350,'authored refinement / not runtime capture',COLORS['graphite']['muted'])
    return c.im


def save_motion():
    base=v7.scene()[2]['lower']['module_start']
    timeline=[(0.,'builtin.Filter','all',base,720)]
    timeline += [(i/10,'builtin.Filter','all',base,20) for i in range(1,10)]
    timeline += [(1.,'builtin.Filter','all',base,1000),(1.,'builtin.Delay','all',base,1000),
                 (1.,'builtin.Ghost','time',None,1200)]
    timeline += [(i/8,'builtin.Ghost','time',None,20 if i else 720) for i in range(7,-1,-1)]
    frames=[composite(progress=t,candidate_id=k,group=g,module_start=s) for t,k,g,s,_ in timeline]
    atlas=Image.new('RGB',(400,496*len(frames)))
    for i,im in enumerate(frames): atlas.paste(im,(0,i*496))
    palette=atlas.quantize(colors=248,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    colors=palette.getpalette()[:248*3]
    for color in (*COLORS['graphite'].values(),OUTSIDE,v7.active_fill('graphite'),(36,36,36),(0,0,0)): colors.extend(color)
    palette.putpalette(colors)
    indexed=[im.quantize(palette=palette,dither=Image.Dither.NONE) for im in frames]
    encoded,durations=[],[]
    for im,step in zip(indexed,timeline):
        if encoded and encoded[-1].tobytes()==im.tobytes(): durations[-1]+=step[-1]
        else: encoded.append(im); durations.append(step[-1])
    big=[im.resize((800,992),Image.Resampling.NEAREST) for im in encoded]
    path=OUT/'narrow-lower-open-close-2x.gif'
    big[0].save(path,save_all=True,append_images=big[1:],duration=durations,loop=0,disposal=1,optimize=False,
                comment=b'v8 authored refinement; lower 280px, upper unchanged; not runtime/input capture')
    with Image.open(path) as decoded:
        assert decoded.n_frames==len(big)
        for i,im in enumerate(big):
            decoded.seek(i)
            assert decoded.convert('RGB').tobytes()==im.convert('RGB').tobytes()
            assert decoded.info['duration']==durations[i]
    errors=[ImageStat.Stat(ImageChops.difference(a,b.convert('RGB'))) for a,b in zip(frames,indexed)]
    assert frames[0].tobytes()==frames[-1].tobytes()
    return dict(v6.item(path,big[0]),source_frames=len(frames),gif_frames=len(big),duration_ms=sum(durations),
                decoded_quantized_exact=True,max_channel_error=max(max(b for a,b in e.extrema) for e in errors),
                worst_mean_channel_error=max(max(e.mean) for e in errors),
                limitation='global palette/no dither; GIF lossy, do not claim decoded upper byte-exact to v7 GIF')


def validate():
    cases=0; layouts={}
    for theme in COLORS:
        for entry in BUILTINS:
            for group in ('all',entry.group):
                top,bottom,state=scene(theme,group=group,candidate_id=entry.id)
                old=v7.scene(theme,group=group,candidate_id=entry.id)
                assert top.im.tobytes()==old[0].im.tobytes()
                assert state['upper']==old[2]['upper']
                assert bottom.im.getpixel((150,35))==v7.active_fill(theme)
                assert entry.id in dict(state['lower']['module_rows'])
                cases+=1
        for group in filters(CUSTOM): scene(theme,group=group,entries=CUSTOM); cases+=1
        scene(theme,group='user'); cases+=1
        layouts[theme]=scene(theme)[2]
    for n in (0,1,6,7,28,31,150,300):
        entries=[Entry('probe.'+str(i),'module '+str(i),'module '+str(i),'group '+str(i%12),True) for i in range(n)]
        for at in sorted({0,n//2,max(0,n-1)}):
            scene(entries=entries,candidate_id=entries[at].id if n else None); cases+=1
    geometry=0
    for n in range(7):
        for selected in ([None] if not n else [*range(n),*([None] if n<6 else [])]):
            width=0
            for i in range(101):
                box=bounds(n,selected,i/100)
                assert inside((box[0],box[1],box[2]+1,box[3]+1),(0,0,320,240))
                assert box[2]-box[0]+1>=width
                width=box[2]-box[0]+1; geometry+=1
            assert bounds(n,selected,1.)==(20,0,299,216)
            for t in (0.,.3,1.):
                top,bottom,_=scene(progress=t,count=n,selected=selected,replace=n==6)
                old=v7.scene(progress=t,count=n,selected=selected,replace=n==6)
                assert top.im.tobytes()==old[0].im.tobytes()
                if t==0: assert bottom.im.tobytes()==old[1].im.tobytes()
                cases+=1
    return dict(layout_frame_cases=cases,geometry_samples=geometry,upper_rgb_exact_to_v7=True,
                width_before=304,width_after=280,margins_before=8,margins_after=20,
                same_glyph_strings_sizes=True,labels_targets_bounded_nonoverlapping=True,
                module_target=[168,27],filter_target=[82,27],closed_rgb_exact=True),layouts


def main():
    assert not OUT.exists(),'preserve existing variants'
    inputs={p:sha(ROOT/p) for p in INPUTS}
    before=protected(); before['Makefile']=sha(ROOT/'Makefile')
    ready={p:sha(ROOT/p) for p in v6.READY}
    previous={str(p.relative_to(ROOT)):sha(p) for p in OUT.parent.rglob('*') if p.is_file()}
    checks,layouts=validate()
    OUT.mkdir(parents=True)
    outputs=[save_png(composite(theme),'narrow-picker-'+theme+'-2x.png') for theme in COLORS]
    outputs.append(save_png(comparison(),'lower-width-comparison-2x.png'))
    outputs.append(save_motion())
    after=protected(); after['Makefile']=sha(ROOT/'Makefile')
    assert before==after and inputs=={p:sha(ROOT/p) for p in INPUTS}
    assert ready=={p:sha(ROOT/p) for p in v6.READY}
    assert all(sha(ROOT/p)==h for p,h in previous.items())
    report=dict(created_at=datetime.now(timezone.utc).isoformat(),status='design-only lower-width refinement',
        origin=__doc__,source_sha256=inputs,protected_sha256=before,ready_sha256=ready,previous_sha256=previous,
        outputs=outputs,validation=checks,layouts=layouts,
        intentional_changes=['only lower final frame becomes 280px centered, 20px side margins',
            'filters move 12px right, names/marker 8px left, scrollbar 14px left; glyphs never scale',
            'upper stays EXACT v7 RGB; lower edges now sit 12px inward relative to upper at the hinge',
            'footer remains outside frame at original positions; its reveal clip is not narrowed',
            'edge-position origins shift toward center while widening; no extra interaction or confirmation'],
        unchanged=['upper content/motion and stable candidate identity','active tile fill and smoke recipe',
            'six 27px rows, catalogue/filter scrolling, fonts and display strings','passive info/no audio preview',
            'quick filter plus one valid tap/lift; v7 input proposal and unresolved user ABI/Unicode policy'],
        limits=['authored Pillow, not runtime/input/device/FPS proof; frozen paper phase',
            'GIF quantized; exact upper comparisons refer to RGB, not two independently quantized GIFs',
            'no application/core/font/assets/build/ARM/FTP/CIA/audio/userdata changes'])
    (OUT/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for o in outputs: print(o['path'],o['size'],o['bytes'],o['sha256'])
    print('pass:',checks,';',len(before),'protected and',len(previous),'prior files unchanged')


if __name__=='__main__': main()
