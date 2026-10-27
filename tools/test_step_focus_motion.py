#!/usr/bin/env python3
"""Focused native ink-plate geometry/clipping/heap proof; no ARM, FTP or full suite."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['tools/test_step_focus_motion.py', 'tools/step_focus_motion_test.cpp',
           'platform/3ds/step_focus_motion.h', 'platform/3ds/step_focus_ink.h',
           'platform/3ds/sequencer_layout.h', 'core/ui/draw.cpp', 'core/ui/draw.h',
           'core/ui/font.cpp', 'core/ui/font.h', 'core/ui/pixel_geometry.h', 'tools/sequencer_test_stubs/3ds.h',
           'tools/sequencer_test_stubs/citro2d.h']


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    out = ROOT/'ui_mockups/step_focus_runtime/motion'/datetime.now(timezone.utc).strftime('run-%Y%m%dT%H%M%S.%fZ')
    out.mkdir(parents=True)
    identity = {p: sha(ROOT/p) for p in SOURCES}
    cmd = ['g++', '-std=gnu++17', '-Wall', '-Wextra', '-O1' if args.sanitize else '-O2']
    if args.sanitize:
        cmd += ['-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined', '-no-pie']
    cmd += ['-Itools/sequencer_test_stubs', 'tools/step_focus_motion_test.cpp',
            'core/ui/draw.cpp', 'core/ui/font.cpp', '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc', '-o', str(out/'host')]
    runs = []
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    for label, command in [('build', cmd), ('check', [str(out/'host')])]:
        start = time.monotonic()
        p = subprocess.run(command, cwd=ROOT, env=env, text=True, capture_output=True)
        (out/f'{label}.log').write_text(p.stdout+p.stderr)
        runs.append(dict(command=command, returncode=p.returncode, seconds=round(time.monotonic()-start, 3), log=f'{label}.log'))
        print(label, p.returncode, p.stdout, p.stderr, flush=True)
        (out/'verification.json').write_text(json.dumps(dict(status='pass' if p.returncode == 0 and label == 'check' else 'incomplete',
            sanitized=args.sanitize, sources=identity, runs=runs,
            limitations=['host geometry and native rectangle/font evidence, not GPU coverage or hardware FPS',
                         'test-only fixed raster buffers are not runtime memory allocations']), indent=2)+'\n')
        p.check_returncode()
    assert identity == {p: sha(ROOT/p) for p in SOURCES}, 'source changed during proof'
    print(out/'verification.json')


if __name__ == '__main__':
    main()
