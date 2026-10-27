#!/usr/bin/env python3
"""Fresh native module rand UI proof. No inherited suite, network or deployment."""
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
import sys
sys.dont_write_bytecode = True
from PIL import Image
import test_fader_lancet_ui as common
import test_lancet_nonmodule as harness
from render_module_geometry_study import protected_hashes

ROOT = Path(__file__).resolve().parents[1]
CASES = ROOT/'tools/module_rand_ui_cases.inc'
BASE = ROOT/'release/module-rand/before-20261005T215806Z/platform/3ds/ui_editor.cpp'
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,v): p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def run(cmd,log):
    with log.open('x') as out:
        out.write('$ '+shlex.join(map(str,cmd))+'\n');out.flush()
        return subprocess.run(cmd,cwd=ROOT,stdout=out,stderr=subprocess.STDOUT,timeout=600,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')).returncode

def driver(folder):
    source=harness.driver(folder)
    text=source.read_text();marker='int main(int argc,char**argv)'
    assert text.count(marker)==1
    texture=(ROOT/'tools/editor_fullscreen_ui_cases.inc').read_text().split('namespace editor_fullscreen_cases {',1)[0]
    texture=texture.replace('#include "../','#include "'+str(ROOT)+'/')
    header=(ROOT/'tools/sequencer_test_stubs/citro2d.h').read_text()
    extra=(ROOT/'tools/paper_motion_test_stubs/citro2d.h').read_text().splitlines()
    (folder/'citro2d.h').write_text(header+'\n'+'\n'.join(extra[6:14]+extra[17:31])+'\n')
    main=(ROOT/'platform/3ds/main.cpp').read_text()
    start=main.index('            if (g_eng->module_random_revision()!=saved_random_revision) {')
    end=main.index('            g_project_session.mark_saved(slot);',start)+len('            g_project_session.mark_saved(slot);')
    (folder/'rand_save_commit.inc').write_text(main[start:end]+'\n')
    loop=main.rindex('while (aptMainLoop())') # runtime, not the earlier startup menu loop
    assert main.index('module_rand_poll();',loop) < main.index('hidScanInput();',loop)
    assert main.count('            module_rand_rebase();')==2
    source.write_text(text.replace(marker,'int unused_nonmodule_main(int argc,char**argv)')+'\n'+texture+'\n'+CASES.read_text())
    old=BASE.read_text();funcs=common.functions(old)
    before=json.loads((BASE.parents[2]/'manifest.json').read_text())
    assert sha(BASE)==before['platform/3ds/ui_editor.cpp'], 'pre-rand editor backup changed'
    includes='\n'.join(line for line in old.splitlines() if line.startswith('#include'))
    baseline=folder/'baseline_draw.cpp'
    baseline.write_text(includes+'\nnamespace seance::platform::ui3ds {\nnamespace {\n'+funcs['draw_module_editor'][2]+
        '\n}\n'+funcs['editor_draw'][2].replace('void editor_draw()', 'void rand_baseline_editor_draw()')+'\n}\n')
    return source,baseline

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--sanitized',action='store_true');ap.add_argument('--exploratory',action='store_true');a=ap.parse_args()
    tag='sanitized' if a.sanitized else 'optimized'
    folder=ROOT/'release/module-rand'/('ui-'+tag+'-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ'))
    folder.mkdir(parents=True);before=protected_hashes();source,baseline=driver(folder)
    shutil.copyfile(__file__,folder/'runner.py');shutil.copyfile(CASES,folder/'cases.inc')
    units=common.make_variable('OSC_INPUT_SOURCES');units[units.index('tools/osc_input_dispatch_test.cpp')]=str(source)
    units+=['platform/3ds/ui_chains.cpp','platform/3ds/ui_chains_top.cpp','platform/3ds/ui_workspace.cpp',str(baseline)]
    compiler=os.environ.get('CXX','g++')
    flags=['-O1' if a.sanitized else '-O2','-g1','-I'+str(folder),*common.make_variable('SEQUENCER_TEST_FLAGS'),'-DSEANCE_REAL_EDITOR','-Itools','-I'+str(ROOT),'-I'+str(ROOT/'platform/3ds'),*common.WRAP]
    if a.sanitized: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    commands=[];objects=[]
    for i,u in enumerate(units):
        obj=folder/(f'{i:02d}_'+Path(u).stem+'.o');objects.append(obj)
        commands.append([compiler,*flags,*(['-D__3DS__'] if u=='platform/3ds/menu_style.cpp' else []),'-MMD','-MF',str(obj.with_suffix('.d')),'-c',u,'-o',str(obj)])
    proof=dict(status='building',source_before=before,commands=commands,fresh_objects=len(objects),sanitized=a.sanitized,
               baseline_sha256=sha(BASE),hardware=False,deployment=False,inherited_suites_run=False,exploratory=a.exploratory)
    receipt=folder/'receipt.json';save(receipt,proof)
    try:
        def build(item):
            i,cmd=item;code=run(cmd,folder/f'compile-{i:02d}.log');print('compile',i,code,flush=True);return code
        with ThreadPoolExecutor(max_workers=2) as pool: proof['compile_exit']=list(pool.map(build,enumerate(commands)))
        assert not any(proof['compile_exit']), 'compile failed'
        binary=folder/'module_rand_ui';link=[compiler,*flags,*map(str,objects),'-o',str(binary)]
        proof['link_command']=link;proof['link_exit']=run(link,folder/'link.log');assert not proof['link_exit'],'link failed'
        deps={}
        for obj in objects:
            for dep in shlex.split(obj.with_suffix('.d').read_text().replace('\\\n',' ').split(':',1)[1]):
                p=(ROOT/dep).resolve();deps[str(p)]=sha(p)
        proof['dependencies']=deps;proof['binary_sha256']=sha(binary)
        proof['run_exit']=run([str(binary),str(folder/'capture')],folder/'run.log');print((folder/'run.log').read_text()[-12000:],flush=True)
        proof['source_after']=protected_hashes();proof['source_drift']=[p for p,h in before.items() if proof['source_after'].get(p)!=h]
        assert not proof['run_exit'],'run failed'
        if not a.exploratory: assert not proof['source_drift'],'source drift during build/run'
        retained={}
        for name,digest in deps.items():
            p=Path(name)
            if p.is_relative_to(ROOT) and not p.is_relative_to(folder):
                dest=folder/'inputs'/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
                assert sha(dest)==digest;retained[str(p.relative_to(ROOT))]=digest
        proof['retained_dependencies']=retained
        ppms=list((folder/'capture').glob('*.ppm'))
        for ppm in ppms:
            with Image.open(ppm) as im:
                im.load();im.save(ppm.with_suffix('.png'))
                with Image.open(ppm.with_suffix('.png')) as restored: assert restored.tobytes()==im.tobytes()
                im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST).save(ppm.with_name(ppm.stem+'-2x.png'))
        proof.update(status='passed',surfaces=len(ppms),png_roundtrips_exact=len(ppms))
    except BaseException as error:
        proof.update(status='failed',error=str(error));raise
    finally:
        proof['evidence_sha256']={str(p.relative_to(folder)):sha(p) for p in sorted(folder.rglob('*')) if p.is_file() and p!=receipt}
        save(receipt,proof);print(receipt,flush=True)
if __name__=='__main__':main()
