#!/usr/bin/env python3
"""One focused ambient-paper host test; no suites, ARM build or render exports."""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie'] if args.sanitize else ['-O2']
    with tempfile.TemporaryDirectory(prefix='seance-paper-motion-', dir='/tmp') as tmp:
        binary = Path(tmp) / 'paper_motion_test'
        command = [
            'g++', '-std=c++17', *flags, '-Wall', '-Wextra', '-Werror', '-D__3DS__',
            '-Itools/paper_motion_test_stubs',
            'tools/paper_motion_test.cpp', 'platform/3ds/menu_style.cpp',
            'core/ui/draw.cpp', 'core/ui/font.cpp',
            '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc', '-o', str(binary),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
