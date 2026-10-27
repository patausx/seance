#!/usr/bin/env python3
"""Strain extended visual proposal; NOT a runtime or console capture.

Pillow composition using the current title bitmap, native 6x8 font and exact
compiled module_fader::needle_horizontal submissions. Waveforms come from an
unchanged private StrainUnit reset render. Motion is its silent/idle control
cycle, not live input. Proposed motion=100%, rate=.032Hz and trim=0dB correspond
to the old behavior for these examples; their new controls are NOT implemented.
Only this tool and timestamped preview outputs are written. No application build.
"""
import argparse
import csv
from dataclasses import asdict, dataclass
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


@dataclass(frozen=True)
class Example:
    slug: str
    values: tuple
    selected: str


EXAMPLES = (
    Example('rounded', (15000, 0, 15000, 12000, 9000, 24500), 'drive'),
    Example('teeth', (15000, 0, 15000, 32767, 9000, 24500), 'teeth'),
)

PROBE_CPP = r'''
#include "platform/3ds/strain_reference.h"
#include <fstream>
#include <filesystem>
#include <cstdio>
using namespace seance;
namespace r=seance::platform::ui3ds::strain_reference;
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    static_assert(ENGINE_SR==32000 && CONTROL_INTERVAL==32);
    std::filesystem::path dir(argv[1]);
    std::filesystem::create_directories(dir);
    const int values[2][6]={{15000,0,15000,12000,9000,24500},
                           {15000,0,15000,32767,9000,24500}};
    const char* names[]={"rounded","teeth"};
    for(int k=0;k<2;++k){
        StrainUnit u;
        for(int p=0;p<6;++p){u.params[p].base=values[k][p];u.params[p].snap();}
        u.reset();
        std::ofstream wave(dir/(std::string(names[k])+"-burst.tsv"));
        wave<<"sample\tin\tout\n";
        for(int block=0;block<1024;block+=32){
            q15 buffer[32];
            for(int i=0;i<32;++i)buffer[i]=r::source_sample(block+i);
            u.control_tick();u.process(buffer,32);
            for(int i=0;i<32;++i)wave<<block+i<<'\t'<<r::source_sample(block+i)<<'\t'<<buffer[i]<<'\n';
        }
        u.reset(); // separate idle reference, never labelled the burst's history
        std::ofstream motion(dir/(std::string(names[k])+"-idle.tsv"));
        motion<<"ms\tmotion\n";
        for(int t=0;t<=31250;++t){
            if(t%25==0)motion<<t<<'\t'<<u.visual_motion()<<'\n';
            u.control_tick(); // energy remains reset zero; no audio input
        }
    }
    std::puts("two actual unchanged StrainUnit 32ms reset traces; separate idle motion cycles; not live audio");
}
'''


