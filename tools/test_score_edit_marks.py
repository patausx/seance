#!/usr/bin/env python3
"""Build actual production sequencer; compare all 12 rasters to approved study.

No cached objects or substituted drawing TU. Reuse only current host harness
boundaries and the deterministic study scene setup. Inherited suites do not run.
All runs have fresh output directories; historical proposals remain read-only.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

from PIL import Image
import test_fader_lancet_ui as common
import test_lancet_nonmodule as harness
import render_score_edit_marks_study as scenes
from render_module_geometry_study import protected_hashes

ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT / 'release/score-edit-marks'
APPROVED = ROOT / 'ui_mockups/score_pip_compact_study/20261005T143643_568213Z'
ORIGINAL = ROOT / 'ui_mockups/score_edit_marks_study/20261005T141732_729257Z'
POSITIONS = {1: ((0, 0),), 2: ((-3, -3), (3, 3)),
             3: ((-4, -4), (0, 0), (4, 4)), 4: ((-3, -3), (3, -3), (-3, 3), (3, 3))}


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def save(p, data):
    p.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')


def preservation():
    before = json.loads((RELEASE / 'before.json').read_text())
    current = protected_hashes()
    assert current.keys() == before['protected'].keys()
    changes = [p for p, h in current.items() if h != before['protected'][p]]
    assert changes == ['platform/3ds/ui_sequencer.cpp'], changes
    baseline = ROOT / before['source_backup']
    assert sha(baseline) == before['source_backup_sha256']
    old = baseline.read_text()
    new = (ROOT / changes[0]).read_text()
    a, b = common.functions(old), common.functions(new)
    removed = {'rect', 'text', 'line', 'frame', 'point'}  # obsolete inverse-only sink
    assert a.keys() - b.keys() == removed and not b.keys() - a.keys()
    draws = {'cell_label', 'draw_event_content', 'draw_event_cell', 'draw_retrig_dots',
             'draw_flow_lane', 'draw_extent_marks', 'draw_score'}
    resets = {'sequencer_forget_pins', 'sequencer_view_cancel_input', 'sequencer_view_draw'}
    changed, unchanged = [], []
    for name in b:
        aa, bb = a[name][2], b[name][2]
        if name in resets:
            aa = aa.replace('g_step_focus.reset();', '')
        if common.tokens(aa) == common.tokens(bb):
            unchanged.append(name)
        else:
            assert name in draws, ('non-drawing function changed', name)
            changed.append(name)
    outer = []
    for source, functions in ((old, a), (new, b)):
        source = common.strip_spans(source, [(s, e) for s, e, _ in functions.values()])
        source = re.sub(r'namespace score_ink\s*\{\s*\}', '', common.masked(source))
        for declaration in ('step_focus::State g_step_focus;', 'step_focus::Box g_focus_box{};',
                            'bool g_focus_inverse = false;'):
            source = source.replace(declaration, '')
        outer.append(common.tokens(source))
    assert outer[0] == outer[1], 'non-render declarations changed'
    return dict(changed_production=changes, unchanged_protected=len(current)-1,
                drawing_functions=changed, unchanged_functions=unchanged,
                removed_inverse_sink=sorted(removed), removed_obsolete_resets=sorted(resets),
                protected_current=current, baseline_sha256=sha(baseline))


def pinned_image(folder, name):
    proof = json.loads((folder / 'proposal.json').read_text())
    record = next(r for r in proof['outputs'] if Path(r['path']).name == name)
    p = ROOT / record['path']
    assert sha(p) == record['sha256'], ('changed historical image', p)
    with Image.open(p) as image:
        return image.convert('RGB').resize((image.width//2, image.height//2), Image.Resampling.NEAREST)


def verify_images(folder):
    records, pips = [], []
    for theme in ('graphite', 'pink'):
        for case in ('flow', 'dice', 'level'):
            pair = pinned_image(ORIGINAL, f'{theme}-{case}-dual-2x.png')
            for screen in ('top', 'bottom'):
                if screen == 'top':
                    expected = pair.crop((0, 0, 400, 240))
                elif case == 'dice':
                    expected = pinned_image(APPROVED, f'{theme}-compact-2x.png')
                else:
                    expected = pair.crop((40, 256, 360, 496))
                path = folder / 'capture' / f'{theme}_{case}_{screen}.ppm'
                with Image.open(path) as image:
                    actual = image.convert('RGB')
                assert actual.size == expected.size and actual.tobytes() == expected.tobytes(), (theme, case, screen)
                actual.save(path.with_suffix('.png'))
                actual.resize((actual.width*2, actual.height*2), Image.Resampling.NEAREST).save(path.with_name(path.stem+'_2x.png'))
                records.append(dict(theme=theme, case=case, screen=screen, exact_approved=True))
                if case != 'dice' or screen != 'bottom':
                    continue
                ink = (244, 244, 244) if theme == 'graphite' else (31, 20, 31)
                ghost = (184, 184, 184) if theme == 'graphite' else (126, 100, 119)
                accent = (181, 154, 232) if theme == 'graphite' else (91, 49, 124)
                for count, col in ((1, 0), (2, 3), (3, 4), (4, 7)):
                    x, y = 8+col*19, 45
                    points = {(xx, yy) for yy in range(18) for xx in range(18)
                              if actual.getpixel((x+xx, y+yy)) == ink}
                    wanted = {(8+dx+i, 8+dy+j) for dx, dy in POSITIONS[count] for i in range(2) for j in range(2)}
                    assert points == wanted and len(points) == 4*count
                    for axis in (0, 1):
                        assert sum(p[axis] for p in points)*2 == 17*len(points)
                        assert min(p[axis] for p in points)+max(p[axis] for p in points) == 17
                    cx = 100+(count-1)*60
                    points = {(xx, yy) for yy in range(15) for xx in range(15)
                              if actual.getpixel((cx-7+xx, 211+yy)) == (accent if count == 4 else ghost)}
                    wanted = {(6+dx+i, 6+dy+j) for dx, dy in POSITIONS[count] for i in range(2) for j in range(2)}
                    assert points == wanted and len(points) == 4*count
                    pips.append(dict(theme=theme, count=count, cell_center=[8.5, 8.5], selector_center=[cx-.5, 217.5]))
    return records, pips


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sanitized', action='store_true')
    args = ap.parse_args()
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    folder = RELEASE / (('capture-sanitized-' if args.sanitized else 'capture-optimized-')+stamp)
    folder.mkdir(parents=True, exist_ok=False)
    guard = preservation()
    def tool_pins():
        return {str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'tools').rglob('*')
                if p.is_file() and p.suffix in ('.cpp', '.c', '.h', '.hpp', '.inc', '.py')} | {'Makefile':sha(ROOT/'Makefile')}
    tools_before = tool_pins()
    (folder/'runner.py').write_bytes(Path(__file__).read_bytes())
    source = harness.driver(folder)
    text = source.read_text()
    marker = 'int main(int argc,char**argv)'
    assert text.count(marker) == 1
    source.write_text(text.replace(marker, 'int unused_nonmodule_main(int argc,char**argv)')+'\n'+scenes.DRIVER)
    units = common.make_variable('OSC_INPUT_SOURCES')
    units[units.index('tools/osc_input_dispatch_test.cpp')] = str(source)
    units += ['platform/3ds/ui_chains.cpp', 'platform/3ds/ui_chains_top.cpp', 'platform/3ds/ui_workspace.cpp']
    flags = ['-O1' if args.sanitized else '-O2', '-g1', '-I'+str(folder),
             *common.make_variable('SEQUENCER_TEST_FLAGS'), '-DSEANCE_REAL_EDITOR', '-Itools', '-I'+str(ROOT), *common.WRAP]
    if args.sanitized:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    compiler = os.environ.get('CXX', 'g++')
    objects = [folder/(f'{i:02d}_'+Path(u).stem+'.o') for i, u in enumerate(units)]
    commands = [[compiler, *flags, '-MMD', '-MF', str(o.with_suffix('.d')), '-c', u, '-o', str(o)] for o, u in zip(objects, units)]
    proof = dict(status='building', preservation=guard, commands=commands, runner_sha256=sha(__file__),
                 study_driver_sha256=sha(scenes.__file__), approved_receipt_sha256=sha(APPROVED/'proposal.json'),
                 fresh_all_objects=True, inherited_suites_run=False, hardware=False, deployment=False)
    receipt = folder/'receipt.json'
    save(receipt, proof)
    def compile_one(item):
        i, cmd = item
        code = harness.run(cmd, folder/f'compile-{i:02d}.log')
        print('compile', i, code, flush=True)
        return code
    with ThreadPoolExecutor(max_workers=2) as pool:
        proof['compile_exit'] = list(pool.map(compile_one, enumerate(commands)))
    save(receipt, proof)
    assert not any(proof['compile_exit']), receipt
    assert tools_before == tool_pins(), 'test source drift during compile; rebuild'
    binary = folder/'score_edit_marks'
    link = [compiler, *flags, *map(str, objects), '-o', str(binary)]
    assert harness.run(link, folder/'link.log') == 0
    dependencies = {}
    for obj in objects:
        for dep in shlex.split(obj.with_suffix('.d').read_text().replace('\\\n', ' ').split(':', 1)[1]):
            path = (ROOT/dep).resolve()
            dependencies[str(path)] = sha(path)
    proof.update(link=link, binary_sha256=sha(binary), dependencies=dependencies)
    save(receipt, proof)
    assert harness.run([str(binary), str(folder/'capture')], folder/'run.log') == 0
    proof['rasters'], proof['pips'] = verify_images(folder)
    assert preservation() == guard
    assert tools_before == tool_pins(), 'test source drift during capture; rebuild'
    proof['test_inputs'] = tools_before
    assert all(sha(p) == h for p, h in dependencies.items()), 'dependency drift during capture'
    proof.update(status='passed', media={str(p.relative_to(folder)):sha(p) for p in (folder/'capture').iterdir()},
                 limitations=['host software raster, not PICA/device acceptance', 'separate focused regressions are not invoked by this capture driver'])
    save(receipt, proof)
    print(receipt)
    print('12 complete native rasters exact approved study / 16 exact cell+selector pip groups / production draw-only')


if __name__ == '__main__':
    main()
