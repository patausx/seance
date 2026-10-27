#!/usr/bin/env python3
"""Noise extended: authored UI proposal, NOT runtime or hardware capture.

Exact current noise title and native 6x8 text. Faders are rectangle submissions
from the unchanged C++ module_fader::needle_horizontal helper. Filter responses
and amplitude contours are illustrative mathematical models, not NoiseUnit DSP,
measured spectra, live telemetry or exported audio. New features are design only.
"""
import argparse
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from functools import lru_cache
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


@dataclass(frozen=True)
class Example:
    slug: str
    colour: str
    mode: str
    grain: float
    cutoff: float
    lowcut: float
    resonance: float
    attack: float
    decay: float
    sweep: float
    gain: float
    event_length: float
    sweep_time: float
    selected: str


EXAMPLES = (
    Example('white-hit', 'white', 'hit', 12000, 3200, 180, .42,
            .004, 1.60, 12, .72, .018, .240, 'sweep'),
    Example('pink-drone', 'pink', 'drone', 2800, 1800, 35, .22,
            .800, 6.00, 0, .64, .018, 1.20, 'grain rate'),
    Example('crackle-hit', 'crackle', 'hit', 4.0, 5400, 90, .28,
            .002, 2.40, -7, .70, .018, .240, 'density'),
)


