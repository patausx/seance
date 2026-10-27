#!/usr/bin/env python3
"""PLAY mode/config proposals. Authored Pillow pixels, NOT C++/device captures.

Two button treatments, five modes, orthogonal trim/slices/loop tools, and three
bounded config pages. Current Gossip + 6x8, static graphite/pink base paper.
Illustrative 4s CC0 loop constructed in memory; no PCM/audio exports or app edits.
"""
from array import array
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

from PIL import Image, PngImagePlugin
from render_bounce_visual_study import Page, Canvas, COLORS, OUTSIDE, ROOT, sha, protected, dual
from render_play_editor_study import fixture, SR, DURATION

OUT = ROOT / 'ui_mockups/play_modes_study'
MODES = ('normal', 'stretch', 'tape', 'scratch', 'hold')
INPUTS = [
    'tools/render_play_modes_study.py', 'tools/render_bounce_visual_study.py',
    'tools/render_play_editor_study.py', 'tools/render_module_geometry_study.py',
    'tools/render_menu_style_study.py', 'tools/generate_start_menu_labels.py',
    'assets/fonts/Gossip-MedPixel.otf', 'core/ui/font.cpp',
    'platform/3ds/ui_palette.h', 'platform/3ds/ui_play_editor.cpp',
    'platform/3ds/ui_module_focus_play.cpp', 'docs/play-notes-stretch.md',
] + ['sample_library/starter_cc0/wav/' + n + '.wav'
     for n in ('kick_punch', 'snare_synth', 'hat_closed')]


def inside(a, b):
    return b[0] <= a[0] < a[2] <= b[2] and b[1] <= a[1] < a[3] <= b[3]


