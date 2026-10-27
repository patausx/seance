#!/usr/bin/env python3
"""Approved fullscreen v3: real controllers/drawing + software Citro2D texture boundary.

Only menu_style.cpp is compiled with __3DS__, against API-shaped host stubs.
Texture decode/filtering is independent host emulation, not a hardware/FPS claim.
Other translation units use the existing native rect/font raster. No deployment.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources as common_sources, functions, tokens
from test_mod_minimal_ui import extract_main

OUT=ROOT/'release/editor-fullscreen'
PROOF=OUT/'host-proof.json'
BASE=ROOT/'.local-state/backups/20261003_111301_fullscreen-v3-source-baseline_3ds'
FILTER=ROOT/'.local-state/backups/20261003_111236_before-fullscreen-v3-filter_ui_module_focus_filter.cpp'
OWN=['tests/test_editor_fullscreen_ui.py','tests/editor_fullscreen_ui_cases.inc','tests/filter_interaction_ui_cases.inc','tests/test_mod_minimal_ui.py','tests/paper_motion_test_stubs/citro2d.h']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def sources():
    out=common_sources()
    for name in OWN:out[name]=sha(ROOT/name)
    return dict(sorted(out.items()))
def preservation():
    baseline=json.loads((OUT/'baseline.json').read_text());changed=[]
    for name,digest in baseline.items():
        assert (ROOT/name).exists(),name
        if sha(ROOT/name)!=digest:
            assert name.startswith('platform/3ds/'),name
            changed.append(name)
    result={}
    # Controllers, value laws, hit boxes, model captures remain byte/token exact.
    for name in changed:
        p=ROOT/name;old=FILTER if p.name=='ui_module_focus_filter.cpp' else BASE/p.name
        assert old.exists(),name
        if p.suffix!='.cpp' or p.name in {'main.cpp','menu_style.cpp'}:continue
        before,after=functions(old.read_text()),functions(p.read_text())
        for fn,(_,_,body) in before.items():
            if any(key in fn for key in ('touch','drag','release','capture','select','back','input','context','owns','edit_value','command','cancel')):
                assert fn in after and tokens(body)==tokens(after[fn][2]),(name,fn)
                result[name+':'+fn]=sha_text(' '.join(tokens(body)))
    for name in ('ui_editor.cpp','ui_param_lens.cpp','filter_interaction.h','ui_chains.cpp','ui_picker.cpp'):
        assert sha(ROOT/'platform/3ds'/name)==baseline['platform/3ds/'+name],name
    return dict(changed=changed,preserved_controllers=result,baseline_sha256=sha(OUT/'baseline.json'))
def sha_text(s):return hashlib.sha256(s.encode()).hexdigest()
def once(text,a,b):
    assert text.count(a)==1,a
    return text.replace(a,b,1)
def driver(directory):
    extract_main(directory)
    # Union of established scalar and texture boundary stubs; no production API changes.
    header=(ROOT/'tests/sequencer_test_stubs/citro2d.h').read_text()
    texture=(ROOT/'tests/paper_motion_test_stubs/citro2d.h').read_text().splitlines()
    header+='\n'+'\n'.join(texture[6:14]+texture[17:31])+'\n'
    (directory/'citro2d.h').write_text(header)
    original=(ROOT/'tests/osc_input_dispatch_test.cpp').read_text()
    original=once(original,'int main(int argc,char** argv) {','int unused_fullscreen_main(int argc,char** argv) {')
    adapters=[];main=(ROOT/'platform/3ds/main.cpp').read_text()
    for ret,name in [('bool','seq_record_lock'),('q15','seq_display_value')]:
        found=re.search(r'^'+ret+' '+name+r'\([^\n]+\) \{.*?^}',main,re.M|re.S);assert found
        adapters.append(found.group())
    original='#define seq_record_lock fullscreen_legacy_lock\n#define seq_display_value fullscreen_legacy_value\n'+original
    original+='\n#undef seq_record_lock\n#undef seq_display_value\nnamespace seance::platform::ui3ds {\n'+'\n'.join(adapters)+'\n}\n'
    case=(ROOT/'tests/filter_interaction_ui_cases.inc').read_text()
    case=once(case,'int main(int argc,char**argv){','int unused_fullscreen_filter_main(int argc,char**argv){')
    case=case.replace('palette::set(theme);Scene s;','palette::set(theme);editor_style::capture();editor_gpu::begin();Scene s;')
    original+='\n#include "../platform/3ds/editor_style.h"\nnamespace editor_gpu { void begin(); }\n'+case
    original+='\n'+(ROOT/'tests/editor_fullscreen_ui_cases.inc').read_text()
    original=original.replace('#include "../','#include "'+str(ROOT)+'/')
    path=directory/'driver.cpp';path.write_text(original);return path

def run(cmd,log,timeout=300):
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with log.open('x') as f:
        f.write('$ '+shlex.join(map(str,cmd))+'\n');f.flush()
        r=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
    print(log.read_text()[-18000:],flush=True)
    return r.returncode

def verify(proof):
    assert sources()==proof['source_sha256'],'sources changed'
    assert sha(proof['binary'])==proof['binary_sha256']
    for p,h in proof['dependencies'].items():assert sha(p)==h,p

def save(proof,stamp):
    text=json.dumps(proof,indent=2)+'\n';PROOF.write_text(text);(OUT/('receipt-'+stamp+'.json')).write_text(text)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build',action='store_true');parser.add_argument('--build-only',action='store_true')
    parser.add_argument('--capture-only',action='store_true');args=parser.parse_args()
    OUT.mkdir(exist_ok=True,parents=True);stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    before=sources();guards=preservation()
    if args.no_build or args.capture_only:
        proof=json.loads(PROOF.read_text());verify(proof);binary=Path(proof['binary'])
    else:
        directory=Path(tempfile.mkdtemp(prefix='seance-editor-fullscreen-'));path=driver(directory)
        units=make_variable('OSC_INPUT_SOURCES');units[units.index('tests/osc_input_dispatch_test.cpp')]=str(path)
        units.remove('platform/3ds/menu_style.cpp');binary=directory/'editor_fullscreen_san'
        flags=['-O1','-g1','-I'+str(directory),*make_variable('SEQUENCER_TEST_FLAGS'),'-DSEANCE_REAL_EDITOR','-Itests','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',*WRAP]
        compiler=os.environ.get('CXX','g++');obj=directory/'menu_style.o'
        commands=[[compiler,*flags,'-D__3DS__','-c','platform/3ds/menu_style.cpp','-o',str(obj)],
                  [compiler,*flags,*units,str(obj),'-o',str(binary)]]
        for i,cmd in enumerate(commands):
            status=run(cmd,OUT/('build-'+stamp+'-'+str(i)+'.log'))
            if status:raise SystemExit(status)
        assert sources()==before
        deps={}
        for extra,src in [([],units),(['-D__3DS__'],['platform/3ds/menu_style.cpp'])]:
            result=subprocess.run([compiler,*[f for f in flags if not f.startswith('-Wl,')],*extra,'-MM',*src],cwd=ROOT,capture_output=True,text=True,check=True)
            for line in result.stdout.replace('\\\n',' ').splitlines():
                if ':' in line:
                    for p in shlex.split(line.split(':',1)[1]):
                        p=(ROOT/p).resolve();deps[str(p)]=sha(p)
        proof=dict(binary=str(binary),binary_sha256=sha(binary),source_sha256=before,dependencies=deps,build_commands=commands,
                   preservation=guards,checks={'status':'not run'},hardware=False,deployment=False)
        save(proof,stamp)
    if args.build_only:return
    if args.capture_only:
        assert proof['checks']['status']=='passed';destination=ROOT/'ui_mockups/editor_fullscreen_runtime'/('capture-'+stamp)
        log=OUT/('capture-'+stamp+'.log');status=run([str(binary),'--capture-only',str(destination)],log)
        if status:raise SystemExit(status)
        from PIL import Image,PngImagePlugin
        media={};pairs=[]
        for top in sorted(destination.glob('*_top.ppm')):
            name=top.name[:-8];bottom=destination/(name+'_bottom.ppm')
            with Image.open(top) as a,Image.open(bottom) as b:
                pair=Image.new('RGB',(400,496),(18,18,21));pair.paste(a,(0,0));pair.paste(b,(40,256))
            large=pair.resize((800,992),Image.Resampling.NEAREST);out=destination/(name+'-2x.png')
            meta=PngImagePlugin.PngInfo();meta.add_text('provenance',__doc__);large.save(out,pnginfo=meta)
            with Image.open(out) as check:assert check.tobytes()==large.tobytes()
            pairs.append((name,pair))
        assert len(pairs)==56
        for theme in ('graphite','pink'):
            chosen=[(n,p) for n,p in pairs if n.startswith(theme)]
            for start in range(0,28,7):
                board=Image.new('RGB',(1600,1020),(18,18,21))
                for i,(name,pair) in enumerate(chosen[start:start+7]):board.paste(pair,((i%4)*400,(i//4)*510))
                board.save(destination/(theme+'-review-'+str(start//7)+'.png'))
        for p in sorted(destination.glob('*')):media[p.name]=sha(p)
        proof['capture']=dict(path=str(destination),sha256=media,log=str(log.relative_to(ROOT)),native_pairs=len(pairs))
    else:
        log=OUT/('check-'+stamp+'.log');status=run([str(binary),'--proof-only'],log,300)
        proof['checks']=dict(status='passed' if status==0 else 'failed',exit=status,log=str(log.relative_to(ROOT)))
        save(proof,stamp)
        if status:raise SystemExit(status)
    verify(proof);save(proof,stamp);print(PROOF)
if __name__=='__main__':main()
