#!/usr/bin/env python3
"""Focused native host MOTOR UI capture; no inherited tests, ARM, hardware or FTP."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import sys
sys.dont_write_bytecode=True
import test_module_headings_ui as host
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'release/motor-extended/ui'
OWN=ROOT/'tests/motor_extended_ui_cases.inc'
HEADER=ROOT/'tests/module_headings_ui_cases.inc'

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def configure():
    OUT.mkdir(parents=True,exist_ok=True)
    text=HEADER.read_text().replace('int main(', 'int unused_motor_header_main(',1)+'\n'+OWN.read_text()
    path=OUT/('cases-'+hashlib.sha256(text.encode()).hexdigest()[:16]+'.inc')
    if path.exists():assert path.read_text()==text
    else:path.write_text(text)
    host.OUT,host.CASES=OUT,path
    host.supplement=lambda:{}

def inspect(proof):
    folder=Path(proof['capture'])
    assert proof['native_duals']==16,proof['native_duals']
    for row in proof['surfaces']:
        assert int(row['outside_rects'])==int(row['outside_lines'])==int(row['text_overlaps'])==0,row
    samples={}
    for theme in ('graphite','pink'):
        for name in ('sync_arp','pulse_width','saw_drift','extended','legacy','bypass','lock','mod'):
            id=f'{theme}_motor_{name}'
            for surface in ('top','bottom'):
                ppm=folder/(id+'_'+surface+'.ppm')
                assert ppm.is_file(),ppm
                with Image.open(ppm) as im:
                    assert im.size==((400 if surface=='top' else 320),240),im.size
            with Image.open(folder/(id+'.png')) as image:
                assert image.size==(400,496)
                if name in ('sync_arp','pulse_width','saw_drift','legacy'):
                    image.resize((800,992),Image.Resampling.NEAREST).save(folder/(id+'_2x.png'))
        for name in ('sync_arp','pulse_width','saw_drift','legacy'):
            p=folder/(theme+'_motor_'+name+'_2x.png');samples[p.name]=sha(p)
    # The upper three-cycle model must respond to the actual motor parameters;
    # bypass only changes status, not falsely invents silence in the reference.
    for theme in ('graphite','pink'):
        top=lambda n:sha(folder/(theme+'_motor_'+n+'_top.ppm'))
        assert len({top('sync_arp'),top('pulse_width'),top('saw_drift'),top('legacy')})==4
    return {'native_duals':proof['native_duals'],'raw_surfaces':len(proof['surfaces']),
        'software_raster':True,'model':'real private MotorUnit.process_with_raw, not live PCM',
        'captures_2x':samples,'hardware':False,'audio_listening':False}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('optimized','sanitized'),default='optimized')
    args=parser.parse_args();configure()
    receipt,proof=host.build(ROOT,args.mode,args.mode=='sanitized',True)
    result=inspect(proof)
    target=OUT/('inspection-'+args.mode+'-'+host.stamp()+'.json')
    target.write_text(json.dumps({'receipt':str(receipt),'receipt_sha256':sha(receipt),'runner_sha256':sha(__file__),
        'case_sha256':sha(OWN),'results':result},indent=2,sort_keys=True)+'\n')
    print(target)
if __name__=='__main__':main()
