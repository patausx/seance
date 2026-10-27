#!/usr/bin/env python3
"""Focused menu audio player/service tests, asset bounds and integration guards. No media exports."""
import argparse
import array
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/menu_audio'


def check_assets():
    manifest = json.loads((ASSETS / 'manifest.json').read_text())
    assert (manifest['sample_rate'], manifest['channels'], manifest['encoding']) == (32000, 2, 's16le')
    for name, digest in manifest['source_sha256'].items():
        assert hashlib.sha256((ASSETS / 'source' / name).read_bytes()).hexdigest() == digest
    for clip in manifest['outputs']:
        raw = (ASSETS / clip['file']).read_bytes()
        assert len(raw) == clip['bytes'] == clip['frames'] * 4
        assert hashlib.sha256(raw).hexdigest() == clip['sha256']
        pcm = array.array('h', raw)
        if sys.byteorder != 'little':
            pcm.byteswap()
        assert max(abs(v) for v in pcm) < 30000
        if clip['file'] == 'menu_drone.bin':
            assert clip['frames'] == 26 * 32000
            for c in range(2):
                # The wrap is a normal adjacent source step, not a forced zero or
                # a new attack. This is numeric continuity, not listening proof.
                assert abs(pcm[c] - pcm[-2+c]) < 1024
                assert any(pcm[c::2])
        else:
            assert 3 * 32000 < clip['frames'] < 4 * 32000
            assert pcm[:2] == pcm[-2:] == array.array('h', [0, 0])
    print('pass menu assets: hashes, stereo sizes, 26s overlap loop and tapered 3.4s entry', flush=True)


def check_integration():
    source = (ROOT / 'platform/3ds/main.cpp').read_text()
    prefs = source.split('static bool save_start_preferences(')[1].split('\n}\n', 1)[0]
    assert prefs.index('if (!start_store_save(') < prefs.index('g_menu_audio->set_enabled(')
    assert 'out.menu_sound = g_start_store.menu_sound;' in source
    assert 'proposed.menu_sound = settings.menu_sound;' in source
    tx = source.split('bool execute_destructive(')[1].split('\nvoid request_destructive', 1)[0]
    assert tx.count('if (g_session_started && g_menu_audio) g_menu_audio->enter_patch();') == 2
    load = tx.split('if (pending.action == DestructiveAction::LoadProject)')[1]
    assert load.index('if (ok) {') < load.index('g_menu_audio->enter_patch();') < load.index('patch_browser_result(ok,')
    main = source.split('int main() {')[1]
    assert main.index('menu_audio.init(') < main.index('startup_choose(top, bot)') < main.index('audio.init(*g_eng)')
    # Current startup returns on cancellation instead of the retired retry loop.
    failure = main.split('if (!project_ready) {\n        menu_audio.shutdown();', 1)[1]
    failure = failure.split('// Loaded transport/sequencer state', 1)[0]
    assert 'return 0;' in failure and 'menu_audio.enter_patch();' not in failure
    assert main.index('g_eng->transport_stop();') < main.index('audio.init(*g_eng)')
    assert main.index('audio.init(*g_eng)') < main.index('g_session_started = true;') < main.index('menu_audio.enter_patch();')
    assert main.count('menu_audio.enter_patch();') == 1
    assert source.count('g_menu_audio->open_menu();') == 1
    assert 'menu_audio.set_patch_audible(patch_audible);' in main
    backend = (ROOT / 'platform/3ds/menu_audio.cpp').read_text()
    assert 'ndspSetCallback(' not in backend and 'engine_->' not in backend
    assert 'fopen(' not in backend and 'SampleBank' not in backend
    print('pass menu integration guards: success-only entry, commit-before-sound, engine isolation', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    check_assets()
    check_integration()
    flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie'] if args.sanitize else ['-O2']
    with tempfile.TemporaryDirectory(prefix='seance-menu-audio-') as tmp:
        tmp = Path(tmp)
        assembly = ['.section .rodata']
        for name in ('menu_drone', 'menu_entry'):
            pcm = ASSETS / (name + '.bin')
            symbol = name + '_bin'
            assembly += ['.balign 4', f'.global {symbol}', f'{symbol}:', f'.incbin "{pcm}"',
                         f'.global {symbol}_end', f'{symbol}_end:']
            # Match current devkitPro bin2s: size lives in the generated header,
            # only start/end are linker symbols. Do not fabricate *_size objects.
            (tmp / (symbol + '.h')).write_text(
                '#pragma once\n#include <stddef.h>\n#include <stdint.h>\n'
                f'extern const uint8_t {symbol}[];\nextern const uint8_t {symbol}_end[];\n'
                f'static constexpr size_t {symbol}_size={pcm.stat().st_size};\n')
        assembly += ['.section .note.GNU-stack,"",@progbits']
        asm = tmp / 'menu_assets.s'
        asm.write_text('\n'.join(assembly) + '\n')
        binary = tmp / 'test'
        subprocess.run(['g++', '-std=c++17', *flags, '-Wall', '-Wextra', '-Werror', '-pthread',
            '-Itests/menu_audio_test_stubs', '-I'+str(tmp), 'tests/menu_audio_test.cpp', 'platform/3ds/menu_audio.cpp',
            str(asm), '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=20)


if __name__ == '__main__':
    main()
