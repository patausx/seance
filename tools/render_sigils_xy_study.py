#!/usr/bin/env python3
"""Design study ONLY: A/B drawn curves + C/D as two coordinates of one XY gesture.

The current engine still has four drawable sigils. No code, patch conversion or
routing changes are made. XY state/path is an authored UI fixture, not hardware
telemetry, recorded audio, or gesture playback. Hold retains position on release;
with hold off release returns zero. Gesture recording is NOT proposed here.

Seance Menu/Note bitmap derivatives: Deborah Khodanovich 2025, SIL OFL 1.1.
See assets/fonts/README.md and OFL.txt; original font/assets unchanged.
"""
from dataclasses import dataclass, replace
from pathlib import Path
import hashlib
import json
import math
from PIL import Image, ImageDraw
from render_menu_style_study import Page, paper, GHOST, BRANCH, overlaps
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair, protected_hashes
from render_sigils_menu_study import CURVE, DIGITS, bead, signed_rail
from render_sigils_single_surface import State, lower, upper, sample_at

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/sigils_xy_study'
SOURCE=ROOT/'hardware_shots/sigils_review_20260907_002440'
FIELD=(20,57,300,191)
WAVE_BOX=(12,61,308,150)
PALETTE=set(paper(400).get_flattened_data())|{INK,SPOT,GHOST,BRANCH,OUTSIDE}
TREATMENTS=('current','thread','sweep')


def restore(c,box):
    c.im.paste(paper(c.im.width).crop(box),box[:2])


def choices(c,gesture=False):
    """Two single source tabs and ONE paired tab, never C and D as two pads."""
    restore(c,(114,4,249,33))
    c.labels=[(b,n) for b,n in c.labels if not overlaps(b,(114,4,249,33))]
    c.hits=[(n,b) for n,b in c.hits if not n.startswith('select ')]
    for x,label,selected,box in (
        (133,'a',not gesture,(119,5,149,32)),
        (169,'b',False,(153,5,185,32)),
        (202,'c+d',gesture,(191,5,245,32))):
        c.text(x,18,label,SPOT if selected else GHOST)
        if selected:c.line((x-3,29,x+len(label)*6+1,29),SPOT)
        c.hit('select '+label,box)


def curve_points():
    # Exact SAME sampled silhouette/extent as the approved single surface.
    x0,x1,y0,y1=20,300,64,147
    center=(y0+y1)//2;scale=(y1-y0-6)/92
    return [(x0+round((x-31)*(x1-x0)/256),center+round((y-94)*scale)) for x,y in CURVE]


POINTS=curve_points()


def curve_bottom(treatment='thread',phase=.62,paired_tabs=True):
    assert treatment in TREATMENTS
    c=lower(State(phase=phase))
    if paired_tabs:choices(c)
    if treatment=='current':return c
    restore(c,WAVE_BOX)
    # Quiet zero: sparse dots, not a ruled box or a full-screen playhead.
    for x in range(20,301,10):c.d.point((x,105),fill=BRANCH)
    c.line((20,102,20,108),GHOST);c.line((300,102,300,108),GHOST)
    for i in range(9):
        x=20+280*i//8;c.line((x,145,x,147),BRANCH)
    index=round((len(POINTS)-1)*phase)
    if treatment=='sweep':
        # Phase coloring only: dark past, muted future; still a single waveform.
        c.d.line(POINTS,fill=BRANCH,width=1)
        if index:c.d.line(POINTS[:index+1],fill=INK,width=2)
    else:
        c.d.line(POINTS,fill=INK,width=1)
        start=max(0,index-18)
        if index>start:c.d.line(POINTS[start:index+1],fill=SPOT,width=2)
    x,y=POINTS[index]
    # A tiny local cursor, no long upright line cutting the drawn contour.
    bead(c,x,y,3,SPOT)
    c.line((x-2,150-3,x+2,150-3),SPOT)
    return c


def number(c,x,baseline,value):
    """Exact installed gothic digits/sign construction; works in either column."""
    start=x
    for ch in f'{value:+d}':
        if ch in '+-':
            c.d.rectangle((x,baseline-14,x+12,baseline-13),fill=SPOT)
            if ch=='+':c.d.rectangle((x+6,baseline-20,x+7,baseline-8),fill=SPOT)
            x+=18
        else:
            mask,top=DIGITS[ch]
            c.im.paste(INK,(x,baseline+top),mask);x+=mask.width+2
    box=(start,baseline-44,x,baseline+4)
    assert 0<=box[0]<box[2]<=c.im.width
    assert all(not overlaps(box,b) for b,_ in c.labels)
    c.labels.append((box,f'coordinate {value:+d}'))
    return x


