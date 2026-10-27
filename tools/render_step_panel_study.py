#!/usr/bin/env python3
"""Visual-only step parameter strip / expanded locks. NOT runtime captures.

Default: previous quiet dense score with the selected-step panel at y190..239.
--wave: current proposal, one full-width illustrative line above closer rows;
all step ink is translated intact and the bottom panel is unchanged.
--levels: historical floating level/lock marks, now rejected.
All values and curves are authored examples, never runtime/audio telemetry.
"""
import json
import math
import sys
from PIL import Image, ImageDraw, ImageChops
from render_pattern_morph_study import (
    ROOT, identity, centered_score, centered_ink, Page, paper,
    INK, SPOT, HI, GHOST, BRANCH, rail, reserve,
)

OUT = ROOT / 'ui_mockups/step_panel_study'
BANDS = [(0, y-2, 320, y+31) for y in (21, 67, 113, 159)]


def score():
    original = centered_score(True)
    c = Page()
    c.im = original.im.copy()
    c.d = ImageDraw.Draw(c.im)
    blank = paper(320)
    for box, label in original.labels:
        if any(box[1] >= band[1] and box[3] <= band[3] for band in BANDS):
            c.labels.append((box, label))
        else:
            c.im.paste(blank.crop(box), box[:2])
    c.im.paste(blank.crop((0, 190, 320, 240)), (0, 190))
    for name, box in original.hits[:64]:
        c.hit(name, box)
    for band in BANDS:
        assert c.im.crop(band).tobytes() == original.im.crop(band).tobytes()
    # Verify the entire score, including cleared surrounding labels, against
    # the previously published quiet picture rather than only this renderer.
    previous = ROOT / 'ui_mockups/pattern_morph_quiet/02_dense_notes_bottom_2x.png'
    old = Image.open(previous).convert('RGB').resize((320, 240), Image.Resampling.NEAREST)
    assert c.im.crop((0, 0, 320, 190)).tobytes() == old.crop((0, 0, 320, 190)).tobytes()
    return c


def tab(c, x0, x1, label, value, active=False, inherited=False, enabled=True):
    color = SPOT if active else INK if enabled else GHOST
    centered_ink(c, (x0+2, 194, x1-2, 203), label, color if active else GHOST)
    centered_ink(c, (x0+2, 206, x1-2, 217), value, GHOST if inherited else color)
    if active:
        c.line((x0+5, 220, x1-6, 220), SPOT)
        c.line((x0+5, 221, x1-6, 221), SPOT)
    if enabled:
        c.hit(label, (x0, 192, x1, 222))


def adjustment(c, caption, pos):
    c.text(8, 230, caption, SPOT)
    rail(c, 92, 304, 233, pos, SPOT)
    c.hit('relative selected-value adjustment', (86, 224, 316, 240))


def overview():
    c = score()
    # The level is inherited from FM gain, not an uncounted fourth lock.
    # Time is expressed in 1/96 of a step. Slice is unavailable on this FM lane.
    entries = (
        ('trig', 'on'), ('note', 'c4'), ('level', '67%'), ('retrig', 'x1'),
        ('when', 'all'), ('time', '0/96'), ('lock', '3/4'), ('slice', '--'),
    )
    for i, (label, value) in enumerate(entries):
        tab(c, i*40, (i+1)*40, label, value,
            active=i == 1, inherited=i == 2, enabled=i != 7)
    adjustment(c, 'note / lock', .5)  # MIDI60 within the existing FM24..96 range.
    return c


def locks():
    c = score()
    tab(c, 0, 40, '<', 'step')
    # All three records belong to FM slot1. Pitch is the same note lock above;
    # INDEX keeps its actual display0..2, and RATIO uses the existing multiplier.
    entries = (('fm pitch', 'c4'), ('fm index', '0.85'),
               ('fm ratio', '2.00x'), ('+ lock', 'empty'))
    for i, (label, value) in enumerate(entries):
        tab(c, 40+i*70, 110+i*70, label, value, active=i == 1, inherited=i == 3)
    adjustment(c, 'index 0.85', .85/2)
    return c