def text_button(p, name, box, label=None, active=False, gothic=False, outline=False):
    x0, y0, x1, y1 = box
    role = 'accent' if active else 'muted'
    if outline:
        p.d.rectangle((x0, y0, x1-1, y1-1), outline=p.c[role if active else 'rule'])
        if active:
            tint = (49, 44, 57) if p.theme == 'graphite' else (216, 178, 202)
            p.d.rectangle((x0+1, y0+1, x1-2, y1-2), fill=tint)
            p.line(x0, y0+2, x0, y1-3, 'accent', 2)
    label = label or name
    if gothic:
        result = p.word((x0+x1)//2, y0+20, label, 22, role, 'center')
    else:
        p.txt((x0+x1)//2, (y0+y1)//2-4, label, role, 'center')
        result = p.labels[-1][0]
    assert inside(result, box), (label, result, box)
    p.hit(name, box)


def mode_rail(p, mode, variant, config=False):
    p.txt(277, 16, 'mode', 'muted', 'center')
    for i, label in enumerate(MODES):
        y = 36+i*32
        active = label == mode and not config
        if variant == 'keys':
            text_button(p, label, (246, y, 312, y+27), active=active, outline=True)
        else:
            text_button(p, label, (236, y, 318, y+28), active=active, gothic=True)
            if active:
                p.diamond(239, y+14, 'accent', True)
            elif config and label == mode:
                p.diamond(239, y+14, 'rule', False)
    p.line(247, 201, 310, 201)
    text_button(p, 'config', (236 if variant == 'index' else 246, 208, 318 if variant == 'index' else 312, 238),
                active=config, gothic=variant=='index', outline=variant=='keys')
    if variant == 'index' and config:
        p.diamond(239, 223, 'accent', True)


def params(p, mode):
    if mode in ('scratch', 'hold'):
        p.txt(14, 37, 'position')
        p.word(14, 77, '46', 40)
        p.txt(60, 68, '%', 'muted')
        p.hit('stored position', (10, 30, 106, 82))
        p.txt(124, 40, 'touch preview', 'accent')
        p.txt(124, 58, 'move to sound' if mode=='scratch' else 'still = sound')
        p.txt(124, 73, 'lift = stop')
        return
    left = 'speed' if mode == 'tape' else 'note'
    p.txt(14, 37, left)
    if mode == 'tape':
        p.word(14, 76, '0.75', 28)
        p.txt(60, 68, 'x')
    else:
        p.word(14, 77, 'c4', 40)
    p.hit(left, (10, 30, 110, 83))
    p.txt(126, 37, 'tempo' if mode=='stretch' else 'gain')
    p.word(126, 76, '0.75' if mode=='stretch' else '100', 28,
           'accent' if mode=='stretch' else 'ink')
    p.txt(181, 68, 'x' if mode=='stretch' else '%')
    p.hit('tempo' if mode=='stretch' else 'gain', (116, 30, 225, 83))


def tools_tabs(p, selected):
    for label, x0, x1 in (('trim', 10, 80), ('slices', 82, 154), ('loop', 156, 225)):
        text_button(p, 'tool '+label, (x0, 85, x1, 109), label=label, active=label==selected)
        if selected == label:
            p.line((x0+x1)//2-9, 107, (x0+x1)//2+9, 107, 'accent')


def wave(p, pcm, box, tool='trim', mode='normal', interactive=False):
    x, y, w, h = box
    zero = y+h//2
    p.line(x, zero, x+w-1, zero)
    def at(ratio): return x+round((w-1)*ratio)
    for col in range(w):
        a, b = col*len(pcm)//w, (col+1)*len(pcm)//w
        low, high = min(pcm[a:b]), max(pcm[a:b])
        if tool=='slices': role = 'ink' if .25 <= col/w < .375 else 'muted'
        elif tool=='loop': role = 'ink' if .25 <= col/w < .75 else 'muted'
        else: role = 'muted'
        p.line(x+col, zero-round(high*(h//2-4)/32768),
               x+col, zero-round(low*(h//2-4)/32768), role)
    if tool == 'slices':
        for i in range(9):
            xx = at(i/8)
            p.line(xx, y, xx, y+h-1, 'accent' if i in (2,3) else 'rule')
        p.line(at(.25), y+h, at(.375), y+h, 'accent', 2)
    elif mode not in ('scratch', 'hold'):
        left, right = (.25,.75) if tool=='loop' else (0.,1.)
        for r in (left, right):
            xx=at(r)
            p.line(xx, y, xx, y+h-1, 'accent')
            p.line(max(x,xx-3), y, min(x+w-1,xx+3), y, 'accent', 2)
        if tool=='loop':
            p.line(at(.7), y+h-1, at(.75), y, 'accent')
    if mode in ('scratch', 'hold'):
        xx = at(.46)
        if mode=='hold':
            p.line(xx-3,y+3,xx-3,y+h-4,'accent')
            p.line(xx+3,y+3,xx+3,y+h-4,'accent')
        else:
            p.line(xx,y+3,xx,y+h-4,'accent')
        p.d.ellipse((xx-5, zero-5, xx+5, zero+5), outline=p.c['accent'])
    if interactive:
        if mode in ('scratch','hold'):
            p.hit('touch audition', (x,y,x+w,y+h))
        elif tool=='trim':
            p.hit('trim start lane', (x,y,x+w,y+h//2))
            p.hit('trim end lane', (x,y+h//2,x+w,y+h))
        else:
            p.hit(tool+' wave', (x,y,x+w,y+h))


def contextual_tools(p, tool, mode):
    if mode in ('scratch','hold'):
        p.txt(14, 181, 'source audition', 'muted')
        p.txt(222, 181, '1x', 'muted', 'right')
        text_button(p,'zoom',(10,190,112,214),'zoom 1x')
        text_button(p,'sequence scrub options',(116,190,225,214),'seq off >')
    elif tool=='slices':
        p.txt(14,181,'03 / 08','accent')
        p.txt(222,181,'0.500s','muted','right')
        text_button(p,'add cut',(10,190,80,214),'+ cut')
        text_button(p,'auto cuts',(82,190,154,214),'auto')
        text_button(p,'cut details',(156,190,225,214),'edit >')
    elif tool=='loop':
        p.txt(14,181,'in 1.00s')
        p.txt(222,181,'out 3.00s','muted','right')
        text_button(p,'crossfade',(10,190,122,214),'seam 200ms')
        text_button(p,'loop details',(126,190,225,214),'edit >')
    else:
        p.txt(14,181,'0.000s')
        p.txt(222,181,'4.000s','muted','right')
        text_button(p,'fades',(10,190,80,214),'fades')
        text_button(p,'zoom',(82,190,154,214),'zoom 1x')
        text_button(p,'trim details',(156,190,225,214),'edit >')


def lower(theme, mode='normal', variant='index', tool='trim', filename='cc0_break.wav', pcm=None):
    p=Page(320,theme)
    label = filename if len(filename)<=30 else filename[:27]+'...'
    p.txt(14,16,label,'ink')
    p.txt(220,16,'>','accent','right')
    p.hit('choose sample',(10,4,225,28))
    mode_rail(p,mode,variant)
    params(p,mode)
    if mode in ('scratch','hold'):
        p.txt(14,94,'drag the wave','accent')
    else:
        tools_tabs(p,tool)
    wave(p,pcm,(14,120,208,54),tool,mode,True)
    contextual_tools(p,tool,mode)
    if mode in ('scratch','hold'):
        p.txt(14,227,'l: mod position')
        text_button(p,'to sequence',(156,216,225,240),'seq >')
    else:
        text_button(p,'listen',(10,216,80,240),'listen',active=True)
        text_button(p,'undo',(82,216,154,240),'undo')
        text_button(p,'to sequence',(156,216,225,240),'seq >')
    return p


def config_page(theme, group='voice', variant='index', gate=False):
    p=Page(320,theme)
    p.word(14,29,'config',28)
    mode_rail(p,'stretch',variant,True)
    for i,name in enumerate(('voice','listen','file')):
        text_button(p,'config '+name,(10+i*72,40,78+i*72,64),name,active=group==name)
        if name==group:p.line(30+i*72,62,57+i*72,62,'accent')
    if group=='voice':
        rows=[('trigger','gate >' if gate else 'one shot >'),('loop','off'),
              ('gate length','1 step' if gate else '--'),('direction','forward'),('gain','100%'),('bypass','off')]
    elif group=='listen':
        rows=[('route','source >'),('repeat','off'),('listen start','>'),('listen end','>'),('listen seam','>')]
    else:
        rows=[('normalize','off'),('remove dc','off'),('bake fades','off'),('bake reverse','off'),('save selection','wav >'),('detach raw','copy >')]
    for i,(key,value) in enumerate(rows):
        y=77+i*24
        p.txt(14,y,key,'muted' if key=='gate length' and not gate else 'ink')
        p.txt(221,y,value,'rule' if key=='gate length' and not gate else 'accent','right')
        p.hit('setting '+key,(10,y-8,225,y+16))
    if group=='voice':p.txt(14,227,'b back to wave')
    elif group=='listen':
        p.txt(14,210,'preview, not patch loop')
        p.txt(14,227,'b back to wave')
    else:p.txt(14,227,'new file / source untouched')
    return p


def upper(theme, mode='normal', tool='trim', pcm=None, config=None):
    p=Page(400,theme)
    p.word(20,40,'play',28)
    p.txt(90,19,'cc0_break.wav','ink')
    p.txt(90,36,'4.000s / mono')
    p.txt(380,19,'ch01 / 01','muted','right')
    p.txt(380,36,'one shot','muted','right')
    p.word(20,73,mode,22,'accent')
    p.txt(380,62,'touch preview' if mode in ('scratch','hold') else 'sample overview','muted','right')
    wave(p,pcm,(20,92,360,62),tool,mode)
    for i in range(5):
        x=20+i*359//4
        p.line(x,157,x,160)
        p.txt(min(x,368),166,str(i)+'s')
    if config:
        p.word(20,213,'config',28)
        p.txt(200,192,{'voice':'playback in the patch','listen':'private audition options','file':'save a separate wav'}[config],'ink')
        p.txt(200,210,{'voice':'not preview repeat','listen':'source or effects chain','file':'original stays unchanged'}[config])
    elif mode in ('scratch','hold'):
        p.txt(20,190,'position')
        p.word(20,225,'46',40)
        p.txt(68,216,'%')
        p.txt(200,193,'move cursor to sound' if mode=='scratch' else 'up to 50ms under cursor','ink')
        p.txt(200,211,'lift stops source audition')
    else:
        p.txt(20,190,'speed' if mode=='tape' else 'note')
        p.word(20,225,'0.75' if mode=='tape' else 'c4',40)
        if mode=='tape':p.txt(83,216,'x')
        p.txt(200,193,{'normal':'note changes duration','stretch':'tempo 0.75x / independent','tape':'speed changes pitch'}[mode],'ink')
        p.txt(200,211,{'normal':'source prepared at c4','stretch':'manual tempo / no bpm sync','tape':'original varispeed'}[mode])
    return p


def board(title, subtitle, cells, cols=2):
    gap=24; cw=320; ch=240; pitch=286
    rows=(len(cells)+cols-1)//cols
    c=Canvas(gap+(cw+gap)*cols,78+rows*pitch,OUTSIDE)
    c.text(24,16,title,COLORS['graphite']['ink'],scale=2)
    c.text(24,44,subtitle,COLORS['graphite']['muted'])
    for i,(label,p) in enumerate(cells):
        x=24+(i%cols)*344; y=78+(i//cols)*pitch
        c.text(x,y,label,COLORS['graphite']['accent'])
        c.im.paste(p.im,(x,y+20))
    c.text(24,c.im.height-14,'seance / design proposal / not implemented',COLORS['graphite']['muted'])
    return c.im


def save(image,name):
    path=OUT/name
    if path.exists():raise SystemExit('preserve existing output: '+str(path))
    out=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
    info=PngImagePlugin.PngInfo()
    info.add_text('description',__doc__)
    out.save(path,pnginfo=info)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes()==out.tobytes()
        assert decoded.resize(image.size,Image.Resampling.NEAREST).tobytes()==image.tobytes()
    return dict(path=str(path.relative_to(ROOT)),size=list(out.size),bytes=path.stat().st_size,sha256=sha(path))


def main():
    before=protected()
    ready=ROOT/'release/play-notes-stretch/seance.3dsx'
    artifact=sha(ready)
    sources={s:sha(ROOT/s) for s in INPUTS}
    pcm=fixture(); pcm_hash=hashlib.sha256(array('h',pcm).tobytes()).hexdigest()
    pages={}; cases=0; outputs=[]
    for theme in COLORS:
        for variant in ('keys','index'):
            for mode in MODES:
                for tool in (('trim','slices','loop') if mode not in ('scratch','hold') else ('trim',)):
                    p=lower(theme,mode,variant,tool,pcm=pcm)
                    pages[theme,variant,mode,tool]=p;cases+=1
                # Long source names truncate before the independent choose arrow.
                lower(theme,mode,variant,filename='a_very_long_sample_source_filename_for_layout.wav',pcm=pcm);cases+=1
            for group in ('voice','listen','file'):
                pages[theme,variant,'config',group]=config_page(theme,group,variant);cases+=1
            config_page(theme,'voice',variant,True);cases+=1
        for mode in MODES:upper(theme,mode,pcm=pcm);cases+=1
        for group in ('voice','listen','file'):upper(theme,'stretch',pcm=pcm,config=group);cases+=1
    OUT.mkdir(parents=True,exist_ok=True)
    for theme in COLORS:
        cells=[('01 / small keys',pages[theme,'keys','normal','trim']),
               ('02 / quiet gothic',pages[theme,'index','normal','trim'])]
        outputs.append(save(board('play / two button directions','same modes / same controls / two visual treatments',cells),
                            'play-directions-'+theme+'-2x.png'))
    cells=[(m+' / '+{'normal':'play by note','stretch':'independent tempo','tape':'varispeed','scratch':'move to sound','hold':'still makes sound'}[m],
            pages['graphite','index',m,'trim']) for m in MODES]
    cells.append(('config / voice',pages['graphite','index','config','voice']))
    outputs.append(save(board('play / one mode at a time','02 / persistent mode buttons, only the relevant controls',cells,3),'play-modes-graphite-2x.png'))
    cells=[('stretch / trim',pages['graphite','index','stretch','trim']),
           ('stretch / slices',pages['graphite','index','stretch','slices']),
           ('stretch / loop',pages['graphite','index','stretch','loop']),
           ('config / voice',pages['graphite','index','config','voice']),
           ('config / listen',pages['graphite','index','config','listen']),
           ('config / file',pages['graphite','index','config','file'])]
    outputs.append(save(board('play / tools and config','sound mode and wave tool are separate / no endless menu',cells,3),'play-tools-config-graphite-2x.png'))
    for theme in COLORS:
        outputs.append(save(dual(upper(theme,'stretch',pcm=pcm),pages[theme,'index','stretch','trim']),
                            'play-stretch-'+theme+'-dual-2x.png'))
    for name,p in [('normal',pages['graphite','index','normal','trim']),
                   ('stretch-slices',pages['graphite','index','stretch','slices']),
                   ('config',pages['graphite','index','config','voice'])]:
        outputs.append(save(p.im,'play-'+name+'-lower-2x.png'))
    assert before==protected(),'production/assets changed'
    assert sha(ready)==artifact,'ready artifact changed'
    assert sources=={s:sha(ROOT/s) for s in INPUTS},'render inputs changed'
    report=dict(created_at=datetime.now(timezone.utc).isoformat(),status='unselected visual proposal; no runtime changes',
        origin='authored Pillow at native 400x240 / 320x240; NOT runtime/hardware capture',
        fonts='current Gossip Med Pixel 22/28/40 ppem + native 6x8',
        background='current static graphite/pink base; no animated paper captured',
        waveform='illustrative 4s in-memory score from 3 local CC0 samples; exhaustive per-column extrema; not user/project/engine audio',
        pcm_sha256=pcm_hash,source_sha256=sources,outputs=outputs,
        source_map='normal/stretch/tape are pitch engines; scratch/hold are direct-position editor workspaces; keep prior engine internally',
        behavior_proposed='mode buttons do not erase trim/cuts/loop, rewrite PCM, silently enable sequence scrub, or enable patch loop',
        config=['voice: patch playback','listen: private preview','file: explicit new copy'],
        design_limits=['input not implemented or tested','no runtime performance/gpu/audio assertions',
                       'edit subpages, sequence-scrub modal, p-lock, error/protected/no-source views remain to be designed',
                       'normal source c4 is prepared, not detected; stretch tempo is manual, no BPM sync',
                       'no app build, broad suites, FTP, CIA or user data changes'],
        references=[dict(url='https://www.ableton.com/en/live-manual/12/live-instrument-reference/#simpler',
                         principle='mode-specific controls, not copied artwork or a claim of equivalent functionality'),
                    dict(url='https://www.studiobrootle.com/ableton-simpler/',principle='classic / one-shot / slice overview')],
        validation=dict(native_layout_cases=cases,labels_bounded_and_nonoverlapping=True,
                        proposed_targets_bounded_nonoverlapping=True,nn2x_lossless_decode=True,
                        unchanged_production_assets=len(before),ready_artifact_sha256=artifact),
        layouts={'/'.join(k):dict(labels=p.labels,targets=p.hits) for k,p in pages.items()})
    (OUT/'proposal.json').write_text(json.dumps(report,indent=2)+'\n')
    for item in outputs:print(item['path'],item['size'],item['sha256'])
    print('pass:',cases,'native layouts;',len(before),'production/assets unchanged; no app build or audio export')


if __name__=='__main__':main()
