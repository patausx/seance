#!/usr/bin/env python3
"""Package verified actual C++ filter captures, never repaint LCD pixels."""
import csv
import hashlib
import json
from pathlib import Path
from PIL import Image,PngImagePlugin
from render_module_geometry_study import Canvas
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/filter_interaction_runtime'
BG=(18,18,21)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    proof=json.loads((OUT/'proof.json').read_text());assert proof['checks']['status']=='passed'
    raw=Path(proof['capture']['path']);receipt=json.loads((raw/'capture.json').read_text())
    assert receipt['source_sha256']==proof['source_sha256']
    assert receipt['binary_sha256']==proof['binary_sha256']==sha(proof['binary'])
    assert receipt['manifest_sha256']==sha(raw/'manifest.tsv') and receipt['regression_groups_run']==0
    for name,h in proof['source_sha256'].items():assert sha(ROOT/name)==h,name
    for name,h in proof['compiler_dependencies']['sha256'].items():assert sha(Path(name) if Path(name).is_absolute() else ROOT/name)==h,name
    destination=raw/'review';destination.mkdir(exist_ok=False)
    meta=PngImagePlugin.PngInfo()
    meta.add_text('provenance','actual C++ filter editor/captured engine scalars; calculated linear post-drive response, not PCM/spectrum; host static paper, not hardware; LCD pixels unchanged at nearest2x')
    meta.add_text('proof_sha256',sha(OUT/'proof.json'));meta.add_text('capture_sha256',sha(raw/'capture.json'))
    pairs={};tops={};outputs=[]
    with (raw/'manifest.tsv').open() as stream:rows=list(csv.DictReader(stream,delimiter='\t'))
    assert len(rows)==receipt['pairs']
    for row in rows:
        name=row['frame'];assert int(row['top'])+int(row['bottom'])==int(row['shared'])<=8192
        pair=Image.new('RGB',(400,496),BG)
        for screen,w,x,y in [('top',400,0,0),('bottom',320,40,256)]:
            path=raw/(name+'_'+screen+'.ppm');assert sha(path)==receipt['surface_sha256'][path.name]
            with Image.open(path) as image:im=image.convert('RGB');im.load()
            assert im.size==(w,240);pair.paste(im,(x,y));assert pair.crop((x,y,x+w,y+240)).tobytes()==im.tobytes()
            if screen=='top':tops[name]=im
        pairs[name]=pair
    def save(image,name):
        path=destination/name;assert not path.exists();big=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST);big.save(path,pnginfo=meta)
        with Image.open(path) as decoded:
            assert decoded.tobytes()==big.tobytes() and decoded.resize(image.size,Image.Resampling.NEAREST).tobytes()==image.tobytes()
        outputs.append(dict(path=str(path.relative_to(ROOT)),sha256=sha(path),bytes=path.stat().st_size,size=list(big.size)))
    def board(theme,names,title,filename,images):
        h=images[theme+'_'+names[0]].height;pitch=h+36
        c=Canvas(872,78+2*pitch,BG);c.text(24,14,title,(244,244,244),scale=2)
        c.text(24,42,'actual c++ / native 2x / local only',(184,184,184))
        for i,name in enumerate(names):
            im=images[theme+'_'+name];x=24+(i%2)*424;y=78+(i//2)*pitch
            c.text(x,y-14,name.replace('_',' / '),(181,154,232));c.im.paste(im,(x,y));assert c.im.crop((x,y,x+im.width,y+h)).tobytes()==im.tobytes()
        save(c.im,filename)
    for theme in ('graphite','pink'):
        save(pairs[theme+'_low'],theme+'-filter-dual-2x.png')
        board(theme,['low','resonance','morph_half','modulation'],'filter / reactive upper',theme+'-filter-upper-2x.png',tops)
        board(theme,['drive','ceiling','lock','mod_lens'],'filter / selected and service',theme+'-filter-service-2x.png',pairs)
    (destination/'packaging.json').write_text(json.dumps(dict(outputs=outputs,source_proof_sha256=sha(OUT/'proof.json'),
        capture_sha256=sha(raw/'capture.json'),packager_sha256=sha(__file__),native_pairs=len(rows),native_pixels_unmodified=True,
        nn2x_decode=True,app_builds=0,capture_calls=0,regression_groups=0,hardware=False),indent=2)+'\n')
    print(destination)
    for o in outputs:print(o['path'],o['size'],o['sha256'])
if __name__=='__main__':main()
