#!/usr/bin/env python3
"""Bounce visual study: authored native pixels, not runtime or hardware capture.

Existing source/bar/actions only. Gossip Med Pixel (Deborah Khodanovich 2025,
SIL OFL) + native 6x8. A labelled illustrative 8-second loop is constructed in
memory from three local CC0 starter samples; no audio is exported or recorded.
"""
from array import array
from datetime import datetime, timezone
from functools import lru_cache
import hashlib
import json
from pathlib import Path
import sys
import wave

from PIL import Image, ImageDraw, ImageFont, PngImagePlugin
from render_module_geometry_study import Canvas
from render_menu_style_study import heading_mask, overlaps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/bounce_visual_study'
OUTSIDE = (18, 18, 21)
COLORS = {
    'graphite': dict(ink=(244,244,244), accent=(181,154,232), muted=(184,184,184), rule=(112,112,112)),
    'pink': dict(ink=(31,20,31), accent=(91,49,124), muted=(126,100,119), rule=(174,130,151)),
}
STATES = ('setup', 'recording', 'take')
VARIANTS = ('01-folio', '02-ribbon')
SAMPLES = ['sample_library/starter_cc0/wav/'+n+'.wav' for n in ('kick_deep','snare_synth','hat_closed')]
INPUTS = ['tools/render_bounce_visual_study.py', 'tools/render_module_geometry_study.py',
          'tools/render_menu_style_study.py', 'tools/generate_start_menu_labels.py',
          'core/ui/font.cpp', 'assets/fonts/Gossip-MedPixel.otf', 'platform/3ds/ui_palette.h',
          'platform/3ds/ui_bounce.cpp', 'platform/3ds/ui_bounce.h', 'docs/bounce.md'] + SAMPLES


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def protected():
    return {str(p.relative_to(ROOT)):sha(p) for folder in ('core','platform','assets')
            for p in sorted((ROOT/folder).rglob('*')) if p.is_file()}


@lru_cache(None)
def glyph(text, size):
    face = ImageFont.truetype(str(ROOT/'assets/fonts/Gossip-MedPixel.otf'), size)
    im = Image.new('L', (640, 100))
    ImageDraw.Draw(im).text((4,70), text, font=face, fill=255, anchor='ls')
    im = im.point(lambda v:255 if v >= 128 else 0)
    box = im.getbbox()
    assert box, text
    mask = im.crop(box)
    if size == 28:
        assert mask.size == heading_mask(text).size and mask.tobytes() == heading_mask(text).tobytes()
    return mask, box[1]-70


