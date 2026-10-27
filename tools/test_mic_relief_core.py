#!/usr/bin/env python3
"""Run unchanged historical five-group MIC core sanitizer proof into MIC relief receipts.

Checks byte preservation of the pre-UI core tree and old MIC harness. No old UI
coordinates, old receipt writes, ARM, network, device claims or presentation audio.
The historical persistence group uses its own unique /tmp SNC and deletes it.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
from test_fader_lancet_ui import ROOT, make_variable
from test_editor_fullscreen_ui import sha, run

OUT = ROOT / 'release/mic-relief'
PROOF = OUT / 'core-proof.json'
BASELINE = OUT / 'core-baseline.json'


def inputs():
    names = list(json.loads(BASELINE.read_text())['sha256']) + [
        'tools/test_mic_relief_core.py', 'tools/test_editor_fullscreen_ui.py',
        'tools/test_fader_lancet_ui.py', 'Makefile', str(BASELINE.relative_to(ROOT))]
    return {name: sha(ROOT / name) for name in sorted(names)}


def preservation():
    expected = json.loads(BASELINE.read_text())['sha256']
    for name, digest in expected.items():
        assert sha(ROOT / name) == digest, 'core/historical test changed: ' + name
    return len(expected)


def verify(proof):
    assert inputs() == proof['inputs'], 'inputs changed'
    assert sha(proof['binary']) == proof['binary_sha256'], 'binary changed'
    for name, digest in proof['dependencies'].items():
        assert sha(name) == digest, name
    preservation()


def save(proof, stamp):
    text = json.dumps(proof, indent=2) + '\n'
    PROOF.write_text(text)
    (OUT / ('core-receipt-' + stamp + '.json')).write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    preserved = preservation()
    if args.verify_only:
        proof = json.loads(PROOF.read_text()); verify(proof)
        print(f'verified {preserved} preserved core/historical test files, inputs/dependencies/binary')
        return
    OUT.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    before = inputs()
    directory = Path(tempfile.mkdtemp(prefix='seance-mic-relief-core-'))
    binary = directory / 'mic_core_san'
    compiler = os.environ.get('CXX', 'g++')
    units = make_variable('MIC_CORE_SOURCES')
    flags = ['-O1', '-g1', '-std=gnu++17', '-Wall', '-Wextra', '-ffunction-sections',
             '-fdata-sections', '-Icore', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    command = [compiler, *flags, *units, '-Wl,--gc-sections', '-o', str(binary)]
    log = OUT / ('core-build-' + stamp + '.log')
    proof = dict(inputs=before, command=command, preserved_files=preserved,
                 checks={'status': 'not run'}, build={'status': 'pending'},
                 unchanged_historical_groups=5, old_coordinate_ui_groups=0,
                 sanitizers='ASAN/UBSAN/leaks', host_only=True, hardware=False, deployment=False)
    save(proof, stamp)
    status = run(command, log)
    if status:
        proof['build'] = dict(status='failed', exit=status, log=str(log.relative_to(ROOT)))
        save(proof, stamp); raise SystemExit(status)
    assert inputs() == before, 'inputs changed during build'
    result = subprocess.run([compiler, *flags, '-MM', *units], cwd=ROOT,
                            capture_output=True, text=True, check=True)
    deps = {}
    for line in result.stdout.replace('\\\n', ' ').splitlines():
        if ':' in line:
            for name in shlex.split(line.split(':', 1)[1]):
                p = (ROOT / name).resolve(); deps[str(p)] = sha(p)
    proof.update(binary=str(binary), binary_sha256=sha(binary), dependencies=deps,
                 build={'status': 'passed', 'log': str(log.relative_to(ROOT))})
    check_log = OUT / ('core-check-' + stamp + '.log')
    status = run([str(binary)], check_log)
    text = check_log.read_text()
    proof['checks'] = dict(status='passed' if status == 0 else 'failed', exit=status,
                          log=str(check_log.relative_to(ROOT)),
                          summary=[line for line in text.splitlines() if line.startswith('mic core:')])
    save(proof, stamp)
    if status:
        raise SystemExit(status)
    assert 'mic core: 5/5 passed (' in text, 'missing historical group summary'
    verify(proof); save(proof, stamp); print(PROOF)


if __name__ == '__main__':
    main()
