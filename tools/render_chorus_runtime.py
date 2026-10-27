#!/usr/bin/env python3
"""Package actual C++ chorus frames, never draw or model instrument UI in Python."""
from pathlib import Path
import csv
import json
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/chorus_double_runtime'
BINARY = '/tmp/seance_chorus_ui_test'


def main():
    raw = OUT / 'raw'
    raw.mkdir(parents=True, exist_ok=True)
    subprocess.run([BINARY, '--dump-chorus', str(raw)], cwd=ROOT, check=True)
    rows = list(csv.DictReader((raw / 'manifest.tsv').open(), delimiter='\t'))
    frames = []
    for row in rows:
        name = row['frame']
        top = Image.open(raw / f'{name}_top.ppm').convert('RGB')
        bottom = Image.open(raw / f'{name}_bottom.ppm').convert('RGB')
        assert top.size == (400, 240) and bottom.size == (320, 240)
        dual = Image.new('RGB', (400, 496), (24, 20, 29))
        dual.paste(top, (0, 0)); dual.paste(bottom, (40, 256))
        if name == 'chorus_14':
            dual.save(OUT / 'chorus_dual_native.png')
            dual.resize((800, 992), Image.Resampling.NEAREST).save(OUT / 'chorus_dual_2x.png')
            bottom.resize((960, 720), Image.Resampling.NEAREST).save(OUT / 'chorus_bottom_3x.png')
        frames.append(dual.resize((800, 992), Image.Resampling.NEAREST))
    colors = sorted(set(frames[0].get_flattened_data()))
    assert len(colors) <= 256
    palette = Image.new('P', (1, 1))
    flat = [channel for color in colors for channel in color]
    palette.putpalette(flat + [0] * (768-len(flat)))
    gifs = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in frames]
    gifs[0].save(OUT / 'chorus_motion.gif', save_all=True, append_images=gifs[1:],
                 loop=0, duration=50, disposal=2, optimize=False)
    receipt = {'origin': 'actual C++ software framebuffer; not hardware/LCD capture',
               'figure': 'schematic common-source shape shifted by actual last-processed DSP delays, not PCM',
               'frames': len(rows),
               'peak_shared_objects': max(int(r['top_objects'])+int(r['bottom_objects']) for r in rows),
               'scope': 'visible chorus lower + selected inspector, no hidden main Chains underlay',
               'ui_pixels_authored_in_python': False}
    (OUT / 'capture.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
