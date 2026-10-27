#!/usr/bin/env python3
"""Current VCA-only native UI captures; no inherited suites, ARM, device or deployment."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
sys.dont_write_bytecode=True
import test_module_headings_ui as host
import test_module_frame as frame_oracle
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'release/vca-extended/ui'
OWN=ROOT/'tools/vca_extended_ui_cases.inc'
HEADER=ROOT/'tools/module_headings_ui_cases.inc'
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def configure():
    OUT.mkdir(parents=True,exist_ok=True)
    text=HEADER.read_text().replace('int main(', 'int unused_vca_header_main(',1)+'\n'+OWN.read_text()
    path=OUT/('cases-'+hashlib.sha256(text.encode()).hexdigest()[:16]+'.inc')
    if path.exists():assert path.read_text()==text
    else:path.write_text(text)
    host.OUT,host.CASES=OUT,path
    host.supplement=lambda:{}
def exact_titles(proof):
    text=(ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw=bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',text.split('bitmap[] = {',1)[1].split('};',1)[0]))
    offset,width,height=map(int,re.search(r'\{UnitKind::Vca, "vca", (\d+), (\d+), (\d+)\}',text).groups())
    bits=bytes(bool(raw[offset+y*((width+7)//8)+x//8] & (128>>(x%8))) for y in range(height) for x in range(width))
    mask=width,height,bits,'Vca'
    frame_oracle.validate_mod_title_oracle(mask)
    checks=[]
    for row in proof['surfaces']:
        path=Path(proof['capture'])/(row['id']+'_'+row['surface']+'.ppm')
        if '_mod' in row['id'] and row['surface']=='bottom':
            frame_oracle.exact_mod_title(path,mask,row['id'].split('_')[0])
        else:
            with Image.open(path) as im:crop=list(im.crop((14,8,14+width,8+height)).getdata())
            ink={c for c,b in zip(crop,bits) if b};assert len(ink)==1,(path,'overwritten title')
            color=next(iter(ink));assert [c==color for c in crop]==list(map(bool,bits)),(path,'title mask mismatch')
        checks.append(path.name)
    return {'exact_current_title_surfaces':len(checks),'anchor':[14,8],'size':[width,height],
            'mod_veil':'exact shared palette/alpha, missing-extra-shift mutation rejection','heading_data_sha256':sha(ROOT/'platform/3ds/module_heading_data.h')}
def inspect(proof):
    folder=Path(proof['capture'])
    assert proof['native_duals']==86,proof['native_duals']
    for row in proof['surfaces']:
        assert int(row['outside_rects'])==int(row['outside_lines'])==int(row['text_overlaps'])==0,row
    samples={}
    for theme in ('graphite','pink'):
        for name in ('default','main','setup','sync_duck','sync_setup','hold0','extreme0','extreme1','extreme2','bypass','mute','wait','reset','ignore','lock','ahd','ahd_setup','extended','ad','route','holes'):
            ident=f'{theme}_vca_{name}'
            for surface in ('top','bottom'):
                p=folder/(ident+'_'+surface+'.ppm');assert p.is_file(),p
                with Image.open(p) as im:assert im.size==((400 if surface=='top' else 320),240)
            if name in ('main','setup','sync_duck','sync_setup','ahd','ad'):
                with Image.open(folder/(ident+'.png')) as im:im.resize((800,992),Image.Resampling.NEAREST).save(folder/(ident+'_2x.png'))
                p=folder/(ident+'_2x.png');samples[p.name]=sha(p)
        for count in (11,8,3):
            for p in range(count):assert (folder/(f'{theme}_vca_mod{count}_{p}.png')).is_file()
    return {'native_duals':proof['native_duals'],'raw_surfaces':len(proof['surfaces']),
            'titles':exact_titles(proof),'captures_2x':samples,
            'model':'353-column normalized opening from-rest edit diagram; dot is actual opening at latched elapsed stage sample time, not audio waveform/history',
            'hardware':False,'fps':False,'audio_listening':False,'overlap_waivers':[]}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('optimized','sanitized'),default='optimized')
    args=parser.parse_args();configure()
    own_pins={str(p):sha(p) for p in (Path(__file__).resolve(),OWN,HEADER)}
    receipt,proof=host.build(ROOT,args.mode,args.mode=='sanitized',True)
    assert all(sha(p)==digest for p,digest in own_pins.items()),'focused sources changed during proof'
    result=inspect(proof);retained={}
    for path,digest in proof['dependencies'].items():
        p=Path(path)
        if p.is_relative_to(ROOT) and not p.is_relative_to(OUT):
            target=receipt.parent/'inputs'/p.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(p,target);assert sha(p)==sha(target)==digest;retained[str(p.relative_to(ROOT))]=digest
    for p in (Path(__file__).resolve(),OWN):
        target=receipt.parent/'inputs'/p.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(p,target);retained[str(p.relative_to(ROOT))]=sha(target)
    target=OUT/('inspection-'+args.mode+'-'+host.stamp()+'.json');result['retained_inputs']=retained
    target.write_text(json.dumps({'receipt':str(receipt),'receipt_sha256':sha(receipt),'runner_sha256':sha(__file__),
        'case_sha256':sha(OWN),'results':result},indent=2,sort_keys=True)+'\n');print(target)
if __name__=='__main__':main()
