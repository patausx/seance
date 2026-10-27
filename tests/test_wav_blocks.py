#!/usr/bin/env python3
"""Only WAV block decoder/writer checks; no engine/UI suite, SD access or media exports."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm', action='store_true', help='compile only WAV IO objects with devkitARM')
    args = parser.parse_args()
    sources = ['core/synth/wav_loader.cpp', 'core/synth/wav_writer.cpp']
    flags = ['-O2']
    if args.sanitize:
        flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    env = os.environ.copy()
    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with tempfile.TemporaryDirectory(prefix='seance-wav-blocks-') as directory:
        tmp = Path(directory)
        binary = tmp / 'test'
        wrappers = ['_Znwm', 'fread', 'fwrite', 'fflush', 'fclose', 'rename']
        subprocess.run(['g++', '-std=gnu++17', *flags, '-Wall', '-Wextra', '-Werror',
                        '-fno-builtin-fwrite', '-fno-builtin-fread',
                        *['-Wl,--wrap=' + name for name in wrappers],
                        'tests/wav_blocks_test.cpp', *sources, '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary), str(tmp)], env=env, check=True, timeout=30)
        if args.arm:
            devkit = Path(os.environ.get('DEVKITARM', '/opt/devkitpro/devkitARM'))
            compiler = str(devkit / 'bin/arm-none-eabi-g++')
            for source in sources:
                subprocess.run([compiler, '-std=gnu++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                '-fno-rtti', '-fno-exceptions', '-march=armv6k', '-mtune=mpcore',
                                '-mfloat-abi=hard', '-mtp=soft', '-c', source,
                                '-o', str(tmp / (Path(source).stem + '.o'))], cwd=ROOT, check=True)
            print('pass ARM WAV IO objects: warning-clean (not full app or hardware storage test)', flush=True)


if __name__ == '__main__':
    main()
