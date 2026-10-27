#!/usr/bin/env python3
"""Sigils, single working surface. Static visual proposal, NOT runtime UI.

No shape-tools page, mode/timing/modulation/route menus, or ornamental trigger.
Reuses exact menu paper, native 6x8 and existing Seance Note digit masks.
Font derivative: Deborah Khodanovich 2025 / SIL OFL 1.1; assets/fonts/README.md.
Only the hardware029 curve silhouette is traced. Values below are design fixtures.
"""
from dataclasses import dataclass, replace
from pathlib import Path
import hashlib
import json
from PIL import Image, ImageDraw
from render_menu_style_study import Page, paper, GHOST, BRANCH, overlaps
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair, protected_hashes
from render_sigils_menu_study import CURVE, DIGITS, bead, signed_rail

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/sigils_single_surface'
SOURCE=ROOT/'hardware_shots/sigils_review_20260907_002440'
MODE_NAMES=('loop','one shot','steps','random step','random smooth','follow play')
MODE_LABELS=('loop','shot','step','rand','smooth','follow')
DIVISIONS=('4b','2b','1b','1/2','1/4','1/8')


@dataclass(frozen=True)
class State:
    sigil: str='a'
    mode: int=0
    sync: bool=False
    division: int=4
    rate_raw: int=8192
    depth: int=99
    mod_source: str='none'
    mod_depth: int=0
    # Source instantaneous output is deliberately zero in modulation fixtures.
    # This keeps current base/live free rates equal without pretending off.
    source_value: int=0
    phase: float=.62
    focus: str='output'
    routes: tuple=(('c1 filt','cutoff'),)


def hz(raw):
    return (3277+((raw*raw)>>15)*80)/65536


def source_options(sigil):
    return ('none',)+tuple('abcd'[:'abcd'.index(sigil)])+('env',)


def effective_raw(s):
    if s.sync or s.mode==5 or s.mod_source=='none':return s.rate_raw
    return max(0,min(32767,s.rate_raw+round(s.source_value*s.mod_depth*32767/(99*99))))


def sample_at(phase):
    return CURVE[round((len(CURVE)-1)*phase)][1]


def output(s):
    if s.mode==5:return 0  # This fixture has osc/filt/vca/rev/ghost, no PLAY.
    if s.mode==3:raw=-18   # Labeled synthetic held random state, not the drawing.
    elif s.mode==4:
        t=s.phase*s.phase*(3-2*s.phase)
        raw=round(-18+(37+18)*t)
    else:
        phase=(min(15,int(s.phase*16))+.5)/16 if s.mode==2 else s.phase
        raw=(94-sample_at(phase))*99/46
    return max(-99,min(99,round(raw*s.depth/99)))


def hero(c,x,baseline,text,suffix=''):
    start=x
    for ch in text:
        if ch in '+-':
            c.d.rectangle((x,baseline-14,x+12,baseline-13),fill=SPOT)
            if ch=='+':c.d.rectangle((x+6,baseline-20,x+7,baseline-8),fill=SPOT)
            x+=18
        elif ch=='.':
            c.d.rectangle((x,baseline-3,x+2,baseline-1),fill=INK);x+=6
        elif ch=='/':
            c.d.line((x+9,baseline-30,x,baseline-2),fill=SPOT,width=2);x+=15
        else:
            mask,top=DIGITS[ch]
            c.im.paste(INK,(x,baseline+top),mask);x+=mask.width+2
    box=(start,baseline-44,x,baseline+4)
    assert 0<=start<x<190 and all(not overlaps(box,b) for b,_ in c.labels)
    c.labels.append((box,'hero '+text))
    if suffix:c.text(x+4,baseline+2,suffix,GHOST)


