#!/usr/bin/env python3
"""Noninteractive upper-screen proposal, with source-derived lower instruments.

No new UI controller, build, font asset, engine data or deployment. Captured FM
and sequencer frames are saved host fixtures, NOT current device telemetry.
Seance Menu / Seance Note bitmap derivatives: Deborah Khodanovich 2025,
SIL OFL 1.1; original Gossip OTF untouched. See assets/fonts/OFL.txt and README.md.
"""
from pathlib import Path
import csv
import hashlib
import json
import re
from PIL import Image, ImageChops
from render_menu_style_study import Page, paper, heading_mask, chains, overlaps, GHOST, BRANCH
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair
from generate_start_menu_labels import masks

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/upper_menu_study'
FM = ROOT / 'ui_mockups/fm_drone_runtime/drone_0.png'
SEQ = ROOT / 'ui_mockups/sequencer_companion/1_note.png'
SEQ_TOP = ROOT / 'ui_mockups/sequencer_companion/1_note_top.png'
OLD_PAPER, OLD_GHOST, OLD_BRANCH, HI = (226,190,206), (174,130,151), (207,165,184), (242,216,226)
COLORS = {INK: INK, SPOT: SPOT, OLD_GHOST: GHOST, OLD_BRANCH: BRANCH, HI: HI}
PROOFS = {}


def reserve(c, box, name):
    assert 0 <= box[0] < box[2] <= c.im.width and 0 <= box[1] < box[3] <= 240, (name, box)
    for old, label in c.labels:
        assert not overlaps(box, old), (name, label, box, old)
    c.labels.append((box, name))


def restored_paper(c, box):
    c.im.paste(paper(c.im.width).crop(box), box[:2])


def source_ink(c, source, box, at=None, proof=None):
    """Recolor non-paper pixels; no resampling or reconstructed instrument geometry."""
    if at is None:
        at = box[:2]
    crop = source.crop(box)
    under = paper(c.im.width)
    for y in range(crop.height):
        for x in range(crop.width):
            px = crop.getpixel((x,y))
            assert px == OLD_PAPER or px in COLORS, px
            target = (at[0]+x, at[1]+y)
            c.im.putpixel(target, under.getpixel(target) if px == OLD_PAPER else COLORS[px])
    if proof:
        # Independent foreground mask and color comparison, including empty holes.
        for y in range(crop.height):
            for x in range(crop.width):
                px = crop.getpixel((x,y))
                xy = (at[0]+x, at[1]+y)
                assert c.im.getpixel(xy) == (under.getpixel(xy) if px == OLD_PAPER else COLORS[px])
        PROOFS[proof] = {'source_rect': box, 'destination': at, 'exact_ink_geometry': True}


