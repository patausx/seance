#!/usr/bin/env python3
"""Focused full DLY pair proof; no historical test main/preservation gate executes.

Default mode prepares only. Builds require an explicit stable pin file. Native
software raster/texture emulation, not 3DS/GPU/FPS/listening. No deployment or
project/sample/userdata IO. Failed attempts remain in their own dated folders.
"""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import sys
import traceback
sys.dont_write_bytecode = True
import test_module_headings_ui as host
from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'release/beta-20261004/delay-pair'
OWN = ROOT / 'tests/delay_pair_ui_cases.inc'
HEADER = ROOT / 'tests/module_headings_ui_cases.inc'
BASE = ROOT / 'release/module-headings/current-optimized-20261004T054122_916843Z/receipt.json'
INITIAL = OUT / 'initial-pins-20261004T060341_317925Z.json'
ALLOWED = {'platform/3ds/module_inspector.cpp', 'platform/3ds/ui_module_focus_delay.cpp',
           'platform/3ds/ui_module_focus_internal.h'}
NEW_ALLOWED = {'platform/3ds/delay_iris.h', 'platform/3ds/delay_readout.h'}


def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def stamp(): return datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
def save(p, value): Path(p).write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def preservation():
    initial = json.loads(INITIAL.read_text())['production']
    current = host.pins(ROOT)
    added = set(current) - set(initial)
    removed = set(initial) - set(current)
    assert not removed and added <= NEW_ALLOWED, ('production inventory changed', removed, added)
    changed = sorted(n for n in initial if current[n] != initial[n])
    assert set(changed) <= ALLOWED, ('non-delay production changed', changed)
    return {'initial': str(INITIAL), 'initial_sha256': sha(INITIAL),
            'allowed_changed': sorted(ALLOWED), 'allowed_new': sorted(NEW_ALLOWED),
            'changed': changed, 'new': {n: current[n] for n in sorted(added)},
            'core_byteexact': sum(n.startswith('core/') for n in initial),
            'unchanged_production': {n: h for n, h in current.items() if n in initial and n not in changed}}


def configure():
    OUT.mkdir(parents=True, exist_ok=True)
    text = HEADER.read_text()
    assert text.count('int main(') == 1
    combined = text.replace('int main(', 'int unused_delay_pair_header_fixture_main(', 1) + '\n' + OWN.read_text()
    path = OUT / ('cases-' + hashlib.sha256(combined.encode()).hexdigest()[:16] + '.inc')
    if path.exists(): assert path.read_text() == combined
    else: path.write_text(combined)
    host.OUT, host.CASES = OUT, path
    # Disable the older builder's baseline supplementation. No old files written.
    host.supplement = lambda: {}
    return {str(p.relative_to(ROOT)): sha(p) for p in
            (Path(__file__), OWN, HEADER, ROOT / 'tests/test_module_headings_ui.py', path)}


def latest(tag):
    ref = json.loads((OUT / (tag + '-latest.json')).read_text())
    assert sha(ref['receipt']) == ref['sha256']
    return ref, json.loads(Path(ref['receipt']).read_text())


