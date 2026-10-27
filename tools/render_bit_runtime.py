#!/usr/bin/env python3
"""Package real C++ Bit pixels; reuse the existing strict text audit. No UI drawing."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
from PIL import Image
from render_fold_runtime import ROOT, CORE_BASELINE, audit_text, sha


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=Path('/tmp/seance_chorus_ui_test'))
    parser.add_argument('--output',type=Path,default=ROOT/'ui_mockups/bit_runtime')
    args=parser.parse_args();out=args.output;raw=out/'raw';raw.mkdir(parents=True,exist_ok=True)
    for path,expected in CORE_BASELINE.items():
        assert sha(ROOT/path)==expected, path
    run=subprocess.run([str(args.binary),'--dump-bit',str(raw)],cwd=ROOT,capture_output=True,text=True)
    (out/'capture.log').write_text(run.stdout+run.stderr);run.check_returncode()
    rows=list(csv.DictReader((raw/'manifest.tsv').open(),delimiter='\t'));assert len(rows)==14
    receipt={'origin':'actual C++ native host raster; not hardware/PICA acceptance',
             'scope':'Bit UI only, unchanged DSP/engine/storage; no ARM build or deployment',
             'reference':'288 samples / 9ms / 3 cycles .75fs triangle; private unchanged BitUnit at reset seed and phase; not live audio',
             'capture':'<=768-byte pointer-free lower; cached synthetic processing outside guard before GPU; detached engine/audio during draw',
             'budget_scope':'visible upper + lower/lens, excludes hidden main Chains underlay',
             'binary_sha256':sha(args.binary),'protected_core_unchanged':True,
             'core':CORE_BASELINE,'frames':[],'peak_shared_objects':0}
    sheet=Image.new('RGB',(1600,1920),'black');duals={}
    for i,row in enumerate(rows):
        name=row['frame'];surfaces={};images={}
        for screen,width in [('top',400),('bottom',320)]:
            path=raw/f'{name}_{screen}.ppm';image=Image.open(path).convert('RGB');assert image.size==(width,240)
            images[screen]=image;surfaces[screen]={'sha256':sha(path),'text':audit_text(raw/f'{name}_{screen}_text.tsv',width)}
        count=int(row['shared_objects']);assert 0<count<=8192
        assert count==int(row['top_objects'])+int(row['bottom_objects'])
        assert int(row['bottom_clipped'])==int(row['bottom_overlaps'])==0
        dual=Image.new('RGB',(400,480),'black');dual.paste(images['top'],(0,0));dual.paste(images['bottom'],(40,240))
        dual.save(out/f'{name}_dual_native.png');dual.resize((800,960),Image.Resampling.NEAREST).save(out/f'{name}_dual_2x.png')
        sheet.paste(dual,((i%4)*400,(i//4)*480));duals[name]=dual
        receipt['frames'].append(dict(row,surfaces=surfaces));receipt['peak_shared_objects']=max(receipt['peak_shared_objects'],count)
    sheet.save(out/'contact_sheet_native.png')
    comparison=Image.new('RGB',(800,960),'black')
    for i,name in enumerate(['01_bit','02_3bits','03_slow_clock','06_dropout']):comparison.paste(duals[name],((i%2)*400,(i//2)*480))
    comparison.save(out/'comparison_native.png')
    files=['platform/3ds/ui_module_focus_bit.cpp','platform/3ds/bit_reference.h','platform/3ds/module_inspector.cpp',
           'platform/3ds/ui_module_focus_internal.h','platform/3ds/ui_param_lens.cpp','platform/3ds/ui_editor.cpp',
           'tools/bit_ui_test.h','tools/module_inspector_test.h','tools/sequencer_test.cpp','tools/render_bit_runtime.py','Makefile']
    receipt['sources']={p:sha(ROOT/p) for p in files}
    (out/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f"{out}: {len(rows)} pairs, peak {receipt['peak_shared_objects']}/8192")

if __name__=='__main__':main()