@lru_cache(None)
def title_mask():
    text = (ROOT / 'platform/3ds/module_heading_data.h').read_text()
    raw = text.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    bits = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', raw))
    row = re.search(r'\{UnitKind::Noise, "noise", (\d+), (\d+), (\d+)\}', text)
    offset, width, height = map(int, row.groups())
    assert (width, height) == (46, 16)
    mask = Image.new('L', (width, height))
    for y in range(height):
        for x in range(width):
            if bits[offset + y*((width+7)//8) + x//8] & (128 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


def freq(value):
    return f'{value/1000:.1f}khz' if value >= 1000 else f'{value:.0f}hz'


def seconds(value):
    return f'{value*1000:.0f}ms' if value < 1 else f'{value:.2f}s'


def logarithmic(value, minimum, maximum):
    return max(0., min(1., math.log(max(minimum, value)/minimum)/math.log(maximum/minimum)))


def page(theme, top, example, metrics, setup=False):
    p = base.Surface(skin.paper(theme, 'top' if top else 'bottom'), theme)
    left, right = metrics['left'], p.im.width-metrics['right']
    mask = title_mask()
    p.im.paste(p.c['accent'], (left, metrics['top']), mask)
    p.labels.append(((left, metrics['top'], left+mask.width, metrics['top']+mask.height),
                     'noise / exact current title mask'))
    if top:
        p.txt(right, metrics['header_y'], 'ch01 / 01', 'muted', 'right')
        p.txt(left, metrics['route_y'], 'noise', 'accent')
        p.txt(56, metrics['route_y'], '/ rev', 'muted')
        p.line(left, metrics['underline_y'], 42, metrics['underline_y'], 'accent')
        p.txt(right, metrics['route_y'], example.colour+' / '+example.mode, 'ink', 'right')
    else:
        p.txt(78, metrics['header_y'], 'ch01 / 01')
        p.txt(206, metrics['header_y'], 'main' if setup else 'setup', 'accent')
        p.txt(258, metrics['header_y'], 'byp')
        p.txt(right, metrics['header_y'], 'x', 'muted', 'right')
        p.hit('main' if setup else 'setup', (198, 0, 246, 26))
        # Bypass and hardware-X retain existing semantics, not new body actions.
    return p


def response(hz, cutoff, lowcut, resonance):
    """Illustrative analog HP1 + resonant LP2 transfer; NOT production DSP."""
    q = .55 + resonance*resonance*7
    x = hz/cutoff
    low = 1 / complex(1-x*x, x/q)
    high = complex(0, hz/lowcut) / complex(1, hz/lowcut) if lowcut else 1
    return 20*math.log10(max(1e-8, abs(low*high)))


def upper(theme, e, metrics):
    p = page(theme, True, e, metrics)
    p.txt(14, 55, 'filter profile')
    p.line(180, 59, 192, 59, 'muted')
    p.txt(198, 55, 'base')
    p.line(248, 59, 260, 59, 'accent')
    p.txt(266, 55, 'onset', 'accent')
    p.txt(386, 55, 'model', 'muted', 'right')
    x0, x1, y0, y1 = 34, 386, 72, 135
    faint = base.mix(p.c['rule'], p.im.getpixel((200, 106)), 68)

    def xx(hz):
        return x0+round(math.log(hz/20, 800)*(x1-x0))

    def yy(db):
        return y1-round((max(-48, min(12, db))+48)/60*(y1-y0))

    for db in (0, -24, -48):
        p.d.line((x0, yy(db), x1, yy(db)), fill=faint)
    p.txt(14, yy(0)-3, '0')
    p.txt(14, yy(-48)-3, '-48')
    for hz, label in ((50, '50'), (200, '200'), (1000, '1k'), (5000, '5k'), (16000, '16k')):
        x = xx(hz)
        p.d.line((x, y0, x, y1), fill=faint)
        p.txt(x, 143, label, 'muted', 'right' if hz == 16000 else 'center')
    onset = min(14000, max(25, e.cutoff*2**(e.sweep/12)))
    for cutoff, role in ((e.cutoff, 'muted'), (onset, 'accent')):
        previous = None
        for x in range(x0, x1+1):
            hz = 20*800**((x-x0)/(x1-x0))
            y = yy(response(hz, cutoff, e.lowcut, e.resonance))
            if previous is not None:
                p.line(x-1, previous, x, y, role)
            previous = y

    p.txt(14, 163, 'amp contour')
    p.txt(386, 163, 'a '+seconds(e.attack)+' / '+('hold' if e.mode == 'drone' else 'd '+seconds(e.decay)),
          'ink', 'right')
    top, bottom = 180, 214
    p.d.line((34, bottom, 386, bottom), fill=faint)
    p.d.line((34, top, 34, bottom), fill=faint)
    duration = max(.30, e.attack*1.35) if e.mode == 'drone' else e.attack+e.decay
    previous = None
    for x in range(34, 387):
        time = (x-34)/352*duration
        if time < e.attack:
            amplitude = time/max(1e-9, e.attack)
        elif e.mode == 'drone':
            amplitude = 1.
        else:
            u = max(0., min(1., (time-e.attack)/e.decay))
            amplitude = (math.exp(-5*u)-math.exp(-5))/(1-math.exp(-5))
        y = bottom-round(amplitude*(bottom-top))
        if previous is not None:
            p.line(x-1, previous, x, y, 'ink')
        previous = y
    p.txt(14, metrics['footer_y'], 'design model / not audio')
    p.txt(386, metrics['footer_y'], 'out --', 'muted', 'right')
    return p


def control(p, native, name, value, amount, left, right, y, selected=False, disabled=False):
    role = 'muted' if disabled else 'accent' if selected else 'ink'
    p.txt(left, y, name, role)
    p.txt(right, y, value, role, 'right')
    native.draw(p, left, right, y+19, amount, disabled)
    p.hit(name+' / proposed relative drag', (left-6, y-5, right+6, y+27))


def lower(theme, e, native):
    p = page(theme, False, e, native.metrics)
    choices = (('white', 14, (8, 29, 74, 56)), ('pink', 93, (83, 29, 147, 56)),
               ('crackle', 164, (154, 29, 233, 56)), ('metal', 276, (246, 29, 312, 56)))
    for name, x, target in choices:
        p.txt(x, 36, name, 'accent' if e.colour == name else 'muted')
        if e.colour == name:
            p.line(x, 49, x+len(name)*6-2, 49, 'accent')
        p.hit('colour / '+name, target)
    for name, x, target in (('hit', 14, (8, 58, 52, 82)), ('drone', 70, (58, 58, 113, 82))):
        p.txt(x, 63, name, 'accent' if name == e.mode else 'muted')
        if name == e.mode:
            p.line(x, 75, x+len(name)*6-2, 75, 'accent')
        p.hit('mode / '+name, target)
    p.txt(306, 63, 'a trig' if e.mode == 'hit' else 'continuous', 'muted', 'right')
    sparse = e.colour == 'crackle'
    controls = (
        ('density' if sparse else 'grain rate', f'{e.grain:.1f}/s' if sparse else freq(e.grain),
         logarithmic(e.grain, .1 if sparse else 30, 8000 if sparse else 32000), False),
        ('gain', f'{e.gain:.0%}', e.gain, False),
        ('tone', freq(e.cutoff), logarithmic(e.cutoff, 25, 14000), False),
        ('low cut', freq(e.lowcut), logarithmic(e.lowcut, 20, 6000), False),
        ('resonance', f'{e.resonance:.0%}', e.resonance, False),
        ('sweep', f'{e.sweep:+.0f}st', (e.sweep+24)/48, e.mode == 'drone'),
        ('attack', seconds(e.attack), logarithmic(e.attack, .001, 2), False),
        ('decay', seconds(e.decay), logarithmic(e.decay, .005, 10), e.mode == 'drone'),
    )
    for i, (name, value, amount, disabled) in enumerate(controls):
        left, right = (14, 148) if i % 2 == 0 else (172, 306)
        control(p, native, name, value, amount, left, right, 91+i//2*34,
                selected=name == e.selected, disabled=disabled)
    p.txt(14, native.metrics['footer_y'], 'b back')
    p.txt(306, native.metrics['footer_y'], 'l: mod >', 'accent', 'right')
    assert len(p.hits) == 15
    return p


def setup(theme, e, native):
    p = page(theme, False, e, native.metrics, setup=True)
    p.txt(14, 38, 'crackle / setup', 'accent')
    p.txt(306, 38, 'hit', 'muted', 'right')
    control(p, native, 'event length', seconds(e.event_length),
            logarithmic(e.event_length, .001, .250), 14, 306, 73, selected=True)
    p.txt(14, 111, 'length of each individual crackle')
    control(p, native, 'sweep time', seconds(e.sweep_time),
            logarithmic(e.sweep_time, .001, 5), 14, 306, 148)
    p.txt(14, 186, 'time to return to the stored tone')
    p.txt(14, 228, 'b main')
    p.txt(306, 228, 'l: mod >', 'accent', 'right')
    assert len(p.hits) == 3
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
    out = (args.out or ROOT/'ui_mockups/noise_extended_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')).resolve()
    out.mkdir(parents=True, exist_ok=False)
    before = dict(base.protected(), Makefile=sha(ROOT/'Makefile'))
    native = NativeNeedles(out)
    layouts, images, outputs = {}, {}, []
    for theme in ('graphite', 'pink'):
        for e in EXAMPLES:
            top, bottom = upper(theme, e, native.metrics), lower(theme, e, native)
            for suffix, lower_page in (('main', bottom), ('setup', setup(theme, e, native)) if e.colour == 'crackle' else ('main-repeat', bottom)):
                if suffix == 'main-repeat':
                    continue
                key = theme+'-'+e.slug+'-'+suffix
                for p in (top, lower_page):
                    for box, label in p.labels:
                        assert 14 <= box[0] < box[2] <= p.im.width-14 and 8 <= box[1] < box[3] <= 236, (label, box)
                images[key] = (top, lower_page)
                layouts[key] = dict(top_labels=top.labels, bottom_labels=lower_page.labels, proposed_targets=lower_page.hits)
                outputs.append(save(out, key+'-dual.png', dual(top, lower_page)))
                outputs.append(save(out, key+'-top-native.png', top.im, 1))
                outputs.append(save(out, key+'-bottom-native.png', lower_page.im, 1))
    for name, columns in (
        ('main-and-setup', (('main / white hit', 'graphite-white-hit-main'),
                            ('setup / crackle hit', 'graphite-crackle-hit-setup'))),
        ('both-themes', (('graphite', 'graphite-white-hit-main'), ('pink', 'pink-white-hit-main'))),
    ):
        board = base.Canvas(848, 584, OUTSIDE)
        board.text(16, 12, 'noise / extended', base.COLORS['graphite']['ink'], scale=2)
        board.text(16, 38, 'filter profile + envelope / eight native needle controls', base.COLORS['graphite']['muted'])
        for i, (caption, key) in enumerate(columns):
            board.text(16+i*416, 57, caption, base.COLORS['graphite']['accent'])
            board.im.paste(dual(*images[key]), (16+i*416, 72))
        board.text(16, 574, 'design study / not implemented / plots are models, not audio', base.COLORS['graphite']['muted'])
        outputs.append(save(out, name+'.png', board.im))
    strip = base.Canvas(1040, 306, OUTSIDE)
    strip.text(16, 12, 'noise / main controls', base.COLORS['graphite']['ink'], scale=2)
    for i, e in enumerate(EXAMPLES):
        strip.text(16+i*344, 40, e.colour+' / '+e.mode, base.COLORS['graphite']['accent'])
        strip.im.paste(images['graphite-'+e.slug+'-main'][1].im, (16+i*344, 56))
    outputs.append(save(out, 'three-main-states.png', strip.im))
    assert dict(base.protected(), Makefile=sha(ROOT/'Makefile')) == before
    report = dict(status='design proposal only, awaiting visual approval', origin=__doc__,
        created_at=datetime.now(timezone.utc).isoformat(), script_sha256=sha(__file__), frame=native.metrics,
        protected_sha256=before, examples=[asdict(e) for e in EXAMPLES], outputs=outputs, layouts=layouts,
        native_needles=dict(header_sha256=sha(ROOT/'platform/3ds/module_fader.h'),
            driver_sha256=sha(out/'helper/needles.cpp'), binary_sha256=sha(native.binary), submissions=native.records),
        plots=dict(filter='illustrative analog HP1 + resonant LP2, -48..+12dB, 20..16khz; not measured PSD',
                   envelope='illustrative attack/decay or startup-hold, not production envelope',
                   onset='stored cutoff times 2^(signed sweep/12), design clamp25..14k',
                   telemetry='out unavailable; no staged measured peak', live_pcm=False),
        controls=dict(main=['colour', 'mode', 'grain rate / density', 'gain', 'tone', 'low cut', 'resonance', 'sweep', 'attack', 'decay'],
                      setup=['event length', 'sweep time'], new_parameter_slots=4,
                      persistence='keep first eight indices; explicit modern opt-in still requires implementation design',
                      sweep='hit-triggered timbral contour; stored but inactive in drone in this proposal'),
        checks=dict(duals=len(images), text_bounds_and_nonoverlap=True, proposed_hit_nonoverlap=True,
                    minimum_body_target=[32,24], shared_needle_calls=len(native.records),
                    protected_unchanged=len(before), integer_nearest_enlargement=True),
        limitations=['no module implementation, engine/controller/audio changes',
                     'no runtime application capture or device/audio/FPS acceptance',
                     'no ARM/application build, FTP, deployment or exported audio',
                     'parameter ranges and exact modern synthesis remain proposals'])
    (out/'proposal.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'README.md').write_text('# noise extended — visual study\n\n'
        'authored Pillow composition, not a running module or device capture. current native title, 6x8 text, '
        'unchanged shared needle helper; common frame14,8 and footer228.\n\n'
        '- `main-and-setup.png`: main editor and the two extra controls.\n'
        '- `both-themes.png`: identical main geometry in graphite and pink.\n'
        '- `three-main-states.png`: white hit, pink drone and sparse crackle.\n'
        '- individual duals are2x; `*-native.png` are exact400x240/320x240 proposal surfaces.\n\n'
        'plots are illustrative filter/envelope models, not FFT, live PCM or current NoiseUnit output. '
        'out remains unavailable. sweep and decay are stored but dim in drone. all DSP/mappings, '
        'input handlers, modern/legacy migration and device acceptance are still unimplemented. '
        'no production/assets were changed; source pins, geometry and native needle submissions in proposal.json.\n')
    print(out.relative_to(ROOT))
    print(f'{len(images)} authored duals / {len(native.records)} native-helper needle calls / {len(before)} protected files unchanged')
    print(out/'main-and-setup.png')


if __name__ == '__main__':
    main()
