#!/usr/bin/env python3
"""package three actual c++ osc morph dual frames; never authors screen pixels."""
import csv, json, subprocess
from pathlib import Path
from PIL import Image
from render_fold_runtime import ROOT, audit_text, sha

OUT=ROOT/'ui_mockups/osc_gothic_runtime'
BIN=Path('/tmp/seance_osc_input_test')
SOURCES=['core/engine/units.h','core/engine/units.cpp','core/engine/patch_io.cpp',
    'platform/3ds/ui_module_focus_osc.cpp','platform/3ds/ui_module_focus_internal.h',
    'platform/3ds/module_inspector.cpp','platform/3ds/menu_labels_data.h','tools/generate_menu_labels.py','platform/3ds/ui_param_lens.cpp','platform/3ds/ui_editor.cpp',
    'tools/osc_morph_core_test.cpp','tools/osc_morph_ui_test.h','tools/osc_input_dispatch_test.cpp','tools/extract_osc_input.py',
    'platform/3ds/main.cpp','tools/sequencer_test.cpp','Makefile','docs/osc-shape-morph.md']

def main():
    raw=OUT/'raw';raw.mkdir(parents=True,exist_ok=True)
    identities={p:sha(ROOT/p) for p in SOURCES};binary=sha(BIN)
    result=subprocess.run([str(BIN),'--dump-osc',str(raw)],cwd=ROOT,capture_output=True,text=True)
    (OUT/'capture.log').write_text(result.stdout+result.stderr);result.check_returncode()
    rows=list(csv.DictReader((raw/'manifest.tsv').open(),delimiter='\t'))
    assert [r['frame'] for r in rows]==['01_sine_tri','02_tri_saw_mod','03_saw_pulse']
    sheet=Image.new('RGB',(1200,480),'black');frames=[];peak=0
    for index,row in enumerate(rows):
        name=row['frame'];surfaces={};images={}
        for screen,width in [('top',400),('bottom',320)]:
            path=raw/f'{name}_{screen}.ppm';image=Image.open(path).convert('RGB');assert image.size==(width,240)
            images[screen]=image;surfaces[screen]={'sha256':sha(path),'text':audit_text(raw/f'{name}_{screen}_text.tsv',width)}
        shared=int(row['shared']);assert shared==int(row['top'])+int(row['bottom']) and 0<shared<=8192
        assert int(row['clipped'])==int(row['overlaps'])==0;peak=max(peak,shared)
        dual=Image.new('RGB',(400,480),'black');dual.paste(images['top'],(0,0));dual.paste(images['bottom'],(40,240))
        dual.save(OUT/f'{name}_dual_native.png');dual.resize((800,960),Image.Resampling.NEAREST).save(OUT/f'{name}_dual_2x.png');sheet.paste(dual,(index*400,0))
        frames.append({'row':row,'surfaces':surfaces})
    sheet.save(OUT/'comparison_native.png');sheet.resize((2400,960),Image.Resampling.NEAREST).save(OUT/'comparison_2x.png')
    assert sha(BIN)==binary and {p:sha(ROOT/p) for p in SOURCES}==identities
    protection=json.loads((OUT/'preservation.json').read_text())
    assert all(sha(ROOT/p)==digest for p,digest in protection['protected'].items()), 'protected core/platform source changed'
    receipt={'origin':'actual c++ native host raster; software line coverage, not hardware/pica acceptance',
      'screen_pixels_drawn_in_python':False,'binary_sha256':binary,'sources':identities,'frames':frames,'peak_shared_objects':peak,
      'model':'273 columns x 8 exact integer probes; two normalized cycles; adjacent raw shape before unchanged fold/sub/gain; fixed +/-FS; no partials/live pcm/agc',
      'controls':'seven unchanged relative controls; ordinary one-pixel tracks/ticks, bold base outlines with violet effective fill; smoothed frozen scalars, no UI timer; two-pixel nonzero waveform segments; OSC upper gothic words and existing note/numeric glyphs; locks and held-L mod unchanged',
      'protected_sources':len(protection['protected']),
      'limits':['no arm build','no ftp','no hardware/audio acceptance','not a live analyzer']}
    (OUT/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f'{OUT}: 3 actual c++ duals, peak {peak}/8192, bounds/text/source identities pass')
if __name__=='__main__':main()
