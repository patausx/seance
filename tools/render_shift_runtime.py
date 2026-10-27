#!/usr/bin/env python3
"""Package actual SHIFT C++ editor/top captures, never draw UI in Python."""
import csv
import hashlib
import json
import subprocess
from pathlib import Path
from PIL import Image
from render_fold_runtime import audit_text

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/shift_runtime'
BIN=Path('/tmp/seance_osc_input_test')
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    raw=OUT/'raw';raw.mkdir(parents=True,exist_ok=True)
    sources={str(p.relative_to(ROOT)):sha(p) for group in ('core','platform/3ds') for p in (ROOT/group).rglob('*') if p.is_file()}
    binary=sha(BIN)
    run=subprocess.run([str(BIN),'--dump-shift',str(raw)],cwd=ROOT,text=True,capture_output=True)
    (OUT/'capture.log').write_text(run.stdout+run.stderr);run.check_returncode()
    rows=list(csv.DictReader((raw/'manifest.tsv').open(),delimiter='\t'));assert len(rows)==3
    sheet=Image.new('RGB',(1200,480),'black');frames=[]
    for i,row in enumerate(rows):
        name=row['frame'];assert int(row['shared'])==int(row['top'])+int(row['bottom'])<=8192
        assert int(row['clipped'])==int(row['overlaps'])==0
        dual=Image.new('RGB',(400,480),'black');surfaces={}
        for screen,width,x,y in [('top',400,0,0),('bottom',320,40,240)]:
            path=raw/f'{name}_{screen}.ppm';image=Image.open(path).convert('RGB');assert image.size==(width,240)
            surfaces[screen]={'sha256':sha(path),'text':audit_text(raw/f'{name}_{screen}_text.tsv',width)}
            dual.paste(image,(x,y))
        dual.save(OUT/f'{name}_dual_native.png');sheet.paste(dual,(400*i,0))
        frames.append({'counts':row,'surfaces':surfaces})
    sheet.save(OUT/'comparison_native.png')
    assert sha(BIN)==binary and all(sha(ROOT/p)==s for p,s in sources.items())
    baseline=json.loads((OUT/'baseline.json').read_text());changed=[p for p,h in baseline.items() if sources[p]!=h]
    (OUT/'verification.json').write_text(json.dumps({
        'origin':'actual C++ main/editor/picker/inspector, host software raster; not hardware acceptance',
        'draws_ui_in_python':False,'frames':frames,'binary_sha256':binary,'source_sha256':sources,
        'peak_shared_objects':max(int(r['shared']) for r in rows),'changed_runtime_files':changed,
        'unchanged_core_platform_count':len(baseline)-len(changed),
        'limits':['no ARM/FTP/CIA','no hardware CPU or listening acceptance','no full suite'],
    },indent=2)+'\n')
    print('three actual duals; peak',max(int(r['shared']) for r in rows),'/8192')
if __name__=='__main__':main()
