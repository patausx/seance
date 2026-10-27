#!/usr/bin/env python3
"""Tilt extended visual study; NOT runtime, current Tilt DSP or a device capture.

Authored Pillow layout at 400x240 / 320x240. The current title mask, native
6x8 font and unchanged compiled C++ needle helper are reused. Curves are a
proposed reciprocal first-order digital tilt model, not the installed TiltUnit.
No audio export, application build, controller, serializer or DSP changes.
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
from render_motor_extended_study import CPP as NEEDLE_CPP, sha

base = skin.base
ROOT = Path(__file__).resolve().parents[1]
OUTSIDE = (18, 18, 21)
SR = 32000
X0, X1, Y0, Y1 = 38, 386, 74, 178
DB_LIMIT = 12.0


@dataclass(frozen=True)
class Example:
    slug: str
    amount: float = 6.0
    pivot: float = 1000.0
    range_db: float = 9.0
    drive: float = 0.0
    trim: float = 0.0
    mix: float = 1.0
    effective_amount: float | None = None

    @property
    def linked(self):
        return self.effective_amount is not None

    @property
    def effective(self):
        return self.amount if self.effective_amount is None else self.effective_amount


EXAMPLES = (Example('air'), Example('body', amount=-6), Example('zero', amount=0),
            Example('mod', amount=3, effective_amount=6))


@lru_cache(None)
def title_mask():
    source = (ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw = source.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    data = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', raw))
    row = re.search(r'\{UnitKind::Tilt, "tilt", (\d+), (\d+), (\d+)\}', source)
    offset, width, height = map(int, row.groups())
    assert width <= 57 and height <= 25
    mask = Image.new('L', (width, height))
    for y in range(height):
        for x in range(width):
            if data[offset+y*((width+7)//8)+x//8] & (128 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


class Needles:
    """Use production submissions, including the hollow base / solid live pair."""
    def __init__(self, out):
        folder = out/'helper'
        folder.mkdir()
        cpp = NEEDLE_CPP
        for old, new in (
            ('int theme,x0,x1,y,edit,live,linked,disabled;',
             'int theme,x0,x1,y,edit,live,linked,disabled,neutral;'),
            ('>>live>>linked>>disabled)', '>>live>>linked>>disabled>>neutral)'),
            ('{bool(linked),bool(disabled),false});',
             '{bool(linked),bool(disabled),false},neutral);'),
        ):
            assert cpp.count(old) == 1
            cpp = cpp.replace(old, new)
        self.binary = folder/'needles'
        (folder/'needles.cpp').write_text(cpp)
        command = ['g++', '-std=c++17', '-O2', '-I'+str(ROOT),
                   str(folder/'needles.cpp'), '-o', str(self.binary)]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        (folder/'build.log').write_text(result.stdout+result.stderr)
        (folder/'command.json').write_text(json.dumps(command, indent=2)+'\n')
        result.check_returncode()
        self.metrics = json.loads(subprocess.check_output([str(self.binary), 'frame'], text=True))
        assert self.metrics == dict(left=14, right=14, top=8, header_y=14,
                                    footer_y=228, route_y=35, underline_y=45)
        self.records = []

    def draw(self, p, left, right, y, amount, neutral=None, live=None, linked=False):
        # Rails are inset by one native pixel to keep endpoint heads in the frame.
        x0, x1 = left+1, right-1
        position = lambda v: x0+round((x1-x0)*max(0., min(1., v)))
        edit = position(amount)
        effective = edit if live is None else position(live)
        zero = -1 if neutral is None else position(neutral)
        request = f'{int(p.theme=="pink")} {x0} {x1} {y} {edit} {effective} {int(linked)} 0 {zero}\n'
        rects = json.loads(subprocess.check_output([str(self.binary)], input=request, text=True))
        for x, yy, w, h, color in rects:
            assert 14 <= x and x+w <= 307 and 26 <= yy and yy+h <= 224
            assert w > 0 and h > 0
            rgb = tuple((color >> shift) & 255 for shift in (0, 8, 16))
            p.d.rectangle((x, yy, x+w-1, yy+h-1), fill=rgb)
        self.records.append(dict(request=request.strip(), rectangles=rects))


def shelf(amount, pivot, hz):
    """Proposed float frequency response only, not an audio implementation.

    Bilinear transform of (k + g*s)/(k*g + s), with prewarped k.
    DC=1/g, Nyquist=g, |H(pivot)|=1, H(a)*H(-a)=1.
    These identities refer to the EQ, not arbitrary trim/drive/parallel mix.
    """
    g = 10**(amount/20)
    if hz <= 0:
        return complex(1/g)
    if hz >= SR/2:
        return complex(g)
    k = math.tan(math.pi*pivot/SR)
    s = complex(0, math.tan(math.pi*hz/SR))
    return (k+g*s)/(k*g+s)


def response(e, hz, effective=True):
    amount = e.effective if effective else e.amount
    wet = shelf(amount, e.pivot, hz)*10**((e.drive+e.trim)/20)
    # Phase-aware parallel sum. Saturation and quantization deliberately excluded.
    return 20*math.log10(max(1e-12, abs((1-e.mix)+e.mix*wet)))


def frequency_x(hz):
    return X0+round(math.log(hz/20)/math.log(800)*(X1-X0))


def response_y(db):
    return Y0+round((DB_LIMIT-max(-DB_LIMIT, min(DB_LIMIT, db)))*(Y1-Y0)/(2*DB_LIMIT))


def db_text(v):
    return '0.0db' if abs(v) < .05 else f'{v:+.1f}db'


def hz_text(v):
    return f'{v/1000:.2f}khz' if v >= 1000 else f'{v:.0f}hz'


def page(theme, top, native):
    p = base.Surface(skin.paper(theme, 'top' if top else 'bottom'), theme)
    mask = title_mask()
    p.im.paste(p.c['accent'], (14, 8), mask)
    p.labels.append(((14, 8, 14+mask.width, 8+mask.height), 'tilt / exact current title'))
    if top:
        p.txt(386, native.metrics['header_y'], 'ch01 / 02', 'muted', 'right')
        p.txt(14, native.metrics['route_y'], 'play /')
        p.txt(56, native.metrics['route_y'], 'tilt', 'accent')
        p.txt(86, native.metrics['route_y'], '/ vca')
        p.line(56, native.metrics['underline_y'], 78, native.metrics['underline_y'], 'accent')
    else:
        p.txt(56, native.metrics['header_y'], 'ch01 / 02')
        p.txt(258, native.metrics['header_y'], 'byp')
        p.txt(306, native.metrics['header_y'], 'x', 'muted', 'right')
        p.hit('bypass / unavailable under armed lock', (246, 0, 284, 26))
    return p


def curve(p, e, effective=True):
    points, values = [], []
    for x in range(X0, X1+1):
        hz = 20*800**((x-X0)/(X1-X0))
        value = response(e, hz, effective)
        points.append((x, response_y(value)))
        values.append(value)
    p.d.line(points, fill=p.c['accent' if effective else 'muted'], width=2 if effective else 1)
    if effective:
        for overflow, y, dy in ((max(values) > DB_LIMIT, Y0, 3), (min(values) < -DB_LIMIT, Y1, -3)):
            if overflow:
                p.d.line(((X1-8, y+dy), (X1-5, y), (X1-2, y+dy)), fill=p.c['accent'])
    return values


def upper(theme, e, native):
    p = page(theme, True, native)
    p.txt(386, 35, 'body / air', 'muted', 'right')
    if e.linked:
        p.line(14, 60, 26, 60, 'muted'); p.txt(32, 56, 'base '+db_text(e.amount))
        p.line(154, 60, 166, 60, 'accent', 2); p.txt(172, 56, 'effective '+db_text(e.effective), 'accent')
        p.txt(386, 56, 'mod', 'muted', 'right')
    else:
        p.txt(14, 56, 'eq response', 'accent')
        p.txt(386, 56, 'clean / wet 100%', 'muted', 'right')
    faint = base.mix(p.c['rule'], p.im.getpixel((200, 126)), 48)
    for db, label in ((12, '+12'), (0, '0'), (-12, '-12')):
        y = response_y(db)
        p.txt(32, y-4, label, 'muted', 'right')
        if db:
            for x in range(X0, X1+1, 6):
                p.d.point((x, y), fill=faint)
        else:
            p.line(X0, y, X1, y, 'rule')
    px = frequency_x(e.pivot)
    for y in range(Y0, Y1+1, 4):
        p.d.point((px, y), fill=p.c['rule'])
    if e.linked:
        curve(p, e, False)
    curve(p, e)
    py = response_y(response(e, e.pivot))
    p.d.polygon(((px, py-3), (px+3, py), (px, py+3), (px-3, py)), outline=p.c['accent'])
    for hz, label, align in ((20, '20', 'left'), (100, '100', 'center'),
                              (1000, '1k', 'center'), (16000, '16k', 'right')):
        x = frequency_x(hz)
        p.line(x, Y1+5, x, Y1+8, 'muted')
        p.txt(x, 190, label, 'muted', align)
    # EQ endpoint targets, not spectrum readings. All shown examples are clean,
    # unity trim, full wet: here they also equal the full model's DC/Nyquist gain.
    assert e.drive == e.trim == 0 and e.mix == 1
    p.txt(14, 210, 'low '+db_text(-e.effective), 'ink')
    p.txt(200, 210, 'pivot '+hz_text(e.pivot), 'muted', 'center')
    p.txt(386, 210, 'high '+db_text(e.effective), 'ink', 'right')
    p.txt(14, native.metrics['footer_y'], 'eq model / not live audio')
    p.txt(386, native.metrics['footer_y'], '+/-12db', 'muted', 'right')
    return p


def lower(theme, e, native):
    p = page(theme, False, native)
    p.txt(14, 39, 'tilt', 'accent')
    p.txt(252, 39, db_text(e.amount), 'accent', 'right')
    p.txt(306, 39, 'zero', 'accent' if e.amount == 0 else 'muted', 'right')
    p.line(283, 51, 305, 51, 'accent' if e.amount == 0 else 'rule')
    position = lambda v: .5+v/(2*e.range_db)
    native.draw(p, 14, 306, 65, position(e.amount), .5, position(e.effective), e.linked)
    p.txt(14, 78, 'body'); p.txt(306, 78, 'air', 'muted', 'right')
    # Split the Tilt caption and rail targets, leaving a real disjoint zero action.
    p.hit('tilt / select and relative drag from caption', (8, 28, 268, 55))
    p.hit('zero / tilt only, explicit zero in selected edit-bank lock', (272, 28, 312, 55))
    p.hit('tilt / relative drag, r fine', (8, 55, 312, 88))
    p.txt(14, 99, 'pivot', 'ink'); p.txt(306, 99, hz_text(e.pivot), 'ink', 'right')
    native.draw(p, 14, 306, 119, math.log(e.pivot/125)/math.log(64))
    p.txt(14, 131, '125hz'); p.txt(306, 131, '8khz', 'muted', 'right')
    p.hit('pivot / relative logarithmic drag, r fine', (8, 91, 312, 143))
    controls = (
        ('range', f'{e.range_db:.1f}db', (e.range_db-3)/9, None),
        ('drive', 'clean' if e.drive == 0 else db_text(e.drive), e.drive/12.0412, None),
        ('wet trim', db_text(e.trim), (e.trim+24)/30, .8),
        ('mix', f'{e.mix:.0%}', e.mix, None),
    )
    for i, (name, value, amount, neutral) in enumerate(controls):
        left, right = (14, 148) if i % 2 == 0 else (172, 306)
        y = 154 if i < 2 else 196
        p.txt(left, y, name)
        p.txt(right, y, value, 'ink', 'right')
        native.draw(p, left, right, y+17, amount, neutral)
        p.hit(name+' / relative drag, r fine, selected-bank lock', (left-6, y-8, right+6, y+27))
    p.txt(14, native.metrics['footer_y'], 'b back')
    p.txt(160, native.metrics['footer_y'], 'r fine', 'muted', 'center')
    p.txt(306, native.metrics['footer_y'], 'l: mod >', 'accent', 'right')
    assert len(p.hits) == 9
    return p


def dual(top, bottom):
    image = Image.new('RGB', (400, 496), OUTSIDE)
    image.paste(top.im, (0, 0)); image.paste(bottom.im, (40, 256))
    return image


def save(out, name, image, scale=2):
    path = out/name
    assert not path.exists()
    large = image.resize((image.width*scale, image.height*scale), Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo(); meta.add_text('provenance', __doc__)
    large.save(path, pnginfo=meta)
    with Image.open(path) as check:
        assert check.convert('RGB').tobytes() == large.tobytes()
        assert check.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    return dict(path=str(path.relative_to(ROOT)), sha256=sha(path), size=list(large.size))


def protected():
    result = dict(base.protected(), Makefile=sha(ROOT/'Makefile'))
    for path in sorted(ROOT.glob('seance.*')):
        if path.is_file():
            result[str(path.relative_to(ROOT))] = sha(path)
    return result


def verify_model():
    checks = 0
    for amount in (-12., -6., 0., 6., 12.):
        for pivot in (125., 1000., 8000.):
            assert abs(abs(shelf(amount, pivot, pivot))-1) < 1e-12
            assert abs(20*math.log10(abs(shelf(amount, pivot, 0)))+amount) < 1e-12
            assert abs(20*math.log10(abs(shelf(amount, pivot, SR/2)))-amount) < 1e-12
            for hz in (20., 100., 1000., 8000., 16000.):
                assert abs(shelf(amount, pivot, hz)*shelf(-amount, pivot, hz)-1) < 1e-12
                assert abs(response(replace(EXAMPLES[0], mix=0, amount=amount, pivot=pivot), hz)) < 1e-12
                checks += 2
            checks += 3
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    out = (args.out or ROOT/'ui_mockups/tilt_extended_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')).resolve()
    out.mkdir(parents=True, exist_ok=False)
    before = protected()
    native = Needles(out)
    model_checks = verify_model()
    images, layouts, outputs = {}, {}, []
    for theme in ('graphite', 'pink'):
        for e in EXAMPLES:
            top, bottom = upper(theme, e, native), lower(theme, e, native)
            for p in (top, bottom):
                for box, label in p.labels:
                    assert 14 <= box[0] < box[2] <= p.im.width-14 and 8 <= box[1] < box[3] <= 236, (label, box)
            key = theme+'-'+e.slug
            images[key] = (top, bottom)
            layouts[key] = dict(top_labels=top.labels, bottom_labels=bottom.labels, proposed_targets=bottom.hits)
            outputs.append(save(out, key+'-dual.png', dual(top, bottom)))
            outputs.append(save(out, key+'-top-native.png', top.im, 1))
            outputs.append(save(out, key+'-bottom-native.png', bottom.im, 1))
    for name, columns, subtitle in (
        ('both-themes', (('graphite / air', 'graphite-air'), ('pink / air', 'pink-air')),
         'large eq above / six native needle controls below'),
        ('body-and-zero', (('body / -6db', 'graphite-body'), ('zero / flat', 'graphite-zero')),
         'same fixed scale / same pivot / no automatic rescaling'),
        ('base-and-effective', (('stored / +6db', 'graphite-air'), ('mod example / +3 to +6db', 'graphite-mod')),
         'thin stored curve / accent effective curve / illustrative modulation'),
    ):
        board = base.Canvas(848, 584, OUTSIDE)
        board.text(16, 12, 'tilt / extended', base.COLORS['graphite']['ink'], scale=2)
        board.text(16, 38, subtitle, base.COLORS['graphite']['muted'])
        for i, (caption, key) in enumerate(columns):
            x = 16+i*416
            board.text(x, 57, caption, base.COLORS['graphite']['accent'])
            board.im.paste(dual(*images[key]), (x, 72))
        board.text(16, 574, 'design only / proposed eq model / no runtime or audio changes', base.COLORS['graphite']['muted'])
        outputs.append(save(out, name+'.png', board.im))
    assert protected() == before, 'production/assets/artifacts changed during study'
    report = dict(status='visual proposal only; awaiting approval', provenance=__doc__,
        created_at=datetime.now(timezone.utc).isoformat(), script_sha256=sha(__file__),
        protected_sha256=before, frame=native.metrics, examples=[asdict(e) for e in EXAMPLES],
        outputs=outputs, layouts=layouts,
        native_needles=dict(header_sha256=sha(ROOT/'platform/3ds/module_fader.h'),
            driver_sha256=sha(out/'helper/needles.cpp'), binary_sha256=sha(native.binary), submissions=native.records),
        model=dict(kind='proposed reciprocal first-order digital tilt, prewarped pivot; independent float response',
            formula='H=(k+g*j*tan(pi*f/sr))/(k*g+j*tan(pi*f/sr)); k=tan(pi*pivot/sr); g=10^(amount_db/20)',
            scale='20..16000hz log / fixed +/-12db / endpoint overflow marks',
            unity_pivot='EQ-only property, not arbitrary drive/trim/parallel mix',
            endpoint_readouts='DC and Nyquist EQ targets, not measurements of the incoming signal',
            modulation='static invented fixture base+3db/effective+6db, not engine telemetry',
            limitations='not current TiltUnit, fixed-point DSP, clipping simulation, PCM, spectrum or audio-quality proof'),
        proposed=dict(main=['tilt', 'pivot', 'range', 'drive', 'wet trim', 'mix'],
            range='continuous3..12db; tilt displayed as signed EQ amount, internal normalized control retained',
            pivot='125..8000hz logarithmic', trim='-24..+6db, unity at80% of rail',
            gestures='relative full-scale296px + held-r fine; zero changes only tilt; no controls implemented',
            compatibility='new explicit revision required; preserve legacy5 and currentmodern6 sound, no migration here'),
        checks=dict(authored_duals=len(images), text_nonoverlap_and_frame_bounds=True,
            proposed_targets_disjoint=True, exact_shared_needle_calls=len(native.records),
            independent_model_identities=model_checks, nearest_integer_enlargement=True,
            protected_unchanged=len(before)),
        limitations=['Pillow composition, not native module rendering or a console screenshot',
            'only the small unchanged drawing helper was compiled; no application or ARM build',
            'no controller, DSP, serializer, source assets, installed binary or deployment changes'])
    (out/'proposal.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'README.md').write_text('# tilt extended — visual proposal only\n\n'
        'Authored Pillow layout with the current Tilt title, native6x8 font and actual shared C++ needle submissions. '
        'Not runtime, current TiltUnit processing or a 3DS capture.\n\n'
        '- `both-themes.png`: graphite and pink.\n'
        '- `body-and-zero.png`: negative tilt and neutral, same fixed axes.\n'
        '- `base-and-effective.png`: static illustrative modulation state.\n'
        '- Individual duals are2x, top/bottom surfaces are native400x240/320x240.\n\n'
        'The response is an independent proposed reciprocal first-order digital EQ, with prewarped unity pivot, '
        'not installed DSP. All visible examples use drive0, trim0 and full wet; graph ignores saturation. '
        'Low/high are DC/Nyquist EQ targets, not live spectrum values. The dual curves use explicitly authored '
        'base/effective values, not captured modulation. No new behavior has been implemented.\n\n'
        'Six controls only: tilt/pivot on long rails; range/drive and wettrim/mix below. Separate zero target; '
        'relative drags, r-fine and selected-bank p-lock are proposed, not working controllers. '
        'Production/assets/root artifacts are unchanged. Wait for visual approval before implementation.\n')
    print(out.relative_to(ROOT))
    print(f'{len(images)} authored duals / {len(native.records)} native needle calls / {len(before)} protected files unchanged')
    print(out/'both-themes.png')


if __name__ == '__main__':
    main()
