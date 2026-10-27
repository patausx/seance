#!/usr/bin/env python3
"""Package verified PLAY mode C++ captures without painting any LCD pixels."""
import csv
import hashlib
import json
from pathlib import Path
from PIL import Image, PngImagePlugin
from render_module_geometry_study import Canvas

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/play_modes_runtime'
BG=(18,18,21)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()


def main():
    proof=json.loads((OUT/'proof.json').read_text())
    assert proof['checks']['status']=='passed'
    raw=Path(proof['capture']['path']); receipt=json.loads((raw/'capture.json').read_text())
    assert receipt['source_sha256']==proof['source_sha256']
    assert receipt['binary_sha256']==proof['binary_sha256']==sha(proof['binary'])
    assert receipt['manifest_sha256']==sha(raw/'manifest.tsv') and receipt['regression_groups_run']==0
    for name,h in proof['source_sha256'].items():assert sha(ROOT/name)==h,name
    for name,h in proof['compiler_dependencies']['sha256'].items():assert sha(Path(name) if Path(name).is_absolute() else ROOT/name)==h,name
    destination=raw/'review';destination.mkdir(exist_ok=False)
    meta=PngImagePlugin.PngInfo()
    meta.add_text('provenance','actual C++ PLAY editor; synthetic PCM engine fixture; host static paper, not hardware; unchanged LCD pixels at nearest2x')
    meta.add_text('proof_sha256',sha(OUT/'proof.json'))
    meta.add_text('capture_sha256',sha(raw/'capture.json'))
    lower={};pairs={};outputs=[]
    with (raw/'manifest.tsv').open() as stream:rows=list(csv.DictReader(stream,delimiter='\t'))
    assert len(rows)==receipt['pairs']
    for row in rows:
        name=row['frame'];assert int(row['top'])+int(row['bottom'])==int(row['shared'])<=8192
        pair=Image.new('RGB',(400,496),BG)
        for screen,w,x,y in [('top',400,0,0),('bottom',320,40,256)]:
            path=raw/(name+'_'+screen+'.ppm');assert sha(path)==receipt['surface_sha256'][path.name]
            with Image.open(path) as image:im=image.convert('RGB');im.load()
            assert im.size==(w,240)
            pair.paste(im,(x,y));assert pair.crop((x,y,x+w,y+240)).tobytes()==im.tobytes()
            if screen=='bottom':lower[name]=im
        pairs[name]=pair
    def save(image,name):
        path=destination/name;assert not path.exists()
        large=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
        large.save(path,pnginfo=meta)
        with Image.open(path) as decoded:
            decoded.load();assert decoded.tobytes()==large.tobytes()
            assert decoded.resize(image.size,Image.Resampling.NEAREST).tobytes()==image.tobytes()
        outputs.append(dict(path=str(path.relative_to(ROOT)),sha256=sha(path),bytes=path.stat().st_size,size=list(large.size)))
    def board(names,title,filename,columns=3):
        c=Canvas(24+columns*344,74+((len(names)+columns-1)//columns)*278,BG)
        c.text(24,14,title,(244,244,244),scale=2)
        c.text(24,42,'actual c++ / native 2x / local only',(180,180,180))
        for i,name in enumerate(names):
            x=24+(i%columns)*344;y=74+(i//columns)*278
            c.text(x,y-15,name.replace('_',' / '),(181,154,232))
            c.im.paste(lower[name],(x,y));assert c.im.crop((x,y,x+320,y+240)).tobytes()==lower[name].tobytes()
        save(c.im,filename)
    for theme in ('graphite','pink'):
        main=[theme+'_'+n for n in ('normal','stretch','tape','scratch','hold','config_voice')]
        detail=[theme+'_'+n for n in ('stretch_slices','loop','trim_edit','config_voice','config_listen','config_file')]
        board(main,'play / modes',theme+'-play-modes-2x.png')
        board(detail,'play / tools and config',theme+'-play-tools-config-2x.png')
        service=[r['frame'] for r in rows if r['frame'].startswith(theme+'_') and r['frame'] not in main+detail]
        board(service,'play / editing sheets',theme+'-play-editing-2x.png')
        save(pairs[theme+'_stretch'],theme+'-play-stretch-dual-2x.png')
    report=dict(outputs=outputs,source_proof_sha256=sha(OUT/'proof.json'),capture_receipt_sha256=sha(raw/'capture.json'),
                packager_sha256=sha(__file__),native_pairs=len(rows),native_pixels_unmodified=True,
                native_nn2x_decode=True,app_builds=0,capture_calls=0,test_groups=0,hardware=False)
    (destination/'packaging.json').write_text(json.dumps(report,indent=2)+'\n')
    print(destination)
    for o in outputs:print(o['path'],o['size'],o['sha256'])


if __name__=='__main__':main()
