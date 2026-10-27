#!/usr/bin/env python3
"""Package current C++ cabinet PPMs only; no authored replacement UI pixels."""
from pathlib import Path
import hashlib,json
from PIL import Image,PngImagePlugin
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/start_menu_cabinet_runtime/implemented'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    proof=json.loads((OUT/'proof.json').read_text())
    for n,h in proof['sources'].items():assert sha(ROOT/n)==h,n
    for n,h in proof['capture'].items():assert sha(ROOT/n)==h,n
    outputs=[]
    def save(image,name):
        path=OUT/name;assert not path.exists(),name
        big=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
        meta=PngImagePlugin.PngInfo();meta.add_text('provenance',json.dumps(dict(
            actual_cpp=True,proof_sha256=sha(OUT/'proof.json'),packager_sha256=sha(__file__),
            limitations='software host coverage and 20px background fallback, not hardware or new paper design')))
        big.save(path,pnginfo=meta)
        with Image.open(path) as p:
            p.load();assert p.tobytes()==big.tobytes()
            assert p.resize(image.size,Image.Resampling.NEAREST).tobytes()==image.tobytes()
        outputs.append(dict(path=str(path.relative_to(ROOT)),sha256=sha(path),bytes=path.stat().st_size))
    for theme in ['graphite','pink']:
        frames={}
        for bottom in sorted((OUT/'capture'/theme).glob('*_bottom.ppm')):
            name=bottom.stem.removesuffix('_bottom')
            with Image.open(bottom) as im:lower=im.convert('RGB')
            with Image.open(bottom.with_name(name+'_top.ppm')) as im:upper=im.convert('RGB')
            assert lower.size==(320,240) and upper.size==(400,240)
            frames[name]=lower
            pair=Image.new('RGB',(400,496),(20,20,22));pair.paste(upper,(0,0));pair.paste(lower,(40,256))
            save(pair,theme+'-'+name+'-dual-2x.png')
        board=Image.new('RGB',(672,512),(20,20,22))
        for i,name in enumerate(['03-library','07-settings','08-palette','05-browser']):
            board.paste(frames[name],(8+(i%2)*336,8+(i//2)*256))
        save(board,theme+'-cabinet-review-2x.png')
        save(frames['03-library'],theme+'-library-lower-2x.png')
    (OUT/'packaging.json').write_text(json.dumps(dict(source_proof=sha(OUT/'proof.json'),outputs=outputs,
        validation='24 PNGs decoded, exact native NN2x; all source/PPM hashes verified'),indent=2)+'\n')
    print('packaged',len(outputs),'current C++ PNGs at',OUT)
if __name__=='__main__':main()
