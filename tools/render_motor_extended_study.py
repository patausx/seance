#!/usr/bin/env python3
"""Motor extended UI proposal, NOT runtime, native UI capture or an audio upgrade.

Pillow composition at native LCD sizes, exact current motor title and 6x8 text.
Faders are rectangle submissions from the UNMODIFIED C++ module_fader helper.
Waveform and FFT share a deterministic, illustrative Python synthesis model;
that model is NOT MotorUnit, not an approved DSP implementation, and no audio
is exported. New controls, note/ARP support and gesture laws are design only.
"""
import argparse
import cmath
from dataclasses import dataclass, asdict
from datetime import datetime, timezone
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess

from PIL import Image, ImageDraw, PngImagePlugin
import render_module_editor_fullscreen_study as skin

base = skin.base
ROOT = Path(__file__).resolve().parents[1]
OUTSIDE = (18, 18, 21)
SR, FFT_SIZE = 32000, 8192


@dataclass(frozen=True)
class Example:
    slug: str
    source: str
    note: str
    hz: float
    arp: int
    width: float
    sync: float
    load: float
    jitter: float
    speed: float
    res: float
    gain: float
    selected: str

    @property
    def effective_hz(self):
        return self.hz * 2 ** (self.arp / 12)


EXAMPLES = (
    Example('sync', 'mix', 'a2', 110., 7, .32, 2.5, .38, .09, 1.2, .62, .68, 'hard sync'),
    Example('pwm', 'pulse', 'a1', 55., 0, .24, 1., .12, .04, .4, .42, .72, 'width'),
    Example('drift', 'saw', '', 88., 0, .50, 1., .72, .35, 3.2, .75, .64, 'jitter speed'),
)

# Read production geometry and palette directly through a tiny host bridge.
# This compiles no application, engine, controller or audio implementation.
CPP = r'''
#include "platform/3ds/module_fader.h"
#include "platform/3ds/module_frame_layout.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>
namespace m = seance::platform::ui3ds::module_fader;
namespace f = seance::platform::ui3ds::module_frame_layout;
namespace p = seance::platform::ui3ds::palette;
struct R { int x,y,w,h; uint32_t c; };
std::vector<R> rects;
namespace seance::ui {
void rect(float x,float y,float w,float h,uint32_t c) {
    rects.push_back({int(x),int(y),int(w),int(h),c});
}
void line(int,int,int,int,uint32_t,int) { std::abort(); }
}
int main(int argc, char** argv) {
    if(argc==2) {
        std::printf("{\"left\":%d,\"right\":%d,\"top\":%d,\"header_y\":%d,\"footer_y\":%d,\"route_y\":%d,\"underline_y\":%d}\n",
            f::kLeft,f::kRightInset,f::kTopInset,f::kHeaderTextY,f::kFooterTextY,f::kRouteY,f::kRouteUnderlineY);
        return 0;
    }
    int theme,x0,x1,y,edit,live,linked,disabled;
    while(std::cin>>theme>>x0>>x1>>y>>edit>>live>>linked>>disabled) {
        p::set(theme ? p::Theme::Pink : p::Theme::Graphite);
        rects.clear();
        m::needle_horizontal(x0,x1,y,edit,live,{bool(linked),bool(disabled),false});
        std::cout<<'[';
        for(size_t i=0;i<rects.size();++i) {
            const auto& r=rects[i];
            if(i)std::cout<<',';
            std::cout<<'['<<r.x<<','<<r.y<<','<<r.w<<','<<r.h<<','<<r.c<<']';
        }
        std::cout<<"]\n";
    }
}
'''


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def overlap(a, b):
    return min(a[2], b[2]) > max(a[0], b[0]) and min(a[3], b[3]) > max(a[1], b[1])


