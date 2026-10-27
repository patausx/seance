#!/usr/bin/env python3
"""Focused REC cache proof only. Additive per-run artifacts; no make/ARM/network.

The archived pre-fix 3 core files under the receipt baseline are compiled as a
second actual engine for PCM/serialization equality. Other baseline core files
are symlinks to unchanged sources, protected by the baseline manifest.
"""
from pathlib import Path
import argparse
import datetime
import difflib
import hashlib
import json
import os
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/resource_cleanup_runtime/rec'
SOURCES = ['core/engine/engine.cpp', 'core/engine/units.cpp',
           'core/engine/patch_io.cpp', 'core/engine/workspace_layout.cpp',
           'core/synth/sampler.cpp',
           'core/synth/wav_loader.cpp', 'core/synth/wav_writer.cpp']
WRAPS = ['_Znwm', '_Znam', '_ZnwmRKSt9nothrow_t']
CHANGED = ['core/engine/engine.cpp', 'core/engine/units.cpp', 'core/engine/units.h']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, logfile, env=None):
    started = time.monotonic()
    with logfile.open('w') as f:
        f.write('$ ' + ' '.join(map(str, command)) + '\n')
        f.flush()
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env,
                                stdout=f, stderr=subprocess.STDOUT)
    elapsed = time.monotonic() - started
    print(f'{logfile.name}: exit={result.returncode} {elapsed:.3f}s', flush=True)
    if result.returncode:
        print(logfile.read_text()[-12000:], flush=True)
        raise RuntimeError(f'failed: {logfile}')
    return elapsed


def preservation(run_dir):
    baseline = OUT / 'baseline'
    manifest = json.loads((baseline / 'manifest.json').read_text())
    for path, expected in manifest.items():
        assert sha(baseline / path) == expected, ('baseline changed', path)
        if path not in CHANGED:
            assert sha(ROOT / path) == expected, ('unrelated core changed', path)
    for path in CHANGED:
        old = (baseline / path).read_text()
        new = (ROOT / path).read_text()
        diff = ''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                                          fromfile='before/' + path, tofile=path))
        (run_dir / (Path(path).name + '.diff')).write_text(diff)
        if path != 'core/engine/engine.cpp':
            # Every old line remains in order; additions confined to REC.
            a, b = old.splitlines(True), new.splitlines(True)
            changes = list(difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes())
            assert all(tag in ('equal', 'insert') for tag, *_ in changes), path
            for tag, i, j, k, l in changes:
                if tag == 'insert':
                    if path.endswith('.cpp'):
                        assert old.index('// ============================ recorder') <= len(''.join(a[:i])) <= old.index('// ============================ factory')
                    else:
                        assert old.index('class RecUnit') <= len(''.join(a[:i])) <= old.index('class BitUnit')
        else:
            start = '                const auto& sample = trackr::synth::SampleBank::instance().slot(rec->sample_slot);'
            end = '\n            }\n            if (rec->is_recording())'
            old_start = old.index(start)
            old_end = old.index(end, old_start)
            new_start = new.index('                static_assert(EngineUiSnapshot::REC_WAVEFORM_POINTS == RecUnit::WAVE_POINTS,')
            new_end = new.index(end, new_start)
            assert old[:old_start] == new[:new_start] and old[old_end:] == new[new_end:]
    ui_path = ROOT / 'platform/3ds/ui_module_focus_recorder.cpp'
    ui = ui_path.read_text()
    draw = ui[ui.index('void recorder_focus_draw('):]
    assert 'SampleBank::' not in draw and 'copy_visual_wave' not in draw
    assert 'const auto& s=g_ui_snapshot;' in ui
    return {'old_core_files': manifest,
            'current_core_files': {str(p.relative_to(ROOT)): sha(p) for p in (ROOT / 'core').rglob('*') if p.is_file()},
            'read_only_rec_draw_unchanged_sha256': sha(ui_path),
            'audio_out_unchanged_sha256': sha(ROOT / 'platform/3ds/audio_out.cpp')}


