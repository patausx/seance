#!/usr/bin/env python3
"""VCA extended visual proposal, NOT runtime UI, live audio or a device capture.

Authored Pillow composition at 400x240 and 320x240. Current VCA title mask,
project 6x8 bitmap text, established editor paper, unchanged compiled native
module_fader needles. Curves and the preview marker are an explicit continuous
AHD formula illustration, not VcaUnit telemetry or a sample-accurate DSP test.
Sync/retrigger/cycle controls are proposed, not implemented. No application build.
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
from render_ring_extended_study import Needles, protected, sha

base = skin.base
ROOT = Path(__file__).resolve().parents[1]
OUTSIDE = (18, 18, 21)


@dataclass(frozen=True)
class Example:
    slug: str
    attack: int = 240
    hold: int = 180
    decay: int = 780
    gain: int = 80
    floor: int = 20
    a_curve: int = 25
    d_curve: int = -55
    duck: bool = False
    sync: bool = False
    cycle: bool = False
    retrigger: str = 'soft'
    selected: str = 'decay'
    preview_ms: int = 650

    @property
    def total(self):
        return self.attack + self.hold + self.decay

    def envelope(self, t):
        if t <= 0 or t >= self.total:
            return 0.
        if t < self.attack:
            return (t / self.attack) ** (2 ** (self.a_curve / 50))
        if t < self.attack + self.hold:
            return 1.
        return (1 - (t-self.attack-self.hold)/self.decay) ** (2 ** (-self.d_curve/50))

    def opening(self, t):
        e = self.envelope(t)
        return self.floor/100 + (1-self.floor/100) * (1-e if self.duck else e)

    def stage(self, t):
        if t < 0 or t >= self.total:
            return 'idle'
        return 'attack' if t < self.attack else 'hold' if t < self.attack+self.hold else 'decay'


EXAMPLES = (
    Example('open'),
    Example('duck', attack=125, hold=125, decay=250, gain=100, floor=15,
            a_curve=-30, d_curve=35, duck=True, sync=True, cycle=True,
            preview_ms=375),
)


@lru_cache(None)
def title_mask():
    text = (ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw = text.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    data = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', raw))
    row = re.search(r'\{UnitKind::Vca, "vca", (\d+), (\d+), (\d+)\}', text)
    offset, width, height = map(int, row.groups())
    assert (width, height) == (26, 12)
    mask = Image.new('L', (width, height))
    for y in range(height):
        for x in range(width):
            if data[offset+y*((width+7)//8)+x//8] & (128 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


def time_text(e, name):
    if e.sync:
        # Explicit 120bpm fixture: 1/16=125ms, 1/8=250ms. No shipping sync mapping.
        return {'attack': '1/16', 'hold': '1/16', 'decay': '1/8'}[name]
    v = getattr(e, name)
    return f'{v}ms' if v < 1000 else f'{v/1000:g}s'


def controls(e):
    def tm(name):
        return math.log(max(1, getattr(e, name))) / math.log(8000)
    return (
        ('attack', time_text(e, 'attack'), tm('attack'), False),
        ('hold', time_text(e, 'hold'), tm('hold'), False),
        ('decay', time_text(e, 'decay'), tm('decay'), False),
        ('gain', f'{e.gain}%', e.gain/100, False),
        ('a curve', f'{e.a_curve:+d}', (e.a_curve+100)/200, True),
        ('d curve', f'{e.d_curve:+d}', (e.d_curve+100)/200, True),
        ('floor', f'{e.floor}%', e.floor/100, False),
    )


def page(theme, top, e, native, setup=False):
    p = base.Surface(skin.paper(theme, 'top' if top else 'bottom'), theme)
    mask = title_mask()
    p.im.paste(p.c['accent'], (14, 8), mask)
    p.labels.append(((14, 8, 40, 20), 'vca / unchanged native title'))
    if top:
        p.txt(386, 14, 'ch01 / 02', 'muted', 'right')
        p.txt(14, 35, 'play /')
        p.txt(56, 35, 'vca', 'accent')
        p.txt(80, 35, '/ rev')
        p.line(56, 45, 72, 45, 'accent')
        chosen = next(c for c in controls(e) if c[0] == e.selected)
        p.txt(386, 35, chosen[0]+' '+chosen[1], 'ink', 'right')
    else:
        p.txt(49, 14, 'ch01 / 02')
        p.txt(206, 14, 'main' if setup else 'setup', 'accent')
        p.txt(258, 14, 'byp')
        p.txt(306, 14, 'x', 'muted', 'right')
        p.hit('main' if setup else 'setup', (198, 0, 246, 26))
        p.hit('bypass / blocked in lock mode', (246, 0, 284, 26))
        p.hit('close editor', (286, 0, 320, 26))
    return p


def dotted(p, x0, y0, x1, y1, color, spacing=4):
    n = max(abs(x1-x0), abs(y1-y0))
    for i in range(0, n+1, spacing):
        p.d.point((round(x0+(x1-x0)*i/max(1, n)), round(y0+(y1-y0)*i/max(1, n))), fill=color)


def upper(theme, e, native):
    p = page(theme, True, e, native)
    p.txt(14, 55, 'opening')
    p.txt(386, 55, 'sync / 120bpm' if e.sync else f'free / {e.total/1000:.2f}s', 'muted', 'right')
    left, right, high, low = 34, 386, 88, 178
    px = lambda t: left+round((right-left)*t/e.total)
    py = lambda v: low-round((low-high)*v)
    faint = base.mix(p.c['rule'], p.im.getpixel((200, 140)), 42)
    times = (0, e.attack, e.attack+e.hold, e.total)
    for name, start, end in zip(('attack', 'hold', 'decay'), times, times[1:]):
        role = 'accent' if e.selected in (name, 'a curve' if name == 'attack' else 'd curve' if name == 'decay' else name) else 'muted'
        p.txt((px(start)+px(end))//2, 69, name, role, 'center')
    p.txt(14, high-4, '100')
    p.txt(14, low-4, '0')
    dotted(p, left, high, right, high, faint)
    p.d.line((left, low, right, low), fill=faint)
    dotted(p, left, py(e.floor/100), right, py(e.floor/100), faint)
    for t in times[1:-1]:
        dotted(p, px(t), high-5, px(t), low, faint)
    for name, start, end in zip(('attack', 'hold', 'decay'), times, times[1:]):
        role = 'accent' if e.selected in (name, 'a curve' if name == 'attack' else 'd curve' if name == 'decay' else name) else 'ink'
        points = [(px(start+(end-start)*i/400), py(e.opening(start+(end-start)*i/400))) for i in range(401)]
        p.d.line(points, fill=p.c[role], width=2)
    # Small junctions are not draggable controls: the upper LCD is not touch-sensitive.
    for t in times:
        x, y = px(t), py(e.opening(t))
        p.d.rectangle((x-1, y-1, x+1, y+1), fill=p.c['ink'])
    t = min(e.total, max(0, e.preview_ms))
    x, y = px(t), py(e.opening(t))
    dotted(p, x, y+6, x, low, base.mix(p.c['accent'], p.im.getpixel((x, low)), 40))
    p.d.ellipse((x-4, y-4, x+4, y+4), outline=p.c['accent'])
    p.d.rectangle((x-1, y-1, x+1, y+1), fill=p.c['accent'])
    for i in range(5):
        x = left+(right-left)*i//4
        p.line(x, low+2, x, low+4, 'rule')
        label = ('0', '1/16', '1/8', '3/16', '1/4')[i] if e.sync else str(e.total*i//4)+('ms' if i == 4 else '')
        p.txt(x, 187, label, 'muted', 'left' if i == 0 else 'right' if i == 4 else 'center')
    opening = e.opening(t)
    multiplier = opening*e.gain/100
    db = '-inf' if multiplier == 0 else f'{20*math.log10(multiplier):.1f}'
    p.txt(14, 207, f'opening {opening:.0%}', 'ink')
    p.txt(386, 207, f'multiplier {multiplier:.0%} / {db}db', 'accent', 'right')
    p.txt(14, 228, 'shape preview / not live')
    p.txt(386, 228, 'preview: '+e.stage(t), 'muted', 'right')
    return p


def option(p, label, x, y, active, box, group):
    p.txt(x, y, label, 'accent' if active else 'muted')
    if active:
        p.line(x, y+12, x+len(label)*6-2, y+12, 'accent')
    p.hit(group+' / '+label, box)


def footer(p, setup=False):
    p.txt(14, 228, 'b main' if setup else 'b back')
    p.txt(118, 228, 'a trig' if setup else 'a trig / r fine')
    p.txt(306, 228, 'l: mod >', 'accent', 'right')


def lower(theme, e, native):
    p = page(theme, False, e, native)
    for i, (name, value, amount, bipolar) in enumerate(controls(e)):
        left, right = (14, 148) if i % 2 == 0 else (172, 306)
        y = 43+i//2*46
        p.txt(left, y, name, 'accent' if name == e.selected else 'muted')
        p.txt(right, y, value, 'accent' if name == e.selected else 'ink', 'right')
        native.draw(p, left, right, y+23, amount, bipolar=bipolar)
        p.hit(name+' / proposed relative drag; r fine', (left-6, y-8, right+6, y+34))
    p.txt(172, 181, 'action')
    option(p, 'open', 174, 203, not e.duck, (164, 193, 232, 220), 'action')
    option(p, 'duck', 268, 203, e.duck, (244, 193, 312, 220), 'action')
    footer(p)
    assert len(p.hits) == 12
    return p


def setup(theme, e, native):
    p = page(theme, False, e, native, setup=True)
    p.txt(14, 45, 'time', 'ink')
    option(p, 'free', 174, 45, not e.sync, (162, 34, 232, 62), 'time')
    option(p, 'sync', 268, 45, e.sync, (246, 34, 312, 62), 'time')
    p.txt(14, 69, 'note lengths / project tempo' if e.sync else 'milliseconds / independent stage times')
    p.line(14, 89, 306, 89)
    p.txt(14, 105, 'retrigger', 'ink')
    for label, x, box in (('soft', 108, (96, 94, 156, 122)),
                          ('reset', 180, (168, 94, 228, 122)),
                          ('ignore', 258, (240, 94, 312, 122))):
        option(p, label, x, 105, e.retrigger == label, box, 'retrigger')
    p.txt(14, 129, 'restarts from current envelope')
    p.line(14, 149, 306, 149)
    p.txt(14, 165, 'run', 'ink')
    option(p, 'one shot', 156, 165, not e.cycle, (144, 154, 228, 182), 'run')
    option(p, 'cycle', 270, 165, e.cycle, (246, 154, 312, 182), 'run')
    p.txt(14, 189, 'repeats / source stays untouched' if e.cycle else 'one trigger / one envelope')
    p.txt(14, 209, 'time + curve latch at stage entry')
    footer(p, True)
    assert len(p.hits) == 10
    return p


def dual(top, bottom):
    im = Image.new('RGB', (400, 496), OUTSIDE)
    im.paste(top.im, (0, 0))
    im.paste(bottom.im, (40, 256))
    return im


def save(out, name, image, scale=2):
    path = out/name
    assert not path.exists()
    scaled = image.resize((image.width*scale, image.height*scale), Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    scaled.save(path, pnginfo=meta)
    with Image.open(path) as actual:
        assert actual.convert('RGB').tobytes() == scaled.tobytes()
        assert actual.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    return dict(path=str(path.relative_to(ROOT)), sha256=sha(path), size=list(scaled.size))


def check_model():
    checks = 0
    for e in EXAMPLES:
        for duck in (False, True):
            model = replace(e, duck=duck)
            assert model.envelope(0) == model.envelope(model.total) == 0
            assert model.envelope(model.attack) == 1
            for i in range(1001):
                t = model.total*i/1000
                assert model.floor/100-1e-12 <= model.opening(t) <= 1+1e-12
                assert abs(model.opening(t)+replace(model, duck=not duck).opening(t)-(1+model.floor/100)) < 1e-12
                checks += 2
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    out = (args.out or ROOT/'ui_mockups/vca_extended_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')).resolve()
    out.mkdir(parents=True, exist_ok=False)
    before = protected()
    native = Needles(out)
    model_checks = check_model()
    images, layouts, outputs = {}, {}, []
    for theme in ('graphite', 'pink'):
        for e in EXAMPLES:
            top = upper(theme, e, native)
            for mode, bottom in (('main', lower(theme, e, native)), ('setup', setup(theme, e, native))):
                key = theme+'-'+e.slug+'-'+mode
                for p in (top, bottom):
                    for box, label in p.labels:
                        assert 14 <= box[0] < box[2] <= p.im.width-14 and 8 <= box[1] < box[3] <= 236, (label, box)
                images[key] = (top, bottom)
                outputs.append(save(out, key+'-dual.png', dual(top, bottom)))
                outputs.append(save(out, key+'-top-native.png', top.im, 1))
                outputs.append(save(out, key+'-bottom-native.png', bottom.im, 1))
                layouts[key] = dict(top_labels=top.labels, bottom_labels=bottom.labels, proposed_targets=bottom.hits)
    for name, columns in (
        ('main-and-setup', (('main / seven needles + action', 'graphite-open-main'),
                            ('setup / time + retrigger + run', 'graphite-open-setup'))),
        ('open-and-duck', (('open / free / one shot', 'graphite-open-main'),
                          ('duck / sync / cycle', 'pink-duck-main'))),
        ('both-themes', (('graphite', 'graphite-open-main'), ('pink', 'pink-open-main'))),
    ):
        board = base.Canvas(848, 584, OUTSIDE)
        board.text(16, 12, 'vca / extended', base.COLORS['graphite']['ink'], scale=2)
        board.text(16, 38, 'opening above / native controls below', base.COLORS['graphite']['muted'])
        for i, (caption, key) in enumerate(columns):
            x = 16+i*416
            board.text(x, 57, caption, base.COLORS['graphite']['accent'])
            board.im.paste(dual(*images[key]), (x, 72))
        board.text(16, 574, 'visual proposal / new modes not implemented / marker is a model fixture', base.COLORS['graphite']['muted'])
        outputs.append(save(out, name+'.png', board.im))
    assert protected() == before, 'production/assets/application artifacts changed during study'
    report = dict(status='visual proposal only; awaiting feedback', provenance=__doc__,
        created_at=datetime.now(timezone.utc).isoformat(), script_sha256=sha(__file__),
        examples=[asdict(e) for e in EXAMPLES], outputs=outputs, layouts=layouts,
        frame=native.metrics, protected_sha256=before,
        native_needles=dict(header_sha256=sha(ROOT/'platform/3ds/module_fader.h'),
            bridge_source_sha256=sha(ROOT/'tools/render_ring_extended_study.py'),
            driver_sha256=sha(out/'helper/needles.cpp'), binary_sha256=sha(native.binary), submissions=native.records),
        model=dict(formula='continuous from-rest AHD; p**2**(2*bend), decay inverse bend; floor+(1-floor)*E or (1-E)',
            multiplier='gain * opening; dB is 20log10(multiplier), NOT measured PCM/dBFS',
            time_axis='linear physical elapsed time, no artificially widened stages',
            sync='authored 120bpm note-length fixture only; not an implemented clock mapping',
            marker='authored example at 650ms/375ms; not live, not retrigger history',
            rail_mapping='time needles keep logarithmic physical time positions in both fixtures; sync snapping law not finalized'),
        design=dict(main=['attack', 'hold', 'decay', 'gain', 'a curve', 'd curve', 'floor', 'open/duck'],
            setup=['free/sync', 'soft/reset/ignore retrigger', 'one shot/cycle'],
            deferred=['adsr/note-off', 'sidechain detector', 'new sound or persistence implementation'],
            gesture='proposed relative controls and r fine; no hit/controller implementation'),
        checks=dict(duals=len(images), text_bounds_and_nonoverlap=True, proposed_targets_disjoint=True,
            exact_needle_calls=len(native.records), continuous_model_checks=model_checks,
            nearest_integer_enlargement=True, protected_unchanged=len(before)),
        limitations=['authored composition, not native runtime or device UI',
            'no application or ARM build; only unchanged native drawing helper is compiled',
            'no DSP/controllers/serialization/assets/shared-frame edits, audio exports, FTP, listening or FPS proof'])
    (out/'proposal.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'README.md').write_text('# vca extended — visual proposal only\n\n'
        'main-and-setup.png: proposed main controls and mode setup.\n'
        'open-and-duck.png: free open vs clocked duck examples, graphite/pink.\n'
        'both-themes.png: identical open fixture in both themes.\n\n'
        'current title mask, project6x8 and unchanged compiled C++ needles; fixed frame14/8/footer228. '
        'authored Pillow pixels, NOT current runtime/device capture. formula/marker/readouts are consistent '
        'illustrations of an envelope, NOT audio waveforms or live telemetry. gain dB is a multiplier. '
        'new sync/retrigger/cycle modes and input gestures remain unimplemented. no production edits.\n\n'
        'references: https://www.cableguys.com/volumeshaper ; https://deviousmachines.com/duck/ ; '
        'https://intellijel.com/shop/eurorack/quadrax/ ; https://www.fabfilter.com/help/pro-g/using/metering\n')
    print(out.relative_to(ROOT))
    print(f'{len(images)} duals; {len(native.records)} native needle calls; {model_checks} model checks; {len(before)} protected files unchanged')
    print(out/'main-and-setup.png')


if __name__ == '__main__':
    main()
