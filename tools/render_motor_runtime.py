#!/usr/bin/env python3
"""package three actual c++ MOTOR dual frames; never authors screen pixels."""
import csv, json, subprocess
from pathlib import Path
from PIL import Image
from render_fold_runtime import ROOT, audit_text, sha
OUT=ROOT/'ui_mockups/motor_clear_runtime';BIN=Path('/tmp/seance_chorus_ui_test')
SOURCES=['core/engine/units.h','core/engine/units.cpp','platform/3ds/ui_module_focus_motor.cpp',
 'platform/3ds/ui_module_focus_generators.cpp','platform/3ds/ui_module_focus_internal.h','platform/3ds/module_inspector.cpp',
 'platform/3ds/ui_param_lens.cpp','platform/3ds/ui_editor.cpp','platform/3ds/menu_labels_data.h','tools/generate_menu_labels.py',
 'tools/motor_ui_test.h','tools/sequencer_test.cpp','tools/render_motor_runtime.py','Makefile']
def main():
 raw=OUT/'raw';raw.mkdir(parents=True,exist_ok=True);identities={p:sha(ROOT/p) for p in SOURCES};binary=sha(BIN)
 result=subprocess.run([str(BIN),'--dump-motor',str(raw)],cwd=ROOT,capture_output=True,text=True);(OUT/'capture.log').write_text(result.stdout+result.stderr);result.check_returncode()
 rows=list(csv.DictReader((raw/'manifest.tsv').open(),delimiter='\t'));assert [r['frame'] for r in rows]==['01_saw','02_mix_res','03_pulse']
 sheet=Image.new('RGB',(1200,480),'black');frames=[];peak=0
 for index,row in enumerate(rows):
  name=row['frame'];surfaces={};images={}
  for screen,width in [('top',400),('bottom',320)]:
   path=raw/f'{name}_{screen}.ppm';image=Image.open(path).convert('RGB');assert image.size==(width,240);images[screen]=image;surfaces[screen]={'sha256':sha(path),'text':audit_text(raw/f'{name}_{screen}_text.tsv',width)}
  shared=int(row['shared']);assert shared==int(row['top'])+int(row['bottom']) and 0<shared<=8192;assert int(row['clipped'])==int(row['overlaps'])==0;peak=max(peak,shared)
  dual=Image.new('RGB',(400,480),'black');dual.paste(images['top'],(0,0));dual.paste(images['bottom'],(40,240));dual.save(OUT/f'{name}_dual_native.png');dual.resize((800,960),Image.Resampling.NEAREST).save(OUT/f'{name}_dual_2x.png');sheet.paste(dual,(index*400,0));frames.append({'row':row,'surfaces':surfaces})
 sheet.save(OUT/'comparison_native.png');sheet.resize((2400,960),Image.Resampling.NEAREST).save(OUT/'comparison_2x.png')
 assert sha(BIN)==binary and {p:sha(ROOT/p) for p in SOURCES}==identities
 receipt={'origin':'actual c++ native host raster; software line coverage, not hardware/pica acceptance','screen_pixels_drawn_in_python':False,'binary_sha256':binary,'sources':identities,'frames':frames,'peak_shared_objects':peak,'model':'unchanged exact first 1168 samples / 36.5ms after MOTOR reset; raw last-of-four sample trace + final four-sample extrema per 292 fixed-scale column; raw thin, output bold nonzero; no phase overlay, live PCM scan or AGC','controls':'same five relative controls + exact three-state source / thin tracks, outline edit, filled effective / shared held-L mod / locks retained','presentation':'MOTOR-only clean selected upper / conditional live and route / three gothic source words / independent published output-energy header meter','preservation_receipt':'preservation.json','limits':['no arm build','no ftp','no hardware/audio acceptance']}
 (OUT/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n');print(f'{OUT}: 3 actual c++ duals, peak {peak}/8192, bounds/text/source identities pass')
if __name__=='__main__':main()
