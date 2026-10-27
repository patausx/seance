#!/usr/bin/env python3
"""Focused nonmodule lancet pages: actual host controllers/renderers, new receipts only."""
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
OUT = ROOT/'release/lancet-everywhere/nonmodule'
BASE = ROOT/'.local-state/backups/20261005_101142_before-lancet-everywhere_3ds'
CASES = ROOT/'tests/lancet_nonmodule_cases.inc'
ALLOWED = {'ui_chains.cpp': {'param_rail'},
           'ui_transition.cpp': {'draw','draw_fx'}, 'ui_patchbay.cpp': {'patchbay_draw'},
           'ui_sigils.cpp': {'rail','editor'}, 'ui_sequencer.cpp': {'draw_lock_editor','draw_editor'}}
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,v): p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def pins():
    return {str(p.relative_to(ROOT)):sha(p) for d in ('core','platform/3ds','assets') for p in sorted((ROOT/d).rglob('*')) if p.is_file()}
def preservation():
    result={}
    for name,draws in ALLOWED.items():
        a=(BASE/name).read_text();b=(ROOT/'platform/3ds'/name).read_text()
        old,new=common.functions(a),common.functions(b)
        assert old.keys()==new.keys(),name
        changed=[];preserved=[]
        for fn,(_,_,body) in old.items():
            if common.tokens(body)==common.tokens(new[fn][2]):preserved.append(fn);continue
            assert fn in draws,(name,fn)
            assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|malloc|calloc|new|delete|Guard)\b',common.masked(new[fn][2])),(name,fn,'impure')
            changed.append(fn)
        outer=[]
        for src,func in ((a,old),(b,new)):
            src=common.strip_spans(src,[(s,e) for s,e,_ in func.values()])
            src=re.sub(r'^\s*#include "module_fader(?:_painter)?.h"\s*$', '',src,flags=re.M)
            outer.append(common.tokens(src))
        assert outer[0]==outer[1],(name,'non-render declarations changed')
        result[name]={'changed_draw_functions':changed,'preserved_functions':preserved,'baseline_sha256':sha(BASE/name),'current_sha256':sha(ROOT/'platform/3ds'/name)}
    for name in ('ui_performance.cpp','ui_start_menu.cpp','ui_theme.cpp','ui_play_editor.cpp','ui_top.cpp','ui_nav.cpp','ui_bounce.cpp','ui_sample_library.cpp','sigil_surface.h'):
        assert sha(BASE/name)==sha(ROOT/'platform/3ds'/name),('excluded source changed',name)
    return result

def driver(folder):
    # Private copies only: replace unreachable chain/workspace link stubs with
    # actual production translation units. Inherited test mains never execute.
    from test_mod_minimal_ui import extract_main
    extract_main(folder)
    seq=(ROOT/'tests/sequencer_test.cpp').read_text()
    for line in ('void chains_context_draw_top() {} // real Chains path is exercised by menu_stage1_test.cpp',
                 'const WorkspacePoint& workspace_point(int, int) { static const WorkspacePoint point{}; return point; }',
                 'int workspace_camera_x(int) { return 0; }','int workspace_camera_y(int) { return 0; }',
                 'void workspace_shift_slots_right(int,int,int) {++rec_workspace_shifts;}'):
        assert seq.count(line)==1;seq=seq.replace(line,'')
    seq=seq.replace('#include "../','#include "'+str(ROOT)+'/')
    (folder/'sequencer_boundary.cpp').write_text(seq)
    src=(ROOT/'tests/osc_input_dispatch_test.cpp').read_text()
    for line in ('bool chains_picker_background() { return false; }','void workspace_move_slot(int,int,int) { CHECK(false); }'):
        assert src.count(line)==1;src=src.replace(line,'')
    src=src.replace('#include "sequencer_test.cpp"','#include "sequencer_boundary.cpp"')
    src=src.replace('int main(int argc,char** argv) {','int unused_lancet_nonmodule_original_main(int argc,char** argv) {')
    src=src.replace('#include "../','#include "'+str(ROOT)+'/')
    src+='\n'+CASES.read_text()
    source=folder/'driver.cpp';source.write_text(src);return source

