#!/usr/bin/env python3
"""Fresh focused module RAND core proofs; retained pre-edit baseline, no UI/ARM/deploy."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'release/module-rand'
BASE_PREFIX='.local-state/backups/20261005_215846_before-module-rand_'
BASE={
    'engine.cpp':'41d4816d5f40a4f4d7c707c11823f19d6292292d71389ad42668d4376704254b',
    'engine.h':'e389355948c653e5dbcc4d577b015b39a9cf7d546bf24fa31c3967c30453b898',
    'patch_io.cpp':'5792daf79a4a690f8694b464b3e9e85796e34f347d202b4e733daefe40c6d82d',
    'unit.h':'69612bd723ab0ee44049ac0189c1ebea37934e69160c3eec47f75b205f276f8d',
    'units.cpp':'83ef29df9f80ea4c2e511c05b9ccdc17340eb09abc66717736b660ecf194f18d',
}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def inventory(root):return {str(p.relative_to(root)):sha(p) for p in sorted(root.rglob('*')) if p.is_file()}
def save(path,value):path.write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--sanitized',action='store_true');args=parser.parse_args()
    tag='sanitized' if args.sanitized else 'optimized';stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out=OUT/('core-'+tag+'-'+stamp);out.mkdir(parents=True,exist_ok=False)
    inputs=out/'inputs';shutil.copytree(ROOT/'core',inputs/'core');(inputs/'tools').mkdir()
    for name in ('module_rand_core_test.cpp','test_module_rand_core.py'):shutil.copyfile(ROOT/'tools'/name,inputs/'tools'/name)
    pins=inventory(inputs);baseline=out/'baseline-inputs';shutil.copytree(inputs,baseline)
    for name,digest in BASE.items():
        path=ROOT/(BASE_PREFIX+name);assert sha(path)==digest,('baseline backup mismatch',name)
        shutil.copyfile(path,baseline/'core/engine'/name)
    (baseline/'core/engine/module_random_impl.inc').unlink() # old layout/translation units, not mixed objects
    baseline_pins=inventory(baseline)
    flags=['-std=gnu++17','-Wall','-Wextra','-Icore','-ffunction-sections','-fdata-sections']
    flags+=['-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie'] if args.sanitized else ['-O2','-g1']
    compiler=os.environ.get('CXX','g++')
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    proof=dict(status='building',mode=tag,inputs=pins,baseline_inputs=baseline_pins,baseline_backups=BASE,
               compiler=compiler,flags=flags,host_only=True,full_suite=False,ui=False,hardware=False,deployment=False,
               reuse_objects=False,compiles=[],dependencies={},pcm='direct host engine; no device or listening claim')
    receipt=out/'receipt.json';save(receipt,proof)
    def run(command,cwd,log):
        with log.open('x') as stream:
            stream.write('$ '+shlex.join(map(str,command))+'\n');stream.flush()
            p=subprocess.run(command,cwd=cwd,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=600)
        return p.returncode
    def build(root,label):
        build_dir=out/label;build_dir.mkdir()
        sources=[str(p.relative_to(root)) for p in sorted((root/'core/engine').glob('*.cpp'))]
        sources+=['core/synth/sampler.cpp','core/synth/wav_loader.cpp','core/synth/wav_writer.cpp','tools/module_rand_core_test.cpp']
        f=flags+(['-DMODULE_RAND_BASELINE'] if label=='baseline' else [])
        objects=[];jobs=[]
        for i,source in enumerate(sources):
            obj=build_dir/(f'{i:02d}_'+Path(source).stem+'.o');objects.append(obj)
            command=[compiler,*f,'-MMD','-MF',str(obj.with_suffix('.d')),'-c',source,'-o',str(obj)]
            jobs.append((i,source,command))
        def compile_one(job):
            i,source,command=job;code=run(command,root,build_dir/f'compile-{i:02d}.log');print(label,i,code,flush=True)
            return dict(source=source,exit=code,command=command)
        with ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(compile_one,jobs))
        proof['compiles'].append(dict(label=label,results=results,fresh_objects=len(objects)))
        assert all(not r['exit'] for r in results),'compile failed: '+label
        binary=build_dir/'module-rand-core';command=[compiler,*f,*map(str,objects),'-Wl,--gc-sections','-Wl,--wrap=fopen','-o',str(binary)]
        code=run(command,root,build_dir/'link.log');assert code==0,'link failed: '+label
        dependencies={}
        for obj in objects:
            for dep in shlex.split(obj.with_suffix('.d').read_text().replace('\\\n',' ').split(':',1)[1]):
                path=(root/dep).resolve();dependencies[str(path.relative_to(root))]=sha(path)
        proof['dependencies'][label]=dependencies
        capture=build_dir/'capture';capture.mkdir();code=run([str(binary),str(capture)],root,build_dir/'run.log')
        text=(build_dir/'run.log').read_text();print(text[-14000:],flush=True)
        proof[label]=dict(binary_sha256=sha(binary),run_exit=code,summary=next((s for s in text.splitlines() if s.startswith('module rand core:')),None),capture=inventory(capture))
        assert code==0,'focused tests failed: '+label
        return capture
    try:
        old=build(baseline,'baseline');current=build(inputs,'current')
        frozen=inventory(old/'frozen');assert len(frozen)==112
        assert inventory(current/'frozen')==frozen,'unassigned PCM/ordinary saves changed from pre-edit engine'
        proof['exact_pre_edit_files']=len(frozen);proof['exact_pre_edit_pcm_files']=56
        assert '7 groups' in proof['current']['summary']
        assert inventory(inputs)==pins and inventory(baseline)==baseline_pins,'retained inputs changed'
        for path,digest in pins.items():assert sha(ROOT/path)==digest,('current input changed',path)
        proof['status']='passed'
    except BaseException as error:
        proof['status']='failed';proof['error']=str(error);raise
    finally:
        proof['evidence_sha256']={str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file() and p!=receipt}
        save(receipt,proof);print(receipt,flush=True)
if __name__=='__main__':main()
