#!/usr/bin/env python3
"""Retained isolated GRAN perform ASAN/UBSAN proof; no broad suite/build/deploy.

--baseline compiles only compatibility fixtures using the explicitly retained
pre-edit core/engine snapshot. Future checks always compare that frozen receipt.
Every attempt retains copied inputs, compile/runtime logs and exact inventories.
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

ROOT=Path(__file__).resolve().parents[1]
RELEASE=ROOT/'release/gran-perform'
BACKUP=ROOT/'.local-state/backups/20261003_130938_before-gran-perform-runtime-co_engine'
BASELINE=RELEASE/'core-pre-edit-baseline.json'

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def inv(paths, base):
    return {str(p.relative_to(base)):{'bytes':p.stat().st_size,'sha256':sha(p)} for p in sorted(paths) if p.is_file()}
def dump(p,obj): p.write_text(json.dumps(obj,indent=2,sort_keys=True)+'\n')
def run(cmd,cwd,log,env=None):
    with log.open('w') as f:
        f.write('$ '+' '.join(map(str,cmd))+'\n'); f.flush()
        r=subprocess.run(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,env=env)
    print(log.read_text(),end=''); return r.returncode

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline',action='store_true')
    ap.add_argument('--engine-backup',type=Path,default=BACKUP)
    args=ap.parse_args()
    if args.baseline and BASELINE.exists(): raise SystemExit('baseline receipt already exists; refusing replacement')
    if not args.baseline and not BASELINE.exists(): raise SystemExit('capture --baseline first')
    if not args.baseline:
        expected=json.loads(BASELINE.read_text()); prior=ROOT/expected['directory']
        if sha(prior/'report.json')!=expected['report_sha256'] or sha(prior/'frozen.pcm')!=expected['pcm']['sha256']:
            raise SystemExit('frozen baseline receipt/PCM changed')
    now=dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out=RELEASE/(('core-pre-edit-' if args.baseline else 'core-check-')+now)
    inputs=out/'inputs'; inputs.mkdir(parents=True)
    shutil.copytree(ROOT/'core',inputs/'core')
    if args.baseline:
        shutil.rmtree(inputs/'core/engine')
        shutil.copytree(args.engine_backup,inputs/'core/engine')
    (inputs/'tools').mkdir()
    names=['gran_perform_core_test.cpp','test_gran_perform_core.py']
    if not args.baseline: names+=['gran_clean_core_test.cpp']
    for name in names:
        shutil.copy2(ROOT/'tools'/name,inputs/'tools'/name)
    before=inv(inputs.rglob('*'),inputs)
    report={'kind':'pre-edit-baseline' if args.baseline else 'focused-modern-core','created_utc':now,
            'engine_source':str(args.engine_backup.relative_to(ROOT)) if args.baseline else 'core/engine',
            'inputs':before,'asan':True,'ubsan':True,'leaks':True,'broad_suites':False,'arm':False,'deployment':False}
    dump(out/'report.json',report)
    cpp=[str(p.relative_to(inputs)) for p in sorted((inputs/'core/engine').glob('*.cpp'))]
    cmd=['g++','-O1','-g','-std=gnu++17','-Wall','-Wextra','-Icore','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    if args.baseline: cmd+=['-DGRAN_PERFORM_BASELINE_ONLY']
    cmd+=['tools/gran_perform_core_test.cpp',*cpp,'core/synth/sampler.cpp','core/synth/wav_loader.cpp','core/synth/wav_writer.cpp','-o',str(out/'gran-perform-core')]
    report['compile_command']=cmd
    result=run(cmd,inputs,out/'compile.log')
    report['compile_exit']=result
    if result==0:
        env=os.environ.copy(); env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result=run([str(out/'gran-perform-core'),str(out)],inputs,out/'run.log',env)
        report['run_exit']=result
        report['fixtures']=[s for s in (out/'run.log').read_text().splitlines() if s.startswith('fixture ')]
        if (out/'frozen.pcm').exists():
            report['pcm']={'bytes':(out/'frozen.pcm').stat().st_size,'sha256':sha(out/'frozen.pcm')}
        if not args.baseline:
            report['frozen_exact']=report['fixtures']==expected['fixtures'] and report.get('pcm')==expected['pcm']
            if not report['frozen_exact']: result=1; print('FAIL: frozen pre-edit output differs')
        elif result==0:
            dump(BASELINE,{'directory':str(out.relative_to(ROOT)),'report_sha256_pending':True,'fixtures':report['fixtures'],'pcm':report['pcm']})
    if not args.baseline:
        # The unchanged historical focused suite independently covers SNC1..7,
        # all prior clean DSP invariants and six genuinely frozen legacy hashes.
        oldcmd=cmd.copy(); oldcmd[oldcmd.index('tools/gran_perform_core_test.cpp')]='tools/gran_clean_core_test.cpp'
        oldcmd[-1]=str(out/'gran-clean-core')
        report['historical_compile_command']=oldcmd
        oldresult=run(oldcmd,inputs,out/'historical-compile.log')
        report['historical_compile_exit']=oldresult
        if oldresult==0:
            env=os.environ.copy(); env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
            oldresult=run([str(out/'gran-clean-core')],inputs,out/'historical-run.log',env)
            report['historical_run_exit']=oldresult
        if oldresult: result=oldresult
    report['retained_inputs_unchanged']=inv(inputs.rglob('*'),inputs)==before
    if not report['retained_inputs_unchanged']: result=1
    report['passed']=result==0
    report['artifacts']=inv([p for p in out.iterdir() if p.is_file() and p.name!='report.json'],out)
    dump(out/'report.json',report)
    if args.baseline and result==0:
        b=json.loads(BASELINE.read_text()); b.pop('report_sha256_pending',None); b['report_sha256']=sha(out/'report.json'); dump(BASELINE,b)
    print('retained:',out.relative_to(ROOT)); return result

if __name__=='__main__': sys.exit(main())
