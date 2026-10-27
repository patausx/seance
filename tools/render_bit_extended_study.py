#!/usr/bin/env python3
"""Bit extended visual proposal, NOT runtime UI, BitUnit audio or device capture.

Native-size Pillow composition; exact current title and 6x8 text, shared frame.
Needles are submissions from the unchanged C++ module_fader helper. The traces,
capture ticks and dropout gate share an independent deterministic Python model.
All new DSP, controls, bank-safe gestures and migration remain proposals only.
"""
import argparse
from dataclasses import asdict, dataclass, replace
from datetime import datetime, timezone
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import re

from PIL import Image, PngImagePlugin
import render_module_editor_fullscreen_study as skin
from render_motor_extended_study import NativeNeedles, sha

base = skin.base
ROOT = Path(__file__).resolve().parents[1]
OUTSIDE = (18, 18, 21)
SR, WINDOW, WARMUP = 32000, 1024, 16000


@dataclass(frozen=True)
class Example:
    slug: str
    bits: int
    rate: float
    drive: float
    tone: float
    jitter: float
    drop: float
    mix: float
    gain: float
    drift: float
    comp: float
    dither: float
    drop_time: float
    selected: str


EXAMPLES = (
    Example('crunch', 4, 2400, 6, 9000, .12, 0, .85, -6, 0, .10, .20, .012, 'bits'),
    Example('broken', 3, 1800, 12, 4800, .38, .10, 1, -6, -.45, .35, 0, .018, 'dropout'),
    Example('clean', 16, 32000, 0, 0, 0, 0, 1, 0, 0, 0, 0, .012, 'rate'),
)
PARAMETERS = ['bits', 'rate', 'mix', 'jitter', 'dropout', 'drift', 'comp curve',
              'drive', 'tone', 'wet gain', 'dither', 'drop length']


