#!/usr/bin/env python3
"""Ring extended design study; NOT runtime, production DSP or device capture.

Pillow composition at native LCD sizes using the current title mask and 6x8
font. Needles come from the unchanged C++ module_fader helper. Input is the
existing fixed Ring reference burst; carrier and output are an independent
illustrative Python model, NOT RingUnit. No audio export or shipping edits.
"""
import argparse
from dataclasses import asdict, dataclass, replace
from datetime import datetime, timezone
from functools import lru_cache
import json
import math
from pathlib import Path
import re
import subprocess

from PIL import Image, PngImagePlugin
import render_module_editor_fullscreen_study as skin
from render_motor_extended_study import sha

base = skin.base
ROOT = Path(__file__).resolve().parents[1]
OUTSIDE = (18, 18, 21)
SR = 32000


@dataclass(frozen=True)
class Example:
    slug: str
    shape: str
    note: str
    hz: float
    depth: float
    feedback: float
    diode: float
    follow: float
    release: float
    drive: float
    mix: float
    selected: str


EXAMPLES = (
    Example('clean', 'sine', 'a2', 110., 1., 0., 0., 0., .120, 0., 1., 'pitch'),
    Example('bridge', 'tri', '', 139.04, .85, -.25, 1., 0., .120, 6., .75, 'diode'),
    Example('envelope', 'pulse', 'a1', 55., 1., .20, .35, .8, .080, 9., .85, 'follow'),
)

