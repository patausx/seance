#!/usr/bin/env python3
"""Focused production audio/UI mailbox tests; optional ARM object check, no app build/media."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def integration():
    source = (ROOT / 'platform/3ds/audio_out.cpp').read_text()
    assert 'return ui_snapshots_.read_latest(out);' in source
    assert 'ui_snapshot_seq_' not in source and 'memory_barrier' not in source
    init = source.split('bool AudioOut::init(')[1].split('void AudioOut::shutdown(')[0]
    assert init.index('reset_quiescent();') < init.index('capture_ui_snapshot(') < init.index('ui_snapshots_.publish();')
    assert init.index('ui_snapshots_.publish();') < init.index('ndspInit()') < init.index('threadCreate(')
    worker = source.split('void AudioOut::worker_loop()')[1]
    assert worker.index('producer_buffer()') < worker.index('capture_ui_snapshot(snapshot,')
    assert worker.index('capture_ui_snapshot(snapshot,') < worker.index('snapshot.xrun_count =') < worker.index('ui_snapshots_.publish();')
    assert worker.index('ui_snapshots_.publish();') < worker.index('DSP_FlushDataCache(')
    print('pass snapshot integration guards: initial publish before worker/failure, telemetry before ownership transfer', flush=True)


def arm_check(tmp):
    devkit = Path(os.environ.get('DEVKITARM', '/opt/devkitpro/devkitARM'))
    libctru = Path(os.environ.get('CTRULIB', '/opt/devkitpro/libctru'))
    compiler = str(devkit / 'bin/arm-none-eabi-g++')
    flags = ['-std=gnu++17', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-rtti', '-fno-exceptions',
             '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft', '-D__3DS__',
             '-Icore', '-Iplatform/3ds', '-I' + str(libctru / 'include')]
    obj = tmp / 'audio_out.o'
    subprocess.run([compiler, *flags, '-c', 'platform/3ds/audio_out.cpp', '-o', str(obj)], cwd=ROOT, check=True)
    symbols = subprocess.check_output([str(devkit / 'bin/arm-none-eabi-nm'), '-u', str(obj)], text=True)
    assert '__atomic_' not in symbols and '__sync_' not in symbols, symbols
    disassembly = subprocess.check_output([str(devkit / 'bin/arm-none-eabi-objdump'), '-d', str(obj)], text=True)
    assert 'ldrex' in disassembly and 'strex' in disassembly
    print('pass ARM audio_out object: warning-clean, native ldrex/strex, no atomic helper/lock fallback (not app/device test)', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', choices=['address', 'thread'])
    parser.add_argument('--arm', action='store_true', help='compile only the real audio backend object with devkitARM')
    args = parser.parse_args()
    integration()
    flags = ['-O2']
    env = os.environ.copy()
    if args.sanitize:
        sanitizer = 'address,undefined' if args.sanitize == 'address' else 'thread'
        flags = ['-O1', '-g', '-fsanitize=' + sanitizer, '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
        env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1', TSAN_OPTIONS='halt_on_error=1')
    with tempfile.TemporaryDirectory(prefix='seance-audio-snapshot-') as directory:
        tmp = Path(directory)
        binary = tmp / 'test'
        subprocess.run(['g++', '-std=gnu++17', *flags, '-Wall', '-Wextra', '-Werror', '-pthread',
                        '-fno-builtin-memcpy', '-Wl,--wrap=memcpy', 'tests/audio_snapshot_test.cpp',
                        '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], env=env, check=True, timeout=30)
        if args.arm:
            arm_check(tmp)


if __name__ == '__main__':
    main()
