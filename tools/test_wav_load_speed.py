#!/usr/bin/env python3
"""Paired frozen/current WAV loader proof, scoped temporary fixtures and receipts.

Default/--sanitize checks correctness, PCM/metadata, workspace and fault atomicity.
--benchmark is separate: at most five repeats of warm host filesystem wall time;
neither fread counters nor timings are hardware/SD/FS-service measurements.
--arm compiles ONLY the production decoder object, never an application/deployment.
"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = 'tools/wav_load_reference.inc'
REFERENCE_SHA256 = '5554db9eacce3a0fd6fb009ec61731e9e4d2223334898ab8e130a4421be60773'
SEEDS = ['tools/test_wav_load_speed.py', 'tools/wav_load_speed_test.cpp', REFERENCE,
         'core/synth/wav_loader.cpp', 'core/synth/wav_loader.h']
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inventory():
    """Resolve only this test's quoted local includes before copying a snapshot."""
    pending, result = list(SEEDS), {}
    while pending:
        name = pending.pop()
        if name in result:
            continue
        source = ROOT / name
        data = source.read_bytes()
        result[name] = {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}
        if source.suffix not in ('.cpp', '.h', '.inc'):
            continue
        for include in INCLUDE.findall(data.decode('utf-8')):
            choices = [source.parent / include, ROOT / 'core/synth' / include]
            resolved = next((p.resolve() for p in choices if p.is_file()), None)
            if resolved is None:
                raise RuntimeError(f'unresolved local include {name}: {include}')
            pending.append(str(resolved.relative_to(ROOT)))
    if result[REFERENCE]['sha256'] != REFERENCE_SHA256:
        raise RuntimeError('frozen reference does not match the approved original SHA256')
    return dict(sorted(result.items()))


def verify(inputs, directory):
    for name, metadata in inputs.items():
        path = directory / name
        if not path.is_file() or sha(path) != metadata['sha256']:
            raise RuntimeError(f'input changed or missing: {path}')


