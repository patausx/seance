#!/usr/bin/env python3
"""Focused current swarm UI proof. Actual native C++ raster, not the Pillow study.

Only the new swarm main executes. Imports reuse build/raster fixture utilities,
never their historical suites, preservation rules, captures, or old mains.
Default is one optimized capture; ASAN/UBSAN/leaks and exact comparison are explicit.
No ARM/device/network/user sample or project IO and no audio/performance claim.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

sys.dont_write_bytecode = True
import test_module_headings_ui as host
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'release/swarm-minimal/ui'
OWN = ROOT / 'tests/swarm_minimal_ui_cases.inc'
HEADER = ROOT / 'tests/module_headings_ui_cases.inc'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def configure():
    OUT.mkdir(parents=True, exist_ok=True)
    header = HEADER.read_text()
    assert header.count('int main(') == 1
    text = header.replace('int main(', 'int unused_swarm_header_main(', 1) + '\n' + OWN.read_text()
    path = OUT / ('cases-' + hashlib.sha256(text.encode()).hexdigest()[:16] + '.inc')
    if path.exists():
        assert path.read_text() == text
    else:
        path.write_text(text)
    host.OUT, host.CASES = OUT, path
    # Do not invoke the historical baseline supplement or modify its receipts.
    host.supplement = lambda: {str(p.relative_to(ROOT)): sha(p) for p in (OWN, HEADER, Path(__file__))}


def draw_guards():
    """Follow all local helpers reachable from both frozen render entry points."""
    path = ROOT / 'platform/3ds/ui_module_focus_stranular.cpp'
    source = path.read_text()
    definitions = host.common.functions(source)
    pending = ['stranular_focus_draw_top', 'stranular_focus_draw_bottom']
    checked = {}
    forbidden = r'\b(?:g_eng|g_audio|g_ui_frame|g_ui_snapshot|osGetTime|svcGetSystemTick|malloc|calloc|realloc|new|delete|C3D_TexInit|C2D_SpriteSheetLoad)\b'
    while pending:
        name = pending.pop()
        if name in checked:
            continue
        assert name in definitions, ('missing draw function', name)
        body = host.common.masked(definitions[name][2])
        assert not re.search(forbidden, body), ('engine/clock/heap access in draw graph', name)
        checked[name] = hashlib.sha256(body.encode()).hexdigest()
        calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', body))
        pending += sorted(calls.intersection(definitions).difference(checked))
    assert 'module_heading::header(UnitKind::Stranular' in source
    assert 'module_fader::horizontal(' in source and 'module_fader::vertical(' in source
    return checked


def title_mask():
    source = (ROOT / 'platform/3ds/module_heading_data.h').read_text()
    raw = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', source.split('bitmap[] = {', 1)[1].split('};', 1)[0]))
    match = re.search(r'\{UnitKind::Stranular, "swarm", (\d+), (\d+), (\d+)\}', source)
    assert match, 'current private swarm mask missing'
    offset, width, height = map(int, match.groups())
    assert 0 < width <= 57 and 0 < height <= 25
    bits = [bool(raw[offset + y * ((width + 7) // 8) + x // 8] & (128 >> (x % 8)))
            for y in range(height) for x in range(width)]
    return width, height, bits


def inspect(proof):
    folder = Path(proof['capture'])
    width, height, bits = title_mask()
    masks = 0
    ids = set()
    for row in proof['surfaces']:
        assert int(row['outside_rects']) == int(row['outside_lines']) == int(row['text_overlaps']) == 0, row
        key = (row['id'], row['surface'])
        assert key not in ids, ('duplicate surface', key)
        ids.add(key)
        with Image.open(folder / (row['id'] + '_' + row['surface'] + '.ppm')) as image:
            assert image.size == (400 if row['surface'] == 'top' else 320, 240)
            # Uniform foreground AND exact holes at the mandatory current (14,8).
            # A shared MOD lens may dim the title but cannot move or overwrite it.
            crop = list(image.crop((14, 8, 14 + width, 8 + height)).getdata())
            ink = {value for value, bit in zip(crop, bits) if bit}
            assert len(ink) == 1, ('title ink overwritten', row)
            ink = next(iter(ink))
            assert [value == ink for value in crop] == bits, ('title shape/holes differ', row)
            masks += 1
    assert ids and masks == proof['native_duals'] * 2
    behavior = json.loads((folder / 'behavior.json').read_text())
    assert behavior['status'] == 'passed' and behavior['old_suites_run'] == 0
    assert behavior['duals'] == proof['native_duals'] == behavior['frozen_pairs']
    assert behavior['edit_banks_tested'] == 8 and behavior['lens_params_tested'] == 12
    assert behavior['peak_objects'] <= 8192
    for theme in ('graphite_', 'pink_'):
        for side in ('top', 'bottom'):
            a = folder / (theme + 'swarm_idle_first_' + side + '.ppm')
            b = folder / (theme + 'swarm_idle_later_' + side + '.ppm')
            assert sha(a) == sha(b), ('wall clock fabricated motion', theme, side)
        for name in ('default', 'setup', 'frozen', 'waiting', 'lock', 'lens_11'):
            with Image.open(folder / (theme + 'swarm_' + name + '.png')) as image:
                image.resize((800, 992), Image.Resampling.NEAREST).save(folder / (theme + 'swarm_' + name + '_2x.png'))
    motion = list(csv.DictReader((folder / 'motion.tsv').open(), delimiter='\t'))
    assert len(motion) == behavior['motion_frames'] > 0
    for theme in ('graphite_', 'pink_'):
        rows = [row for row in motion if row['theme'] == theme]
        assert rows and len({row['wave_fnv64'] for row in rows}) > 1
        images = []
        for row in rows:
            with Image.open(folder / (row['id'] + '.png')) as image:
                images.append(image.convert('RGB').resize((800, 992), Image.Resampling.NEAREST))
        palette = images[0].quantize(colors=128)
        frames = [image.quantize(palette=palette, dither=Image.Dither.NONE) for image in images]
        frames[0].save(folder / (theme + 'native-input.gif'), save_all=True, append_images=frames[1:],
                       duration=80, loop=0, optimize=False, disposal=2)
    return {'fixed_title_masks_14_8': masks, 'behavior': behavior,
            'motion_frames': len(motion), 'draw_graph': draw_guards(),
            'presentation': {p.name: sha(p) for p in sorted(folder.iterdir())
                             if p.suffix == '.gif' or p.name.endswith('_2x.png')}}


def freeze_inputs(receipt, proof):
    """Retain precisely the dependency bytes used by this successful new build."""
    folder = Path(receipt).parent
    retained = {}
    for path, digest in proof['dependencies'].items():
        source = Path(path)
        assert sha(source) == digest, ('dependency drift', path)
        try:
            name = source.relative_to(ROOT)
        except ValueError:
            name = Path('external') / source.name
        target = folder / 'inputs' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            assert sha(target) == digest
        else:
            shutil.copyfile(source, target)
        retained[str(name)] = digest
    for source in (Path(__file__), OWN, HEADER, ROOT / 'tests/test_module_headings_ui.py',
                   ROOT / 'tests/test_fader_lancet_ui.py', ROOT / 'tests/test_editor_fullscreen_ui.py',
                   ROOT / 'tests/test_mod_minimal_ui.py'):
        name = source.relative_to(ROOT)
        target = folder / 'inputs' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            assert sha(target) == sha(source)
        else:
            shutil.copyfile(source, target)
        retained[str(name)] = sha(source)
    manifest = folder / 'retained-inputs.json'
    save(manifest, retained)
    return {'manifest': str(manifest), 'sha256': sha(manifest), 'count': len(retained)}


def latest(tag):
    reference = json.loads((OUT / (tag + '-latest.json')).read_text())
    assert sha(reference['receipt']) == reference['sha256']
    return json.loads(Path(reference['receipt']).read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('optimized', 'sanitized', 'compare'), default='optimized')
    parser.add_argument('--guards-only', action='store_true', help='cheap source-only draw graph; no compile/capture')
    args = parser.parse_args()
    if args.guards_only:
        print(json.dumps(draw_guards(), indent=2, sort_keys=True))
        return
    configure()
    if args.mode == 'compare':
        a, b = latest('optimized'), latest('sanitized')
        assert a['status'] == b['status'] == 'passed'
        for tag, proof in (('optimized', a), ('sanitized', b)):
            inspection = latest('inspection-' + tag)
            assert inspection['status'] == 'passed', ('native inspection did not pass', tag)
            reference = json.loads((OUT / (tag + '-latest.json')).read_text())
            assert inspection['receipt_sha256'] == reference['sha256']
            assert inspection['runner_sha256'] == sha(__file__)
        assert a['source_sha256'] == b['source_sha256'] == host.pins(ROOT)
        assert a['case_sha256'] == b['case_sha256'] == sha(host.CASES)
        aa = {(r['id'], r['surface']): r['raster_fnv64'] for r in a['surfaces']}
        bb = {(r['id'], r['surface']): r['raster_fnv64'] for r in b['surfaces']}
        assert aa == bb, 'optimized/sanitized raster inventory or pixels differ'
        for name, side in aa:
            assert sha(Path(a['capture']) / (name + '_' + side + '.ppm')) == sha(Path(b['capture']) / (name + '_' + side + '.ppm'))
        for name in ('behavior.json', 'motion.tsv'):
            assert sha(Path(a['capture']) / name) == sha(Path(b['capture']) / name)
        path = OUT / ('compare-' + host.stamp() + '.json')
        save(path, {'status': 'passed', 'raw_surfaces_equal': len(aa), 'source_sha256': a['source_sha256'],
                    'runner_sha256': sha(__file__), 'host_only': True, 'hardware': False})
        print(path)
        return
    guards = draw_guards()
    receipt, proof = host.build(ROOT, args.mode, args.mode == 'sanitized', True)
    path = OUT / ('inspection-' + args.mode + '-' + host.stamp() + '.json')
    result = {'receipt': str(receipt), 'receipt_sha256': sha(receipt), 'runner_sha256': sha(__file__),
              'status': 'pending', 'draw_graph': guards, 'host_only': True, 'hardware': False,
              'limitations': ['synthetic process-memory input, not microphone/hardware',
                              'software raster/object budget, not GPU timing/FPS',
                              'no listening, ARM build, deployment, or core regression claim']}
    save(path, result)
    try:
        result['inspection'] = inspect(proof)
        result['retained_inputs'] = freeze_inputs(receipt, proof)
        result['status'] = 'passed'
    except Exception as error:
        result['status'] = 'failed'
        result['failure'] = str(error)
        save(path, result)
        raise
    save(path, result)
    save(OUT / ('inspection-' + args.mode + '-latest.json'), {'receipt': str(path), 'sha256': sha(path)})
    print(path)


if __name__ == '__main__':
    main()
