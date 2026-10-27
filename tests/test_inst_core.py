#!/usr/bin/env python3
"""Only INST: pinned-bank deterministic rebuild + actual units.cpp focused core proof.
No full application, UI, ARM, device/deploy, unrelated suite or media generation.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_dependencies():
    pending = [ROOT / p for p in ('tests/inst_core_test.cpp', 'core/engine/units.cpp', 'core/synth/sampler.cpp')]
    result = {}
    while pending:
        path = pending.pop().resolve()
        if path in result:
            continue
        text = path.read_text()
        result[path] = sha(path)
        for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
            candidates = (path.parent / name, ROOT / 'core' / name, ROOT / 'core/engine' / name)
            dependency = next((p for p in candidates if p.is_file()), None)
            assert dependency is not None, (path, name)
            pending.append(dependency)
    return result


def source_guards():
    unit = (ROOT / 'core/engine/unit.h').read_text()
    units = (ROOT / 'core/engine/units.h').read_text()
    implementation = (ROOT / 'core/engine/units.cpp').read_text()
    assert re.search(r'\bInst\b', unit) and '"inst_unit.h"' in units and '"inst_impl.inc"' in implementation, \
        'parent UnitKind/header/implementation integration not ready; do not substitute test-only runtime definitions'
    source = (ROOT / 'core/engine/inst_impl.inc').read_text()
    source = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', '', source)
    process = source[source.index('void InstUnit::process('):source.index('InstUnit::Visual InstUnit::visual(')]
    assert '/' not in process and '%' not in process, 'per-sample path must not divide/modulo'
    for token in ('float', 'double', 'new ', 'delete ', 'malloc(', 'calloc(', 'realloc(', 'fopen(', 'lock(', 'SampleBank'):
        assert token not in source, token
    print('pass inst source guards: fixed const bank, no mutable SampleBank/fs/heap/locks/float or per-sample division', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    source_guards()
    subprocess.run([os.environ.get('PYTHON', 'python3'), 'tools/prepare_inst_bank.py', '--check'], cwd=ROOT, check=True)
    before = source_dependencies()
    flags = ['-O2'] if not args.sanitize else ['-O1', '-g', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    env = os.environ.copy()
    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    wraps = ('malloc', 'calloc', 'realloc', 'free', '_Znwm', '_Znam', '_ZnwmRKSt9nothrow_t', '_ZnamRKSt9nothrow_t')
    with tempfile.TemporaryDirectory(prefix='seance-inst-core-') as directory:
        binary = Path(directory) / 'test'
        command = ['g++', '-std=gnu++17', *flags, '-Wall', '-Wextra', '-fno-rtti',
                   '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                   '-Icore', '-Icore/engine', *('-Wl,--wrap=' + name for name in wraps),
                   'tests/inst_core_test.cpp', 'core/engine/units.cpp', 'core/synth/sampler.cpp', '-o', str(binary)]
        print(shlex.join(command), flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
        assert before == source_dependencies(), 'source changed during compile; do not claim this binary tests current core'
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True, timeout=60)
        assert before == source_dependencies(), 'source changed during test; do not claim current-source coverage'
    print('pass inst focused core' + (' / ASAN+UBSAN+leaks' if args.sanitize else '') +
          '; hardware sound/performance not measured', flush=True)


if __name__ == '__main__':
    main()
