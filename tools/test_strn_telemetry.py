#!/usr/bin/env python3
"""Only STRN telemetry: pinned old process vs real new process, optional one ARM object.
Temporary copies add operation counters; production has no instrumentation/test hooks.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def position_counter(source, name):
    result, count = re.subn(r'(visual_positions_\[gi\]\s*=\s*)(.*?);',
                           lambda m: m[1] + '(++strn_work::' + name + ', ' + m[2] + ');',
                           source, flags=re.S)
    assert count == 1
    return result


def generate(tmp):
    source = (ROOT / 'core/engine/units.cpp').read_text()
    header = (ROOT / 'core/engine/units.h').read_text()
    original = (ROOT / 'tools/strn_telemetry_reference.inc').read_text()
    original = function(original, 'void StranularUnit::process(')
    current = function(source, 'void StranularUnit::process(')
    wave = function(source, 'const q15* StranularUnit::visual_wave(')
    # Guard the actual audio/pitch/window/wrap operations, not just example PCM.
    def section(text, start, end):
        a = text.index(start)
        return text[a:text.index(end, a)]
    assert section(original, '    if (!buffer_)', '    for (int i =') == section(
        current, '    if (!buffer_)', '    // Retain the last visible')
    old_grain = section(original, '            uint32_t index = grain_index', '        q15 wet =')
    assert old_grain.replace('            visual_increments_[gi] = g.inc_q16;\n', '') == section(
        current, '            uint32_t index = grain_index', '        q15 wet =')
    assert section(original, '        q15 wet =', '        if (++visual_decim_') == section(
        current, '        q15 wet =', '        if (++visual_decim_')
    counters = ('#include <cstdint>\nnamespace strn_work { extern uint64_t current_positions, '
                'current_wave, legacy_positions, legacy_wave; }\n')
    instrumented = source.replace(current, position_counter(current, 'current_positions'))
    instrumented = instrumented.replace(wave, wave.replace(
        '    if (!visual_wave_dirty_) return visual_wave_;',
        '    if (!visual_wave_dirty_) return visual_wave_;\n    ++strn_work::current_wave;'))
    (tmp / 'units_counted.cpp').write_text(counters + instrumented)
    cls_start = header.index('class StranularUnit :')
    cls_end = header.index('\n};', cls_start) + len('\n};')
    cls = header[cls_start:cls_end].replace('StranularUnit', 'StrnReference')
    (tmp / 'strn_reference.h').write_text('#pragma once\n#include "engine/units.h"\nnamespace seance {\n' + cls + '\n}\n')
    strn_start = source.index('StranularUnit::StranularUnit()')
    strn_end = source.index('// ============================ SHIFT', strn_start)
    methods = source[strn_start:strn_end]
    original = position_counter(original, 'legacy_positions').replace(
        '            visual_decim_ = 0;', '            visual_decim_ = 0;\n            ++strn_work::legacy_wave;')
    methods = methods.replace(current, original).replace(wave,
        'const q15* StranularUnit::visual_wave() const { return visual_wave_; }')
    methods = methods.replace('StranularUnit', 'StrnReference')
    # Unchanged helpers get their own internal linkage, avoiding mocks or new APIs.
    pitch = source[source.index('static const int32_t* pitch_lut()'):source.index('static uint32_t playable_frames(')]
    clock = function(source, 'static void rescale_clock_countdown(')
    prefix = ('#include "strn_reference.h"\n#include <algorithm>\n#include <cstring>\n'
              '#include <cmath>\n#include <new>\nnamespace seance {\n'
              'using trackr::fx::Q15_ONE; using trackr::fx::lerp_q15; using trackr::fx::sat_q15;\n')
    (tmp / 'strn_reference.cpp').write_text(counters + prefix + pitch + clock + '\n' + methods + '\n}\n')
    print('pass STRN source guards: audio/pitch/window/frozen-wrap operations unchanged', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--arm', action='store_true')
    args = parser.parse_args()
    flags = ['-O2'] if not args.sanitize else ['-O1', '-g', '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    env = os.environ.copy()
    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with tempfile.TemporaryDirectory(prefix='seance-strn-telemetry-') as directory:
        tmp = Path(directory)
        generate(tmp)
        binary = tmp / 'test'
        subprocess.run(['g++', '-std=gnu++17', *flags, '-Wall', '-Wextra', '-fno-rtti',
                        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                        '-Icore', '-Icore/engine', '-I' + str(tmp),
                        '-Wl,--wrap=_Znwm', '-Wl,--wrap=_Znam', '-Wl,--wrap=_ZnamRKSt9nothrow_t',
                        'tools/strn_telemetry_test.cpp', str(tmp / 'units_counted.cpp'),
                        str(tmp / 'strn_reference.cpp'), '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], env=env, check=True, timeout=30)
        if args.arm:
            devkit = Path(os.environ.get('DEVKITARM', '/opt/devkitpro/devkitARM'))
            libctru = Path(os.environ.get('CTRULIB', '/opt/devkitpro/libctru'))
            subprocess.run([str(devkit / 'bin/arm-none-eabi-g++'), '-std=gnu++17', '-O2', '-Wall',
                            '-Wextra', '-Werror', '-fno-rtti', '-fno-exceptions', '-march=armv6k',
                            '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft', '-D__3DS__',
                            '-Icore', '-I' + str(libctru / 'include'), '-c', 'core/engine/units.cpp',
                            '-o', str(tmp / 'units.o')], cwd=ROOT, check=True)
            print('pass ARM original units.cpp object: warning-clean, no app build/device run', flush=True)


if __name__ == '__main__':
    main()
