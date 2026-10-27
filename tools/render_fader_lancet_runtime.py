#!/usr/bin/env python3
"""Package verified, fresh native lancet captures. Never build, test or paint UI."""
import argparse
import csv
import json
import re
from pathlib import Path
from PIL import Image, ImageDraw, PngImagePlugin
from test_fader_lancet_ui import ROOT, BINARY, PROOF, OUT, sources, sha


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-dir', type=Path, default=OUT / 'capture')
    parser.add_argument('--output', type=Path, default=OUT)
    args = parser.parse_args()
    raw, out = args.capture_dir.resolve(), args.output.resolve()
    proof = json.loads(PROOF.read_text())
    receipt_path = raw / 'capture.json'
    receipt = json.loads(receipt_path.read_text())
    receipt_sha = sha(receipt_path)
    before = sources()
    assert proof['checks']['status'] == 'passed', 'focused UI proof must pass first'
    assert before == proof['source_sha256'] == receipt['source_sha256'], 'stale capture/source proof'
    assert sha(BINARY) == proof['binary_sha256'] == receipt['binary_sha256'], 'stale native binary'
    assert receipt['regression_groups_run'] == 0 and receipt['modules'] == 28
    with (raw / 'manifest.tsv').open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    assert len(rows) == receipt['pairs'] and len({row['frame'] for row in rows}) == len(rows)
    assert len(receipt['surface_sha256']) == len(rows) * 2
    out.mkdir(parents=True, exist_ok=True)
    outside = (29, 30, 30)
    caption = (201, 201, 193)
    images, encoded, surfaces = {}, {}, {}
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text('provenance', 'fresh actual current C++ native editor/top; host raster, not device footage; only outer captions editorial')
    metadata.add_text('source_sha256', json.dumps(before, sort_keys=True), zip=True)
    metadata.add_text('binary_sha256', proof['binary_sha256'])
    metadata.add_text('capture_receipt_sha256', receipt_sha)

    def save(image, path, native=None):
        assert not path.exists(), f'preserve previous image: {path}'
        if native is not None:
            assert image.size == (native.width * 2, native.height * 2)
            assert image.resize(native.size, Image.Resampling.NEAREST).tobytes() == native.tobytes()
        image.save(path, pnginfo=metadata)
        with Image.open(path) as decoded:
            decoded.load()
            assert decoded.size == image.size and decoded.tobytes() == image.tobytes()
        encoded[str(path.relative_to(out))] = {'size': list(image.size), 'bytes': path.stat().st_size, 'sha256': sha(path)}

    for row in rows:
        name = row['frame']
        assert re.fullmatch(r'\d\d_[a-z_]+', name)
        assert int(row['shared']) == int(row['top']) + int(row['bottom']) <= 8192
        pair = []
        for screen, width in (('top', 400), ('bottom', 320)):
            path = raw / f'{name}_{screen}.ppm'
            assert sha(path) == receipt['surface_sha256'][path.name], f'altered native surface: {path.name}'
            with Image.open(path) as source:
                source.load()
                image = source.convert('RGB')
            assert image.size == (width, 240)
            surfaces[path.name] = sha(path)
            pair.append(image)
        images[name] = pair

    chosen = ['16_rev', '09_filter', '17_ghost', '03_fm', '06_gran', '21_shift_lens']
    sheet = Image.new('RGB', (3 * 432, 2 * 530), outside)
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(chosen):
        pair = images[name]
        x, y = (index % 3) * 432 + 16, (index // 3) * 530 + 26
        draw.text((x, y - 18), name[3:].replace('_', ' '), fill=caption)
        sheet.paste(pair[0], (x, y))
        sheet.paste(pair[1], (x + 40, y + 254))
        assert sheet.crop((x, y, x + 400, y + 240)).tobytes() == pair[0].tobytes()
        assert sheet.crop((x + 40, y + 254, x + 360, y + 494)).tobytes() == pair[1].tobytes()
    save(sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST),
         out / 'lancet-native-six-2x.png', sheet)

    regular = [row['frame'] for row in rows if re.fullmatch(r'\d\d_[a-z]+', row['frame'])]
    assert len(regular) == 28
    overview = Image.new('RGB', (4 * 344, 7 * 272), outside)
    draw = ImageDraw.Draw(overview)
    for index, name in enumerate(regular):
        x, y = (index % 4) * 344 + 12, (index // 4) * 272 + 24
        draw.text((x, y - 17), name[3:], fill=caption)
        overview.paste(images[name][1], (x, y))
        assert overview.crop((x, y, x + 320, y + 240)).tobytes() == images[name][1].tobytes()
    save(overview.resize((overview.width * 2, overview.height * 2), Image.Resampling.NEAREST),
         out / 'lancet-all-28-lower-2x.png', overview)

    pairs = out / 'pairs'
    pairs.mkdir(exist_ok=True)
    for name, pair in images.items():
        native = Image.new('RGB', (400, 496), outside)
        native.paste(pair[0], (0, 0))
        native.paste(pair[1], (40, 256))
        save(native.resize((800, 992), Image.Resampling.NEAREST), pairs / f'{name}-2x.png', native)

    assert before == sources() and sha(BINARY) == proof['binary_sha256'], 'sources/binary moved during packaging'
    assert sha(receipt_path) == receipt_sha, 'capture receipt moved during packaging'
    report = {
        'source_sha256': before, 'binary_sha256': proof['binary_sha256'],
        'capture_receipt_sha256': sha(receipt_path), 'surface_sha256': surfaces,
        'captures': rows, 'images': encoded, 'chosen': chosen,
        'host_only': True, 'nearest_2x': True, 'screen_pixels_unmodified': True,
        'compilations_run': 0, 'regression_groups_run': 0, 'captures_run': 0,
        'packager_sha256': sha(Path(__file__)),
    }
    report_path = out / 'packaging.json'
    assert not report_path.exists(), 'preserve previous packaging provenance'
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    for name in ('lancet-native-six-2x.png', 'lancet-all-28-lower-2x.png'):
        item = encoded[name]
        print(f'{out / name}: {item["size"]} / {item["bytes"]} bytes / sha256 {item["sha256"]}')
    print(f'{len(rows)} decoded native pairs; {len(encoded)} verified images; no UI pixels painted/reused')


if __name__ == '__main__':
    main()
