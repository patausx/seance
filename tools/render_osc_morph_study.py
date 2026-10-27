#!/usr/bin/env python3
"""Authored OSC shape-morph proposal, not runtime, hardware, or shipped DSP.

The plot uses a Python transcription of the proposed adjacent-anchor interpolation
followed by the current OSC fold/sub/gain integer arithmetic. It is a normalized
reset-phase two-cycle reference, never live PCM or an engine-phase claim.
"""
from dataclasses import dataclass
from pathlib import Path
import cmath
import hashlib
import json
import math
from PIL import Image
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import INK, SPOT, OUTSIDE
from render_fold_wave_study import numeric, rail, diamond

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/osc_morph_study'
PROTECTED = (
    'core/engine/units.h', 'core/engine/units.cpp', 'core/engine/patch_io.cpp',
    'core/engine/engine.h', 'core/engine/engine.cpp',
    'platform/3ds/ui_module_focus_osc.cpp',
    'platform/3ds/ui_module_focus_internal.h',
    'platform/3ds/module_inspector.cpp', 'platform/3ds/ui_param_lens.cpp',
    'platform/3ds/ui_editor.cpp', 'platform/3ds/main.cpp',
    'core/ui/font.cpp', 'assets/fonts/Gossip-MedPixel.otf',
    'release/gran-classic/seance.3dsx', 'release/samples-go-to/seance.3dsx',
)
ANCHORS = (0, 10922, 21845, 32767)
SHAPES = ('sine', 'tri', 'saw', 'pulse')
X0, X1, ZERO, AMP = 32, 230, 94, 25
MODEL_SAMPLES = (X1-X0+1)*8


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity():
    return {name: sha(ROOT/name) for name in PROTECTED}


def raw_shape(position):
    return round(max(0.0, min(3.0, position))*32767/3)


def cdiv(value, divisor):
    """C++ signed integer division: truncate toward zero."""
    return abs(value)//divisor * (-1 if value < 0 else 1)


def sat(value):
    return max(-32768, min(32767, value))


def base_wave(kind, phase, width):
    phase &= 0xffffffff
    p = phase >> 16
    if kind == 2:
        return p-32768
    if kind == 3:
        threshold = 0x40000000 + width*32768
        return -32767 if phase < threshold else 32767
    tri = p*2-32768 if p < 0x8000 else 32767-(p-32768)*2
    if kind == 1:
        return tri
    value = (tri*(65536-abs(tri))) >> 15
    return sat(value)


def morphed(shape, phase, width):
    if shape <= ANCHORS[1]:
        left, right, amount, span = 0, 1, shape, ANCHORS[1]
    elif shape <= ANCHORS[2]:
        left, right = 1, 2
        amount, span = shape-ANCHORS[1], ANCHORS[2]-ANCHORS[1]
    else:
        left, right = 2, 3
        amount, span = shape-ANCHORS[2], ANCHORS[3]-ANCHORS[2]
    a, b = base_wave(left, phase, width), base_wave(right, phase, width)
    return a+cdiv((b-a)*amount, span)


def processed(state, index, count):
    # Two main-oscillator cycles and exactly one octave-down sub cycle.
    total_phase = index*(2 << 32)//count
    phase = total_phase & 0xffffffff
    source = morphed(state.live, phase, state.width)
    shaped = (source*(32767+state.fold*6)) >> 15
    for _ in range(4):
        if -32768 <= shaped <= 32767:
            break
        if shaped > 32767:
            shaped = 65534-shaped
        if shaped < -32768:
            shaped = -65536-shaped
    shaped = sat(shaped)
    sub_phase = total_phase >> 1
    sub_wave = 32767 if sub_phase & 0x80000000 else -32767
    shaped = sat(shaped+((sub_wave-shaped)*state.sub >> 15))
    return source, (shaped*state.gain) >> 15


