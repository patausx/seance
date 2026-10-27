#!/usr/bin/env python3
"""Package actual VCA C++ captures; never author UI pixels or simulate its DSP."""
from pathlib import Path
import csv
import hashlib
import json
import subprocess
from PIL import Image
from render_module_geometry_study import pair, OUTSIDE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/vca_flexible_runtime'
RAW = OUT / 'raw'


def main():
    RAW.mkdir(parents=True, exist_ok=True)
    subprocess.run(['/tmp/seance_chorus_ui_test', '--dump-vca', str(RAW)], cwd=ROOT, check=True)
    rows = list(csv.DictReader((RAW / 'manifest.tsv').open(), delimiter='\t'))
    frames = []
    for row in rows:
        name = row['frame']
        top = Image.open(RAW / f'{name}_top.ppm').convert('RGB')
        bottom = Image.open(RAW / f'{name}_bottom.ppm').convert('RGB')
        assert top.size == (400, 240) and bottom.size == (320, 240)
        assert int(row['top_objects']) + int(row['bottom_objects']) <= 8192
        dual = pair(top, bottom)
        if name.startswith('motion_'):
            frames.append(dual.resize((800, 992), Image.Resampling.NEAREST))
        else:
            for suffix, image in [('top', top), ('bottom', bottom), ('dual', dual)]:
                image.save(OUT / f'{name}_{suffix}_native.png')
                image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST).save(OUT / f'{name}_{suffix}_2x.png')
    assert len(frames) == 40
    colors = sorted(set().union(*(set(frame.get_flattened_data()) for frame in frames)))
    assert len(colors) <= 256
    palette = Image.new('P', (1, 1))
    flat = [v for color in colors for v in color]
    palette.putpalette(flat + [0]*(768-len(flat)))
    gifs = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
    gifs[0].save(OUT / 'vca_motion.gif', save_all=True, append_images=gifs[1:], duration=100, loop=0, disposal=2, optimize=False)
    comparison = Image.new('RGB', (816, 1008), OUTSIDE)
    for i, name in enumerate(['01_open', '02_hold', '03_duck', '04_curve']):
        comparison.paste(Image.open(OUT / f'{name}_dual_native.png'), (8+(i%2)*400, 8+(i//2)*496))
    comparison.save(OUT / 'comparison_native.png')
    comparison.resize((1632, 2016), Image.Resampling.NEAREST).save(OUT / 'comparison_2x.png')
    (OUT / 'capture.json').write_text(json.dumps({
        'source': 'actual C++ VCA DSP/controller/rendering with host software Citro2D raster; not LCD capture',
        'harness_sha256': hashlib.sha256(Path('/tmp/seance_chorus_ui_test').read_bytes()).hexdigest(),
        'frames': len(rows), 'motion': '40 actual 100ms audio advances; open one-shot then duck one-shot, with idle',
        'peak_focused_pair_primitives': max(int(row['top_objects'])+int(row['bottom_objects']) for row in rows),
        'limits': 'focused pair only; excludes main hidden Chains underlay; hardware timing/audio not measured',
    }, indent=2)+'\n')
    print(OUT / '01_open_dual_2x.png')
    print(OUT / 'vca_motion.gif')


if __name__ == '__main__':
    main()
