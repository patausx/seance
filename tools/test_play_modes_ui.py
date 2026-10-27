#!/usr/bin/env python3
"""Focused current PLAY modes proof; actual main/editor/engine, no inherited suites.

One isolated sanitizer executable. Capture-only reuses an exact successful
source/binary inventory and does not run tests. No ARM/network/device actions.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources as common_sources
from test_mod_minimal_ui import extract_main

OUT = ROOT / 'ui_mockups/play_modes_runtime'
PROOF = OUT / 'proof.json'
OWN = ('tools/test_play_modes_ui.py', 'tools/play_modes_ui_cases.inc',
       'tools/generate_play_editor_type.py', 'assets/fonts/Gossip-MedPixel.otf')


def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def sources():
    result = common_sources()
    for name in OWN:
        result[name] = sha(ROOT/name)
    return dict(sorted(result.items()))


def run(command, path, timeout=300):
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with path.open('x') as stream:
        stream.write('$ ' + shlex.join(map(str, command)) + '\n'); stream.flush()
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=timeout)
    print(path.read_text()[-22000:], end='', flush=True)
    return result.returncode


def dependencies(compiler, flags, units, stamp):
    command = [compiler, *[f for f in flags if not f.startswith('-Wl,')], '-MM', *units]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=True, timeout=120)
    raw = OUT/('dependencies-'+stamp+'.txt'); raw.write_text(result.stdout)
    found = set()
    for line in result.stdout.replace('\\\n',' ').splitlines():
        if ':' in line: found.update(shlex.split(line.split(':',1)[1]))
    pinned = {}
    for name in found:
        p = (Path(name) if Path(name).is_absolute() else ROOT/name).resolve()
        key = str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p)
        pinned[key] = sha(p)
    assert 'platform/3ds/ui_play_editor.cpp' in pinned
    return dict(command=command, sha256=dict(sorted(pinned.items())), raw=str(raw.relative_to(ROOT)), stderr=result.stderr)


def verify_deps(proof):
    for name, h in proof['compiler_dependencies']['sha256'].items():
        assert sha(Path(name) if Path(name).is_absolute() else ROOT/name)==h,name


def publish(proof, stamp):
    text=json.dumps(proof,indent=2)+'\n'
    PROOF.write_text(text)
    (OUT/('receipt-'+stamp+'.json')).write_text(text)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-only',action='store_true')
    parser.add_argument('--capture-dir',type=Path)
    parser.add_argument('--no-build',action='store_true')
    parser.add_argument('--build-only',action='store_true')
    args=parser.parse_args()
    assert not(args.capture_only and args.build_only)
    OUT.mkdir(exist_ok=True,parents=True)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    before=sources()
    if args.no_build or args.capture_only:
        proof=json.loads(PROOF.read_text());binary=Path(proof['binary'])
        assert before==proof['source_sha256'],'stale sources'
        assert sha(binary)==proof['binary_sha256'],'stale binary'
        verify_deps(proof)
    else:
        directory=Path(tempfile.mkdtemp(prefix='seance-play-modes-'))
        original=(ROOT/'tools/osc_input_dispatch_test.cpp').read_text()
        entry='int main(int argc,char** argv) {';assert original.count(entry)==1
        driver=directory/'driver.cpp'
        driver.write_text(original.replace(entry,'int unused_play_modes_driver(int argc,char** argv) {',1)
                          +'\n'+(ROOT/OWN[1]).read_text())
        extracted=extract_main(directory)
        binary=directory/'play_modes_ui_san'
        units=make_variable('OSC_INPUT_SOURCES')
        units[units.index('tools/osc_input_dispatch_test.cpp')]=str(driver)
        compiler=os.environ.get('CXX','g++')
        flags=['-O0','-g1',*make_variable('SEQUENCER_TEST_FLAGS'),'-DSEANCE_REAL_EDITOR',
               '-Itools','-I'+str(directory),'-fsanitize=address,undefined',
               '-fno-omit-frame-pointer','-fno-pie','-no-pie',*WRAP]
        command=[compiler,*flags,*units,'-o',str(binary)]
        proof=dict(built_at=stamp,binary=str(binary),source_sha256=before,build_command=command,
                   extracted_main_sha256=extracted,driver_sha256=sha(driver),host_only=True,
                   inherited_groups_run=0,deployment=False,checks={'status':'not run'})
        log=OUT/('build-'+stamp+'.log')
        status=run(command,log)
        proof['build']=dict(exit=status,log=str(log.relative_to(ROOT)))
        publish(proof,stamp)
        if status:raise SystemExit(status)
        assert before==sources(),'inputs changed during build'
        proof['binary_sha256']=sha(binary)
        proof['compiler_dependencies']=dependencies(compiler,flags,units,stamp)
        verify_deps(proof);publish(proof,stamp)
    if args.build_only:print(PROOF);return
    if args.capture_only:
        assert proof['checks']['status']=='passed'
        destination=(args.capture_dir or OUT/('capture-'+stamp)).resolve()
        assert not destination.exists(),'preserve previous captures'
        log=OUT/('capture-'+stamp+'.log')
        status=run([str(binary),'--capture-only',str(destination)],log)
        assert status==0,'capture failed'
        assert '0 regression groups' in log.read_text()
        receipt=json.loads((destination/'capture.json').read_text())
        surfaces=sorted(destination.glob('*.ppm'));assert surfaces and len(surfaces)%2==0
        receipt.update(source_sha256=before,binary_sha256=sha(binary),
                       surface_sha256={p.name:sha(p) for p in surfaces},
                       manifest_sha256=sha(destination/'manifest.tsv'),captured_at=stamp)
        (destination/'capture.json').write_text(json.dumps(receipt,indent=2)+'\n')
        proof['capture']=dict(path=str(destination),surfaces=len(surfaces),log=str(log.relative_to(ROOT)))
    else:
        log=OUT/('check-'+stamp+'.log')
        status=run([str(binary),'--proof-only'],log)
        proof['checks']=dict(status='passed' if status==0 else 'failed',exit=status,
                             log=str(log.relative_to(ROOT)),sanitizer='ASAN/UBSAN/leaks')
        publish(proof,stamp)
        if status:raise SystemExit(status)
    assert sources()==before and sha(binary)==proof['binary_sha256'],'source/binary moved'
    verify_deps(proof);publish(proof,stamp)
    print(PROOF)


if __name__=='__main__':main()