def dependency_paths(path, snapshot):
    text = path.read_text().replace('\\\n', ' ')
    files = shlex.split(text.split(':', 1)[1])
    result = []
    for name in files:
        resolved = (snapshot / name).resolve() if not Path(name).is_absolute() else Path(name).resolve()
        result.append(str(resolved.relative_to(snapshot.resolve())))
    return sorted(set(result))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--repeats', type=int, default=5, help='benchmark samples, 1..5 (default 5)')
    parser.add_argument('--arm', action='store_true', help='compile production WAV decoder object only')
    args = parser.parse_args()
    if args.sanitize and args.benchmark:
        parser.error('--benchmark and --sanitize are separate runs; sanitized timings are not collected')
    if not 1 <= args.repeats <= 5:
        parser.error('--repeats must be between 1 and 5')

    started = dt.datetime.now(dt.timezone.utc)
    prefix = 'benchmark' if args.benchmark else 'check'
    stamp = started.strftime('%Y%m%dT%H%M%S_%fZ')
    output_root = ROOT / 'release/sample-load-speed'
    run = output_root / f'{prefix}-{stamp}'
    run.mkdir(parents=True, exist_ok=False)
    snapshot, build = run / 'inputs', run / 'build'
    snapshot.mkdir()
    build.mkdir()
    log_path, receipt_path = run / 'run.log', run / 'receipt.json'
    results_path = run / 'results.json'
    receipt = {
        'schema': 'seance-wav-load-speed-proof-v1', 'started_utc': started.isoformat(),
        'status': 'running', 'mode': 'host-benchmark-only' if args.benchmark else 'paired-correctness',
        'sanitize': args.sanitize, 'arm_object_requested': args.arm,
        'app_build': False, 'deployment': False, 'hardware_tested': False,
        'user_audio_used': False, 'engine_ui_suites': False,
        'boundary': 'synthetic temporary WAV only; fread is stdio, not device/FS calls; timings are warm host wall time only',
        'reference_sha256': REFERENCE_SHA256, 'commands': [],
        'inputs': {}, 'compiler_dependencies': [], 'artifacts': {},
    }
    failure = None
    monotonic_start = time.monotonic()
    with log_path.open('w') as log:
        def emit(text):
            print(text, flush=True)
            log.write(text + '\n')
            log.flush()

        def command(argv, cwd, timeout=180, env=None):
            emit('$ ' + shlex.join(str(x) for x in argv))
            begin = time.monotonic()
            completed = subprocess.run([str(x) for x in argv], cwd=cwd, env=env,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True, timeout=timeout, check=False)
            entry = {'argv': [str(x) for x in argv], 'cwd': str(cwd),
                     'seconds': round(time.monotonic() - begin, 6), 'exit': completed.returncode}
            receipt['commands'].append(entry)
            if completed.stdout:
                emit(completed.stdout.rstrip())
            if completed.returncode:
                raise RuntimeError(f'command failed ({completed.returncode}): {shlex.join(entry["argv"])}')
            return completed.stdout

        try:
            inputs = inventory()
            receipt['inputs'] = inputs
            for name in inputs:
                destination = snapshot / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, destination)
            verify(inputs, ROOT)
            verify(inputs, snapshot)
            emit(f'pinned frozen reference {REFERENCE_SHA256}; {len(inputs)} source inputs snapshotted')

            compiler = shutil.which('g++')
            if not compiler:
                raise RuntimeError('g++ not found')
            compiler = Path(compiler).resolve()
            receipt['host_compiler'] = {'path': str(compiler), 'sha256': sha(compiler),
                                        'version': command([compiler, '--version'], snapshot).strip()}
            common = ['-std=gnu++17', '-Wall', '-Wextra', '-Werror', '-fno-builtin-fread',
                      '-fno-builtin-fseek', '-Icore/synth']
            optimization = ['-O2']
            if args.sanitize:
                optimization = ['-O1', '-g', '-fsanitize=address,undefined',
                                '-fno-omit-frame-pointer', '-fno-pie']
            units = [('reference', REFERENCE, ['-x', 'c++',
                      '-Dload_wav_to_sample=reference_load_wav_to_sample']),
                     ('optimized', 'core/synth/wav_loader.cpp', []),
                     ('test', 'tools/wav_load_speed_test.cpp', [])]
            objects, dependency_files = [], []
            for name, source, extra in units:
                obj, dep = build / f'{name}.o', build / f'{name}.d'
                command([compiler, *common, *optimization, *extra, '-MMD', '-MF', dep,
                         '-c', source, '-o', obj], snapshot)
                objects.append(obj)
                dependency_files.append(dep)
            binary = build / 'wav-load-speed-test'
            command([compiler, *optimization, *(['-no-pie'] if args.sanitize else []),
                     *objects, '-Wl,--wrap=fread,--wrap=fseek,--wrap=_Znwm', '-o', binary], snapshot)
            verify(inputs, ROOT)
            verify(inputs, snapshot)

            env = os.environ.copy()
            if args.sanitize:
                env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            with tempfile.TemporaryDirectory(prefix='seance-wav-load-speed-') as temporary:
                command([binary, temporary, results_path,
                         'benchmark' if args.benchmark else 'correctness', str(args.repeats)],
                        snapshot, timeout=180, env=env)
            receipt['results'] = json.loads(results_path.read_text())

            if args.arm:
                devkit = Path(os.environ.get('DEVKITARM', '/opt/devkitpro/devkitARM'))
                arm = (devkit / 'bin/arm-none-eabi-g++').resolve()
                receipt['arm_compiler'] = {'path': str(arm), 'sha256': sha(arm),
                                           'version': command([arm, '--version'], snapshot).strip()}
                dep = build / 'wav-loader-arm.d'
                command([arm, '-std=gnu++17', '-O2', '-Wall', '-Wextra', '-Werror',
                         '-fno-rtti', '-fno-exceptions', '-march=armv6k', '-mtune=mpcore',
                         '-mfloat-abi=hard', '-mtp=soft', '-MMD', '-MF', dep,
                         '-c', 'core/synth/wav_loader.cpp', '-o', build / 'wav-loader-arm.o'], snapshot)
                dependency_files.append(dep)
                receipt['arm_object_passed'] = True
                emit('pass ARM decoder object only; not an application build or device storage test')

            dependencies = set()
            for dep in dependency_files:
                names = dependency_paths(dep, snapshot)
                unknown = set(names) - set(inputs)
                if unknown:
                    raise RuntimeError(f'compiler used unsnapshotted inputs: {sorted(unknown)}')
                dependencies.update(names)
            receipt['compiler_dependencies'] = [dict(path=name, **inputs[name]) for name in sorted(dependencies)]
            verify(inputs, ROOT)
            verify(inputs, snapshot)
            receipt['live_and_snapshot_inputs_reverified'] = True
            receipt['status'] = 'passed'
            emit(f'pass final input verification: {len(inputs)} live/snapshot files, {len(dependencies)} compiler dependencies')
        except (Exception, KeyboardInterrupt) as error:
            failure = error
            receipt['status'] = 'failed'
            receipt['error'] = f'{type(error).__name__}: {error}'
            emit(receipt['error'])
        finally:
            receipt['finished_utc'] = dt.datetime.now(dt.timezone.utc).isoformat()
            receipt['elapsed_seconds'] = round(time.monotonic() - monotonic_start, 6)
            emit(f'receipt: {receipt_path.relative_to(ROOT)} ({receipt["status"]})')
    for path in [*sorted(build.glob('*')), results_path, log_path]:
        if path.is_file():
            receipt['artifacts'][str(path.relative_to(run))] = {'sha256': sha(path), 'bytes': path.stat().st_size}
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
    if failure is not None:
        return 1
    latest_name = 'benchmark-latest.json' if args.benchmark else 'sanitize-latest.json' if args.sanitize else 'check-latest.json'
    (output_root / latest_name).write_text(json.dumps({
        'receipt': str(receipt_path.relative_to(ROOT)), 'sha256': sha(receipt_path),
        'status': receipt['status'], 'hardware_tested': False,
    }, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
