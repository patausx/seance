#!/usr/bin/env python3
"""Non-shipping two-screen geometry concepts, not DSP output or hardware captures."""
from pathlib import Path
import hashlib
import json
import math
import re
from PIL import Image, ImageDraw, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/module_geometry_study'
PAPER = (226, 190, 206)
HI = (242, 216, 226)
FAINT = (207, 165, 184)
MUTED = (174, 130, 151)
INK = (31, 20, 31)
SPOT = (91, 49, 124)
OUTSIDE = (24, 20, 29)
PALETTE = {PAPER, HI, FAINT, MUTED, INK, SPOT, OUTSIDE}


def load_font():
    src = (ROOT / 'core/ui/font.cpp').read_text().split('font_6x8[96][8] = {', 1)[1].split('};', 1)[0]
    glyphs = []
    for body in re.findall(r'\{([^{}]*)\}', src):
        rows = []
        for macro, zero in re.findall(r'R\(([^)]*)\)|(\b0\b)', re.sub(r'//.*', '', body)):
            bits = 0
            for bit in macro.split(',') if macro else []:
                bits = (bits << 1) | int(bit.strip())
            rows.append(bits)
        if len(rows) == 8:
            glyphs.append(rows)
    assert len(glyphs) == 96
    return glyphs


FONT = load_font()


class Canvas:
    def __init__(self, width, height=240, bg=PAPER):
        self.im = Image.new('RGB', (width, height), bg)
        self.d = ImageDraw.Draw(self.im)
        self.labels = []

    def text(self, x, y, s, color=INK, scale=1, right=False):
        assert s == s.lower(), s
        if right:
            x -= len(s) * 6 * scale
        box = (x, y, x + len(s) * 6 * scale, y + 8 * scale)
        assert 0 <= box[0] < box[2] <= self.im.width, (s, box)
        assert 0 <= box[1] < box[3] <= self.im.height, (s, box)
        for old, label in self.labels:
            assert min(box[2], old[2]) <= max(box[0], old[0]) or min(box[3], old[3]) <= max(box[1], old[1]), (s, label)
        self.labels.append((box, s))
        for ch in s:
            assert 32 <= ord(ch) < 128
            for row, bits in enumerate(FONT[ord(ch)-32]):
                for col in range(6):
                    if bits & (1 << (5-col)):
                        self.d.rectangle((x+col*scale, y+row*scale, x+(col+1)*scale-1, y+(row+1)*scale-1), fill=color)
            x += 6*scale


def hatch(c, box, color=INK, spacing=7, slope=1, phase=0, shape='ellipse', solid=False):
    mask = Image.new('L', c.im.size, 0)
    md = ImageDraw.Draw(mask)
    if shape == 'ellipse':
        md.ellipse(box, fill=255)
    elif shape == 'round':
        md.rounded_rectangle(box, radius=8, fill=255)
    else:
        md.polygon(box, fill=255)
    if solid:
        c.im.paste(color, (0, 0), mask)
        return
    lines = Image.new('L', c.im.size, 0)
    ld = ImageDraw.Draw(lines)
    for k in range(-600+phase, 800, spacing):
        ld.line((k, 0, k+slope*c.im.height, c.im.height), fill=255)
    c.im.paste(color, (0, 0), ImageChops.multiply(mask, lines))


DATA = {
    'fm': dict(path='fm / ghost / out', status='drone', title='two operators', selected=2,
               primary='index', value='42%', route='sg.a  +12%', summary='ratio 2.00x   fine +0.003x',
               params=[('pitch','c2',.25),('ratio','2.00x',.4),('index','42%',.42),('feedback','36%',.36),('fine','+.003x',.51),('gain','67%',.67)],
               pages=('tone','shape'), actions=('strike','drone','mod / temp'), active=1),
    'ghost': dict(path='play / ghost / out', status='memory', title='four resonances', selected=2,
                  primary='smear', value='64%', route='sg.b  +08%', summary='root 110hz   drift 18%',
                  params=[('root','110hz',.68),('decay','87%',.87),('smear','64%',.64),('drift','18%',.18),('freeze','off',0),('mix','58%',.58)],
                  pages=('memory',''), actions=('freeze','bypass','mod / temp'), active=-1),
    'strn': dict(path='mic / strn / out', status='frozen', title='held material', selected=0,
                 primary='position', value='38%', route='sg.a  +06%', summary='8 voices max   long window',
                 params=[('position','38%',.38),('size','720ms',.7),('density','14%',.14),('pitch','+0.03st',.501),('spray','04%',.04),('mix','100%',1)],
                 pages=('cloud','motion'), actions=('freeze','long','mod / temp'), active=0),
    'rev': dict(path='fm / rev / out', status='tail', title='three delay lines', selected=2,
                primary='damp', value='76%', route='sg.c  +09%', summary='size 62%   decay 84%',
                params=[('size','62%',.62),('decay','84%',.84),('damp','76%',.76),('mix','31%',.31)],
                pages=('space',''), actions=('freeze','bypass','mod / temp'), active=-1),
}


