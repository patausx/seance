#!/usr/bin/env python3
"""Package verified fresh minimal-L runtime captures; never paint UI or run builds."""
import argparse
import csv
import json
from pathlib import Path
from PIL import Image, PngImagePlugin
from render_module_geometry_study import Canvas
from test_mod_minimal_ui import ROOT, OUT, PROOF, sources, sha

BG = (20, 20, 23)
INK = (244, 244, 244)
MUTED = (177, 174, 184)


def pair(top, bottom):
    canvas = Image.new('RGB', (400, 496), BG)
    canvas.paste(top, (0, 0)); canvas.paste(bottom, (40, 256))
    assert canvas.crop((0, 0, 400, 240)).tobytes() == top.tobytes()
    assert canvas.crop((40, 256, 360, 496)).tobytes() == bottom.tobytes()
    return canvas


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-dir', type=Path, default=OUT / 'capture')
    parser.add_argument('--output', type=Path, default=OUT)
    args = parser.parse_args()
    raw, out = args.capture_dir.resolve(), args.output.resolve()
    proof = json.loads(PROOF.read_text()); receipt = json.loads((raw / 'capture.json').read_text())
    before = sources()
    assert proof['checks']['status'] == 'passed'
    assert before == proof['source_sha256'] == receipt['source_sha256']
    assert sha(Path(proof['binary'])) == proof['binary_sha256'] == receipt['binary_sha256']
    assert receipt['regression_groups_run'] == 0
    assert sha(raw / 'manifest.tsv') == receipt['manifest_sha256']
    assert not (out / 'packaging.json').exists(), 'preserve earlier package; choose another output'
    with (raw / 'manifest.tsv').open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    assert len(rows) == receipt['pairs']
    native = {}
    for row in rows:
        assert int(row['top']) + int(row['bottom']) == int(row['shared']) <= 8192
        images = []
        for screen, width in (('top', 400), ('bottom', 320)):
            path = raw / (row['frame'] + '_' + screen + '.ppm')
            assert sha(path) == receipt['surface_sha256'][path.name]
            with Image.open(path) as image:
                image.load(); images.append(image.convert('RGB'))
            assert images[-1].size == (width, 240)
        native[row['frame']] = images
    assert len(receipt['surface_sha256']) == len(native) * 2
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text('provenance', 'fresh current actual C++ minimal-L runtime; native UI pixels untouched; outer labels only; host raster, not hardware footage')
    metadata.add_text('source_sha256', json.dumps(before, sort_keys=True), zip=True)
    metadata.add_text('binary_sha256', proof['binary_sha256'])
    metadata.add_text('capture_receipt_sha256', sha(raw / 'capture.json'))
    metadata.add_text('packager_sha256', sha(Path(__file__)))
    encoded = {}

    def save(image, name):
        path = out / name
        assert not path.exists(), path
        path.parent.mkdir(parents=True, exist_ok=True)
        large = image.resize((2 * image.width, 2 * image.height), Image.Resampling.NEAREST)
        assert large.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
        large.save(path, pnginfo=metadata)
        with Image.open(path) as decoded:
            decoded.load(); assert decoded.size == large.size and decoded.tobytes() == large.tobytes()
        encoded[name] = {'size': list(large.size), 'bytes': path.stat().st_size, 'sha256': sha(path)}

    for name, (top, bottom) in native.items():
        save(pair(top, bottom), 'pairs/' + name + '-2x.png')
    lower = Canvas(704, 618, BG)
    lower.text(16, 12, 'l / minimal', INK, scale=2)
    lower.text(688, 17, 'actual runtime / current c++', MUTED, right=True)
    dual = Canvas(864, 1110, BG)
    dual.text(16, 12, 'l / minimal', INK, scale=2)
    dual.text(848, 17, 'ordinary module / native calque', MUTED, right=True)
    for row, theme in enumerate(('graphite', 'pink')):
        for column, module in enumerate(('filter', 'rev')):
            name = theme + '_' + module + '_held'
            top, bottom = native[name]
            x, y = 16 + column * 352, 64 + row * 282
            lower.text(x, y - 14, module + ' / ' + theme, MUTED)
            lower.im.paste(bottom, (x, y))
            assert lower.im.crop((x, y, x + 320, y + 240)).tobytes() == bottom.tobytes()
            x, y = 16 + column * 432, 60 + row * 518
            dual.text(x, y - 14, module + ' / ' + theme, MUTED)
            p = pair(top, bottom); dual.im.paste(p, (x, y))
            assert dual.im.crop((x, y, x + 400, y + 496)).tobytes() == p.tobytes()
    lower.text(16, 602, 'live module context / held l / r fine', MUTED)
    dual.text(16, 1094, 'fresh native capture / not a mockup / not device footage', MUTED)
    save(lower.im, 'mod-minimal-native-lower-2x.png')
    save(dual.im, 'mod-minimal-native-dual-2x.png')
    extra = Canvas(1056, 580, BG)
    extra.text(16, 12, 'l / contexts and link states', INK)
    chosen = ('graphite_ghost_held', 'graphite_strn_held', 'graphite_fm_held',
              'graphite_filter_env', 'graphite_filter_muted_env', 'graphite_filter_off')
    for index, name in enumerate(chosen):
        x, y = 16 + 352 * (index % 3), 48 + 264 * (index // 3)
        extra.text(x, y - 14, name.removeprefix('graphite_').replace('_', ' / '), MUTED)
        image = native[name][1]; extra.im.paste(image, (x, y))
        assert extra.im.crop((x, y, x + 320, y + 240)).tobytes() == image.tobytes()
    extra.text(16, 564, 'native lower surfaces / contextual env / retained muted link / off', MUTED)
    save(extra.im, 'mod-minimal-native-states-2x.png')
    assert before == sources() and sha(Path(proof['binary'])) == receipt['binary_sha256']
    report = {'source_sha256': before, 'binary_sha256': receipt['binary_sha256'],
              'capture_receipt_sha256': sha(raw / 'capture.json'), 'packager_sha256': sha(Path(__file__)),
              'images': encoded, 'native_pairs': len(native), 'native_pixels_unmodified': True,
              'nearest_2x_and_full_decode': True, 'capture_calls': 0, 'builds': 0, 'test_groups': 0,
              'boundary': 'pure native packaging; not a mockup, animation, audio review or device/FPS measurement'}
    (out / 'packaging.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    for name, item in encoded.items():
        if '/' not in name: print(name, item)
    print(f'{len(native)} fresh native pairs; {len(encoded)} decoded/nearest-2x PNGs; no UI repaint or app operation')


if __name__ == '__main__':
    main()
