#!/usr/bin/env python3
"""Fold visual proposal only: authored UI, not C++/LCD capture or live audio.

Five existing parameters. A fixed 0.75FS, two-cycle synthetic sine is processed
by a Python transcription of the current integer Fold formula, including its
rectification/DC and final saturation. No new sound features are simulated.
"""
from dataclasses import dataclass, asdict, replace
from pathlib import Path
import hashlib
import json
import math
from PIL import Image
from render_menu_style_study import Page, paper, GHOST, BRANCH, overlaps
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair
from render_project_clock_study import DIGITS

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/fold_wave_study'
X0, X1, ZERO, HEIGHT = 28, 306, 132, 36
COUNT = (X1-X0)*8
SOURCE = tuple(round(24575 * math.sin(4*math.pi*i/COUNT)) for i in range(COUNT+1))


@dataclass(frozen=True)
class State:
    name: str = '01_fold'
    drive: int = 8192
    mode: int = 0
    bias: int = 0
    trim: int = 32767
    mix: int = 32767
    selected: str = 'drive'


CASES = (State(), State('02_wrap', mode=16384, selected='mode'),
         State('03_diode', mode=32767, selected='mode'),
         State('04_bias', bias=11468, selected='bias'))


def shape(dry, s):
    x = (dry*(32767+7*s.drive) >> 15) + s.bias
    folded = x
    for _ in range(8):
        if -32768 <= folded <= 32767:
            break
        if folded > 32767:
            folded = 65534-folded
        if folded < -32768:
            folded = -65536-folded
    wrapped = ((x+32768) & 0xffff)-32768
    diode = -(x >> 2) if x < 0 else x
    shaped = (folded+((wrapped-folded)*s.mode >> 14) if s.mode < 16384 else
              wrapped+((diode-wrapped)*(s.mode-16384) >> 14))
    shaped = (shaped-s.bias)*s.trim >> 15
    return max(-32768, min(32767, dry+((shaped-dry)*s.mix >> 15)))


def value(s, key):
    raw = getattr(s, key)
    if key == 'drive':
        return f'{1+raw*7/32767:.2f}', 'x'
    if key == 'bias':
        return f'{raw/32767:+.2f}', 'fs'
    if key == 'trim':
        return ('mute', '') if raw == 0 else (f'{20*math.log10(raw/32767):.1f}', 'db')
    return str(round(raw*100/32767)), '%'


def text_value(s, key):
    number, unit = value(s, key)
    return number+unit


def position(s, key):
    return (s.bias+32767)/65534 if key == 'bias' else getattr(s, key)/32767


def mode_description(s):
    if s.mode == 0:
        return 'fold / reflection'
    if s.mode == 16384:
        return 'wrap / discontinuity'
    if s.mode == 32767:
        return 'diode / rectification'
    return 'fold > wrap' if s.mode < 16384 else 'wrap > diode'


def diamond(c, x, y, color, filled=True):
    points = [(x,y-3),(x+3,y),(x,y+3),(x-3,y)]
    c.d.polygon(points, fill=color if filled else None, outline=color)