def glyphs():
    text = (ROOT / 'platform/3ds/note_display_data.h').read_text()
    data = bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})', text.split('bitmap[] = {')[1].split('};')[0]))
    result = {}
    for ch, small, offset, w, h, top in re.findall(r"\{'(.)', (true|false), (\d+), (\d+), (\d+), (-?\d+)\}", text):
        offset,w,h,top = map(int,(offset,w,h,top))
        mask = Image.new('L',(w,h))
        for y in range(h):
            for x in range(w):
                mask.putpixel((x,y),255 if data[offset+y*((w+7)//8)+x//8] & (128>>(x&7)) else 0)
        result[(ch,small=='true')] = (mask,top)
    assert len(result)==28
    return result


GLYPHS = glyphs()


def number(c, x, baseline, value, scale=2):
    """Exact installed digits; a separate ordinary dot, never missing font glyphs."""
    for ch in value:
        if ch == '.':
            box = (x,baseline-6,x+6,baseline)
            reserve(c,box,'decimal point')
            c.d.rectangle((x,baseline-6,x+5,baseline-1),fill=INK)
            x += 12
        else:
            mask,top = GLYPHS[(ch,False)]
            enlarged = mask.resize((mask.width*scale,mask.height*scale),Image.Resampling.NEAREST)
            assert enlarged.resize(mask.size,Image.Resampling.NEAREST).tobytes()==mask.tobytes()
            y=baseline+top*scale
            reserve(c,(x,y,x+enlarged.width,y+enlarged.height),'hero '+ch)
            c.im.paste(INK,(x,y),enlarged)
            x += enlarged.width+6
    return x


def header(c, right, route, selected):
    c.text(24,16,'patch01',GHOST)
    c.text(376,16,right,GHOST,right=True)
    x=24
    for i,name in enumerate(route):
        c.text(x,38,name,SPOT if i==selected else INK)
        if i==selected:
            c.line((x,50,x+len(name)*6-2,50),SPOT)
        x+=len(name)*6
        if i<len(route)-1:
            c.text(x+8,38,'/',GHOST)
            x+=26


def chains_top():
    c=Page(400)
    header(c,'chain 01',('fm','strn','ghost','rev','out'),1)
    c.heading(24,85,'strn',SPOT)
    c.text(24,120,'live grain processor',INK)
    c.text(24,142,'selected / slot 02',GHOST)
    c.text(264,89,'chain',GHOST)
    c.text(264,110,'4 / 6 modules',INK)
    c.line((248,93,248,148,257,148))
    c.text(264,143,'2 free slots',GHOST)
    c.text(24,179,'from',GHOST)
    c.text(73,179,'fm',INK)
    c.text(130,179,'into',GHOST)
    c.text(178,179,'ghost',INK)
    c.footer('tap module / actions','select / go to')
    return c


def fm_top():
    c=Page(400)
    header(c,'chain 01 / drone',('fm','out'),0)
    c.text(24,62,'index / base',SPOT)
    number(c,24,149,'0.85')
    # Mirrors the original FM display mapping (2 * raw / 32767), not percent,
    # Hz, or an assumed conventional FM deviation/carrier ratio.
    c.text(24,170,'display scale 0.00 - 2.00',GHOST)
    c.text(24,193,'stored value',GHOST)
    c.text(252,76,'live',GHOST)
    c.text(252,94,'0.85',SPOT,scale=2)
    c.line((238,80,238,150,245,150))
    c.text(252,140,'modulation',GHOST)
    c.text(252,158,'off',INK)
    c.text(252,180,'no source assigned',GHOST)
    c.footer('drone / sustained','index selected')
    return c


def fm_bottom():
    c=Page()
    source=Image.open(FM).convert('RGB')
    # Keep ratio/pitch rails, two operators, feedback orbit, signal tail, all
    # six main controls, A-D strip and local modulation route. Only chrome changes.
    source_ink(c,source,(0,22,320,228),proof='fm entire working body')
    reserve(c,(0,22,320,228),'preserved fm body')
    c.text(14,6,'fm',INK)
    c.text(52,6,'ch01  01',GHOST)
    c.text(141,6,'drone',SPOT)
    c.line((141,18,170,18),SPOT)
    c.text(205,6,'byp',GHOST)
    c.text(246,6,'i',SPOT)
    c.text(258,6,'ii',GHOST)
    c.line((244,18,252,18),SPOT)
    c.text(284,6,'x del',GHOST)
    c.text(12,231,'b back',GHOST)
    c.text(308,231,'mod / temp >',SPOT,right=True)
    c.hit('strike / drone',(126,0,188,22))
    c.hit('bypass',(192,0,235,22))
    c.hit('pages i / ii',(240,0,277,22))
    c.hit('mod / temp',(210,228,320,240))
    # Narrow assertion on the number explicitly used by the upper fixture.
    label=Canvas(320)
    label.text(112,170,'index 0.85',SPOT)
    for y in range(170,178):
        for x in range(112,172):
            if label.im.getpixel((x,y))==SPOT:
                assert source.getpixel((x,y))==SPOT, 'saved FM fixture has a different index'
    return c


def sequence_top():
    c=Page(400)
    header(c,'120 bpm / 1.1 / stop',('fm','out'),0)
    c.text(376,38,'chain 01',GHOST,right=True)
    c.text(24,60,'note',SPOT)
    source=Image.open(SEQ_TOP).convert('RGB')
    # Keep the exact approved a#2 including authored # and small octave.
    # Validate the full crop independently against shipped masks and authored #.
    expected=Image.new('RGB',(400,240),OLD_PAPER)
    a,top=GLYPHS[('a',False)]
    octave,octave_top=GLYPHS[('2',True)]
    expected.paste(INK,(10,96+top),a)
    expected.paste(SPOT,(10+a.width+19+6,99+octave_top),octave)
    from PIL import ImageDraw
    d=ImageDraw.Draw(expected)
    sx=10+a.width+5
    for x,y,w,h in ((sx+4,60,2,18),(sx+10,60,2,18),(sx,64,14,2),(sx,72,14,2)):
        d.rectangle((x,y,x+w-1,y+h-1),fill=SPOT)
    assert expected.crop((10,58,65,103)).tobytes()==source.crop((10,58,65,103)).tobytes()
    source_ink(c,source,(10,58,65,103),(24,78),proof='sequencer a#2 hero')
    reserve(c,(24,78,79,123),'original a#2 hero')
    c.text(24,136,'midi 46',GHOST)
    for y,text in zip((76,92,108,124,140),('01/64 on','lev 18 x1','all','-23/96','locks 2/4')):
        c.text(112,y,text,SPOT if y==76 else GHOST)
    # Same stored-step profile, not a new decorative trace or a glide prediction.
    c.text(208,64,'pitch',INK)
    c.text(390,64,'01-16',GHOST,right=True)
    source_ink(c,source,(208,64,392,143),(208,78),proof='sequencer stored pitch profile')
    reserve(c,(208,78,392,157),'original sixteen-step pitch profile')
    c.text(24,178,'level > fm / gain',GHOST)
    c.text(24,200,'strike / reverse',GHOST)
    c.text(208,170,'dot lock / pale base',GHOST)
    c.text(208,186,'hollow = conditional',GHOST)
    c.text(208,202,'one step / then base',GHOST)
    c.text(24,228,'edit note to hear',GHOST)
    c.text(376,228,'l value / page',GHOST,right=True)
    # Prevent invented readouts or a mismatch with the saved lower step.
    values={row['text'] for row in csv.DictReader((ROOT/'ui_mockups/sequencer_companion/1_note_top_text.tsv').open(),delimiter='\t')}
    assert {'01/64 on','lev 18 x1','all','-23/96','locks 2/4','midi 46','strike / reverse'} <= values
    return c


def sequence_bottom():
    c=Page()
    source=Image.open(SEQ).convert('RGB')
    source_ink(c,source,(0,0,320,240),proof='sequencer lower before chrome removal')
    # A rule above the editor is chrome, unlike the cut-square faces/lock marks.
    restored_paper(c,(0,194,320,197))
    restored_paper(c,(0,227,320,240))
    for row in csv.DictReader((ROOT/'ui_mockups/sequencer_companion/1_note_text.tsv').open(),delimiter='\t'):
        x,y,w,h=(int(row[k]) for k in ('x','y','w','h'))
        if y<227:
            reserve(c,(x,y,x+w,y+h),row['text'])
    c.text(8,231,'dpad move / l value',GHOST)
    c.text(312,231,'r mode / hold',GHOST,right=True)
    for lane,y in enumerate((21,67,113,159)):
        for i in range(16):
            x=8+19*i
            c.hit(f'lane {lane+1} step {i+1}',(x,y,x+17,y+29))
            expected=Page()
            box=(x,y,x+17,y+29)
            source_ink(expected,source,box)
            assert c.im.crop(box).tobytes()==expected.im.crop(box).tobytes()
    PROOFS['all64 cut-square cells and note labels']={'same_position':True,'exact_after_palette_mapping':True,'touch_band_height':29}
    return c


def protected():
    paths=['platform/3ds/ui_top.cpp','platform/3ds/ui_sequencer.cpp','platform/3ds/ui_module_focus_generators.cpp',
           'platform/3ds/ui_start_menu.cpp','platform/3ds/ui_state.h','platform/3ds/note_display_data.h',
           'core/engine/units.cpp','core/engine/engine.cpp','seance.3dsx','seance.cia',
           'tools/render_menu_style_study.py','docs/ui-direction.md','assets/fonts/Gossip-MedPixel.otf',
           'ui_mockups/menu_style_study/comparison_2x.png','ui_mockups/menu_style_study/chains_native.png',
           str(FM.relative_to(ROOT)),str(SEQ.relative_to(ROOT)),str(SEQ_TOP.relative_to(ROOT))]
    return {p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths}


def save(im,name):
    allowed=set(paper(400).get_flattened_data())|{INK,SPOT,GHOST,BRANCH,HI,OUTSIDE}
    assert set(im.get_flattened_data())<=allowed,name
    im.save(OUT/f'{name}_native.png')
    big=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
    assert big.resize(im.size,Image.Resampling.NEAREST).tobytes()==im.tobytes()
    big.save(OUT/f'{name}_2x.png')


def main():
    before=protected()
    OUT.mkdir(parents=True,exist_ok=True)
    for text,mask in masks():
        assert ImageChops.difference(mask,heading_mask(text)).getbbox() is None
    frames={'chains':(chains_top(),chains()),'fm':(fm_top(),fm_bottom()),'sequencer':(sequence_top(),sequence_bottom())}
    assert frames['chains'][1].im.tobytes()==Image.open(ROOT/'ui_mockups/menu_style_study/chains_native.png').convert('RGB').tobytes()
    sheet=Image.new('RGB',(1232,544),OUTSIDE)
    titles=Canvas(1232,24,OUTSIDE)
    captions=Canvas(1232,24,OUTSIDE)
    for i,(name,(t,b)) in enumerate(frames.items()):
        save(t.im,name+'_top');save(b.im,name+'_bottom')
        dual=pair(t.im,b.im);save(dual,name+'_dual')
        sheet.paste(dual,(i*416,24))
        titles.text(i*416+14,8,('01 / chains','02 / fm editor','03 / sequencer')[i],(228,194,207))
        captions.text(i*416+14,8,('selected module / signal path','base / live / modulation','note / step / stored pitch')[i],(228,194,207))
    sheet.paste(titles.im,(0,0));sheet.paste(captions.im,(0,520))
    save(sheet,'comparison')
    upper=Image.new('RGB',(1232,264),OUTSIDE)
    upper.paste(titles.im,(0,0))
    for i,(t,_) in enumerate(frames.values()):upper.paste(t.im,(i*416,24))
    save(upper,'upper_comparison')
    ref=Image.open(ROOT/'ui_mockups/start_menu_runtime/expanded_bottom_native.png').convert('RGB')
    for t,b in frames.values():
        for image in (t.im,b.im):
            assert image.crop((0,0,2,240)).tobytes()==ref.crop((0,0,2,240)).tobytes()
    assert before==protected(),'source / prior study changed'
    report={'status':'static design proposals; NOT runtime screens or hardware captures',
        'pairs':list(frames),
        'sources':before,
        'data':{'chains':'illustrative 4-module FM/STRN/Ghost/REV chain; two free slots',
                'fm':'saved real C++ drone fixture; selected index raw13926 -> display0.85; base/live equal; no assigned source',
                'sequencer':'saved C++ dense fixture; a#2/midi46, step01/64, gain18, x1, all, time-23/96, locks2/4; stopped reverse'},
        'preservation':PROOFS,
        'checks':{'menu_heading_masks_exact':True,'runtime_gradient_strip_exact':True,
                  'native_palette_nearest2x':True,'registered_text_bounds_no_overlap':True,
                  'declared_hit_bounds_no_overlap':True,'chains_lower_identical_to_approved_study':True,
                  'unchanged_protected_files':len(before)},
        'scope':['chains selection overview; editor selected-parameter base/live/mod context; sequencer actual stored-step context',
                 'no fabricated PCM/spectrum/source trace; FM mod off has no fake curve',
                 'FM index uses existing 0..2 display mapping, not percent or newly claimed physical units',
                 'FM two-operator graphic and all main controls stay in native positions below; page ii/locks/TEMP not removed',
                 'sequencer cells/29px hit bands and glyphs preserved, not remade from synthetic notes',
                 'seven-spoke chooser and other modes are untouched, not depicted',
                 'modulated states should show actual source/depth/base/live on implementation, not hardcoded examples'],
        'limitations':['static snapshots; no input controller/GPU budget/device readability verification',
                       'captured host FM body is preserved as source ink; its internal label layout is not re-audited here',
                       'FM main is shown, page ii not redesigned; sigils/perform/lock-mode top not designed in this pass',
                       'no build or upload; design approval is not deployment']}
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'README.txt').write_text(__doc__+'\nrun: python3 tools/render_upper_menu_study.py\n'
        'comparison_2x.png: three native-derived duals. *_dual_2x.png: separate pairs.\n'
        'upper_comparison_2x.png: upper screens only. sources/hashes/limits in report.json.\n'
        'FM body / sequencer ink use real saved host fixtures recolored onto menu paper.\n'
        'All upper layouts are proposals, not installed C++ renders. No runtime edits.\n')
    print(OUT/'comparison_2x.png')
    print('pass: menu paper/headings; exact approved chains; FM body, a#2, pitch profile and64 cells; label/hit bounds; 2x; protected hashes')


if __name__=='__main__':
    main()
