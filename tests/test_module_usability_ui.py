#!/usr/bin/env python3
"""Six-module usability proof, current real main/editor/storage, no inherited suites.

Builds a private host driver. Only the eight new groups run; old fixtures supply
raster/audio boundaries, not behavior overrides. Never builds ARM or deploys.
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

from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources as common_sources
from test_mod_minimal_ui import extract_main

OUT = ROOT / 'release/module-usability'
PROOF = OUT / 'proof.json'
OWN = ('tests/test_module_usability_ui.py', 'tests/module_usability_ui_cases.inc',
       'tests/module_usability_gestures.inc', 'tests/test_mod_minimal_ui.py')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sources():
    result = common_sources()
    for name in OWN:
        result[name] = sha(ROOT / name)
    return dict(sorted(result.items()))


def run(command, log, timeout=300):
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with log.open('x') as stream:
        stream.write('$ ' + shlex.join(map(str, command)) + '\n')
        stream.flush()
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=timeout)
    print(log.read_text()[-24000:], end='', flush=True)
    return result.returncode


def publish(proof, stamp):
    text = json.dumps(proof, indent=2) + '\n'
    PROOF.write_text(text)
    (OUT / ('receipt-' + stamp + '.json')).write_text(text)


def verify(proof):
    assert sources() == proof['source_sha256'], 'stale source inventory'
    assert sha(proof['binary']) == proof['binary_sha256'], 'stale binary'
    for name, digest in proof['compiler_dependencies'].items():
        assert sha(Path(name) if Path(name).is_absolute() else ROOT / name) == digest, name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True, parents=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    before = sources()
    if args.no_build:
        proof = json.loads(PROOF.read_text())
        verify(proof)
        binary = Path(proof['binary'])
    else:
        directory = Path(tempfile.mkdtemp(prefix='seance-module-usability-'))
        original = (ROOT / 'tests/osc_input_dispatch_test.cpp').read_text()
        entry = 'int main(int argc,char** argv) {'
        assert original.count(entry) == 1
        driver = directory / 'driver.cpp'
        extracted = extract_main(directory)
        adapters = []
        source = (ROOT / 'platform/3ds/main.cpp').read_text()
        for return_type, name in (('bool', 'seq_record_lock'), ('q15', 'seq_display_value')):
            match = re.search(r'^' + return_type + ' ' + name + r'\([^\n]+\) \{.*?^}', source, re.M | re.S)
            assert match, name
            adapters.append(match.group())
            extracted[name] = hashlib.sha256(match.group().encode()).hexdigest()
        driver.write_text('#define seq_record_lock usability_legacy_record_lock\n'
                          '#define seq_display_value usability_legacy_display_value\n' +
                          original.replace(entry, 'int unused_usability_driver(int argc,char** argv) {', 1) +
                          '\n#undef seq_record_lock\n#undef seq_display_value\nnamespace seance::platform::ui3ds {\n' +
                          '\n'.join(adapters) + '\n}\n' + (ROOT / OWN[2]).read_text() +
                          '\n' + (ROOT / OWN[1]).read_text())
        binary = directory / 'module_usability_ui_san'
        units = make_variable('OSC_INPUT_SOURCES')
        units[units.index('tests/osc_input_dispatch_test.cpp')] = str(driver)
        compiler = os.environ.get('CXX', 'g++')
        flags = ['-O0', '-g1', *make_variable('SEQUENCER_TEST_FLAGS'),
                 '-DSEANCE_REAL_EDITOR', '-Itests', '-I' + str(directory),
                 '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', *WRAP]
        command = [compiler, *flags, *units, '-o', str(binary)]
        proof = dict(built_at=stamp, binary=str(binary), source_sha256=before,
                     build_command=command, extracted_main_sha256=extracted,
                     driver_sha256=sha(driver), inherited_groups_run=0, host_only=True,
                     deployment=False, checks={'status': 'not run'})
        log = OUT / ('host-build-' + stamp + '.log')
        status = run(command, log)
        proof['build'] = dict(exit=status, log=str(log.relative_to(ROOT)))
        publish(proof, stamp)
        if status:
            raise SystemExit(status)
        assert sources() == before, 'inputs changed during compilation'
        dep_command = [compiler, *[f for f in flags if not f.startswith('-Wl,')], '-MM', *units]
        result = subprocess.run(dep_command, cwd=ROOT, capture_output=True, text=True, check=True, timeout=120)
        (OUT / ('dependencies-' + stamp + '.txt')).write_text(result.stdout)
        found = set()
        for line in result.stdout.replace('\\\n', ' ').splitlines():
            if ':' in line:
                found.update(shlex.split(line.split(':', 1)[1]))
        deps = {}
        for name in found:
            path = (Path(name) if Path(name).is_absolute() else ROOT / name).resolve()
            deps[str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)] = sha(path)
        proof['compiler_dependencies'] = dict(sorted(deps.items()))
        proof['binary_sha256'] = sha(binary)
        verify(proof)
        publish(proof, stamp)
    if not args.build_only:
        log = OUT / ('check-' + stamp + '.log')
        status = run([str(binary), '--proof-only'], log)
        proof['checks'] = dict(status='passed' if not status else 'failed', exit=status,
                               log=str(log.relative_to(ROOT)), sanitizer='ASAN/UBSAN/leaks')
        if not status:
            proof['checks']['summary'] = next(line for line in log.read_text().splitlines()
                                               if line.startswith('module usability:'))
        publish(proof, stamp)
        if status:
            raise SystemExit(status)
    verify(proof)
    publish(proof, stamp)
    print(PROOF)


if __name__ == '__main__':
    main()
