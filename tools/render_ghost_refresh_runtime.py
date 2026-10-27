#!/usr/bin/env python3
"""Package fresh, verified ghost C++ captures; no UI repaint, builds or device IO."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
from PIL import Image, PngImagePlugin
from render_module_geometry_study import Canvas

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/ghost_refresh_runtime'
BG=(20,20,23)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-dir',type=Path,default=OUT/'capture')
    args=parser.parse_args()
    raw=args.capture_dir.resolve()
    proof=json.loads((OUT/'sources.json').read_text())
    receipt=json.loads((raw/'capture.json').read_text())
    assert proof['checks']['status']=='passed'
    assert proof['source_sha256']==receipt['source_sha256']
    for name,value in proof['source_sha256'].items():
        assert sha(ROOT/name)==value,name
    assert sha(Path(proof['binary']))==proof['binary_sha256']==receipt['binary_sha256']
    assert receipt['regression_groups_run']==0
    with (raw/'manifest.tsv').open() as stream:
        rows=list(csv.DictReader(stream,delimiter='\t'))
    assert len(rows)==receipt['pairs']
    assert not (OUT/'packaging.json').exists(),'preserve prior package'
    meta=PngImagePlugin.PngInfo()
    meta.add_text('provenance','fresh actual C++ ghost runtime; native pixels untouched; not device footage or audio acceptance')
    meta.add_text('source_sha256',json.dumps(proof['source_sha256'],sort_keys=True),zip=True)
    meta.add_text('binary_sha256',proof['binary_sha256'])
    meta.add_text('capture_receipt_sha256',sha(raw/'capture.json'))
    images={}
    def save(image,name):
        path=OUT/name;assert not path.exists(),path
        path.parent.mkdir(parents=True,exist_ok=True)
        large=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
        assert large.resize(image.size,Image.Resampling.NEAREST).tobytes()==image.tobytes()
        large.save(path,pnginfo=meta)
        with Image.open(path) as decoded:
            decoded.load();assert decoded.size==large.size and decoded.tobytes()==large.tobytes()
        images[name]={'size':list(large.size),'bytes':path.stat().st_size,'sha256':sha(path)}
    columns=4
    sheet=Canvas(16+columns*416,64+((len(rows)+columns-1)//columns)*530,BG)
    sheet.text(16,14,'ghost / memory traces', (244,244,244),scale=2)
    sheet.text(sheet.im.width-16,18,'fresh c++ / native 2x',(177,174,184),right=True)
    for i,row in enumerate(rows):
        assert int(row['top'])+int(row['bottom'])==int(row['shared'])<=8192
        pair=Image.new('RGB',(400,496),BG)
        for screen,width,y,x in [('top',400,0,0),('bottom',320,256,40)]:
            path=raw/(row['frame']+'_'+screen+'.ppm')
            assert sha(path)==receipt['surface_sha256'][path.name],path
            with Image.open(path) as source:
                source.load();native=source.convert('RGB')
            assert native.size==(width,240)
            pair.paste(native,(x,y))
            assert pair.crop((x,y,x+width,y+240)).tobytes()==native.tobytes()
        save(pair,'pairs/'+row['frame']+'-2x.png')
        x=16+(i%columns)*416;y=64+(i//columns)*530
        sheet.text(x,y-16,row['frame'].replace('_',' / '),(177,174,184))
        sheet.im.paste(pair,(x,y))
        assert sheet.im.crop((x,y,x+400,y+496)).tobytes()==pair.tobytes()
    save(sheet.im,'ghost-memory-native-2x.png')
    for name,value in proof['source_sha256'].items():assert sha(ROOT/name)==value,name
    report={'images':images,'source_sha256':proof['source_sha256'],
            'binary_sha256':proof['binary_sha256'],'capture_receipt_sha256':sha(raw/'capture.json'),
            'packager_sha256':sha(Path(__file__)),'native_pairs':len(rows),
            'native_pixels_unmodified':True,'nearest_2x_and_full_decode':True,
            'builds':0,'test_groups':0,'capture_calls':0,'hardware':False}
    (OUT/'packaging.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print(json.dumps(images['ghost-memory-native-2x.png']))
    print(f'{len(rows)} fresh native pairs; {len(images)} decoded PNGs; no painted UI or app operations')

if __name__=='__main__':main()
