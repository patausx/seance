#!/usr/bin/env python3
"""Report-only default editor body comparison to saved pre-DLY header run (never relabel it)."""
from pathlib import Path
from PIL import Image, ImageChops
import hashlib, json
ROOT=Path(__file__).resolve().parents[1]
OLD=ROOT/'release/module-headings/current-optimized-20261004T054122_916843Z/capture'
OUT=ROOT/'release/module-identity-current'

def main():
    pointer=json.loads((OUT/'current-optimized-latest.json').read_text())
    result=json.loads(Path(pointer['receipt']).read_text());fresh=Path(result['capture'])
    # DLY is independently redesigned after the archived run, not an eligible oracle.
    # MIC/REC art begins near y36; no pixels are compared inside their header art.
    rows=[]
    for name in sorted(fresh.glob('*_default_*.ppm')):
        if name.name.startswith(('graphite_delay_','pink_delay_')):continue
        old=OLD/name.name
        if not old.exists():continue
        a,b=Image.open(old).convert('RGB'),Image.open(name).convert('RGB')
        module=name.name.split('_')[1];top=name.stem.endswith('_top')
        cut=(60 if module in ('mic','rec') else 46) if top else 46
        same=ImageChops.difference(a.crop((0,cut,a.width,a.height)),b.crop((0,cut,b.width,b.height))).getbbox() is None
        rows.append({'surface':name.name,'body_y_from':cut,'body_byte_equal':same})
    proof={'status':'passed' if rows and all(r['body_byte_equal'] for r in rows) else 'nonidentical',
           'old_receipt':str(OLD.parent/'receipt.json'),'old_receipt_sha256':hashlib.sha256((OLD.parent/'receipt.json').read_bytes()).hexdigest(),
           'new_receipt':str(pointer['receipt']),'new_receipt_sha256':pointer['sha256'],'surfaces':len(rows),
           'excluded':['DLY previous full-pair redesign','header bands above cut','nondefault specialized workflow','non-module neighboring screens'],
           'compared':rows}
    path=OUT/'default-bodies-vs-older-current.json';path.write_text(json.dumps(proof,indent=2)+'\n')
    print(proof['status'],len(rows),[x for x in rows if not x['body_byte_equal']][:15])

if __name__=='__main__':main()
