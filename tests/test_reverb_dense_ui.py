#!/usr/bin/env python3
"""Build/check only REV against exact current main/editor; record binary provenance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path('/tmp/seance_rev_dense_ui_san')
PROVENANCE = BINARY.with_suffix('.sources.json')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    paths = [p for parent in ('core', 'platform/3ds') for p in (ROOT / parent).rglob('*')
             if p.is_file() and p.suffix in ('.h', '.cpp', '.inc')]
    paths += list((ROOT / 'tests').glob('*.h'))
    paths += [ROOT / p for p in ('Makefile', 'tests/sequencer_test.cpp',
              'tests/osc_input_dispatch_test.cpp', 'tests/extract_osc_input.py',
              'tests/test_reverb_dense_ui.py')]
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted(set(paths))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    before = sources()
    subprocess.run(['make', str(BINARY)], cwd=ROOT, check=True)
    assert before == sources(), 'source changed during build; do not claim current-binary coverage'
    report = {'source_sha256': before, 'binary_sha256': sha(BINARY),
              'checks': 'not run' if args.build_only else 'pending'}
    PROVENANCE.write_text(json.dumps(report, indent=2) + '\n')
    if not args.build_only:
        env = os.environ.copy()
        env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        subprocess.run([str(BINARY), '--rev-dense-only'], cwd=ROOT, env=env, check=True, timeout=120)
        assert before == sources(), 'source changed during checks'
        report['checks'] = '11 focused REV UI groups, ASAN/UBSAN/leaks'
        PROVENANCE.write_text(json.dumps(report, indent=2) + '\n')
    print(PROVENANCE)


if __name__ == '__main__':
    main()