def numeric_glyphs():
    """Independently decode the original packed Note digit masks, not UI draw_numeric."""
    text = (ROOT / 'platform/3ds/note_display_data.h').read_text()
    table = text.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    data = bytes(int(s, 16) for s in re.findall(r'0x([0-9a-fA-F]{2})', table))
    digits = {}
    for char, offset, w, h, top in re.findall(r"\{'([0-9])', false, (\d+), (\d+), (\d+), (-?\d+)\}", text):
        offset, w, h, top = int(offset), int(w), int(h), int(top)
        stride = (w + 7) // 8
        assert 0 <= offset and offset + stride * h <= len(data)
        mask = {(x, y) for y in range(h) for x in range(w)
                if data[offset + y * stride + x // 8] & (128 >> (x % 8))}
        digits[char] = (w, h, top, mask)
    assert set(digits) == set('0123456789')
    return digits


def check_main_digits(folder, rows):
    """Actual native-bitmap hero numbers, not only marker/label movement.

    Production uses inspector_gothic with Note digits at (14,85)/(218,85).
    Pixel-mask oracle independently checks every expected digit and every hole
    against the saved real composed native raster for all 96 routed frames.
    """
    face = numeric_glyphs(); checked = []
    for row in rows:
        for side, fields in (('bottom', [('time', 14, 85), ('feedback', 218, 85)]),
                             ('top', [('time', None, 140)])):
            path = folder / (row['id'] + '_' + side + '.ppm')
            with Image.open(path) as image:
                for field, origin, baseline in fields:
                    number = str(int(row[field + '_expected_number']))
                    if origin is None:
                        origin = 200 - sum(face[c][0] + 2 for c in number) // 2
                    pixels = set(); absent = set(); x = origin
                    for char in number:
                        w, h, top, mask = face[char]
                        for yy in range(h):
                            for xx in range(w):
                                where = (x + xx, baseline + top + yy)
                                (pixels if (xx, yy) in mask else absent).add(where)
                        x += w + 2
                    assert x <= (192 if field == 'time' and side == 'bottom' else 307 if side == 'bottom' else 240), (
                        'hero number too wide', row['id'], side, field, number)
                    colors = {image.getpixel(p) for p in pixels}
                    assert len(colors) == 1, ('incorrect moving hero digit ink', row['id'], side, field, number, colors)
                    ink = next(iter(colors))
                    assert ink != image.getpixel((origin, baseline - 37)), ('hero masked by background', row['id'], side, field)
                    assert all(image.getpixel(p) != ink for p in absent), ('incorrect moving hero digit/holes', row['id'], side, field, number)
                    checked.append({'surface': row['id'] + '_' + side, 'field': field,
                                    'expected_number': int(number), 'ink': ink, 'mask_pixels': len(pixels)})
    return {'checked_numbers': len(checked), 'glyph_asset_sha256': sha(ROOT / 'platform/3ds/note_display_data.h'),
            'samples': checked[::36]}


def check_secondary_text(folder, rows):
    # These controls use the *real* native ui::text path rather than Note masks.
    # Different x bands separate principal live digits from quiet base captions.
    counts = {field: 0 for field in ('mix', 'tone', 'wow', 'sat')}
    for row in rows:
        path = folder / (row['id'] + '_bottom_text.tsv')
        text = list(csv.DictReader(path.open(), delimiter='\t'))
        for field, x in (('mix', 14), ('tone', 90), ('wow', 166), ('sat', 242)):
            value = int(row[field + '_expected_number'])
            expected = str(value) + ('Hz' if field == 'tone' else '%')
            principal = [r for r in text if r['text'] == expected and
                         int(r['y']) == 158 and x <= int(r['x']) <= x + 2]
            assert len(principal) == 1, ('live number did not follow actual resolved parameter', row['id'], field, expected)
            base_raw = int(row[field + '_base'])
            base = int(200 * 60 ** (base_raw / 32767) + 0.5) if field == 'tone' else (base_raw * 100 + 16383) // 32767
            caption = 'b ' + str(base) + ('Hz' if field == 'tone' else '%')
            quiet = [r for r in text if r['text'] == caption and
                     int(r['y']) == 173 and x <= int(r['x']) <= x + 2]
            assert len(quiet) == 1, ('stored base changed or lost quiet numeric label', row['id'], field, caption)
            counts[field] += 1
        # The frozen source route must be identified for *all* six controls.
        assert sum(r['text'] == 'a' for r in text) >= 6, ('missing sigil link badge', row['id'])
    return {'principal_native_text_and_quiet_base_checks': counts, 'source_a_badges_at_least_six_per_frame': True}


def inspect(proof):
    folder = Path(proof['capture'])
    base = json.loads(BASE.read_text())
    old = {r['id'] + '_' + r['surface']: r for r in base['surfaces']}
    exact, delay, headers = [], [], []
    defaults = [r for r in proof['surfaces'] if r['variant'] == 'default' and r['surface'] == 'top']
    for theme in ('graphite', 'pink'):
        assert {int(r['kind']) for r in defaults if r['id'].startswith(theme + '_')} == set(range(1, 29))
    for row in proof['surfaces']:
        name = row['id'] + '_' + row['surface']
        assert int(row['outside_rects']) == int(row['outside_lines']) == 0, name
        assert int(row['objects']) < 8192, name
        if row['module'] != 'delay':
            assert row['variant'] == 'default' and name in old, name
            # Compare raw native files, not recompressed/annotated review media.
            wanted = Path(base['capture']) / (name + '.ppm')
            assert sha(wanted) == base['media'][wanted.name], ('historical raw capture drift', name)
            assert sha(folder / (name + '.ppm')) == sha(wanted), ('untouched neighbor differs', name)
            exact.append(name)
            continue
        assert int(row['text_overlaps']) == 0, ('delay native text collision', name)
        delay.append(name)
        if row['surface'] == 'top' and row['variant'] in {'default', 'bypass', 'long6', 'holes', 'live', 'lock'}:
            assert name in old, name
            with Image.open(folder / (name + '.ppm')) as got, Image.open(Path(base['capture']) / (name + '.ppm')) as before:
                assert not ImageChops.difference(got.crop((0, 0, 400, 41)), before.crop((0, 0, 400, 41))).getbbox(), ('identity/context header changed', name)
            headers.append(name)
    assert len(exact) == 108, len(exact)
    assert len(headers) == 12, len(headers)
    # Behavioral/read-state assertions live in the new C++ cases, not obsolete
    # draft inspector phrases. Text contract is extended when the design is stable.
    behavior = json.loads((folder / 'behavior.json').read_text())
    assert behavior['status'] == 'passed' and behavior['gesture_cases'] > 300
    assert behavior['detached_delay_pairs'] > 100 and behavior['old_suites_run'] == 0
    assert behavior['motion_frames'] == 96 and behavior['physical_oracles'] == 576
    assert behavior['route_cases'] == 26
    rows = list(csv.DictReader((folder / 'motion.tsv').open(), delimiter='\t'))
    assert len(rows) == 96 and {r['theme'] for r in rows} == {'graphite', 'pink'}
    motion = {}
    for theme in ('graphite', 'pink'):
        theirs = [r for r in rows if r['theme'] == theme]
        assert [int(r['frame']) for r in theirs] == list(range(48))
        assert len({r['id'] for r in theirs}) == 48
        assert min(int(r['sigil_value']) for r in theirs) < -15000
        assert max(int(r['sigil_value']) for r in theirs) > 15000
        assert {r['actual_read_ready'] for r in theirs} == {'1'}
        kinds = {}
        for field in ('time', 'feedback', 'mix', 'tone', 'wow', 'sat'):
            assert len({r[field + '_base'] for r in theirs}) == 1, ('base mutated by modulation', theme, field)
            assert len({r[field + '_target'] for r in theirs}) >= 20, ('target static', theme, field)
            assert len({r[field + '_expected_number'] for r in theirs}) >= 4, ('visible rounded number static', theme, field)
            kinds[field] = {'target_distinct': len({r[field + '_target'] for r in theirs}),
                            'physical_number_distinct': len({r[field + '_expected_number'] for r in theirs})}
        iris_regions = {}
        for direction, rect in (('left', (30, 65, 145, 170)), ('right', (255, 65, 372, 170))):
            masks = []
            for r in theirs:
                with Image.open(folder / (r['id'] + '_top.ppm')) as image:
                    masks.append(hashlib.sha256(image.crop(rect).tobytes()).hexdigest())
            assert len(set(masks)) >= 10, ('parametric iris did not react to actual sigil', theme, direction)
            iris_regions[direction] = len(set(masks))
        for side in ('top', 'bottom'):
            before = folder / (theme + '_delay_idle_first_' + side + '.ppm')
            after = folder / (theme + '_delay_idle_later_' + side + '.ppm')
            assert sha(before) == sha(after), ('unprompted time-based UI animation', theme, side)
        motion[theme] = {'rows': len(theirs), 'sigil_min': min(int(r['sigil_value']) for r in theirs),
                         'sigil_max': max(int(r['sigil_value']) for r in theirs), 'controls': kinds,
                         'iris_annulus_distinct': iris_regions, 'no_autonomous_movement': True}
    numeric = check_main_digits(folder, rows)
    assert numeric['checked_numbers'] == 288
    secondaries = check_secondary_text(folder, rows)
    return {'historical_receipt_sha256': sha(BASE), 'neighbor_raw_exact': exact,
            'delay_surfaces': delay, 'context_header_raw_exact': headers,
            'no_delay_native_text_overlaps': True, 'native_hero_digits': numeric,
            'native_secondary_numbers': secondaries, 'motion': motion, 'behavior': behavior}


def review(proof):
    folder = Path(proof['capture'])
    selected = ['default', 'long6', 'sync_straight', 'sync_dotted', 'sync_triplet',
                'sync_limited', 'free_min', 'free_max', 'dense', 'clean_handoff',
                'hold_arrival', 'clear_armed', 'clear_pending', 'clear_fade',
                'fresh', 'reset', 'mute', 'lock_full', 'lens', 'held_mod', 'legacy_fresh']
    files = []
    for theme in ('graphite', 'pink'):
        pairs = []
        for state in selected:
            path = folder / (theme + '_delay_' + state + '.png')
            if path.exists():
                with Image.open(path) as image: pairs.append((state, image.copy()))
        for start in range(0, len(pairs), 6):
            sheet = Image.new('RGB', (1200, 1040), (18, 18, 21)); draw = ImageDraw.Draw(sheet)
            for i, (name, image) in enumerate(pairs[start:start + 6]):
                x, y = i % 3 * 400, i // 3 * 520
                sheet.paste(image, (x, y + 20)); draw.text((x + 8, y + 3), theme + ' / ' + name, fill='white')
            target = folder / ('delay-review-' + theme + '-' + str(start // 6) + '.png')
            sheet.save(target); files.append(target)
    for theme in ('graphite', 'pink'):
        source = folder / (theme + '_delay_default.png')
        with Image.open(source) as image:
            target = folder / (theme + '_delay_default_2x.png')
            image.resize((800, 992), Image.Resampling.NEAREST).save(target); files.append(target)
        frames = [folder / (theme + '_delay_motion_' + str(i).zfill(2) + '.png') for i in range(48)]
        assert all(f.is_file() for f in frames), ('missing real modulation frames', theme)
        images = []
        for frame in frames:
            with Image.open(frame) as image: images.append(image.convert('P', palette=Image.Palette.ADAPTIVE, colors=128))
        target = folder / (theme + '-engine-sigil-48-pairs.gif')
        images[0].save(target, save_all=True, append_images=images[1:], duration=32, loop=0,
                       optimize=False, disposal=2)
        files.append(target)
    return {str(p): sha(p) for p in files}


def freeze_dependencies(proof):
    # Content-addressed storage deduplicates actual compiler dependencies across
    # optimized/sanitized attempts; no recursive full workspace snapshots.
    blobs = OUT / 'dependency-blobs'; blobs.mkdir(exist_ok=True)
    inventory = {}
    for name, digest in proof['dependencies'].items():
        p = Path(name); assert sha(p) == digest, ('dependency drift', name)
        target = blobs / digest
        if target.exists(): assert sha(target) == digest
        else:
            with target.open('xb') as f: f.write(p.read_bytes())
        inventory[name] = {'sha256': digest, 'blob': str(target)}
    return inventory


def verify(proof):
    assert proof['status'] == 'passed' and proof['run']['exit'] == 0
    assert proof['source_sha256'] == host.pins(ROOT)
    assert sha(proof['binary']) == proof['binary_sha256']
    assert proof['case_sha256'] == sha(host.CASES)
    for name, digest in proof['dependencies'].items(): assert sha(name) == digest, name
    for name, digest in proof['media'].items(): assert sha(Path(proof['capture']) / name) == digest, name


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mode', choices=('prepare', 'pin-stable', 'optimized', 'sanitized', 'compare'), default='prepare')
    p.add_argument('--stable-pins', type=Path)
    args = p.parse_args()
    inputs = configure(); guard = preservation()
    if args.mode in {'prepare', 'pin-stable'}:
        target = OUT / (args.mode + '-' + stamp() + '.json')
        save(target, {'mode': args.mode, 'inputs': inputs, 'preservation': guard,
                      'production': host.pins(ROOT), 'build_started': False})
        print(target); return
    assert args.stable_pins and args.stable_pins.is_file(), 'explicit stable production pin file required'
    stable = json.loads(args.stable_pins.read_text())
    assert stable['mode'] == 'pin-stable' and stable['production'] == host.pins(ROOT), 'production drift since stable authorization'
    target = OUT / ('attempt-' + args.mode + '-' + stamp() + '.json')
    result = {'status': 'started', 'mode': args.mode, 'inputs': inputs, 'preservation': guard,
              'stable_pins': str(args.stable_pins), 'stable_pins_sha256': sha(args.stable_pins),
              'host_only': True, 'hardware': False, 'deployment': False, 'compiler_workers': 2,
              'inherited_suites_run': 0, 'limitations': [
                  'native software raster/texture emulation, not PICA/device/fps/listening',
                  'synthetic memory-only fixtures, no physical mic or user sample/project IO',
                  'host glyph tracking allocates; not a general allocation-free draw proof',
                  'focused delay pair/controller regression, not whole-app or DSP sonic verification']}
    save(target, result)
    # Preserve changing implementation + the new harness even if compilation or
    # a runtime oracle fails. Other production remains protected by initial pins.
    result['immutable_attempt_inputs'] = freeze_dependencies({'dependencies': {
        str(ROOT / name): sha(ROOT / name) for name in set(inputs) | ALLOWED |
        {n for n in NEW_ALLOWED if (ROOT / n).is_file()}}})
    save(target, result)
    try:
        if args.mode != 'compare':
            receipt, built = host.build(ROOT, 'pair-' + args.mode, args.mode == 'sanitized', True)
            result['build'] = {'receipt': str(receipt), 'sha256': sha(receipt)}
            result['actual_dependency_snapshot'] = freeze_dependencies(built)
            verify(built)
            result['inspection'] = inspect(built)
            result['review'] = review(built)
            result['native_duals'] = built['native_duals']
        else:
            ar, a = latest('pair-optimized'); br, b = latest('pair-sanitized')
            verify(a); verify(b)
            assert a['source_sha256'] == b['source_sha256']
            aa = {(r['id'], r['surface']): r['raster_fnv64'] for r in a['surfaces']}
            bb = {(r['id'], r['surface']): r['raster_fnv64'] for r in b['surfaces']}
            assert aa == bb, 'optimized/sanitized raster mismatch'
            for name, side in aa:
                filename = name + '_' + side + '.ppm'
                assert sha(Path(a['capture']) / filename) == sha(Path(b['capture']) / filename), filename
            result['inspection'] = inspect(a); inspect(b)
            result['optimized_sanitized_raw_exact'] = len(aa)
            result['builds'] = [ar, br]
        for name, digest in inputs.items(): assert sha(ROOT / name) == digest, ('new harness drift', name)
        assert stable['production'] == host.pins(ROOT), 'production changed during proof'
        result['status'] = 'passed'
    except Exception as error:
        result.update(status='failed', failure=str(error), traceback=traceback.format_exc())
        save(target, result); raise
    save(target, result)
    save(OUT / (args.mode + '-inspection-latest.json'), {'receipt': str(target), 'sha256': sha(target)})
    print(target)


if __name__ == '__main__': main()
