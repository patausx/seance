#!/usr/bin/env python3
"""Isolated real-core REC dictaphone ASAN/UBSAN/leaks proof; no ARM/network.

--baseline captures six fixtures from the retained pre-edit core; current checks
compare raw engine/recorded PCM byte-for-byte. Finalized SNC may differ ONLY by
one existing SEXT v1 chunk marking newly recorded fixture slot16 exact-native;
all pre-existing bytes and all other slot flags must remain exact. Every
attempt retains copied inputs, inventories, compile/case logs and failures in
additive release/rec-dictaphone/core-* directories. Selected historical REC/GRAN
functions use their unchanged sources; omitted functions are explicitly recorded.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT / 'release/rec-dictaphone'
BACKUP = ROOT / '.local-state/backups/20261003_145814_before-rec-dictaphone_core'
BASELINE = RELEASE / 'core-pre-edit-baseline.json'
CASES = ['lifecycle', 'limits', 'engine-pcm', 'commands', 'legacy-queue', 'pending-stale',
         'ownership', 'occupied', 'stale-plan', 'restart', 'busy-save', 'interrupt', 'serialization',
         'native-endpoint']
TOOLS = ['rec_dictaphone_core_test.cpp', 'test_rec_dictaphone_core.py',
         'rec_wave_cache_test.cpp', 'gran_clean_core_test.cpp']
FLAGS = ['-O1', '-g', '-std=gnu++17', '-Wall', '-Wextra', '-Icore',
         '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity(path):
    return {'bytes': path.stat().st_size, 'sha256': sha(path)}


def inventory(paths, base):
    return {str(p.relative_to(base)): identity(p) for p in sorted(paths) if p.is_file()}


def dump(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def run(command, cwd, logfile, env=None):
    start = time.monotonic()
    with logfile.open('w') as f:
        f.write('$ ' + ' '.join(map(str, command)) + '\n'); f.flush()
        result = subprocess.run(command, cwd=cwd, stdout=f, stderr=subprocess.STDOUT, env=env)
    elapsed = time.monotonic() - start
    print(f'{logfile.name}: exit={result.returncode} elapsed={elapsed:.3f}s', flush=True)
    content = logfile.read_text()
    if result.returncode:
        print(content[-18000:], end='', flush=True)
    else:
        for line in content.splitlines():
            if line.startswith(('ok ', 'PASS ', 'fixture ')):
                print(line, flush=True)
    return {'exit': result.returncode, 'seconds': elapsed,
            'log': logfile.name, 'command': list(map(str, command))}


def new_rec_endpoint_snc(old, current):
    """Allow exactly one known existing-format opt-in, never blanket-normalize SNC.

    These six immutable fixtures contain only one REC using internal sample16 and
    no old endpoint flags. Require insertion immediately before their SAMP chunk;
    no other byte, chunk order, length or flag change is permitted.
    """
    if old[:5] != b'SNC7\x07':
        raise ValueError('unexpected baseline SNC header')
    position, samp = 5, None
    while position < len(old):
        if position + 8 > len(old):
            raise ValueError('truncated baseline chunk header')
        tag = old[position:position+4]
        length = int.from_bytes(old[position+4:position+8], 'little')
        end = position + 8 + length
        if end > len(old):
            raise ValueError('truncated baseline chunk')
        if tag == b'SEXT':
            raise ValueError('fixture baseline unexpectedly already opted in')
        if tag == b'SAMP':
            if samp is not None:
                raise ValueError('duplicate baseline SAMP')
            samp = position
        position = end
    if samp is None:
        raise ValueError('missing baseline SAMP')
    flags = bytes(1 if slot == 16 else 0 for slot in range(64))
    chunk = b'SEXT' + (66).to_bytes(4, 'little') + bytes((1, 64)) + flags
    required = old[:samp] + chunk + old[samp:]
    return current == required, {'insert_offset': samp, 'insert_bytes': len(chunk),
                                'chunk': 'SEXT', 'version': 1, 'slot_count': 64,
                                'sole_enabled_sample_slot': 16,
                                'all_preexisting_bytes_exact': current == required}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--case', action='append', choices=CASES, dest='cases')
    parser.add_argument('--no-regressions', action='store_true')
    args = parser.parse_args()
    if args.baseline and BASELINE.exists():
        raise SystemExit('frozen baseline receipt exists; refusing replacement')
    if not args.baseline and not BASELINE.exists():
        raise SystemExit('capture --baseline from retained pre-edit core first')
    expected = None
    if not args.baseline:
        expected = json.loads(BASELINE.read_text())
        prior = ROOT / expected['directory']
        if sha(prior / 'report.json') != expected['report_sha256']:
            raise SystemExit('frozen baseline receipt changed')
        if inventory((prior / 'compatibility').iterdir(), prior / 'compatibility') != expected['fixtures']:
            raise SystemExit('frozen baseline outputs changed')
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out = RELEASE / (('core-pre-edit-' if args.baseline else 'core-check-') + stamp)
    inputs = out / 'inputs'; inputs.mkdir(parents=True, exist_ok=False)
    core = BACKUP if args.baseline else ROOT / 'core'
    sources = {'core/' + str(p.relative_to(core)): p for p in core.rglob('*') if p.is_file()}
    sources.update({'tools/' + name: ROOT / 'tools' / name for name in TOOLS})
    before = {name: identity(path) for name, path in sorted(sources.items())}
    shutil.copytree(core, inputs / 'core'); (inputs / 'tools').mkdir()
    for name in TOOLS:
        shutil.copy2(ROOT / 'tools' / name, inputs / 'tools' / name)
    copied = inventory(inputs.rglob('*'), inputs)
    report = {
        'kind': 'pre-edit-compatibility' if args.baseline else 'focused-rec-dictaphone-real-core',
        'created_utc': stamp, 'core_source': str(core.relative_to(ROOT)),
        'inputs': copied, 'snapshot_matches_source_at_start': copied == before,
        'asan': True, 'ubsan': True, 'leaks': True, 'broad_suites': False,
        'platform_io': False, 'arm': False, 'deployment': False,
        'cases': {}, 'compiles': [], 'passed': False,
        'limitations': [
            'host state/PCM/serialization/ownership proof, not hardware microphone, listening, FPS or SD behavior',
            'recording_active_or_pending and core preview guards do not establish platform export/crop/controller wiring',
            'unchanged rec-wave selection excludes dual_engine, whose last assertion saves an active take under the old policy',
            'unchanged gran-clean selection is five of six functions; persistence_layers excluded because it writes hardcoded /tmp paths',
            'no allocator failure injection: malformed staged-vector and bank-budget failures exercise real preflight paths',
        ],
    }
    dump(out / 'report.json', report)
    result = 0
    env = os.environ.copy()
    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')

    def compile_named(name, command):
        status = run(command, inputs, out / ('compile-' + name + '.log'))
        report['compiles'].append(status); dump(out / 'report.json', report)
        return status['exit']

    def run_named(name, command):
        status = run(command, inputs, out / ('case-' + name + '.log'), env)
        report['cases'][name] = status; dump(out / 'report.json', report)
        return status['exit']

    try:
        if copied != before:
            raise RuntimeError('sources changed while snapshotting; inconsistent attempt retained, not compiled')
        objects = out / 'objects'; objects.mkdir()
        cpp = sorted((inputs / 'core/engine').glob('*.cpp'))
        cpp += [inputs / ('core/synth/' + name) for name in ['sampler.cpp', 'wav_loader.cpp', 'wav_writer.cpp']]
        object_paths = []
        for source in cpp:
            relative = source.relative_to(inputs)
            obj = objects / (str(relative).replace('/', '_') + '.o')
            cmd = ['g++', *FLAGS, '-MMD', '-MF', str(obj.with_suffix('.d')), '-c', str(relative), '-o', str(obj)]
            result = compile_named(source.stem, cmd)
            if result: break
            object_paths.append(str(obj))
        if result == 0:
            compat = out / 'rec-compatibility'; fixture_dir = out / 'compatibility'; fixture_dir.mkdir()
            cmd = ['g++', *FLAGS, '-DREC_DICTAPHONE_COMPATIBILITY', 'tools/rec_dictaphone_core_test.cpp',
                   *object_paths, '-o', str(compat)]
            result = compile_named('compatibility', cmd)
            if result == 0:
                result = run_named('compatibility', [str(compat), str(fixture_dir)])
                report['fixtures'] = inventory(fixture_dir.iterdir(), fixture_dir)
                if expected is not None:
                    report['frozen_exact'] = report['fixtures'] == expected['fixtures']
                    names = set(report['fixtures'])
                    if names != set(expected['fixtures']):
                        raise ValueError('fixture set changed')
                    pcm = sorted(name for name in names if name.endswith('.pcm'))
                    snc = sorted(name for name in names if name.endswith('.snc'))
                    if len(pcm) != 12 or len(snc) != 6 or len(names) != 18:
                        raise ValueError('unexpected compatibility fixture inventory')
                    report['frozen_pcm_exact'] = all(report['fixtures'][name] == expected['fixtures'][name] for name in pcm)
                    report['new_rec_endpoint_snc_deltas'] = {}
                    all_snc = True
                    for name in snc:
                        old = (prior / 'compatibility' / name).read_bytes()
                        current = (fixture_dir / name).read_bytes()
                        permitted, delta = new_rec_endpoint_snc(old, current)
                        # Comparison guard must reject a bit flip outside the newly
                        # permitted chunk and a different sample's endpoint flag.
                        bad = bytearray(current); bad[-1] ^= 1
                        assert not new_rec_endpoint_snc(old, bytes(bad))[0]
                        bad = bytearray(current)
                        if permitted:
                            bad[delta['insert_offset'] + 10 + 17] = 1
                            assert not new_rec_endpoint_snc(old, bytes(bad))[0]
                        delta['permitted'] = permitted
                        report['new_rec_endpoint_snc_deltas'][name] = delta
                        all_snc = all_snc and permitted
                    report['frozen_snc_existing_bytes_exact'] = all_snc
                    report['frozen_regression_passed'] = report['frozen_pcm_exact'] and all_snc
                    if not report['frozen_regression_passed']:
                        result = 1; print('FAIL: PCM differs or SNC differs beyond exact new-REC endpoint opt-in', flush=True)
            if not args.baseline:
                binary = out / 'rec-dictaphone-core'
                cmd = ['g++', *FLAGS, 'tools/rec_dictaphone_core_test.cpp', *object_paths, '-o', str(binary)]
                focused = compile_named('focused', cmd)
                if focused: result = 1
                else:
                    for name in args.cases or CASES:
                        if run_named(name, [str(binary), str(out), name]): result = 1
                if not args.no_regressions:
                    # Only units needs the existing read-counter instrumentation.
                    wave_units = objects / 'units-wave.o'
                    cmd = ['g++', *FLAGS, '-DSEANCE_REC_WAVE_CACHE_TEST', '-c', 'core/engine/units.cpp', '-o', str(wave_units)]
                    wave_compile = compile_named('wave-units', cmd)
                    if wave_compile: result = 1
                    else:
                        wave_objs = [str(wave_units) if p.endswith('core_engine_units.cpp.o') else p for p in object_paths]
                        regressions = [
                            ('rec-wave', 'REC_DICTAPHONE_WAVE_REGRESSION', wave_objs,
                             ['-DSEANCE_REC_WAVE_CACHE_TEST', '-Wl,--wrap=_Znwm', '-Wl,--wrap=_Znam', '-Wl,--wrap=_ZnwmRKSt9nothrow_t']),
                            ('gran-clean', 'REC_DICTAPHONE_GRAN_REGRESSION', object_paths, []),
                        ]
                        for name, define, objs, extra in regressions:
                            binary = out / name
                            cmd = ['g++', *FLAGS, '-D' + define, *extra, 'tools/rec_dictaphone_core_test.cpp', *objs, '-o', str(binary)]
                            if compile_named(name, cmd): result = 1; continue
                            fixture_dir = out / name.replace('-', '_'); fixture_dir.mkdir()
                            if run_named('regression-' + name, [str(binary), str(fixture_dir)]): result = 1
    except Exception as error:
        report['error'] = repr(error); result = 1; print(report['error'], flush=True)
    report['retained_inputs_unchanged'] = inventory(inputs.rglob('*'), inputs) == copied
    source_after = {name: identity(path) for name, path in sorted(sources.items())}
    report['source_inputs_unchanged'] = source_after == copied
    report['source_changed_paths'] = sorted(key for key in copied if source_after.get(key) != copied[key])
    if not report['retained_inputs_unchanged']: result = 1
    report['tested_copy_passed'] = result == 0
    report['passed'] = result == 0 and report['source_inputs_unchanged']
    report['artifacts'] = inventory([p for p in out.rglob('*') if p.is_file()
                                     and inputs not in p.parents and p.name != 'report.json'], out)
    report['exit'] = result if result else (0 if report['passed'] else 2)
    dump(out / 'report.json', report)
    if args.baseline and report['passed']:
        dump(BASELINE, {'directory': str(out.relative_to(ROOT)), 'report_sha256': sha(out / 'report.json'),
                        'fixtures': report['fixtures']})
    print('retained:', out.relative_to(ROOT), flush=True)
    print('tested_copy_passed:', report['tested_copy_passed'],
          'source_inputs_unchanged:', report['source_inputs_unchanged'], flush=True)
    return report['exit']


if __name__ == '__main__':
    sys.exit(main())
