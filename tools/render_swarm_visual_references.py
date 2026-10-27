#!/usr/bin/env python3
"""Reference board of credited third-party screenshots, NOT a seance UI render.

All source screenshots stay untouched. Crops are for visual comparison only;
none of their pixels, fonts or assets are proposed for shipping in seance.
"""
from pathlib import Path
import hashlib
import json
from PIL import Image, ImageDraw, ImageFont, PngImagePlugin

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'ui_mockups/swarm_v2_study/visual-research'
REF = OUT/'references'


def main():
    path = OUT/'reference-board.png'
    assert not path.exists(), 'preserve previous review boards'
    font_path = Path('/usr/share/fonts/TTF/DejaVuSans.ttf')
    if not font_path.exists():
        import subprocess
        font_path = Path(subprocess.check_output(['fc-match','-f','%{file}','sans'],text=True).strip())
    heading = ImageFont.truetype(str(font_path),26)
    label = ImageFont.truetype(str(font_path),20)
    small = ImageFont.truetype(str(font_path),15)
    board = Image.new('RGB',(1000,830),(18,18,21))
    d = ImageDraw.Draw(board)
    ink, accent, muted = (237,235,239),(181,154,232),(167,164,174)
    d.text((24,16),'swarm / visual references',font=heading,fill=ink)
    d.text((976,27),'third-party screenshots / not a seance render',font=small,fill=muted,anchor='rt')
    entries = [
        dict(file='quanta2.png',name='01 / quanta 2',by='audio damage',
             box=(40,308,990,508),at=(24,88),width=952,
             label_y=60,source='https://www.audiodamage.com/products/ad055-quanta-2'),
        dict(file='borderlands-ipad-6.jpg',name='02 / borderlands granular',by='chris carlson',
             box=(70,106,492,309),at=(24,344),width=448,
             label_y=315,source='https://apps.apple.com/us/app/borderlands-granular/id561369733'),
        dict(file='fragments-release.jpg',name='03 / efx fragments',by='arturia',
             box=(466,618,1335,744),at=(24,636),width=952,
             label_y=602,source='https://www.arturia.com/products/software-effects/efx-fragments/overview')]
    for e in entries:
        d.text((24,e['label_y']),e['name'],font=label,fill=accent)
        d.text((976,e['label_y']+3),e['by'],font=small,fill=muted,anchor='rt')
        p=REF/e['file']
        source=Image.open(p).convert('RGB')
        crop=source.crop(e['box'])
        display=crop.resize((e['width'],round(crop.height*e['width']/crop.width)),Image.Resampling.LANCZOS)
        board.paste(display,e['at'])
        e['sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
    for i,text in enumerate(('pulsing clouds over sound material',
                             'blue rings = cloud controls',
                             'small inner particles = grain activity')):
        d.text((504,383+i*29),text,font=small,fill=ink if i==0 else muted)
    d.text((24,790),'reference artwork belongs to its respective authors. no production or engine changes.',font=small,fill=muted)
    meta=PngImagePlugin.PngInfo();meta.add_text('provenance',__doc__)
    board.save(path,pnginfo=meta)
    (OUT/'board-sources.json').write_text(json.dumps(dict(status=__doc__,sources=entries),indent=2)+'\n')
    print(path)


if __name__=='__main__':
    main()
