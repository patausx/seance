#!/usr/bin/env python3
"""Flow ribbon + retrigger pips: authored design study, NOT application capture.

Pillow at 400x240 / 320x240, current native 6x8 font, production palette,
sequence grid and compiled, unmodified lancet + numeric bitmap helpers.
All pattern values are an explicit illustrative fixture. No engine/controller,
audio, persistence, application build or device work. New gestures are proposals.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, PngImagePlugin
from render_module_geometry_study import Canvas, protected_hashes

ROOT = Path(__file__).resolve().parents[1]
OUTSIDE = (18, 18, 21)
ROWS = (45, 83, 121, 159)
PIPS = {1: ((0, 0),), 2: ((-4, -4), (4, 4)),
        3: ((-4, -4), (0, 0), (4, 4)),
        4: ((-4, -4), (4, -4), (-4, 4), (4, 4))}
# None is inheritance from base, NOT a hold from the preceding lock.
DENS = (None, None, 48, None, 72, 72, 72, None,
        24, 36, 54, 80, None, None, 56, None)
GAIN = (None, None, None, None, 68, 68, None, None,
        18, 18, 18, 18, None, 76, None, None)
EVENTS = ({0: 1, 4: 1, 8: 1, 11: 2, 12: 1, 15: 3},
          {0: 1, 8: 1}, {2: 1, 6: 2, 10: 1, 14: 4}, {})

CPP = r'''
#include "platform/3ds/module_fader.h"
#include "platform/3ds/inspector_gothic.h"
#include "platform/3ds/sequencer_layout.h"
#include <iostream>
#include <string>
#include <vector>
namespace p=seance::platform::ui3ds::palette;
namespace f=seance::platform::ui3ds::module_fader;
namespace s=seance::platform::ui3ds::seq_layout;
struct R {int x,y,w,h; uint32_t c;};
std::vector<R> rs;
namespace seance::ui {
void rect(float x,float y,float w,float h,uint32_t c) {rs.push_back({int(x),int(y),int(w),int(h),c});}
}
int main() {
 char op; int theme;
 while(std::cin>>op>>theme) {
  p::set(theme?p::Theme::Pink:p::Theme::Graphite); rs.clear();
  if(op=='p') {
   std::cout<<"{\"colors\":["<<p::ink()<<','<<p::spot()<<','<<p::ghost()<<','<<p::subdued()<<','<<p::cell()<<','<<p::inverse()<<','<<p::playhead()<<"],\"paper\":[";
   for(int y=0;y<240;++y){if(y)std::cout<<',';std::cout<<p::paper_color(y);}
   std::cout<<"],\"grid\":["<<s::X0<<','<<s::PITCH<<','<<s::CELL_W<<','<<s::CELL_H<<','<<s::BADGE_H<<','<<s::ROWS[0]<<','<<s::ROWS[1]<<','<<s::ROWS[2]<<','<<s::ROWS[3]<<"]}\n";continue;
  }
  if(op=='f') {int v;std::cin>>v;const int at=s::VALUE_X0+(s::VALUE_X1-s::VALUE_X0)*v/100;
   f::horizontal(s::VALUE_X0,s::VALUE_X1,s::VALUE_Y,at,at);}
  if(op=='n') {int x,y;std::string n;std::cin>>x>>y>>n;
   seance::platform::ui3ds::inspector_gothic::draw_numeric(x,y,n.c_str(),p::ink(),p::spot());}
  std::cout<<'[';
  for(size_t i=0;i<rs.size();++i){const auto&r=rs[i];if(i)std::cout<<',';
   std::cout<<'['<<r.x<<','<<r.y<<','<<r.w<<','<<r.h<<','<<r.c<<']';}
  std::cout<<"]\n";
 }
}
'''


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def rgb(v):
    return tuple((v >> shift) & 255 for shift in (0, 8, 16))


def mix(a, b, amount):
    return tuple(round(x*(1-amount)+y*amount) for x, y in zip(a, b))


class Helpers:
    def __init__(self, out):
        directory = out/'helper'
        directory.mkdir()
        source, self.binary = directory/'drawing.cpp', directory/'drawing'
        source.write_text(CPP)
        result = subprocess.run(['g++', '-std=c++17', '-O2', '-I'+str(ROOT),
                                 str(source), '-o', str(self.binary)],
                                capture_output=True, text=True, check=True)
        (directory/'build.log').write_text(result.stdout+result.stderr)
        self.records = []
        self.palettes = {}
        for theme in ('graphite', 'pink'):
            raw = self.query('p', theme)
            assert raw['grid'] == [8, 19, 18, 31, 18, *ROWS]
            self.palettes[theme] = dict(colors=dict(zip(
                ('ink', 'accent', 'ghost', 'rule', 'cell', 'inverse', 'play'),
                map(rgb, raw['colors']))), paper=list(map(rgb, raw['paper'])))

    def query(self, op, theme, args=''):
        request = f'{op} {int(theme=="pink")} {args}\n'
        result = json.loads(subprocess.check_output([str(self.binary)], input=request, text=True))
        self.records.append(dict(request=request.strip(), result=result))
        return result

    def draw(self, page, op, args):
        rects = self.query(op, page.theme, args)
        for x, y, w, h, color in rects:
            page.rect(x, y, w, h, rgb(color))
        return max(x+w for x, y, w, h, color in rects)


class Page(Canvas):
    def __init__(self, width, theme, native):
        super().__init__(width)
        self.theme, self.native = theme, native
        self.c = native.palettes[theme]['colors']
        for y, color in enumerate(native.palettes[theme]['paper']):
            self.d.line((0, y, width-1, y), fill=color)

    def txt(self, x, y, text, role='ghost', right=False):
        self.text(x, y, text, self.c[role], right=right)

    def color(self, role):
        return self.c[role] if isinstance(role, str) else role

    def rect(self, x, y, w, h, role):
        assert 0 <= x < x+w <= self.im.width and 0 <= y < y+h <= self.im.height
        self.d.rectangle((x, y, x+w-1, y+h-1), fill=self.color(role))

    def line(self, x0, y0, x1, y1, role='rule', width=1):
        assert 0 <= min(x0, x1) <= max(x0, x1) < self.im.width
        assert 0 <= min(y0, y1) <= max(y0, y1) < self.im.height
        self.d.line((x0, y0, x1, y1), fill=self.color(role), width=width)

    def point(self, cx, cy, role, hollow=False):
        # Four-pixel rounded disc: 2/4/4/2 spans, not a tiny plus-shaped glyph.
        if hollow:
            self.d.rectangle((cx-2, cy-2, cx+2, cy+2), outline=self.color(role))
        else:
            self.rect(cx-1, cy-2, 2, 4, role)
            self.rect(cx-2, cy-1, 4, 2, role)

    def badge(self, x, y, role='cell'):
        for dx, dy, w, h in ((2, 0, 14, 1), (1, 1, 16, 1), (0, 2, 18, 14),
                              (1, 16, 16, 1), (2, 17, 14, 1)):
            self.rect(x+dx, y+dy, w, h, role)

    def dice(self, x, y, count, role='ink'):
        assert 1 <= count <= 4
        for dx, dy in PIPS[count]:
            self.point(x+dx, y+dy, role)


def ribbon(p, x0, y0, pitch, values, base, selected=-1, height=17, events=None,
           selection=(), conditional=(), guide=True):
    """Stored piecewise-constant profile; no interpolation or live-audio claim."""
    bottom = y0+height
    ys = [bottom-round((base if v is None else v)*height/100) for v in values]
    bg = p.im.getpixel((x0, y0))
    grid = mix(bg, p.c['rule'], .25)
    base_ink = mix(bg, p.c['ghost'], .65)
    if selected >= 0:
        x = x0+selected*pitch
        p.rect(x, y0-2, pitch-1, height+5, mix(bg, p.c['accent'], .15))
        p.line(x+2, y0-3, x+pitch-4, y0-3, 'accent')
        p.line(x+2, bottom+3, x+pitch-4, bottom+3, 'accent')
    if selection:
        for i in selection:
            p.rect(x0+i*pitch, y0-2, pitch-1, height+5, mix(bg, p.c['accent'], .15))
        left, right = x0+min(selection)*pitch, x0+(max(selection)+1)*pitch-2
        p.line(left, bottom+3, right, bottom+3, 'accent')
        p.line(left, bottom+1, left, bottom+3, 'accent')
        p.line(right, bottom+1, right, bottom+3, 'accent')
    for i in range(len(values)):
        x = x0+i*pitch
        if guide and i%4 == 0:
            p.line(x-1 if x else x, y0, x-1 if x else x, bottom, grid)
        # Each step has its own faint time tick, not an event or written lock.
        p.rect(x+pitch//2, bottom+2, 1, 1, grid)
        if i:
            p.line(x-1, ys[i-1], x-1, ys[i], base_ink)
        role = ('accent' if i == selected or i in selection else 'ink') if values[i] is not None else base_ink
        p.line(x, ys[i], x+pitch-2, ys[i], role, 2 if values[i] is not None else 1)
        if values[i] is not None:
            p.point(x+pitch//2, ys[i], role, hollow=i in conditional)
        if events and i in events:
            p.rect(x+pitch//2-1, bottom+6, 3, 3, 'ink')
    return ys


def event_row(p, row, retrig=False, selected=-1):
    y = ROWS[row]
    for i in range(16):
        x = 8+i*19
        if i in EVENTS[row]:
            p.badge(x, y, 'accent' if i == selected else 'cell')
            role = 'inverse' if i == selected else 'ink'
            if retrig:
                p.dice(x+8, y+8, EVENTS[row][i], role)
            else:
                p.rect(x+5, y+5, 7, 7, role)
        else:
            p.rect(x+7, y+7, 3, 3, 'rule')
    for beat in range(4):
        p.line(8+beat*76, y+26, 8+beat*76+72, y+26,
               mix(p.im.getpixel((8, y+26)), p.c['rule'], .25))


def lower(theme, native, mode='ribbon'):
    p = Page(320, theme, native)
    # A stopped/silent fixture: no invented scope trace or animated audio meter.
    p.line(0, 22, 319, 22, mix(p.im.getpixel((0, 22)), p.c['rule'], .45))
    p.txt(8, 7, 'edit 1 / 01-16')
    p.txt(312, 7, 'write' if mode == 'write' else 'retrig' if mode == 'dice' else 'score',
          'accent', right=True)
    labels = ('01 play', '02 gran', '03 fm', '04 noise')
    kinds = ('strike', 'flow / density', 'strike', 'flow / gain')
    for row, y in enumerate(ROWS):
        p.txt(8, y-9, labels[row], 'accent' if row == 1 and mode != 'dice' else 'ghost')
        p.txt(312, y-9, kinds[row] if mode != 'dice' else 'hits', right=True)
    if mode == 'dice':
        for row in range(4):
            event_row(p, row, True, 14 if row == 2 else -1)
    else:
        event_row(p, 0)
        event_row(p, 2)
        values = tuple(72 if 8 <= i <= 11 else v for i, v in enumerate(DENS)) if mode == 'write' else DENS
        ribbon(p, 8, ROWS[1]+3, 19, values, 32,
               selected=4 if mode != 'write' else -1,
               events=EVENTS[1], selection=tuple(range(8, 12)) if mode == 'write' else ())
        ribbon(p, 8, ROWS[3]+3, 19, GAIN, 44)
    p.line(8, 192, 311, 192, mix(p.im.getpixel((8, 192)), p.c['rule'], .3))
    if mode == 'dice':
        p.txt(8, 198, 'retrig', 'accent')
        p.txt(98, 198, '3:15')
        p.txt(312, 198, '4 hits', 'ink', right=True)
        for count in range(1, 5):
            x = 68+(count-1)*61
            p.dice(x, 218, count, 'accent' if count == 4 else 'ghost')
            if count == 4:
                p.line(x-8, 227, x+8, 227, 'accent')
        p.txt(8, 232, 'one step / 1-4 total hits')
        p.txt(312, 232, 'r mode', right=True)
    else:
        p.txt(8, 198, 'density >', 'accent')
        p.txt(96, 198, '2:09-12' if mode == 'write' else '2:05')
        p.txt(312, 198, '72%', 'ink', right=True)
        native.draw(p, 'f', '72')
        p.txt(8, 232, 'four locks / one undo' if mode == 'write' else 'tap select / base 32')
        p.txt(312, 232, 'done' if mode == 'write' else 'write', 'accent', right=True)
    return p


def upper(theme, native):
    p = Page(400, theme, native)
    p.txt(16, 14, 'edit 1 / play 1', 'accent')
    p.txt(384, 14, '120 bpm / stop', right=True)
    p.txt(16, 36, 'gran / filt / out', 'ink')
    p.txt(384, 36, 'chain 02 / flow', right=True)
    p.line(16, 49, 39, 49, 'accent')
    p.txt(16, 65, 'density', 'accent')
    end = native.draw(p, 'n', '16 116 72')
    p.txt(end+4, 109, '%', 'ink')
    p.txt(16, 134, 'step 05 / lock', 'ink')
    p.txt(16, 151, 'base 32%')
    p.txt(16, 168, '1 of 4 locks')
    p.txt(16, 185, 'trig off')
    p.txt(16, 209, 'one step only', 'accent')
    p.txt(154, 65, 'stored values', 'ink')
    p.txt(384, 65, '01-16', right=True)
    ribbon(p, 158, 91, 14, DENS, 32, 4, height=78, events=EVENTS[1])
    for i in (0, 4, 8, 12):
        p.txt(158+i*14, 184, f'{i+1:02}', 'accent' if i == 4 else 'ghost')
    p.txt(154, 209, 'pale base / dot lock')
    p.txt(16, 228, 'stored / no glide')
    p.txt(384, 228, 'l tools / r mode', right=True)
    return p


def dual(top, bottom):
    im = Image.new('RGB', (400, 496), OUTSIDE)
    im.paste(top.im, (0, 0))
    im.paste(bottom.im, (40, 256))
    return im


def save(out, name, im, scale=2):
    path = out/name
    assert not path.exists(), path
    image = im.resize((im.width*scale, im.height*scale), Image.Resampling.NEAREST)
    info = PngImagePlugin.PngInfo()
    info.add_text('provenance', __doc__)
    image.save(path, pnginfo=info)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes() == image.tobytes()
        assert decoded.resize(im.size, Image.Resampling.NEAREST).tobytes() == im.tobytes()
    return dict(path=str(path.relative_to(ROOT)), size=list(image.size), sha256=sha(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    out = (args.out or ROOT/'ui_mockups/flow_ribbon_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')).resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out/'renderer.py').write_bytes(Path(__file__).read_bytes())
    before = protected_hashes()
    native = Helpers(out)
    pages, outputs = {}, []
    for theme in ('graphite', 'pink'):
        top = upper(theme, native)
        for mode in ('ribbon', 'write', 'dice'):
            bottom = lower(theme, native, mode)
            pages[theme, mode] = bottom
            outputs.append(save(out, f'{theme}-{mode}-native.png', bottom.im, 1))
            outputs.append(save(out, f'{theme}-{mode}-2x.png', bottom.im))
        outputs.append(save(out, f'{theme}-dual-2x.png', dual(top, pages[theme, 'ribbon'])))
        pages[theme, 'top'] = top
    board = Canvas(848, 578, OUTSIDE)
    ink, muted, accent = (244, 244, 244), (184, 184, 184), (181, 154, 232)
    board.text(16, 12, 'seance / flow ribbon', ink, scale=2)
    board.text(16, 40, 'design study / stored steps, not a waveform / native grid', muted)
    for column, theme in enumerate(('graphite', 'pink')):
        x = 16+column*416
        board.text(x, 60, theme+' / two event lanes + two flow lanes', accent)
        board.im.paste(dual(pages[theme, 'top'], pages[theme, 'ribbon']), (x, 76))
    outputs.append(save(out, 'ribbon-overview-2x.png', board.im))
    detail = Canvas(688, 432, OUTSIDE)
    detail.text(16, 12, 'flow writing / retrigger dice', ink, scale=2)
    for x, title, mode in ((16, '01 / proposed write gesture', 'write'),
                           (352, '02 / same 1-4 hits, new marks', 'dice')):
        detail.text(x, 40, title, accent)
        detail.im.paste(pages['graphite', mode].im, (x, 58))
    detail.text(16, 314, '09-12: four explicit locks at 72%', ink)
    detail.text(16, 330, '13 returns to base / no hidden hold', muted)
    detail.text(352, 314, 'diagonal 2 / diagonal 3 / corner 4', ink)
    detail.text(352, 330, 'count means total hits, not probability', muted)
    # Exact integer magnification of the proposed native step pips.
    dice = Page(320, 'graphite', native)
    for count in range(1, 5):
        dice.badge(2+(count-1)*27, 2)
        dice.dice(10+(count-1)*27, 10, count)
    crop = dice.im.crop((0, 0, 105, 22))
    detail.im.paste(crop.resize((315, 66), Image.Resampling.NEAREST), (352, 348))
    detail.text(16, 369, 'same 16 columns / 19px pitch', muted)
    detail.text(16, 385, 'native lancet for precise edits', muted)
    detail.text(16, 416, 'design study only / no runtime or sound changes', accent)
    outputs.append(save(out, 'ribbon-write-and-dice-2x.png', detail.im))
    crop = pages['graphite', 'ribbon'].im.crop((0, 72, 320, 112))
    outputs.append(save(out, 'ribbon-detail-4x.png', crop, 4))
    assert before == protected_hashes(), 'production/assets/application artifacts changed'
    report = dict(status='visual proposal only; awaiting feedback', provenance=__doc__,
                  created_at=datetime.now(timezone.utc).isoformat(), outputs=outputs,
                  protected_sha256=before, script_sha256=sha(__file__),
                  helper_sha256=sha(native.binary), helper_submissions=native.records,
                  fixture=dict(density=DENS, density_base=32, gain=GAIN, gain_base=44,
                               events=EVENTS, selected_chain=2, selected_step=5,
                               transport='stopped; no live audio', write_steps=[9, 10, 11, 12], write_value=72),
                  semantics=['None returns to base on that step; no inferred hold/glide',
                             'node=written parameter; small square below=independent trigger',
                             'write group shows four ordinary locks, not a new sustained-note object',
                             'dice 1..4 are total retrigger hits, not probability',
                             'new source labels, selection brackets and write entry are design only'],
                  checks=dict(native_sizes=True, native_font_text_bounds_and_nonoverlap=True,
                              primitive_bounds=True, exact_palette_and_lancet=True,
                              production_grid_checked=True, nearest_scaling=True,
                              production_unchanged=len(before)),
                  limitations=['authored Pillow layout, not a native app or hardware capture',
                               'no new interactive controller, undo, engine or DSP implemented',
                               'gesture ownership, lock-capacity refusal and device readability untested'])
    (out/'proposal.json').write_text(json.dumps(report, indent=2)+'\n')
    (out/'README.md').write_text('# flow ribbon / design only\n\n'+__doc__+'\n\n'
        '- ribbon-overview-2x.png: graphite/pink dual-screen proposal.\n'
        '- ribbon-write-and-dice-2x.png: proposed group write and retrigger pips.\n'
        '- ribbon-detail-4x.png: exact enlarged density row.\n\n'
        'existing source/binary/assets hashes verified unchanged. no application build or deployment.\n')
    print(out.relative_to(ROOT))
    print(f'{len(outputs)} images; {len(before)} production/assets/artifacts unchanged')
    print(out/'ribbon-overview-2x.png')


if __name__ == '__main__':
    main()