def lower(s):
    assert s.mod_source in source_options(s.sigil)
    c=Page(320)
    c.heading(20,10,'sigils')
    # Source selection and undo share one header; no tools button remains.
    for i,name in enumerate('abcd'):
        x=130+i*31
        c.text(x,18,name,SPOT if name==s.sigil else GHOST)
        if name==s.sigil:c.line((x-3,29,x+9,29),SPOT)
        c.hit('select '+name,(x-11,5,x+17,32))
    c.text(300,18,'undo',GHOST,right=True)
    c.hit('undo stroke',(254,5,312,32))
    # All six existing modes are directly addressable. Full names live above.
    for i,(label,center) in enumerate(zip(MODE_LABELS,(34,82,130,178,230,282))):
        x=center-len(label)*3
        c.text(x,43,label,SPOT if s.mode==i else GHOST)
        if s.mode==i:c.line((x,55,x+len(label)*6-2,55),SPOT)
        left,right=((10,58),(58,106),(106,154),(154,202),(202,258),(258,310))[i]
        c.hit('mode '+MODE_NAMES[i],(left,34,right,59))
    # Writable score stays fixed in every mode; random modes retain stored ink
    # but do NOT falsely place a live random value on that stored shape.
    x0,x1,y0,y1=20,300,64,147
    center=(y0+y1)//2
    scale=(y1-y0-6)/92
    random=s.mode in (3,4)
    c.line((x0,center,x1,center))
    for i in range(9):
        x=x0+(x1-x0)*i//8;c.line((x,y1-2,x,y1),BRANCH)
    points=[(x0+round((x-31)*(x1-x0)/256),center+round((y-94)*scale)) for x,y in CURVE]
    c.d.line(points,fill=GHOST if random else SPOT,width=1 if random else 2)
    if s.mode==2:
        for i in range(16):
            a=x0+(x1-x0)*i//16;b=x0+(x1-x0)*(i+1)//16
            y=center+round((sample_at((i+.5)/16)-94)*scale)
            c.line((a,y,b-1,y),SPOT)
    if s.mode not in (3,4,5):
        x=x0+round((x1-x0)*s.phase)
        phase=(min(15,int(s.phase*16))+.5)/16 if s.mode==2 else s.phase
        y=center+round((sample_at(phase)-94)*scale)
        c.line((x,y0,x,y1),GHOST);bead(c,x,y,3)
    if random:c.text(20,65,'stored shape / random ignores it',GHOST)
    c.hit('draw stored curve',(12,61,308,150))
    # Timing: tap free/sync in place, drag value/rail. Sync offers six detents on
    # the same rail; full selected division is always shown in the value label.
    follow=s.mode==5
    timing='play c1' if follow else 'sync' if s.sync else 'free'
    value='no play' if follow else DIVISIONS[s.division] if s.sync else f'{hz(effective_raw(s)):.2f} hz'
    color=GHOST if follow else SPOT
    c.text(20,159,timing,GHOST if follow else INK)
    c.text(176,159,value,color,right=True)
    if not follow:
        c.hit('toggle free / sync',(12,152,76,174))
        c.hit('drag rate / division value',(78,152,184,174))
        c.line((20,182,176,182))
        if s.sync:
            for i in range(6):bead(c,20+round(156*i/5),182,3 if i==s.division else 1,SPOT if i==s.division else GHOST)
        else:bead(c,20+round(156*s.rate_raw/32767),182,3)
        c.hit('drag rate / six divisions',(12,174,184,192))
    c.text(208,159,'depth',GHOST);c.text(300,159,f'{s.depth:+d}',SPOT,right=True)
    signed_rail(c,208,300,182,s.depth)
    c.hit('drag bipolar depth',(198,152,308,192))
    # Direct cycle matches existing None -> earlier sigils -> ENV -> None.
    # Signed depth has its own broad drag territory; no source selection page.
    paused=s.sync or follow
    c.text(20,201,'rate mod',GHOST)
    c.text(80,201,s.mod_source,SPOT if s.mod_source!='none' and not paused else GHOST)
    c.text(139,201,f'{s.mod_depth:+d}',SPOT if s.mod_source!='none' and not paused else GHOST)
    signed_rail(c,190,300,207,s.mod_depth,GHOST if paused or s.mod_source=='none' else SPOT)
    c.hit('cycle legal rate source',(12,195,123,220))
    c.hit('drag rate modulation depth',(129,195,308,220))
    # Assignment is a direct existing action, NOT an entry to another menu.
    # Target is the selected module's primary parameter, identified on top.
    c.text(20,230,'assign c1 cut',SPOT)
    c.text(161,230,'y chains',GHOST)
    c.text(300,230,'trig',SPOT,right=True)
    c.hit('assign selected primary',(12,222,151,240))
    c.hit('go to chains',(153,222,237,240))
    c.hit('trigger / a',(245,222,308,240))
    return c


