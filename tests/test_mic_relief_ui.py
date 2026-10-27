#!/usr/bin/env python3
"""Focused MIC relief: exact current main/controller/draw with host raster + texture boundary.

Only new MIC cases run; inherited legacy-coordinate suites never run. Captures
are actual C++ using deterministic synthetic raw PCM, not a device/MIC recording,
Pillow redraw, FFT, measured GPU budget or FPS proof. No ARM, network or deployment.
All persistent outputs stay under release/mic-relief; historical receipts are kept.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import test_editor_fullscreen_ui as host
from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources as common_sources, functions, masked

OUT = ROOT / 'release/mic-relief'
PROOF = OUT / 'ui-proof.json'
OWN = ('tests/test_mic_relief_ui.py', 'tests/mic_relief_ui_cases.inc',
       'tests/test_editor_fullscreen_ui.py', 'tests/editor_fullscreen_ui_cases.inc',
       'tests/filter_interaction_ui_cases.inc', 'tests/test_mod_minimal_ui.py',
       'tests/paper_motion_test_stubs/citro2d.h', 'release/mic-relief/ui-key-baseline.json')
sha = host.sha


def sources():
    result = common_sources()
    for name in OWN:
        result[name] = sha(ROOT / name)
    return dict(sorted(result.items()))


def verify(proof):
    assert sources() == proof['inputs'], 'sources changed; rebuild after explicit freeze'
    assert sha(proof['binary']) == proof['binary_sha256'], 'binary changed'
    for name, digest in proof['dependencies'].items():
        assert sha(name) == digest, name
    if 'capture' in proof:
        folder = ROOT / proof['capture']['path']
        for name, digest in proof['capture']['sha256'].items():
            assert sha(folder / name) == digest, name


def save(proof, stamp):
    text = json.dumps(proof, indent=2) + '\n'
    PROOF.write_text(text)
    (OUT / ('ui-receipt-' + stamp + '.json')).write_text(text)


def draw_guards():
    result = {}
    definitions = functions((ROOT / 'platform/3ds/ui_module_focus_mic.cpp').read_text())
    for name in ('mic_focus_draw', 'mic_focus_draw_top'):
        body = masked(definitions[name][2])
        assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|malloc|calloc|realloc|new|delete|C3D_TexInit|copy_input|process)\b', body), name
        result[name] = host.sha_text(body)
    baseline = json.loads((OUT / 'ui-key-baseline.json').read_text())
    source = (ROOT / baseline['source']).read_text()
    for label, saved in baseline['paths'].items():
        at = source.index(saved['start'])
        body = source[at:source.index(saved['end'], at)]
        assert host.sha_text(body) == saved['sha256'], 'actual main key path changed: ' + label
        result['main:' + label] = saved['sha256']
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--capture-only', action='store_true')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    assert not (args.capture_only and args.build_only)
    OUT.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    before = sources()
    if args.no_build or args.capture_only or args.verify_only:
        proof = json.loads(PROOF.read_text())
        verify(proof)
        binary = Path(proof['binary'])
        if args.verify_only:
            print(f"verified {len(proof['inputs'])} inputs, {len(proof['dependencies'])} dependencies, binary and retained captures")
            return
    else:
        directory = Path(tempfile.mkdtemp(prefix='seance-mic-relief-'))
        driver = host.driver(directory)
        text = driver.read_text()
        entry = 'int main(int argc,char**argv){'
        assert text.count(entry) == 1
        text = text.replace(entry, 'int unused_mic_fullscreen_main(int argc,char**argv){') + '\n' + (ROOT / OWN[1]).read_text()
        text = text.replace('#include "../', '#include "' + str(ROOT) + '/')
        driver.write_text(text)
        units = make_variable('OSC_INPUT_SOURCES')
        units[units.index('tests/osc_input_dispatch_test.cpp')] = str(driver)
        units.remove('platform/3ds/menu_style.cpp')
        binary = directory / 'mic_relief_ui_san'
        obj = directory / 'menu_style.o'
        flags = ['-O0', '-g1', '-I' + str(directory), *make_variable('SEQUENCER_TEST_FLAGS'),
                 '-DSEANCE_REAL_EDITOR', '-Itests', '-fsanitize=address,undefined',
                 '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', *WRAP,
                 '-Wl,--wrap=_ZN6seance2ui4lineEiiiiji']
        compiler = os.environ.get('CXX', 'g++')
        commands = [[compiler, *flags, '-D__3DS__', '-c', 'platform/3ds/menu_style.cpp', '-o', str(obj)],
                    [compiler, *flags, *units, str(obj), '-o', str(binary)]]
        proof = dict(inputs=before, commands=commands, checks={'status': 'not run'},
                     build={'status': 'pending'}, host_only=True, hardware=False, deployment=False,
                     inherited_suites=0, limitations=['software texture/raster boundary, not device GPU/FPS',
                     'synthetic PCM pushed into real MicUnit, not physical MIC capture',
                     'B/X preserved source paths and cancellation; unrelated main modal screens shimmed'])
        save(proof, stamp)
        for i, command in enumerate(commands):
            log = OUT / ('ui-build-' + stamp + '-' + str(i) + '.log')
            status = host.run(command, log)
            if status:
                proof['build'] = dict(status='failed', exit=status, log=str(log.relative_to(ROOT)))
                save(proof, stamp)
                raise SystemExit(status)
        assert before == sources(), 'source edits during build; rebuild after freeze'
        deps = {}
        for extra, src in [([], units), (['-D__3DS__'], ['platform/3ds/menu_style.cpp'])]:
            result = subprocess.run([compiler, *[x for x in flags if not x.startswith('-Wl,')], *extra, '-MM', *src],
                                    cwd=ROOT, capture_output=True, text=True, check=True)
            for line in result.stdout.replace('\\\n', ' ').splitlines():
                if ':' in line:
                    for name in shlex.split(line.split(':', 1)[1]):
                        path = (ROOT / name).resolve()
                        deps[str(path)] = sha(path)
        proof.update(binary=str(binary), binary_sha256=sha(binary), dependencies=deps,
                     build={'status': 'passed'}, draw_guards=draw_guards())
        save(proof, stamp)
    if args.build_only:
        return
    if args.capture_only:
        assert proof['checks']['status'] == 'passed', 'focused groups must pass before capture'
        destination = OUT / ('capture-' + stamp)
        log = OUT / ('ui-capture-' + stamp + '.log')
        status = host.run([str(binary), '--capture-only', str(destination)], log)
        if status:
            raise SystemExit(status)
        from PIL import Image, PngImagePlugin
        pairs = list(sorted(destination.glob('*_top.ppm')))
        assert 8 <= len(pairs) <= 12, len(pairs)
        for top in pairs:
            name = top.name[:-8]
            with Image.open(top) as a, Image.open(destination / (name + '_bottom.ppm')) as b:
                assert a.size == (400, 240) and b.size == (320, 240)
                image = Image.new('RGB', (400, 496), (18, 18, 21))
                image.paste(a, (0, 0)); image.paste(b, (40, 256))
            large = image.resize((800, 992), Image.Resampling.NEAREST)
            meta = PngImagePlugin.PngInfo(); meta.add_text('provenance', __doc__)
            path = destination / (name + '-2x.png'); large.save(path, pnginfo=meta)
            with Image.open(path) as decoded:
                assert decoded.tobytes() == large.tobytes(), path
        proof['capture'] = dict(path=str(destination.relative_to(ROOT)),
                                sha256={p.name: sha(p) for p in sorted(destination.iterdir()) if p.is_file()},
                                log=str(log.relative_to(ROOT)), native_duals=len(pairs), nearest_neighbor=True)
    else:
        log = OUT / ('ui-check-' + stamp + '.log')
        status = host.run([str(binary), '--proof-only'], log)
        text = log.read_text()
        proof['checks'] = dict(status='passed' if status == 0 else 'failed', log=str(log.relative_to(ROOT)),
                               sanitizers='ASAN/UBSAN/leaks', exit=status,
                               summary=[line for line in text.splitlines() if line.startswith('mic relief UI:')])
        save(proof, stamp)
        if status:
            raise SystemExit(status)
    verify(proof); save(proof, stamp); print(PROOF)


if __name__ == '__main__':
    main()
