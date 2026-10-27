#!/usr/bin/env python3
"""Focused REC dictaphone + direct PLAY crop proof/capture from current C++, ASAN/UBSAN, no inherited suite.

Reuses only established actual-main/raster/texture-boundary scaffolding. The old
fullscreen/filter test mains are renamed and never called. No ARM or FTP here.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import test_editor_fullscreen_ui as host
from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources as common_sources
OUT=ROOT/'release/rec-dictaphone'
PROOF=OUT/'ui-proof.json'
OWN=('tests/test_rec_dictaphone_ui.py','tests/rec_dictaphone_ui_cases.inc','tools/generate_rec_type.py',
     'tests/test_editor_fullscreen_ui.py','tests/editor_fullscreen_ui_cases.inc','tests/filter_interaction_ui_cases.inc',
     'tests/paper_motion_test_stubs/citro2d.h')
sha=host.sha

def sources():
    result=common_sources()
    for p in OWN:result[p]=sha(ROOT/p)
    return dict(sorted(result.items()))

def verify(proof):
    assert sources()==proof['inputs'],'sources changed'
    assert sha(proof['binary'])==proof['binary_sha256']
    for p,h in proof['dependencies'].items():assert sha(p)==h,p

def save(proof,stamp):
    text=json.dumps(proof,indent=2)+'\n';PROOF.write_text(text);(OUT/('ui-receipt-'+stamp+'.json')).write_text(text)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build',action='store_true');parser.add_argument('--build-only',action='store_true')
    parser.add_argument('--capture-only',action='store_true');args=parser.parse_args()
    OUT.mkdir(exist_ok=True);stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ');before=sources()
    if args.no_build or args.capture_only:
        proof=json.loads(PROOF.read_text());verify(proof);binary=Path(proof['binary'])
    else:
        directory=Path(tempfile.mkdtemp(prefix='seance-rec-dictaphone-'));driver=host.driver(directory)
        text=driver.read_text();entry='int main(int argc,char**argv){';assert text.count(entry)==1
        text=text.replace(entry,'int unused_gran_fullscreen_main(int argc,char**argv){')+'\n'+(ROOT/OWN[1]).read_text()
        text=text.replace('#include "../','#include "'+str(ROOT)+'/');driver.write_text(text)
        units=make_variable('OSC_INPUT_SOURCES');units[units.index('tests/osc_input_dispatch_test.cpp')]=str(driver)
        units.remove('platform/3ds/menu_style.cpp');binary=directory/'rec_dictaphone_ui_san';obj=directory/'menu_style.o'
        flags=['-O0','-g1','-I'+str(directory),*make_variable('SEQUENCER_TEST_FLAGS'),'-DSEANCE_REAL_EDITOR','-Itests',
               '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',*WRAP,'-Wl,--wrap=_ZN6seance2ui4lineEiiiiji']
        compiler=os.environ.get('CXX','g++');commands=[[compiler,*flags,'-D__3DS__','-c','platform/3ds/menu_style.cpp','-o',str(obj)],
                                                   [compiler,*flags,*units,str(obj),'-o',str(binary)]]
        for i,cmd in enumerate(commands):
            status=host.run(cmd,OUT/('ui-build-'+stamp+'-'+str(i)+'.log'))
            if status:raise SystemExit(status)
        assert before==sources(),'source edits during build; rebuild after freeze'
        deps={}
        for extra,src in [([],units),(['-D__3DS__'],['platform/3ds/menu_style.cpp'])]:
            r=subprocess.run([compiler,*[x for x in flags if not x.startswith('-Wl,')],*extra,'-MM',*src],cwd=ROOT,capture_output=True,text=True,check=True)
            for line in r.stdout.replace('\\\n',' ').splitlines():
                if ':' in line:
                    for p in shlex.split(line.split(':',1)[1]):
                        p=(ROOT/p).resolve();deps[str(p)]=sha(p)
        proof=dict(binary=str(binary),binary_sha256=sha(binary),inputs=before,dependencies=deps,commands=commands,
                   checks={'status':'not run'},host_only=True,hardware=False,deployment=False,inherited_suites=0)
        save(proof,stamp)
    if args.build_only:return
    if args.capture_only:
        assert proof['checks']['status']=='passed';dest=ROOT/'ui_mockups/rec_dictaphone_runtime'/('capture-'+stamp)
        log=OUT/('ui-capture-'+stamp+'.log');status=host.run([str(binary),'--capture-only',str(dest)],log)
        if status:raise SystemExit(status)
        from PIL import Image,PngImagePlugin
        for top in sorted(dest.glob('*_top.ppm')):
            name=top.name[:-8]
            with Image.open(top) as a,Image.open(dest/(name+'_bottom.ppm')) as b:
                image=Image.new('RGB',(400,496),(18,18,21));image.paste(a,(0,0));image.paste(b,(40,256))
            large=image.resize((800,992),Image.Resampling.NEAREST);meta=PngImagePlugin.PngInfo();meta.add_text('provenance',__doc__)
            path=dest/(name+'-2x.png');large.save(path,pnginfo=meta)
            with Image.open(path) as decoded:assert decoded.tobytes()==large.tobytes()
        proof['capture']=dict(path=str(dest.relative_to(ROOT)),sha256={p.name:sha(p) for p in sorted(dest.iterdir()) if p.is_file()},log=str(log.relative_to(ROOT)))
    else:
        log=OUT/('ui-check-'+stamp+'.log');status=host.run([str(binary),'--proof-only'],log)
        proof['checks']=dict(status='passed' if status==0 else 'failed',log=str(log.relative_to(ROOT)),sanitizers='ASAN/UBSAN/leaks',exit=status)
        save(proof,stamp)
        if status:raise SystemExit(status)
    verify(proof);save(proof,stamp);print(PROOF)
if __name__=='__main__':main()
