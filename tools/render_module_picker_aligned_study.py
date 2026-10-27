#!/usr/bin/env python3
"""Corrected width study: BOTH halves are 280px, aligned through the hinge.

Lower RGB is exactly v8. Upper uses those same x bounds +40, with native-size
text reflowed inside. Same active-tile fill and smoke. Design only, no runtime.
"""
from datetime import datetime, timezone
import json
import textwrap
from PIL import Image, ImageDraw, ImageChops, ImageStat, PngImagePlugin
import render_module_picker_narrow_study as v8
from render_module_picker_patch_study import Surface, FIXTURE, clipped_native
from render_module_quick_picker_study import BUILTINS, CUSTOM, Entry, inside
from render_bounce_visual_study import COLORS, ROOT, OUTSIDE, dual, sha, protected

v7,v6=v8.v7,v8.v6
OUT=ROOT/'ui_mockups/module_picker_study/aligned-width-v9'
INPUTS=['tools/render_module_picker_aligned_study.py',*v8.INPUTS]


def information(theme,candidate,chain,target,replace):
    p=Surface(Image.new('RGBA',(400,240)),theme)
    if candidate is None:
        p.txt(76,26,'catalogue / empty')
        name,size='no modules',28
        description='choose another filter below.'
    else:
        p.txt(76,26,'category / '+clipped_native(candidate.group,90))
        p.txt(326,26,'example only' if candidate.id.startswith('fiction.') else
              'user module' if candidate.user else 'built-in','muted','right')
        name,size=v7.fit_title(candidate.name,248)
        description=v7.INFO.get(candidate.id,'no description provided.')
    p.word(76,92,name,size,'accent' if candidate else 'muted')
    lines=textwrap.wrap(description,40,break_long_words=True,break_on_hyphens=False)
    assert len(lines)<=2, 'authored descriptions must fit without losing information'
    for i,line in enumerate(lines): p.txt(76,115+i*14,line,'ink')
    p.txt(76,164,'ch01 / replace '+str(target+1).zfill(2)+' '+chain[target]
          if replace else 'ch01 / add at chain end')
    nodes=list(chain)+([] if replace else ['+'])
    marked=target if replace else len(chain)
    pitch=min(64,212//max(1,len(nodes)-1))
    first=200-((len(nodes)-1)*pitch)//2
    centers=[first+i*pitch for i in range(len(nodes))]
    for i,(name,x) in enumerate(zip(nodes,centers)):
        role='accent' if i==marked else 'muted'
        p.txt(x,185,str(i+1).zfill(2),role,'center')
        p.txt(x,201,name,role,'center')
        if i==marked: p.line(x-18,215,x+18,215,'accent')
        if i+1<len(nodes): p.txt((x+centers[i+1])//2,201,'>','muted','center')
    assert not p.hits
    assert all(inside(box,(72,20,329,220)) for box,_ in p.labels)
    return p,dict(candidate_id=candidate.id if candidate else None,description=description,
        displayed_description=lines,chain=list(chain),node_labels=nodes,target_index=marked,
        target_kind='replace' if replace else 'append',labels=p.labels,targets=[])


def top_bounds(count,selected,t):
    left,_,right,_=v8.bounds(count,selected,t)
    return left+40,round(40-32*(1-(1-t)**3)),right+40,239


def upper(theme,t,count,selected,foreground):
    closed,_,field,old_mask=v7.upper_origin(theme,count,selected)
    if t==0: return Surface(closed,theme)
    result=Image.blend(closed,field,v6.smooth(t/.30))
    box=top_bounds(count,selected,t)
    ImageDraw.Draw(result).rectangle(box,fill=v7.active_fill(theme))
    has_active=count>0 and selected is not None
    fade=1-v6.smooth(t/.34)
    if has_active and fade>0:
        result=Image.composite(closed,result,old_mask.point(lambda a:round(a*fade)))
    result=Image.alpha_composite(result.convert('RGBA'),v7.edge_layer(theme,t,box,True,has_active))
    ink=foreground.im.copy(); left,y,right,_=box
    clip=Image.new('L',(400,240))
    ImageDraw.Draw(clip).rectangle((left+5,y+4,right-5,239),fill=255)
    ink.putalpha(ImageChops.multiply(ink.getchannel('A').point(lambda a:round(a*v6.smooth((t-.38)/.42))),clip))
    return Surface(Image.alpha_composite(result,ink).convert('RGB'),theme)


def scene(theme='graphite',progress=1.,group='all',candidate_id='builtin.Filter',entries=BUILTINS,
          count=3,selected=1,replace=False,module_start=None):
    if not count: selected=None
    old_top,bottom,state=v8.scene(theme,progress,group,candidate_id,entries,count,selected,replace,module_start)
    candidate=next((e for e in entries if e.id==state['lower']['selected_id']),None)
    foreground,info=information(theme,candidate,FIXTURE[:count],selected,replace)
    for key in ('candidate_id','description','chain','node_labels','target_index','target_kind'):
        assert info[key]==state['upper'][key]
    top=upper(theme,progress,count,selected,foreground)
    if progress==0: assert top.im.tobytes()==old_top.im.tobytes()
    return top,bottom,dict(lower=state['lower'],upper=info)


def composite(**kwargs):
    a,b,_=scene(**kwargs)
    return dual(a,b)


def save_png(image,name):
    path=OUT/name; assert not path.exists()
    big=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
    meta=PngImagePlugin.PngInfo(); meta.add_text('provenance',__doc__)
    big.save(path,pnginfo=meta)
    with Image.open(path) as check: assert check.convert('RGB').tobytes()==big.tobytes()
    return v6.item(path,big)


def save_motion():
    start=v8.scene()[2]['lower']['module_start']
    timeline=[(0.,'builtin.Filter','all',start,720)]
    timeline += [(i/10,'builtin.Filter','all',start,20) for i in range(1,10)]
    timeline += [(1.,'builtin.Filter','all',start,1000),(1.,'builtin.Delay','all',start,1000),
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
    path=OUT/'aligned-open-close-2x.gif'
    big[0].save(path,save_all=True,append_images=big[1:],duration=durations,loop=0,disposal=1,optimize=False,
                comment=b'corrected authored study: both 280px, shared aligned edges; not runtime/input capture')
    with Image.open(path) as decoded:
        assert decoded.n_frames==len(big)
        for i,im in enumerate(big):
            decoded.seek(i)
            assert decoded.convert('RGB').tobytes()==im.convert('RGB').tobytes()
            assert decoded.info['duration']==durations[i]
    assert frames[0].tobytes()==frames[-1].tobytes()
    errors=[ImageStat.Stat(ImageChops.difference(a,b.convert('RGB'))) for a,b in zip(frames,indexed)]
    return dict(v6.item(path,big[0]),source_frames=len(frames),gif_frames=len(big),duration_ms=sum(durations),
                decoded_quantized_exact=True,max_channel_error=max(max(b for a,b in e.extrema) for e in errors),
                worst_mean_channel_error=max(max(e.mean) for e in errors),limitation='global palette, lossy vs RGB')


def validate():
    cases=0; geometry=0
    for theme in COLORS:
        for entry in [*BUILTINS,*[e for e in CUSTOM if e.user]]:
            entries=CUSTOM if entry.user else BUILTINS
            top,bottom,state=scene(theme,candidate_id=entry.id,entries=entries)
            original=v8.scene(theme,candidate_id=entry.id,entries=entries)
            assert bottom.im.tobytes()==original[1].im.tobytes()
            assert state['lower']==original[2]['lower']
            assert top.im.getpixel((200,150))==v7.active_fill(theme)
            cases+=1
        scene(theme,group='user'); cases+=1
        e=next(e for e in BUILTINS if e.id=='builtin.Delay')
        for target in range(6): information(theme,e,('strain',)*6,target,True); cases+=1
    for n in range(7):
        for selected in ([None] if not n else [*range(n),*([None] if n<6 else [])]):
            for i in range(101):
                t=i/100; lower=v8.bounds(n,selected,t); top=top_bounds(n,selected,t)
                assert (top[0],top[2])==(lower[0]+40,lower[2]+40)
                assert top[2]-top[0]==lower[2]-lower[0]
                assert inside((top[0],top[1],top[2]+1,top[3]+1),(0,0,400,240))
                geometry+=1
            assert top_bounds(n,selected,1.)==(60,8,339,239)
            for t in (0.,.3,1.):
                scene(progress=t,count=n,selected=selected,replace=n==6); cases+=1
    return dict(layout_frame_cases=cases,aligned_geometry_samples=geometry,
                lower_rgb_and_layout_exact_to_v8=True,top_width=280,lower_width=280,
                top_x=[60,339],lower_x=[20,299],shared_edges_all_progress=True,
                fonts_sizes_preserved_for_builtins=True,description_wrap_columns=40,
                six_duplicate_nodes_fit=True,upper_passive=True,closed_rgb_exact=True)


def main():
    assert not OUT.exists(),'preserve prior artifacts'
    inputs={p:sha(ROOT/p) for p in INPUTS}
    before=protected(); before['Makefile']=sha(ROOT/'Makefile')
    ready={p:sha(ROOT/p) for p in v6.READY}
    previous={str(p.relative_to(ROOT)):sha(p) for p in OUT.parent.rglob('*') if p.is_file()}
    validation=validate()
    OUT.mkdir(parents=True)
    outputs=[save_png(composite(theme=theme),'aligned-picker-'+theme+'-2x.png') for theme in COLORS]
    outputs.append(save_motion())
    after=protected(); after['Makefile']=sha(ROOT/'Makefile')
    assert before==after and inputs=={p:sha(ROOT/p) for p in INPUTS}
    assert ready=={p:sha(ROOT/p) for p in v6.READY}
    assert all(sha(ROOT/p)==h for p,h in previous.items())
    report=dict(created_at=datetime.now(timezone.utc).isoformat(),origin=__doc__,status='design only, awaiting feedback',
        source_sha256=inputs,protected_sha256=before,ready_sha256=ready,previous_sha256=previous,
        validation=validation,outputs=outputs,
        correction='v8 wrongly left the upper wide; now both halves use the same 280px width and exact shared x edges',
        changed='upper text margins +12/-12, descriptions reflow to40 columns, six-node chain pitch fits246px; no font shrinking',
        unchanged='lower RGB/layout/touch targets from v8; active-tile fill, smoke, metadata, quick selection and modal ownership',
        limits=['Pillow authored fixture, not runtime/C++/GPU/device/input/performance proof',
            'GIF lossy: lower RGB exact does not imply decoded GIF exact to v8',
            'no app/fonts/assets/core/ARM/FTP/CIA/audio/userdata changes; previous variants untouched',
            'input/plugin ABI/Unicode proposals remain unimplemented'])
    (OUT/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for o in outputs: print(o['path'],o['size'],o['bytes'],o['sha256'])
    print('pass:',validation,';',len(before),'protected;',len(previous),'previous files unchanged')


if __name__=='__main__': main()
