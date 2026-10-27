#!/usr/bin/env python3
"""Preview only: global clock ownership and proposed Sigil modulation.

Not a C++/LCD capture. Neither clock modulation nor these controls exist yet.
Paper/headings/6x8 text/Seance Note digits reuse existing font assets; all
readouts are explicit synthetic arithmetic fixtures, never engine telemetry.
OFL font derivative: Deborah Khodanovich 2025; assets/fonts/README.md.
"""
from dataclasses import dataclass, replace
from pathlib import Path
import hashlib
import json
import re
from PIL import Image, ImageDraw
from render_menu_style_study import Page, paper, GHOST, BRANCH, overlaps
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/project_clock_study'
HI = (242, 216, 226)


def digits():
    # Read the actual existing masks, without importing unrelated screenshot studies.
    source = (ROOT / 'platform/3ds/note_display_data.h').read_text()
    raw = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})',
                source.split('bitmap[] = {')[1].split('};')[0]))
    result = {}
    for ch, small, offset, w, h, top in re.findall(
            r"\{'(.)', (true|false), (\d+), (\d+), (\d+), (-?\d+)\}", source):
        if small == 'true' or not ch.isdigit():
            continue
        offset, w, h, top = map(int, (offset, w, h, top))
        mask = Image.new('L', (w, h))
        for y in range(h):
            for x in range(w):
                mask.putpixel((x, y), 255 if raw[offset+y*((w+7)//8)+x//8] & (128>>(x&7)) else 0)
        result[ch] = mask, top
    assert len(result) == 10
    return result


DIGITS = digits()


def hero(c, x, baseline, value, suffix, color=INK):
    start = x
    for ch in str(value):
        mask, top = DIGITS[ch]
        c.im.paste(color, (x, baseline+top), mask)
        x += mask.width+2
    box = (start, baseline-44, x, baseline+4)
    assert 0 <= box[0] < box[2] <= c.im.width and 0 <= box[1] < box[3] <= 240
    assert all(not overlaps(box, old) for old, _ in c.labels)
    c.labels.append((box, 'numeric hero '+str(value)))
    c.text(x+7, baseline-6, suffix, GHOST)


@dataclass(frozen=True)
class Clock:
    name: str
    base: int
    depth: int
    source: str
    output_percent: int
    minimum: int
    maximum: int
    unit: str
    depth_unit: str

    @property
    def contribution(self):
        return self.depth*self.output_percent/100 if self.source != 'none' else 0

    @property
    def live(self):
        return max(self.minimum, min(self.maximum, self.base+self.contribution))

    @property
    def applied_delta(self):
        return self.live-self.base

    @property
    def span(self):
        d = abs(self.depth) if self.source != 'none' else 0
        return max(self.minimum, self.base-d), min(self.maximum, self.base+d)


TEMPO = Clock('tempo', 120, 12, 'a', 50, 20, 300, 'bpm', 'bpm')
SWING = Clock('swing', 58, 6, 'b', -50, 50, 75, '%', 'pp')
ITEMS = [('patch table', 'build'), ('sigils', 'draw'), ('sequencer', 'score'),
         ('perform', 'morph'), ('project', 'clock / files'), ('stress', 'test'), ('exit', 'hbmenu')]


def signed(value):
    return f'{value:+g}'


def source_badge(clock):
    return 'none' if clock.source == 'none' else f'{clock.source} {clock.depth:+d}{clock.depth_unit}'


def nav(tempo=TEMPO, swing=SWING):
    c = Page()
    c.heading(38, 20, 'go to')
    # Use unused header width. ALL existing navigation row/hit positions stay fixed.
    for clock, x, right, box in ((tempo, 164, 234, (154, 6, 240, 54)),
                                (swing, 254, 308, (244, 6, 316, 54))):
        c.text(x, 14, clock.name, GHOST)
        if clock.source != 'none':
            c.text(right, 14, clock.source, SPOT, right=True)
        value = f'{clock.base}{clock.unit}' if clock.unit == '%' else f'{clock.base} {clock.unit}'
        c.text(x, 34, value, INK)
        c.line((x, 47, right-1, 47), BRANCH)
        c.hit('relative drag '+clock.name+' base in place', box)
    c.line((52, 62, 52, 200), BRANCH)
    for i, (name, hint) in enumerate(ITEMS):
        y = 64+i*22
        color = SPOT if i == 0 else INK
        c.line((52, y+4, 67, y+4), SPOT if i == 0 else BRANCH)
        if i == 0:
            c.marker(68, y+4)
        c.text(77, y, name, color)
        c.text(292, y, hint, GHOST, right=True)
        c.hit('navigate '+name, (38, 58+i*22, 300, 80+i*22))
    c.footer('a open', 'b / select close')
    return c


def project(tempo=TEMPO, swing=SWING):
    c = Page()
    c.heading(24, 14, 'project')
    c.text(300, 24, 'patch01 / saved', GHOST, right=True)
    c.text(146, 47, 'base', GHOST)
    c.text(242, 47, 'sigil', GHOST)
    for i, clock in enumerate((tempo, swing)):
        y = 68+i*28
        c.text(24, y, clock.name, INK)
        c.text(146, y, f'{clock.base}{clock.unit}' if clock.unit == '%' else f'{clock.base} {clock.unit}', INK)
        c.text(300, y, source_badge(clock), SPOT if clock.source != 'none' else GHOST, right=True)
        c.hit('open '+clock.name+' editor', (16, y-7, 304, y+17))
    c.line((24, 119, 299, 119), BRANCH)
    for i, (name, value) in enumerate((('beats per bar', '4'), ('trigger launch', 'immediate'), ('pattern launch', 'bar'))):
        y = 128+i*24
        c.text(24, y, name, GHOST)
        c.text(300, y, value, INK, right=True)
        c.hit('edit '+name, (16, y-5, 304, y+17))
    c.line((24, 195, 299, 195), BRANCH)
    c.text(24, 207, 'files', INK)
    c.text(300, 207, 'save / load >', GHOST, right=True)
    c.hit('open existing project file operations', (16, 198, 304, 221))
    c.footer('a edit', 'b return')
    return c


def clock_editor(clock):
    c = Page()
    c.heading(24, 14, clock.name)
    c.text(300, 24, 'project', GHOST, right=True)
    c.text(24, 57, 'base', GHOST)
    hero(c, 24, 115, clock.base, clock.unit)
    c.line((24, 123, 299, 123), BRANCH)
    c.hit('relative drag base', (16, 48, 304, 127))
    c.text(24, 143, 'sigil', GHOST)
    for i, src in enumerate(('none', 'a', 'b', 'c', 'd')):
        center = (111, 158, 204, 250, 293)[i]
        x = center-len(src)*3
        c.text(x, 143, src, SPOT if src == clock.source else GHOST)
        if src == clock.source:
            c.line((x-2, 155, x+len(src)*6, 155), SPOT)
        left, right = ((86, 135), (137, 180), (182, 225), (227, 270), (272, 314))[i]
        c.hit('source '+src, (left, 132, right, 162))
    c.text(24, 178, 'depth', GHOST)
    c.text(300, 178, f'{clock.depth:+d} {clock.depth_unit}', SPOT, right=True)
    c.line((219, 190, 299, 190), BRANCH)
    c.hit('relative drag physical-unit depth', (16, 165, 304, 197))
    lo, hi = clock.span
    c.text(24, 207, f'range {lo} .. {hi} {clock.unit}', GHOST)
    c.footer('drag base / depth', 'b project')
    return c


def overview(tempo=TEMPO, swing=SWING):
    c = Page(400)
    c.text(24, 16, 'patch01', GHOST)
    c.text(376, 16, 'project clock', GHOST, right=True)
    c.heading(24, 37, 'project')
    for x, clock in ((24, tempo), (222, swing)):
        c.text(x, 82, clock.name+' / live', SPOT)
        hero(c, x, 145, int(clock.live), clock.unit)
        c.text(x, 164, f'base {clock.base} {clock.unit}', GHOST)
        c.text(x, 185, f'sigil {clock.source} / {clock.depth:+d} {clock.depth_unit}', GHOST)
        c.text(x, 206, 'change '+signed(clock.applied_delta)+' '+clock.depth_unit, SPOT)
    c.footer('edit base below', 'live follows sigils')
    return c


def inspector(clock):
    c = Page(400)
    c.text(24, 16, 'patch01 / project', GHOST)
    c.text(376, 16, 'clock / '+clock.name, GHOST, right=True)
    c.heading(24, 38, clock.name)
    c.text(24, 80, 'live', SPOT)
    hero(c, 24, 144, int(clock.live), clock.unit)
    c.text(24, 164, f'base {clock.base} {clock.unit}', GHOST)
    c.text(24, 184, 'change '+signed(clock.applied_delta)+' '+clock.depth_unit, SPOT)
    c.text(220, 80, 'modulation', GHOST)
    c.text(220, 105, 'source   '+('none' if clock.source == 'none' else 'sigil '+clock.source), INK)
    c.text(220, 126, 'output   '+('--' if clock.source == 'none' else f'{clock.output_percent:+d}%'), GHOST)
    c.text(220, 147, f'depth    {clock.depth:+d} {clock.depth_unit}', GHOST if clock.source == 'none' else INK)
    lo, hi = clock.span
    c.text(220, 184, f'range {lo} .. {hi}', GHOST)
    c.text(24, 207, 'depth in percentage points' if clock.name == 'swing' else 'base stays fixed / clock moves', GHOST)
    clipped = clock.live != clock.base+clock.contribution
    c.footer('edit below', 'live / limited' if clipped else 'live = base + mod')
    return c


def protected():
    paths = [p for folder in ('core', 'platform', 'assets', 'release',
                              'ui_mockups/chain_workflow_study')
             for p in (ROOT/folder).rglob('*') if p.is_file()]
    paths += [p for p in ROOT.glob('seance.*') if p.is_file()]
    paths += [ROOT/'Makefile']
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}


def validate(c):
    bg = paper(c.im.width)
    assert c.im.size in ((320, 240), (400, 240))
    assert all(pixel == bgpixel or pixel in {INK, SPOT, GHOST, BRANCH}
               for pixel, bgpixel in zip(c.im.get_flattened_data(), bg.get_flattened_data()))
    for _, box in c.hits:
        assert box[2]-box[0] >= 22 and box[3]-box[1] >= 22


def save(im, stem):
    im.save(OUT/(stem+'_native.png'))
    big = im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST)
    assert big.resize(im.size, Image.Resampling.NEAREST).tobytes() == im.tobytes()
    big.save(OUT/(stem+'_2x.png'))


def main():
    before = protected()
    OUT.mkdir(parents=True, exist_ok=True)
    frames = [('01_go_to', '01 / go to - drag numbers here', overview(), nav()),
              ('02_project', '02 / project - clock and files', overview(), project()),
              ('03_tempo', '03 / tempo - sigil depth in bpm', inspector(TEMPO), clock_editor(TEMPO)),
              ('04_swing', '04 / swing - depth in points', inspector(SWING), clock_editor(SWING))]
    report_frames = {}
    sheet = Canvas(664, 560, OUTSIDE)
    for i, (key, caption, top, bottom) in enumerate(frames):
        validate(top); validate(bottom)
        save(top.im, key+'_top'); save(bottom.im, key+'_bottom')
        dual = Image.new('RGB', (400, 496), OUTSIDE)
        dual.paste(top.im, (0, 0)); dual.paste(bottom.im, (40, 256))
        save(dual, key+'_dual')
        x, y = 8+(i%2)*328, 8+(i//2)*280
        sheet.text(x+4, y, caption, HI)
        sheet.im.paste(bottom.im, (x, y+24))
        report_frames[key] = {'bottom_labels': bottom.labels, 'proposed_hits': bottom.hits,
                              'top_labels': top.labels}
    save(sheet.im, 'bottom_comparison')
    # Boundary/source layout probes. Live rendering is illustrative integer readout;
    # precision, smoothing, tick policy and persistent schema are NOT specified here.
    count = 0
    for seed in (TEMPO, SWING):
        for base in (seed.minimum, seed.base, seed.maximum):
            for source in ('none', 'a', 'b', 'c', 'd'):
                for depth in (-seed.depth, 0, seed.depth):
                    for output in (-100, 0, 100):
                        clock = replace(seed, base=base, source=source, depth=depth, output_percent=output)
                        validate(clock_editor(clock)); validate(inspector(clock))
                        assert seed.minimum <= clock.live <= seed.maximum
                        assert clock.base+clock.applied_delta == clock.live
                        if source == 'none':
                            assert clock.live == clock.base and clock.applied_delta == 0
                        count += 1
    # Original seven contiguous 22px nav bands are preserved exactly; header fields
    # have 48px touch height, with no intersections with list or each other.
    navigation = frames[0][3]
    assert [box for name, box in navigation.hits if name.startswith('navigate ')] == [
        (38, 58+i*22, 300, 80+i*22) for i in range(7)]
    for base in (20, 300):
        for swing in (50, 75):
            validate(nav(replace(TEMPO, base=base), replace(SWING, base=swing)))
            validate(project(replace(TEMPO, base=base), replace(SWING, base=swing)))
    assert before == protected(), 'protected runtime/assets/earlier proposals changed'
    report = {'status': 'offline visual proposal, not implemented or uploaded',
              'design': ['remove global manual BPM ownership from Sigils in a future implementation',
                         'GO TO header directly drags BASE tempo/swing; list never shifts',
                         'project replaces patches, same navigation count; existing file browser linked',
                         'modulation detail is per-clock parameter, not another top-level menu',
                         'lower values are stable base; upper reports illustrative base/change/live',
                         'tempo modulation depth uses bpm; swing uses percentage points (pp)'],
              'checks': {'clock_source_depth_output_layout_probes': count,
                         'text_and_proposed_hit_bounds_separation': True,
                         'hit_minimum_height_px': 22, 'original_nav_bands_exact': True,
                         'per_row_palette_and_integer_scale': True,
                         'protected_files_unchanged': len(before)},
              'limits': ['Sigil global clock routing does not exist; fixture is future functionality',
                         'no scheduler/clock feedback policy or depth limits approved here',
                         'set_bpm/set_swing currently clear sequence runtime: not usable as modulation callbacks',
                         'integer readouts are design fixtures, not a decision on effective clock precision',
                         'a future live readout MUST show applied clock, not an uncommitted target',
                         'no runtime input, GPU, hardware or audio validation',
                         'touch regions are proposals; keyboard focus semantics need controller design',
                         'macro/mixer/sample editors remain in their existing domains, not duplicated'],
              'frames': report_frames, 'protected_inputs': before}
    (OUT/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    (OUT/'README.txt').write_text(__doc__+'\nrun: python3 tools/render_project_clock_study.py\n'
        'bottom_comparison_2x.png: four native bottom states, nearest 2x.\n'
        '03_tempo_dual_2x.png: full two-screen base/live/source illustration.\n'
        '120 base + 12 bpm * 50% = 126 live; 58% base + 6pp * -50% = 55% live.\n'
        'pp means percentage points, never percent multiplication of swing base.\n')
    print(OUT/'bottom_comparison_2x.png')
    print(f'pass: 4 dual concepts; {count} layout probes; {len(before)} protected files unchanged')


if __name__ == '__main__':
    main()
