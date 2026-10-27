#!/usr/bin/env python3
"""Filter interaction proposal: authored Pillow, NOT runtime/device capture.

Native Gossip 22/28/40 + 6x8, current static paper. Curves evaluate the existing
linearized post-drive SVF model; the animated parameter script is illustrative,
not engine telemetry, input handling, an audio waveform or a spectrum analyser.
"""
from datetime import datetime, timezone
import cmath
import json
import math
from pathlib import Path

from PIL import Image, PngImagePlugin
from render_bounce_visual_study import Page, Canvas, COLORS, OUTSIDE, ROOT, sha, protected, dual

OUT = ROOT / 'ui_mockups/filter_interaction_study'
SR, FMAX = 32000, 32767 * 85 // 100
MIN_HZ, MAX_HZ = 20., 16000.
INPUTS = ['tools/render_filter_interaction_study.py', 'tools/render_bounce_visual_study.py',
          'tools/render_module_geometry_study.py', 'tools/render_menu_style_study.py',
          'tools/generate_start_menu_labels.py', 'assets/fonts/Gossip-MedPixel.otf',
          'core/ui/font.cpp', 'core/dsp/filter.h', 'platform/3ds/ui_palette.h',
          'platform/3ds/filter_response_model.h', 'platform/3ds/ui_module_focus_filter.cpp',
          'platform/3ds/module_inspector.cpp', 'docs/filter-clear.md']
MODES = ('low', 'high', 'band', 'notch', 'off')


def raw_hz(hz):
    return max(1, min(FMAX, round(65536 * math.sin(math.pi * hz / SR))))


def nominal(raw):
    return SR / math.pi * math.asin(max(1, min(FMAX, raw)) / 65536)


def response(raw, res, morph, mode, hz):
    if mode == 'off':
        return 0.
    f = max(1, min(FMAX, raw)) / 32768
    d = max(1024, 32767 - res) / 32768
    z = cmath.exp(-2j * math.pi * hz / SR)
    a = 1 - z
    denominator = a*a + f*d*z*a + f*f*z
    numerator = {'low': f*f, 'high': a*a, 'band': f*a, 'notch': denominator - f*a}[mode]
    m = morph / 32768
    h = m + (1-2*m)*numerator/denominator
    return 20 * math.log10(max(1.e-6, abs(h)))


def at(hz, x, w):
    return x + round(math.log(max(MIN_HZ, min(MAX_HZ, hz)) / MIN_HZ) / math.log(MAX_HZ/MIN_HZ) * w)


def config(hz=800, res=.32, morph=0., selected='cutoff', mode='low', live_hz=None, bypass=False, muted=False):
    return dict(raw=raw_hz(hz), res=round(res*32767), morph=round(morph*32767),
                selected=selected, mode=mode, live_raw=raw_hz(live_hz) if live_hz else raw_hz(hz),
                mod=live_hz is not None, bypass=bypass, muted=muted)


