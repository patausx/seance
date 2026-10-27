#!/usr/bin/env python3
"""Package actual C++ bounce LCD captures; no authored/mock layout."""
from pathlib import Path
import subprocess, hashlib, json
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/bounce_runtime'; OUT.mkdir(exist_ok=True)
p=subprocess.run(['/tmp/seance_bounce_ui_test','--dump-bounce',str(OUT)],cwd=ROOT,capture_output=True,text=True)
(OUT/'checks/capture.log').write_text(p.stdout+p.stderr);p.check_returncode()
frames=[]
for file in sorted(OUT.glob('*_top.ppm')):
    name=file.name.removesuffix('_top.ppm');top=Image.open(file).convert('RGB');bottom=Image.open(OUT/(name+'_bottom.ppm')).convert('RGB')
    assert top.size==(400,240) and bottom.size==(320,240)
    dual=Image.new('RGB',(400,480),(31,20,31));dual.paste(top,(0,0));dual.paste(bottom,(40,240))
    dual.save(OUT/(name+'_dual.png'));dual.resize((800,960),Image.Resampling.NEAREST).save(OUT/(name+'_dual_2x.png'));frames.append(name)
assert len(frames)==7
sheet=Image.new('RGB',(400*4,480*2),(31,20,31))
for i,name in enumerate(frames):sheet.paste(Image.open(OUT/(name+'_dual.png')),((i%4)*400,(i//4)*480))
sheet.save(OUT/'contact.png')
files=['platform/3ds/ui_bounce.cpp','platform/3ds/ui_bounce.h','platform/3ds/main.cpp','platform/3ds/ui_nav.cpp','platform/3ds/ui_nav.h','platform/3ds/ui_state.h','platform/3ds/ui_top.cpp','platform/3ds/ui_sample_library.cpp','platform/3ds/menu_labels_data.h','core/engine/engine.cpp','core/engine/engine.h','core/engine/bounce.h','core/engine/bounce_impl.inc','tools/bounce_ui_test.cpp','tools/extract_bounce_main.py','Makefile']
(OUT/'verification.json').write_text(json.dumps({'origin':'actual C++ bounce/core/main-routing/WAV UI test and existing native software raster','frames':frames,'log':p.stdout,'sha256':{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},'binary_sha256':hashlib.sha256(Path('/tmp/seance_bounce_ui_test').read_bytes()).hexdigest(),'limits':'software raster is not PICA/hardware acceptance; filesystem test is host IO not SD. existing WAV loader zeroes last source frame. no ARM/deployment.'},indent=2)+'\n')
print('packaged 7 actual frozen bounce duals:',OUT)
