#!/usr/bin/env python3
"""Scoped REV field native host UI/controller capture. No old suites, ARM or device.

Retains actual compiled dependency payloads and compares optimized/sanitized raw
rasters. GIFs package native C++ pixels; wet/return traces come from real core
processing synthetic input, never authored envelopes or a physical microphone.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
sys.dont_write_bytecode = True
import test_module_headings_ui as host
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'release/reverb-field/ui'
OWN = ROOT / 'tests/reverb_field_ui_cases.inc'
HEADER = ROOT / 'tests/module_headings_ui_cases.inc'


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def save(p, value):
    p.write_text(json.dumps(value, indent=2, sort_keys=True)+'\n')


def configure():
    OUT.mkdir(parents=True, exist_ok=True)
    text = HEADER.read_text().replace('int main(', 'int unused_rev_field_header_main(', 1)+'\n'+OWN.read_text()
    path = OUT / ('cases-'+hashlib.sha256(text.encode()).hexdigest()[:16]+'.inc')
    if path.exists():
        assert path.read_text() == text
    else:
        path.write_text(text)
    host.OUT, host.CASES = OUT, path
    host.supplement = lambda: {}


def number(p, value):
    v = max(0, min(32767, value))
    if p == 1:
        ms = 200+(7800*v*v+1073676289//2)//1073676289
        cs = (ms+5)//10
        return f'{cs//100}.{cs%100:02d}s'
    if p == 4:
        return 'on' if v > 16384 else 'off'
    if p == 5:
        return str((150*v+16383)//32767)+'ms'
    if p == 6:
        return str(20+(980*v*v+536838144)//1073676289)+'hz' if v else 'off'
    return str((100*v+16383)//32767)+'%'


def inspect(proof):
    folder = Path(proof['capture'])
    for row in proof['surfaces']:
        assert int(row['outside_rects']) == int(row['outside_lines']) == int(row['text_overlaps']) == 0, row
    src = (ROOT/'platform/3ds/module_heading_data.h').read_text()
    raw = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', src.split('bitmap[] = {', 1)[1].split('};', 1)[0]))
    off,w,h = map(int,re.search(r'\{UnitKind::Reverb, "rev", (\d+), (\d+), (\d+)\}', src).groups())
    bits = [bool(raw[off+y*((w+7)//8)+x//8] & (128>>(x%8))) for y in range(h) for x in range(w)]
    for row in proof['surfaces']:
        with Image.open(folder/(row['id']+'_'+row['surface']+'.ppm')) as im:
            crop = list(im.crop((14,2,14+w,2+h)).get_flattened_data())
            ink = {v for v,b in zip(crop,bits) if b}
            assert len(ink)==1, row
            ink = next(iter(ink))
            assert [v==ink for v in crop] == bits, row
    numbers = list(csv.DictReader((folder/'numbers.tsv').open(), delimiter='\t'))
    assert len(numbers)==2*24*7
    changes = {}
    for row in numbers:
        text = list(csv.DictReader((folder/(row['id']+'_bottom_text.tsv')).open(),delimiter='\t'))
        p, i = int(row['param']), int(row['row'])
        expected = number(p, int(row['live'])); base='base '+number(p,int(row['base']))
        assert any(r['text']==expected and 47+37*i <= int(r['y']) < 55+37*i and int(r['x'])>=230 for r in text),(row,expected)
        assert any(r['text']==base and 63+37*i <= int(r['y']) < 71+37*i and int(r['x'])==16 for r in text),(row,base)
        assert any(r['text']=='a' and 47+37*i <= int(r['y']) < 55+37*i for r in text),row
        changes.setdefault((row['id'].split('_')[0], p), set()).add(expected)
    assert all(len(values)>=4 for values in changes.values()),changes
    motion = list(csv.DictReader((folder/'motion.tsv').open(),delimiter='\t'))
    assert len(motion)==200
    for theme in ('graphite_', 'pink_'):
        rows = [r for r in motion if r['theme']==theme]
        assert len(rows)==100
        assert all(int(r['wet'])==0 and all(int(r['return'+str(i)])==0 for i in range(4)) for r in rows[:8])
        assert any(len({int(r['return'+str(i)]) for i in range(4)})>1 for r in rows)
        assert all(int(rows[n]['freeze'])==32767 for n in range(40,66))
        assert all(int(rows[n]['freeze'])==0 for n in range(67,100))
        for side in ('top','bottom'):
            assert sha(folder/(theme+'rev_idle-first_'+side+'.ppm'))==sha(folder/(theme+'rev_idle-later_'+side+'.ppm'))
        images=[]
        for row in rows:
            with Image.open(folder/(row['id']+'.png')) as im:
                images.append(im.convert('RGB'))
        strip=Image.new('RGB',(400,496*len(images)))
        for i,im in enumerate(images):strip.paste(im,(0,496*i))
        palette=strip.quantize(colors=128,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
        frames=[im.quantize(palette=palette,dither=Image.Dither.NONE).resize((800,992),Image.Resampling.NEAREST) for im in images]
        frames[0].save(folder/(theme+'real-tail.gif'),save_all=True,append_images=frames[1:],duration=80,loop=0,optimize=False,disposal=2)
        for name in ('default','more','held','legacy','lens','more-lens','settling','motion_10'):
            with Image.open(folder/(theme+'rev_'+name+'.png')) as im:
                im.resize((800,992),Image.Resampling.NEAREST).save(folder/(theme+'rev_'+name+'_2x.png'))
    return dict(fixed_header_masks=len(proof['surfaces']),numeric_base_source_checks=len(numbers),
                behavior=json.loads((folder/'behavior.json').read_text()),
                audio_step_samples=2560,audio_step_ms=80,gif_frame_ms=80,
                native_ui=True,physical_capture=False,synthetic_input=True,
                presentation={p.name:sha(p) for p in folder.glob('*.gif')})


def retain_dependencies(proof):
    folder=Path(proof['binary']).parent/'inputs'
    entries={}
    paths=set(proof['dependencies']) | {str(ROOT/n) for n in proof['source_sha256']}
    paths|={str(OWN),str(Path(__file__).resolve()),str(ROOT/'tools/generate_reverb_field.py')}
    for name in sorted(paths):
        p=Path(name).resolve()
        try:rel=p.relative_to(ROOT)
        except ValueError:continue
        # Private generated driver/dependencies inside this result are already
        # retained as artifacts; preserve a short disjoint pathname for them.
        if 'release' in rel.parts:rel=Path('_generated')/p.name
        dest=folder/rel;dest.parent.mkdir(parents=True,exist_ok=True)
        assert not dest.exists(),dest
        shutil.copy2(p,dest);assert sha(p)==sha(dest)
        entries[str(rel)]={'origin':str(p),'sha256':sha(dest)}
    save(folder.parent/'retained-inputs.json',entries)
    return {'files':len(entries),'manifest_sha256':sha(folder.parent/'retained-inputs.json')}


def latest(tag):
    ref=json.loads((OUT/(tag+'-latest.json')).read_text());assert sha(ref['receipt'])==ref['sha256']
    return json.loads(Path(ref['receipt']).read_text())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('optimized','sanitized','compare'),default='optimized')
    args=parser.parse_args();configure()
    if args.mode=='compare':
        a,b=latest('optimized'),latest('sanitized')
        assert a['source_sha256']==b['source_sha256']==host.pins(ROOT)
        assert a['case_sha256']==b['case_sha256']==sha(host.CASES)
        aa={r['id']+'_'+r['surface']:r['raster_fnv64'] for r in a['surfaces']}
        bb={r['id']+'_'+r['surface']:r['raster_fnv64'] for r in b['surfaces']}
        assert aa==bb
        for n in aa:assert sha(Path(a['capture'])/(n+'.ppm'))==sha(Path(b['capture'])/(n+'.ppm'))
        dest=OUT/('compare-'+host.stamp()+'.json')
        save(dest,dict(status='passed',raw_surfaces_equal=len(aa),source_sha256=a['source_sha256'],host_only=True))
        print(dest);return
    receipt,proof=host.build(ROOT,args.mode,args.mode=='sanitized',True)
    dest=OUT/('inspection-'+args.mode+'-'+host.stamp()+'.json')
    result=dict(receipt=str(receipt),receipt_sha256=sha(receipt),runner_sha256=sha(__file__),status='pending')
    save(dest,result)
    try:
        result['inspection']=inspect(proof)
        result['retained']=retain_dependencies(proof)
        result['status']='passed'
    except Exception as error:
        result['status']='failed';result['failure']=str(error);save(dest,result);raise
    save(dest,result);print(dest)


if __name__=='__main__':main()
