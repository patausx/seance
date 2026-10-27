#!/usr/bin/env python3
"""Package actual C++ GRAN dual frames. No authored UI, synthesis or build."""
import csv
import json
from pathlib import Path
import subprocess
from PIL import Image
from render_fold_runtime import ROOT, audit_text, sha

OUT = ROOT/'ui_mockups/gran_runtime'
BINARY = Path('/tmp/seance_chorus_ui_test')
SOURCES = ['core/engine/units.h','core/engine/units.cpp',
    'platform/3ds/ui_module_focus_grain.cpp','platform/3ds/ui_module_focus_internal.h',
    'platform/3ds/ui_editor.cpp','platform/3ds/module_inspector.cpp',
    'platform/3ds/ui_param_lens.cpp','tools/sequencer_test.cpp','tools/gran_ui_test.h',
    'tools/gran_visual_core_test.cpp','tools/module_inspector_test.h','Makefile',
    'tools/render_gran_runtime.py']
WAV = 'local_sample_library/sdcard/3ds/seance/samples/granular/gran_dark_atmos_01.wav'

def main():
    raw=OUT/'raw';raw.mkdir(parents=True,exist_ok=True)
    protected=json.loads((ROOT/'.local-state/backups/gran-classic-baseline/protected.json').read_text())
    study=json.loads((ROOT/'ui_mockups/gran_fade_study/verification.json').read_text())
    for p,h in study['preserved_sha256'].items():
        if p not in SOURCES:protected[p]=h
    for p,h in protected.items():assert sha(ROOT/p)==h,p
    sources={p:sha(ROOT/p) for p in SOURCES};binary=sha(BINARY);wav=sha(ROOT/WAV)
    result=subprocess.run([str(BINARY),'--dump-gran',str(raw)],cwd=ROOT,capture_output=True,text=True)
    (OUT/'capture.log').write_text(result.stdout+result.stderr);result.check_returncode()
    rows=list(csv.DictReader((raw/'manifest.tsv').open(),delimiter='\t'))
    assert [r['frame'] for r in rows]==['01_grain','02_cloud','03_reverse','04_lock_envelope']
    receipt=dict(origin='actual C++ native host raster, not hardware/PICA acceptance',
        screen_pixels_drawn_in_python=False,sources=sources,binary_sha256=binary,protected=protected,
        source_wave=dict(path=WAV,sha256=wav,columns=293,probes_per_bin=8,max_pcm_reads=4688,
            channels='stereo extrema union',scale='fixed +/-full scale; sampled extrema, not exhaustive peaks',
            viewport='source-time half-length, centered on effective POS, clamped at source ends; view only'),
        grains='real next-read primary heads; signed latched source trajectory; own exact integer envelope; alpha0..190, 12px contours and20px cursors; clipped at viewport; no declick tails or accumulated trails',
        capture='lower pointerfree <=1024 bytes; read-only scalars/PCM before GPU, projection/formatting after guard; detached draw',
        controls='11 unchanged parameters on grain/cloud pages plus existing clock config; fixed relative84px rails; page/name lock removal; selected mod/TEMP; clock/bypass/browser inert while lock-armed',
        sound='only const additive accessor; all previous h/cpp bytes exact after removing additions; no instance fields, audio code, defaults, scheduler, RNG or serialization changes',
        limits=['no ARM or FTP','no hardware audio acceptance','software line coverage, not GPU timing',
                'fixture directly processes actual GRAN; browser entry uses existing main-owned host shim, not new browser IO proof'],
        frames=[],peak_shared_objects=0)
    sheet=Image.new('RGB',(800,960),'black')
    for i,row in enumerate(rows):
        name=row['frame'];images={};surfaces={}
        for screen,width in [('top',400),('bottom',320)]:
            path=raw/f'{name}_{screen}.ppm';image=Image.open(path).convert('RGB');assert image.size==(width,240)
            images[screen]=image;surfaces[screen]=dict(sha256=sha(path),text=audit_text(raw/f'{name}_{screen}_text.tsv',width))
        count=int(row['shared_objects']);assert 0<count<=8192 and count==int(row['top_objects'])+int(row['bottom_objects'])
        assert int(row['bottom_clipped'])==int(row['bottom_overlaps'])==0
        dual=Image.new('RGB',(400,480),'black');dual.paste(images['top'],(0,0));dual.paste(images['bottom'],(40,240))
        dual.save(OUT/f'{name}_dual_native.png');dual.resize((800,960),Image.Resampling.NEAREST).save(OUT/f'{name}_dual_2x.png')
        sheet.paste(dual,((i%2)*400,(i//2)*480))
        receipt['frames'].append(dict(row,surfaces=surfaces));receipt['peak_shared_objects']=max(receipt['peak_shared_objects'],count)
    sheet.save(OUT/'contact_sheet_native.png')
    receipt['checks']={p.name:sha(p) for p in sorted((OUT/'checks').glob('*.log'))}
    receipt['core_preservation_sha256']=sha(OUT/'core-preservation.json')
    assert sha(BINARY)==binary and {p:sha(ROOT/p) for p in SOURCES}==sources
    for p,h in protected.items():assert sha(ROOT/p)==h,p
    assert sha(ROOT/WAV)==wav
    (OUT/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f"{OUT}: 4 actual C++ duals, peak {receipt['peak_shared_objects']}/8192, text/bounds/protected identities pass")

if __name__=='__main__':main()