@lru_cache(None)
def title_mask():
    text = (ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw = text.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    bits = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', raw))
    row = re.search(r'\{UnitKind::Motor, "motor", (\d+), (\d+), (\d+)\}', text)
    offset, width, height = map(int, row.groups())
    image = Image.new('L', (width, height))
    for y in range(height):
        for x in range(width):
            if bits[offset+y*((width+7)//8)+x//8] & (128 >> (x % 8)):
                image.putpixel((x,y),255)
    assert width <= 57 and height <= 25
    return image


class NativeNeedles:
    def __init__(self, out):
        self.out = out
        folder = out/'helper'
        folder.mkdir()
        cpp, self.binary = folder/'needles.cpp', folder/'needles'
        cpp.write_text(CPP)
        cmd = ['g++','-std=c++17','-O2','-I'+str(ROOT),str(cpp),'-o',str(self.binary)]
        result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, check=True)
        (folder/'build.log').write_text(result.stdout+result.stderr)
        self.metrics = json.loads(subprocess.check_output([str(self.binary),'frame'],text=True))
        assert self.metrics == dict(left=14,right=14,top=8,header_y=14,footer_y=228,route_y=35,underline_y=45)
        self.records = []

    def draw(self, p, x0, x1, y, amount, disabled=False):
        at = x0 + round((x1-x0)*max(0.,min(1.,amount)))
        request = f'{int(p.theme=="pink")} {x0} {x1} {y} {at} {at} 0 {int(disabled)}\n'
        raw = subprocess.check_output([str(self.binary)],input=request,text=True)
        rects = json.loads(raw)
        for x,yy,w,h,color in rects:
            assert 14 <= x and x+w <= 307 and 8 <= yy and yy+h <= 224
            assert w > 0 and h > 0
            rgb = tuple((color >> shift) & 255 for shift in (0,8,16))
            p.d.rectangle((x,yy,x+w-1,yy+h-1),fill=rgb)
        self.records.append(dict(request=request.strip(),rectangles=rects))


def fft(values):
    """Radix-2 FFT for the illustration only; no third-party numeric dependency."""
    a = list(map(complex,values)); n = len(a)
    assert n and not(n & (n-1))
    j = 0
    for i in range(1,n):
        bit = n >> 1
        while j & bit:
            j ^= bit; bit >>= 1
        j ^= bit
        if i < j:
            a[i],a[j] = a[j],a[i]
    span = 2
    while span <= n:
        rotation = cmath.exp(-2j*math.pi/span)
        for start in range(0,n,span):
            w = 1+0j
            for k in range(span//2):
                even,odd = a[start+k],w*a[start+k+span//2]
                a[start+k],a[start+k+span//2] = even+odd,even-odd
                w *= rotation
        span *= 2
    return a


@lru_cache(None)
def signal(e):
    """Illustrative PWM+sync with motor-like satellites and resonator.

    No bit-exact equivalence to current MotorUnit is claimed. Frequency is
    jittered before a master phase; sync resets a higher-ratio slave. The source
    waveform uses that slave, with a variable duty cycle for pulse/mix only.
    """
    master = satellite_b = satellite_c = 0.
    body = previous = wander = target = 0.
    seed = 0x4d4f544f
    every = max(1,round(SR/e.speed))
    raw, out, wraps = [], [], []
    for i in range(2*SR):
        if i % every == 0:
            seed = (1664525*seed+1013904223) & 0xffffffff
            target = (seed/0xffffffff*2-1)*e.jitter*.32
        wander += (target-wander)/max(64.,SR*.08)
        delta = e.effective_hz*(1+wander)/SR
        old_cycle = int(master)
        master += delta
        if int(master) != old_cycle:
            wraps.append(i)
        phase = ((master % 1.)*e.sync) % 1.
        spread = (1+e.load*15)/512
        satellite_b = (satellite_b+delta*(1+spread)) % 1
        satellite_c = (satellite_c+delta*(1-spread)) % 1
        saw = phase*2-1
        pulse = 1. if phase < e.width else -1.
        sample = saw if e.source=='saw' else pulse if e.source=='pulse' else (saw+pulse)*.5
        sample += ((satellite_b*2-1)+(satellite_c*2-1))*e.load*.25
        raw.append(max(-1.,min(1.,sample)))
        damp = .125 + (1-e.res)*.25
        next_body = body+(sample-body)*damp+(body-previous)*e.res*.5
        previous,body = body,max(-1.,min(1.,next_body))
        out.append(body*e.gain)
    # Start at an actual model master wrap, not a decorative moving playhead.
    start,end = wraps[-4],wraps[-1]
    window = [.5-.5*math.cos(math.tau*i/(FFT_SIZE-1)) for i in range(FFT_SIZE)]
    tail = out[end-FFT_SIZE:end]
    mean = sum(tail)/len(tail)
    spectrum = fft([(v-mean)*w for v,w in zip(tail,window)])
    amplitudes = [2*abs(v)/sum(window) for v in spectrum[:FFT_SIZE//2+1]]
    return dict(raw=raw[start:end],out=out[start:end],milliseconds=(end-start)*1000/SR,
                amplitudes=amplitudes,window_end=end,source_sha256=hashlib.sha256(
                    json.dumps([raw,out],separators=(',',':')).encode()).hexdigest())


def page(theme, top, e, metrics):
    p = base.Surface(skin.paper(theme,'top' if top else 'bottom'),theme)
    left,right = metrics['left'],p.im.width-metrics['right']
    p.im.paste(p.c['accent'],(left,metrics['top']),title_mask())
    p.labels.append(((left,metrics['top'],left+title_mask().width,metrics['top']+title_mask().height),'motor / exact private mask'))
    if top:
        p.txt(right,metrics['header_y'],'ch01 / '+('02' if e.arp else '01'),'muted','right')
        if e.arp:
            p.txt(left,metrics['route_y'],'arp /','muted')
            p.txt(50,metrics['route_y'],'motor','accent')
            p.txt(92,metrics['route_y'],'/ filt','muted')
            p.line(50,metrics['underline_y'],78,metrics['underline_y'],'accent')
        else:
            p.txt(left,metrics['route_y'],'motor','accent')
            p.txt(56,metrics['route_y'],'/ filt / dly','muted')
            p.line(left,metrics['underline_y'],42,metrics['underline_y'],'accent')
    else:
        p.txt(102,metrics['header_y'],'ch01 / '+('02' if e.arp else '01'))
        p.txt(246,metrics['header_y'],'byp')
        p.txt(right,metrics['header_y'],'x','muted','right')
    return p


def plot(p, values, left, right, center, radius, role, thick=1):
    previous = None
    for x in range(left,right+1):
        a = (x-left)*len(values)/(right-left+1)
        b = (x-left+1)*len(values)/(right-left+1)
        selected = values[int(a):int(b)]
        if not selected:
            i = min(len(values)-1,int(a))
            j = min(len(values)-1,i+1)
            selected = [values[i]*(1-(a-i))+values[j]*(a-i)]
        low,high = min(selected),max(selected)
        yy = center-round(selected[-1]*radius)
        if previous is not None:
            p.line(x-1,previous,x,yy,role,thick)
        p.line(x,center-round(high*radius),x,center-round(low*radius),role,thick)
        previous = yy


def spectrum_curve(p, amplitudes):
    def at(freq):
        k = freq*FFT_SIZE/SR
        left,right = int(k),min(len(amplitudes)-1,int(k)+1)
        a = amplitudes[left]*(1-(k-left))+amplitudes[right]*(k-left)
        return max(-60.,min(0.,20*math.log10(max(1e-9,a))))
    last = None
    for x in range(32,387):
        f0 = 20*800**((x-32)/354)
        f1 = 20*800**(min(354,x-31)/354)
        lo = min(len(amplitudes)-1,math.ceil(f0*FFT_SIZE/SR))
        hi = min(len(amplitudes)-1,math.floor(f1*FFT_SIZE/SR))
        db = at(f0)
        if hi >= lo:
            db = max(db,20*math.log10(max(1e-9,max(amplitudes[lo:hi+1]))))
        y = 207-round(max(0.,min(1.,(db+60)/60))*31)
        if last is not None:
            p.line(x-1,last,x,y,'accent')
        last = y
    for freq,label in ((50,'50'),(200,'200'),(1000,'1k'),(5000,'5k'),(16000,'16k')):
        x = 32+round(math.log(freq/20,800)*354)
        p.txt(x,213,label,'muted','right' if freq==16000 else 'center')


def upper(theme,e,metrics):
    p = page(theme,True,e,metrics)
    data = signal(e)
    p.line(32,59,44,59,'muted'); p.txt(50,55,'raw')
    p.line(86,59,98,59,'accent',2); p.txt(104,55,'out','accent')
    selected = {'hard sync':f'sync {e.sync:.2f}x','width':f'width {e.width:.0%}',
                'jitter speed':f'jitter {e.speed:.1f}hz'}[e.selected]
    p.txt(386,55,selected,'ink','right')
    faint = base.mix(p.c['rule'],p.im.getpixel((200,106)),68)
    for y in (76,106,136):
        p.d.line((32,y,386,y),fill=faint)
    for x in (32,150,268,386):
        p.d.line((x,76,x,136),fill=faint)
    p.txt(14,73,'+1'); p.txt(20,103,'0'); p.txt(14,133,'-1')
    plot(p,data['raw'],32,386,106,29,'muted')
    plot(p,data['out'],32,386,106,29,'accent',2)
    p.txt(32,145,'0ms')
    p.txt(386,145,f"{data['milliseconds']:.1f}ms / 3 cycles",'muted','right')
    p.txt(14,163,'spectrum')
    p.txt(386,163,'-60..0dbfs','muted','right')
    p.d.line((32,207,386,207),fill=faint)
    spectrum_curve(p,data['amplitudes'])
    p.txt(14,metrics['footer_y'],'study model')
    now = 'e3' if e.arp else e.note
    readout = (now+' / ' if now else '')+f'{e.effective_hz:.1f}hz'
    p.txt(386,metrics['footer_y'],readout,'ink','right')
    return p


def lower(theme,e,native):
    p = page(theme,False,e,native.metrics)
    for source,x,bounds in (('saw',14,(8,30,44,57)),('pulse',52,(44,30,92,57)),('mix',102,(94,30,130,57))):
        p.txt(x,37,source,'accent' if source==e.source else 'muted')
        if source==e.source:
            p.line(x,49,x+len(source)*6-2,49,'accent')
        p.hit('source / '+source,bounds)
    for mode,x,bounds in (('free',198,(190,30,236,57)),('note',270,(254,30,312,57))):
        chosen = bool(e.note)==(mode=='note')
        p.txt(x,37,mode,'accent' if chosen else 'muted')
        if chosen:
            p.line(x,49,x+len(mode)*6-2,49,'accent')
        p.hit('tuning mode / '+mode,bounds)
    pitch = e.note or f'{e.hz:.1f}hz'
    controls = (
        ('pitch' if e.note else 'rate',pitch,(math.log2(e.hz/18)/math.log2(760/18)),False),
        ('hard sync',f'{e.sync:.2f}x' if e.sync>1 else 'off',math.log2(e.sync)/4,False),
        ('width',f'{e.width:.0%}',(e.width-.05)/.9,e.source=='saw'),
        ('load',f'{e.load:.0%}',e.load,False),
        ('jitter',f'{e.jitter:.0%}',e.jitter,False),
        ('jitter speed',f'{e.speed:.1f}hz',math.log(e.speed/.1)/math.log(125/.1),False),
        ('resonance',f'{e.res:.0%}',e.res,False),
        ('gain',f'{e.gain:.0%}',e.gain,False),
    )
    for i,(name,value,amount,disabled) in enumerate(controls):
        left,right = (14,148) if i%2==0 else (172,306)
        y = 72+(i//2)*40
        role = 'muted' if disabled else 'accent' if name==e.selected else 'ink'
        p.txt(left,y,name,role)
        p.txt(right,y,'--' if disabled else value,role,'right')
        native.draw(p,left,right,y+21,amount,disabled)
        p.hit(name+' / proposed relative drag',(left-6,y-10,right+7,y+28))
    p.txt(14,native.metrics['footer_y'],'arp +7st' if e.arp else 'b back')
    p.txt(306,native.metrics['footer_y'],'l: mod >','accent','right')
    return p


def dual(top,bottom):
    result = Image.new('RGB',(400,496),OUTSIDE)
    result.paste(top.im,(0,0)); result.paste(bottom.im,(40,256))
    return result


def save(out,name,image,scale=2):
    path = out/name
    assert not path.exists()
    large = image.resize((image.width*scale,image.height*scale),Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo(); meta.add_text('provenance',__doc__)
    large.save(path,pnginfo=meta)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes()==large.tobytes()
        assert decoded.resize(image.size,Image.Resampling.NEAREST).tobytes()==image.tobytes()
    return dict(path=str(path.relative_to(ROOT)),sha256=sha(path),size=list(large.size))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path)
    args = parser.parse_args()
    out = args.out or ROOT/'ui_mockups/motor_extended_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out = out.resolve(); out.mkdir(parents=True,exist_ok=False)
    before = base.protected()
    native = NativeNeedles(out)
    images,layouts,outputs = {},{},[]
    for theme in ('graphite','pink'):
        for e in EXAMPLES:
            top,bottom = upper(theme,e,native.metrics),lower(theme,e,native)
            for p in (top,bottom):
                for box,label in p.labels:
                    assert 14<=box[0]<box[2]<=p.im.width-14 and 8<=box[1]<box[3]<=236,(label,box)
            assert len(bottom.hits)==13
            key = theme+'-'+e.slug
            images[key] = (top,bottom)
            layouts[key] = dict(top_labels=top.labels,bottom_labels=bottom.labels,proposed_targets=bottom.hits)
            outputs.append(save(out,key+'-dual.png',dual(top,bottom)))
            outputs.append(save(out,key+'-top-native.png',top.im,1))
            outputs.append(save(out,key+'-bottom-native.png',bottom.im,1))
    board = base.Canvas(848,570,OUTSIDE)
    board.text(16,12,'motor / extended sound controls',base.COLORS['graphite']['ink'],scale=2)
    board.text(16,38,'two traces + spectrum / native-size controls',base.COLORS['graphite']['muted'])
    for i,theme in enumerate(('graphite','pink')):
        board.im.paste(dual(*images[theme+'-sync']),(16+416*i,56))
    board.text(16,558,'design study / synthetic signal / new synthesis not implemented',base.COLORS['graphite']['muted'])
    outputs.append(save(out,'both-themes.png',board.im))
    strip = base.Canvas(1040,306,OUTSIDE)
    strip.text(16,12,'motor / all eight controls, one page',base.COLORS['graphite']['ink'],scale=2)
    for i,e in enumerate(EXAMPLES):
        x = 16+i*344
        strip.text(x,40,{'sync':'hard sync / arp +7st','pwm':'pulse width / note mode','drift':'jitter speed / free tuning'}[e.slug],base.COLORS['graphite']['accent'])
        strip.im.paste(images['graphite-'+e.slug][1].im,(x,56))
    outputs.append(save(out,'controls-three-states.png',strip.im))
    assert base.protected()==before,'production/assets changed during study'
    report = dict(status='visual proposal only; no production approval implied',provenance=__doc__,
        created_at=datetime.now(timezone.utc).isoformat(),script_sha256=sha(__file__),
        frame=native.metrics,source_sha256=before,outputs=outputs,layouts=layouts,
        examples=[dict(**asdict(e),effective_hz=e.effective_hz,signal_sha256=signal(e)['source_sha256'],
                       displayed_ms=signal(e)['milliseconds']) for e in EXAMPLES],
        native_needles=dict(header_sha256=sha(ROOT/'platform/3ds/module_fader.h'),
                            driver_sha256=sha(out/'helper/needles.cpp'),binary_sha256=sha(native.binary),
                            submissions=native.records),
        signal=dict(origin='independent deterministic illustrative Python model, NOT production MotorUnit',
                    rate=SR,scope='three model master cycles; raw/out same sample window; fixed +/-1 scale',
                    spectrum='same model output; Hann 8192, mean removed, fixed -60..0 dBFS amplitude',
                    production_equivalence=False,live_telemetry=False,audio_export=False),
        design=dict(new=['pulse width in pulse/mix only','hard-sync slave/master ratio; off=1x',
                         'jitter target-update speed independent of depth','free/note and future ARP note acceptance'],
                    retained=['source saw/pulse/mix','load','jitter amount','res','gain','MOD entry','bypass/delete'],
                    arp='passive status of preceding arp card, not a second local arpeggiator',
                    labels='native 6x8; same unscaled current motor bitmap at14,8',
                    faders='exact compiled shared needle_horizontal submissions; no Pillow substitute'),
        checks=dict(native_duals=len(images),text_bounds_and_nonoverlap=True,proposed_hit_nonoverlap=True,
                    minimum_proposed_body_target=[32,24],exact_shared_needle_calls=len(native.records),
                    protected_unchanged=len(before),nearest_integer_enlargement=True),
        limitations=['no new runtime/controller or DSP implementation','no audio preview or listening evaluation',
                     'no native application capture, ARM build, FTP or device/FPS acceptance',
                     'parameter mappings and modern/legacy compatibility still require implementation design'])
    (out/'proposal.json').write_text(json.dumps(report,indent=2)+'\n')
    print(out.relative_to(ROOT))
    print('6 design duals / 48 exact native-helper needle calls / protected:',len(before))
    print(out/'both-themes.png')


if __name__=='__main__':
    main()
