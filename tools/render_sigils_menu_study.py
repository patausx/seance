#!/usr/bin/env python3
"""Static Sigils proposal, not a runtime render; no device or DSP changes.

Menu heading study derivative / Seance Note: Deborah Khodanovich 2025,
SIL OFL 1.1; see assets/fonts/README.md and OFL.txt. Original font unchanged.
"""
from pathlib import Path
import hashlib
import json
import re
from PIL import Image, ImageDraw
from render_menu_style_study import Page, paper, GHOST, BRANCH, overlaps
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair, protected_hashes

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/sigils_menu_study'
SOURCE = ROOT / 'hardware_shots/sigils_review_20260907_002440'


def bead(c, x, y, radius=3, color=SPOT, hollow=False):
    c.d.ellipse((x-radius,y-radius,x+radius,y+radius), outline=color,
                fill=None if hollow else color)


def glyphs():
    source = (ROOT/'platform/3ds/note_display_data.h').read_text()
    data = bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})', source.split('bitmap[] = {')[1].split('};')[0]))
    result = {}
    for ch, small, offset, w, h, top in re.findall(r"\{'(.)', (true|false), (\d+), (\d+), (\d+), (-?\d+)\}", source):
        if small == 'true' or not ch.isdigit(): continue
        offset,w,h,top = map(int,(offset,w,h,top))
        mask = Image.new('L',(w,h))
        for y in range(h):
            for x in range(w):
                mask.putpixel((x,y),255 if data[offset+y*((w+7)//8)+x//8] & (128>>(x&7)) else 0)
        result[ch] = mask,top
    assert len(result) == 10
    return result


DIGITS = glyphs()


def number(c, x, baseline, text):
    start=x
    for ch in text:
        if ch in '+-':
            c.d.rectangle((x,baseline-14,x+12,baseline-13),fill=SPOT)
            if ch == '+':c.d.rectangle((x+6,baseline-20,x+7,baseline-8),fill=SPOT)
            x+=18
        else:
            mask,top=DIGITS[ch]
            c.im.paste(INK,(x,baseline+top),mask)
            x+=mask.width+2
    box=(start,baseline-44,x,baseline+4)
    assert all(not overlaps(box,old) for old,_ in c.labels)
    c.labels.append((box,'output '+text))
    return x


def captured_curve():
    """Extract only violet curve ink from the actual 029 lower screenshot.

    This is a raster tracing, not original Q15 table data. Marker thickness is
    collapsed to its median. No claim of exact DSP reconstruction.
    """
    image=Image.open(SOURCE/'shot_029_bot.png').convert('RGB')
    values=[]
    for x in range(31,288):
        ys=[y for y in range(49,139) if image.getpixel((x,y))==SPOT]
        if ys:values.append((x,sorted(ys)[len(ys)//2]))
    assert len(values)>240
    return values


CURVE=captured_curve()
# All dynamic data below are internally consistent DESIGN fixtures, NOT device
# telemetry. Only curve silhouette, installed order and one route use shot029.
PHASE=.62
DEPTH=99
RATE_RAW=8192
RATE_HZ=(3277+((RATE_RAW*RATE_RAW)>>15)*80)/65536
RATE_TEXT=f'{RATE_HZ:.2f} hz'
MODES=('loop','one shot','steps','random step','random smooth','follow play')
TOOLS=('undo','smooth','quantize','invert','mirror','rotate','copy','paste','jitter','clear')


def curve_value(phase):
    # Display-space approximation only, matching ui_sigils.cpp's +/-46px extent.
    _,y=CURVE[round((len(CURVE)-1)*phase)]
    return max(-99,min(99,round((94-y)*DEPTH/46)))


def control_header(title, context='a'):
    c=Page(320)
    c.heading(24,17,title)
    c.text(296,26,'sigil '+context,GHOST,right=True)
    return c


def back(c, hint):
    c.text(20,228,hint,GHOST)
    c.text(300,228,'b back',GHOST,right=True)
    c.hit('close branch',(248,216,316,240))


def signed_rail(c,x0,x1,y,value,color=SPOT):
    center=(x0+x1)//2
    c.line((x0,y,x1,y))
    c.line((center,y-4,center,y+4),GHOST)
    x=center+round((x1-x0)*value/198)
    c.line((center,y,x,y),color)
    bead(c,x,y,3,color)


def upper(phase=PHASE, state='main'):
    c=Page(400)
    c.text(24,16,'patch01',GHOST)
    follow=state=='follow'
    empty=state=='unassigned'
    c.text(376,16,'sigil a / '+('follow' if follow else 'loop'),GHOST,right=True)
    c.heading(24,39,'sigils')
    c.text(376,48,'no destinations' if empty else '1 destination',GHOST,right=True)
    c.text(24,78,'output / bipolar',SPOT)
    output=0 if follow else curve_value(phase)
    number(c,24,135,f'{output:+d}')
    c.text(24,148,'-99',GHOST);c.text(154,148,'+99',GHOST,right=True)
    c.line((52,152,123,152))
    c.line((87,149,87,155),GHOST)
    bead(c,87+round(35*output/99),152,3)
    c.line((188,81,188,133,201,133))
    c.text(215,80,'depth',GHOST);c.text(376,80,'+99',SPOT,right=True)
    c.text(215,102,'timing',GHOST)
    c.text(376,102,'no play' if follow else RATE_TEXT,INK,right=True)
    c.text(215,124,'modulation',GHOST);c.text(376,124,'none',GHOST,right=True)
    # Serial installed order from screenshot 029. Only FILT is a destination.
    c.text(24,176,'c1',GHOST)
    xs=(72,137,202,267,332)
    names=('osc','filt','vca','rev','ghost')
    c.line((72,181,363,181),INK);c.marker(370,181,INK)
    for x,name in zip(xs,names):
        selected=name=='filt' and not empty
        color=SPOT if selected else INK
        c.text(x-len(name)*3,165,name,color)
        # Gradient-colored holes, never flat paper patches.
        patch=paper(400).crop((x-5,176,x+6,187))
        c.im.paste(patch,(x-5,176))
        bead(c,x,181,4,color,not selected)
    if empty:
        c.text(24,200,'no route / open routes below',GHOST)
    elif follow:
        c.text(24,200,'no play in c1 / output held at zero',GHOST)
    else:
        c.line((137,186,137,204,150,204),SPOT)
        c.text(158,200,'cutoff',SPOT)
        c.text(376,200,'a > c1 filt',GHOST,right=True)
    c.footer('follow needs a play module' if follow else 'free rate / draw below','y chains')
    return c


def lower(phase=PHASE, state='main'):
    c=Page(320)
    c.heading(20,15,'sigils')
    for i,name in enumerate('abcd'):
        x=174+i*39
        c.text(x,23,name,SPOT if i==0 else GHOST)
        if i==0:c.line((x-4,37,x+10,37),SPOT)
        c.hit('select '+name,(x-12,10,x+20,43))
    c.text(20,48,'draw / a',GHOST)
    c.text(224,48,'undo',GHOST)
    c.text(300,48,'tools',INK,right=True)
    c.hit('undo',(215,43,252,62));c.hit('tools',(259,43,312,62))
    # Unframed writable field: only time ticks and a fine zero line.
    x0,x1,y0,y1=20,300,65,154
    center=(y0+y1)//2
    c.line((x0,center,x1,center))
    for i in range(9):
        x=x0+(x1-x0)*i//8
        c.line((x,y1-2,x,y1),BRANCH)
    points=[(x0+round((x-31)*(x1-x0)/256), center+round((y-94)*.93)) for x,y in CURVE]
    c.d.line(points,fill=SPOT,width=2)
    if state!='follow':
        index=round((len(points)-1)*phase)
        px,py=points[index]
        c.line((px,y0+1,px,y1),GHOST);bead(c,px,py,4)
    c.hit('free draw',(16,64,305,158))
    # Three stable control columns and a separate momentary strike.
    follow=state=='follow'
    c.text(20,169,'mode >',GHOST)
    c.text(20,185,'follow' if follow else 'loop',INK)
    c.hit('mode chooser',(12,160,76,214))
    c.text(91,169,'play' if follow else 'free >',GHOST)
    c.text(91,185,'no play' if follow else RATE_TEXT,GHOST if follow else SPOT)
    if not follow:
        c.line((91,202,151,202));bead(c,106,202,3)
        c.hit('open timing branch',(84,160,158,180))
        c.hit('drag free rate',(84,180,158,214))
    c.text(179,169,'depth',GHOST);c.text(179,185,'+99',SPOT)
    signed_rail(c,179,242,202,DEPTH)
    c.hit('bipolar depth',(168,160,252,214))
    # Quiet text-only momentary trigger; no sun, star, or replacement decoration.
    c.text(273,185,'trig',SPOT)
    c.hit('trigger',(263,160,312,214))
    c.text(20,228,'mod none >',GHOST)
    c.text(300,228,'route none >' if state=='unassigned' else 'route c1 >',SPOT,right=True)
    c.hit('modulation branch',(12,216,146,240))
    c.hit('destination branch',(154,216,308,240))
    return c


def mode_branch():
    c=control_header('mode')
    c.line((39,68,39,205))
    for i,label in enumerate(MODES):
        y=66+i*26
        color=SPOT if i==0 else INK
        c.line((39,y+4,53,y+4),color if i==0 else BRANCH)
        if i==0:c.marker(57,y+4)
        c.text(69,y,label,color)
        c.hit(label,(24,y-8,300,y+18))
    back(c,'tap mode')
    return c


def tools_branch():
    c=control_header('shape tools')
    c.text(24,52,'edit the stored curve',GHOST)
    for column in range(2):
        x=34+column*145
        c.line((x,81,x,193))
        for row in range(5):
            i=column*5+row
            label=TOOLS[i]
            y=77+row*28
            # Empty clipboard is a disabled item, not silently removed.
            color=GHOST if label=='paste' else SPOT if label=='clear' else INK
            c.line((x,y+4,x+10,y+4))
            c.text(x+21,y,label,color)
            c.hit(label+(' / disabled' if label=='paste' else ''),(x-6,y-8,x+119,y+18))
    back(c,'undo swaps before / after')
    return c


def modulation_branch(selected='b', synced=False):
    c=control_header('rate mod',selected)
    c.text(24,54,'source',GHOST)
    sources=('none',)+tuple('sigil '+s for s in 'abcd'[:'abcd'.index(selected)])+('env',)
    # Only legal earlier sources are present; all four possible choices fit D.
    c.line((39,83,39,83+(len(sources)-1)*24))
    for i,label in enumerate(sources):
        y=79+i*24
        active=label=='sigil a' if selected!='a' else label=='none'
        c.line((39,y+4,52,y+4),SPOT if active else BRANCH)
        if active:c.marker(55,y+4)
        c.text(67,y,label,SPOT if active else INK)
        c.hit('source '+label,(24,y-7,143,y+17))
    c.text(178,79,'amount',GHOST)
    c.text(178,98,'+24' if selected!='a' else '+0',SPOT if selected!='a' else GHOST)
    signed_rail(c,178,296,126,24 if selected!='a' else 0)
    c.hit('rate mod depth',(164,91,308,148))
    c.text(178,160,'rate only',GHOST)
    c.text(178,179,'sync / paused' if synced else 'off' if selected=='a' else 'free / active',SPOT if selected!='a' else GHOST)
    c.text(24,203,'sync and follow ignore rate mod',GHOST)
    back(c,'earlier sigils / env')
    return c


def timing_branch(synced=False):
    c=control_header('timing')
    for x,label,on in ((34,'free',not synced),(188,'sync',synced)):
        c.text(x,63,label,SPOT if on else GHOST)
        if on:c.line((x,76,x+len(label)*6,76),SPOT)
        c.hit('timing '+label,(x-10,52,x+111,86))
    if synced:
        c.text(24,95,'division / 120 bpm',GHOST)
        labels=('4b','2b','1b','1/2','1/4','1/8')
        for i,label in enumerate(labels):
            x=34+(i%3)*94;y=121+(i//3)*41
            c.text(x,y,label,SPOT if i==4 else INK)
            if i==4:c.line((x-1,y+13,x+20,y+13),SPOT)
            c.hit('division '+label,(x-10,y-8,x+69,y+26))
        c.text(24,203,'rate modulation paused',GHOST)
    else:
        c.text(24,103,'free rate',GHOST)
        c.text(296,103,RATE_TEXT,SPOT,right=True)
        c.line((34,147,286,147));bead(c,97,147,4)
        c.text(24,170,'0.05 hz',GHOST);c.text(296,170,'40.05 hz',GHOST,right=True)
        c.hit('continuous free rate',(20,118,300,160))
        c.text(24,203,'drag / rate modulation available',GHOST)
    back(c,'tap timing / drag rate' if not synced else 'tap division')
    return c


def routes_branch(empty=False):
    c=control_header('routes')
    c.text(24,57,'from a',GHOST)
    c.text(296,57,'none' if empty else '1 destination',GHOST,right=True)
    if not empty:
        c.line((39,92,39,118,57,118),SPOT)
        c.text(69,88,'c1 / filt',INK)
        c.text(69,112,'cutoff',SPOT)
        c.text(296,112,'edit >',GHOST,right=True)
        c.hit('open existing destination editor',(24,76,304,140))
    else:
        c.text(39,92,'no destinations yet',GHOST)
        c.text(39,115,'the curve is still stored',GHOST)
    c.text(24,158,'selected / c1 filt',GHOST)
    c.line((39,190,55,190),BRANCH);c.marker(59,190)
    c.text(71,186,'assign to cutoff',SPOT)
    c.hit('assign selected module primary parameter',(24,174,304,210))
    back(c,'y chains / choose module')
    return c


def save(im,name):
    allowed=set(paper(400).get_flattened_data())|{INK,SPOT,GHOST,BRANCH,OUTSIDE}
    assert set(im.get_flattened_data())<=allowed,name
    im.save(OUT/(name+'_native.png'))
    big=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
    assert big.resize(im.size,Image.Resampling.NEAREST).tobytes()==im.tobytes()
    big.save(OUT/(name+'_2x.png'))


def main():
    before=protected_hashes()
    OUT.mkdir(parents=True,exist_ok=True)
    top,bottom=upper(),lower()
    pages={'bottom':bottom, 'mode':mode_branch(), 'tools':tools_branch(),
           'mod_b':modulation_branch(), 'mod_a':modulation_branch('a'),
           'mod_d':modulation_branch('d'), 'mod_sync':modulation_branch('b',True),
           'timing_free':timing_branch(), 'timing_sync':timing_branch(True),
           'routes':routes_branch(), 'routes_empty':routes_branch(True),
           'follow_no_play':lower(state='follow'), 'unassigned':lower(state='unassigned')}
    save(top.im,'top')
    for name,page in pages.items():save(page.im,name)
    dual=pair(top.im,bottom.im);save(dual,'sigils_dual')
    save(pair(top.im,pages['mode'].im),'mode_dual')
    for state in ('follow','unassigned'):
        save(pair(upper(state=state).im,lower(state=state).im),state+'_dual')
    # Primary visual deliverable plus all six action branches at native/nearest2x.
    sheet=Image.new('RGB',(980,540),OUTSIDE)
    order=('mode','tools','timing_sync','mod_b','routes','follow_no_play')
    captions=Canvas(980,540,OUTSIDE)
    for i,name in enumerate(order):
        x=(i%3)*330;y=(i//3)*270
        captions.text(x+12,y+8,('01 / mode','02 / shape tools','03 / timing',
                               '04 / rate modulation','05 / routes','06 / follow without play')[i],(228,194,207))
    sheet.paste(captions.im)
    for i,name in enumerate(order):sheet.paste(pages[name].im,((i%3)*330,(i//3)*270+26))
    save(sheet,'branches')
    # Validate the Sun removal as pixels, not just source absence.
    expected=Page(320);expected.text(273,185,'trig',SPOT)
    assert bottom.im.crop((263,160,312,214)).tobytes()==expected.im.crop((263,160,312,214)).tobytes()
    assert set(MODES)=={name for name,_ in pages['mode'].hits if name!='close branch'}
    assert set(TOOLS)=={name.split(' /')[0] for name,_ in pages['tools'].hits if name!='close branch'}
    assert not any('sigil a' in name for name,_ in pages['mod_a'].hits)
    for selected in 'abcd':
        mod=modulation_branch(selected)
        legal={name for name,_ in mod.hits if name.startswith('source ')}
        assert legal=={'source none','source env'}|{'source sigil '+x for x in 'abcd'[:'abcd'.index(selected)]}
    for phase in (0,.1,.25,.5,.62,.75,.9,1):
        upper(phase);lower(phase)
        assert -99<=curve_value(phase)<=99
    # Test sign/digit bounds for all possible output values, not just one fixture.
    for value in range(-99,100):
        c=Page(400);number(c,24,135,f'{value:+d}')
        assert c.labels[0][0][2]<188
    assert lower(state='follow').im.crop((16,64,305,158)).tobytes()!=bottom.im.crop((16,64,305,158)).tobytes()
    assert (3277+((32767*32767)>>15)*80)/65536 < 40.05
    # Keep facts on the diagram and distinguish illustrative values from capture.
    review='''sigils / visual review, iteration 2

kept: exact shared paper/four inks, gothic headline/output, free-draw silhouette,
serial chain context, text-only trig. no new ornamental glyph replaces the sun.

fixed: output follows the same raster sample as the cursor (-28 at .62), not an
unrelated -38; free speed reads 2.55 hz from raw8192 using current engine formula;
mod and routes now own 24px footer targets, no duplicate a-trig label; rate has
separate open-branch and drag territories. field loses 4px to gain control space.

completed: all six mode choices; all ten shape tools including disabled paste;
free and all six sync divisions; legal earlier-sigil/env rate sources with depth;
explicit sync pause; existing route + assign-selected action; no-route and
follow-without-play states. follow does not draw an invented cursor.

not complete: no live hardware rendering/controller/GPU validation. exact Q15
shape is not available: only screenshot silhouette is traced. rate/source/phase
values are designed examples, not newly downloaded telemetry. route editor
navigation is proposed, not a current runtime binding. clear has no claimed undo
restoration: current tool does not snapshot clear. multi-route overflow and all
six mode-specific live displays still need rendering before implementation.

implementation boundary: leave DSP/routing/order/serializer untouched. capture a
bounded read-only dual-screen DTO before GPU work; free/sync/follow mod precedence
must match engine.cpp. source a must never offer self or later sources.
'''
    (OUT/'review.txt').write_text(review)
    # Actual old capture beside proposal; external labels prevent capture confusion.
    comparison=Image.new('RGB',(816,524),OUTSIDE)
    labels=Canvas(816,28,OUTSIDE)
    labels.text(20,10,'device / shot 029',(228,194,207))
    labels.text(436,10,'proposal / menu style',(228,194,207))
    comparison.paste(labels.im,(0,0))
    comparison.paste(Image.open(SOURCE/'shot_029_dual.png'),(0,28))
    comparison.paste(dual,(416,28))
    comparison.save(OUT/'before_after_native.png')
    comparison.resize((1632,1048),Image.Resampling.NEAREST).save(OUT/'before_after_2x.png')
    assert before==protected_hashes(),'runtime or binary changed'
    ref=Image.open(SOURCE/'shot_030_bot.png').convert('RGB')
    assert ref.crop((0,0,2,240)).tobytes()==paper(320).crop((0,0,2,240)).tobytes()
    report={'status':'static visual proposal, not implemented or deployed',
            'reference':'hardware shots 029 (sigils) and 030 (current Ghost menu style)',
            'data':{'curve':'raster traced from shot029, not original Q15 table',
                    'fixture_output':curve_value(PHASE),'fixture_phase':PHASE,
                    'fixture_free_rate_raw':RATE_RAW,'fixture_free_rate_hz':RATE_HZ,
                    'provenance':'illustrative values; no current telemetry; chain order from029'},
            'checks':{'labels_nonoverlapping':True,'proposed_touch_regions_nonoverlapping':True,
                      'exact_menu_paper_against_hardware030':True,'native_palette_nearest2x':True,
                      'all6_modes_all10_tools':True,'all4_sigils_legal_sources':True,
                      'all199_output_glyph_bounds':True,'trig_pixels_text_only':True,
                      'protected_runtime_and_binaries_unchanged':len(before)},
            'pages':{name:{'controls':page.hits,'labels':len(page.labels)} for name,page in pages.items()},
            'limitations':['static Python proposal; no controller/GPU test or deployment',
                          'modulation changes free rate only; sync/follow ignore it',
                          'original source curve samples are not recovered from a screenshot',
                          'large output uses -99..99 display scale, not percent or Hz',
                          'routing branch/editor entry is a proposed navigation change',
                          'full multi-route overflow and remaining live mode states pending'],
            'source_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in SOURCE.glob('*.bmp')}}
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(OUT/'sigils_dual_2x.png')
    print('pass: label/hit bounds, palette, nearest2x, hardware paper and protected runtime hashes')
    print(OUT/'branches_2x.png')
    print(f'pass: {len(pages)} lower states, 6 mode choices, 10 tools, legal sources a-d, 199 numeric values; no sun')


if __name__=='__main__':main()
