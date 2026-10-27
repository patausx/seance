#!/usr/bin/env python3
"""One focused REV host suite: actual DSP/engine/SNC IO, pinned old process.
No shared harness, UI, broad suite, app build, deployment, render or listening.
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
SOURCES = ('tests/reverb_dense_core_test.cpp', 'core/engine/engine.cpp',
           'core/engine/units.cpp', 'core/engine/patch_io.cpp', 'core/engine/workspace_layout.cpp',
           'core/synth/sampler.cpp', 'core/synth/wav_loader.cpp')
ORIGINAL_SHA = '8d809bc4d9200e597a93c0ded8b4913d960a437c96fe3b2e2ac2b5e0115f5bcd'


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


def dependencies():
    pending = [ROOT / name for name in SOURCES]
    result = {}
    while pending:
        path = pending.pop().resolve()
        if path in result:
            continue
        data = path.read_bytes()
        result[path] = hashlib.sha256(data).hexdigest()
        for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', data.decode(), re.M):
            candidates = (path.parent / name, ROOT / 'core' / name, ROOT / 'core/engine' / name)
            include = next((p for p in candidates if p.is_file()), None)
            assert include is not None, (path, name)
            pending.append(include)
    return result


def guards():
    test = (ROOT / SOURCES[0]).read_text()
    original = function(test, 'void OldReverb::process(q15* buf, int frames)').replace(
        'OldReverb::process', 'ReverbUnit::process')
    assert hashlib.sha256(original.encode()).hexdigest() == ORIGINAL_SHA, 'pinned legacy oracle changed'
    source = (ROOT / 'core/engine/units.cpp').read_text()
    legacy = function(source, 'void ReverbUnit::process_legacy(q15* buf, int frames)').replace(
        'ReverbUnit::process_legacy', 'ReverbUnit::process')
    assert legacy == original, 'historical REV arithmetic changed'
    modern = (ROOT / 'core/engine/reverb_impl.inc').read_text()
    process = function(modern, 'void ReverbUnit::process_modern(q15* buf, int frames)')
    process = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/', '', process)
    for token in ('new ', 'delete ', 'malloc(', 'calloc(', 'realloc(', 'lock(', 'pow(',
                  'sqrt(', 'exp(', 'sin(', 'float', 'double', 'memset(', 'memcpy('):
        assert token not in process, token
    assert 'if (decay_cache != next_decay)' in modern and 'if (damp_cache != next_damp)' in modern
    assert 'if (mix_cache != next_mix)' in modern
    print('pass source guards: byte-pinned legacy process; cached scalar maps; no process heap/locks/transcendentals/history copy', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm', action='store_true', help='compile only touched core ARM objects')
    args = parser.parse_args()
    guards()
    before = dependencies()
    for name in ('core/engine/units.h', 'core/engine/units.cpp', 'core/engine/patch_io.cpp',
                 'core/engine/reverb_dsp.h', 'core/engine/reverb_impl.inc', SOURCES[0]):
        print(before[(ROOT / name).resolve()], name, flush=True)
    flags = ['-O2'] if not args.sanitize else ['-O1', '-g', '-fsanitize=address,undefined',
             '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    env = os.environ.copy()
    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    wraps = ('malloc', 'calloc', 'realloc', 'free', '_Znwm', '_Znam',
             '_ZnwmRKSt9nothrow_t', '_ZnamRKSt9nothrow_t')
    with tempfile.TemporaryDirectory(prefix='seance-reverb-core-') as directory:
        binary = Path(directory) / 'test'
        command = ['g++', '-std=gnu++17', *flags, '-Wall', '-Wextra', '-fno-rtti',
                   '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                   '-Icore', '-Icore/engine', *('-Wl,--wrap=' + name for name in wraps),
                   *SOURCES, '-o', str(binary)]
        print(shlex.join(command), flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
        assert before == dependencies(), 'source changed during compile'
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True, timeout=180)
        assert before == dependencies(), 'source changed during host test'
        if args.arm:
            devkit = Path(os.environ.get('DEVKITARM', '/opt/devkitpro/devkitARM'))
            libctru = Path(os.environ.get('CTRULIB', '/opt/devkitpro/libctru'))
            for name in ('units.cpp', 'patch_io.cpp'):
                command = [str(devkit / 'bin/arm-none-eabi-g++'), '-std=gnu++17', '-O2', '-Wall',
                           '-Wextra', '-Werror', '-fno-rtti', '-fno-exceptions', '-march=armv6k',
                           '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft', '-D__3DS__',
                           '-Icore', '-I' + str(libctru / 'include'), '-c', 'core/engine/' + name,
                           '-o', str(Path(directory) / (name + '.o'))]
                print(shlex.join(command), flush=True)
                subprocess.run(command, cwd=ROOT, check=True)
            assert before == dependencies(), 'source changed during ARM objects'
            print('pass two touched ARM objects, warning-clean; no app build/device run', flush=True)
    print('pass focused reverb core' + (' / ASAN+UBSAN+leaks' if args.sanitize else '') +
          '; no hardware/listening/performance validation', flush=True)


if __name__ == '__main__':
    main()
