#!/usr/bin/env python3
"""Fold extended visual study, NOT runtime UI, production DSP or a device capture.

Authored native-size Pillow composition with the current title bitmap, 6x8 font
and unchanged compiled C++ needle helper. The floating-point signal/transfer
illustrates proposed smooth/tone/DC processing; it is NOT FoldUnit and does not
establish audio quality, alias suppression or a final DSP contract.
"""
import argparse
from dataclasses import asdict, dataclass, replace
from datetime import datetime, timezone
from functools import lru_cache
import json
import math
from pathlib import Path
import re

from PIL import Image, PngImagePlugin
import render_module_editor_fullscreen_study as skin
from render_motor_extended_study import sha
from render_strain_extended_study import NativeNeedles
from render_ring_extended_study import protected, dual

ROOT = Path(__file__).resolve().parents[1]
base = skin.base
OUTSIDE = (18, 18, 21)
# Dense offline illustration only; this is NOT an implemented oversampling mode.
SR, HZ, AMPLITUDE = 128000, 250, .75
WINDOW = SR * 8 // 1000


@dataclass(frozen=True)
class Example:
    slug: str
    drive: float = 2.75
    morph: float = 0.
    bias: float = 0.
    smooth: float = .70
    tone: float = 2400.
    trim: float = 0.
    mix: float = 1.
    selected: str = 'smooth'


EXAMPLES = (
    Example('rounded'),
    Example('sharp', smooth=0., tone=0., selected='drive'),
    Example('wrap', drive=3.5, morph=.5, bias=.18, smooth=.15,
            tone=7000., trim=-3., mix=.85, selected='morph'),
    Example('diode', drive=2., morph=1., bias=-.15, smooth=.35,
            tone=5000., trim=-6., selected='bias'),
)


def clamp(x, lo=-1., hi=1.):
    return max(lo, min(hi, x))


def shaper(dry, e):
    """Illustrative memoryless branch BEFORE DC, tone, wet trim and mix.

    Float triangle reflection / wrap / positive quarter-negative rectifier.
    Smooth morphs triangle into sine, wrap into a continuous sinusoidal shape,
    and rectifier cusp into a rounded absolute-value law. These equations are
    an explicit design illustration, not the old integer DSP or approved code.
    """
    x = dry * e.drive + e.bias
    sine = math.sin(math.pi * x / 2)
    fold = (1-e.smooth) * (2/math.pi * math.asin(clamp(sine))) + e.smooth*sine
    wrap = (1-e.smooth) * ((x+1) % 2 - 1) + e.smooth*math.sin(math.pi*x)
    radius = .16 * e.smooth
    absolute = abs(x) if radius == 0 else math.sqrt(x*x+radius*radius)-radius
    diode = .375*x + .625*absolute
    shaped = (fold + (wrap-fold)*e.morph*2 if e.morph <= .5 else
              wrap + (diode-wrap)*(e.morph-.5)*2)
    return shaped-e.bias


@lru_cache(None)
def signal(e):
    """Fixed 8ms/two-cycle reference after 500ms warm-up; never peak-normalized."""
    previous = dc = tone = 0.
    pole = math.exp(-math.tau*8/SR)
    alpha = 1. if not e.tone else 1-math.exp(-math.tau*e.tone/SR)
    gain = 10**(e.trim/20)
    source, output = [], []
    warmup = SR//2
    for i in range(warmup+WINDOW):
        dry = AMPLITUDE*math.sin(math.tau*HZ*i/SR)
        nonlinear = shaper(dry, e)
        dc = nonlinear-previous+pole*dc
        previous = nonlinear
        tone += alpha*(dc-tone)
        wet = tone*gain
        if i >= warmup:
            source.append(dry)
            output.append(clamp(dry+(wet-dry)*e.mix))
    assert len(source) == len(output) == WINDOW
    assert all(math.isfinite(x) and -1 <= x <= 1 for x in source+output)
    return {'input': source, 'output': output}


