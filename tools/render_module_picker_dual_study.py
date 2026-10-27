#!/usr/bin/env python3
"""v7 design-only: both halves of the active tile expand, upper shows metadata.

User's latest requirement: interior MUST retain the active tile's flat fill;
smoke stays outside, in the same frozen screen-space field. Lower foreground,
layout and one-tap input proposal are inherited from v6; no runtime changes.
Pillow authored fixture, not C++/3DS capture, live audio or an input simulation.
"""
from datetime import datetime, timezone
from functools import lru_cache
import json
import textwrap

from PIL import Image, ImageDraw, ImageChops, ImageStat, PngImagePlugin
import render_module_picker_expansion_study as v6
from render_module_picker_patch_study import Surface, paper, patch_pages, FIXTURE, clipped_native
from render_module_quick_picker_study import BUILTINS, CUSTOM, Entry, filters, inside
from render_bounce_visual_study import Canvas, COLORS, ROOT, OUTSIDE, glyph, dual, sha, protected

OUT = ROOT/'ui_mockups/module_picker_study/dual-expansion-v7'
INPUTS = ['tools/render_module_picker_dual_study.py', *v6.INPUTS,
          'core/engine/units.h', 'core/engine/units.cpp', 'core/engine/inst_unit.h']
# New PROPOSED prose, looked up by stable builtin ID, never by display name.
# Readonly source review: units.h/cpp and inst_unit.h. Not new runtime strings.
DESCRIPTIONS = {
    'Osc': 'generates pitched sine, triangle, saw and pulse waveforms.',
    'Noise': 'generates white, pink, crackle and metallic noise.',
    'Fm': 'creates tones with two-operator fm and feedback.',
    'Motor': 'generates jittered saw and pulse tones with resonant body.',
    'Inst': 'plays built-in multisampled instruments and one-shot drums.',
    'SamplePlayer': 'plays a sample with pitch, looping and playback-window controls.',
    'Grain': 'turns a loaded sample into a cloud of overlapping grains.',
    'MicIn': 'brings microphone input into the chain with gain and low-cut.',
    'Filter': 'shapes tone with resonant filtering, drive and filter morph.',
    'Vca': 'controls gain with an attack-hold-decay envelope and ducking.',
    'Delay': 'adds repeating echoes with feedback, tone and tempo sync.',
    'Recorder': 'records the incoming chain signal into a sample while passing audio through.',
    'Fold': 'folds the input waveform to add harmonic distortion.',
    'Bitcrush': 'reduces bit depth and holds samples for a lo-fi texture.',
    'Chorus': 'thickens input with two short, modulated delay voices.',
    'Stranular': 'turns live input into overlapping grains, with freeze and pitch control.',
    'Reverb': 'adds a diffuse reverb tail with damping and freeze.',
    'Ghost': 'builds drifting resonant tails from live input, with freeze.',
    'Ring': 'multiplies input by a carrier for metallic tones and sidebands.',
    'Tilt': 'balances low and high frequencies around a pivot frequency.',
    'Strain': 'adds multi-stage distortion with moving tone and feedback.',
    'Shift': 'shifts all input frequencies by a fixed offset, up or down.',
    'Bird': 'synthesizes short bird-like calls with pitch sweeps, trills and noise.',
    'Fract': 'captures short live-input loops for repeats, reverse and damage.',
    'Comp': 'reduces loud peaks with threshold, ratio, attack and release controls.',
    'Mouth': 'shapes input with vowel-like resonances from three formant filters.',
    'Arp': 'generates clocked note patterns; incoming audio passes through.',
    'Glitch': 'rearranges slices of the previous bar into rhythmic patterns.',
}
INFO = {'builtin.'+kind: text for kind, text in DESCRIPTIONS.items()}


@lru_cache(None)
def active_fill(theme):
    # Sample the actual authored selected tile, not a new near-matching tint.
    top, bottom, layout, _ = patch_pages(theme)
    x, _ = layout[1]
    color = bottom.im.getpixel((x+4, 1))
    assert color == top.im.getpixel((x+44, 41))
    return color


def fit_title(text, width=274):
    size = 40 if glyph(text, 40)[0].width <= width else 28
    if glyph(text, size)[0].width <= width:
        return text, size
    while text and glyph(text+'...', size)[0].width > width:
        text = text[:-1]
    assert text
    return text.rstrip()+'...', size


