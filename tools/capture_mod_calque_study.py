#!/usr/bin/env python3
"""Capture matched current module/lens surfaces for a design study, not a suite.

One isolated host compilation; no production edits, old PNGs, ARM build, device
operations or audio exports. The temporary translation unit changes ONLY the
old CLI entry name, then appends tools/mod_calque_capture.inc. Actual renderers,
controllers, glyphs and the established host raster are compiled unchanged.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

from PIL import Image
from test_fader_lancet_ui import ROOT, PROOF, BINARY as REFERENCE, sources, sha

OUT = ROOT / 'ui_mockups/mod_calque_study'
BINARY = Path('/tmp/seance_mod_calque_study_san')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=OUT)
    args = parser.parse_args()
    out = args.output.resolve()
    assert not out.exists(), 'preserve existing study; supply a new output directory'
    before = sources()
    previous = json.loads(PROOF.read_text())
    assert before == previous['source_sha256'], 'reference inventory is not current'
    assert sha(REFERENCE) == previous['binary_sha256'], 'reference executable changed'
    inputs = {name: sha(ROOT / name) for name in (
        'tools/capture_mod_calque_study.py', 'tools/mod_calque_capture.inc')}
    out.mkdir(parents=True)
    temporary = Path('/tmp/seance_mod_calque_native')
    assert not temporary.exists(), 'preserve earlier temporary driver'
    temporary.mkdir()
    original = (ROOT / 'tools/osc_input_dispatch_test.cpp').read_text()
    entry = 'int main(int argc,char** argv) {'
    assert original.count(entry) == 1
    changed = original.replace(entry, 'int unused_lancet_driver_main(int argc,char** argv) {', 1)
    driver = temporary / 'driver.cpp'
    driver.write_text(changed + '\n' + (ROOT / 'tools/mod_calque_capture.inc').read_text())
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')

    def run(command, log, timeout):
        print(shlex.join(map(str, command)), flush=True)
        with (out / log).open('w') as stream:
            stream.write('$ ' + shlex.join(map(str, command)) + '\n')
            stream.flush()
            result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
        text = (out / log).read_text()
        print(text[-10000:], end='', flush=True)
        assert result.returncode == 0, f'command failed: {out / log}'
        return text

    run([sys.executable, 'tools/extract_osc_input.py'], 'extract.log', 30)
    extracted = {p.name: sha(p) for p in sorted(Path('/tmp/seance_osc_input_native').glob('*.inc'))}
    command = list(previous['build_command'])
    assert command.count('tools/osc_input_dispatch_test.cpp') == 1
    command[command.index('tools/osc_input_dispatch_test.cpp')] = str(driver)
    assert command[-2] == '-o'
    command[-1] = str(BINARY)
    command.insert(1, '-Itools')
    run(command, 'build.log', 300)
    assert before == sources(), 'current sources changed during host build'
    captured_at = datetime.now(timezone.utc).isoformat()
    raw = out / 'capture'
    capture_command = [str(BINARY), str(raw)]
    message = run(capture_command, 'capture.log', 60)
    assert '20 fresh native surfaces; 0 regression groups' in message
    surfaces = sorted(raw.glob('*.ppm'))
    assert len(surfaces) == 20
    for path in surfaces:
        with Image.open(path) as image:
            image.load()
            assert image.size == (400 if path.name.endswith('_top.ppm') else 320, 240)
    assert before == sources(), 'production changed during capture'
    assert inputs == {name: sha(ROOT / name) for name in inputs}, 'capture tooling changed during operation'
    receipt = {
        'captured_at': captured_at, 'source_sha256': before, 'capture_tool_sha256': inputs,
        'binary_sha256': sha(BINARY), 'binary': str(BINARY),
        'reference_binary_sha256': previous['binary_sha256'],
        'temporary_driver_sha256': sha(driver), 'extracted_main_sha256': extracted,
        'build_command': command, 'capture_command': capture_command,
        'surface_sha256': {p.name: sha(p) for p in surfaces},
        'manifest_sha256': sha(raw / 'manifest.tsv'),
        'host_compilations': 1, 'regression_groups_run': 0,
        'matched_states': 4, 'native_surfaces': 20, 'hardware': False,
        'provenance': 'fresh current C++ module/lens/paper; same private engine frame before/after opening held-l',
        'limits': 'capture assertions only, not functional regression proof, animation, hardware FPS or audio review',
    }
    (out / 'capture.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n')
    print(f'{raw}: 4 matched states / 20 decoded native surfaces / zero suites', flush=True)


if __name__ == '__main__':
    main()
