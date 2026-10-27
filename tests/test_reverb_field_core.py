#!/usr/bin/env python3
"""Retained focused REV utility proof; --baseline must precede production edits."""
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
ROOT=Path(__file__).resolve().parents[1]
RELEASE=ROOT/'release/reverb-field'
BASELINE=RELEASE/'core-pre-edit-baseline.json'
TOOLS=['reverb_field_core_test.cpp','test_reverb_field_core.py']
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def inv(paths,base): return {str(p.relative_to(base)):{'bytes':p.stat().st_size,'sha256':sha(p)} for p in sorted(paths) if p.is_file()}
def dump(p,v): p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def run(cmd,cwd,log,env):
    start=time.monotonic()
    with log.open('x') as f:
        f.write('$ '+' '.join(map(str,cmd))+'\n'); f.flush()
        r=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
    print(log.read_text(),end='',flush=True)
    return {'command':list(map(str,cmd)),'exit':r.returncode,'seconds':time.monotonic()-start,'log':log.name,'sha256':sha(log)}
def main():
    ap=argparse.ArgumentParser(description=__doc__); ap.add_argument('--baseline',action='store_true'); ap.add_argument('--sanitized',action='store_true'); args=ap.parse_args()
    expected=None
    if args.baseline and BASELINE.exists(): raise SystemExit('baseline exists; refusing replacement')
    if not args.baseline:
        expected=json.loads(BASELINE.read_text()); prior=ROOT/expected['directory']; old=json.loads((prior/'receipt.json').read_text())
        assert sha(prior/'receipt.json')==expected['receipt_sha256']
        assert inv((prior/'fixtures').iterdir(),prior/'fixtures')==expected['fixtures']
        assert inv((prior/'inputs').rglob('*'),prior/'inputs')==old['inputs']
    stamp=dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ'); mode='pre-edit' if args.baseline else 'sanitized' if args.sanitized else 'optimized'
    out=RELEASE/('core-'+mode+'-'+stamp); inputs=out/'inputs'; inputs.mkdir(parents=True)
    sources={str(p.relative_to(ROOT)):p for p in (ROOT/'core').rglob('*') if p.is_file()}; sources.update({'tests/'+n:ROOT/'tests'/n for n in TOOLS})
    before=inv(sources.values(),ROOT)
    for name,source in sources.items():
        dest=inputs/name; dest.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(source,dest)
    copied=inv(inputs.rglob('*'),inputs)
    proof={'kind':'reverb-field-'+mode,'created_utc':stamp,'inputs':copied,'snapshot_matches_source':copied==before,'asan':args.sanitized,'ubsan':args.sanitized,'leaks':args.sanitized,'arm':False,'ui':False,'deployment':False,'broad_suite':False,'passed':False,'limitations':['host synthetic PCM/SNC, not listening/hardware/timing','synthetic compatibility cases, not every user file','allocation failure tested only fixed REV state setup and staged load via size-scoped injection']}
    dump(out/'receipt.json',proof); status=1
    try:
        assert before==copied
        if expected:
            allowed={'core/engine/reverb_dsp.h','core/engine/reverb_impl.inc','core/engine/units.h','core/engine/units.cpp','core/engine/patch_io.cpp'}
            assert all(copied[name]==identity for name,identity in old['inputs'].items() if name.startswith('core/') and name not in allowed),'unowned core differs'
            def outside(text,start,end):
                a=text.index(start); b=text.index(end,a); return text[:a]+text[b:]
            assert outside((prior/'inputs/core/engine/units.h').read_text(),'class ReverbUnit :','class TiltUnit :')==outside((inputs/'core/engine/units.h').read_text(),'class ReverbUnit :','class TiltUnit :')
            assert (prior/'inputs/core/engine/units.cpp').read_bytes()==(inputs/'core/engine/units.cpp').read_bytes(),'legacy process/clone/unrelated units changed'
            proof['non_reverb_core_byteexact']=True
        flags=['-O1','-g1'] if args.sanitized else ['-O2']; flags+=['-std=gnu++17','-Wall','-Wextra','-ffunction-sections','-fdata-sections','-Icore']
        if args.sanitized: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
        if args.baseline: flags+=['-DREVERB_FIELD_BASELINE_ONLY']
        cpp=[str(p.relative_to(inputs)) for p in sorted((inputs/'core/engine').glob('*.cpp'))]+['core/synth/sampler.cpp','core/synth/wav_loader.cpp','core/synth/wav_writer.cpp']
        cmd=[os.environ.get('CXX','g++'),*flags,'tests/reverb_field_core_test.cpp',*cpp,'-Wl,--gc-sections','-o',str(out/'reverb-field-core')]
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        proof['compile']=run(cmd,inputs,out/'compile.log',env); dump(out/'receipt.json',proof); assert proof['compile']['exit']==0
        fixtures=out/'fixtures'; fixtures.mkdir(); proof['run']=run([str(out/'reverb-field-core'),str(fixtures)],inputs,out/'run.log',env); assert proof['run']['exit']==0
        proof['fixtures']=inv(fixtures.iterdir(),fixtures)
        if expected:
            proof['frozen_exact']=all(proof['fixtures'].get(n)==v for n,v in expected['fixtures'].items()); proof['frozen_fixture_count']=len(expected['fixtures']); assert proof['frozen_exact'],'pre-edit artifacts differ'
            # Compile the current probe ONLY against the immutable actual pre-edit
            # core. This proves bit1 rejection, not a simulation of old parsing.
            oldcmd=[os.environ.get('CXX','g++'),*flags,'-DREVERB_FIELD_BASELINE_ONLY',str(inputs/'tests/reverb_field_core_test.cpp'),*cpp,'-Wl,--gc-sections','-o',str(out/'old-reader-core')]
            proof['old_reader_compile']=run(oldcmd,prior/'inputs',out/'old-reader-compile.log',env); assert proof['old_reader_compile']['exit']==0
            proof['old_reader_run']=run([str(out/'old-reader-core'),'--old-reader',str(fixtures)],prior/'inputs',out/'old-reader-run.log',env); assert proof['old_reader_run']['exit']==0
            proof['old_reader_core_inventory_exact']=inv((prior/'inputs').rglob('*'),prior/'inputs')==old['inputs']; assert proof['old_reader_core_inventory_exact']
        status=0
    except Exception as error:
        import traceback
        proof['failure']=str(error); proof['traceback']=traceback.format_exc(); print(proof['traceback'])
    proof['retained_inputs_unchanged']=inv(inputs.rglob('*'),inputs)==copied; proof['live_inputs_unchanged']=inv(sources.values(),ROOT)==copied
    proof['tested_copy_passed']=status==0; proof['passed']=status==0 and proof['retained_inputs_unchanged'] and proof['live_inputs_unchanged']
    proof['artifacts']=inv([p for p in out.rglob('*') if p.is_file() and inputs not in p.parents and p.name!='receipt.json'],out); dump(out/'receipt.json',proof)
    if args.baseline and proof['passed']: dump(BASELINE,{'directory':str(out.relative_to(ROOT)),'receipt_sha256':sha(out/'receipt.json'),'fixtures':proof['fixtures']})
    print('retained:',out.relative_to(ROOT),'passed:',proof['passed'],flush=True); return 0 if proof['passed'] else 1
if __name__=='__main__': sys.exit(main())
