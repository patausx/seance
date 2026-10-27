#!/usr/bin/env python3
"""One narrow liquid-stylus ASAN/UBSAN/leak test; no media, suites or ARM build."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='seance-stylus-liquid-', dir='/tmp') as tmp:
        binary = Path(tmp) / 'stylus_liquid_test'
        subprocess.run([
            'g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
            'tests/stylus_liquid_test.cpp',
            '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc', '-o', str(binary),
        ], cwd=ROOT, check=True)
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)


if __name__ == '__main__':
    main()