def xy_pixel(x,y):
    """Bipolar: right/up positive. Device's descending screen Y is inverted."""
    assert -99<=x<=99 and -99<=y<=99
    x0,y0,x1,y1=FIELD
    return round(x0+(x+99)*(x1-x0)/198),round(y1-(y+99)*(y1-y0)/198)


@dataclass(frozen=True)
class XY:
    x: int=61
    y: int=28
    hold: bool=True
    down: bool=True
    trail: tuple=()

    def released(self):
        return replace(self,down=False,trail=(),x=self.x if self.hold else 0,y=self.y if self.hold else 0)


def dotted(c,a,b,color=BRANCH,spacing=6):
    x0,y0=a;x1,y1=b
    length=max(abs(x1-x0),abs(y1-y0))
    for n in range(0,int(length)+1,spacing):
        t=n/length if length else 0
        c.d.point((round(x0+(x1-x0)*t),round(y0+(y1-y0)*t)),fill=color)


def xy_bottom(s):
    c=Page(320)
    c.heading(20,10,'sigils');choices(c,True)
    c.text(300,18,'xy',GHOST,right=True)
    c.text(20,41,'c / horizontal',GHOST)
    c.text(300,41,'d / vertical',GHOST,right=True)
    x0,y0,x1,y1=FIELD
    mx,my=xy_pixel(0,0)
    # A wide open touch surface, only four registration corners and a quiet zero.
    for x,y,sx,sy in ((x0,y0,1,1),(x1,y0,-1,1),(x0,y1,1,-1),(x1,y1,-1,-1)):
        c.line((x+sx*5,y,x,y,x,y+sy*5),GHOST)
    dotted(c,(x0,my),(x1,my));dotted(c,(mx,y0),(mx,y1))
    for x in (-50,50):
        for y in (-50,50):c.d.point(xy_pixel(x,y),fill=BRANCH)
    x,y=xy_pixel(s.x,s.y)
    # Short observed-input history only. This visual is not a recorded loop.
    points=[xy_pixel(a,b) for a,b in s.trail]+[(x,y)]
    if s.down and len(points)>1:
        c.d.line(points,fill=GHOST,width=1)
        if len(points)>3:c.d.line(points[-4:],fill=SPOT,width=2)
    dotted(c,(x0,y),(x,y),GHOST)
    dotted(c,(x,y),(x,y1),GHOST)
    c.line((x0-2,y,x0+2,y),SPOT);c.line((x,y1-2,x,y1+2),SPOT)
    if s.down:
        bead(c,x,y,5,SPOT,True);bead(c,x,y,2,SPOT)
    else:bead(c,x,y,3,INK if s.hold else GHOST,True)
    c.hit('xy direct gesture',(14,53,307,198))
    c.text(20,204,f'c {s.x:+d}',SPOT)
    c.text(160,204,'touch' if s.down else 'held' if s.hold else 'center',GHOST)
    c.text(300,204,f'd {s.y:+d}',SPOT,right=True)
    c.text(20,230,'hold on' if s.hold else 'hold off',SPOT if s.hold else GHOST)
    c.text(139,230,'center',INK)
    c.text(300,230,'y chains',GHOST,right=True)
    c.hit('toggle hold',(12,220,112,240))
    c.hit('center both coordinates',(124,220,210,240))
    c.hit('go to chains / assign axes',(218,220,308,240))
    return c


def xy_top(s):
    c=Page(400)
    c.text(24,16,'patch01',GHOST)
    c.text(376,16,'c+d / xy gesture',GHOST,right=True)
    c.heading(24,39,'gesture')
    c.text(376,48,'touch' if s.down else 'held' if s.hold else 'center',SPOT,right=True)
    c.text(24,79,'c / horizontal',GHOST)
    c.text(224,79,'d / vertical',GHOST)
    number(c,24,135,s.x);number(c,224,135,s.y)
    c.text(24,159,'c1 filt / cutoff',SPOT)
    c.text(224,159,'c1 rev / mix',SPOT)
    signed_rail(c,24,176,184,s.x)
    signed_rail(c,224,376,184,s.y)
    c.text(24,208,'two coordinates / one gesture',GHOST)
    c.footer('hold keeps position' if s.hold else 'release returns zero','no playback')
    return c