@lru_cache(None)
def title_mask():
    source = (ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw = source.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    data = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', raw))
    row = re.search(r'\{UnitKind::Strain, "strain", (\d+), (\d+), (\d+)\}', source)
    offset, width, height = map(int, row.groups())
    assert width <= 57 and height <= 25
    mask = Image.new('L', (width, height))
    for y in range(height):
        for x in range(width):
            if data[offset+y*((width+7)//8)+x//8] & (128 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


class NativeNeedles:
    """Extend the existing study bridge only to pass native neutral markers."""
    def __init__(self, out):
        folder = out/'helper'
        folder.mkdir()
        cpp = NEEDLE_CPP
        for old, new in (
            ('int theme,x0,x1,y,edit,live,linked,disabled;',
             'int theme,x0,x1,y,edit,live,linked,disabled,neutral;'),
            ('>>live>>linked>>disabled)', '>>live>>linked>>disabled>>neutral)'),
            ('{bool(linked),bool(disabled),false});', '{bool(linked),bool(disabled),false},neutral);'),
        ):
            assert cpp.count(old) == 1
            cpp = cpp.replace(old, new)
        self.binary = folder/'needles'
        (folder/'needles.cpp').write_text(cpp)
        command = ['g++', '-std=c++17', '-O2', '-I'+str(ROOT), str(folder/'needles.cpp'), '-o', str(self.binary)]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=True)
        (folder/'needles-build.log').write_text(result.stdout+result.stderr)
        self.metrics = json.loads(subprocess.check_output([str(self.binary), 'frame'], text=True))
        assert self.metrics == dict(left=14, right=14, top=8, header_y=14, footer_y=228, route_y=35, underline_y=45)
        self.records = []

    def draw(self, p, left, right, y, amount, neutral=None):
        at = left+round((right-left)*max(0., min(1., amount)))
        zero = -1 if neutral is None else left+round((right-left)*neutral)
        request = f'{int(p.theme=="pink")} {left} {right} {y} {at} {at} 0 0 {zero}\n'
        rects = json.loads(subprocess.check_output([str(self.binary)], input=request, text=True))
        for x, yy, w, h, color in rects:
            assert 13 <= x and x+w <= 308 and 8 <= yy and yy+h <= 224
            assert w > 0 and h > 0
            p.d.rectangle((x, yy, x+w-1, yy+h-1), fill=tuple((color >> shift) & 255 for shift in (0, 8, 16)))
        self.records.append(dict(request=request.strip(), rectangles=rects))


def probe(out):
    folder = out/'reference'
    folder.mkdir()
    source, binary = folder/'probe.cpp', folder/'probe'
    source.write_text(PROBE_CPP)
    command = ['g++', '-std=gnu++17', '-O2', '-Wall', '-Wextra', '-ffunction-sections', '-fdata-sections',
               '-I'+str(ROOT), '-I'+str(ROOT/'core'), str(source), str(ROOT/'core/engine/units.cpp'),
               '-Wl,--gc-sections', '-o', str(binary)]
    (folder/'command.json').write_text(json.dumps(command, indent=2)+'\n')
    with (folder/'build.log').open('w') as log:
        subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
    run = subprocess.run([str(binary), str(folder)], text=True, capture_output=True, check=True)
    (folder/'probe.log').write_text(run.stdout+run.stderr)
    result = {}
    for e in EXAMPLES:
        def rows(suffix):
            with (folder/(e.slug+'-'+suffix+'.tsv')).open() as f:
                return [{k: int(v) for k, v in row.items()} for row in csv.DictReader(f, delimiter='\t')]
        wave, idle = rows('burst'), rows('idle')
        assert len(wave) == 1024 and len(idle) == 1251
        assert all(row['sample'] == i for i, row in enumerate(wave))
        assert all(-32768 <= row['out'] <= 32767 for row in wave)
        assert all(row['in'] == 0 for row in wave[512:])
        assert all(-16384 <= row['motion'] <= 16384 for row in idle)
        result[e.slug] = dict(wave=wave, idle=idle)
    assert [r['in'] for r in result['rounded']['wave']] == [r['in'] for r in result['teeth']['wave']]
    assert result['rounded']['wave'] != result['teeth']['wave']
    return result


def controls(e):
    drive, bias, body, teeth, feedback, mix = e.values
    pct = lambda raw: f'{round(raw*100/32767)}%'
    return (
        ('drive', f'{(32767+7*drive)/32768:.2f}x', drive/32767, None),
        ('teeth', pct(teeth), teeth/32767, None),
        ('body', pct(body), body/32767, None),
        ('bias', '0%' if not bias else f'{round(bias*100/32767):+d}%', (bias+32767)/65534, .5),
        ('feedback', pct(feedback), feedback/32767, None),
        ('motion', '100%', 1., None),
        ('wet trim', '0.0db', .75, .75),
        ('mix', pct(mix), mix/32767, None),
    )


def page(theme, top, e, metrics, setup=False):
    p = base.Surface(skin.paper(theme, 'top' if top else 'bottom'), theme)
    mask = title_mask()
    p.im.paste(p.c['accent'], (metrics['left'], metrics['top']), mask)
    p.labels.append(((14, 8, 14+mask.width, 8+mask.height), 'strain / unchanged title mask'))
    if top:
        p.txt(386, metrics['header_y'], 'ch01 / 02', 'muted', 'right')
        p.txt(14, metrics['route_y'], 'play /')
        p.txt(56, metrics['route_y'], 'strain', 'accent')
        p.txt(98, metrics['route_y'], '/ vca')
        p.line(56, metrics['underline_y'], 90, metrics['underline_y'], 'accent')
        selected = next(c for c in controls(e) if c[0] == e.selected)
        p.txt(386, metrics['route_y'], selected[0]+' '+selected[1], 'ink', 'right')
    else:
        p.txt(78, metrics['header_y'], 'ch01 / 02')
        p.txt(206, metrics['header_y'], 'main' if setup else 'setup', 'accent')
        p.txt(258, metrics['header_y'], 'byp')
        p.txt(306, metrics['header_y'], 'x', 'muted', 'right')
        p.hit('main' if setup else 'setup', (198, 0, 246, 26))
        p.hit('bypass', (246, 0, 286, 26))
    return p


def trace(p, samples, key, center, height, role):
    left, right = 34, 386
    buckets = [[] for _ in range(right-left+1)]
    for i, row in enumerate(samples):
        buckets[i*(right-left)//(len(samples)-1)].append(row[key])
    previous = None
    for col, values in enumerate(buckets):
        assert values
        yy = [center-round(v*height/32768) for v in values]
        x = left+col
        if previous is not None:
            p.line(x-1, previous, x, yy[0], role)
        p.line(x, min(yy), x, max(yy), role)
        previous = yy[-1]


def upper(theme, e, native, data):
    p = page(theme, True, e, native.metrics)
    p.line(14, 59, 26, 59, 'muted'); p.txt(32, 55, 'in')
    p.line(64, 59, 76, 59, 'accent'); p.txt(82, 55, 'out', 'accent')
    p.txt(386, 55, 'test / 250hz burst', 'muted', 'right')
    center, height = 109, 34
    faint = base.mix(p.c['rule'], p.im.getpixel((200, 109)), 56)
    for level, label in ((1, '+1'), (0, '0'), (-1, '-1')):
        y = center-level*height
        p.txt(14, y-4, label)
        if level:
            for x in range(34, 387, 5):
                p.d.point((x, y), fill=faint)
        else:
            p.d.line((34, y, 386, y), fill=faint)
    for y in range(72, 147, 4):
        p.d.point((210, y), fill=faint)
    trace(p, data['wave'], 'in', center, height, 'muted')
    trace(p, data['wave'], 'out', center, height, 'accent')
    for x, label, align in ((34, '0', 'left'), (210, '16', 'center'), (386, '32ms', 'right')):
        p.txt(x, 151, label, 'muted', align)
    p.txt(14, 170, 'body motion')
    p.txt(386, 170, 'idle / 31.25s', 'muted', 'right')
    body = e.values[2]
    y = lambda value: 213-round(max(0, min(32767, value))*27/32767)
    p.d.line((34, y(body), 386, y(body)), fill=faint)
    last = None
    for x in range(34, 387):
        raw = data['idle'][round((x-34)/352*(len(data['idle'])-1))]['motion']
        yy = y(body+raw)
        if last is not None:
            p.line(x-1, last, x, yy, 'accent')
        last = yy
    p.txt(14, native.metrics['footer_y'], 'reset reference / not live')
    p.txt(386, native.metrics['footer_y'], '1ms return', 'muted', 'right')
    return p


def control(p, native, name, value, amount, left, right, y, selected=False, neutral=None):
    p.txt(left, y, name, 'accent' if selected else 'muted')
    p.txt(right, y, value, 'accent' if selected else 'ink', 'right')
    native.draw(p, left, right, y+23, amount, neutral)
    p.hit(name+' / proposed relative gesture', (left-6, y-8, right+6, y+34))


def lower(theme, e, native):
    p = page(theme, False, e, native.metrics)
    for i, (name, value, amount, neutral) in enumerate(controls(e)):
        left, right = (14, 148) if i % 2 == 0 else (172, 306)
        control(p, native, name, value, amount, left, right, 43+i//2*46, name == e.selected, neutral)
    p.txt(14, native.metrics['footer_y'], 'b back')
    p.txt(306, native.metrics['footer_y'], 'l: mod >', 'accent', 'right')
    assert len(p.hits) == 10
    return p


def setup(theme, e, native):
    p = page(theme, False, e, native.metrics, setup=True)
    p.txt(14, 43, 'motion / setup', 'accent')
    control(p, native, 'rate', '0.032hz', math.log(.032/.01)/math.log(8/.01), 14, 306, 79, True)
    p.txt(14, 125, 'one cycle')
    p.txt(306, 125, '31.25s', 'ink', 'right')
    p.txt(14, 158, 'motion 0 = no internal movement')
    p.txt(14, 176, 'body keeps its stored tone')
    p.txt(14, 202, 'wet trim is outside the return loop')
    p.txt(14, native.metrics['footer_y'], 'b main')
    p.txt(306, native.metrics['footer_y'], 'l: mod >', 'accent', 'right')
    return p


def dual(top, bottom):
    image = Image.new('RGB', (400, 496), OUTSIDE)
    image.paste(top.im, (0, 0)); image.paste(bottom.im, (40, 256))
    return image


def save(out, name, image, scale=2):
    path = out/name
    assert not path.exists()
    scaled = image.resize((image.width*scale, image.height*scale), Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo(); meta.add_text('provenance', __doc__)
    scaled.save(path, pnginfo=meta)
    with Image.open(path) as check:
        assert check.convert('RGB').tobytes() == scaled.tobytes()
    return dict(path=str(path.relative_to(ROOT)), sha256=sha(path), size=list(scaled.size))


def protected():
    result = dict(base.protected(), Makefile=sha(ROOT/'Makefile'))
    for name in ('seance.3dsx', 'seance.elf', 'seance.smdh'):
        if (ROOT/name).is_file():
            result[name] = sha(ROOT/name)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    out = (args.out or ROOT/'ui_mockups/strain_extended_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')).resolve()
    out.mkdir(parents=True, exist_ok=False)
    before = protected()
    native = NativeNeedles(out)
    data = probe(out)
    images, outputs, layouts = {}, [], {}
    for theme in ('graphite', 'pink'):
        for e in EXAMPLES:
            top, bottom = upper(theme, e, native, data[e.slug]), lower(theme, e, native)
            variants = [('main', bottom)]
            if e.slug == 'rounded':
                variants.append(('setup', setup(theme, e, native)))
            for variant, lower_page in variants:
                key = theme+'-'+e.slug+'-'+variant
                for p in (top, lower_page):
                    for box, label in p.labels:
                        assert 14 <= box[0] < box[2] <= p.im.width-14 and 8 <= box[1] < box[3] <= 236, (label, box)
                images[key] = (top, lower_page)
                outputs.append(save(out, key+'-dual.png', dual(top, lower_page)))
                outputs.append(save(out, key+'-top-native.png', top.im, 1))
                outputs.append(save(out, key+'-bottom-native.png', lower_page.im, 1))
                layouts[key] = dict(top_labels=top.labels, bottom_labels=lower_page.labels, proposed_targets=lower_page.hits)
    for name, columns in (
        ('both-themes', (('graphite', 'graphite-rounded-main'), ('pink', 'pink-rounded-main'))),
        ('main-and-setup', (('main / eight controls', 'graphite-rounded-main'), ('setup / motion rate', 'graphite-rounded-setup'))),
        ('teeth-comparison', (('teeth 37%', 'graphite-rounded-main'), ('teeth 100%', 'graphite-teeth-main'))),
    ):
        board = base.Canvas(848, 582, OUTSIDE)
        board.text(16, 12, 'strain / extended', base.COLORS['graphite']['ink'], scale=2)
        board.text(16, 38, 'reference above / eight native needles below', base.COLORS['graphite']['muted'])
        for i, (caption, key) in enumerate(columns):
            x = 16+i*416
            board.text(x, 57, caption, base.COLORS['graphite']['accent'])
            board.im.paste(dual(*images[key]), (x, 72))
        board.text(16, 572, 'visual proposal / new controls not implemented / no live audio', base.COLORS['graphite']['muted'])
        outputs.append(save(out, name+'.png', board.im))
    assert protected() == before, 'production/assets/application artifact changed'
    report = dict(status='visual proposal only, awaiting approval', provenance=__doc__,
        created_at=datetime.now(timezone.utc).isoformat(), script_sha256=sha(__file__), frame=native.metrics,
        protected_sha256=before, examples=[asdict(e) for e in EXAMPLES], outputs=outputs, layouts=layouts,
        native_needles=dict(header_sha256=sha(ROOT/'platform/3ds/module_fader.h'),
            driver_sha256=sha(out/'helper/needles.cpp'), binary_sha256=sha(native.binary), submissions=native.records),
        references=dict(files={str(p.relative_to(out)): sha(p) for p in sorted((out/'reference').iterdir()) if p.is_file()},
            waveform='unchanged real StrainUnit, .75fs250hz16ms burst +16ms silence,32khz,reset+preblockticks',
            motion='separate actual StrainUnit idle cycle; zero energy state,31.25seconds; not burst history or live telemetry',
            scale='burst fixed +/-1fs; motion actual clamped body+motion, fixed0..100%',
            new_controls_applied=False, live_audio=False),
        proposed=dict(main=[c[0] for c in controls(EXAMPLES[0])], setup=['motion rate'],
            motion='0..100%,100 retains existing internal depth;0 proposes disabling internal auto+reactive movement',
            rate='provisional .01..8hz, displayed .032hz matches current phase increment',
            trim='provisional -18..+6db after loop tap before mix; displayed0dB is unchanged behavior',
            gestures='relative full-scale296px proposed independent of134px rail; fine bias adjustment needs implementation',
            compatibility='retain original six indices and legacy PCM; no migration or production flags implemented'),
        checks=dict(duals=len(images), text_bounds_and_nonoverlap=True, proposed_targets_nonoverlap=True,
            exact_shared_needle_calls=len(native.records), protected_unchanged=len(before)),
        limitations=['authored composition, not a native module render or hardware capture',
            'no controllers, DSP, serializer or shared frame edits', 'no application/ARM build, deployment, listening or FPS acceptance'])
    (out/'proposal.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'README.md').write_text('# strain extended — visual proposal\n\n'
        'authored Pillow layout, current private title/native6x8 and unchanged compiled shared needles. '
        'not runtime UI or console capture. new controls are not implemented.\n\n'
        '- `both-themes.png`: graphite and pink main page.\n'
        '- `main-and-setup.png`: main eight controls and the proposed motion-rate setup.\n'
        '- `teeth-comparison.png`: unchanged real DSP with only teeth changed.\n\n'
        '32ms graph is an actual unchanged private StrainUnit reset burst. lower top-screen trace is a '
        'separate idle body-motion cycle; not live input or the burst history. new knobs remain at '
        'old-behavior equivalents: motion100%,rate.032hz,trim0dB. sound/control semantics still require '
        'implementation approval. production/assets and root application artifacts are hash-protected.\n')
    print(out.relative_to(ROOT))
    print(f'{len(images)} authored duals; {len(native.records)} shared needle calls; {len(before)} protected files unchanged')
    print(out/'both-themes.png')


if __name__ == '__main__':
    main()