def run(cmd,log):
    with log.open('x') as f:
        f.write('$ '+shlex.join(map(str,cmd))+'\n');f.flush()
        code=subprocess.run(cmd,cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,timeout=600,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')).returncode
    return code

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--sanitized',action='store_true');ap.add_argument('--baseline',action='store_true');ap.add_argument('--exploratory',action='store_true');args=ap.parse_args()
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    tag=('baseline' if args.baseline else 'sanitized' if args.sanitized else 'optimized')+('-exploratory' if args.exploratory else '')
    folder=OUT/(tag+'-'+stamp);folder.mkdir(parents=True)
    before=pins();guard=preservation();source=driver(folder)
    (folder/'runner-source.py').write_bytes(Path(__file__).read_bytes())
    (folder/'cases-source.inc').write_bytes(CASES.read_bytes())
    units=common.make_variable('OSC_INPUT_SOURCES');units[units.index('tests/osc_input_dispatch_test.cpp')]=str(source)
    units+=['platform/3ds/ui_chains.cpp','platform/3ds/ui_chains_top.cpp','platform/3ds/ui_workspace.cpp']
    if args.baseline:
        baseline=folder/'baseline'
        for d in ('core','platform/3ds'):shutil.copytree(ROOT/d,baseline/d)
        for name in ALLOWED:shutil.copyfile(BASE/name,baseline/'platform/3ds'/name)
        units=[str(baseline/u) if u.startswith('platform/3ds/') and Path(u).name in ALLOWED else u for u in units]
    flags=['-O1' if args.sanitized else '-O2','-g1','-I'+str(folder),*common.make_variable('SEQUENCER_TEST_FLAGS'),'-DSEANCE_REAL_EDITOR','-Itests','-I'+str(ROOT),*common.WRAP]
    if args.sanitized:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    if args.baseline:flags+=['-DLANCET_BASELINE']
    compiler=os.environ.get('CXX','g++');objects=[];commands=[]
    for i,u in enumerate(units):
        obj=folder/(f'{i:02d}_'+Path(u).stem+'.o');objects.append(obj)
        commands.append([compiler,*flags,'-MMD','-MF',str(obj.with_suffix('.d')),'-c',u,'-o',str(obj)])
    proof={'tag':tag,'runner_sha256':sha(__file__),'cases_sha256':sha(CASES),'source_before':before,'preservation':guard,'commands':commands,'status':'building','host_only':True,'inherited_suites_run':0,'hardware':False,'deployment':False}
    receipt=folder/'receipt.json';save(receipt,proof)
    def compile_one(item):
        i,cmd=item;log=folder/f'compile-{i:02d}.log';code=run(cmd,log);print('compile',i,code,flush=True);return {'source':units[i],'exit':code,'log':str(log)}
    with ThreadPoolExecutor(max_workers=2) as pool:proof['compiles']=list(pool.map(compile_one,enumerate(commands)))
    if any(r['exit'] for r in proof['compiles']):proof['status']='compile failed';save(receipt,proof);raise RuntimeError(str(receipt))
    binary=folder/'lancet_nonmodule';link=[compiler,*flags,*map(str,objects),'-o',str(binary)]
    code=run(link,folder/'link.log');proof['link_exit']=code
    if code:proof['status']='link failed';save(receipt,proof);raise RuntimeError(str(receipt))
    deps={}
    for obj in objects:
        for dep in shlex.split(obj.with_suffix('.d').read_text().replace('\\\n',' ').split(':',1)[1]):
            p=(ROOT/dep).resolve();deps[str(p)]=sha(p)
    proof.update(binary=str(binary),binary_sha256=sha(binary),dependencies=deps)
    capture=folder/'capture';code=run([str(binary),str(capture)],folder/'run.log')
    print((folder/'run.log').read_text()[-6000:],flush=True)
    proof.update(status='passed' if not code else 'run failed',run_exit=code,capture=str(capture),source_after=pins())
    proof['source_drift']=[n for n,h in before.items() if proof['source_after'].get(n)!=h]
    if not args.exploratory:assert not proof['source_drift'],proof['source_drift']
    if code:save(receipt,proof);raise RuntimeError(str(receipt))
    from PIL import Image
    for ppm in capture.glob('*.ppm'):
        with Image.open(ppm) as im:im.save(ppm.with_suffix('.png'));im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST).save(ppm.with_name(ppm.stem+'_2x.png'))
    proof['media']={p.name:sha(p) for p in capture.iterdir() if p.is_file()};save(receipt,proof)
    print(receipt)
if __name__=='__main__':main()