def save(im,name):
    assert set(im.get_flattened_data())<=PALETTE,name
    im.save(OUT/(name+'_native.png'))
    large=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
    assert large.resize(im.size,Image.Resampling.NEAREST).tobytes()==im.tobytes()
    large.save(OUT/(name+'_2x.png'))


def trajectory(t):
    # Authored demonstrator path: an intentional loop of the hand, not an LFO.
    return (round(68*math.sin(2*math.pi*t-.7)),round(60*math.sin(2*math.pi*t*1.35+.2)))


def main():
    before=protected_hashes()
    old_files=[ROOT/'tools/render_sigils_single_surface.py',ROOT/'ui_mockups/sigils_single_surface/main_dual_2x.png',
               ROOT/'ui_mockups/sigils_single_surface/report.json']
    old_hash={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in old_files}
    OUT.mkdir(parents=True,exist_ok=True)
    trail=((-30,-35),(-23,-18),(-12,4),(2,25),(19,37),(37,37),(50,32),(57,29))
    xy=XY(trail=trail)
    t=upper(State());b=curve_bottom()
    save(t.im,'thread_top');save(b.im,'thread_bottom');save(pair(t.im,b.im),'thread_dual')
    for name,state in (('xy',xy),('xy_held',xy.released()),('xy_center',replace(xy,hold=False).released())):
        save(xy_top(state).im,name+'_top');save(xy_bottom(state).im,name+'_bottom')
        save(pair(xy_top(state).im,xy_bottom(state).im),name+'_dual')
    # Two instruments, same family; no suggestion that XY controls alter the curve.
    sheet=Image.new('RGB',(824,528),OUTSIDE)
    titles=Canvas(824,32,OUTSIDE)
    titles.text(20,10,'a / b  -  ink thread',(228,194,207))
    titles.text(444,10,'c + d  -  xy gesture',(228,194,207))
    sheet.paste(titles.im,(0,0))
    sheet.paste(pair(t.im,b.im),(0,32));sheet.paste(pair(xy_top(xy).im,xy_bottom(xy).im),(424,32))
    save(sheet,'curve_and_xy')
    # Fair curve comparison: EXACT same control/header pixels; only field changes.
    strip=Image.new('RGB',(984,272),OUTSIDE)
    labels=Canvas(984,32,OUTSIDE)
    for i,(treatment,label) in enumerate(zip(TREATMENTS,('01 / current','02 / ink thread','03 / phase sweep'))):
        x=i*332;labels.text(x+16,10,label,(228,194,207))
        page=curve_bottom(treatment,paired_tabs=False)
        save(page.im,'curve_'+treatment)
        strip.paste(page.im,(x,32))
    strip.paste(labels.im,(0,0));save(strip,'curve_comparison')
    # Three seconds of touch + a held pause; top and bottom share every coordinate.
    frames=[];trail=[]
    for i in range(40):
        x,y=trajectory(i/39)
        state=XY(x,y,trail=tuple(trail[-9:]))
        trail.append((x,y))
        im=pair(xy_top(state).im,xy_bottom(state).im)
        canvas=Image.new('RGB',(400,520),OUTSIDE)
        caption=Canvas(400,24,OUTSIDE);caption.text(16,8,'xy concept / illustrative hand gesture',(228,194,207))
        canvas.paste(caption.im);canvas.paste(im,(0,24))
        frames.append(canvas.resize((800,1040),Image.Resampling.NEAREST))
    held=state.released()
    im=pair(xy_top(held).im,xy_bottom(held).im)
    end=frames[-1].copy();end.paste(im.resize((800,992),Image.Resampling.NEAREST),(0,48))
    frames.append(end)
    # One shared palette prevents GIF color drift, no dithering on native pixel art.
    colors=sorted(PALETTE)
    flat=[v for rgb in colors for v in rgb]
    palette=flat+[0]*(768-len(flat))
    index={color:i for i,color in enumerate(colors)}
    gif=[]
    for frame in frames:
        # Pillow quantize uses approximate lookup; build exact indices ourselves
        # so adjacent one-channel paper shades cannot collapse into each other.
        indexed=Image.frombytes('P',frame.size,bytes(index[p] for p in frame.get_flattened_data()))
        indexed.putpalette(palette);gif.append(indexed)
    gif[0].save(OUT/'xy_gesture_2x.gif',save_all=True,append_images=gif[1:],duration=[80]*40+[1200],loop=0,disposal=2,optimize=False)
    with Image.open(OUT/'xy_gesture_2x.gif') as playback:
        assert playback.n_frames==len(gif)
        for i,expected in enumerate(frames):
            playback.seek(i)
            assert playback.convert('RGB').tobytes()==expected.tobytes(),'GIF palette/delta changed native ink'
        assert playback.info['duration']==1200
    # Curve motion is just a conceptual phase sweep of the traced stored contour.
    curve_frames=[pair(upper(State(phase=i/31)).im,curve_bottom(phase=i/31).im).resize((800,992),Image.Resampling.NEAREST)
                  for i in range(32)]
    curve_frames[0].save(OUT/'thread_phase_2x.png',save_all=True,append_images=curve_frames[1:],duration=90,loop=0)
    # Focused checks; never claim model fixtures are actual engine output.
    for value in range(-99,100):
        number(Page(400),24,135,value);number(Page(400),224,135,value)
    for x in (-99,-50,0,50,99):
        for y in (-99,-50,0,50,99):
            for hold in (False,True):
                state=XY(x,y,hold=hold)
                xy_bottom(state);xy_top(state)
                released=state.released();xy_bottom(released);xy_top(released)
                assert (released.x,released.y)==((x,y) if hold else (0,0))
    assert xy_pixel(-99,99)==FIELD[:2]
    assert xy_pixel(99,-99)==FIELD[2:]
    current=curve_bottom('current',paired_tabs=False).im
    for treatment in ('thread','sweep'):
        alternate=curve_bottom(treatment,paired_tabs=False).im.copy()
        alternate.paste(current.crop(WAVE_BOX),WAVE_BOX[:2])
        assert alternate.tobytes()==current.tobytes(),'controls drifted between curve options'
    for phase in (0,.1,.5,.62,.9,1):
        for treatment in ('thread','sweep'):curve_bottom(treatment,phase)
    original=lower(State()).im
    assert curve_bottom('current',paired_tabs=False).im.tobytes()==original.tobytes()
    assert before==protected_hashes()
    assert old_hash=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in old_files}
    report={'status':'preview only / not implemented / no FTP',
            'concept':'a and b remain drawn sources; c and d become the X/Y outputs of one proposed gesture pad',
            'curve':'same traced hardware029 silhouette and amplitude; only stroke/color/phase-cursor treatment changes',
            'xy':'illustrative bipolar -99..99 output values and assumed c1 filt cutoff / c1 rev mix assignments',
            'semantics':['x increases right / y increases up','hold on retains the last coordinates after lift',
                         'hold off returns both coordinates to zero after lift','center resets both axes',
                         'short trail is recent hand positions, NOT a recorded/looping gesture',
                         'route assignment remains in chains; no extra pad menus or recording controls'],
            'checks':{'all199_values_both_columns':True,'100_xy_touch_and_release_cases':True,
                      'curve_controls_pixel_identical':True,'original_curve_option_exact':True,
                      'native_labels_hits_bounds':True,'palette_nearest2x':True,'gif_all41_frames_exact':True,
                      'old_proposal_and_runtime_unchanged':True,'protected_runtime_count':len(before)},
            'limits':['host concept, not new device feature or controller validation',
                      'c/d replacement must not silently reinterpret existing saved curves; storage/migration not designed',
                      'xy smoothness/slew and destination depth/base behavior are undecided',
                      'no new code removing runtime shape tools or existing c/d curves',
                      'current + traces from hardware raster, not exact original samples',
                      'thin 1px ink thread must be evaluated on LCD; thick sweep alternative supplied']}
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'README.txt').write_text(__doc__+'\nRun: python3 tools/render_sigils_xy_study.py\n'
        'curve_and_xy_2x.png: drawn and xy instruments side by side.\n'
        'curve_comparison_2x.png: current / ink thread / phase sweep, same controls.\n'
        'xy_gesture_2x.gif: illustrative hand motion then hold, not recorded playback.\n'
        'thread_phase_2x.png: APNG conceptual phase, not hardware telemetry.\n')
    print(OUT/'curve_and_xy_2x.png');print(OUT/'curve_comparison_2x.png');print(OUT/'xy_gesture_2x.gif')
    print('pass: native label/hit bounds, exact unchanged controls/old proposal, coordinate endpoints/release, 199 numerals, palette and protected runtime hashes')


if __name__=='__main__':main()
