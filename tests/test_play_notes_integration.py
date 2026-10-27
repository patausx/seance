#!/usr/bin/env python3
"""Scoped current PLAY note/stretch UI + persistence test; no inherited suites or deploy."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from datetime import datetime, timezone
from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources
from test_mod_minimal_ui import extract_main

OUT = ROOT / 'release/play-notes-stretch'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def inventory():
    result = sources()
    for name in ('tests/play_notes_integration_cases.inc', 'tests/test_play_notes_integration.py'):
        result[name] = sha(ROOT / name)
    return result

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    directory = Path(tempfile.mkdtemp(prefix='seance-play-notes-ui-'))
    original = (ROOT / 'tests/osc_input_dispatch_test.cpp').read_text()
    entry = 'int main(int argc,char** argv) {'
    assert original.count(entry) == 1
    driver = directory / 'driver.cpp'
    driver.write_text(original.replace(entry, 'int unused_play_note_driver(int argc,char** argv) {', 1)
                      + '\n' + (ROOT / 'tests/play_notes_integration_cases.inc').read_text())
    extracted = extract_main(directory)
    units = make_variable('OSC_INPUT_SOURCES')
    units[units.index('tests/osc_input_dispatch_test.cpp')] = str(driver)
    binary = directory / 'play_notes_ui_san'
    before = inventory()
    command = [os.environ.get('CXX', 'g++'), '-O0', '-g1', *make_variable('SEQUENCER_TEST_FLAGS'),
               '-DSEANCE_REAL_EDITOR', '-Itests', '-I' + str(directory), '-fsanitize=address,undefined',
               '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', *WRAP, *units, '-o', str(binary)]
    receipt = {'source_sha256': before, 'build_command': command, 'host_only': True,
               'deployment': False, 'inherited_groups_run': 0, 'extracted_main_sha256': extracted}
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    for stage, args in (('build', command), ('check', [str(binary)])):
        log = OUT / f'integration-{stage}-{stamp}.log'
        with log.open('x') as stream:
            result = subprocess.run(args, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=300)
        print(log.read_text()[-18000:], end='', flush=True)
        receipt[stage] = {'exit': result.returncode, 'log': str(log.relative_to(ROOT))}
        receipt['inputs_unchanged'] = inventory() == before
        (OUT / f'integration-{stamp}.json').write_text(json.dumps(receipt, indent=2) + '\n')
        if result.returncode:
            raise SystemExit(result.returncode)
        assert receipt['inputs_unchanged'], 'source changed during proof; rerun final after edits settle'
    receipt['binary'] = str(binary)
    receipt['binary_sha256'] = sha(binary)
    (OUT / 'integration-latest.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('focused integration PASS; no hardware/audio-quality claim')

if __name__ == '__main__':
    main()