def plot(p, s, box, large=False):
    x,y,w,h = box
    def py(db): return y + round((12-max(-36., min(12., db))) * h / 48)
    mode = 'off' if s['bypass'] else s['mode']
    zero = py(0)
    tint = (44,42,48) if p.theme == 'graphite' else (217,182,201)
    line = 'muted' if s['muted'] or mode == 'off' else 'accent'
    vertices = []
    edit = []
    clipped_high = clipped_low = False
    for i in range(w+1):
        hz = MIN_HZ * (MAX_HZ/MIN_HZ)**(i/w)
        db = response(s['live_raw'],s['res'],s['morph'],mode,hz)
        clipped_high |= db>12
        clipped_low |= db<-36
        yy = py(db)
        vertices.append((x+i,yy))
        edit.append((x+i,py(response(s['raw'],s['res'],s['morph'],mode,hz))))
        if large and not s['muted'] and mode != 'off':
            p.d.line((x+i,yy,x+i,y+h),fill=tint)
    p.line(x,zero,x+w,zero)
    p.line(x,y+h,x+w,y+h)
    for hz,label in ((20,'20'),(100,'100'),(1000,'1k'),(16000,'16k')):
        xx=at(hz,x,w)
        for yy in range(y,y+h,7): p.d.point((xx,yy),fill=p.c['rule'])
        p.txt(xx,y+h+9,label,'muted','right' if hz==16000 else 'center')
    p.txt(x-6,y-3,'+12','muted','right')
    p.txt(x-6,zero-3,'0','muted','right')
    p.txt(x-6,y+h-5,'-36','muted','right')
    if s['mod']:
        for i in range(1,w+1):
            if i%7<4: p.d.line((edit[i-1],edit[i]),fill=p.c['muted'])
    p.d.line(vertices,fill=p.c[line],width=1)
    # Nominal cutoff is not the resonant peak or a measured -3dB point.
    if mode!='off':
        xx=at(nominal(s['raw']),x,w)
        for yy in range(y-3,y+h+1,5): p.line(xx,yy,xx,min(y+h,yy+1),'muted' if s['mod'] else 'accent')
        p.diamond(xx,y-7,'accent',False)
        if s['mod']:
            live_x=at(nominal(s['live_raw']),x,w)
            p.diamond(live_x,y-7,line,True)
    if clipped_high: p.txt(x+w-8,y+1,'^')
    if clipped_low: p.txt(x+w-8,y+h-16,'v')


def upper(theme,s):
    p=Page(400,theme)
    p.word(18,33,'filter',28)
    p.word(116,32,s['mode']+' pass' if s['mode'] in ('low','high','band') else s['mode'],22,'accent')
    p.txt(382,17,'ch01 / 02','muted','right')
    selected=s['selected']
    p.txt(20,46,selected,'muted')
    value=str(round(nominal(s['raw']))) if selected=='cutoff' else str(round(s['res' if selected=='resonance' else 'morph']*100/32767))
    box=p.word(20,92,value,40,'ink')
    p.txt(box[2]+7,82,'hz' if selected=='cutoff' else '%','muted')
    p.txt(382,48,'nominal frequency','muted','right')
    p.txt(382,64,'res %d%% / drive 18%%' % round(s['res']*100/32767),'muted','right')
    state='bypassed / dry' if s['bypass'] else 'muted / model only' if s['muted'] else 'sg.a / illustrative live' if s['mod'] else 'morph %d%%' % round(s['morph']*100/32767)
    p.txt(382,80,state,'accent' if s['mod'] else 'muted','right')
    plot(p,s,(32,110,348,75),True)
    p.txt(20,214,'edit %dhz' % round(nominal(s['raw'])),'muted')
    p.txt(382,214,('live %dhz' % round(nominal(s['live_raw']))) if s['mod'] else 'cutoff / '+s['mode'],'accent' if s['mod'] else 'muted','right')
    p.txt(20,231,'post-drive response / not spectrum','muted')
    return p


def lower(theme,s,pointer=None):
    p=Page(320,theme)
    p.txt(12,10,'filter','accent')
    p.txt(250,10,'byp','accent' if s['bypass'] else 'muted')
    p.txt(284,10,'x del','muted')
    p.hit('bypass',(242,0,280,24))
    for i,mode in enumerate(MODES):
        cx=36+61*i
        p.word(cx,49,mode,22,'accent' if s['mode']==mode else 'muted','center')
        if s['mode']==mode:p.diamond(cx,55,'accent',True)
        p.hit(mode,(7+61*i,28,65+61*i,59))
    p.txt(10,65,'cutoff','accent' if s['selected']=='cutoff' else 'muted')
    p.txt(105,65,'%dhz' % round(nominal(s['raw'])),'ink')
    p.txt(292,65,'res','accent' if s['selected']=='resonance' else 'muted','center')
    p.txt(292,78,'%d%%' % round(s['res']*100/32767),'muted','center')
    plot(p,s,(32,94,228,63))
    ry=157-round(s['res']*63/32767)
    p.line(295,94,295,157)
    p.diamond(295,ry,'accent',True)
    p.hit('relative log cutoff',(8,61,274,180))
    p.hit('relative resonance',(278,61,320,180))
    for x,label,text,amount,active in ((14,'drive','18%',.18,False),(172,'morph','%d%%' % round(s['morph']*100/32767),s['morph']/32767,s['selected']=='morph')):
        p.txt(x,188,label,'accent' if active else 'muted')
        p.txt(x+125,188,text,'ink','right')
        p.line(x+4,211,x+123,211)
        p.diamond(x+4+round(119*amount),211,'accent' if active else 'ink',True)
        p.hit(label,(x-4,182,x+133,225))
    p.txt(10,231,'b back','muted')
    p.txt(310,231,'l: mod >','muted','right')
    if pointer:
        xx,yy=pointer
        p.d.ellipse((xx-6,yy-6,xx+6,yy+6),outline=p.c['ink'])
        p.d.point((xx,yy),fill=p.c['ink'])
    return p


