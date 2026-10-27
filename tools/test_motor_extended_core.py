#!/usr/bin/env python3
"""Focused retained MOTOR core proof. Requires preserved pre-edit fixtures."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'release/motor-extended'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def inventory(base):return {str(p.relative_to(base)):sha(p) for p in sorted(base.rglob('*')) if p.is_file()}
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--sanitized',action='store_true');args=ap.parse_args()
 label='sanitized' if args.sanitized else 'optimized'
 out=OUT/(label+'-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ'));out.mkdir(parents=True)
 root=out/'inputs';shutil.copytree(ROOT/'core',root/'core');(root/'tools').mkdir()
 for name in ('motor_extended_core_test.cpp','test_motor_extended_core.py'):shutil.copy2(ROOT/'tools'/name,root/'tools'/name)
 before=inventory(root);fixtures=out/'fixtures';fixtures.mkdir()
 flags=['-std=gnu++17','-Wall','-Wextra','-Icore','-ffunction-sections','-fdata-sections']
 flags+=['-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie'] if args.sanitized else ['-O2']
 sources=[str(p.relative_to(root)) for p in sorted((root/'core/engine').glob('*.cpp'))]+['core/synth/sampler.cpp','core/synth/wav_loader.cpp','core/synth/wav_writer.cpp']
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 proof=dict(mode=label,inputs=before,passed=False,host_only=True,broad_suite=False)
 def run(cmd,cwd,name):
  with (out/name).open('x') as log:
   log.write('$ '+' '.join(map(str,cmd))+'\n');log.flush()
   r=subprocess.run(cmd,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
  print((out/name).read_text(),end='',flush=True)
  assert r.returncode==0,(name,r.returncode)
 try:
  binary=out/'motor-core'
  run(['g++',*flags,'tools/motor_extended_core_test.cpp',*sources,'-Wl,--gc-sections','-o',str(binary)],root,'compile.log')
  run([str(binary),str(fixtures)],root,'run.log')
  old=OUT/'before/fixtures';frozen=inventory(old)
  assert all(sha(fixtures/name)==digest for name,digest in frozen.items()),'legacy pre-edit PCM/SNC changed'
  proof['frozen_exact']=frozen
  if not args.sanitized:
   oldcore=OUT/'before/inputs'
   run(['g++',*flags,'-DMOTOR_BASELINE',str(root/'tools/motor_extended_core_test.cpp'),*sources,'-Wl,--gc-sections','-o',str(out/'old-reader')],oldcore,'old-reader-compile.log')
   run([str(out/'old-reader'),'--old-reader',str(fixtures/'modern.snc')],oldcore,'old-reader-run.log')
  proof.update(binary_sha256=sha(binary),fixtures=inventory(fixtures),passed=True)
 finally:
  proof['retained_inputs_unchanged']=inventory(root)==before
  proof['current_core_unchanged']=inventory(ROOT/'core')==inventory(root/'core')
  proof['passed']&=proof['retained_inputs_unchanged'] and proof['current_core_unchanged']
  (out/'receipt.json').write_text(json.dumps(proof,indent=2)+'\n')
  print(out,proof['passed'],flush=True)
if __name__=='__main__':main()