def upper(s):
    c=Page(400)
    c.text(24,16,'patch01',GHOST)
    c.text(376,16,f'sigil {s.sigil} / {MODE_NAMES[s.mode]}',GHOST,right=True)
    c.heading(24,39,'sigils')
    focus=s.focus
    if focus=='rate':
        title='division' if s.sync else 'free rate'
        value=DIVISIONS[s.division].rstrip('b') if s.sync else f'{hz(s.rate_raw):.2f}'
        suffix='beats' if s.sync else 'hz'
        detail=f'{hz(effective_raw(s)):.2f} hz live' if not s.sync else '120 bpm / clocked'
        help_text='tap free / sync; drag rate'
    elif focus=='depth':
        title='output depth';value=f'{s.depth:+d}';suffix=''
        detail=f'output {output(s):+d}'
        help_text='drag depth / bipolar'
    elif focus=='mod':
        title='rate mod amount';value=f'{s.mod_depth:+d}';suffix=''
        detail=('paused / '+('follow' if s.mode==5 else 'sync')) if s.sync or s.mode==5 else 'source '+s.mod_source+' / free rate'
        help_text='tap source / drag amount'
    else:
        title='output / bipolar';value=f'{output(s):+d}';suffix=''
        detail='-99 ... +99'
        help_text='draw / tap mode / pull value'
    c.text(24,79,title,SPOT)
    hero(c,24,135,value,suffix)
    c.text(24,151,detail,GHOST)
    mode_info=('free-drawn loop','one shot / a retriggers','16 step curve',
               'random / held','random / smooth','no play / output zero')[s.mode]
    c.text(24,172,mode_info,GHOST)
    if s.mod_source!='none':
        mod_status='mod paused / follow' if s.mode==5 else 'mod paused / sync' if s.sync else 'rate from '+s.mod_source
        c.text(24,190,mod_status,GHOST)
    # Always-visible destinations in a fixed second column. No repeated curve.
    c.text(215,79,'destinations',GHOST)
    c.text(376,79,str(len(s.routes)),SPOT,right=True)
    if not s.routes:
        c.text(215,108,'none yet',GHOST)
        c.text(215,130,'assign below',GHOST)
    else:
        for i,(module,param) in enumerate(s.routes[:3]):
            y=103+28*i
            c.line((201,y+3,209,y+3),SPOT)
            c.text(217,y,module,INK)
            c.text(217,y+12,param,SPOT)
        if len(s.routes)>3:c.text(217,190,f'+{len(s.routes)-3} / y chains',GHOST)
    # Full explicit destination for the direct assignment, not arbitrary graph UI.
    c.text(24,209,'assign target / c1 filt cutoff',GHOST)
    c.footer(help_text,'a trig')
    return c


def save(image,name):
    allowed=set(paper(400).get_flattened_data())|{INK,SPOT,GHOST,BRANCH,OUTSIDE}
    assert set(image.get_flattened_data())<=allowed
    image.save(OUT/(name+'_native.png'))
    big=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
    assert big.resize(image.size,Image.Resampling.NEAREST).tobytes()==image.tobytes()
    big.save(OUT/(name+'_2x.png'))


