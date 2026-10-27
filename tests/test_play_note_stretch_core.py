#!/usr/bin/env python3
"""Focused real-core PLAY note/stretch test; no media/build/deploy side effects."""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--sanitize', action='store_true', help='strict ASAN/UBSAN and leak checks')
    args = parser.parse_args()
    sources = [ROOT / 'tests/play_note_stretch_core_test.cpp', *sorted((ROOT / 'core/engine').glob('*.cpp')),
               *(ROOT / f'core/synth/{name}.cpp' for name in ('sampler', 'wav_loader', 'wav_writer'))]
    inputs = sorted({*sources, Path(__file__), *(path for path in (ROOT / 'core').rglob('*')
                                             if path.suffix in ('.h', '.inc', '.cpp'))})
    before = {str(path.relative_to(ROOT)): digest(path) for path in inputs}
    with tempfile.TemporaryDirectory(prefix='seance-play-note-stretch-') as directory:
        binary = str(Path(directory) / 'core-test')
        flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie'] if args.sanitize else ['-O2']
        command = ['g++', *flags, '-std=gnu++17', '-Wall', '-Wextra', '-Icore',
                   *(str(path.relative_to(ROOT)) for path in sources), '-o', binary]
        print('+', shlex.join(command), flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
        assert before == {str(path.relative_to(ROOT)): digest(path) for path in inputs}, 'core inputs changed during compile'
        env = os.environ.copy()
        if args.sanitize:
            env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
            env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        print('+', binary, '(strict sanitizers)' if args.sanitize else '', flush=True)
        subprocess.run([binary], cwd=ROOT, env=env, check=True)
        assert before == {str(path.relative_to(ROOT)): digest(path) for path in inputs}, 'core inputs changed during test'
        print(f'verified {len(inputs)} unchanged core/test inputs; no hardware/media/deployment', flush=True)


if __name__ == '__main__':
    main()