def shape_name(raw):
    if raw in ANCHORS:
        return SHAPES[ANCHORS.index(raw)]
    for i in range(3):
        if raw < ANCHORS[i+1]:
            amount = round((raw-ANCHORS[i])*100/(ANCHORS[i+1]-ANCHORS[i]))
            return f'{SHAPES[i]} > {SHAPES[i+1]} {amount}%'
    return 'pulse'


def shape_number(raw):
    for i in range(3):
        if raw <= ANCHORS[i+1]:
            return i+(raw-ANCHORS[i])/(ANCHORS[i+1]-ANCHORS[i])
    return 3.0


def cents(raw):
    return round(raw*27/32767)


def fold_x(raw):
    return 1+raw*6/32767


def gain_db(raw):
    return 'mute' if raw <= 0 else f'{20*math.log10(raw/32767):.1f}db'


def midi_note(raw):
    midi = round(24+raw*72/32767)
    notes = ('c','c#','d','d#','e','f','f#','g','g#','a','a#','b')
    return f'{notes[midi%12]}{midi//12-1}', 440*2**((midi-69)/12)


@dataclass(frozen=True)
class State:
    name: str
    base: int
    live: int
    width: int = 16384
    fold: int = 0
    sub: int = 0
    drift: int = 0
    gain: int = 26000
    pitch: int = 10922
    source: str = 'off'
    depth: int = 0
    lock: bool = False


CASES = (
    State('01_sine_tri', raw_shape(.62), raw_shape(.62), gain=28000),
    State('02_sigil_tri_saw', raw_shape(1.35), raw_shape(1.72), fold=12000,
          sub=6200, drift=11500, gain=25000, source='sg.a', depth=58),
    State('03_saw_pulse', raw_shape(2.30), raw_shape(2.78), width=6553,
          fold=19660, sub=13107, drift=21298, gain=27852, source='sg.c', depth=35),
)


def model(state, count=MODEL_SAMPLES):
    values = [processed(state, i, count) for i in range(count)]
    return [v[0] for v in values], [v[1] for v in values]


def harmonic_bars(output):
    # The two-cycle window resolves the octave-down component as bin 1 and the
    # main oscillator as bin 2. Fixed magnitude scale is 0..1.30FS, never AGC.
    n = len(output)
    bars = []
    for harmonic_bin in (1,2,4,6,8,10,12,14):
        total = sum(value*cmath.exp(-2j*math.pi*harmonic_bin*i/n)
                    for i, value in enumerate(output))
        bars.append(2*abs(total)/n/32768)
    return bars


def waveform(c, state):
    source, output = model(state)
    c.text(14,54,'two-cycle',GHOST)
    c.line((79,58,89,58),GHOST); c.text(94,54,'morph',GHOST)
    c.line((142,58,152,58),SPOT); c.text(157,54,'out',SPOT)
    c.text(306,54,'partials',GHOST,right=True)
    for raw, label in ((32767,'+1'),(0,'0'),(-32768,'-1')):
        y = ZERO-round(raw*AMP/32768)
        c.text(14,y-4,label,GHOST)
        c.line((X0,y,X1,y),BRANCH)
    for values, color in ((source,GHOST),(output,SPOT)):
        for x in range(X0,X1+1):
            a=(x-X0)*len(values)//(X1-X0+1)
            b=(x-X0+1)*len(values)//(X1-X0+1)
            lo,hi=min(values[a:b]),max(values[a:b])
            y0=ZERO-round(hi*AMP/32768);y1=ZERO-round(lo*AMP/32768)
            assert 65 <= y0 <= y1 <= 122
            c.line((x,y0,x,y1),color)
    bars=harmonic_bars(output)
    c.line((247,119,306,119),BRANCH)
    c.line((247,81,306,81),BRANCH)  # 1.0FS on fixed 1.30FS scale
    for i,value in enumerate(bars):
        height=round(min(1.3,value)*49/1.3)
        x=249+i*7
        if height:
            c.d.rectangle((x,119-height,x+3,118),fill=SPOT if i in (0,1) else INK)
    return bars