class Page(Canvas):
    def __init__(self, width, theme):
        super().__init__(width, bg=(36,36,36))
        self.theme = theme
        self.c = COLORS[theme]
        self.hits = []
        if theme == 'pink':
            d = 239**3
            for y in range(240):
                n = y*y*(717-2*y)
                rgb = tuple((a*d-delta*n+d//2)//d for a,delta in ((228,17),(194,7),(207,1)))
                self.d.line((0,y,width-1,y), fill=rgb)

    def txt(self,x,y,text,role='muted',align='left'):
        if align == 'center': x -= len(text)*3
        self.text(x,y,text,self.c[role],right=align=='right')

    def word(self,x,baseline,text,size=28,role='ink',align='left'):
        assert text == text.lower()
        mask,dy = glyph(text,size)
        if align == 'center': x -= mask.width//2
        elif align == 'right': x -= mask.width
        y = baseline+dy
        box = (x,y,x+mask.width,y+mask.height)
        assert 0 <= x < box[2] <= self.im.width and 0 <= y < box[3] <= 240,(text,box)
        assert all(not overlaps(box,old) for old,_ in self.labels),(text,box,self.labels)
        self.labels.append((box,text))
        self.im.paste(self.c[role],(x,y),mask)
        return box

    def line(self,x0,y0,x1,y1,role='rule',width=1):
        assert 0 <= min(x0,x1) <= max(x0,x1) < self.im.width
        assert 0 <= min(y0,y1) <= max(y0,y1) < 240
        self.d.line((x0,y0,x1,y1),fill=self.c[role],width=width)

    def diamond(self,x,y,role='accent',filled=False):
        points = [(x,y-3),(x+2,y),(x,y+3),(x-2,y)]
        self.d.polygon(points,outline=self.c[role],fill=self.c[role] if filled else None)

    def hit(self,name,box):
        assert self.im.width == 320
        assert 0 <= box[0] < box[2] <= 320 and 0 <= box[1] < box[3] <= 240,(name,box)
        assert box[2]-box[0] >= 32 and box[3]-box[1] >= 24,(name,box)
        assert all(not overlaps(box,other) for _,other in self.hits),(name,box,self.hits)
        self.hits.append((name,box))

    def button(self,label,baseline,primary=False,key=None,small=False):
        self.word(40,baseline,label,22 if small else 28,'accent' if primary else 'muted' if small else 'ink')
        if primary: self.diamond(24,baseline-9,filled=True)
        if key: self.txt(296,baseline-11,key,'muted','right')
        self.hit(label,(16,baseline-25,304,baseline+11))


def illustrative_wave():
    """Real sample values, illustrative score. NOT engine bounce, no output WAV."""
    rate = 32000
    pcm = array('i',[0])*(rate*8)
    for kind,name in enumerate(SAMPLES):
        with wave.open(str(ROOT/name),'rb') as f:
            assert f.getsampwidth() == 2
            ch,sr,n = f.getnchannels(),f.getframerate(),f.getnframes()
            src = array('h',f.readframes(n))
            if sys.byteorder != 'little': src.byteswap()
        notes = range(16) if kind == 0 else range(1,16,2) if kind == 1 else range(32)
        step = .25 if kind == 2 else .5
        gain = (.60,.40,.22)[kind]
        for note in notes:
            start = round(note*step*rate)
            count = min(round(n*rate/sr),len(pcm)-start)
            for i in range(count):
                at = min(n-1,int(i*sr/rate))*ch
                value = sum(src[at:at+ch])/ch
                pcm[start+i] += int(value*gain)
    pcm = array('h',(max(-32768,min(32767,v)) for v in pcm))
    # Current UI's eight probes/column and fixed full-scale 34px amplitude.
    low,high = [],[]
    for x in range(352):
        a,b = x*len(pcm)//352,(x+1)*len(pcm)//352
        values = [pcm[min(len(pcm)-1,a+(b-a-1)*j//7)] for j in range(8)]
        low.append(int(min(values)*34/32768)); high.append(int(max(values)*34/32768))
    return low,high,hashlib.sha256(pcm.tobytes()).hexdigest()


def rail(p,y,progress=0.,bars=4,x0=24,x1=None,segmented=False):
    if x1 is None: x1=p.im.width-25
    length=x1-x0
    if segmented:
        for i in range(bars):
            a=x0+round(length*i/bars); b=x0+round(length*(i+1)/bars)-5
            p.line(a,y,b,y)
            fill=min(b,x0+round(length*progress))
            if fill>=a: p.line(a,y,fill,y,'accent',2)
    else:
        p.line(x0,y,x1,y)
        if progress: p.line(x0,y,x0+round(length*progress),y,'accent',2)
        for i in range(bars+1):
            x=x0+round(length*i/bars)
            p.line(x,y-3,x,y+3,'muted')
    if progress:
        p.diamond(x0+round(length*progress),y,'accent',filled=True)


def upper(variant,state,theme,waveform):
    p=Page(400,theme)
    p.word(24,39,'bounce')
    p.txt(376,25,'mix / stereo','muted','right')
    ribbon=variant=='02-ribbon'
    if state=='setup':
        p.txt(200,75,'record length','muted','center')
        p.word(200,139,'4' if ribbon else '8.00',56,'ink','center')
        p.txt(200,155,'bars / 8.00 seconds' if ribbon else 'seconds','muted','center')
        if ribbon: rail(p,183,segmented=True)
        else: p.txt(200,185,'4 bars / 120 bpm','muted','center')
        p.txt(200,213,'120 bpm / full mix' if ribbon else 'records the sound you hear','muted','center')
    elif state=='recording':
        p.txt(24,72,'recording','accent')
        p.txt(376,72,'bar 2 / 4','muted','right')
        p.word(200 if ribbon else 24,141,'2' if ribbon else '3.50',56,'accent','center' if ribbon else 'left')
        if ribbon: p.txt(200,156,'of 4 bars / 3.50s','muted','center')
        else: p.txt(376,132,'/ 8.00s','muted','right')
        rail(p,183,3.5/8,segmented=ribbon)
        p.txt(24,213,'mix / 4 bars','muted')
        p.txt(376,213,'stops automatically','muted','right')
    else:
        p.word(24,85,'take ready',28)
        p.txt(376,73,'8.00s','muted','right')
        p.line(24,137,375,137)
        low,high,_=waveform
        for x,(lo,hi) in enumerate(zip(low,high)):
            p.line(24+x,137-hi,24+x,137-lo,'ink')
        p.txt(24,186,'4 bars / 120 bpm')
        p.txt(376,186,'not saved','accent','right')
        p.txt(24,215,'0.00s');p.txt(376,215,'8.00s','muted','right')
    return p


def source_row(p,baseline,source=0):
    centers=(50,128,182,236,290)
    bounds=(20,99,155,209,263,310)
    for i,(x,label) in enumerate(zip(centers,('mix','1','2','3','4'))):
        p.word(x,baseline,label,28,'accent' if i==source else 'ink','center')
        if i==source: p.diamond(x,baseline+9,filled=True)
        p.hit('source '+label,(bounds[i],baseline-25,bounds[i+1],baseline+16))


def bar_row(p,baseline,bars=4,available=(True,True,True,True),ribbon=False):
    for i,(x,n) in enumerate(zip((50,123,196,269),(1,2,4,8))):
        role='rule' if not available[i] else 'accent' if n==bars else 'ink'
        p.word(x,baseline,str(n),40 if ribbon else 28,role,'center')
        if ribbon: p.line(x-24,baseline+12,x+24,baseline+12,role)
        elif n==bars: p.diamond(x,baseline+9,filled=True)
        p.hit(str(n)+' bars',(14+73*i,baseline-(36 if ribbon else 25),14+73*(i+1),baseline+18))


def lower(variant,state,theme,source=0,bars=4,available=(True,)*4,notice='',partial=False,waiting=False):
    p=Page(320,theme)
    ribbon=variant=='02-ribbon'
    if state=='setup':
        p.txt(24,23,'source')
        source_row(p,65,source)
        p.txt(24,97,'bars')
        bar_row(p,141 if ribbon else 138,bars,available,ribbon)
        if ribbon:
            p.word(160,203,'record',28,'accent','center')
            p.line(73,197,103,197,'accent');p.line(217,197,247,197,'accent')
            p.hit('record',(20,176,300,213))
        else: p.button('record',203,True,'a')
        if notice: p.txt(160,164,notice,'accent','center')
        p.txt(24,225,'a record' if ribbon else 'starts the song')
        p.txt(296,225,'b back','muted','right')
    elif state=='recording':
        if ribbon:
            p.txt(24,25,'mix / 4 bars');p.txt(296,25,'waiting' if waiting else 'recording','accent','right')
            for i,x in enumerate((50,123,196,269)):
                p.word(x,112,str(i+1),40,'muted' if waiting or i>1 else 'accent','center')
            rail(p,133,0 if waiting else 3.5/8,x0=26,x1=293,segmented=True)
            p.word(160,203,'cancel' if waiting else 'stop early',28,'accent','center')
            p.hit('cancel' if waiting else 'stop early',(20,176,300,213))
        else:
            p.word(24,61,'waiting' if waiting else 'recording',28,'accent')
            p.txt(24,78,'mix / 4 bars')
            p.line(24,109,47,109,'accent')
            p.txt(24,132,'starts next bar' if waiting else 'stops after 4 bars','ink')
            p.txt(24,152,'you can leave this page')
            p.button('cancel' if waiting else 'stop early',203,True,'a')
        p.txt(24,225,'a cancel' if waiting else 'a stop early')
        p.txt(296,225,'b back','muted','right')
    elif ribbon:
        p.word(160,68,'listen',28,'accent','center')
        p.diamond(160,82,filled=True)
        p.hit('listen',(20,36,300,95))
        p.word(79,132,'save wav',22,'ink','center')
        p.line(29,146,129,146)
        p.hit('save wav',(16,109,145,159))
        if not partial:
            p.word(237,132,'use in chain',22,'ink','center')
            p.line(174,146,299,146)
            p.hit('use in chain',(163,109,306,159))
        p.word(160,200,'record again',22,'muted','center')
        p.hit('record again',(20,177,300,213))
        p.txt(24,225,'a listen / x save');p.txt(296,225,'b back','muted','right')
    else:
        p.button('listen',60,True,'a')
        p.button('save wav',106,key='x')
        if not partial: p.button('use in chain',152,key='y')
        else: p.txt(40,142,'partial take / save only')
        p.button('record again',198,small=True)
        p.txt(296,225,'b back','muted','right')
    if notice and state!='setup':
        # Refusal notices require a separate state layout, not text on controls.
        raise ValueError('unsupported notice state')
    return p


def dual(top,bottom):
    im=Image.new('RGB',(400,496),OUTSIDE)
    im.paste(top.im,(0,0));im.paste(bottom.im,(40,256))
    assert im.crop((0,0,400,240)).tobytes()==top.im.tobytes()
    assert im.crop((40,256,360,496)).tobytes()==bottom.im.tobytes()
    return im


def board(theme,pages,full=False):
    # Rows are variants, columns the three stages; native LCDs stay 1:1 before 2x.
    cellw,cellh=(400,496) if full else (320,240)
    gap=24;rowpitch=cellh+68
    c=Canvas(gap*4+cellw*3,80+rowpitch*2+26,OUTSIDE)
    c.text(24,17,'bounce / two visual directions',COLORS['graphite']['ink'],scale=2)
    c.text(24,45,'01 folio: quiet type   /   02 ribbon: bar ruler',COLORS['graphite']['muted'])
    for row,variant in enumerate(VARIANTS):
        y=80+row*rowpitch
        for col,state in enumerate(STATES):
            x=gap+col*(cellw+gap)
            c.text(x,y,variant+' / '+state,COLORS['graphite']['accent'])
            top,bottom=pages[variant,state]
            c.im.paste(dual(top,bottom) if full else bottom.im,(x,y+20))
    c.text(24,c.im.height-20,'visual study / example loop / not implemented',COLORS['graphite']['muted'])
    return c.im


def save(im,name,meta):
    path=OUT/name
    assert not path.exists(),path
    result=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
    info=PngImagePlugin.PngInfo();info.add_text('description',__doc__)
    info.add_text('provenance',json.dumps(meta,sort_keys=True))
    result.save(path,pnginfo=info)
    with Image.open(path) as check:
        check.load()
        assert check.size==result.size and check.convert('RGB').tobytes()==result.tobytes()
        assert check.resize(im.size,Image.Resampling.NEAREST).tobytes()==im.tobytes()
    return dict(path=str(path.relative_to(ROOT)),size=list(result.size),bytes=path.stat().st_size,sha256=sha(path))


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    before=protected();sources={name:sha(ROOT/name) for name in INPUTS}
    waveform=illustrative_wave()
    rendered={};cases=0
    for theme in COLORS:
        pages={}
        for variant in VARIANTS:
            for state in STATES:
                pages[variant,state]=(upper(variant,state,theme,waveform),lower(variant,state,theme))
                cases+=2
            # Layout-only stress: all existing choices, unavailable lengths,
            # contextual chain warning, partial result and next-bar wait.
            for source in range(5):
                for bars in (1,2,4,8):
                    lower(variant,'setup',theme,source,bars,(True,True,False,False));cases+=1
            lower(variant,'setup',theme,1,4,notice='chain muted / records silence');cases+=1
            lower(variant,'take',theme,partial=True);cases+=1
            lower(variant,'recording',theme,waiting=True);cases+=1
        rendered[theme]=pages
    meta=dict(status='unselected visual proposal; no runtime or controller edits',
              created_at=datetime.now(timezone.utc).isoformat(),source_sha256=sources,
              rendering='authored Pillow native 400x240 / 320x240; existing static graphite/pink paper; no capture',
              fonts='original Gossip Med Pixel at 22/28/40/56 ppem and native 6x8; binary glyph masks',
              waveform='illustrative 8s / 4 bars / 120bpm score from local CC0 samples, constructed in memory; not engine output',
              pcm_sha256=waveform[2],audio_export=False,
              limitations='no input, animation, hardware, DSP or runtime test; send/discard modals not redesigned yet')
    outputs=[]
    for theme,pages in rendered.items():
        outputs.append(save(board(theme,pages),f'bounce-two-{theme}-lower-2x.png',meta))
    outputs.append(save(board('graphite',rendered['graphite'],True),'bounce-two-dual-2x.png',meta))
    for state in STATES:
        top,bottom=rendered['graphite']['01-folio',state]
        outputs.append(save(dual(top,bottom),f'01-folio-{state}-dual-2x.png',meta))
    assert before==protected(),'production/assets changed'
    assert sources=={name:sha(ROOT/name) for name in INPUTS}
    report=dict(meta,outputs=outputs,layout_checks=dict(surface_cases=cases,text_bounds_and_no_overlap=True,
                    proposed_targets_bounds_and_no_overlap=True,minimum_target=[32,24],
                    native_glyph_masks=True,nn2x_decode=True,unchanged_production_and_assets=len(before)),
                layouts={variant:{state:dict(labels=bottom.labels,proposed_targets=bottom.hits)
                                  for (v,state),(_,bottom) in rendered['graphite'].items() if v==variant}
                         for variant in VARIANTS})
    (OUT/'proposal.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    for item in outputs:print(item['path'],item['size'],item['bytes'],item['sha256'])
    print('pass:',cases,'native layout cases;',len(before),'production/assets unchanged; no app build, audio export or deployment')


if __name__=='__main__':main()
