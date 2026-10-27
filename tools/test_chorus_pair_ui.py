#!/usr/bin/env python3
"""Focused native host Chorus proof/capture. No ARM, device, user data or old suites."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
import test_module_headings_ui as host
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'release/chorus-pair/ui'
OWN = ROOT / 'tools/chorus_pair_ui_cases.inc'
HEADER = ROOT / 'tools/module_headings_ui_cases.inc'
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,v): p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def configure():
    OUT.mkdir(parents=True,exist_ok=True)
    text=HEADER.read_text().replace('int main(', 'int unused_chorus_header_main(',1)+'\n'+OWN.read_text()
    path=OUT/('cases-'+hashlib.sha256(text.encode()).hexdigest()[:16]+'.inc')
    if path.exists(): assert path.read_text()==text
    else: path.write_text(text)
    host.OUT,host.CASES=OUT,path
    host.supplement=lambda:{}
def number(p,v):
    if p==0:
        c=((5000+max(0,v)*20)*32000*100+(1<<31))>>32
        return f'{c//100}.{c%100:02d}hz'
    if p==1:
        c=((16+(max(0,v)*300//32768))*100000+16000)//32000
        return f'{c//100}.{c%100:02d}ms'
    if p==2:return str((max(0,v)*100+16383)//32767)+'%'
    return str(((max(-32767,v)+32767)*90+16383)//32767)+' deg'
def inspect(proof):
    folder=Path(proof['capture'])
    for row in proof['surfaces']:
        assert int(row['outside_rects'])==int(row['outside_lines'])==int(row['text_overlaps'])==0,row
    # Fixed native mask foreground AND holes on both LCDs, not submitted title text.
    source=(ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw=bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',source.split('bitmap[] = {',1)[1].split('};',1)[0]))
    off,w,h=map(int,re.search(r'\{UnitKind::Chorus, "chor", (\d+), (\d+), (\d+)\}',source).groups())
    bits=[bool(raw[off+y*((w+7)//8)+x//8]&(128>>(x%8))) for y in range(h) for x in range(w)]
    masks=0
    for row in proof['surfaces']:
        # Shared translucent lens dims its context title, still one uniform ink.
        with Image.open(folder/(row['id']+'_'+row['surface']+'.ppm')) as im:
            crop=list(im.crop((14,2,14+w,2+h)).getdata())
            ink={v for v,b in zip(crop,bits) if b};assert len(ink)==1,row
            ink=next(iter(ink));assert [v==ink for v in crop]==bits,row
            masks+=1
    rows=list(csv.DictReader((folder/'motion.tsv').open(),delimiter='\t'))
    assert len(rows)==96
    checks=0
    for row in rows:
        text=list(csv.DictReader((folder/(row['id']+'_bottom_text.tsv')).open(),delimiter='\t'))
        for p in range(4):
            expected=number(p,int(row['live'+str(p)]));base='base '+number(p,int(row['base'+str(p)]))
            assert any(r['text']==expected and 47+37*p<=int(r['y'])<55+37*p and int(r['x'])>=230 for r in text),(row['id'],p,expected)
            assert any(r['text']==base and 63+37*p<=int(r['y'])<71+37*p and int(r['x'])==16 for r in text),(row['id'],p,base)
            assert any(r['text']=='a' and 47+37*p<=int(r['y'])<55+37*p for r in text)
            checks+=1
        top=list(csv.DictReader((folder/(row['id']+'_top_text.tsv')).open(),delimiter='\t'))
        for i in range(2):
            cent=(int(row['delay'+str(i)])*100000+(32000<<15))//(32000<<16)
            expected=f'{i+1} / {cent//100}.{cent%100:02d}ms'
            assert any(r['text']==expected for r in top),(row['id'],expected)
    for theme in ('graphite_','pink_'):
        group=[r for r in rows if r['theme']==theme];assert len(group)==48
        for p in range(4):
            assert len({r['base'+str(p)] for r in group})==1
            assert len({number(p,int(r['live'+str(p)])) for r in group})>=8
        for side in ('top','bottom'):
            assert sha(folder/(theme+'chorus_idle_first_'+side+'.ppm'))==sha(folder/(theme+'chorus_idle_later_'+side+'.ppm'))
        images=[]
        for row in group:
            with Image.open(folder/(row['id']+'.png')) as im:images.append(im.convert('RGB').resize((800,992),Image.Resampling.NEAREST))
        # One shared palette avoids temporal palette churn in these presentation GIFs.
        palette=images[0].quantize(colors=128)
        frames=[im.quantize(palette=palette,dither=Image.Dither.NONE) for im in images]
        frames[0].save(folder/(theme+'engine-sigil.gif'),save_all=True,append_images=frames[1:],duration=32,loop=0,optimize=False,disposal=2)
        for name in ('default','compare','lens','maximum','lock'):
            with Image.open(folder/(theme+'chorus_'+name+'.png')) as im:im.resize((800,992),Image.Resampling.NEAREST).save(folder/(theme+'chorus_'+name+'_2x.png'))
    return {'fixed_header_masks':masks,'live_number_base_badge_checks':checks,'behavior':json.loads((folder/'behavior.json').read_text()),
            'presentation':{p.name:sha(p) for p in folder.glob('*.gif')}}
def latest(tag):
    ref=json.loads((OUT/(tag+'-latest.json')).read_text());assert sha(ref['receipt'])==ref['sha256']
    return json.loads(Path(ref['receipt']).read_text())
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--mode',choices=('optimized','sanitized','compare'),default='optimized');args=parser.parse_args()
    configure()
    if args.mode=='compare':
        a,b=latest('optimized'),latest('sanitized');assert a['source_sha256']==b['source_sha256']==host.pins(ROOT)
        assert a['case_sha256']==b['case_sha256']==sha(host.CASES)
        aa={r['id']+'_'+r['surface']:r['raster_fnv64'] for r in a['surfaces']};bb={r['id']+'_'+r['surface']:r['raster_fnv64'] for r in b['surfaces']};assert aa==bb
        for n in aa:assert sha(Path(a['capture'])/(n+'.ppm'))==sha(Path(b['capture'])/(n+'.ppm'))
        dest=OUT/('compare-'+host.stamp()+'.json');save(dest,{'status':'passed','raw_surfaces_equal':len(aa),'source_sha256':a['source_sha256'],'host_only':True});print(dest);return
    receipt,proof=host.build(ROOT,args.mode,args.mode=='sanitized',True)
    dest=OUT/('inspection-'+args.mode+'-'+host.stamp()+'.json')
    result={'receipt':str(receipt),'receipt_sha256':sha(receipt),'runner_sha256':sha(__file__),'status':'pending'}
    save(dest,result)
    try:result['inspection']=inspect(proof);result['status']='passed'
    except Exception as error:result['status']='failed';result['failure']=str(error);save(dest,result);raise
    save(dest,result);print(dest)
if __name__=='__main__':main()
