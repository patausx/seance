#!/usr/bin/env python3
"""Focused bounce ribbon proof: actual engine/WAV/main/UI, no inherited groups.

One sanitized host build. --capture-only verifies its successful receipt and
emits native C++ surfaces without tests/rebuilds. WAV fixtures live only in an
isolated automatically removed temporary directory. No hardware/network/PNG.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from test_fader_lancet_ui import ROOT, make_variable, sources as runtime_sources

OUT = ROOT / 'ui_mockups/bounce_ribbon_runtime'
PROOF = OUT / 'proof.json'
OWN = ('tests/test_bounce_ribbon.py', 'tests/bounce_ribbon_ui_test.cpp')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sources():
    result = runtime_sources()
    for name in OWN:
        result[name] = sha(ROOT / name)
    return dict(sorted(result.items()))


def run(command, log, timeout=300):
    print(shlex.join(map(str, command)), flush=True)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with log.open('x') as stream:
        stream.write('$ ' + shlex.join(map(str, command)) + '\n')
        stream.flush()
        result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    text = log.read_text()
    print(text[-18000:], end='', flush=True)
    if result.returncode:
        raise RuntimeError(f'command failed ({result.returncode}); log: {log}')
    return text


def extract_main(directory):
    source = (ROOT / 'platform/3ds/main.cpp').read_text()
    def block(start):
        begin = source.index(start)
        opening = source.index('{', begin)
        depth, at = 1, opening + 1
        while depth:
            depth += (source[at] == '{') - (source[at] == '}')
            at += 1
        return source[begin:at] + '\n'
    files = {}
    for name, start in (
        ('buttons.inc', '        if (g_view == View::Bounce && g_overlay == Overlay::None && !nav_is_open())'),
        ('touch.inc', '    if (g_view == View::Bounce && g_overlay == Overlay::None)'),
        ('bottom.inc', 'static void draw_bottom_surface(View view, Overlay overlay) {'),
    ):
        path = directory / name
        path.write_text(block(start))
        files[name] = sha(path)
    return files


def compiler_dependencies(compiler, flags, units, directory, stamp):
    # The compiler itself resolves transitive includes, including the production
    # bounce.cpp included in our TU. This is preprocessing only, not a second build.
    command = [compiler, *[f for f in flags if not f.startswith('-Wl,')], '-MM', *units]
    result = subprocess.run(command, cwd=ROOT, check=True, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    path = OUT / f'dependencies-{stamp}.txt'
    path.write_text(result.stdout)
    dependencies = set()
    for rule in result.stdout.replace('\\\n', ' ').splitlines():
        if ':' in rule:
            dependencies.update(shlex.split(rule.split(':', 1)[1]))
    paths = [Path(name) if Path(name).is_absolute() else ROOT / name for name in dependencies]
    pinned = {}
    for p in paths:
        p = p.resolve()
        name = str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p)
        pinned[name] = sha(p)
    assert 'platform/3ds/ui_bounce.cpp' in pinned and len(pinned) > 100
    return {'command': command, 'sha256': dict(sorted(pinned.items())),
            'raw': str(path.relative_to(ROOT)), 'raw_sha256': sha(path),
            'stderr': result.stderr}


def verify_dependencies(proof):
    for name, digest in proof['compiler_dependencies']['sha256'].items():
        p = Path(name) if Path(name).is_absolute() else ROOT / name
        assert sha(p) == digest, f'compiler dependency changed: {name}'


def publish(proof, stamp):
    text = json.dumps(proof, indent=2, sort_keys=True) + '\n'
    PROOF.write_text(text)
    (OUT / f'receipt-{stamp}.json').write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--capture-only', action='store_true')
    parser.add_argument('--capture-dir', type=Path)
    args = parser.parse_args()
    assert not (args.capture_only and args.build_only)
    OUT.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    before = sources()
    if args.no_build or args.capture_only:
        proof = json.loads(PROOF.read_text())
        binary = Path(proof['binary'])
        assert proof['source_sha256'] == before, 'source inventory changed; explicit rebuild required'
        assert sha(binary) == proof['binary_sha256'], 'binary changed since proof'
        verify_dependencies(proof)
    else:
        directory = Path(tempfile.mkdtemp(prefix='seance-bounce-ribbon-build-'))
        extracted = extract_main(directory)
        binary = directory / 'seance_bounce_ribbon_san'
        units = make_variable('BOUNCE_UI_SOURCES')
        assert units.count('tests/bounce_ui_test.cpp') == 1
        units[units.index('tests/bounce_ui_test.cpp')] = OWN[1]
        units.remove('platform/3ds/ui_bounce.cpp')  # included in the TU for private frozen-frame/target proof
        compiler = os.environ.get('CXX', 'g++')
        flags = ['-O1', '-g1', *make_variable('SEQUENCER_TEST_FLAGS'), '-Itests', '-I' + str(directory),
                 '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                 '-Wl,--wrap=_ZN6seance2ui4rectEffffj']
        dependencies = compiler_dependencies(compiler, flags, units, directory, stamp)
        command = [compiler, *flags, *units, '-o', str(binary)]
        log = OUT / f'build-{stamp}.log'
        text = run(command, log)
        assert sources() == before, 'sources changed during build'
        proof = dict(built_at=stamp, binary=str(binary), binary_sha256=sha(binary),
                     source_sha256=before, extracted_main_sha256=extracted,
                     compiler_dependencies=dependencies, build_command=command,
                     compiler_version=subprocess.check_output([compiler, '--version'], text=True).splitlines()[0],
                     build_log=str(log.relative_to(ROOT)), warning_count=text.count('warning:'),
                     checks={'status': 'not run'}, host_only=True, deployment=False,
                     inherited_groups_run=0)
        verify_dependencies(proof)
        publish(proof, stamp)
    if args.build_only:
        print(PROOF)
        return
    if args.capture_only:
        assert proof['checks']['status'] == 'passed', 'focused proof must pass before capture'
        destination = (args.capture_dir or OUT / ('capture-' + stamp)).resolve()
        assert not destination.exists(), 'capture destination exists; preserve artifacts'
        log = OUT / f'capture-{stamp}.log'
        with tempfile.TemporaryDirectory(prefix='seance-bounce-ribbon-wav-') as recordings:
            text = run([str(binary), '--capture-only', str(destination), recordings], log)
        assert '0 regression groups' in text
        from PIL import Image
        surfaces = sorted(destination.glob('*.ppm'))
        receipt = json.loads((destination / 'capture.json').read_text())
        assert len(surfaces) == receipt['pairs'] * 2
        for path in surfaces:
            with Image.open(path) as image:
                image.load()
                assert image.size == (400 if path.name.endswith('_top.ppm') else 320, 240)
        receipt.update(captured_at=stamp, source_sha256=before,
                       binary_sha256=sha(binary), binary=str(binary),
                       surface_sha256={p.name: sha(p) for p in surfaces}, native_surfaces=len(surfaces),
                       manifest_sha256=sha(destination / 'manifest.tsv'),
                       regression_groups_run=0, capture_log=str(log.relative_to(ROOT)),
                       temporary_recordings_removed=True)
        (destination / 'capture.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n')
        proof['capture'] = dict(path=str(destination), surfaces=len(surfaces), log=str(log.relative_to(ROOT)))
    else:
        log = OUT / f'check-{stamp}.log'
        proof['checks'] = dict(status='pending', log=str(log.relative_to(ROOT)))
        publish(proof, stamp)
        try:
            with tempfile.TemporaryDirectory(prefix='seance-bounce-ribbon-wav-') as recordings:
                text = run([str(binary), '--proof-only', recordings], log)
            assert 'bounce ribbon UI: passed;' in text
        except Exception:
            proof['checks']['status'] = 'failed'
            publish(proof, stamp)
            raise
        proof['checks'].update(status='passed', sanitizer='ASAN/UBSAN/leaks', completed_at=stamp,
                               temporary_recordings_removed=True,
                               summary=next(line for line in text.splitlines() if line.startswith('bounce ribbon UI:')))
    assert sources() == before and sha(binary) == proof['binary_sha256'], 'source/binary changed during proof/capture'
    verify_dependencies(proof)
    publish(proof, stamp)
    print(PROOF)


if __name__ == '__main__':
    main()
