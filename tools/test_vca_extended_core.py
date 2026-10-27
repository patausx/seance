#!/usr/bin/env python3
"""Retained VCA pre-edit PCM/SNC and focused current core checks; host only."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'release/vca-extended'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def inventory(root): return {str(p.relative_to(root)):sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--baseline',action='store_true');ap.add_argument('--sanitized',action='store_true')
    a=ap.parse_args();label='sanitized' if a.sanitized else 'optimized'
    out=OUT/'before' if a.baseline else OUT/(label+'-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ'))
    out.mkdir(parents=True,exist_ok=False);root=out/'inputs'
    shutil.copytree(ROOT/'core',root/'core');(root/'tools').mkdir()
    for name in ('vca_extended_core_test.cpp','test_vca_extended_core.py'):
        shutil.copy2(ROOT/'tools'/name,root/'tools'/name)
    inputs=inventory(root);fixtures=out/'fixtures';fixtures.mkdir()
    flags=['-std=gnu++17','-Wall','-Wextra','-Icore','-ffunction-sections','-fdata-sections']
    flags+=['-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie'] if a.sanitized else ['-O2']
    if a.baseline:flags+=['-DVCA_BASELINE']
    sources=[str(p.relative_to(root)) for p in sorted((root/'core/engine').glob('*.cpp'))]+['core/synth/sampler.cpp','core/synth/wav_loader.cpp','core/synth/wav_writer.cpp']
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    proof=dict(mode='pre-edit-baseline' if a.baseline else label,inputs=inputs,passed=False,host_only=True)
    def run(command,cwd,name):
        with (out/name).open('x') as log:
            log.write('$ '+' '.join(map(str,command))+'\n');log.flush()
            r=subprocess.run(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        print((out/name).read_text(),end='',flush=True);assert r.returncode==0,(name,r.returncode)
    try:
        binary=out/'vca-core'
        run(['g++',*flags,'tools/vca_extended_core_test.cpp',*sources,'-Wl,--gc-sections','-o',str(binary)],root,'compile.log')
        run([str(binary),str(fixtures)],root,'run.log')
        if not a.baseline:
            frozen=inventory(OUT/'before/fixtures')
            assert all(sha(fixtures/p)==h for p,h in frozen.items()),'old VCA PCM/SNC changed'
            proof['frozen_exact']=frozen
            run([str(OUT/'before/vca-core'),'--old-reader',str(fixtures/'extended.snc')],ROOT,'old-reader.log')
        proof.update(passed=True,binary_sha256=sha(binary),fixtures=inventory(fixtures))
    finally:
        proof['retained_inputs_unchanged']=inventory(root)==inputs
        proof['current_core_unchanged']=inventory(ROOT/'core')==inventory(root/'core')
        proof['passed'] &= proof['retained_inputs_unchanged'] and proof['current_core_unchanged']
        (out/'receipt.json').write_text(json.dumps(proof,indent=2)+'\n')
        print(out,proof['passed'],flush=True)
if __name__=='__main__':main()