def build(run_dir, name, base, flags):
    binary = run_dir / name
    cmd = ['g++', '-std=gnu++17', '-Wall', '-Wextra', '-I' + str(base / 'core'),
           *flags, ROOT / 'tools/rec_wave_cache_test.cpp',
           *[base / path for path in SOURCES],
           *['-Wl,--wrap=' + symbol for symbol in WRAPS], '-o', binary]
    run(cmd, run_dir / (name + '-build.log'))
    return binary


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true', help='also run ASan/UBSan/leaks')
    args = parser.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    run_dir = OUT / ('run-' + stamp)
    run_dir.mkdir(parents=True, exist_ok=False)
    proof = preservation(run_dir)
    proof['started_utc'] = stamp
    proof['harness'] = {name: sha(ROOT / name) for name in ('tools/test_rec_wave_cache.py', 'tools/rec_wave_cache_test.cpp')}
    binary = build(run_dir, 'rec-wave-cache', ROOT, ['-O2', '-DSEANCE_REC_WAVE_CACHE_TEST'])
    current = run_dir / 'current'
    current.mkdir()
    run([binary, current], run_dir / 'focused.log')
    legacy = build(run_dir, 'rec-wave-legacy', OUT / 'baseline', ['-O2', '-DSEANCE_REC_WAVE_LEGACY'])
    before = run_dir / 'legacy'
    before.mkdir()
    run([legacy, before, '--equivalence'], run_dir / 'legacy.log')
    equivalence = {}
    for name in ('engine-output.pcm', 'recorded-take.pcm', 'engine.snc'):
        assert (before / name).read_bytes() == (current / name).read_bytes(), ('legacy mismatch', name)
        equivalence[name] = {'bytes': (current / name).stat().st_size, 'sha256': sha(current / name)}
    proof['legacy_equivalence'] = equivalence
    focused = (run_dir / 'focused.log').read_text()
    sizes = re.search(r'sizeof RecUnit=(\d+) Unit=(\d+)', focused)
    old_sizes = re.search(r'sizeof RecUnit=(\d+) Unit=(\d+)', (run_dir / 'legacy.log').read_text())
    proof['host_rec_bytes'] = {'old': int(old_sizes[1]), 'new': int(sizes[1]),
                               'increase': int(sizes[1]) - int(old_sizes[1])}
    if args.sanitize:
        sanitized = build(run_dir, 'rec-wave-sanitize', ROOT,
                          ['-O1', '-g', '-DSEANCE_REC_WAVE_CACHE_TEST',
                           '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
        san_dir = run_dir / 'sanitized'
        san_dir.mkdir()
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        run([sanitized, san_dir], run_dir / 'sanitize.log', env)
        for name in equivalence:
            assert (san_dir / name).read_bytes() == (current / name).read_bytes()
        proof['asan_ubsan_leaks'] = 'pass (focused safe master-drive fixture; not unrelated DSP safety)'
    else:
        proof['asan_ubsan_leaks'] = 'not requested'
    proof['work_bounds'] = {'block_samples': 256, 'peak_storage_bytes': 5000,
                            'streamed_changed_snapshot_max_pcm': 32640,
                            'streamed_changed_snapshot_max_summaries': 2500,
                            'restore_snapshot_max_pcm': 32768,
                            'unchanged_ready_snapshot_pcm_and_summary_reads': 0,
                            'restore_640k_frames_publications': 20,
                            'allocation_free_guarded_operations': True}
    proof['limitations'] = ['restored/adopted/replaced takes show partial signed peaks until bounded reduction completes',
                            'arbitrary unannounced in-place PCM writes require adopt_take (normal REC writes are tracked)',
                            'no ARM, device budget measurement, listening, UI rerender, full suite, network or deployment']
    proof['result'] = 'pass'
    (run_dir / 'verification.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(focused.strip())
    print('legacy PCM + recording + SNC byte equality: pass')
    print('receipt:', run_dir.relative_to(ROOT) / 'verification.json')


if __name__ == '__main__':
    main()