def paragraph(text):
    lines = textwrap.wrap(text, 44, break_long_words=True, break_on_hyphens=False)
    if len(lines) > 2:
        lines = lines[:2]
        lines[-1] = lines[-1][:41].rstrip()+'...'
    return lines


def information(theme, candidate, chain=FIXTURE[:3], target=1, replace=False):
    assert (replace and 0 <= target < len(chain)) or (not replace and len(chain) < 6)
    p = Surface(Image.new('RGBA', (400,240)), theme)
    if candidate is None:
        p.txt(64, 26, 'catalogue / empty')
        name, size = 'no modules', 28
        description = 'choose another filter below.'
    else:
        p.txt(64, 26, 'category / '+clipped_native(candidate.group, 90))
        p.txt(338, 26, 'example only' if candidate.id.startswith('fiction.') else
              'user module' if candidate.user else 'built-in', 'muted', 'right')
        name, size = fit_title(candidate.name)
        description = INFO.get(candidate.id, 'no description provided.')
    p.word(64, 92, name, size, 'accent' if candidate else 'muted')
    lines = paragraph(description)
    for row, line in enumerate(lines):
        p.txt(64, 115+row*14, line, 'ink')
    context = ('ch01 / replace '+str(target+1).zfill(2)+' '+chain[target]
               if replace else 'ch01 / add at chain end')
    p.txt(64, 164, context)
    nodes = list(chain)+([] if replace else ['+'])
    marked = target if replace else len(chain)
    pitch = min(64, 238//max(1,len(nodes)-1))
    first = 200-((len(nodes)-1)*pitch)//2
    centers = [first+i*pitch for i in range(len(nodes))]
    for i, (name, x) in enumerate(zip(nodes, centers)):
        role = 'accent' if i == marked else 'muted'
        p.txt(x, 185, str(i+1).zfill(2), role, 'center')
        p.txt(x, 201, name, role, 'center')
        if i == marked:
            p.line(x-18, 215, x+18, 215, 'accent')
        if i+1 < len(nodes):
            p.txt((x+centers[i+1])//2, 201, '>', 'muted', 'center')
    assert not p.hits, 'upper is passive, not a second editor'
    assert all(inside(box, (60,20,341,220)) for box,_ in p.labels), p.labels
    return p, dict(candidate_id=candidate.id if candidate else None,
                   description=description, displayed_description=lines,
                   chain=list(chain), target_index=marked, target_kind='replace' if replace else 'append',
                   node_labels=nodes, labels=p.labels, targets=p.hits)


def edge_layer(theme, progress, box, top, has_active):
    left, upper, right, lower = box
    edge = Image.new('RGBA', (400 if top else 320, 240))
    d = ImageDraw.Draw(edge)
    strength = (1-.4*v6.smooth(progress))*(1 if has_active else v6.smooth(progress/.45))
    for radius, alpha in reversed(list(enumerate((17,13,10,7,5,3,2,1),1))):
        d.rectangle((left-radius, upper-radius, right+radius, lower+radius),
                    outline=(*COLORS[theme]['accent'], round(alpha*strength)))
    color = (*COLORS[theme]['accent'], round(255*strength))
    points = ((left,lower,left,upper,right,upper,right,lower) if top else
              (left,upper,left,lower,right,lower,right,upper))
    d.line(points, fill=color)
    return edge


def lower(theme, t, count, selected, foreground):
    _, closed, _, frozen, field, old_mask, _ = v6.origin(theme,count,selected)
    if t == 0:
        return Surface(closed,theme)
    box = v6.bounds(count,selected,t)
    left,upper,right,bottom = box
    result = Image.blend(closed,frozen,v6.smooth(t/.8))
    # The ONE intentional lower-background change from v6: keep tile fill.
    ImageDraw.Draw(result).rectangle(box, fill=active_fill(theme))
    footer = Image.blend(result.crop((0,217,320,240)),field.crop((0,217,320,240)),v6.smooth(t/.8))
    result.paste(footer,(0,217))
    has_active = count > 0 and selected is not None
    old_opacity = 1-v6.smooth(t/.42)
    if has_active and old_opacity > 0:
        result = Image.composite(closed,result,old_mask.point(lambda a:round(a*old_opacity)))
    result = Image.alpha_composite(result.convert('RGBA'),edge_layer(theme,t,box,False,has_active))
    # Exact inherited native foreground, timing, header movement and clip.
    ink = foreground.im.copy()
    ImageDraw.Draw(ink).rectangle((0,0,319,39),fill=(0,0,0,0))
    alpha = ink.getchannel('A').point(lambda a:round(a*v6.smooth((t-.38)/.42)))
    clip = Image.new('L',(320,240))
    ImageDraw.Draw(clip).rectangle((left+5,upper,right-5,239),fill=255)
    ink.putalpha(ImageChops.multiply(alpha,clip))
    header = Image.new('RGBA',(320,240))
    header.paste(foreground.im.crop((0,0,160,40)),(left-8,0))
    header.paste(foreground.im.crop((160,0,320,40)),(160+right-311,0))
    header.putalpha(ImageChops.multiply(
        header.getchannel('A').point(lambda a:round(a*v6.smooth((t-.20)/.20))),clip))
    return Surface(Image.alpha_composite(Image.alpha_composite(result,header),ink).convert('RGB'),theme)


def top_bounds(count, selected, t):
    left,_,right,_ = v6.bounds(count,selected,t)
    # Exactly shared x edges / easing / rounding across the 40px LCD offset.
    y = round(40+(8-40)*(1-(1-t)**3))
    return left+40,y,right+40,239


@lru_cache(None)
def upper_origin(theme,count,selected):
    closed,_,layout,_ = patch_pages(theme,count,selected)
    field = paper(theme).crop((0,0,400,240))
    frozen = v6.origin(theme,count,selected)[2]
    old_mask = Image.new('L',(400,240))
    if count and selected is not None:
        x,w = layout[selected]
        box = (x+43,41,x+w+37,240)
        crop = closed.im.crop(box)
        delta = ImageChops.difference(crop,Image.new('RGB',crop.size,active_fill(theme)))
        old_mask.paste(delta.convert('L').point(lambda a:255 if a else 0),box[:2])
    return closed.im,frozen,field,old_mask


def upper(theme,t,count,selected,foreground):
    closed,frozen,field,old_mask = upper_origin(theme,count,selected)
    if t == 0:
        return Surface(closed,theme)
    # Return the whole surrounding surface to the SAME field, not just a
    # header-height strip: mixing raw paper above with washed blur below made
    # a visible horizontal seam at y40 outside the expanded frame. This also
    # removes the old heading before new metadata appears, without fragments.
    result = Image.blend(closed,field,v6.smooth(t/.30))
    box = top_bounds(count,selected,t)
    ImageDraw.Draw(result).rectangle(box,fill=active_fill(theme))
    has_active = count > 0 and selected is not None
    old_opacity = 1-v6.smooth(t/.34)
    if has_active and old_opacity > 0:
        result = Image.composite(closed,result,old_mask.point(lambda a:round(a*old_opacity)))
    result = Image.alpha_composite(result.convert('RGBA'),edge_layer(theme,t,box,True,has_active))
    ink = foreground.im.copy()
    left,y,right,_ = box
    clip = Image.new('L',(400,240))
    ImageDraw.Draw(clip).rectangle((left+5,y+4,right-5,239),fill=255)
    ink.putalpha(ImageChops.multiply(
        ink.getchannel('A').point(lambda a:round(a*v6.smooth((t-.38)/.42))),clip))
    return Surface(Image.alpha_composite(result,ink).convert('RGB'),theme)


def scene(theme='graphite',progress=1.,group='all',candidate_id='builtin.Filter',
          entries=BUILTINS,count=3,selected=1,replace=False,module_start=None):
    assert 0 <= progress <= 1 and 0 <= count <= 6
    if not count:
        selected = None
    assert (not replace and count<6) or (replace and selected is not None and 0<=selected<count)
    foreground,layout = v6.catalogue(theme,group,entries,candidate_id,replace,module_start)
    visible_id = layout['selected_id']
    candidate = next((e for e in entries if e.id==visible_id),None)
    info,metadata = information(theme,candidate,FIXTURE[:count],selected,replace)
    assert metadata['candidate_id'] == layout['selected_id']
    return (upper(theme,progress,count,selected,info),
            lower(theme,progress,count,selected,foreground),
            dict(lower=layout,upper=metadata))


def composite(*args,**kwargs):
    top,bottom,_ = scene(*args,**kwargs)
    return dual(top,bottom)


def board(title,subtitle,cells):
    c = Canvas(24+len(cells)*424,626,OUTSIDE)
    c.text(24,14,title,COLORS['graphite']['ink'],scale=2)
    c.text(24,43,subtitle,COLORS['graphite']['muted'])
    for i,(label,image) in enumerate(cells):
        c.text(24+i*424,76,label,COLORS['graphite']['accent'])
        c.im.paste(image,(24+i*424,96))
    c.text(24,610,'authored study / existing tile fill / not runtime capture',COLORS['graphite']['muted'])
    return c.im


def save_png(image,name):
    path = OUT/name
    assert not path.exists()
    big = image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo(); meta.add_text('provenance',__doc__)
    big.save(path,pnginfo=meta)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes()==big.tobytes()
    return v6.item(path,big)


def save_motion(start):
    timeline = [(0.,'builtin.Filter','all',start,720)]
    timeline += [(i/10,'builtin.Filter','all',start,20) for i in range(1,10)]
    timeline += [(1.,'builtin.Filter','all',start,1000),
                 (1.,'builtin.Delay','all',start,1000),
                 (1.,'builtin.Ghost','time',None,1200)]
    timeline += [(i/8,'builtin.Ghost','time',None,20 if i else 720) for i in range(7,-1,-1)]
    frames = [composite(progress=t,candidate_id=key,group=g,module_start=s) for t,key,g,s,_ in timeline]
    assert frames[0].tobytes()==frames[-1].tobytes()
    atlas = Image.new('RGB',(400,496*len(frames)))
    for i,im in enumerate(frames): atlas.paste(im,(0,i*496))
    palette = atlas.quantize(colors=248,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    colors = palette.getpalette()[:248*3]
    for color in (*COLORS['graphite'].values(),OUTSIDE,active_fill('graphite'),(36,36,36),(0,0,0)):
        colors.extend(color)
    palette.putpalette(colors)
    indexed = [im.quantize(palette=palette,dither=Image.Dither.NONE) for im in frames]
    encoded,durations = [],[]
    for im,step in zip(indexed,timeline):
        if encoded and encoded[-1].tobytes()==im.tobytes(): durations[-1]+=step[-1]
        else: encoded.append(im); durations.append(step[-1])
    big = [im.resize((800,992),Image.Resampling.NEAREST) for im in encoded]
    path = OUT/'dual-expansion-info-2x.gif'
    big[0].save(path,save_all=True,append_images=big[1:],duration=durations,loop=0,disposal=1,optimize=False,
                comment=b'authored v7: scripted focus/filter changes, passive metadata, exact tile fill; no device/input/audio capture')
    with Image.open(path) as decoded:
        assert decoded.n_frames==len(big)
        for i,im in enumerate(big):
            decoded.seek(i)
            assert decoded.convert('RGB').tobytes()==im.convert('RGB').tobytes()
            assert decoded.info['duration']==durations[i]
    errors = [ImageStat.Stat(ImageChops.difference(a,b.convert('RGB'))) for a,b in zip(frames,indexed)]
    # Flat fill is reserved in the global palette, even though smoke is lossy.
    for i,step in enumerate(timeline):
        if step[0] == 1:
            assert indexed[i].convert('RGB').getpixel((200,150))==active_fill('graphite')
    return dict(v6.item(path,big[0]),rgb_frames=len(frames),gif_frames=len(big),duration_ms=sum(durations),
                decoded_quantized_frames_exact=True,global_palette=True,dither=False,timeline=timeline,
                max_channel_error=max(max(b for a,b in e.extrema) for e in errors),
                worst_mean_channel_error=max(max(e.mean) for e in errors),
                gif_tile_fill_exact=True,limitation='GIF is lossy vs RGB; PNG endpoints lossless')


def validate():
    assert set(INFO)=={e.id for e in BUILTINS} and len(INFO)==28
    cases, comparisons, geometry = 0,0,0
    layouts={}
    for theme in COLORS:
        color = active_fill(theme)
        opened_top=scene(theme)[0].im
        # Outside the arch/8px halo, every top pixel is the original paper.
        outside_diff=ImageChops.difference(opened_top,paper(theme).crop((0,0,400,240)))
        ImageDraw.Draw(outside_diff).rectangle((40,0,359,239),fill=(0,0,0))
        assert outside_diff.getbbox() is None, 'outside paper seam/old header remains'
        for entry in BUILTINS:
            for group in ('all',entry.group):
                top,bottom,layout = scene(theme,group=group,candidate_id=entry.id)
                assert layout['upper']['candidate_id']==entry.id
                assert entry.id in dict(layout['lower']['module_rows'])
                assert top.im.getpixel((200,150))==bottom.im.getpixel((150,35))==color
                cases+=1
        for entry in (e for e in CUSTOM if e.user):
            scene(theme,group='user',entries=CUSTOM,candidate_id=entry.id); cases+=1
        _,_,layout = scene(theme,group='user')
        assert layout['upper']['candidate_id'] is None
        assert layout['upper']['description']=='choose another filter below.'
        cases+=1
        for n in range(7):
            selections = [None] if not n else [*range(n),*([None] if n<6 else [])]
            for selected in selections:
                replace = n==6
                for t in (0.,.2,.5,1.):
                    top,bottom,layout = scene(theme,t,count=n,selected=selected,replace=replace)
                    x,y,r,b = v6.bounds(n,selected,t)
                    assert top_bounds(n,selected,t)[::2]==(x+40,r+40)
                    if t==0:
                        oldtop,oldbottom,_,_=patch_pages(theme,n,selected)
                        assert top.im.tobytes()==oldtop.im.tobytes()
                        assert bottom.im.tobytes()==oldbottom.im.tobytes()
                    fg,_ = v6.catalogue(theme,replace=replace)
                    old = v6.scene(theme,t,n,selected,fg)[1].im
                    delta=ImageChops.difference(bottom.im,old)
                    # Lower layout/chrome/outside pixels all unchanged; only
                    # the moving rectangle's interior can differ from v6.
                    ImageDraw.Draw(delta).rectangle((x,y,r,b),fill=(0,0,0))
                    assert delta.getbbox() is None
                    comparisons+=1
                for i in range(101):
                    box = top_bounds(n,selected,i/100)
                    assert inside((box[0],box[1],box[2]+1,box[3]+1),(0,0,400,240))
                    geometry+=1
        # Passive diagram handles identity by POSITION even for repeated names.
        e=next(e for e in BUILTINS if e.id=='builtin.Delay')
        for at in range(6):
            _,data=information(theme,e,('strain',)*6,at,True)
            assert data['target_index']==at and data['node_labels']==['strain']*6
            cases+=1
        # Same-name user must never inherit a builtin's prose.
        fake=Entry('fiction.other-ghost','ghost','ghost','time',True)
        _,data=information(theme,fake)
        assert data['description']=='no description provided.'
        cases+=1
        layouts[theme]=scene(theme)[2]
    size_probes=[]
    for n in (0,1,6,7,28,31,150,300):
        entries=[Entry('probe.'+str(i),'module '+str(i),'module '+str(i),'group '+str(i%12),True) for i in range(n)]
        for at in sorted({0,n//2,max(0,n-1)}):
            e=entries[at] if entries else None
            top,bottom,data=scene(entries=entries,candidate_id=e.id if e else None)
            assert data['upper']['candidate_id']==(e.id if e else None)
            size_probes.append([n,at]); cases+=1
    # Focus changes metadata/marker only, never texture/expansion or chain state.
    base=scene()[2]['lower']['module_start']
    a=scene(candidate_id='builtin.Filter',module_start=base)
    b=scene(candidate_id='builtin.Delay',module_start=base)
    assert a[2]['upper']['chain']==b[2]['upper']['chain']
    assert a[2]['lower']['module_rows']==b[2]['lower']['module_rows']
    for index,rect in ((0,(48,8,352,240)),(1,(8,0,312,217))):
        delta=ImageChops.difference(a[index].im,b[index].im)
        ImageDraw.Draw(delta).rectangle(rect,fill=(0,0,0))
        assert delta.getbbox() is None
    return dict(layout_cases=cases,closed_and_lower_delta_cases=comparisons,seam_geometry_samples=geometry,
                catalog_sizes=size_probes,upper_id_matches_lower=True,passive_upper=True,
                exact_active_fill={theme:active_fill(theme) for theme in COLORS},
                lower_changes_restricted_to_moving_fill=True,duplicate_slot_identity=True,
                unknown_metadata_fallback=True,closed_rgb_exact_to_v6=True),layouts,base


def main():
    assert not OUT.exists(),'preserve earlier studies; choose a fresh revision'
    inputs={p:sha(ROOT/p) for p in INPUTS}
    before=protected(); before['Makefile']=sha(ROOT/'Makefile')
    ready={p:sha(ROOT/p) for p in v6.READY}
    previous={str(p.relative_to(ROOT)):sha(p) for p in OUT.parent.rglob('*') if p.is_file()}
    validation,layouts,start=validate()
    OUT.mkdir(parents=True)
    outputs=[]
    for theme in COLORS:
        outputs.append(save_png(composite(theme),'dual-picker-'+theme+'-2x.png'))
    outputs.append(save_png(board('both screens / one active window','same selected-tile fill inside / smoke remains outside',[
        ('01 / before opening',composite(progress=0.)),
        ('02 / both halves expand',composite(progress=.3)),
        ('03 / catalogue + information',composite())]),'dual-expansion-stages-2x.png'))
    outputs.append(save_png(board('upper follows the highlighted module','description only / no audio preview or extra confirmation',[
        ('all / filter',composite(module_start=start)),
        ('same list / delay',composite(candidate_id='builtin.Delay',module_start=start)),
        ('time / ghost',composite(group='time',candidate_id='builtin.Ghost'))]),'upper-info-states-2x.png'))
    outputs.append(save_motion(start))
    assert inputs=={p:sha(ROOT/p) for p in INPUTS}
    after=protected(); after['Makefile']=sha(ROOT/'Makefile')
    assert before==after and ready=={p:sha(ROOT/p) for p in v6.READY}
    assert all(sha(ROOT/p)==h for p,h in previous.items())
    report=dict(created_at=datetime.now(timezone.utc).isoformat(),origin=__doc__,
        status='new design-only variant; no app/ARM/deployment',source_sha256=inputs,
        protected_sha256=before,ready_sha256=ready,previous_sha256=previous,outputs=outputs,
        validation=validation,layouts=layouts,description_proposals=INFO,
        user_update='rectangle interior must be exactly the active tile background on both screens; smoke outside only',
        visual='one upper arch and lower U, shared x/40 offset; no stretching of glyphs or paper',
        behavior=['upper metadata is driven by actual visible lower selected_id; empty filter clears it',
            'focus/scroll/filter update passive prose, never audio/editor/preview/DSP/sample I/O',
            'one valid tap/lift commits as before; looking at the upper card is never required',
            'A in filter-column applies filter only; A in module-column commits',
            'opener/held repeats consumed; fresh touch during opening snaps BEFORE final hit-test, later valid lift may commit',
            'fresh A during opening snaps only; next fresh A commits. B cancels pending and reverses from current geometry',
            'chain diagram is existing order: append + at end, replace highlights old physical position/name; no pre-commit mutation'],
        limits=['authored Pillow fixture and scripted timing, not runtime/GPU/device/input/FPS proof',
            'original tile fill exact to authored patch fixture; Pillow paper/blur not guaranteed GPU pixel exact',
            'smoke phase is frozen, not newly animated; only geometry and metadata change',
            'upper expansion/info newly proposed; v6 stays intact as history',
            'aliases/navigation categories inherited; category time does not imply every entry is an audio effect',
            'proposed descriptions, no new runtime strings; unknown user IDs never get builtin descriptions by name',
            'user entries fictional; plugin ABI/Unicode/duplicate disambiguation not finalized',
            'no runtime font/assets/core changes, ARM/C++ capture/FTP/CIA/audio export/userdata access'])
    (OUT/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for o in outputs: print(o['path'],o['size'],o['bytes'],o['sha256'])
    print('pass:',validation,';',len(before),'protected,',len(previous),'previous files unchanged')


if __name__=='__main__': main()
