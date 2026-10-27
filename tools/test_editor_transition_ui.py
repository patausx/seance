#!/usr/bin/env python3
"""Focused editor transition integration + current C++ raster; never ARM or deploy.

Private copied boundary links real chains/workspace/editor and exact main slices.
Only menu_style.cpp uses __3DS__ against independent software texture sampling.
No inherited test group runs. Captures require an exact-source successful proof.
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
from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources as common_sources, functions, masked, close_brace
from test_mod_minimal_ui import extract_main

OUT = ROOT / 'release/editor-transition'
PROOF = OUT / 'host-proof.json'
OWN = ('tools/test_editor_transition_ui.py', 'tools/editor_transition_ui_cases.inc',
       'tools/editor_fullscreen_ui_cases.inc', 'tools/test_mod_minimal_ui.py',
       'tools/paper_motion_test_stubs/citro2d.h')
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def sources():
    out = common_sources()
    for name in OWN: out[name] = sha(ROOT / name)
    return dict(sorted(out.items()))
def once(text, old, new=''):
    assert text.count(old) == 1, old
    return text.replace(old, new, 1)
def driver(directory):
    extracted = extract_main(directory)
    main = (ROOT / 'platform/3ds/main.cpp').read_text()
    start = main.index('        if ((down & KEY_B) && !(g_view == View::Sequencer')
    end = close_brace(masked(main), main.index('{', start))
    (directory / 'main_editor_back.inc').write_text(main[start:end] + '\n')
    extracted['main_editor_back.inc'] = sha(directory / 'main_editor_back.inc')
    header = (ROOT / 'tools/sequencer_test_stubs/citro2d.h').read_text()
    texture = (ROOT / 'tools/paper_motion_test_stubs/citro2d.h').read_text().splitlines()
    (directory / 'citro2d.h').write_text(header + '\n' + '\n'.join(texture[6:14] + texture[17:31]) + '\n')
    seq = (ROOT / 'tools/sequencer_test.cpp').read_text()
    for line in ('void workspace_shift_slots_right(int,int,int) {++rec_workspace_shifts;}',
                 'void chains_context_draw_top() {} // real Chains path is exercised by menu_stage1_test.cpp',
                 'const WorkspacePoint& workspace_point(int, int) { static const WorkspacePoint point{}; return point; }',
                 'int workspace_camera_x(int) { return 0; }', 'int workspace_camera_y(int) { return 0; }'):
        seq = once(seq, line)
    seq = seq.replace('#include "../', '#include "' + str(ROOT) + '/')
    (directory / 'sequencer_test.cpp').write_text(seq)
    text = (ROOT / 'tools/osc_input_dispatch_test.cpp').read_text()
    text = once(text, 'bool chains_picker_background() { return false; }')
    text = once(text, 'void workspace_move_slot(int,int,int) { CHECK(false); }')
    for name, args, params in (('picker_touch_down','int x,int y','x,y'), ('picker_touch_hold','int x,int y','x,y'),
            ('picker_touch_release','',''), ('chains_view_touch_down','int x,int y','x,y'),
            ('chains_view_touch_hold','int x,int y','x,y'), ('chains_view_touch_release','bool commit','commit'), ('picker_open','','')):
        found = re.findall(r'^void ' + name + r'\([^\n]*\) \{[^\n]*\}$', text, re.M)
        assert len(found) == 1, (name, found)
        text = once(text, found[0], 'void %s(%s) { seance::platform::ui3ds::%s(%s); }' % (name,args,name,params))
    text = once(text, 'bool nav_is_open() { return nav_opened; }',
                'bool nav_is_open() { return seance::platform::ui3ds::nav_is_open(); }')
    text = once(text, 'int main(int argc,char** argv) {', 'int unused_transition_original_main(int argc,char** argv) {')
    # Use exact production adapters instead of the historical lock-only boundary.
    adapters=[]
    for ret,name in [('bool','seq_record_lock'),('q15','seq_display_value')]:
        found=re.search(r'^'+ret+' '+name+r'\([^\n]+\) \{.*?^}',main,re.M|re.S); assert found
        adapters.append(found.group())
    pre=''.join('#include "'+str(ROOT/'platform/3ds'/name)+'"\n' for name in
                ('ui_chains.h','ui_picker.h','ui_nav.h','editor_transition.h','editor_style.h','chain_columns.h'))
    text=pre+'#define seq_record_lock transition_legacy_lock\n#define seq_display_value transition_legacy_value\n'+text
    text+='\n#undef seq_record_lock\n#undef seq_display_value\nnamespace seance::platform::ui3ds {\n'+'\n'.join(adapters)+'\n}\n'
    # Reuse ONLY the independent GPU boundary, never fullscreen cases/main.
    gpu=(ROOT/'tools/editor_fullscreen_ui_cases.inc').read_text().split('namespace editor_fullscreen_cases {')[0]
    text+='\n'+gpu+'\n'+(ROOT/OWN[1]).read_text()
    text=text.replace('#include "../','#include "'+str(ROOT)+'/')
    path=directory/'driver.cpp';path.write_text(text)
    return path,extracted

def run(cmd, log, timeout=300):
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with log.open('x') as stream:
        stream.write('$ '+shlex.join(map(str,cmd))+'\n');stream.flush()
        result=subprocess.run(cmd,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
    print(log.read_text()[-18000:],flush=True)
    return result.returncode

def guards():
    defs=functions((ROOT/'platform/3ds/ui_chains.cpp').read_text())
    body=masked(defs['chains_editor_draw'][2])
    assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|malloc|calloc|realloc|new|delete|C3D_TexInit|C3D_TexFlush)\b',body)
    main=(ROOT/'platform/3ds/main.cpp').read_text()
    assert main.index('chains_editor_input(')<main.index('const View input_view = g_view;')
    assert main.index('chains_editor_capture();')<main.index('        chains_capture();')
    return {'frozen_draw_sha256':hashlib.sha256(body.encode()).hexdigest()}
def save(proof, stamp):
    text=json.dumps(proof,indent=2)+'\n';PROOF.write_text(text);(OUT/('receipt-'+stamp+'.json')).write_text(text)
def verify(proof):
    assert sources()==proof['source_sha256'],'source inputs changed'
    assert sha(proof['binary'])==proof['binary_sha256'],'binary changed'
    for p,h in proof['dependencies'].items(): assert sha(p)==h,p

def package(destination):
    from PIL import Image, PngImagePlugin, ImageChops, ImageStat
    pairs={};media={}
    for top in sorted(destination.glob('*_top.ppm')):
        name=top.name[:-8]
        with Image.open(top) as a,Image.open(destination/(name+'_bottom.ppm')) as b:
            assert a.size==(400,240) and b.size==(320,240)
            dual=Image.new('RGB',(400,496),(18,18,21));dual.paste(a,(0,0));dual.paste(b,(40,256))
        pairs[name]=dual
    assert len(pairs)==22,len(pairs)
    timing=[('closed',400),('open-000',40),('open-040',40),('open-080',40),('open-120',40),('open-160',40),('open-200',1000),
            ('close-040',40),('close-080',40),('close-120',40),('close-160',500)]
    gifs={}
    for theme in ('graphite','pink'):
        selected=[pairs[theme+'_'+name] for name,_ in timing]
        for name in ('closed','open-080','open-200'):
            out=destination/(theme+'_'+name+'-2x.png');image=pairs[theme+'_'+name].resize((800,992),Image.Resampling.NEAREST)
            meta=PngImagePlugin.PngInfo();meta.add_text('provenance','current C++ host raster + independent texture sampler; NN2x only; not hardware')
            image.save(out,pnginfo=meta)
            with Image.open(out) as decoded: assert decoded.convert('RGB').tobytes()==image.tobytes()
        contact=Image.new('RGB',(2400,1000),(18,18,21))
        for i,image in enumerate(selected):contact.paste(image,((i%6)*400,(i//6)*504))
        contact.save(destination/(theme+'-phases.png'))
        # One global no-dither palette. Account explicitly for coalesced frames.
        atlas=Image.new('RGB',(400,496*len(selected)))
        for i,image in enumerate(selected):atlas.paste(image,(0,496*i))
        pal=atlas.quantize(colors=256,dither=Image.Dither.NONE)
        quant=[image.quantize(palette=pal,dither=Image.Dither.NONE).resize((800,992),Image.Resampling.NEAREST) for image in selected]
        coalesced=[]
        for image,(_,ms) in zip(quant,timing):
            if coalesced and image.tobytes()==coalesced[-1][0].tobytes(): coalesced[-1][1]+=ms
            else: coalesced.append([image,ms])
        out=destination/(theme+'-open-close-2x.gif')
        coalesced[0][0].save(out,save_all=True,append_images=[p[0] for p in coalesced[1:]],duration=[p[1] for p in coalesced],loop=0,optimize=False,disposal=1)
        with Image.open(out) as decoded:
            assert decoded.n_frames==len(coalesced)
            for i,(image,ms) in enumerate(coalesced):
                decoded.seek(i);assert decoded.convert('RGB').tobytes()==image.convert('RGB').tobytes();assert decoded.info['duration']==ms
        diffs=[ImageChops.difference(a.resize((800,992),Image.Resampling.NEAREST),b.convert('RGB')) for a,b in zip(selected,quant)]
        gifs[theme]={'source_frames':len(selected),'encoded_frames':len(coalesced),'duration_ms':sum(ms for _,ms in timing),
                     'maximum_channel_error':max(max(v[1] for v in d.getextrema()) for d in diffs),
                     'worst_mean_channel_error':max(sum(ImageStat.Stat(d).mean)/3 for d in diffs)}
    for p in sorted(destination.glob('*')):media[p.name]=sha(p)
    return {'path':str(destination),'sha256':media,'native_pairs':len(pairs),'gif_quantization':gifs}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build',action='store_true');parser.add_argument('--build-only',action='store_true');parser.add_argument('--capture-only',action='store_true')
    args=parser.parse_args();OUT.mkdir(exist_ok=True,parents=True)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ');before=sources();checks=guards()
    if args.no_build or args.capture_only:
        proof=json.loads(PROOF.read_text());verify(proof);binary=Path(proof['binary'])
    else:
        directory=Path(tempfile.mkdtemp(prefix='seance-editor-transition-'));path,extracted=driver(directory)
        units=make_variable('OSC_INPUT_SOURCES');units[units.index('tools/osc_input_dispatch_test.cpp')]=str(path);units.remove('platform/3ds/menu_style.cpp')
        units+=['platform/3ds/ui_chains.cpp','platform/3ds/ui_chains_top.cpp','platform/3ds/ui_workspace.cpp']
        compiler=os.environ.get('CXX','g++');binary=directory/'editor_transition_san';obj=directory/'menu_style.o'
        flags=['-O1','-g1','-I'+str(directory),*make_variable('SEQUENCER_TEST_FLAGS'),'-DSEANCE_REAL_EDITOR','-Itools','-I.',
               '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',*WRAP,
               '-Wl,--wrap=_ZN6seance8platform5ui3ds20editor_touch_releaseEv']
        commands=[[compiler,*flags,'-D__3DS__','-c','platform/3ds/menu_style.cpp','-o',str(obj)],
                  [compiler,*flags,*units,str(obj),'-o',str(binary)]]
        proof=dict(source_sha256=before,binary=str(binary),build_commands=commands,extracted_main_sha256=extracted,guards=checks,
                   inherited_groups_run=0,host_only=True,deployment=False,checks={'status':'not run'})
        for i,cmd in enumerate(commands):
            log=OUT/('build-'+stamp+'-'+str(i)+'.log');status=run(cmd,log)
            proof['build']={'exit':status,'log':str(log.relative_to(ROOT))};save(proof,stamp)
            if status:raise SystemExit(status)
        assert sources()==before,'sources changed during build';deps={}
        for extra,src in [([],units),(['-D__3DS__'],['platform/3ds/menu_style.cpp'])]:
            result=subprocess.run([compiler,*[f for f in flags if not f.startswith('-Wl,')],*extra,'-MM',*src],cwd=ROOT,capture_output=True,text=True,check=True,timeout=120)
            for line in result.stdout.replace('\\\n',' ').splitlines():
                if ':' in line:
                    for p in shlex.split(line.split(':',1)[1]):p=(ROOT/p).resolve();deps[str(p)]=sha(p)
        proof['dependencies']=dict(sorted(deps.items()));proof['binary_sha256']=sha(binary);verify(proof);save(proof,stamp)
    if args.build_only:return
    if args.capture_only:
        assert proof['checks']['status']=='passed'
        destination=ROOT/'ui_mockups/editor_transition_runtime'/('capture-'+stamp)
        log=OUT/('capture-'+stamp+'.log');status=run([str(binary),'--capture-only',str(destination)],log)
        if status:raise SystemExit(status)
        proof['capture']=package(destination);proof['capture']['log']=str(log.relative_to(ROOT))
    else:
        log=OUT/('check-'+stamp+'.log');status=run([str(binary),'--proof-only'],log)
        proof['checks']={'status':'passed' if status==0 else 'failed','exit':status,'log':str(log.relative_to(ROOT)),'sanitizer':'ASAN/UBSAN/leaks'}
        if not status:proof['checks']['summary']=next(line for line in log.read_text().splitlines() if line.startswith('editor transition:'))
        save(proof,stamp)
        if status:raise SystemExit(status)
    verify(proof);save(proof,stamp);print(PROOF)
if __name__=='__main__':main()
