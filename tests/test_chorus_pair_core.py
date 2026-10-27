#!/usr/bin/env python3
"""Retained scoped Chorus core proof. Run --baseline BEFORE production edits.

Copies real core inputs and this driver; compile/run only the focused Chorus
fixture, never ARM/UI/device. Baseline artifacts/receipt cannot be overwritten.
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
RELEASE = ROOT / 'release/chorus-pair'
BASELINE = RELEASE / 'core-pre-edit-baseline.json'
TOOLS = ['chorus_pair_core_test.cpp', 'test_chorus_pair_core.py']


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def inv(paths, base):
    return {str(p.relative_to(base)): {'bytes': p.stat().st_size, 'sha256': sha(p)}
            for p in sorted(paths) if p.is_file()}
def dump(p, value): p.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
def run(cmd, cwd, log, env):
    start = time.monotonic()
    with log.open('x') as stream:
        stream.write('$ ' + ' '.join(map(str, cmd)) + '\n'); stream.flush()
        r = subprocess.run(cmd, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT)
    print(log.read_text(), end='', flush=True)
    return {'command': list(map(str, cmd)), 'exit': r.returncode, 'seconds': time.monotonic()-start,
            'log': log.name, 'sha256': sha(log)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline', action='store_true')
    ap.add_argument('--sanitized', action='store_true')
    args = ap.parse_args()
    expected = None
    if args.baseline and BASELINE.exists(): raise SystemExit('baseline exists; refusing replacement')
    if not args.baseline:
        expected = json.loads(BASELINE.read_text()); prior = ROOT / expected['directory']
        assert sha(prior / 'receipt.json') == expected['receipt_sha256'], 'baseline receipt changed'
        assert inv((prior/'fixtures').iterdir(),prior/'fixtures') == expected['fixtures'], 'baseline artifacts changed'
        old = json.loads((prior/'receipt.json').read_text())
        assert inv((prior/'inputs').rglob('*'), prior/'inputs') == old['inputs'], 'baseline inputs changed'
    stamp = dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    mode = 'pre-edit' if args.baseline else ('sanitized' if args.sanitized else 'optimized')
    out = RELEASE / ('core-' + mode + '-' + stamp); inputs = out/'inputs'; inputs.mkdir(parents=True)
    sources = {str(p.relative_to(ROOT)): p for p in (ROOT/'core').rglob('*') if p.is_file()}
    sources.update({'tests/'+name: ROOT/'tests'/name for name in TOOLS})
    before = inv(sources.values(), ROOT)
    for name, source in sources.items():
        dest = inputs/name; dest.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, dest)
    copied = inv(inputs.rglob('*'), inputs)
    proof = {'kind':'chorus-pair-'+mode, 'created_utc':stamp, 'inputs':copied,
             'snapshot_matches_source':copied==before, 'asan':args.sanitized,
             'ubsan':args.sanitized, 'leaks':args.sanitized, 'arm':False, 'ui':False,
             'deployment':False, 'broad_suite':False, 'passed':False,
             'limitations':['host synthetic PCM/SNC, not listening/hardware/timing',
                            'lease counts processed audio only; platform must cancel bypass/stopped/context paths',
                            'legacy fixtures are synthetic correctly padded formats, not every user-authored file']}
    dump(out/'receipt.json',proof); status=1
    try:
        assert before==copied, 'source changed while snapshotting'
        if not args.baseline:
            oldinputs=prior/'inputs'
            def outside(text, start, end):
                a=text.index(start); b=text.index(end,a); return text[:a]+text[b:]
            assert outside((oldinputs/'core/engine/units.cpp').read_text(),'ChorusUnit::ChorusUnit()',
                           'void ReverbUnit::process_legacy') == outside((inputs/'core/engine/units.cpp').read_text(),
                           'ChorusUnit::ChorusUnit()', 'void ReverbUnit::process_legacy')
            assert outside((oldinputs/'core/engine/units.h').read_text(),'class ChorusUnit :',
                           'class ReverbUnit :') == outside((inputs/'core/engine/units.h').read_text(),
                           'class ChorusUnit :', 'class ReverbUnit :')
            allowed={'core/engine/units.cpp','core/engine/units.h','core/engine/chorus_motion.h'}
            assert all(copied[name]==identity for name,identity in old['inputs'].items()
                       if name.startswith('core/') and name not in allowed), 'unowned core differs'
            proof['non_chorus_core_byteexact']=True
        flags=['-O1','-g1'] if args.sanitized else ['-O2']
        flags+=['-std=gnu++17','-Wall','-Wextra','-ffunction-sections','-fdata-sections','-Icore']
        if args.sanitized: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
        if args.baseline: flags+=['-DCHORUS_PAIR_BASELINE_ONLY']
        cpp=[str(p.relative_to(inputs)) for p in sorted((inputs/'core/engine').glob('*.cpp'))]
        cpp+=['core/synth/sampler.cpp','core/synth/wav_loader.cpp','core/synth/wav_writer.cpp']
        command=[os.environ.get('CXX','g++'),*flags,'tests/chorus_pair_core_test.cpp',*cpp,
                 '-Wl,--gc-sections','-o',str(out/'chorus-pair-core')]
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        proof['compile']=run(command,inputs,out/'compile.log',env); dump(out/'receipt.json',proof)
        assert proof['compile']['exit']==0, 'compile failed'
        fixtures=out/'fixtures'; fixtures.mkdir()
        proof['run']=run([str(out/'chorus-pair-core'),str(fixtures)],inputs,out/'run.log',env)
        assert proof['run']['exit']==0, 'focused test failed'
        proof['fixtures']=inv(fixtures.iterdir(),fixtures)
        if expected is not None:
            proof['frozen_exact']=all(proof['fixtures'].get(name)==identity for name,identity in expected['fixtures'].items())
            assert proof['frozen_exact'], 'pre-edit PCM/SNC differs'
            proof['frozen_fixture_count']=len(expected['fixtures'])
        status=0
    except Exception as error:
        import traceback
        proof['failure']=str(error); proof['traceback']=traceback.format_exc(); print(proof['traceback'])
    proof['retained_inputs_unchanged']=inv(inputs.rglob('*'),inputs)==copied
    proof['live_inputs_unchanged']=inv(sources.values(),ROOT)==copied
    proof['changed_live_paths']=[name for name,value in copied.items() if inv([ROOT/name],ROOT).get(name)!=value]
    proof['tested_copy_passed']=status==0
    proof['passed']=status==0 and proof['retained_inputs_unchanged'] and proof['live_inputs_unchanged']
    proof['artifacts']=inv([p for p in out.rglob('*') if p.is_file() and inputs not in p.parents and p.name!='receipt.json'],out)
    dump(out/'receipt.json',proof)
    if args.baseline and proof['passed']:
        dump(BASELINE,{'directory':str(out.relative_to(ROOT)), 'receipt_sha256':sha(out/'receipt.json'),
                       'fixtures':proof['fixtures']})
    print('retained:',out.relative_to(ROOT), 'passed:',proof['passed'],flush=True)
    return 0 if proof['passed'] else 1

if __name__=='__main__': sys.exit(main())