def control(c, index, name, value, position):
    x=14+(index%3)*104; y=164+(index//3)*33
    c.text(x,y,name,INK)
    c.text(x+84,y+12,value,GHOST,right=True)
    rail(c,x,x+84,y+26,position,INK,bipolar=False)
    top=160 if index<3 else 194
    bottom=194 if index<3 else 227
    c.hit(name+' / relative horizontal drag',(x-6,top,x+90,bottom))


def lower(state):
    c=Page()
    c.text(14,9,'ch01 / 01',GHOST)
    c.text(205,9,'byp',GHOST);c.text(244,9,'x del',GHOST);c.text(292,9,'qnt',SPOT)
    c.hit('existing bypass',(200,3,240,21));c.hit('existing pitch quantize',(284,3,316,21))
    c.heading(14,29,'osc',INK)
    bars=waveform(c,state)
    c.text(14,126,'shape',SPOT)
    c.text(306,126,shape_name(state.live),SPOT,right=True)
    c.line((14,144,306,144),BRANCH)
    for raw,name in zip(ANCHORS,SHAPES):
        x=14+raw*292//32767
        c.line((x,141,x,147),BRANCH)
        c.text(max(14,min(306-len(name)*6,x-len(name)*3)),150,name,GHOST)
    base_x=14+state.base*292//32767;live_x=14+state.live*292//32767
    diamond(c,base_x,144,INK,True);diamond(c,live_x,144,SPOT,False)
    c.hit('shape / continuous relative drag; tap anchor; selects mod-temp',(8,122,312,160))
    note,hz=midi_note(state.pitch)
    values=(
        ('pitch',f'{note} / {hz:.1f}hz',state.pitch/32767),
        ('width',f'{25+state.width*50/32767:.0f}%',state.width/32767),
        ('fold',f'{fold_x(state.fold):.2f}x',state.fold/32767),
        ('sub',f'{state.sub*100/32767:.0f}% / -1',state.sub/32767),
        ('drift',f'+/-{cents(state.drift)}ct',state.drift/32767),
        ('gain',gain_db(state.gain),state.gain/32767),
    )
    for i,args in enumerate(values):control(c,i,*args)
    c.line((4,228,316,228),BRANCH)
    c.text(5,231,'normalized model / not live pcm',GHOST)
    c.text(306,231,'mod / temp >',SPOT,right=True)
    c.hit('selected shape mod / temp',(214,229,314,240))
    return c,bars


def upper(state):
    c=Page(400)
    c.text(14,10,'osc',SPOT);c.line((14,22,31,22),SPOT)
    c.text(42,10,'/ filter / vca / out',GHOST)
    c.text(386,10,'ch01 / 01',GHOST,right=True)
    c.text(14,41,'shape / oscillator morph',SPOT)
    c.text(14,56,'base',GHOST)
    numeric(c,f'{shape_number(state.base):.2f}')
    c.text(14,150,shape_name(state.base),GHOST)
    c.text(14,166,'stored base / exact anchor-compatible',GHOST)
    c.line((239,48,239,211),BRANCH)
    c.text(254,46,'effective',GHOST)
    c.text(254,60,f'{shape_number(state.live):.2f}',SPOT,scale=2)
    c.text(254,80,shape_name(state.live),SPOT)
    c.text(254,97,f'control {shape_number(state.live):.2f}',INK)
    c.text(254,111,f'target  {shape_number(state.live):.2f}',GHOST)
    c.text(254,132,state.source,SPOT if state.source!='off' else INK)
    c.text(254,146,f'depth {state.depth:+d}%',GHOST)
    c.text(254,165,'potential range',GHOST)
    c.text(254,179,'0.00 sine',INK)
    c.text(254,193,'3.00 pulse',INK)
    c.line((14,184,224,184),BRANCH)
    for raw,name in zip(ANCHORS,SHAPES):
        x=14+raw*210//32767
        c.line((x,181,x,187),BRANCH)
        c.text(max(14,min(224-len(name)*6,x-len(name)*3)),199,name,GHOST)
    diamond(c,14+state.base*210//32767,184,INK,True)
    diamond(c,14+state.live*210//32767,184,SPOT,False)
    c.text(14,217,'pre-fold morph / exact anchors / range clamps',GHOST)
    c.text(14,230,'sigil moves effective shape continuously / no wrap',GHOST)
    return c


def main():
    before=identity();OUT.mkdir(parents=True,exist_ok=True)
    receipt=dict(
        origin='authored Pillow native-pixel proposal, NOT C++ runtime/LCD capture',
        proposed_sound='adjacent sine-tri-saw-pulse linear interpolation before existing fold/sub/gain',
        shape_anchors_q15=list(ANCHORS),
        waveform='normalized reset-phase two-cycle proposed model; fixed +/-1FS; 8 samples/column; no AGC/live PCM',
        partials='DFT bins: sub fundamental, main fundamental, main harmonics 2..7; fixed 0..1.30FS magnitude scale',
        compatibility=['legacy discrete Wave remains exact until explicit morph enable',
                       'new parameter appends after existing six; UNIT bit opts in',
                       'old cards initialize shape to their exact stored wave anchor without an audible jump',
                       'tap/drag shape explicitly enables morph; new OSC cards start morph-enabled'],
        controls=['shape continuous rail with exact anchors and base/effective markers',
                  'pitch','width 25..75%','fold 1..7x','sub octave -1','drift +/-0..27 cents','gain'],
        semantics=['width influences the pulse component only, progressively during saw-to-pulse morph',
                   'sigil/p-lock/temp target shape like an ordinary clamped unipolar parameter',
                   'no pulse-to-sine wrap at modulation range edges',
                   'thin morph trace is pre-fold source; violet out trace includes fold/sub/gain',
                   'drift is numeric cents extent, not a fake moving waveform'],
        limits=['no runtime, DSP, engine, serializer, font, sample, build, FTP or installed artifact changes',
                'proposed arithmetic is not yet compiled C++ proof',
                'spectrum is a reset reference, not measured chain audio or anti-alias analysis'],
        protected_sha256=before,frames=[])
    sheet=Image.new('RGB',(len(CASES)*416-16,480),OUTSIDE)
    for i,state in enumerate(CASES):
        bottom,bars=lower(state);top=upper(state)
        pair=Image.new('RGB',(400,480),OUTSIDE);pair.paste(top.im,(0,0));pair.paste(bottom.im,(40,240))
        pair.save(OUT/f'{state.name}_dual_native.png')
        pair.resize((800,960),Image.Resampling.NEAREST).save(OUT/f'{state.name}_dual_2x.png')
        bottom.im.save(OUT/f'{state.name}_bottom_native.png')
        top.im.save(OUT/f'{state.name}_top_native.png')
        sheet.paste(pair,(i*416,0))
        receipt['frames'].append(dict(name=state.name,base_q15=state.base,effective_q15=state.live,
            base_shape=shape_name(state.base),effective_shape=shape_name(state.live),source=state.source,
            depth_percent=state.depth,partial_magnitudes_fixed_fs=bars,
            bottom_labels=bottom.labels,top_labels=top.labels,proposed_hits=bottom.hits))
    sheet.save(OUT/'comparison_native.png')
    sheet.resize((sheet.width*2,960),Image.Resampling.NEAREST).save(OUT/'comparison_2x.png')
    assert identity()==before
    (OUT/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('pass: three authored OSC shape-morph duals; proposed integer model and fixed-scale partials')
    print('runtime/DSP/patch/font/artifacts unchanged; no build or deployment')
    print(OUT/'comparison_2x.png')


if __name__=='__main__':main()
