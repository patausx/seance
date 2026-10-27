#!/usr/bin/env python3
"""Package actual C++ Tilt pixels; no authored UI, signal simulation or deployment."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
from PIL import Image
from render_fold_runtime import ROOT, audit_text, sha


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=Path('/tmp/seance_chorus_ui_test'))
    parser.add_argument('--output',type=Path,default=ROOT/'ui_mockups/tilt_runtime')
    args=parser.parse_args();out=args.output;raw=out/'raw';raw.mkdir(parents=True,exist_ok=True)
    protected=json.loads((ROOT/'.local-state/backups/tilt-clear-ui-before/protected.json').read_text())
    for path,expected in protected.items():assert sha(ROOT/path)==expected,path
    run=subprocess.run([str(args.binary),'--dump-tilt',str(raw)],cwd=ROOT,capture_output=True,text=True)
    (out/'capture.log').write_text(run.stdout+run.stderr);run.check_returncode()
    rows=list(csv.DictReader((raw/'manifest.tsv').open(),delimiter='\t'));assert len(rows)==19
    receipt={'origin':'actual C++ native host raster / no hardware or PICA acceptance',
             'scope':'approved Tilt UI + DSP + SNC7 UNIT compatibility / no ARM build or deployment',
             'model':'actual integer LUT/trim gains, linearized one-pole; phase-aware dry/wet transfer; excludes rounding, clipping, transient state and live PCM',
             'capture':'<=320-byte pointer-free lower / cached model outside guard and before GPU / detached engine+audio in draw',
             'axes':'fixed log20..16000hz / -12..+12db / overflow chevrons',
             'compatibility':'old SNC1..6 and unflagged SNC7: legacy5, range0 and stepped law PCM retained; new Tilt: flagged UNIT bit0, 6 params, interpolated shelves + trim; no auto upgrade',
             'budget_scope':'visible upper+lower/lens; excludes hidden main Chains underlay',
             'binary_sha256':sha(args.binary),'protected':protected,'frames':[],'peak_shared_objects':0}
    sheet=Image.new('RGB',(1600,2400),'black');duals={};bottoms={}
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
        sheet.paste(dual,((i%4)*400,(i//4)*480));duals[name]=dual;bottoms[name]=images['bottom']
        receipt['frames'].append(dict(row,surfaces=surfaces));receipt['peak_shared_objects']=max(receipt['peak_shared_objects'],count)
    sheet.save(out/'contact_sheet_native.png')
    comparison=Image.new('RGB',(800,960),'black');surfaces=Image.new('RGB',(640,480),'black')
    for i,name in enumerate(['01_air','02_body','03_zero','04_trim']):
        comparison.paste(duals[name],((i%2)*400,(i//2)*480));surfaces.paste(bottoms[name],((i%2)*320,(i//2)*240))
    for name,image in [('comparison',comparison),('surfaces',surfaces)]:
        image.save(out/f'{name}_native.png');image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST).save(out/f'{name}_2x.png')
    files=['core/engine/units.cpp','core/engine/units.h','core/engine/tilt_eq.h','core/engine/patch_io.cpp',
           'platform/3ds/ui_module_focus_tilt.cpp','platform/3ds/tilt_response_model.h','platform/3ds/module_inspector.cpp',
           'platform/3ds/module_inspector.h','platform/3ds/ui_module_focus_internal.h','platform/3ds/ui_param_lens.cpp','platform/3ds/ui_editor.cpp',
           'tools/tilt_ui_test.h','tools/tilt_dsp_test.cpp','tools/tilt_persistence_test.cpp','tools/module_inspector_test.h','tools/sequencer_test.cpp','tools/render_tilt_runtime.py','Makefile']
    receipt['sources']={p:sha(ROOT/p) for p in files}
    # Compare production outside the approved Tilt class/process/clone changes.
    backup=ROOT/'.local-state/backups/20260908_142503_before-tilt-modern_units.cpp'
    def other_dsp(text):
        start=text.index('// ============================ TILT spectral balance')
        end=text.index('// ============================ RING / diode bridge',start)
        text=text[:start]+text[end:]
        text=text.replace('#include "tilt_eq.h"\n','')
        return text
    old=other_dsp(backup.read_text());new=other_dsp((ROOT/'core/engine/units.cpp').read_text())
    # Clone is configuration, outside process; the one Tilt branch is approved.
    clause='        : source->kind == UnitKind::Tilt\n        ? new(std::nothrow) TiltUnit(static_cast<const TiltUnit*>(source)->modern())\n'
    assert new.count(clause)==1
    new=new.replace(clause,'')
    receipt['non_tilt_units_cpp_identical']=old==new
    assert old==new,'unexpected non-Tilt processing change'
    engine_hash='707d2414b972483c3584c5a3bf79481e19b7beb456b8e30bc3a8df28aa68957e'
    assert sha(ROOT/'core/engine/engine.cpp')==engine_hash
    receipt['engine_unchanged']=engine_hash
    receipt['checks']={p.name:{'sha256':sha(p),'log':str(p.relative_to(out))} for p in sorted((out/'checks').glob('*.log'))}
    (out/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f"{out}: {len(rows)} real C++ pairs, peak {receipt['peak_shared_objects']}/8192; strict text audit passed")

if __name__=='__main__':main()