def levels_main():
    """One authored additive preview; no step/panel pixels or controls move."""
    before = identity()
    c = overview()
    original = c.im.copy()
    old = Image.open(OUT / '01_step_parameters_bottom.png').convert('RGB')
    assert original.tobytes() == old.tobytes()
    bases = (67, 56, 72, 63)
    overrides = (
        {0: 94, 3: 38, 7: 85, 9: 25, 11: 100, 14: 48},
        {0: 90, 2: 35, 4: 75, 6: 28, 8: 95, 10: 42, 12: 80, 14: 20},
        {1: 40, 3: 95, 6: 55, 9: 100, 11: 35, 14: 85},
        {0: 88, 3: 40, 6: 95, 8: 30, 11: 80, 14: 45},
    )
    # Additional locks EXCLUDE the note/pitch and level records already shown.
    # Selected FM step5 has pitch+index+ratio, inherited gain67%: still3/4.
    extra = ({0: 1, 4: 2, 8: 1, 11: 1}, {3: 1, 7: 1, 11: 1, 15: 1},
             {2: 1, 6: 1, 10: 1}, {0: 1, 5: 1, 9: 1, 13: 1})
    records, zones = [], []
    for ch, y in enumerate((21, 67, 113, 159)):
        zones.append((0, y-12, 320, y-4))
        for i in range(16):
            value = overrides[ch].get(i, bases[ch])
            locked = i in overrides[ch]
            height = (value*8+50)//100
            x = i*20
            if height:
                c.d.rectangle((x+7, y-4-height, x+10, y-5),
                              fill=SPOT if locked else BRANCH)
            if extra[ch].get(i, 0):
                c.d.rectangle((x+14, y-7, x+15, y-6), fill=SPOT)
            # Dense pitched rows use a pitch record; the noise row has none.
            count = int(ch != 1) + int(locked) + extra[ch].get(i, 0)
            assert count <= 4
            records.append({'chain': ch+1, 'step': i+1, 'level_percent': value,
                            'level_locked': locked, 'extra_locks': extra[ch].get(i, 0),
                            'total_locks': count, 'bar_height': height})
    selected = records[4]
    assert selected['level_percent'] == 67 and not selected['level_locked']
    assert selected['total_locks'] == 3 and selected['extra_locks'] == 2
    # Every original pixel outside the four empty 8px strips is protected.
    restored = c.im.copy()
    for zone in zones:
        restored.paste(original.crop(zone), zone[:2])
    assert restored.tobytes() == original.tobytes()
    for band in BANDS + [(0, 190, 320, 240)]:
        assert c.im.crop(band).tobytes() == original.crop(band).tobytes()
    assert c.hits == overview().hits  # Passive marks: no tiny extra touch targets.
    out = ROOT / 'ui_mockups/step_level_study'
    out.mkdir(parents=True, exist_ok=True)
    c.im.save(out / '01_step_levels_bottom.png')
    c.im.resize((640, 480), Image.Resampling.NEAREST).save(out / '01_step_levels_bottom_2x.png')
    assert before == identity()
    (out / 'verification.json').write_text(json.dumps({
        'origin': 'one authored Pillow lower-screen proposal, NOT runtime/hardware data',
        'scope': 'four passive8px strips in existing gaps; all other pixels and touch targets exact',
        'legend': 'bar0..8px = stored/inherited level0..100%; branch = inherited, violet = written level lock; violet2x2 dot = additional locks excluding note and level',
        'fixture': 'authored dense score; selected FM step5 unchanged: c4, inherited67%, pitch+index+ratio3/4',
        'limits': 'quantized overview, not exact numeric values, amplitude meter, waveform, envelope or new modulation; no runtime/all-mode/hardware cost proof',
        'changed_zones': zones, 'unchanged_targets': c.hits,
        'preserved_source_sha256': before, 'stored_examples': records,
    }, indent=2)+'\n')
    print('pass: one authored lower; four8px data strips only; note bands, bottom panel and all targets exact')
    print('64 bounded level examples, shared lock capacity and selected-step consistency; runtime/config hashes unchanged')
    print(out)


