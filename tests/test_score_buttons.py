#!/usr/bin/env python3
"""Fresh focused score-button host proof. No legacy suites, ARM, network or deployment."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
sys.dont_write_bytecode = True
import test_fader_lancet_ui as common

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'release/score-buttons'
DRIVER = ROOT/'tests/score_buttons_test.cpp'
SELF = Path(__file__).resolve()

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p, value): p.write_text(json.dumps(value, indent=2, sort_keys=True)+'\n')
def pins():
    paths = [p for directory in ('core', 'platform/3ds', 'assets') for p in (ROOT/directory).rglob('*') if p.is_file()]
    paths += [p for p in (ROOT/'tests').rglob('*') if p.is_file() and p.suffix in {'.h', '.cpp', '.inc', '.py'}]
    paths += [ROOT/p for p in ('Makefile', 'AGENTS.md')]
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted(set(paths))}

def extract_main(folder):
    path = ROOT/'platform/3ds/main.cpp'
    source = path.read_text(); masked = common.masked(source)
    pattern = r'if\s*\(\(down\s*&\s*KEY_Y\)\s*&&\s*!\(held\s*&\s*\(KEY_L\s*\|\s*KEY_R\)\)'
    matches = list(re.finditer(pattern, masked)); assert len(matches)==1, 'ambiguous global Y section'
    start = matches[0].start(); brace = masked.index('{', start); end = common.close_brace(masked, brace)
    exact = source[start:end]; destination = folder/'score_main_y.inc'; destination.write_text(exact+'\n')
    assert destination.read_text()[:-1] == source[start:end]
    return {'source': str(path.relative_to(ROOT)), 'source_sha256': sha(path), 'first_line': source[:start].count('\n')+1,
            'last_line': source[:end].count('\n')+1, 'exact_section_sha256': hashlib.sha256(exact.encode()).hexdigest(),
            'extracted_file': str(destination.relative_to(ROOT)), 'extracted_sha256': sha(destination),
            'execution': 'included verbatim inside main_y_probe::dispatch; 108 view/overlay/modifier cases + real bare-Y route'}

def run(command, log, timeout=600):
    with log.open('x') as stream:
        stream.write('$ '+shlex.join(map(str, command))+'\n'); stream.flush()
        proc = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout,
            env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
    return proc.returncode

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sanitized', action='store_true')
    args = ap.parse_args()
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    tag = 'sanitized' if args.sanitized else 'optimized'
    folder = OUT/(tag+'-'+stamp);folder.mkdir(parents=True)
    before = pins(); source = extract_main(folder)
    shutil.copyfile(SELF, folder/'runner-source.py');shutil.copyfile(DRIVER, folder/'driver-source.cpp')
    units = common.make_variable('SEQUENCER_TEST_SOURCES');units[units.index('tests/sequencer_test.cpp')]=str(DRIVER)
    compiler = os.environ.get('CXX', 'g++')
    flags = ['-O1' if args.sanitized else '-O2', '-g1', *common.make_variable('SEQUENCER_TEST_FLAGS'), '-I'+str(folder),
             '-Wl,--wrap=_ZN6seance6Engine22audition_sequence_stepEiii']
    if args.sanitized: flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    objects=[];commands=[]
    for i, unit in enumerate(units):
        obj=folder/(f'{i:02d}_'+Path(unit).stem+'.o');objects.append(obj)
        commands.append([compiler,*flags,'-MMD','-MF',str(obj.with_suffix('.d')),'-c',unit,'-o',str(obj)])
    proof={'status':'building','tag':tag,'source_before':before,'source_after':None,'main_y':source,'compiler':compiler,
           'fresh_objects':len(objects),'compile_workers':2,'commands':commands,'host_only':True,'inherited_suites_run':0,
           'full_suite':False,'hardware':False,'deployment':False,'audio':'host engine PCM only, not listening or hardware',
           'raster':'actual native renderer with legacy host Citro2D boundary; not PICA pixel/FPS claim'}
    receipt=folder/'receipt.json';save(receipt,proof)
    try:
        def build_one(item):
            i, command=item;log=folder/f'compile-{i:02d}.log';code=run(command,log)
            print('compile',i,code,flush=True)
            return {'source':units[i],'exit':code,'log':str(log.relative_to(ROOT))}
        with ThreadPoolExecutor(max_workers=2) as pool: proof['compiles']=list(pool.map(build_one,enumerate(commands)))
        assert not any(p['exit'] for p in proof['compiles']), 'compilation failed'
        binary=folder/'score_buttons';link=[compiler,*flags,*map(str,objects),'-o',str(binary)]
        proof['link_command']=link;proof['link_exit']=run(link,folder/'link.log')
        assert not proof['link_exit'], 'link failed'
        deps={}
        for obj in objects:
            for dep in shlex.split(obj.with_suffix('.d').read_text().replace('\\\n',' ').split(':',1)[1]):
                path=(ROOT/dep).resolve();deps[str(path)]=sha(path)
        proof.update(dependencies=deps,binary_sha256=sha(binary))
        for name,digest in before.items():
            assert sha(ROOT/name)==digest, ('input changed during build',name)
        retained={}
        for name,digest in deps.items():
            path=Path(name)
            if path.is_relative_to(ROOT) and not path.is_relative_to(folder):
                relative=path.relative_to(ROOT);copy=folder/'inputs'/relative;copy.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(path,copy);assert sha(copy)==digest;retained[str(relative)]=digest
        proof['retained_dependencies']=retained
        capture=folder/'capture';proof['run_exit']=run([str(binary),str(capture)],folder/'run.log')
        text=(folder/'run.log').read_text();print(text[-20000:],flush=True)
        proof['summary']=next((line for line in text.splitlines() if line.startswith('score buttons:')),None)
        proof['source_after']=pins();assert proof['source_after']==before, 'source drift during run'
        assert all(sha(path)==digest for path,digest in deps.items()), 'dependency drift'
        assert not proof['run_exit'] and proof['summary'] and '13/13 groups' in proof['summary'], 'focused test failure'
        from PIL import Image
        ppms=sorted(capture.glob('*.ppm'));assert len(ppms)==44
        for ppm in ppms:
            with Image.open(ppm) as image:
                image.load();assert image.size==(400 if ppm.name.endswith('-top.ppm') else 320,240)
                image.save(ppm.with_suffix('.png'))
                with Image.open(ppm.with_suffix('.png')) as restored:assert restored.tobytes()==image.tobytes()
                image.resize((image.width*2,480),Image.Resampling.NEAREST).save(ppm.with_name(ppm.stem+'-2x.png'))
        proof['surfaces']=len(ppms);proof['png_roundtrips_exact']=len(ppms);proof['status']='passed'
    except BaseException as error:
        proof['status']='failed';proof['error']=str(error);raise
    finally:
        proof['evidence_sha256']={str(p.relative_to(folder)):sha(p) for p in sorted(folder.rglob('*')) if p.is_file() and p!=receipt}
        save(receipt,proof);print(receipt,flush=True)

if __name__=='__main__': main()
