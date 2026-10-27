#!/usr/bin/env python3
"""Package fresh actual C++ REV screens only. No painted/reused UI pixels or tests."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, PngImagePlugin
from test_reverb_dense_ui import ROOT, sources, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('/tmp/seance_rev_dense_ui_san'))
    parser.add_argument('--output', type=Path, default=ROOT / 'ui_mockups/reverb_dense_runtime')
    args = parser.parse_args()
    binary = args.binary.resolve()
    proof = json.loads(binary.with_suffix('.sources.json').read_text())
    before = sources()
    assert before == proof['source_sha256'] and sha(binary) == proof['binary_sha256'], 'rebuild current UI proof first'
    out = args.output.resolve()
    raw = out / 'capture'
    raw.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    subprocess.run([str(binary), '--capture-rev-dense', str(raw)], cwd=ROOT, env=env, check=True)
    assert before == sources(), 'production changed during capture'
    rows = list(csv.DictReader((raw / 'manifest.tsv').open(), delimiter='\t'))
    assert len(rows) == 8
    outside = (29, 30, 30)
    chosen = ['02_dense_tail', '03_small_space', '04_dark_long', '05_held', '07_legacy', '08_bypassed']
    sheet = Image.new('RGB', (3 * 432, 2 * 530), outside)
    draw = ImageDraw.Draw(sheet)
    surfaces = {}
    for row in rows:
        name = row['frame']
        pair = []
        for screen, width in (('top', 400), ('bottom', 320)):
            path = raw / f'{name}_{screen}.ppm'
            with Image.open(path) as source:
                source.load()
                image = source.convert('RGB')
            assert image.size == (width, 240)
            surfaces[path.name] = sha(path)
            pair.append(image)
            image.save(raw / f'{name}_{screen}.png')
        if name == '02_dense_tail':
            dual = Image.new('RGB', (400, 496), outside)
            dual.paste(pair[0], (0, 0))
            dual.paste(pair[1], (40, 256))
            dual.resize((800, 992), Image.Resampling.NEAREST).save(out / 'reverb-dense-dual-2x.png')
        if name in chosen:
            i = chosen.index(name)
            x, y = (i % 3) * 432 + 16, (i // 3) * 530 + 26
            draw.text((x, y - 18), name[3:].replace('_', ' '), fill=(201, 201, 193))
            sheet.paste(pair[0], (x, y))
            sheet.paste(pair[1], (x + 40, y + 254))
    native = sheet
    sheet = native.resize((native.width * 2, native.height * 2), Image.Resampling.NEAREST)
    assert sheet.resize(native.size, Image.Resampling.NEAREST).tobytes() == native.tobytes()
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text('provenance', 'fresh current C++ native surfaces; host Citro2D raster, not device footage')
    metadata.add_text('source_sha256', json.dumps(before, sort_keys=True))
    metadata.add_text('binary_sha256', proof['binary_sha256'])
    path = out / 'reverb-dense-native-2x.png'
    sheet.save(path, pnginfo=metadata)
    with Image.open(path) as decoded:
        decoded.load()
        assert decoded.size == sheet.size and decoded.tobytes() == sheet.tobytes()
    report = {'source_sha256': before, 'binary_sha256': proof['binary_sha256'],
              'surface_sha256': surfaces, 'captures': rows, 'sheet_sha256': sha(path),
              'host_only': True, 'nearest_2x': True, 'no_tests_run_by_render': True,
              'packager_sha256': sha(Path(__file__))}
    (out / 'sources.json').write_text(json.dumps(report, indent=2) + '\n')
    print(path)
    print(f'{sheet.width}x{sheet.height} / {path.stat().st_size} bytes / sha256 {sha(path)}')


if __name__ == '__main__':
    main()
