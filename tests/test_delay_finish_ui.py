#!/usr/bin/env python3
"""Fresh Delay finishing proof, separate from the historical header-only stage.

Actual composed UI; all28 defaults + modern/legacy Delay parameter/read-state cases.
Imports build utilities only, never an older test main or old preservation gate.
"""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import csv
import hashlib
import json
import re
import sys
sys.dont_write_bytecode=True
import test_module_headings_ui as host
from PIL import Image, ImageChops

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'release/beta-20261004/delay-ui'
OWN=ROOT/'tests/delay_finish_ui_cases.inc'
BASE=ROOT/'release/module-headings/current-optimized-20261004T054122_916843Z/receipt.json'


def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,d):p.write_text(json.dumps(d,indent=2)+'\n')
def latest(tag):
    ref=json.loads((OUT/(tag+'-latest.json')).read_text())
    assert sha(ref['receipt'])==ref['sha256']
    return json.loads(Path(ref['receipt']).read_text())


def configure():
    OUT.mkdir(parents=True,exist_ok=True)
    header=ROOT/'tests/module_headings_ui_cases.inc'
    text=header.read_text()
    assert text.count('int main(')==1
    combined=text.replace('int main(','int unused_header_fixture_main(',1)+'\n'+OWN.read_text()
    path=OUT/('cases-'+hashlib.sha256(combined.encode()).hexdigest()[:16]+'.inc')
    if path.exists():assert path.read_text()==combined
    else:path.write_text(combined)
    host.OUT=OUT;host.CASES=path
    # Current-only build: no additions/rewrites to the old frozen header baseline.
    host.supplement=lambda: {}
    return {str(p.relative_to(ROOT)):sha(p) for p in [Path(__file__),OWN,header,
            ROOT/'tests/test_module_headings_ui.py',path]}


def inspect(proof):
    folder=Path(proof['capture']);base=json.loads(BASE.read_text())
    old={r['id']+'_'+r['surface']:r for r in base['surfaces']}
    exact=[];states=[];failures=[]
    for row in proof['surfaces']:
        name=row['id']+'_'+row['surface'];p=folder/(name+'.ppm')
        if row['surface']=='top':assert int(row['text_overlaps'])==0,('upper native text collision',name)
        if name in old and not(row['module']=='delay' and row['surface']=='top'):
            with Image.open(p) as got,Image.open(Path(base['capture'])/(name+'.ppm')) as wanted:
                assert not ImageChops.difference(got,wanted).getbbox(),('untouched surface differs',name)
            exact.append(name)
        if row['module']=='delay' and row['surface']=='top':
            with (folder/(name+'_text.tsv')).open() as f: texts=list(csv.DictReader(f,delimiter='\t'))
            words=[r['text'].lower() for r in texts]
            assert any('time / hollow aim / solid read' in s for s in words),name
            assert not any('potential range' in s for s in words),name
            if row['variant'] in ('fresh','reset'):
                assert 'await processing' in words,('invented read before processing',name,words)
            if row['variant']=='clear-pending':assert 'clear pending' in words,(name,words)
            if row['variant']=='clear-fade':assert 'clearing history' in words,(name,words)
            if row['variant']=='hold-arrival':assert any(w.startswith('hold ') for w in words),(name,words)
            states.append(name)
    assert len({r['kind'] for r in proof['surfaces'] if r['variant']=='default'})==28
    return {'unchanged_surface_comparisons':exact,'delay_upper_checks':states,
            'no_upper_native_text_overlaps':True,'baseline_receipt_sha256':sha(BASE)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('optimized','sanitized','compare'),default='optimized')
    args=parser.parse_args();inputs=configure()
    if args.mode!='compare':
        _,proof=host.build(ROOT,'delay-'+args.mode,args.mode=='sanitized',True)
        result=inspect(proof)
        for p,digest in inputs.items():assert sha(ROOT/p)==digest,('test source drift',p)
        target=OUT/('inspection-'+args.mode+'-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')+'.json')
        save(target,{'status':'pass','inputs':inputs,'inspection':result,'binary_sha256':proof['binary_sha256'],
                     'native_duals':proof['native_duals'],'host_only':True,'hardware':False,'deployment':False})
        print(target)
    else:
        a,b=latest('delay-optimized'),latest('delay-sanitized')
        assert a['source_sha256']==b['source_sha256']==host.pins(ROOT)
        aa={r['id']+'_'+r['surface']:r for r in a['surfaces']};bb={r['id']+'_'+r['surface']:r for r in b['surfaces']}
        assert aa.keys()==bb.keys()
        for name in aa:
            assert sha(Path(a['capture'])/(name+'.ppm'))==sha(Path(b['capture'])/(name+'.ppm')),name
        result=inspect(a);inspect(b)
        p=OUT/('verification-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')+'.json')
        save(p,{'status':'pass','inputs':inputs,'inspection':result,'native_duals':a['native_duals'],
                'all28_defaults_both_themes':True,'optimized_sanitized_raw_exact':len(aa),
                'binaries':{a['binary']:a['binary_sha256'],b['binary']:b['binary_sha256']},
                'host_only':True,'hardware':False,'deployment':False,
                'limitations':['native software raster, not PICA/device/fps/listening',
                    'synthetic fixtures; scoped upper/parameter-state proof, not a full application regression']})
        save(OUT/'verification-latest.json',{'receipt':str(p),'sha256':sha(p)})
        print(p)


if __name__=='__main__':main()
