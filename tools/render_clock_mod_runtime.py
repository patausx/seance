#!/usr/bin/env python3
"""Audit real clock-mod C++ paired frames; no Python UI drawing or synthetic live values."""
import argparse
import csv
import hashlib
import json
import os
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageChops
from render_nav_clock_runtime import native, save

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary',type=Path,default=Path('/tmp/seance_clock_mod_ui_test'))
    ap.add_argument('--out',type=Path,default=ROOT/'ui_mockups/clock_mod_runtime')
    ap.add_argument('--compare-dir',type=Path)
    a=ap.parse_args();out=a.out.resolve()
    assert out not in ((ROOT/'ui_mockups/nav_clock_runtime').resolve(), (ROOT/'ui_mockups/project_clock_study').resolve())
    out.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env.setdefault('ASAN_OPTIONS','detect_leaks=1:halt_on_error=1');env.setdefault('UBSAN_OPTIONS','halt_on_error=1:print_stacktrace=1')
    subprocess.run([str(a.binary.resolve()),'--dump-clock-mod',str(out)],cwd=ROOT,env=env,check=True)
    subprocess.run([str(a.binary.resolve()),'--dump',str(out/'legacy_menu')],cwd=ROOT,env=env,check=True)
    rows=list(csv.DictReader((out/'frames.tsv').open(),delimiter='\t'))
    texts=list(csv.DictReader((out/'raw_text.tsv').open(),delimiter='\t'));groups={}
    for r in texts:
        x,y,w,h=[int(r[k]) for k in ('x','y','w','h')];width=400 if r['screen']=='top' else 320
        assert x>=0 and y>=0 and x+w<=width and y+h<=240,r
        for ox,oy,ow,oh in groups.setdefault((r['name'],r['screen']),[]):
            assert not (x<ox+ow and ox<x+w and y<oy+oh and oy<y+h),r
        groups[(r['name'],r['screen'])].append((x,y,w,h))
    cards=[]
    for r in rows:
        assert int(r['out_of_bounds'])==0 and int(r['top_objects'])+int(r['bottom_objects'])<8192
        pair=Image.new('RGB',(400,496),(24,20,29))
        for screen,w,x,y in (('top',400,0,0),('bottom',320,40,256)):
            im=native(out/(r['name']+'_'+screen+'.ppm'),w);save(im,out/(r['name']+'_'+screen));pair.paste(im,(x,y))
        save(pair,out/(r['name']+'_dual'));cards.append(pair)
    sheet=Image.new('RGB',(816,((len(cards)+1)//2)*524),(24,20,29));d=ImageDraw.Draw(sheet)
    for i,im in enumerate(cards):
        x,y=8+(i%2)*400,(i//2)*524;d.text((x,y+4),rows[i]['name'],fill=(228,194,207));sheet.paste(im,(x,y+24))
    save(sheet,out/'comparison')
    # Both real stored route letters, header geometry matches accepted proposal.
    header=Image.open(out/'05_header_both_bottom.ppm').convert('RGB')
    approved=Image.open(ROOT/'ui_mockups/go_to_clock_refinement/02_branch_native.png').convert('RGB')
    # Focus ink may differ, numbers/branch positions remain the same.
    assert header.crop((296,10,308,23)).tobytes()==approved.crop((296,10,308,23)).tobytes()
    assert header.crop((296,36,308,49)).tobytes()==approved.crop((296,36,308,49)).tobytes()
    legacy=0
    for p in (ROOT/'ui_mockups/nav_clock_runtime/old_menu_regression').glob('*.ppm'):
        assert p.read_bytes()==(out/'legacy_menu'/p.name).read_bytes(),p.name
        legacy+=1
    exact=0
    if a.compare_dir:
        for p in out.rglob('*.ppm'):
            assert p.read_bytes()==(a.compare_dir/p.relative_to(out)).read_bytes(),p.name
            exact+=1
        assert (out/'raw_text.tsv').read_bytes()==(a.compare_dir/'raw_text.tsv').read_bytes()
    paths=['platform/3ds/ui_nav.cpp','platform/3ds/ui_nav.h','platform/3ds/main.cpp','platform/3ds/ui_sigils.cpp',
           'core/engine/engine.cpp','core/engine/engine.h','core/engine/clock_mod.h','core/engine/sigil.h',
           'core/engine/units.cpp','core/engine/units.h','core/engine/patch_io.cpp','platform/3ds/menu_labels_data.h',
           'tools/clock_mod_ui_test.h','tools/menu_stage1_test.cpp','tools/render_clock_mod_runtime.py','Makefile']
    report={'status':'real C++ host captures; no device upload or hardware acceptance',
            'pairs':len(rows),'raw_text_records':len(texts),'raw_overlap_and_outside':0,
            'legacy_menu_ppms_exact':legacy,'sanitized_ppms_exact':exact,
            'peak_paired':max(int(r['top_objects'])+int(r['bottom_objects']) for r in rows),
            'applied_vs_target':'upper inspector reads actual engine ClockInspector; lower pending is only draft',
            'source_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths},
            'limits':['single-thread engine capture, not audio worker concurrency','software raster, not Citro2D LCD timing','main top dispatch statically integrated, host draws actual nav_clock_draw_top directly']}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'pass {len(rows)} pairs; {len(texts)} raw texts; {legacy} legacy exact; {exact} sanitizer PPMs exact')
    print(out/'comparison_2x.png')

if __name__=='__main__':main()