def ease(t):
    t=max(0.,min(1.,t));return t*t*(3-2*t)


def tween(t,keys,log=False):
    for (a,v),(b,w) in zip(keys,keys[1:]):
        if t<=b:
            s=ease((t-a)/(b-a))
            return v*(w/v)**s if log else v+(w-v)*s
    return keys[-1][1]


def demo(t):
    if t<3.6:
        hz=tween(t,[(0,800),(.4,800),(1.2,160),(2.5,2400),(3.2,800),(3.6,800)],True)
        s=config(hz=hz)
        pointer=(at(nominal(s['raw']),32,228),132) if .4<=t<3.2 else None
    elif t<6.0:
        res=tween(t,[(3.6,.32),(4.7,.82),(5.7,.32),(6.,.32)])
        s=config(res=res,selected='resonance')
        pointer=(295,157-round(s['res']*63/32767)) if t<5.7 else None
    elif t<9.0:
        morph=tween(t,[(6.,0),(6.8,.5),(7.2,.5),(7.7,1),(8.6,0),(9.,0)])
        s=config(morph=morph,selected='morph')
        pointer=(176+round(119*morph),211) if t<8.6 else None
    else:
        s=config();pointer=None
    return s,pointer


def save_png(im,name):
    path=OUT/name
    big=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
    info=PngImagePlugin.PngInfo();info.add_text('provenance',__doc__)
    big.save(path,pnginfo=info)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes()==big.tobytes()
        assert decoded.resize(im.size,Image.Resampling.NEAREST).tobytes()==im.tobytes()
    return path