def figure(c, module, phase=0., treatment='hatch'):
    """Parameter/process metaphor only: animation is illustrative, not measured audio."""
    solid = treatment == 'solid'
    if module == 'fm':
        offset = round(7*math.sin(phase*math.tau))
        # Two clearly bounded operator discs. Overlap produces an interference field.
        hatch(c, (102,57,238,193), INK, 7, 1, solid=solid)
        hatch(c, (169+offset,57,305+offset,193), SPOT, 7, -1, solid=solid)
        if solid:
            c.d.ellipse((172+offset,83,223+offset,168), fill=HI)
        c.d.line((96,200,310,200), fill=FAINT)
        c.d.line((102,198,102,202), fill=INK)
        c.d.line((305+offset,198,305+offset,202), fill=SPOT)
    elif module == 'ghost':
        # Four wells, not a made-up frequency spectrum.
        for i, (cx, ry) in enumerate(((127,38),(176,57),(225,66),(274,46))):
            dx = round(3*math.sin(phase*math.tau+i))
            hatch(c, (cx-30+dx,128-ry,cx+30+dx,128+ry), SPOT if i==2 else INK, 7, 1 if i%2 else -1, solid=solid)
            c.d.line((cx,199,cx,202), fill=SPOT if i==2 else MUTED)
    elif module == 'strn':
        # Fixed captured region; overlapping windows above it, no fake waveform.
        c.d.rectangle((91,106,315,154), fill=FAINT)
        c.d.line((91,98,91,164), fill=INK)
        c.d.line((315,98,315,164), fill=INK)
        for i, (x,w,h) in enumerate(((112,56,64),(148,69,110),(196,63,91),(246,51,57))):
            dx = round(4*math.sin(phase*math.tau+i*.6))
            hatch(c, (x+dx,130-h//2,x+w+dx,130+h//2), SPOT if i==1 else INK, 6, 1, solid=solid)
        c.d.line((178,184,218,184), fill=SPOT, width=2)
        c.d.line((178,180,178,188), fill=SPOT)
    else:
        # Three receding chambers echo the existing three-loop reverb architecture.
        boxes = [(106,66,272,188),(139,83,295,177),(174,102,315,164)]
        for i, box in enumerate(boxes):
            hatch(c, box, (INK, MUTED, SPOT)[i], 7, 1, shape='round', solid=solid)
            c.d.rounded_rectangle(box, radius=8, outline=(INK, MUTED, SPOT)[i])
        c.d.line((106,200,315,200), fill=FAINT)
        for x in (106,174,244,315):
            c.d.line((x,198,x,202), fill=MUTED)


def top(module, phase=0., treatment='hatch'):
    s = DATA[module]
    c = Canvas(400)
    c.text(12,11,'01 / '+module)
    c.text(388,11,'120.0  >', right=True)
    c.d.line((12,28,388,28), fill=MUTED)
    c.text(12,40,s['status'],SPOT)
    c.text(388,40,s['path'],MUTED,right=True)
    figure(c,module,phase,treatment)
    c.text(12,211,s['primary'],SPOT)
    c.text(94,211,s['value'])
    c.text(388,211,s['route'],SPOT,right=True)
    c.d.line((150,215,272,215), fill=MUTED)
    c.d.line((183,215,229,215), fill=SPOT,width=2)
    c.d.line((207,211,207,219), fill=INK)
    c.d.rectangle((223,213,226,216),fill=SPOT)
    c.text(12,229,s['summary'],MUTED)
    c.text(388,229,'01 / 04',MUTED,right=True)
    return c


def bottom(module):
    s = DATA[module]
    c = Canvas(320)
    c.text(12,13,s['pages'][0],SPOT)
    if s['pages'][1]:
        c.text(86,13,s['pages'][1],MUTED)
    c.text(308,13,'1 / '+('2' if s['pages'][1] else '1'),MUTED,right=True)
    c.d.line((12,29,308,29),fill=MUTED)
    c.d.line((12,29,48,29),fill=SPOT,width=2)
    rows = 3 if len(s['params'])==6 else 2
    row_h = 49 if rows==3 else 73
    boxes=[]
    for i,(name,value,amount) in enumerate(s['params']):
        x=12+(i%2)*154
        y=40+(i//2)*row_h
        chosen=i==s['selected']
        color=SPOT if chosen else INK
        boxes.append((x-4,y-4,x+142,y+row_h-5))
        if chosen:
            c.d.rectangle((x-4,y-4,x+142,y+row_h-5),fill=HI)
            c.d.rectangle((x-4,y-4,x-3,y+row_h-5),fill=SPOT)
        c.text(x+4,y,name,color)
        # Fine value still at 2x font: 7 chars fit into the 146px touch band.
        c.text(x+4,y+14,value,color,scale=2)
        rail_y=y+row_h-9
        c.d.line((x+4,rail_y,x+133,rail_y),fill=MUTED)
        p=x+4+round(129*amount)
        c.d.line((x+4,rail_y,p,rail_y),fill=color)
        c.d.rectangle((p-1,rail_y-2,p+1,rail_y+2),fill=color)
    for i,a in enumerate(boxes):
        assert a[2]-a[0]>=140 and a[3]-a[1]>=45
        for b in boxes[i+1:]:
            assert min(a[2],b[2])<=max(a[0],b[0]) or min(a[3],b[3])<=max(a[1],b[1])
    for i, action in enumerate(s['actions']):
        x=(12,95,223)[i]
        active=i==s['active']
        if active:
            c.d.rectangle((x-4,195,x+len(action)*6+4,216),fill=SPOT)
        c.text(x,202,action,HI if active else SPOT if i==2 else INK)
    c.d.line((12,223,308,223),fill=MUTED)
    c.text(12,231,'drag value',MUTED)
    c.text(308,231,'b back',MUTED,right=True)
    c.touch_boxes=boxes
    return c


def pair(t,b):
    im=Image.new('RGB',(400,496),OUTSIDE)
    im.paste(t,(0,0))
    im.paste(b,(40,256))
    return im


def save(im,name):
    colors = im.getcolors(maxcolors=len(PALETTE))
    assert colors is not None and {color for _, color in colors} <= PALETTE, name
    im.save(OUT/(name+'.png'))
    doubled=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
    doubled.save(OUT/(name+'_2x.png'))
    assert ImageChops.difference(doubled.resize(im.size,Image.Resampling.NEAREST),im).getbbox() is None


def protected_hashes():
    paths=[]
    for folder in ('core','platform','assets'):
        paths += [p for p in (ROOT/folder).rglob('*') if p.is_file()]
    paths += [ROOT/('seance.'+ext) for ext in ('3dsx','cia','smdh')]
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.exists()}


def main():
    before=protected_hashes()
    OUT.mkdir(parents=True,exist_ok=True)
    sheet=Image.new('RGB',(824,1052),OUTSIDE)
    report={'status':'concept only; illustrative state; no DSP implementation or runtime changes',
            'native':{'top':[400,240],'bottom':[320,240]},'modules':{},
            'reference':'user image_59b10f10.png; geometric/hatching direction, not copied artwork',
            'limitations':['FM drone is proposed, not implemented.',
                            'No spectrum, per-resonator energy or real grain telemetry is claimed.',
                            'Figures are process metaphors, not audio measurements.',
                            'Fine readouts and bottom layout are UI proposals.',
                            'Old 3DS GPU budget and readability not tested on hardware.']}
    for n,module in enumerate(DATA):
        t,b=top(module),bottom(module)
        dual=pair(t.im,b.im)
        save(t.im,module+'_top'); save(b.im,module+'_bottom'); save(dual,module+'_dual')
        x=8+(n%2)*408; y=24+(n//2)*520
        sheet.paste(dual,(x,y))
        report['modules'][module]={'touch_boxes':b.touch_boxes,'top_labels':len(t.labels),'bottom_labels':len(b.labels)}
    save(sheet,'all_modules')
    # One controlled alternative: identical geography, solid shapes rather than hatching.
    solid=top('fm',treatment='solid')
    save(pair(solid.im,bottom('fm').im),'fm_solid_dual')
    comparison=Image.new('RGB',(816,240),OUTSIDE)
    comparison.paste(top('fm').im,(0,0));comparison.paste(solid.im,(416,0))
    save(comparison,'fm_hatch_vs_solid')
    frames=[pair(top('fm',n/48).im,bottom('fm').im).resize((800,992),Image.Resampling.NEAREST) for n in range(48)]
    frames[0].save(OUT/'fm_motion_2x.gif',save_all=True,append_images=frames[1:],duration=100,loop=0,disposal=2)
    assert before==protected_hashes(), 'protected source/artifact changed'
    report['checks']={'font_glyphs':96,'exact_palette':True,'text_bounds_and_overlap':True,
                      'touch_bounds_and_overlap':True,'nearest_2x':True,'protected_files_unchanged':len(before)}
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'README.txt').write_text(
        'preview only: native 400x240 top / 320x240 touch; integer nearest 2x.\n'
        'top: simple process geometry, serial module path, one selected parameter/mod rail.\n'
        'bottom: 4-6 big touch areas, two columns, no draggable diagram.\n'
        'figures: fm two interfering discs; ghost four resonant wells; strn overlapping\n'
        'windows inside held material; rev three receding chambers. Not spectra or PCM.\n'
        'FM drone and fine-value UI are proposals, not implemented features.\n'
        'animation is a slow illustrative gesture loop, not actual operator phase.\n'
        'strn long/size and fractional pitch exist in DSP; regular grain scheduling is\n'
        'not implemented here. Ghost smear and rev damp keep their existing meanings.\n'
        'secondary pages retain other controls; concept actions are not runtime bindings.\n'
        'no wordmark, splash, sequencer, font asset, engine or installed binary changes.\n'
        'reproduce: python3 tools/render_module_geometry_study.py\n')
    print(OUT/'all_modules_2x.png')
    print(json.dumps(report['checks']))


if __name__=='__main__':
    main()
