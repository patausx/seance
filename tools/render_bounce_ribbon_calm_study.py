#!/usr/bin/env python3
"""Quieter bounce ribbon v2: authored Pillow mockup, not an app/device capture.

Selected direction: 02/ribbon. No runtime/controller/audio edits. Original Gossip
Med Pixel and native 6x8, static current graphite/pink paper, native then NN2x.
The waveform is the exact previous illustrative in-memory CC0 starter loop.
"""
from datetime import datetime, timezone
import json
from pathlib import Path

from PIL import Image, PngImagePlugin
from render_bounce_visual_study import (
    ROOT, COLORS, OUTSIDE, INPUTS as OLD_INPUTS, Page, Canvas,
    sha, protected, illustrative_wave, dual,
)

OUT = ROOT/'ui_mockups/bounce_visual_study/ribbon-calm-v2'
OLD = ROOT/'ui_mockups/bounce_visual_study'
STATES = ('setup', 'recording', 'take', 'saved')
INPUTS = ['tools/render_bounce_ribbon_calm_study.py'] + OLD_INPUTS


def inside(inner, outer):
    return (outer[0] <= inner[0] < inner[2] <= outer[2]
            and outer[1] <= inner[1] < inner[3] <= outer[3])


def control(p, text, baseline, key='', size=22, role='ink', center=False,
            active=True, target=None):
    # A narrow centrally placed list, not the full-width large-type folio rows.
    box = p.word(160 if center else 88, baseline, text, size, role,
                 'center' if center else 'left')
    target = target or (68, baseline-25, 252, baseline+11)
    assert inside(box, target), (text, box, target)
    if key:
        assert box[2] <= 226, (text, box)
        p.txt(242, baseline-10, key, 'muted', 'right')
    if active:
        p.hit(text, target)


def choices(p, label, labels, centers, bounds, baseline, selected, size,
            available=None):
    p.txt(24, baseline-42, label)
    for i, (word, x) in enumerate(zip(labels, centers)):
        enabled = available is None or available[i]
        role = 'rule' if not enabled else 'accent' if i == selected else 'muted'
        box = p.word(x, baseline, word, size, role, 'center')
        target = (bounds[i], baseline-25, bounds[i+1], baseline+17)
        assert inside(box, target)
        if i == selected:
            # Only a two-pixel selection mark. No full underline under each choice.
            p.d.rectangle((x, baseline+8, x+1, baseline+9), fill=p.c[role])
        p.hit(label+' '+word, target)


def ribbon(p, bars, progress, y=139):
    # One native-pixel rule; no thick fill or ornamental diamond. The current
    # bar alone gets accent. Numbers remain readable for all 1/2/4/8 lengths.
    assert bars in (1,2,4,8) and 0 <= progress <= 1
    x0, width = 30, 260
    current = min(bars-1, int(progress*bars)) if progress else -1
    tip = x0+round(width*progress)
    for i in range(bars):
        a=x0+round(width*i/bars); b=x0+round(width*(i+1)/bars)-6
        cx=(a+b)//2
        p.word(cx, 116, str(i+1), 22, 'accent' if i==current else 'muted', 'center')
        p.line(a, y, b, y)
        if tip > a:
            p.line(a, y, min(b,tip), y, 'accent' if i==current else 'muted')
    if progress:
        p.line(tip, y-2, tip, y+2, 'accent')