def save_gif(frames):
    colors=sorted(set().union(*(set(im.getdata()) for im in frames)))
    assert len(colors)<=256,len(colors)
    palette=[v for c in colors for v in c]+[0]*(768-3*len(colors))
    indices={c:i for i,c in enumerate(colors)}
    indexed=[]
    for im in frames:
        p=Image.new('P',im.size);p.putpalette(palette)
        p.putdata([indices[c] for c in im.getdata()])
        assert p.convert('RGB').tobytes()==im.tobytes()
        indexed.append(p.resize((800,992),Image.Resampling.NEAREST))
    path=OUT/'filter-response-motion-2x.gif'
    indexed[0].save(path,save_all=True,append_images=indexed[1:],duration=50,loop=0,disposal=1,optimize=False)
    with Image.open(path) as d:
        tick=0
        for i in range(d.n_frames):
            d.seek(i);duration=d.info['duration'];assert duration%50==0
            for _ in range(duration//50):
                assert d.convert('RGB').tobytes()==indexed[tick].convert('RGB').tobytes();tick+=1
        assert tick==len(frames)
    return path,len(colors)


def main():
    assert not OUT.exists(),'preserve previous proposal; use a new revision directory'
    before=protected()
    ready={s:sha(ROOT/s) for s in ('release/play-modes/seance.3dsx','release/play-notes-stretch/seance.3dsx')}
    sources={s:sha(ROOT/s) for s in INPUTS}
    cases=0
    for theme in COLORS:
        for mode in MODES:
            for hz in (20,100,800,nominal(FMAX)):
                for res in (0.,1.):
                    for morph in (0.,.5,1.):
                        s=config(hz,res,morph,mode=mode);upper(theme,s);lower(theme,s);cases+=1
        for flags in ({'bypass':True},{'muted':True},{'live_hz':1500}):
            s=config(**flags);upper(theme,s);lower(theme,s);cases+=1
    frames=[]
    for i in range(192):
        s,pointer=demo(i*.05)
        frames.append(dual(upper('graphite',s),lower('graphite',s,pointer)))
    assert frames[0].tobytes()==frames[-1].tobytes()
    OUT.mkdir(parents=True)
    outputs=[]
    for theme in COLORS:
        outputs.append(save_png(dual(upper(theme,config()),lower(theme,config())),'filter-response-'+theme+'-dual-2x.png'))
    states=[('01 / frequency',config()),('02 / resonance',config(res=.82,selected='resonance')),
            ('03 / half post-drive input',config(morph=.5,selected='morph')),
            ('04 / modulation example',config(live_hz=1500))]
    board=Canvas(872,626,OUTSIDE)
    board.text(24,16,'filter / reactive upper screen',COLORS['graphite']['ink'],scale=2)
    board.text(24,42,'design only / calculated response, not audio / current fonts',COLORS['graphite']['muted'])
    for i,(label,s) in enumerate(states):
        x=24+(i%2)*424;y=74+(i//2)*276
        board.text(x,y,label,COLORS['graphite']['accent'])
        board.im.paste(upper('graphite',s).im,(x,y+18))
    board.text(24,610,'cutoff / resonance / morph move the curve; drive stays a separate amount',COLORS['graphite']['muted'])
    outputs.append(save_png(board.im,'filter-response-states-2x.png'))
    gif,palette=save_gif(frames);outputs.append(gif)
    assert before==protected() and sources=={s:sha(ROOT/s) for s in INPUTS}
    assert ready=={s:sha(ROOT/s) for s in ready}
    # Diagnostic of the EXISTING gesture, not proof of proposed input handling.
    old=[]
    for hz in (20,100,800,3000,nominal(FMAX)):
        raw=raw_hz(hz);next_raw=min(32767,raw+32767//232)
        old.append(dict(raw=raw,hz=nominal(raw),dx_touch=1,dx_marker=at(nominal(next_raw),32,232)-at(nominal(raw),32,232),next_hz=nominal(next_raw)))
    report=dict(created_at=datetime.now(timezone.utc).isoformat(),status='unapproved design-only proposal; no application changes',
        origin=__doc__,source_sha256=sources,protected_sha256=before,ready_artifacts_sha256=ready,
        old_swipe_probe=old,existing_nominal_ceiling_hz=nominal(FMAX),
        outputs=[dict(path=str(p.relative_to(ROOT)),bytes=p.stat().st_size,sha256=sha(p)) for p in outputs],
        validation=dict(native_layout_cases=cases,animated_dual_cases=len(frames),labels_bounded_nonoverlapping=True,
                        proposed_targets_min_32x24_and_nonoverlapping=True,lossless_png_nn2x=True,
                        exact_indexed_gif_colors=palette,gif_duration_ms=9600,periodic_first_last=True),
        limits=['no runtime controller/gesture tests, no app compile or deployment',
                'parameter animation is scripted, not captured audio or modulation',
                'linear post-drive model omits nonlinear drive, saturation, state history and rounding',
                'the <20hz and legacy raw plateau gesture policy still needs implementation and focused tests',
                'no new frequency range, DSP mapping, automatic migration, fine modifier or XY resonance promised',
                'p-lock/TEMP/lens/ownership/error states require runtime integration; this is a concept, not finished UI'])
    (OUT/'proposal.json').write_text(json.dumps(report,indent=2)+'\n')
    for p in outputs:print(p.relative_to(ROOT),p.stat().st_size,sha(p))
    print('pass:',cases,'static native dual layouts, 192 scripted dual frames; app/assets/artifacts unchanged')


if __name__=='__main__':main()