def wave_main():
    """One visual-only line; denser score, no level bars or live signal claim."""
    before = identity()
    original = overview()
    c = Page()
    blank = paper(320)
    rows = (45, 83, 121, 159)  # Pitch38 instead of46; bottom row/panel stay put.
    translations = []
    for ch, (band, y) in enumerate(zip(BANDS, rows)):
        dy = y - (band[1]+2)
        target = (0, band[1]+dy, 320, band[3]+dy)
        source = original.im.crop(band)
        # Move only ink, not strips of the old gradient (which would look ruled).
        mask = ImageChops.difference(source, blank.crop(band)).convert('L')
        mask = mask.point(lambda p: 255 if p else 0)
        c.im.paste(source, target[:2], mask)
        expected = Image.new('RGB', source.size)
        actual = expected.copy()
        expected.paste(source, (0, 0), mask)
        actual.paste(c.im.crop(target), (0, 0), mask)
        assert expected.tobytes() == actual.tobytes()
        for box, label in original.labels:
            if band[1] <= box[1] and box[3] <= band[3]:
                reserve(c, (box[0], box[1]+dy, box[2], box[3]+dy), label)
        for name, box in original.hits[ch*16:(ch+1)*16]:
            shifted = (box[0], box[1]+dy, box[2], box[3]+dy)
            assert shifted[2]-shifted[0] == 20 and shifted[3]-shifted[1] == 31
            c.hit(name, shifted)
        translations.append({'chain': ch+1, 'dy': dy, 'old_band': band, 'new_band': target})
    panel = (0, 190, 320, 240)
    c.im.paste(original.im.crop(panel), panel[:2])
    for box, label in original.labels:
        if box[1] >= 190:
            reserve(c, box, label)
    for name, box in original.hits[64:]:
        c.hit(name, box)
    assert len(c.labels) == len(original.labels)
    assert len(c.hits) == len(original.hits)
    assert c.hits[64:] == original.hits[64:]
    assert c.im.crop(panel).tobytes() == original.im.crop(panel).tobytes()
    # Deliberately authored visual line, not PCM, a meter, lock data or timebase.
    # Black/carbon shared ink; occasional violet stretches, no frame/axis/fill.
    points = []
    violet = ((45, 66), (149, 168), (256, 277))
    for x in range(320):
        phase = .23*x + .65*math.sin(.037*x)
        amplitude = 3 + 11*(.5+.5*math.sin(.055*x-.6))**1.5
        y = round(22 + amplitude*(math.sin(phase)+.17*math.sin(2*phase))/1.17)
        assert 6 <= y <= 38 < rows[0]-2
        points.append((x, y))
    for a, b in zip(points, points[1:]):
        color = SPOT if any(lo <= b[0] < hi for lo, hi in violet) else INK
        c.line((*a, *b), color)
    assert points[0][0] == 0 and points[-1][0] == 319
    assert c.im.crop(panel).tobytes() == original.im.crop(panel).tobytes()
    assert before == identity()
    out = ROOT / 'ui_mockups/step_wave_study'
    out.mkdir(parents=True, exist_ok=True)
    c.im.save(out / '01_wave_score_bottom.png')
    c.im.resize((640, 480), Image.Resampling.NEAREST).save(out / '01_wave_score_bottom_2x.png')
    (out / 'verification.json').write_text(json.dumps({
        'origin': 'one authored Pillow lower-screen proposal, NOT runtime/hardware or PCM',
        'scope': 'remove historical floating bars/dots; full-width1px carbon/violet wave above denser rows; bottom panel unchanged',
        'wave': {'bounds': [0, min(y for x, y in points), 320, max(y for x, y in points)+1],
                 'violet_x_ranges': violet, 'semantics': 'visual accents only; not clip, lock, amplitude or measured audio'},
        'row_origins': rows, 'row_pitch': 38, 'previous_pitch': 46,
        'translations': translations, 'step_ink': 'all64 notes/octaves/caps/selection masks and colors exact after translation',
        'controls': 'same64 proposed20x31 step targets translated with rows; panel targets exact; wave has no target',
        'labels': c.labels, 'proposed_targets': c.hits,
        'preserved_source_sha256': before,
        'limits': 'static visual only; animation/source mapping, runtime ownership, all modes and hardware cost not implemented or tested',
    }, indent=2)+'\n')
    print('pass: one authored lower; full-width visual wave; rows38px apart instead of46')
    print('all64 step ink masks/colors preserved; labels/targets in bounds and nonoverlapping; panel exact; runtime/config hashes unchanged')
    print(out)


def main():
    if sys.argv[1:] == ['--wave']:
        wave_main()
        return
    if sys.argv[1:] == ['--levels']:
        levels_main()
        return
    before = identity()
    OUT.mkdir(parents=True, exist_ok=True)
    frames = []
    pages = [('01_step_parameters', overview()), ('02_expanded_locks', locks())]
    for name, c in pages:
        c.im.save(OUT / f'{name}_bottom.png')
        c.im.resize((640, 480), Image.Resampling.NEAREST).save(OUT / f'{name}_bottom_2x.png')
        frames.append({'name': name, 'labels': c.labels, 'proposed_targets': c.hits})
    assert pages[0][1].im.crop((0, 0, 320, 190)).tobytes() == pages[1][1].im.crop((0, 0, 320, 190)).tobytes()
    assert pages[0][1].hits[:64] == pages[1][1].hits[:64]
    assert before == identity()
    (OUT / 'verification.json').write_text(json.dumps({
        'origin': 'two authored Pillow lower screens, NOT runtime or hardware captures',
        'scope': 'only bottom y190..239; score top190 rows match prior quiet dense picture exactly',
        'fixture': 'chain1 FM slot1 step5 c4/on; inherited gain67%; retrig1; always; time0/96; three locks: pitch c4, index0.85, ratio2.00x; one empty slot; no slice target',
        'proposed_ui': 'seven available parameter targets; slice unavailable; lock view replaces strip with back + four shared lock slots; relative rail below',
        'checks': 'native text bounds/nonoverlap, declared target bounds/nonoverlap, prior score pixels and64 step targets preserved, protected runtime/config hashes unchanged',
        'limits': 'static example only; no controller, all-mode score policy, lock add/remove picker, input ownership, hardware readability or GPU cost proof',
        'native_size': [320, 240], 'panel_bounds': [0, 190, 320, 240],
        'preserved_source_sha256': before, 'frames': frames,
    }, indent=2)+'\n')
    print('pass: two authored lower screens; prior quiet score190 rows and64 step targets exact')
    print('native labels/targets in bounds and nonoverlapping; runtime/config identities unchanged')
    print(OUT)


if __name__ == '__main__':
    main()