def lower(state, theme, source=0, bars=4, available=(True,)*4, notice=''):
    p = Page(320,theme)
    assert source in range(5) and bars in (1,2,4,8)
    if state == 'setup':
        choices(p,'source',('mix','1','2','3','4'),(48,116,170,224,278),
                (20,86,143,197,251,304),67,source,22)
        choices(p,'bars',('1','2','4','8'),(52,124,196,268),
                (16,88,160,232,304),141,(1,2,4,8).index(bars),28,available)
        if notice: p.txt(160,164,notice,'accent','center')
        control(p,'record',202,size=28,center=True,
                role='ink' if available[(1,2,4,8).index(bars)] else 'muted')
        p.txt(24,225,'a record'); p.txt(296,225,'b back','muted','right')
    elif state in ('recording','waiting'):
        source_name = 'mix' if source == 0 else 'chain '+str(source)
        p.txt(24,25,source_name+' / '+str(bars)+' bars')
        p.txt(296,25,'waiting' if state=='waiting' else 'recording','muted','right')
        ribbon(p,bars,0 if state=='waiting' else 3.5/8)
        control(p,'cancel' if state=='waiting' else 'stop early',202,size=28,center=True)
        p.txt(24,225,'a cancel' if state=='waiting' else 'a stop early')
        p.txt(296,225,'b back','muted','right')
    else:
        assert state in ('take','saved','listening','partial','save-error')
        control(p,'stop listening' if state=='listening' else 'listen',64,'a',role='accent')
        # Saving changes only the second line into a quiet status. Listen/use/again
        # never move and no new browser, filename editor or export mode is implied.
        control(p,'wav saved' if state=='saved' else 'save wav',104,
                '' if state=='saved' else 'x',role='muted' if state=='saved' else 'ink',
                active=state!='saved')
        if state=='partial':
            p.txt(88,134,'partial take','muted')
        else:
            control(p,'use in chain',144,'y')
        if state=='save-error':
            p.txt(160,165,'could not save / try again','accent','center')
        control(p,'record again',202,role='muted',center=True)
        p.txt(296,225,'b back','muted','right')
    return p


def upper(state,theme,waveform):
    p=Page(400,theme)
    p.word(24,39,'bounce')
    p.txt(376,25,'mix / stereo','muted','right')
    if state=='setup':
        p.txt(200,77,'record length','muted','center')
        p.word(200,135,'4',40,'ink','center')
        p.txt(200,152,'bars','muted','center')
        p.txt(200,188,'8.00s / 120 bpm','muted','center')
    elif state in ('recording','waiting'):
        p.txt(200,77,'starts next bar' if state=='waiting' else 'recording','muted','center')
        p.word(200,135,'0' if state=='waiting' else '2',40,'ink','center')
        p.txt(200,152,'of 4 bars','muted','center')
        p.txt(200,188,'0.00 / 8.00s' if state=='waiting' else '3.50 / 8.00s','muted','center')
        # The lower screen owns the bar ribbon: no second decorative/progress rail.
    else:
        assert state in ('take','saved','listening','partial','save-error')
        p.word(24,80,'listening' if state=='listening' else 'partial take' if state=='partial' else 'take ready',22)
        p.txt(376,69,'saved to recordings' if state=='saved' else 'not saved','muted','right')
        p.line(24,137,375,137)
        low,high,_=waveform
        for x,(lo,hi) in enumerate(zip(low,high)):
            p.line(24+x,137-hi,24+x,137-lo,'muted')
        if state=='listening': p.line(178,100,178,174,'accent')
        p.txt(24,184,'3.50s' if state=='listening' else '0.00s')
        p.txt(376,184,'8.00s','muted','right')
        p.txt(200,212,'4 bars / 120 bpm','muted','center')
    return p


