#!/usr/bin/env python3
"""package actual c++ mod/temp lens states; never authors ui pixels or builds."""
import argparse
import csv
import hashlib
import json
import subprocess
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/mod_lens_runtime'
NAMES=('01_base','02_mod_clipped','03_env','04_temp','05_return')
OUTSIDE=(24,20,29)

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary',type=Path,default=Path('/tmp/seance_module_inspector_test'))
    ap.add_argument('--output',type=Path,default=OUT)
    a=ap.parse_args();out=a.output.resolve();raw=out/'lens_raw';raw.mkdir(parents=True,exist_ok=True)
    run=subprocess.run([str(a.binary),'--dump-lens',str(raw)],cwd=ROOT,text=True,capture_output=True)
    (out/'lens_dump.log').write_text(run.stdout+run.stderr);run.check_returncode()
    cards=[];frames=[]
    for name in NAMES:
        top_path=raw/f'{name}_top.ppm';bottom_path=raw/f'{name}.ppm'
        top=Image.open(top_path).convert('RGB');bottom=Image.open(bottom_path).convert('RGB')
        assert top.size==(400,240) and bottom.size==(320,240)
        dual=Image.new('RGB',(400,480),OUTSIDE);dual.paste(top,(0,0));dual.paste(bottom,(40,240))
        dual.save(out/f'lens_{name}_dual_native.png')
        dual.resize((800,960),Image.Resampling.NEAREST).save(out/f'lens_{name}_dual_2x.png')
        cards.append(dual);frames.append({'name':name,'top_sha256':sha(top_path),'bottom_sha256':sha(bottom_path)})
    sheet=Image.new('RGB',(1200,960),OUTSIDE)
    for i,card in enumerate(cards):sheet.paste(card,((i%3)*400,(i//3)*480))
    sheet.save(out/'lens_contact_sheet_native.png')
    sheet.resize((2400,1920),Image.Resampling.NEAREST).save(out/'lens_contact_sheet_2x.png')
    report={'status':'actual c++ host software-raster frames; not hardware acceptance',
            'binary':str(a.binary),'frames':frames,
            'states':['base/off','negative clipped sigil route','contextual envelope route','hold-l temporary','restored return'],
            'limits':['no build or test invocation','gpu/pica/lcd/device input not proven by host capture']}
    (out/'lens_verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'output':str(out),'frames':len(frames)}))
if __name__=='__main__':main()
