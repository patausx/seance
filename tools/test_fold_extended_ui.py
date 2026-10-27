#!/usr/bin/env python3
"""Current Fold-only actual native host capture; no inherited suites/ARM/device."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
sys.dont_write_bytecode=True
import test_module_headings_ui as host
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'release/fold-extended/ui'
OWN=ROOT/'tools/fold_extended_ui_cases.inc'
HEADER=ROOT/'tools/module_headings_ui_cases.inc'
LEGACY=ROOT/'tools/fold_ui_test.h'

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def configure():
    OUT.mkdir(parents=True,exist_ok=True)
    text=HEADER.read_text().replace('int main(', 'int unused_fold_header_main(',1)+'\n'+OWN.read_text()
    path=OUT/('cases-'+hashlib.sha256(text.encode()).hexdigest()[:16]+'.inc')
    if path.exists():assert path.read_text()==text
    else:path.write_text(text)
    host.OUT,host.CASES=OUT,path
    for symbol in ('_ZN6seance8FoldUnit7processEPsi','_ZN6seance8FoldUnit8transferEfPKs'):
        flag='-Wl,--wrap='+symbol
        if flag not in host.common.WRAP:host.common.WRAP.append(flag)
    host.supplement=lambda:{}

def draw_boundary():
    path=ROOT/'platform/3ds/ui_module_focus_fold.cpp';functions=host.common.functions(path.read_text())
    seen=set()
    def visit(name):
        if name in seen:return
        seen.add(name);body=host.common.masked(functions[name][2]);body=body[body.index('{'):]
        assert not re.search(r'\b(?:g_eng|g_audio|param_lens_is_open|osGetTime|malloc|calloc|realloc|new|delete|process|transfer|control_value|capture_plot)\b',body),(name,'nonfrozen draw')
        for other in functions:
            if re.search(r'\b'+re.escape(other)+r'\s*\(',body):visit(other)
    visit('fold_focus_draw');visit('fold_focus_draw_top')
    return {'production_sha256':sha(path),'frozen_draw_graph':sorted(seen)}

def exact_titles(proof):
    text=(ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw=bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',text.split('bitmap[] = {',1)[1].split('};',1)[0]))
    offset,width,height=map(int,re.search(r'\{UnitKind::Fold, "fold", (\d+), (\d+), (\d+)\}',text).groups())
    assert (width,height)==(35,17)
    bits=[bool(raw[offset+y*((width+7)//8)+x//8] & (128>>(x%8))) for y in range(height) for x in range(width)]
    checks=[]
    for row in proof['surfaces']:
        path=Path(proof['capture'])/(row['id']+'_'+row['surface']+'.ppm')
        with Image.open(path) as im:crop=list(im.crop((14,8,14+width,8+height)).getdata())
        ink={c for c,b in zip(crop,bits) if b};assert len(ink)==1,(path,'overwritten title')
        color=next(iter(ink));assert [c==color for c in crop]==bits,(path,'title mask mismatch')
        checks.append(path.name)
    return {'exact_current_title_surfaces':len(checks),'anchor':[14,8],'size':[width,height],
            'heading_data_sha256':sha(ROOT/'platform/3ds/module_heading_data.h')}

def inspect(proof):
    folder=Path(proof['capture']);assert proof['native_duals']==66,proof['native_duals']
    for row in proof['surfaces']:
        assert int(row['outside_rects'])==int(row['outside_lines'])==int(row['text_overlaps'])==0,row
    samples={}
    for theme in ('graphite','pink'):
        for name in ('default','wave','curve','sharp','wrap','diode','offscale','attenuate','fine','updating','bypass','mute','mix0','trim0','lock','curve_lock','legacy','legacy_lock','extended','route','holes'):
            ident=f'{theme}_fold_{name}'
            for surface in ('top','bottom'):
                p=folder/(ident+'_'+surface+'.ppm');assert p.is_file(),p
                with Image.open(p) as im:assert im.size==((400 if surface=='top' else 320),240)
            if name in ('wave','curve','offscale','attenuate','fine','updating','legacy'):
                with Image.open(folder/(ident+'.png')) as im:im.resize((800,992),Image.Resampling.NEAREST).save(folder/(ident+'_2x.png'))
                p=folder/(ident+'_2x.png');samples[p.name]=sha(p)
        for p in range(7):assert (folder/(f'{theme}_fold_mod_{p}.png')).is_file()
        for p in range(5):assert (folder/(f'{theme}_fold_legacy_mod_{p}.png')).is_file()
    return {'native_duals':proof['native_duals'],'raw_surfaces':len(proof['surfaces']),
            'titles':exact_titles(proof),'captures_2x':samples,'draw_boundary':draw_boundary(),
            'model':'actual reset FoldUnit: 500ms warmup +257 samples spanning 8ms; max2048/capture; linear DISPLAY interpolation to353columns preserves native extrema; curve353 actual transfer values before DC/tone/trim/mix',
            'legacy_oracle':'full retained test_fold_wave_model with explicit FoldUnit(false), including exhaustive sample-code seams; old controller/run() suites not invoked',
            'hardware':False,'fps':False,'audio_listening':False,'overlap_waivers':[]}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('optimized','sanitized'),default='optimized')
    args=parser.parse_args();configure();draw_boundary()
    own_pins={str(p):sha(p) for p in (Path(__file__).resolve(),OWN,HEADER,LEGACY)}
    receipt,proof=host.build(ROOT,args.mode,args.mode=='sanitized',True)
    assert all(sha(p)==digest for p,digest in own_pins.items()),'focused sources changed during proof'
    result=inspect(proof);retained={}
    for path,digest in proof['dependencies'].items():
        p=Path(path)
        if p.is_relative_to(ROOT) and not p.is_relative_to(OUT):
            target=receipt.parent/'inputs'/p.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(p,target);assert sha(p)==sha(target)==digest
            retained[str(p.relative_to(ROOT))]=digest
    for p in (Path(__file__).resolve(),OWN):
        target=receipt.parent/'inputs'/p.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(p,target);retained[str(p.relative_to(ROOT))]=sha(target)
    target=OUT/('inspection-'+args.mode+'-'+host.stamp()+'.json');result['retained_inputs']=retained
    target.write_text(json.dumps({'receipt':str(receipt),'receipt_sha256':sha(receipt),'runner_sha256':sha(__file__),
        'case_sha256':sha(OWN),'results':result},indent=2,sort_keys=True)+'\n');print(target)
if __name__=='__main__':main()