def board(pages,full=False):
    w,h=(400,496) if full else (320,240)
    gap=24
    c=Canvas(w*2+gap*3,90+(h+50)*2+12,OUTSIDE)
    c.text(24,18,'bounce / ribbon, quieter',COLORS['graphite']['ink'],scale=2)
    c.text(24,47,'smaller type / fewer marks / a compact take menu',COLORS['graphite']['muted'])
    labels=('01 / setup','02 / recording','03 / take ready','04 / after save')
    for i,(state,label) in enumerate(zip(STATES,labels)):
        x=gap+(i%2)*(w+gap);y=82+(i//2)*(h+50)
        c.text(x,y,label,COLORS['graphite']['accent'])
        top,bottom=pages[state]
        c.im.paste(dual(top,bottom) if full else bottom.im,(x,y+20))
    c.text(24,c.im.height-19,'visual mockup / not implemented',COLORS['graphite']['muted'])
    return c.im


def save(im,name,meta):
    path=OUT/name
    assert not path.exists(),path
    out=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
    info=PngImagePlugin.PngInfo();info.add_text('description',__doc__)
    info.add_text('provenance',json.dumps(meta,sort_keys=True))
    out.save(path,pnginfo=info)
    with Image.open(path) as decoded:
        decoded.load()
        assert decoded.size==out.size and decoded.convert('RGB').tobytes()==out.tobytes()
        assert decoded.resize(im.size,Image.Resampling.NEAREST).tobytes()==im.tobytes()
    return dict(path=str(path.relative_to(ROOT)),size=list(out.size),bytes=path.stat().st_size,sha256=sha(path))


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    old={str(p.relative_to(ROOT)):sha(p) for p in OLD.iterdir() if p.is_file()}
    before=protected();sources={name:sha(ROOT/name) for name in INPUTS}
    prior=json.loads((OLD/'proposal.json').read_text())
    for name,h in prior['source_sha256'].items(): assert sha(ROOT/name)==h,name
    for item in prior['outputs']: assert sha(ROOT/item['path'])==item['sha256'],item['path']
    waveform=illustrative_wave()
    assert waveform[2]==prior['pcm_sha256']
    rendered={};cases=0
    for theme in COLORS:
        pages={}
        for state in STATES:
            pages[state]=upper(state,theme,waveform),lower(state,theme);cases+=2
        for source in range(5):
            for bars in (1,2,4,8):
                lower('setup',theme,source,bars,(True,True,False,False));cases+=1
        for bars in (1,2,4,8):
            for state in ('recording','waiting'):
                lower(state,theme,4,bars);cases+=1
        for state in ('listening','partial','save-error'):
            lower(state,theme);cases+=1
        for notice in ('chain muted / records silence','too long / not enough sample memory'):
            lower('setup',theme,1,notice=notice);cases+=1
        # The saved-state change is deliberately local to the second menu row.
        a,b=pages['take'][1].im,pages['saved'][1].im
        assert a.crop((0,0,320,79)).tobytes()==b.crop((0,0,320,79)).tobytes()
        assert a.crop((0,116,320,240)).tobytes()==b.crop((0,116,320,240)).tobytes()
        rendered[theme]=pages
    meta=dict(status='02/ribbon selected as a direction; this quieter revision awaits feedback; no app edits',
              created_at=datetime.now(timezone.utc).isoformat(),source_sha256=sources,
              origin='authored Pillow at native 400x240 / 320x240; no C++ or hardware capture',
              fonts='existing Gossip Med Pixel 22/28/40 ppem + native 6x8; thresholded glyphs',
              waveform='same prior 8s / 4 bars / 120bpm illustrative CC0 loop, in memory only, not engine recording',
              pcm_sha256=waveform[2],prior_proposal_sha256=sha(OLD/'proposal.json'),
              limits='no runtime/input/audio/animation/device testing; send/discard modals not redesigned')
    outputs=[]
    for theme,pages in rendered.items():
        outputs.append(save(board(pages),f'ribbon-calm-v2-{theme}-lower-2x.png',meta))
    outputs.append(save(board(rendered['graphite'],True),'ribbon-calm-v2-dual-2x.png',meta))
    for state in ('take','saved'):
        outputs.append(save(dual(*rendered['graphite'][state]),f'ribbon-calm-v2-{state}-dual-2x.png',meta))
    assert before==protected(),'production/assets changed'
    assert old=={str(p.relative_to(ROOT)):sha(p) for p in OLD.iterdir() if p.is_file()},'old study changed'
    assert sources=={name:sha(ROOT/name) for name in INPUTS}
    report=dict(meta,outputs=outputs,validation=dict(native_layout_cases=cases,
                text_bounds_and_no_overlap=True,proposed_target_bounds_and_no_overlap=True,
                controls_inside_targets=True,minimum_target=[32,24],saved_state_stable_outside_second_row=True,
                native_nn2x_decode=True,unchanged_production_assets=len(before),unchanged_prior_files=len(old)),
                layouts={state:dict(labels=p.labels,targets=p.hits) for state,(_,p) in rendered['graphite'].items()})
    (OUT/'proposal.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    for item in outputs: print(item['path'],item['size'],item['bytes'],item['sha256'])
    print('pass:',cases,'layout cases;',len(before),'production/assets and',len(old),'prior study files unchanged')


if __name__=='__main__':main()