# Only the production drawing helper and frame constants are compiled, not the
# application, controller or engine. Neutral tick is passed to the real helper.
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
struct Rect { int x,y,w,h; uint32_t color; };
std::vector<Rect> rects;
namespace seance::ui {
void rect(float x,float y,float w,float h,uint32_t color) {
    rects.push_back({int(x),int(y),int(w),int(h),color});
}
void line(int,int,int,int,uint32_t,int) { std::abort(); }
}
int main(int argc,char**) {
    if(argc==2) {
        std::printf("{\"left\":%d,\"right\":%d,\"top\":%d,\"header_y\":%d,\"footer_y\":%d,\"route_y\":%d,\"underline_y\":%d}\n",
            f::kLeft,f::kRightInset,f::kTopInset,f::kHeaderTextY,
            f::kFooterTextY,f::kRouteY,f::kRouteUnderlineY);
        return 0;
    }
    int theme,x0,x1,y,at,disabled,neutral;
    while(std::cin>>theme>>x0>>x1>>y>>at>>disabled>>neutral) {
        p::set(theme?p::Theme::Pink:p::Theme::Graphite);
        rects.clear();
        m::needle_horizontal(x0,x1,y,at,at,{false,bool(disabled),false},neutral);
        std::cout<<'[';
        for(size_t i=0;i<rects.size();++i) {
            const auto& r=rects[i];
            if(i)std::cout<<',';
            std::cout<<'['<<r.x<<','<<r.y<<','<<r.w<<','<<r.h<<','<<r.color<<']';
        }
        std::cout<<"]\n";
    }
}
'''


class Needles:
    def __init__(self, out):
        folder = out/'helper'
        folder.mkdir()
        cpp, self.binary = folder/'needles.cpp', folder/'needles'
        cpp.write_text(CPP)
        cmd = ['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I'+str(ROOT), str(cpp), '-o', str(self.binary)]
        result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
        (folder/'build.log').write_text(result.stdout+result.stderr)
        (folder/'command.json').write_text(json.dumps(cmd, indent=2)+'\n')
        result.check_returncode()
        self.metrics = json.loads(subprocess.check_output([str(self.binary), 'frame'], text=True))
        assert self.metrics == dict(left=14, right=14, top=8, header_y=14,
                                    footer_y=228, route_y=35, underline_y=45)
        self.records = []

    def draw(self, page, left, right, y, value, disabled=False, bipolar=False):
        # One pixel inside each label edge leaves room for the native 3px head
        # at both endpoints. No scale, substitute arrow or paper erasure.
        x0, x1 = left+1, right-1
        at = x0+round((x1-x0)*max(0., min(1., value)))
        neutral = (x0+x1)//2 if bipolar else -1
        request = f'{int(page.theme=="pink")} {x0} {x1} {y} {at} {int(disabled)} {neutral}\n'
        rects = json.loads(subprocess.check_output([str(self.binary)], input=request, text=True))
        for x, yy, w, h, color in rects:
            assert 14 <= x and x+w <= 307 and 26 <= yy and yy+h <= 224
            assert w > 0 and h > 0
            rgb = tuple((color >> shift) & 255 for shift in (0, 8, 16))
            page.d.rectangle((x, yy, x+w-1, yy+h-1), fill=rgb)
        self.records.append(dict(request=request.strip(), rectangles=rects))


@lru_cache(None)
def title_mask():
    text = (ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw = text.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    bits = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', raw))
    row = re.search(r'\{UnitKind::Ring, "ring", (\d+), (\d+), (\d+)\}', text)
    offset, width, height = map(int, row.groups())
    assert width <= 57 and height <= 25
    mask = Image.new('L', (width, height))
    for y in range(height):
        for x in range(width):
            if bits[offset+y*((width+7)//8)+x//8] & (128 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


@lru_cache(None)
def source():
    text = (ROOT/'platform/3ds/ring_reference_data.h').read_text()
    body = text.split('BURST[1024] = {', 1)[1].split('};', 1)[0]
    values = tuple(int(v)/32768 for v in re.findall(r'-?\d+', body))
    assert len(values) == 1024
    return values


@lru_cache(None)
def signal(e):
    """Illustration only: new waveform/drive/follower laws are not approved DSP.

    Immediate follower attack; exponential release to input magnitude; follow
    raises the base rate up to8x, capped12k. Signed, depth-scaled return and a
    bridge crossfade illustrate the existing topology. Float tanh is NOT the
    current fixed-point clipper. No bandlimiting or sound-quality claim.
    """
    phase = envelope = feedback = dc_x = dc_y = 0.
    carrier, output = [], []
    release = math.exp(-1/(SR*e.release))
    pole = math.exp(-math.tau*25/SR)
    for dry in source():
        if e.depth == 0:
            carrier.append(-1.)
            output.append(dry)
            continue
        magnitude = abs(dry)
        envelope = max(magnitude, magnitude+(envelope-magnitude)*release)
        hz = min(12000., e.hz*(1+7*envelope*e.follow))
        phase = (phase+hz/SR) % 1.
        c = (-math.cos(math.tau*phase) if e.shape == 'sine' else
             1-4*abs(phase-.5) if e.shape == 'tri' else
             -1. if phase < .5 else 1.)
        driven = dry*10**(e.drive/20) + feedback*.85*e.feedback*e.depth
        multiply = driven*c
        bridge = abs(driven+c)-abs(driven-c)
        full = multiply+(bridge-multiply)*e.diode
        wet = math.tanh(driven+(full-driven)*e.depth)
        highpass = wet-dc_x+pole*dc_y
        dc_x, dc_y = wet, max(-1., min(1., highpass))
        feedback = math.tanh(dc_y)
        final = dry+(feedback-dry)*e.mix
        carrier.append(c)
        output.append(max(-1., min(1., final)))
    assert all(math.isfinite(v) and -1 <= v <= 1 for v in carrier+output)
    return dict(input=source(), carrier=carrier, output=output)


def hz_text(value):
    return f'{value/1000:.2f}khz' if value >= 1000 else f'{value:.2f}'.rstrip('0').rstrip('.')+'hz'


def logarithmic(value, lo, hi):
    return max(0., min(1., math.log(max(lo, value)/lo)/math.log(hi/lo)))


def page(theme, top, e, metrics):
    p = base.Surface(skin.paper(theme, 'top' if top else 'bottom'), theme)
    left, right = metrics['left'], p.im.width-metrics['right']
    mask = title_mask()
    p.im.paste(p.c['accent'], (left, metrics['top']), mask)
    p.labels.append(((left, metrics['top'], left+mask.width, metrics['top']+mask.height),
                     'ring / exact current title'))
    if top:
        p.txt(right, metrics['header_y'], 'ch01 / 02', 'muted', 'right')
        p.txt(left, metrics['route_y'], 'play /')
        p.txt(56, metrics['route_y'], 'ring', 'accent')
        p.txt(86, metrics['route_y'], '/ rev')
        p.line(56, metrics['underline_y'], 78, metrics['underline_y'], 'accent')
        p.txt(right, metrics['route_y'], 'base carrier / '+hz_text(e.hz), 'ink', 'right')
    else:
        p.txt(78, metrics['header_y'], 'ch01 / 02')
        p.txt(258, metrics['header_y'], 'byp')
        p.txt(right, metrics['header_y'], 'x', 'muted', 'right')
        p.hit('bypass / blocked in lock mode', (246, 0, 284, 26))
    return p


def trace(p, values, center, role):
    left, right, radius = 76, 386, 17
    faint = base.mix(p.c['rule'], p.im.getpixel((200, center)), 65)
    p.d.line((left, center, right, center), fill=faint)
    previous = None
    for column in range(right-left+1):
        start = column*len(values)//(right-left+1)
        end = (column+1)*len(values)//(right-left+1)
        bucket = values[start:end]
        assert bucket
        ys = [center-round(v*radius) for v in bucket]
        x = left+column
        if previous is not None:
            p.line(x-1, previous, x, ys[0], role)
        p.line(x, min(ys), x, max(ys), role)
        previous = ys[-1]
    for x in (left, 231, right):
        p.d.point((x, center-radius-2), fill=faint)
        p.d.point((x, center+radius+2), fill=faint)


def upper(theme, e, metrics):
    p = page(theme, True, e, metrics)
    p.txt(14, 55, 'test 250hz / .75fs')
    p.txt(386, 55, '32ms reference', 'muted', 'right')
    data = signal(e)
    for key, name, y, role in (('input', 'input', 88, 'muted'),
                               ('carrier', 'carrier', 137, 'ink'),
                               ('output', 'out', 186, 'accent')):
        p.txt(14, y-4, name, role)
        trace(p, data[key], y, role)
    for x, label, align in ((76, '0', 'left'), (231, '16', 'center'), (386, '32ms', 'right')):
        p.txt(x, 212, label, 'muted', align)
    p.txt(14, metrics['footer_y'], 'study model / not live')
    p.txt(386, metrics['footer_y'], '+/-1fs', 'muted', 'right')
    return p


def lower(theme, e, native):
    p = page(theme, False, e, native.metrics)
    for name, x, bounds in (('sine', 14, (8, 30, 48, 57)),
                             ('tri', 62, (54, 30, 94, 57)),
                             ('pulse', 108, (100, 30, 146, 57))):
        chosen = name == e.shape
        p.txt(x, 37, name, 'accent' if chosen else 'muted')
        if chosen:
            p.line(x, 49, x+len(name)*6-2, 49, 'accent')
        p.hit('carrier / '+name, bounds)
    for name, x, bounds in (('free', 204, (196, 30, 240, 57)),
                             ('note', 276, (266, 30, 312, 57))):
        chosen = bool(e.note) == (name == 'note')
        p.txt(x, 37, name, 'accent' if chosen else 'muted')
        if chosen:
            p.line(x, 49, x+len(name)*6-2, 49, 'accent')
        p.hit('base tuning / '+name, bounds)
    controls = (
        ('pitch' if e.note else 'rate', e.note or hz_text(e.hz),
         logarithmic(e.hz, 32.7032, 2093.005) if e.note else logarithmic(e.hz, .5, 8189.749), False, False),
        ('depth', f'{e.depth:.0%}', e.depth, False, False),
        ('feedback', f'{e.feedback:+.0%}' if e.feedback else '0%', (e.feedback+1)/2, False, True),
        ('diode', f'{e.diode:.0%}', e.diode, False, False),
        ('follow', f'{e.follow:.0%}', e.follow, False, False),
        ('release', f'{e.release*1000:.0f}ms', logarithmic(e.release, .001, 1.), e.follow == 0, False),
        ('drive', f'{e.drive:.0f}db', e.drive/18, False, False),
        ('mix', f'{e.mix:.0%}', e.mix, False, False),
    )
    for i, (name, value, amount, disabled, bipolar) in enumerate(controls):
        left, right = (14, 148) if i % 2 == 0 else (172, 306)
        y = 72+i//2*40
        role = 'muted' if disabled else 'accent' if name == e.selected else 'ink'
        p.txt(left, y, name, role)
        p.txt(right, y, value, role, 'right')
        native.draw(p, left, right, y+21, amount, disabled, bipolar)
        p.hit(name+' / proposed relative drag', (left-6, y-10, right+7, y+28))
    p.txt(14, native.metrics['footer_y'], 'b back')
    p.txt(306, native.metrics['footer_y'], 'l: mod >', 'accent', 'right')
    assert len(p.hits) == 14
    return p


def dual(top, bottom):
    image = Image.new('RGB', (400, 496), OUTSIDE)
    image.paste(top.im, (0, 0))
    image.paste(bottom.im, (40, 256))
    return image


def save(out, name, image, scale=2):
    path = out/name
    assert not path.exists()
    large = image.resize((image.width*scale, image.height*scale), Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    large.save(path, pnginfo=meta)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes() == large.tobytes()
        assert decoded.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    return dict(path=str(path.relative_to(ROOT)), sha256=sha(path), size=list(large.size))


def protected():
    result = dict(base.protected(), Makefile=sha(ROOT/'Makefile'))
    for path in sorted(ROOT.glob('seance.*')):
        if path.is_file():
            result[str(path.relative_to(ROOT))] = sha(path)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    out = (args.out or ROOT/'ui_mockups/ring_extended_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')).resolve()
    out.mkdir(parents=True, exist_ok=False)
    before = protected()
    native = Needles(out)
    images, layouts, outputs = {}, {}, []
    for theme in ('graphite', 'pink'):
        for e in EXAMPLES:
            top, bottom = upper(theme, e, native.metrics), lower(theme, e, native)
            for p in (top, bottom):
                for box, label in p.labels:
                    assert 14 <= box[0] < box[2] <= p.im.width-14 and 8 <= box[1] < box[3] <= 236, (label, box)
            key = theme+'-'+e.slug
            images[key] = (top, bottom)
            layouts[key] = dict(top_labels=top.labels, bottom_labels=bottom.labels, proposed_targets=bottom.hits)
            outputs.append(save(out, key+'-dual.png', dual(top, bottom)))
            outputs.append(save(out, key+'-top-native.png', top.im, 1))
            outputs.append(save(out, key+'-bottom-native.png', bottom.im, 1))
    board = base.Canvas(848, 584, OUTSIDE)
    board.text(16, 12, 'ring / extended', base.COLORS['graphite']['ink'], scale=2)
    board.text(16, 38, 'input - carrier - output / eight native needle controls', base.COLORS['graphite']['muted'])
    for i, theme in enumerate(('graphite', 'pink')):
        board.text(16+i*416, 57, theme, base.COLORS['graphite']['accent'])
        board.im.paste(dual(*images[theme+'-clean']), (16+i*416, 72))
    board.text(16, 574, 'design only / signal illustration, not production dsp or live audio', base.COLORS['graphite']['muted'])
    outputs.append(save(out, 'both-themes.png', board.im))
    strip = base.Canvas(1040, 306, OUTSIDE)
    strip.text(16, 12, 'ring / three sound directions', base.COLORS['graphite']['ink'], scale=2)
    for i, e in enumerate(EXAMPLES):
        strip.text(16+i*344, 40, {'clean': 'sine / note', 'bridge': 'tri / diode bridge',
                                  'envelope': 'pulse / envelope follow'}[e.slug], base.COLORS['graphite']['accent'])
        strip.im.paste(images['graphite-'+e.slug][1].im, (16+i*344, 56))
    outputs.append(save(out, 'three-main-states.png', strip.im))
    for e in EXAMPLES:
        assert signal(replace(e, depth=0))['output'] == list(source())
        assert signal(replace(e, mix=0))['output'] == list(source())
    assert protected() == before, 'production/assets/artifacts changed during study'
    sources = {str(p.relative_to(ROOT)): sha(p) for p in sorted((ROOT/'tools').glob('render_*study.py'))}
    report = dict(status='visual proposal only; awaiting approval', provenance=__doc__,
        created_at=datetime.now(timezone.utc).isoformat(), script_sha256=sha(__file__),
        frame=native.metrics, protected_sha256=before, study_sources_sha256=sources,
        examples=[asdict(e) for e in EXAMPLES], outputs=outputs, layouts=layouts,
        native_needles=dict(header_sha256=sha(ROOT/'platform/3ds/module_fader.h'),
            driver_sha256=sha(out/'helper/needles.cpp'), binary_sha256=sha(native.binary), submissions=native.records),
        model=dict(input='existing fixed 1024-sample 250hz/.75fs reference burst',
            carrier_output='independent Python illustration, not current or approved RingUnit DSP',
            scales='same 32ms window and +/-1 scale on all three traces; column extrema and continuity',
            limitations='no live PCM, pitch tracking, FFT, meter, antialias or listening claim; slow rates do not complete a cycle'),
        design=dict(main=['carrier shape', 'free/note', 'rate/pitch', 'depth', 'feedback', 'diode', 'follow', 'release', 'drive', 'mix'],
            new_slots=['drive=6', 'follow release=7', 'wave=8', 'note=9', 'mode=10'],
            note='stored chromatic carrier C1..C7 only; not pitch tracking or ARP targeting',
            compatibility='proposed legacy/modern separation and explicit conversion; not implemented'),
        checks=dict(authored_duals=len(images), text_bounds_and_nonoverlap=True,
            proposed_targets_disjoint=True, minimum_target=[32, 24], exact_shared_needle_calls=len(native.records),
            illustration_dry_cases=6, protected_unchanged=len(before), nearest_integer_enlargement=True),
        limitations=['no application/DSP/input implementation or current native UI capture',
                     'no ARM build, 3dsx, FTP, device, audio export or FPS proof',
                     'this is not implementation approval or a finalized sound/persistence contract'])
    (out/'proposal.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'README.md').write_text('# ring extended — design only\n\n'
        'authored Pillow composition, not runtime/device capture. exact current ring title and native6x8 text, '
        'unchanged C++ needle helper; fixed shared frame14,8/footer228.\n\n'
        '- `both-themes.png`: same proposed editor in graphite/pink.\n'
        '- `three-main-states.png`: sine/note, triangle/bridge, pulse/follow.\n'
        '- individual duals are2x; native surfaces are400x240 and320x240.\n\n'
        'input is the existing fixed burst. carrier/output are a new independent illustrative model, '
        'not RingUnit, live audio or a promised sound. the32ms view does not show a complete slow cycle. '
        'note tunes the carrier manually, not ARP or pitch tracking. release means follower release, '
        'not amplitude envelope. new engine, persistence and controllers remain unimplemented.\n\n'
        'proposal.json records native helper submissions, protected hashes and geometry checks. '
        'no production, assets or console artifacts were changed. await design approval.\n')
    print(out.relative_to(ROOT))
    print(f'{len(images)} design duals / {len(native.records)} native needle calls / {len(before)} protected files unchanged')
    print(out/'both-themes.png')


if __name__ == '__main__':
    main()
