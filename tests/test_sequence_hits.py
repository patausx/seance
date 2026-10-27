#!/usr/bin/env python3
"""Focused real-engine hit telemetry + source-exact prechange differential proof.

Fresh additive run dirs only; no ARM/make/UI/network/deploy. Baseline is the
archived, hash-bound complete prechange core (not git HEAD or a synthetic model).
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
OUT = ROOT / 'ui_mockups/playhead_press_runtime/core'
SOURCES = ['core/engine/engine.cpp', 'core/engine/units.cpp', 'core/engine/patch_io.cpp',
           'core/engine/workspace_layout.cpp', 'core/synth/sampler.cpp', 'core/synth/wav_loader.cpp', 'core/synth/wav_writer.cpp']
CHANGED = ['core/engine/engine.h', 'core/engine/engine.cpp']
WRAPS = ['malloc', 'calloc', 'realloc', '_Znwm', '_Znam', '_ZnwmRKSt9nothrow_t']
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def run(command, log, env=None):
    start = time.monotonic()
    with log.open('w') as f:
        f.write('$ ' + ' '.join(map(str, command)) + '\n'); f.flush()
        result = subprocess.run(list(map(str, command)), cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
    seconds = time.monotonic() - start
    print(f'{log.name}: exit={result.returncode} {seconds:.3f}s', flush=True)
    if result.returncode:
        print(log.read_text()[-14000:], flush=True)
        raise RuntimeError(f'failed: {log}')
    return seconds
def build(directory, name, base, flags):
    binary = directory / name
    run(['g++', '-std=gnu++17', '-Wall', '-Wextra', '-I' + str(base / 'core'), *flags,
         ROOT / 'tests/sequence_hit_test.cpp', *[base / x for x in SOURCES],
         *['-Wl,--wrap=' + s for s in WRAPS], '-o', binary], directory / (name + '-build.log'))
    return binary
def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    directory = OUT / ('run-' + stamp); directory.mkdir(parents=True, exist_ok=False)
    baseline = OUT / 'baseline'
    manifest = json.loads((baseline / 'manifest.json').read_text())
    for path, digest in manifest.items():
        assert sha(baseline / path) == digest, ('baseline changed', path)
        if path not in CHANGED:
            assert sha(ROOT / path) == digest, ('unrelated core changed', path)
    current = {str(p.relative_to(ROOT)): sha(p) for p in (ROOT / 'core').rglob('*') if p.is_file()}
    for path in CHANGED:
        (directory / (Path(path).name + '.diff')).write_text(''.join(difflib.unified_diff(
            (baseline / path).read_text().splitlines(True), (ROOT / path).read_text().splitlines(True),
            fromfile='before/' + path, tofile=path)))
    proof = {'started_utc': stamp, 'baseline_core': manifest, 'current_core': current,
             'harness': {str(p.relative_to(ROOT)): sha(p) for p in [ROOT / 'tests/sequence_hit_test.cpp', Path(__file__).resolve()]}}
    binary = build(directory, 'sequence-hit', ROOT, ['-O2'])
    optimized = directory / 'current'; optimized.mkdir()
    proof['optimized_seconds'] = run([binary, optimized], directory / 'focused.log')
    legacy = build(directory, 'sequence-hit-baseline', baseline, ['-O2', '-DSEQUENCE_HIT_BASELINE'])
    before = directory / 'baseline'; before.mkdir()
    proof['baseline_seconds'] = run([legacy, before], directory / 'baseline.log')
    proof['equivalence'] = {}
    for name in ['output.pcm', 'events.txt', 'legacy.snc', 'phase.snc']:
        assert (optimized / name).read_bytes() == (before / name).read_bytes(), ('prechange mismatch', name)
        proof['equivalence'][name] = {'bytes': (optimized / name).stat().st_size, 'sha256': sha(optimized / name)}
    if args.sanitize:
        sanitized = build(directory, 'sequence-hit-sanitize', ROOT,
                          ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])
        san = directory / 'sanitized'; san.mkdir()
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        proof['sanitized_seconds'] = run([sanitized, san], directory / 'sanitize.log', env)
        for name in proof['equivalence']:
            assert (san / name).read_bytes() == (optimized / name).read_bytes(), ('sanitized mismatch', name)
        proof['asan_ubsan_leaks'] = 'pass (focused safe-gain fixtures)'
    for label in ['focused', 'baseline']:
        text = (directory / (label + '.log')).read_text()
        values = re.search(r'sizeof Engine=(\d+) EngineUiSnapshot=(\d+) PendingSequenceEvent=(\d+)', text)
        proof[label + '_host_bytes'] = dict(zip(['Engine', 'EngineUiSnapshot', 'PendingSequenceEvent'], map(int, values.groups())))
    assert proof['focused_host_bytes']['EngineUiSnapshot'] <= 3840
    assert all(sha(ROOT / path) == digest for path, digest in current.items()), 'core changed during compilation/run'
    assert all(sha(ROOT / path) == digest for path, digest in proof['harness'].items()), 'harness changed during compilation/run'
    proof['evidence'] = {str(p.relative_to(directory)): sha(p) for p in directory.rglob('*') if p.is_file()}
    proof['semantics'] = ['actual accepted sequence Trig/retrig only; no RNG or scheduling changes',
        'event-bank/index captured from selected pending tuple, not active pattern at dispatch',
        'age is audio frames / 32; expiration returns age65535/step255/bank255/duration140',
        'duration min140ms and 85% trigger-time native interval divided by retrig count; minimum1ms',
        'generic/edit-bank snapshots preserve source identity; existing topology scan hides flow/empty/bypassed source',
        'lifecycle reset, disable and replace clear telemetry without affecting audio scheduling']
    proof['limitations'] = ['host resources are not ARM/GPU or hardware FPS measurements',
        'only latest recent event per chain; intermediate hits between UI publications are not queued',
        'phase-clock duration uses instantaneous trigger-time tempo; later tempo modulation cannot be predicted',
        'source topology is filtered when snapshot is captured, not additionally scanned on trigger',
        'no ARM, UI rendering, network, deployment, full-suite or listening acceptance']
    proof['result'] = 'pass'
    (directory / 'verification.json').write_text(json.dumps(proof, indent=2) + '\n')
    print((directory / 'focused.log').read_text().strip())
    print('prechange/new events + PCM + both SNC byte equality: pass')
    print('receipt:', directory.relative_to(ROOT) / 'verification.json')
if __name__ == '__main__':
    main()
