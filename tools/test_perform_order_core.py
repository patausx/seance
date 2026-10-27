#!/usr/bin/env python3
"""Retained isolated real-core ORDER + transition proof. No ARM build/media/deploy.

Default optimized; --sanitize enables ASAN/UBSAN/leaks. Each attempt copies all
inputs, records hashes/logs and a receipt, and rejects edits during compile/run.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output-dir', default='release/perform-order/core')
    args = parser.parse_args()
    mode = 'sanitized' if args.sanitize else 'optimized'
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    output = ROOT / args.output_dir / f'{mode}-{stamp}'
    output.mkdir(parents=True, exist_ok=False)
    frozen = output / 'inputs'
    sources = [Path('tools/perform_order_core_test.cpp'),
               *(path.relative_to(ROOT) for path in sorted((ROOT / 'core/engine').glob('*.cpp'))),
               *(Path(f'core/synth/{name}.cpp') for name in ('sampler', 'wav_loader', 'wav_writer'))]
    inputs = sorted({*sources, Path(__file__).relative_to(ROOT), Path('tools/transition_core_test.h'),
                     *(path.relative_to(ROOT) for path in (ROOT / 'core').rglob('*')
                       if path.suffix in ('.h', '.inc', '.cpp'))})
    before = {str(path): digest(ROOT / path) for path in inputs}
    for path in inputs:
        target = frozen / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((ROOT / path).read_bytes())
        assert digest(target) == before[str(path)], f'input changed while copying: {path}'
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie'] if args.sanitize else ['-O2']
    binary = output / 'perform-order-core-test'
    command = ['g++', *flags, '-std=gnu++17', '-Wall', '-Wextra', '-Icore',
               *(str(path) for path in sources), '-o', str(binary)]
    receipt = {'status': 'running', 'mode': mode, 'stamp': stamp, 'inputs': before,
               'compile': command, 'output': str(output.relative_to(ROOT))}
    proof = output / 'proof.json'
    proof.write_text(json.dumps(receipt, indent=2) + '\n')

    def run(argv, cwd, log, env=None):
        print('+', shlex.join(str(a) for a in argv), flush=True)
        with log.open('w') as f:
            result = subprocess.run(argv, cwd=cwd, env=env, stdout=f, stderr=subprocess.STDOUT)
        print(log.read_text(), end='', flush=True)
        if result.returncode:
            raise RuntimeError(f'{log.name}: exit {result.returncode}')

    try:
        run(command, frozen, output / 'compile.log')
        env = os.environ.copy()
        if args.sanitize:
            env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
            env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        work = output / 'work'
        work.mkdir()
        run([str(binary), str(work)], frozen, output / 'test.log', env)
        assert before == {str(path): digest(ROOT / path) for path in inputs}, 'live core/test inputs changed during proof'
        assert before == {str(path): digest(frozen / path) for path in inputs}, 'frozen inputs changed during proof'
        receipt.update(status='passed', executable_sha256=digest(binary),
                       compile_log_sha256=digest(output / 'compile.log'), test_log_sha256=digest(output / 'test.log'),
                       checked_unchanged_inputs=len(inputs), sanitizers='address,undefined,leaks' if args.sanitize else None)
    except Exception as exc:
        receipt.update(status='failed', error=str(exc))
        raise
    finally:
        proof.write_text(json.dumps(receipt, indent=2) + '\n')
        print('receipt:', proof.relative_to(ROOT), flush=True)


if __name__ == '__main__':
    main()