def rail(c, x0, x1, y, pos, color, bipolar=False):
    c.line((x0,y,x1,y), BRANCH)
    for x in (x0,(x0+x1)//2,x1):
        c.line((x,y-2,x,y+2), GHOST if bipolar and x == (x0+x1)//2 else BRANCH)
    diamond(c, round(x0+(x1-x0)*pos), y, color)


def waveform(c, s):
    # No input-dependent normalization, scrolling phase, invented telemetry or
    # decorative ringing. Eight model samples per pixel preserve wrap edges.
    c.line((14,84,24,84), GHOST)
    c.text(30,80,'test sine / .75fs',GHOST)
    c.line((236,84,250,84),SPOT)
    c.text(256,80,'shaped',SPOT)
    for raw, label in ((32767,'+1'),(0,'0'),(-32768,'-1')):
        y = round(ZERO-HEIGHT*raw/32768)
        c.text(8,y-4,label,GHOST)
        if raw == 0:
            c.line((X0,y,X1,y),BRANCH)
        else:
            for x in range(X0,X1,5):
                c.d.point((x,y),fill=BRANCH)
    for raw in (SOURCE, tuple(shape(v,s) for v in SOURCE)):
        output = raw is not SOURCE
        pts = [(round(X0+(X1-X0)*i/COUNT),round(ZERO-HEIGHT*v/32768))
               for i,v in enumerate(raw)]
        assert all(X0 <= x <= X1 and 96 <= y <= 168 for x,y in pts)
        c.d.line(pts, fill=SPOT if output else GHOST, width=2 if output else 1)


def lower(s):
    c = Page()
    c.text(14,10,'fold',SPOT)
    c.line((14,23,37,23),SPOT)
    c.text(56,10,'ch01 / 02',GHOST)
    c.text(258,10,'byp',GHOST)
    c.text(294,10,'x',GHOST)
    c.hit('bypass', (246,0,284,25))
    # x is a physical delete-button hint, not a new tap-delete action.
    color = SPOT if s.selected == 'drive' else INK
    c.text(14,32,'drive',color)
    c.text(14,48,text_value(s,'drive'),color)
    rail(c,14,142,64,position(s,'drive'),color)
    c.hit('drive / horizontal relative drag; tap only selects',(8,27,154,73))
    color = SPOT if s.selected == 'mode' else INK
    c.text(174,32,'mode',color)
    c.text(306,32,text_value(s,'mode'),color,right=True)
    rail(c,186,292,48,position(s,'mode'),color)
    # These are continuous-morph anchor labels, NOT three discrete buttons.
    for raw,x,label in ((0,174,'fold'),(16384,227,'wrap'),(32767,278,'diode')):
        c.text(x,60,label,SPOT if s.mode == raw else GHOST)
    c.hit('mode / one continuous relative drag, anchors 0 / 50 / 100%',(164,27,314,73))
    waveform(c,s)
    for i,key in enumerate(('bias','trim','mix')):
        x = 14+104*i
        color = SPOT if s.selected == key else INK
        c.text(x,182,key,SPOT if s.selected == key else GHOST)
        c.text(x,196,text_value(s,key),color)
        rail(c,x,x+82,213,position(s,key),color,bipolar=key=='bias')
        c.hit(key+' / fixed horizontal relative drag; tap only selects',
              (8+104*i,177,104+104*i,220))
    c.line((14,223,306,223),BRANCH)
    c.footer('b back','mod / temp >')
    c.hit('back',(8,225,92,240))
    c.hit('selected parameter mod / temp',(214,225,314,240))
    return c


def numeric(c, text):
    # Same masks, signs, decimal, scale selection and baseline as inspector_gothic.
    widths = [DIGITS[ch][0].width+2 if ch.isdigit() else 6 if ch=='.' else 18 for ch in text]
    scale = 2 if sum(widths)*2 <= 210 else 1
    x, baseline = 14,134
    boxes=[]
    for ch, advance in zip(text,widths):
        if ch.isdigit():
            mask, top = DIGITS[ch]
            mask = mask.resize((mask.width*scale,mask.height*scale),Image.Resampling.NEAREST)
            y = baseline+top*scale
            c.im.paste(INK,(x,y),mask)
            boxes.append((x,y,x+mask.width,y+mask.height))
        elif ch=='.':
            box=(x,baseline-3*scale,x+3*scale,baseline)
            c.d.rectangle((box[0],box[1],box[2]-1,box[3]-1),fill=INK)
            boxes.append(box)
        else:
            y=baseline-14*scale
            c.d.rectangle((x,y,x+13*scale-1,y+2*scale-1),fill=SPOT)
            if ch=='+':
                cx=x+(11*scale+1)//2
                c.d.rectangle((cx,baseline-20*scale,cx+2*scale-1,baseline-7*scale-1),fill=SPOT)
            boxes.append((x,baseline-20*scale,x+13*scale,y+2*scale))
        x+=advance*scale
    box=(min(b[0] for b in boxes),min(b[1] for b in boxes),max(b[2] for b in boxes),max(b[3] for b in boxes))
    assert box[2] <= 224 and box[1] >= 66 and box[3] <= 147
    assert all(not overlaps(box,old) for old,_ in c.labels)
    c.labels.append((box,'gothic value '+text))


def upper(s):
    c=Page(400)
    c.text(14,10,'fold',SPOT)
    c.line((14,23,37,23),SPOT)
    c.text(74,10,'osc > fold > out',GHOST)
    c.text(386,10,'ch01 / 02',GHOST,right=True)
    titles={'drive':'drive / input gain','mode':'mode / continuous morph',
            'bias':'bias / input offset','trim':'trim / wet level','mix':'mix / dry and shaped'}
    c.text(14,42,titles[s.selected],SPOT)
    c.text(14,58,'base',GHOST)
    number,unit=value(s,s.selected)
    if number == 'mute':
        c.text(14,104,'mute',INK)
    else:
        numeric(c,number)
    c.text(14,152,unit if unit else 'wet muted',GHOST)
    c.text(14,167,'stored '+text_value(s,s.selected),GHOST)
    rail(c,14,224,184,position(s,s.selected),INK,bipolar=s.selected=='bias')
    lo,hi={'drive':('1x','8x'),'mode':('fold','diode'),'bias':('-1fs','+1fs'),
           'trim':('mute','0db'),'mix':('0%','100%')}[s.selected]
    c.text(14,199,lo,GHOST)
    c.text(224,199,hi,GHOST,right=True)
    if s.selected=='mode':
        c.text(105,199,'wrap',GHOST)
    c.line((240,42,240,210),BRANCH)
    v=text_value(s,s.selected)
    for y,text,color in ((42,'example state',GHOST),(64,'target '+v,INK),
                         (81,'live '+v,SPOT),(108,'mod none',INK),
                         (124,'depth --',GHOST),(148,'potential range',GHOST),
                         (162,v,INK),(175,v,INK),(191,'| base   <> live',GHOST),
                         (205,'via base',GHOST)):
        c.text(254,y,text,color)
    c.text(14,217,mode_description(s)+' / 2 test cycles',GHOST)
    c.text(14,230,'concept / fixed test sine, not captured audio',GHOST)
    return c


def guarded_sources():
    files=('core/engine/units.cpp','core/engine/units.h','core/engine/patch_io.cpp',
           'platform/3ds/ui_module_focus_fold.cpp','platform/3ds/module_inspector.cpp',
           'platform/3ds/ui_param_lens.cpp','platform/3ds/note_display_data.h','core/ui/font.cpp')
    return {name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files}


def validate():
    assert SOURCE[0] == SOURCE[-1] == 0 and max(SOURCE)==24575 and min(SOURCE)==-24575
    for mode in (0,8192,16384,24576,32767):
        for bias in (-32767,0,32767):
            s=State(drive=32767,mode=mode,bias=bias)
            for v in SOURCE:
                assert shape(v,replace(s,mix=0))==v
                assert -32768 <= shape(v,s) <= 32767
    s=State(drive=0,mode=32767)
    assert all(shape(v,s)>=0 for v in SOURCE)
    assert shape(0,s)==0
    # Same targets at all values; never follow the moving waveform or marker.
    expected=lower(CASES[0]).hits
    for key in ('drive','mode','bias','trim','mix'):
        for raw in ((-32767,0,32767) if key=='bias' else (0,16384,32767)):
            s=replace(CASES[0],selected=key,**{key:raw})
            assert lower(s).hits==expected
            upper(s)


def main():
    before=guarded_sources()
    validate()
    OUT.mkdir(parents=True,exist_ok=True)
    allowed=set(paper(400).get_flattened_data())|{INK,SPOT,GHOST,BRANCH,OUTSIDE}
    board=Canvas(1232,532,OUTSIDE)
    comparison=Canvas(992,276,OUTSIDE)
    report={'status':'visual proposal only; authored UI, not runtime/hardware capture',
            'runtime_implemented':False,'new_dsp_implemented':False,'sources':before,
            'reference':'same 0.75FS two-cycle sine, q15 values, fixed +/-1FS vertical range; no normalization',
            'model':'Python transcription of current Fold integer formula; includes output clipping and diode DC; not measured PCM or independently certified C++ parity',
            'typography':'native 6x8 and existing note_display_data.h gothic masks / inspector_gothic geometry',
            'gestures':'all five controls fixed, horizontal relative drag; taps select without changing value; mode one continuous 0..100% rail; footer retains selected mod/temp',
            'inspector':'explicit unmodulated example states; target=live=base, never actual engine telemetry',
            'cases':{}}
    titles=('01 / fold','02 / wrap','03 / diode','04 / bias +0.35fs')
    for i,s in enumerate(CASES):
        u,b=upper(s),lower(s)
        for name,im in (('top',u.im),('bottom',b.im),('dual',pair(u.im,b.im))):
            assert set(im.get_flattened_data())<=allowed
            im.save(OUT/f'{s.name}_{name}_native.png')
            im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST).save(OUT/f'{s.name}_{name}_2x.png')
        if i<3:
            board.text(14+i*408,9,titles[i],BRANCH)
            board.im.paste(pair(u.im,b.im),(8+i*408,28))
            comparison.text(14+i*328,9,titles[i],BRANCH)
            comparison.im.paste(b.im,(8+i*328,28))
        output=[shape(v,s) for v in SOURCE]
        report['cases'][s.name]={'settings':asdict(s),'touch_regions':b.hits,'labels':u.labels+b.labels,
                                'output_min':min(output),'output_max':max(output)}
    for name,im in (('comparison',board.im),('surfaces',comparison.im)):
        im.save(OUT/f'{name}_native.png')
        im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST).save(OUT/f'{name}_2x.png')
    assert before==guarded_sources(),'runtime/font source changed during preview'
    report['checks']=['native label bounds/nonoverlap for four fixtures and 15 selected extrema',
                      'fixed nonoverlapping touch targets including extrema', 'exact palette / nearest-only scaling',
                      'synthetic model range and dry identity checks', 'guarded runtime/font hashes unchanged']
    (OUT/'concept.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'README.txt').write_text('fold / wave shaping study\n\npreview only, no application changes or deployment.\n'
        '01 fold, 02 wrap, 03 diode share every parameter except mode. 04 adds bias.\n'
        'grey = unchanged fixed test sine; violet = final shaped/mixed model including trim and clipping.\n'
        'continuous mode anchors at 0/50/100%. relative gestures, no moving touch zones.\n'
        'diode is the EXISTING positive rectifier; no DC removal/oversampling added to the preview.\n'
        'native 400x240 + centered 320x240, 16px presentation gap; nearest 2x.\n'
        'reproduce: python3 tools/render_fold_wave_study.py\n')
    print('fold concept: 4 duals + fold/wrap/diode comparison; model/layout/palette/source guards passed')
    print(OUT/'01_fold_dual_2x.png')
    print(OUT/'surfaces_2x.png')


if __name__=='__main__':
    main()