@lru_cache(None)
def title_mask():
    text = (ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw = text.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    bits = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', raw))
    row = re.search(r'\{UnitKind::Bitcrush, "bit", (\d+), (\d+), (\d+)\}', text)
    offset, width, height = map(int, row.groups())
    assert (width, height) == (23, 19)
    mask = Image.new('L', (width, height))
    for y in range(height):
        for x in range(width):
            if bits[offset+y*((width+7)//8)+x//8] & (128 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


def clamp(value, lo=0., hi=1.):
    return max(lo, min(hi, value))


def log_position(value, lo, hi):
    return clamp(math.log(max(lo, value)/lo)/math.log(hi/lo))


def frequency(value):
    return f'{value/1000:.1f}khz' if value >= 1000 else f'{value:.0f}hz'


class Random:
    def __init__(self, seed):
        self.state = seed

    def unit(self):
        x = self.state
        x ^= (x << 13) & 0xffffffff
        x ^= x >> 17
        x ^= (x << 5) & 0xffffffff
        self.state = x & 0xffffffff
        return self.state / 4294967296.


def quantize(value, bits):
    # Design choice: sign-magnitude 1-bit; symmetric rounding for 2..15;
    # 16-bit endpoint preserves the already-q15 reference source exactly.
    value = clamp(value, -1., 1.)
    if bits == 16:
        return value
    if bits == 1:
        return 0. if value == 0 else math.copysign(1., value)
    step = 2. / (2**bits)
    result = math.copysign(math.floor(abs(value)/step+.5)*step, value)
    return clamp(result, -1., 1.-step)


@lru_cache(None)
def signal(e):
    """Independent illustrative DSP; no equivalence to production BitUnit.

    500ms warmup, then exactly32ms of the same fixed q15 125Hz triangle.
    Fractional sample deadlines carry residual across captures. Jitter changes
    each capture interval, drift0 is neutral. Drop chance applies only while
    no fixed-length event is active. Endpoint drop1 forces wet silence after
    filters; mix0 exact dry. Clean neutral16/32k explicitly passes input.
    """
    clock_rng, drop_rng, noise_rng = Random(0xB17C0DE), Random(0xD09F0A17), Random(0xD17E1234)
    left = 0.
    held = low = old_wet = dc = 0.
    remaining = 0
    drive, gain = 10**(e.drive/20), 10**(e.gain/20)
    alpha = 1-math.exp(-math.tau*e.tone/SR) if e.tone else 1.
    pole = math.exp(-math.tau*8/SR)
    neutral = (e.bits == 16 and e.rate == SR and e.drive == e.tone == e.jitter ==
               e.drop == e.drift == e.comp == e.dither == 0)
    records = dict(source=[], output=[], captures=[], dropout=[])
    for i in range(WARMUP+WINDOW):
        t = (i % 256)/256
        source = round((1-4*abs(t-.5))*.625*32768)/32768
        capture = left <= 0.
        if capture:
            nominal = e.rate*(1+.125*math.sin(math.tau*e.drift*i/SR)) if e.drift else e.rate
            interval = SR/nominal*(1+(clock_rng.unit()*2-1)*e.jitter*.5)
            left += max(1., interval)
            wet = clamp(source*drive, -1., 1.)
            if e.dither and e.bits < 16:
                wet += (noise_rng.unit()-noise_rng.unit())*(2./2**e.bits)*e.dither
            held = quantize(wet, e.bits)
            if not remaining and e.drop > 0 and drop_rng.unit() < e.drop:
                remaining = max(1, round(e.drop_time*SR))
        left -= 1.
        dropped = e.drop >= 1 or remaining > 0
        if remaining:
            remaining -= 1
        wet = 0. if dropped else held
        compressed = wet*(1+e.comp)/(1+abs(wet)*e.comp)
        wet += (compressed-wet)*e.comp
        low += alpha*(wet-low)
        dc = low-old_wet+pole*dc
        old_wet = low
        wet = 0. if dropped else source if neutral else dc
        wet *= gain
        output = source if e.mix == 0 else wet if e.mix == 1 else source+(wet-source)*e.mix
        if i >= WARMUP:
            records['source'].append(source)
            records['output'].append(clamp(output, -1., 1.))
            records['captures'].append(int(capture))
            records['dropout'].append(int(dropped))
    assert all(len(v) == WINDOW for v in records.values())
    records['sha256'] = hashlib.sha256(json.dumps(records, separators=(',', ':')).encode()).hexdigest()
    return records


def page(theme, top, e, metrics, setup=False):
    p = base.Surface(skin.paper(theme, 'top' if top else 'bottom'), theme)
    left, right = metrics['left'], p.im.width-metrics['right']
    mask = title_mask()
    p.im.paste(p.c['accent'], (left, metrics['top']), mask)
    p.labels.append(((left, metrics['top'], left+mask.width, metrics['top']+mask.height),
                     'bit / exact current title mask'))
    if top:
        p.txt(right, metrics['header_y'], 'ch01 / 02', 'muted', 'right')
        p.txt(left, metrics['route_y'], 'play /')
        p.txt(56, metrics['route_y'], 'bit', 'accent')
        p.txt(80, metrics['route_y'], '/ rev')
        p.line(56, metrics['underline_y'], 72, metrics['underline_y'], 'accent')
        p.txt(right, metrics['route_y'], f'{e.bits}bit / '+frequency(e.rate), 'ink', 'right')
    else:
        p.txt(64, metrics['header_y'], 'ch01 / 02')
        p.txt(206, metrics['header_y'], 'main' if setup else 'setup', 'accent')
        p.txt(258, metrics['header_y'], 'byp')
        p.txt(right, metrics['header_y'], 'x', 'muted', 'right')
        p.hit('main' if setup else 'setup', (198, 0, 246, 26))
    return p


def upper(theme, e, metrics):
    p = page(theme, True, e, metrics)
    model = signal(e)
    p.line(14, 59, 25, 59, 'muted')
    p.txt(31, 55, 'in')
    p.line(62, 59, 73, 59, 'accent')
    p.txt(79, 55, 'out', 'accent')
    p.txt(386, 55, '125hz test triangle', 'muted', 'right')
    x0, x1, y0, y1 = 34, 386, 77, 149
    zero = (y0+y1)//2
    faint = base.mix(p.c['rule'], p.im.getpixel((200, zero)), 62)
    for y in (y0, zero, y1):
        p.d.line((x0, y, x1, y), fill=faint)
    p.txt(14, y0, '+1')
    p.txt(14, zero-3, '0')
    p.txt(14, y1-7, '-1')
    for ms in (0, 8, 16, 24, 32):
        x = x0+round(ms/32*(x1-x0))
        p.d.line((x, y0, x, y1), fill=faint)
        p.txt(x, 157, str(ms)+('ms' if ms == 32 else ''), 'muted',
              'right' if ms == 32 else 'center' if ms else 'left')
    # Equal time/amplitude scales; per-pixel min/max keeps short holds visible.
    for key, role in (('source', 'muted'), ('output', 'accent')):
        previous = None
        for x in range(x0, x1+1):
            start = (x-x0)*WINDOW//(x1-x0+1)
            end = max(start+1, (x-x0+1)*WINDOW//(x1-x0+1))
            values = model[key][start:end]
            ys = [zero-round(v*(y1-y0)/2) for v in values]
            if previous is not None:
                p.line(x-1, previous, x, ys[0], role)
            p.line(x, min(ys), x, max(ys), role)
            previous = ys[-1]
    p.txt(14, 175, 'clock / dropout')
    p.txt(386, 175, f'{e.drop:.0%} / {e.drop_time*1000:.0f}ms', 'muted', 'right')
    p.d.line((x0, 193, x1, 193), fill=faint)
    p.d.line((x0, 211, x1, 211), fill=faint)
    for x in range(x0, x1+1):
        start = (x-x0)*WINDOW//(x1-x0+1)
        end = max(start+1, (x-x0+1)*WINDOW//(x1-x0+1))
        if any(model['captures'][start:end]):
            p.line(x, 190, x, 196, 'muted')
        if any(model['dropout'][start:end]):
            p.line(x, 207, x, 211, 'accent')
    p.txt(14, metrics['footer_y'], 'design model / not audio')
    p.txt(386, metrics['footer_y'], 'out --', 'muted', 'right')
    return p


def control(p, native, name, value, amount, left, right, y, selected=False):
    role = 'accent' if selected else 'ink'
    p.txt(left, y, name, role)
    p.txt(right, y, value, role, 'right')
    # Leave the helper's real three-pixel endpoint needle inside the14px frame.
    # This is proposed local rail geometry, never clipping or altering the helper.
    native.draw(p, left+1, right-1, y+20, amount)
    p.hit(name+' / proposed relative drag', (left-6, y-5, right+6, y+30))


def lower(theme, e, native):
    p = page(theme, False, e, native.metrics)
    controls = (
        ('bits', f'{e.bits}bit', (e.bits-1)/15),
        ('rate', frequency(e.rate), log_position(e.rate, 100, 32000)),
        ('drive', f'{e.drive:+.0f}db', e.drive/24),
        ('tone', frequency(e.tone) if e.tone else 'off', log_position(e.tone, 100, 14000) if e.tone else 1),
        ('jitter', f'{e.jitter:.0%}', e.jitter),
        ('dropout', f'{e.drop:.0%}', e.drop),
        ('mix', f'{e.mix:.0%}', e.mix),
        ('wet gain', f'{e.gain:+.0f}db', (e.gain+24)/30),
    )
    for i, (name, value, amount) in enumerate(controls):
        left, right = (14, 148) if i % 2 == 0 else (172, 306)
        control(p, native, name, value, amount, left, right, 48+i//2*46, name == e.selected)
    p.txt(14, native.metrics['footer_y'], 'b back')
    p.txt(306, native.metrics['footer_y'], 'l: mod >', 'accent', 'right')
    assert len(p.hits) == 9
    return p


def setup(theme, e, native):
    p = page(theme, False, e, native.metrics, setup=True)
    controls = (
        ('drift', f'{e.drift:+.2f}hz' if e.drift else '0 / off', (e.drift+8)/16,
         '0 = neutral / signed sweep direction'),
        ('comp curve', f'{e.comp:.0%}', e.comp, 'static curve / not a compressor'),
        ('dither', f'{e.dither:.0%}', e.dither, 'tpdf / 0 off / 100 one lsb peak'),
        ('drop length', f'{e.drop_time*1000:.0f}ms', log_position(e.drop_time, .001, .250),
         'fixed event time / 100% drop = wet mute'),
    )
    for i, (name, value, amount, explanation) in enumerate(controls):
        y = 40+i*45
        control(p, native, name, value, amount, 14, 306, y, name == 'drop length')
        p.txt(14, y+30, explanation)
    p.txt(14, native.metrics['footer_y'], 'b main')
    p.txt(306, native.metrics['footer_y'], 'l: mod >', 'accent', 'right')
    assert len(p.hits) == 5
    return p


def dual(top, bottom):
    image = Image.new('RGB', (400, 496), OUTSIDE)
    image.paste(top.im, (0, 0))
    image.paste(bottom.im, (40, 256))
    return image


def save(out, name, image, scale=2):
    path = out/name
    assert not path.exists()
    scaled = image.resize((image.width*scale, image.height*scale), Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    scaled.save(path, pnginfo=meta)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes() == scaled.tobytes()
        assert decoded.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    return dict(path=str(path.relative_to(ROOT)), sha256=sha(path), size=list(scaled.size))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    out = (args.out or ROOT/'ui_mockups/bit_extended_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')).resolve()
    out.mkdir(parents=True, exist_ok=False)
    before = dict(base.protected(), Makefile=sha(ROOT/'Makefile'))
    native = NativeNeedles(out)
    assert signal(EXAMPLES[2])['source'] == signal(EXAMPLES[2])['output']
    dry = signal(replace(EXAMPLES[1], mix=0))
    assert dry['source'] == dry['output']
    assert not any(signal(replace(EXAMPLES[1], drop=1, mix=1))['output'])
    assert any(signal(EXAMPLES[1])['dropout'])
    images, layouts, outputs = {}, {}, []
    for theme in ('graphite', 'pink'):
        for e in EXAMPLES:
            top = upper(theme, e, native.metrics)
            pages = [('main', lower(theme, e, native))]
            if e.slug == 'broken':
                pages.append(('setup', setup(theme, e, native)))
            for suffix, bottom in pages:
                key = theme+'-'+e.slug+'-'+suffix
                for p in (top, bottom):
                    for box, label in p.labels:
                        assert 14 <= box[0] < box[2] <= p.im.width-14 and 8 <= box[1] < box[3] <= 236, (label, box)
                images[key] = (top, bottom)
                layouts[key] = dict(top_labels=top.labels, bottom_labels=bottom.labels, proposed_targets=bottom.hits)
                outputs.append(save(out, key+'-dual.png', dual(top, bottom)))
                outputs.append(save(out, key+'-top-native.png', top.im, 1))
                outputs.append(save(out, key+'-bottom-native.png', bottom.im, 1))
    for name, columns in (
        ('main-and-setup', (('main / eight controls', 'graphite-crunch-main'), ('setup / four controls', 'graphite-broken-setup'))),
        ('both-themes', (('graphite', 'graphite-crunch-main'), ('pink', 'pink-crunch-main'))),
        ('crunch-and-broken', (('crunch / 4bit', 'graphite-crunch-main'), ('broken / 3bit + dropout', 'graphite-broken-main'))),
    ):
        board = base.Canvas(848, 584, OUTSIDE)
        board.text(16, 12, 'bit / extended', base.COLORS['graphite']['ink'], scale=2)
        board.text(16, 38, 'input + output / sample clock + dropout / native needle controls', base.COLORS['graphite']['muted'])
        for i, (caption, key) in enumerate(columns):
            board.text(16+i*416, 57, caption, base.COLORS['graphite']['accent'])
            board.im.paste(dual(*images[key]), (16+i*416, 72))
        board.text(16, 574, 'design study / not implemented / plots are models, not live audio', base.COLORS['graphite']['muted'])
        outputs.append(save(out, name+'.png', board.im))
    for e in EXAMPLES:
        path = out/(e.slug+'-model.json')
        path.write_text(json.dumps(dict(example=asdict(e), **signal(e)), separators=(',', ':'))+'\n')
    assert dict(base.protected(), Makefile=sha(ROOT/'Makefile')) == before
    report = dict(status='visual proposal only; awaiting layout feedback; no production implementation approval implied',
        origin=__doc__, created_at=datetime.now(timezone.utc).isoformat(), script_sha256=sha(__file__),
        frame=native.metrics, protected_sha256=before, examples=[asdict(e) for e in EXAMPLES],
        outputs=outputs, layouts=layouts, proposed_parameter_order=PARAMETERS,
        model=dict(origin='independent deterministic Python illustration, NOT BitUnit', sample_rate=SR,
                   window_samples=WINDOW, warmup_samples=WARMUP, source='q15 125hz .625fs triangle',
                   scope='same32ms and fixed +/-1 amplitude; per-column min/max plus continuity',
                   clock_and_drop='actual events from that SAME illustrative model window',
                   clean_dry_full_drop_endpoint_assertions='Python model only, not production tests',
                   files={e.slug+'-model.json': sha(out/(e.slug+'-model.json')) for e in EXAMPLES},
                   live_pcm=False, exported_audio=False),
        native_needles=dict(header_sha256=sha(ROOT/'platform/3ds/module_fader.h'),
                           driver_sha256=sha(out/'helper/needles.cpp'), binary_sha256=sha(native.binary), submissions=native.records),
        proposed_fixes=['all8 edit banks for lock read/write/remove; owner bank/view/engine cancellation',
                       'jitter affects fractional interval, no first25percent dead zone',
                       'drift0 neutral rather than retained period offset',
                       'symmetric interior quantization and wet dc removal; special1bit and true16bit endpoint',
                       'fractional nominal rate100..32khz; audible aliasing retained',
                       'exact dry/full-wet and full-drop-wet endpoints',
                       'full32bit independent random states for clock/drop/dither; fixed dropout events'],
        proposed_ranges=dict(bits='integer1..16', rate='log100..32000hz', drive='0..24db pre crush',
                             tone='100..14khz plus bypass at maximum', jitter='0..50percent interval spread',
                             dropout='0..100percent chance per eligible capture; active events do not retrigger',
                             mix='0..100percent', wet_gain='-24..+6db before mix',
                             drift='-8..+8hz / fixed12.5percent nominal-rate depth / zero neutral',
                             comp_curve='0..100percent static post-quantization curve', dither='0..1lsb peak tpdf',
                             drop_length='1..250ms'),
        compatibility='first7 indices retained, 5 appended within existing12; old patches stay legacy, explicit extension only; serialization not designed/changed',
        checks=dict(duals=len(images), text_bounds_and_nonoverlap=True, proposed_hit_nonoverlap=True,
                    exact_shared_needle_calls=len(native.records), protected_unchanged=len(before), nearest_integer_enlargement=True),
        limitations=['no runtime touch handler, gesture or actual audio proof', 'no actual native application capture',
                     'no production/core/assets/Makefile edits', 'no ARM/application build, deployment, listening or FPS check'])
    (out/'proposal.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'README.md').write_text('# bit extended — visual proposal only\n\n'
        'Pillow-authored main/setup study, not an implemented module or device capture. '
        'Exact current23x19 bit title at14,8, native6x8 body text, fixed shared frame and footer228. '
        'Faders use the unmodified compiled needle_horizontal helper.\n\n'
        '- main-and-setup.png: all12 proposed parameters across main8/setup4.\n'
        '- both-themes.png: same main layout in graphite and pink.\n'
        '- crunch-and-broken.png: two signal/clock/dropout examples.\n'
        '- *-native.png: exact400x240/320x240 surfaces; duals nearest2x.\n\n'
        'Scope, sample clock and dropout stripe come from the same independent fixed-seed Python design model. '
        'They are not production BitUnit, live PCM or an audio quality claim. Output meter unavailable. '
        'crunch/broken/clean are study fixtures, not promised preset buttons. Main8: bits, rate, drive, tone, jitter, '
        'dropout, mix, wet gain. Setup4: drift, comp curve, dither, drop length. First7 persisted indices remain '
        'reserved;5 append slots fit the existing12 without enlargement. All DSP/migration/gestures still unimplemented.\n')
    print(out.relative_to(ROOT))
    print(f'{len(images)} authored duals / {len(native.records)} native-helper needle calls / {len(before)} protected files unchanged')
    print(out/'main-and-setup.png')


if __name__ == '__main__':
    main()