def main():
    before=protected_hashes()
    OUT.mkdir(parents=True,exist_ok=True)
    main_state=State()
    frames={
        'main':main_state,
        'sync':State(sigil='b',sync=True,mod_source='a',mod_depth=24,focus='rate'),
        'mod':State(sigil='b',mod_source='a',mod_depth=24,focus='mod',
                    routes=(('c1 filt','cutoff'),('c1 rev','mix'),('c3 ghost','drift'))),
        'depth':State(depth=-64,focus='depth'),
        'follow':State(mode=5),
        'unassigned':State(routes=()),
    }
    for name,s in frames.items():
        save(upper(s).im,name+'_top');save(lower(s).im,name+'_bottom')
        save(pair(upper(s).im,lower(s).im),name+'_dual')
    for mode,label in enumerate(MODE_LABELS):
        state=State(mode=mode)
        save(pair(upper(state).im,lower(state).im),'mode_'+label+'_dual')
    # Three views of the SAME work surface, never three menus/pages to navigate.
    sheet=Image.new('RGB',(1232,524),OUTSIDE)
    captions=Canvas(1232,28,OUTSIDE)
    for i,(name,label) in enumerate((('main','01 / draw'),('sync','02 / drag clock'),('mod','03 / drag modulation'))):
        captions.text(i*416+20,10,label,(228,194,207))
        sheet.paste(Image.open(OUT/(name+'_dual_native.png')),(i*416,28))
    sheet.paste(captions.im,(0,0));save(sheet,'same_surface')
    # Exhaustive native label/hit geometry, legal-source domain and mode coverage.
    count=0
    for sigil in 'abcd':
        for mode in range(6):
            for sync in (False,True):
                for src in source_options(sigil):
                    s=State(sigil=sigil,mode=mode,sync=sync,mod_source=src)
                    top,bot=upper(s),lower(s);count+=1
                    labels={name for _,name in bot.labels}
                    assert set(MODE_LABELS)<=labels
                    assert not any('tools' in name or 'menu' in name or 'branch' in name for name,_ in bot.hits)
                    assert all('tools' not in name for name in labels)
                    assert -99<=output(s)<=99
                    if mode==5:assert output(s)==0
    for div in range(6):
        upper(State(sync=True,division=div,focus='rate'));lower(State(sync=True,division=div))
    for raw in (0,8192,16383,32767):
        upper(State(rate_raw=raw,focus='rate'));lower(State(rate_raw=raw))
    for value in range(-99,100):
        upper(State(depth=value,focus='depth'));lower(State(depth=value))
    base=lower(main_state)
    # Undisturbed stored drawing has EXACTLY the same position through UI focus.
    for focus in ('output','rate','depth','mod'):
        assert lower(replace(main_state,focus=focus)).im.tobytes()==base.im.tobytes()
    expected=Page(320);expected.text(300,230,'trig',SPOT,right=True)
    assert base.im.crop((245,222,308,240)).tobytes()==expected.im.crop((245,222,308,240)).tobytes()
    ref=Image.open(SOURCE/'shot_030_bot.png').convert('RGB')
    for width in (320,400):assert paper(width).crop((0,0,2,240)).tobytes()==ref.crop((0,0,2,240)).tobytes()
    assert before==protected_hashes()
    report={'status':'single-surface design proposal; not implemented, no controller/build/deployment',
            'supersedes':'menu-heavy ui_mockups/sigils_menu_study, left intact as history',
            'scope':'shape tools removed entirely from proposal; undo remains; all mode/timing/mod controls inline',
            'checks':{'state_combinations':count,'native_label_and_hit_bounds_no_overlap':True,
                      'all_six_modes_direct':True,'no_tools_or_modal_hits':True,'legal_sources_only':True,
                      'focus_does_not_move_bottom':True,'trigger_is_text_only':True,
                      'menu_paper_matches_hardware030':True,'native_palette_and_nearest2x':True,
                      'protected_runtime_binary_files_unchanged':len(before)},
            'controls':base.hits,
            'behavior':['mode row selects directly; full mode name above',
                        'free/sync toggles inline; drag same value/rail for rate or six divisions',
                        'tap rate source cycles none -> earlier sigils -> env -> none; drag adjacent amount',
                        'follow disables rate controls; free-rate modulation is paused for sync/follow',
                        'assign uses current selected primary parameter, matching existing routing semantics',
                        'upper inspector changes on touching a control, returns to output on release',
                        'y chains remains for choosing other modules; no claim of arbitrary route editing in this window'],
            'limitations':['static raster design, not implemented input handling',
                           'curve is screenshot tracing, not original Q15 table; all live values are illustrative',
                           'rand and smooth ignore stored curve, so do not show a fake curve-following marker',
                           'footer targets 18px tall and rate rail 18px: stylus sized, need real hardware validation',
                           'compact mode words and source cycling trade discovery for a single surface',
                           'mod-depth display is normalized -99..99, not a physical unit',
                           'no runtime shape-tools code removed yet; this phase is visual only']}
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    (OUT/'README.txt').write_text(__doc__+'\n'+json.dumps(report['behavior'],indent=2)+'\n')
    print(OUT/'main_dual_2x.png')
    print(OUT/'same_surface_2x.png')
    print(f'pass: {count} legal source/mode/clock combinations; all6 divisions; 199 depths; fixed bottom; no tools/no sun; protected hashes')


if __name__=='__main__':main()
