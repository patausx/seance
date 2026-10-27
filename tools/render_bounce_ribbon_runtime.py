#!/usr/bin/env python3
"""Package verified bounce ribbon C++ LCDs, with untouched pixels at nearest 2x."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
from PIL import Image, PngImagePlugin
from render_module_geometry_study import Canvas

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/bounce_ribbon_runtime'
BG = (20, 20, 23)
INK = (244, 244, 244)
MUTED = (177, 174, 184)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-local', action='store_true',
                        help='preserve earlier reviews by writing inside the new capture directory')
    args = parser.parse_args()
    proof = json.loads((OUT/'proof.json').read_text())
    raw = Path(proof['capture']['path'])
    destination = raw/'review' if args.capture_local else OUT
    destination.mkdir(parents=True, exist_ok=True)
    receipt = json.loads((raw/'capture.json').read_text())
    assert proof['checks']['status'] == 'passed'
    assert proof['source_sha256'] == receipt['source_sha256']
    assert receipt['regression_groups_run'] == 0
    for name, value in proof['source_sha256'].items():
        assert sha(ROOT/name) == value, name
    for name, value in proof['compiler_dependencies']['sha256'].items():
        assert sha(Path(name) if Path(name).is_absolute() else ROOT/name) == value, name
    assert sha(Path(proof['binary'])) == proof['binary_sha256'] == receipt['binary_sha256']
    assert sha(raw/'manifest.tsv') == receipt['manifest_sha256']
    with (raw/'manifest.tsv').open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    assert len(rows) == receipt['pairs']
    assert not (destination/'packaging.json').exists(), 'preserve previous package'
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', 'actual C++ bounce ribbon, engine fixture waveform; untouched native pixels at nearest2x; not device footage')
    meta.add_text('source_sha256', json.dumps(proof['source_sha256'], sort_keys=True), zip=True)
    meta.add_text('binary_sha256', proof['binary_sha256'])
    meta.add_text('capture_receipt_sha256', sha(raw/'capture.json'))
    images, pairs, lower = {}, {}, {}

    def save(image, name):
        path = destination/name
        assert not path.exists(), path
        path.parent.mkdir(parents=True, exist_ok=True)
        large = image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST)
        assert large.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
        large.save(path, pnginfo=meta)
        with Image.open(path) as decoded:
            decoded.load()
            assert decoded.size == large.size and decoded.tobytes() == large.tobytes()
        images[name] = {'size': list(large.size), 'bytes': path.stat().st_size, 'sha256': sha(path)}

    for row in rows:
        name = row['frame']
        assert int(row['top'])+int(row['bottom']) == int(row['shared']) <= 8192
        pair = Image.new('RGB', (400, 496), BG)
        for screen, width, x, y in [('top',400,0,0), ('bottom',320,40,256)]:
            path = raw/(name+'_'+screen+'.ppm')
            assert sha(path) == receipt['surface_sha256'][path.name], path
            with Image.open(path) as source:
                source.load()
                native = source.convert('RGB')
            assert native.size == (width,240)
            pair.paste(native, (x,y))
            assert pair.crop((x,y,x+width,y+240)).tobytes() == native.tobytes()
            if screen == 'bottom':
                lower[name] = native
        pairs[name] = pair
        save(pair, 'pairs/'+name+'-2x.png')

    def board(names, title, filename, full=False, columns=2):
        width, height = (400,496) if full else (320,240)
        sheet = Canvas(24+columns*(width+24), 74+((len(names)+columns-1)//columns)*(height+38), BG)
        sheet.text(24,14,title,INK,scale=2)
        sheet.text(24,42,'actual c++ / native 2x / local build',MUTED)
        for i, name in enumerate(names):
            x=24+(i%columns)*(width+24);y=74+(i//columns)*(height+38)
            sheet.text(x,y-15,name.replace('_',' / '),MUTED)
            image=(pairs if full else lower)[name]
            sheet.im.paste(image,(x,y))
            assert sheet.im.crop((x,y,x+width,y+height)).tobytes()==image.tobytes()
        save(sheet.im,filename)

    for theme in ('graphite','pink'):
        names = [theme+'_'+state for state in ('setup','recording','take','saved')]
        board(names, 'bounce / calm ribbon', theme+'-bounce-lower-2x.png')
        board(names, 'bounce / calm ribbon', theme+'-bounce-dual-2x.png', full=True)
        service = [row['frame'] for row in rows if row['frame'].startswith(theme+'_') and row['frame'] not in names]
        if service:
            board(service,'bounce / other states',theme+'-bounce-states-2x.png',columns=4)
    for name,value in proof['source_sha256'].items():
        assert sha(ROOT/name)==value,name
    report = {'images':images, 'source_sha256':proof['source_sha256'],
              'binary_sha256':proof['binary_sha256'], 'capture_receipt_sha256':sha(raw/'capture.json'),
              'packager_sha256':sha(Path(__file__)), 'native_pairs':len(rows),
              'native_pixels_unmodified':True, 'nearest_2x_and_full_decode':True,
              'builds':0, 'test_groups':0, 'capture_calls':0, 'hardware':False}
    (destination/'packaging.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print(f'{len(rows)} fresh C++ pairs, {len(images)} decoded nearest2x PNGs; no UI repaint')
    print(destination/'graphite-bounce-lower-2x.png')


if __name__ == '__main__':
    main()