@lru_cache(None)
def title_mask():
    text = (ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw = text.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    bits = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', raw))
    row = re.search(r'\{UnitKind::Fold, "fold", (\d+), (\d+), (\d+)\}', text)
    offset, width, height = map(int, row.groups())
    assert (width, height) == (35, 17)
    mask = Image.new('L', (width, height))
    for y in range(height):
        for x in range(width):
            if bits[offset+y*((width+7)//8)+x//8] & (128 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


def log_position(value, lo, hi):
    return clamp(math.log(value/lo)/math.log(hi/lo), 0., 1.)


def controls(e):
    return (
        ('drive', f'{e.drive:.2f}x', log_position(e.drive, .25, 8.), .4),
        ('morph', f'{e.morph:.0%}', e.morph, .5),
        ('bias', '0.00fs' if e.bias == 0 else f'{e.bias:+.2f}fs', (e.bias+1)/2, .5),
        ('smooth', f'{e.smooth:.0%}', e.smooth, None),
        ('tone', 'off' if not e.tone else f'{e.tone/1000:.1f}khz',
         1. if not e.tone else log_position(e.tone, 100., 16000.), None),
        ('trim', f'{e.trim:.1f}db', 10**(e.trim/20), None),
        ('mix', f'{e.mix:.0%}', e.mix, None),
    )


def page(theme, top, e, view, metrics):
    p = base.Surface(skin.paper(theme, 'top' if top else 'bottom'), theme)
    mask = title_mask()
    p.im.paste(p.c['accent'], (14, 8), mask)
    p.labels.append(((14, 8, 14+mask.width, 8+mask.height), 'fold / exact current title'))
    if top:
        p.txt(386, metrics['header_y'], 'ch01 / 02', 'muted', 'right')
        p.txt(14, metrics['route_y'], 'play /')
        p.txt(56, metrics['route_y'], 'fold', 'accent')
        p.txt(86, metrics['route_y'], '/ out')
        p.line(56, metrics['underline_y'], 78, metrics['underline_y'], 'accent')
        name, value, _, _ = next(c for c in controls(e) if c[0] == e.selected)
        p.txt(386, metrics['route_y'], name+' '+value+' / mod off', 'ink', 'right')
    else:
        p.txt(62, metrics['header_y'], 'ch01 / 02')
        p.txt(206, metrics['header_y'], view, 'accent')
        p.txt(258, metrics['header_y'], 'byp')
        p.txt(306, metrics['header_y'], 'x', 'muted', 'right')
        p.hit('wave / curve view toggle', (198, 0, 246, 26))
        p.hit('bypass', (246, 0, 286, 26))
    return p


def axes(p, view):
    faint = base.mix(p.c['rule'], p.im.getpixel((200, 132)), 58)
    for level, label in ((1, '+1'), (0, '0'), (-1, '-1')):
        y = 132-level*55
        p.txt(14, y-4, label)
        if level:
            for x in range(34, 387, 5):
                p.d.point((x, y), fill=faint)
        else:
            p.d.line((34, y, 386, y), fill=faint)
    for x in (34, 210, 386):
        for y in range(77, 188, 5):
            p.d.point((x, y), fill=faint)
    labels = ('0', '4', '8ms') if view == 'wave' else ('-1', '0', '+1 in')
    for x, text, align in zip((34, 210, 386), labels, ('left', 'center', 'right')):
        p.txt(x, 198, text, 'muted', align)


def trace(p, values, role):
    # Every sample belongs to one bin. Extrema and continuity share the same x.
    previous = None
    for column in range(353):
        a, b = column*len(values)//353, (column+1)*len(values)//353
        bucket = values[a:b]
        assert bucket
        ys = [132-round(clamp(v)*55) for v in bucket]
        x = 34+column
        if previous is not None:
            p.line(x-1, previous, x, ys[0], role)
        p.line(x, min(ys), x, max(ys), role)
        previous = ys[-1]
        # Display clipping is explicitly marked; it does not clamp the model.
        if max(bucket) > 1:
            p.line(x, 74, x, 76, role)
        if min(bucket) < -1:
            p.line(x, 188, x, 190, role)


def upper(theme, e, view, metrics):
    p = page(theme, True, e, view, metrics)
    if view == 'wave':
        p.line(14, 59, 26, 59, 'muted'); p.txt(32, 55, 'in')
        p.line(62, 59, 74, 59, 'accent'); p.txt(80, 55, 'out', 'accent')
        p.txt(386, 55, 'wave / 250hz .75fs', 'muted', 'right')
    else:
        p.txt(14, 55, 'transfer', 'accent')
        p.txt(386, 55, 'drive + bias + smooth', 'muted', 'right')
    axes(p, view)
    if view == 'wave':
        data = signal(e)
        trace(p, data['input'], 'muted')
        trace(p, data['output'], 'accent')
        p.txt(14, 214, 'post dc / tone / trim / mix')
    else:
        xs = [-1+2*i/4095 for i in range(4096)]
        trace(p, xs, 'muted')
        trace(p, [shaper(x, e) for x in xs], 'accent')
        p.txt(14, 214, 'before dc / tone / trim / mix')
    p.txt(14, metrics['footer_y'], 'study model / not live')
    p.txt(386, metrics['footer_y'], '+/-1fs', 'muted', 'right')
    return p


def lower(theme, e, view, native):
    p = page(theme, False, e, view, native.metrics)
    geometry = (
        (14, 306, 36, 60, (8, 28, 312, 72)),
        (14, 148, 81, 103, (8, 74, 156, 112)),
        (172, 306, 81, 103, (164, 74, 312, 112)),
        (14, 148, 149, 171, (8, 140, 156, 182)),
        (172, 306, 149, 171, (164, 140, 312, 182)),
        (14, 148, 193, 215, (8, 184, 156, 224)),
        (172, 306, 193, 215, (164, 184, 312, 224)),
    )
    for c, g in zip(controls(e), geometry):
        name, value, amount, neutral = c
        left, right, y, rail_y, hit = g
        role = 'accent' if name == e.selected else 'muted'
        p.txt(left, y, name, role)
        p.txt(right, y, value, 'accent' if name == e.selected else 'ink', 'right')
        native.draw(p, left+1, right-1, rail_y, amount, neutral)
        p.hit(name+' / relative drag, r fine', hit)
    for anchor, label, x, bounds in (
        (0., 'fold', 14, (8, 114, 54, 138)),
        (.5, 'wrap', 62, (56, 114, 102, 138)),
        (1., 'diode', 110, (104, 114, 156, 138)),
    ):
        p.txt(x, 122, label, 'accent' if e.morph == anchor else 'muted')
        p.hit('exact morph anchor / '+label, bounds)
    p.txt(172, 122, 'r: fine')
    p.txt(306, 122, 'zero', 'muted', 'right')
    p.hit('bias / exact zero', (268, 114, 312, 138))
    p.txt(14, native.metrics['footer_y'], 'b back')
    p.txt(306, native.metrics['footer_y'], 'l: mod >', 'accent', 'right')
    assert len(p.hits) == 13
    return p


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


def board(images, columns, subtitle):
    b = base.Canvas(848, 582, OUTSIDE)
    b.text(16, 12, 'fold / extended', base.COLORS['graphite']['ink'], scale=2)
    b.text(16, 38, subtitle, base.COLORS['graphite']['muted'])
    for i, (caption, key) in enumerate(columns):
        x = 16+i*416
        b.text(x, 57, caption, base.COLORS['graphite']['accent'])
        b.im.paste(dual(*images[key]), (x, 72))
    b.text(16, 572, 'design only / illustrative signal / no runtime or audio changes', base.COLORS['graphite']['muted'])
    return b.im


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    out = (args.out or ROOT/'ui_mockups/fold_extended_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')).resolve()
    out.mkdir(parents=True, exist_ok=False)
    before = protected()
    native = NativeNeedles(out)
    images, outputs, layouts = {}, [], {}
    for theme in ('graphite', 'pink'):
        for e in EXAMPLES:
            for view in (('wave', 'curve') if e.slug == 'rounded' else ('wave',)):
                top, bottom = upper(theme, e, view, native.metrics), lower(theme, e, view, native)
                for p in (top, bottom):
                    for box, label in p.labels:
                        assert 14 <= box[0] < box[2] <= p.im.width-14 and 8 <= box[1] < box[3] <= 236, (label, box)
                key = theme+'-'+e.slug+'-'+view
                images[key] = (top, bottom)
                layouts[key] = dict(top_labels=top.labels, bottom_labels=bottom.labels, proposed_targets=bottom.hits)
                outputs.append(save(out, key+'-dual.png', dual(top, bottom)))
                outputs.append(save(out, key+'-top-native.png', top.im, 1))
                outputs.append(save(out, key+'-bottom-native.png', bottom.im, 1))
    for name, columns, subtitle in (
        ('both-themes', (('graphite', 'graphite-rounded-wave'), ('pink', 'pink-rounded-wave')),
         'large reference above / seven exact native needles below'),
        ('wave-and-curve', (('wave / complete illustrative path', 'graphite-rounded-wave'), ('curve / nonlinear section only', 'graphite-rounded-curve')),
         'same settings / two views / touch the lower header to switch'),
        ('sharp-and-rounded', (('sharp / smooth 0 / tone off', 'graphite-sharp-wave'), ('rounded / smooth 70 / tone 2.4k', 'graphite-rounded-wave')),
         'same 2.75x drive / different shaping and post-filter settings'),
    ):
        outputs.append(save(out, name+'.png', board(images, columns, subtitle)))
    # Illustration self-check only. This is not a production DSP test.
    dry = signal(replace(EXAMPLES[0], mix=0.))
    assert dry['input'] == dry['output']
    assert signal(EXAMPLES[0])['output'] != signal(EXAMPLES[1])['output']
    assert protected() == before, 'production/assets/application artifacts changed'
    helpers = ('render_fold_extended_study.py', 'render_strain_extended_study.py',
               'render_motor_extended_study.py', 'render_ring_extended_study.py',
               'render_module_editor_fullscreen_study.py', 'render_module_editor_sheet_study.py',
               'render_module_editor_link_study.py', 'render_bounce_visual_study.py',
               'render_module_geometry_study.py', 'render_menu_style_study.py')
    report = dict(status='visual proposal only; awaiting feedback', provenance=__doc__,
        created_at=datetime.now(timezone.utc).isoformat(), protected_sha256=before,
        study_sources_sha256={name: sha(ROOT/'tools'/name) for name in helpers},
        frame=native.metrics, examples=[asdict(e) for e in EXAMPLES], outputs=outputs, layouts=layouts,
        native_needles=dict(header_sha256=sha(ROOT/'platform/3ds/module_fader.h'),
            driver_sha256=sha(out/'helper/needles.cpp'), binary_sha256=sha(native.binary), submissions=native.records),
        proposed=dict(main=['drive', 'morph', 'bias', 'smooth', 'tone', 'trim', 'mix'],
            drive='provisional logarithmic .25..8x; exact 1x tick',
            smooth='illustrative nonlinear rounding; not post-filter or promised anti-alias algorithm',
            tone='provisional post-shaper low-pass 100..16000hz plus off; not an alias cure',
            trim='retained linear wet gain, mute..0dB; shown in dB',
            gestures='relative296px proposal, r fine, exact morph anchors and bias zero, selected MOD',
            view='lower header switches upper wave/curve; upper LCD is not touch-sensitive',
            compatibility='preserve old five indices and legacy sound; modern/versioning not implemented'),
        model=dict(origin='independent float Python illustration, NOT current FoldUnit',
            source='250hz .75fs sine; fixed8ms/two cycles after500ms warmup',
            sample_rate=SR, sample_rate_note='offline illustration density only, NOT an implemented quality mode',
            path='gain+bias -> illustrative morph/smooth -> subtract bias ->8hz DC blocker ->tone ->wet trim ->mix ->clip',
            curve='memoryless branch before DC/tone/trim/mix; fixed +/-1FS visible window, overflow ticks if needed',
            waveform='same-bin min/max and continuity; fixed scale, no normalization',
            limitations='no live PCM/FFT/real meters/audio export/listening/anti-alias or performance proof'),
        checks=dict(authored_duals=len(images), text_bounds_nonoverlap=True, proposed_targets_disjoint=True,
            exact_shared_needle_calls=len(native.records), protected_unchanged=len(before), nearest_scaling=True),
        references=['https://downloads.eventide.com/audio/manuals/plug-ins/Pendulate+User+Guide.pdf',
            'https://www.applied-acoustics.com/multiphonics-cv-2/manual/wavefolder',
            'https://livingmemorysoftware.com/wp-content/uploads/2019/11/wavefolder.pdf'],
        limitations=['no production controller/DSP/persistence edits', 'not a native application or hardware capture',
            'only the drawing helper compiled; no application/ARM build,3dsx,FTP,device,FPS acceptance'])
    (out/'proposal.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'README.md').write_text('# fold extended — design only\n\n'
        'authored native-size composition, not runtime or hardware capture. current35x17 fold title, '
        'native6x8 text, exact unchanged C++ needle calls and shared frame.\n\n'
        '- `wave-and-curve.png`: same settings with two upper views.\n'
        '- `both-themes.png`: graphite/pink.\n'
        '- `sharp-and-rounded.png`: same drive, different smooth AND tone, not an isolated smooth comparison.\n'
        '- individual duals include wrap and diode.\n\n'
        'wave is an independent proposed float model including DC/tone/trim/mix; curve is ONLY the '
        'memoryless nonlinear branch. neither is actual FoldUnit or live audio.128k illustration density '
        'is not a promised audio oversampling feature. no audio export. all production/assets/root '
        'console artifacts were checked unchanged. compatibility and new controls remain unimplemented.\n')
    print(out.relative_to(ROOT))
    print(f'{len(images)} authored duals / {len(native.records)} exact native needles / {len(before)} protected files unchanged')
    print(out/'wave-and-curve.png')


if __name__ == '__main__':
    main()
