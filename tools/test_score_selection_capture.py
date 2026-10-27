#!/usr/bin/env python3
"""Fresh production capture of approved event plates + compact flow selection.

No cached objects, substituted production TU, deployment or historical rewrites.
The 17-scene frozen study driver runs against current host boundaries. Every
complete raster is compared, including both themes and all upper screens.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

from PIL import Image, PngImagePlugin
import test_fader_lancet_ui as common
import test_lancet_nonmodule as harness
from render_module_geometry_study import protected_hashes

ROOT=Path(__file__).resolve().parents[1]
RELEASE=ROOT/'release/score-selection'
APPROVED=ROOT/'ui_mockups/flow_selection_plate_study/20261005T180733_121280Z'
SOURCE=ROOT/'platform/3ds/ui_sequencer.cpp'


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def save(path,data):path.write_text(json.dumps(data,indent=2,sort_keys=True)+'\n')


def preservation():
    proof=json.loads((APPROVED/'proposal.json').read_text())
    drawing=APPROVED/'native/proposal.cpp'
    assert sha(drawing)==proof['source_hashes']['proposal']
    approved=drawing.read_text().replace('#include "'+str(ROOT)+'/', '#include "../../')
    current=SOURCE.read_text()
    assert common.tokens(current)==common.tokens(approved), 'production differs from approved drawing code'
    before=json.loads((RELEASE/'before.json').read_text())
    baseline=ROOT/before['source_backup'];assert sha(baseline)==before['source_backup_sha256']
    protected=protected_hashes();assert protected.keys()==before['protected'].keys()
    changes=[p for p,h in protected.items() if h!=before['protected'][p]]
    assert changes==['platform/3ds/ui_sequencer.cpp'], changes
    old=baseline.read_text();a,b=common.functions(old),common.functions(current)
    assert not a.keys()-b.keys()
    assert b.keys()-a.keys()=={'rect','text','line','frame','point'}
    resets={'sequencer_forget_pins','sequencer_view_cancel_input','sequencer_view_draw'}
    draws={'cell_label','draw_event_content','draw_event_cell','draw_retrig_dots',
           'draw_flow_lane','draw_extent_marks','draw_score'}
    same,changed=[],[]
    for name in a:
        aa,bb=a[name][2],b[name][2]
        if name in resets:bb=bb.replace('g_step_focus.reset();','')
        if common.tokens(aa)==common.tokens(bb):same.append(name)
        else:
            assert name in draws,(name,'non-drawing function changed')
            changed.append(name)
    outer=[]
    for text,functions in ((old,a),(current,b)):
        text=common.strip_spans(text,[(s,e) for s,e,_ in functions.values()])
        text=re.sub(r'namespace score_ink\s*\{\s*\}','',common.masked(text))
        for decl in ('step_focus::State g_step_focus;','step_focus::Box g_focus_box{};',
                     'bool g_focus_inverse = false;'):text=text.replace(decl,'')
        outer.append(common.tokens(text))
    assert outer[0]==outer[1]
    return dict(changed_production=changes,protected=protected,unchanged_functions=same,
                changed_draws=changed,restored_visual_resets=sorted(resets),
                production_tokens_equal_approved=True,approved_receipt_sha256=sha(APPROVED/'proposal.json'))


def verify(folder, expected):
    records=[]
    for path,expected_sha in expected.items():
        path=Path(path);assert sha(path)==expected_sha
        actual=folder/'capture'/path.name
        assert actual.is_file()
        if path.suffix=='.ppm':
            with Image.open(path) as a,Image.open(actual) as b:
                assert a.mode==b.mode and a.size==b.size and a.tobytes()==b.tobytes(),path.name
                b.save(actual.with_suffix('.png'))
            records.append(dict(name=path.name,pixels_exact=True,reference_sha256=expected_sha))
        else:assert actual.read_bytes()==path.read_bytes(),('scene state changed',path.name)
    assert len(records)==68
    # Fresh presentable duals, copied only from actual production rasters.
    for theme in ('graphite','pink'):
        for scene in ('flow','dice','level','trig','note'):
            top=Image.open(folder/'capture'/f'{theme}_{scene}_top.ppm').convert('RGB')
            bottom=Image.open(folder/'capture'/f'{theme}_{scene}_bottom.ppm').convert('RGB')
            dual=Image.new('RGB',(400,496),(18,18,21));dual.paste(top,(0,0));dual.paste(bottom,(40,256))
            metadata=PngImagePlugin.PngInfo();metadata.add_text('provenance',__doc__)
            for suffix,image in (('dual',dual),('bottom',bottom)):
                image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST).save(
                    folder/f'{theme}-{scene}-{suffix}-2x.png',pnginfo=metadata)
    proof=json.loads((APPROVED/'proposal.json').read_text())
    for entry in proof['outputs']:
        p=ROOT/entry['path'];assert sha(p)==entry['sha256']
        if 'closeup' in p.name:continue
        with Image.open(p) as a,Image.open(folder/p.name) as b:
            assert a.tobytes()==b.tobytes(),('approved displayed image differs',p.name)
    return records


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--sanitize',action='store_true');args=ap.parse_args()
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    folder=RELEASE/(('capture-sanitized-' if args.sanitize else 'capture-optimized-')+stamp)
    folder.mkdir(parents=True,exist_ok=False)
    guard=preservation()
    def inputs():
        paths=[p for d in ('core','platform','tools') for p in (ROOT/d).rglob('*')
               if p.is_file() and p.suffix in ('.cpp','.c','.h','.hpp','.inc','.py')]
        paths += [ROOT/'Makefile']
        return {str(p.relative_to(ROOT)):sha(p) for p in paths}
    input_pins=inputs()
    expected={str(p):sha(p) for p in sorted((APPROVED/'proposal').iterdir()) if p.suffix in ('.ppm','.txt')}
    assert len(expected)==102
    (folder/'runner.py').write_bytes(Path(__file__).read_bytes())
    source=harness.driver(folder)
    header=source.read_text();marker='int main(int argc,char**argv)';assert header.count(marker)==1
    frozen=APPROVED/'native/driver.cpp';proof=json.loads((APPROVED/'proposal.json').read_text())
    assert sha(frozen)==proof['source_hashes']['driver']
    text=frozen.read_text();scene=text[text.index('namespace flow_selection_plate_study {'):]
    source.write_text(header.replace(marker,'int unused_capture_main(int argc,char**argv)')+'\n'+scene)
    units=common.make_variable('OSC_INPUT_SOURCES');units[units.index('tools/osc_input_dispatch_test.cpp')]=str(source)
    units += ['platform/3ds/ui_chains.cpp','platform/3ds/ui_chains_top.cpp','platform/3ds/ui_workspace.cpp']
    flags=['-O1' if args.sanitize else '-O2','-g1','-I'+str(folder),*common.make_variable('SEQUENCER_TEST_FLAGS'),
           '-DSEANCE_REAL_EDITOR','-Itools','-I'+str(ROOT),*common.WRAP]
    if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    compiler=os.environ.get('CXX','g++');objects=[folder/(f'{i:02d}_'+Path(u).stem+'.o') for i,u in enumerate(units)]
    commands=[[compiler,*flags,'-MMD','-MF',str(o.with_suffix('.d')),'-c',u,'-o',str(o)] for o,u in zip(objects,units)]
    result=dict(status='building',guard=guard,commands=commands,fresh_all_objects=True,
                test_inputs=input_pins,expected_inputs=expected,runner_sha256=sha(__file__),
                hardware=False,deployed=False,inherited_suites_run=False)
    receipt=folder/'receipt.json';save(receipt,result)
    def compile_one(item):
        i,cmd=item;rc=harness.run(cmd,folder/f'compile-{i:02d}.log');print('compile',i,rc,flush=True);return rc
    with ThreadPoolExecutor(max_workers=2) as pool:result['compile_exit']=list(pool.map(compile_one,enumerate(commands)))
    save(receipt,result);assert not any(result['compile_exit'])
    assert input_pins==inputs(),'inputs changed during compile; rebuild'
    binary=folder/'score-selection-capture';link=[compiler,*flags,*map(str,objects),'-o',str(binary)]
    assert harness.run(link,folder/'link.log')==0
    env=dict(os.environ)
    if args.sanitize:env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with (folder/'run.log').open('x') as log:
        run=subprocess.run([str(binary),str(folder/'capture')],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    result['run_exit']=run.returncode;save(receipt,result);assert run.returncode==0
    result['rasters']=verify(folder,expected)
    dependencies={}
    for obj in objects:
        for dep in shlex.split(obj.with_suffix('.d').read_text().replace('\\\n',' ').split(':',1)[1]):
            path=(ROOT/dep).resolve();dependencies[str(path)]=sha(path)
    assert input_pins==inputs() and guard==preservation(),'inputs changed during capture'
    assert all(sha(p)==h for p,h in expected.items())
    result.update(status='passed',dependencies=dependencies,link=link,binary_sha256=sha(binary),
                  media={str(p.relative_to(folder)):sha(p) for p in folder.rglob('*') if p.suffix in ('.ppm','.png','.txt')},
                  limits=['native software raster, not hardware/PICA/fps/audio acceptance','focused runtime regressions run separately'])
    save(receipt,result);print(receipt);print('68 complete production rasters exact approved preview')


if __name__=='__main__':main()
