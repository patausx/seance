#!/usr/bin/env python3
"""Opaque native modulation controls on an authored translucent paper veil.

Consumes ONLY this study's freshly captured, source/hash-verified C++ surfaces.
No redraw of glyphs, faders, graphs, values or controls. No builds, suites, audio,
production edits or hardware operations. Right-hand composition is a proposal,
not an implemented overlay. Native font license: assets/fonts/{README.md,OFL.txt}.
"""
import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path

from PIL import Image, ImageChops, PngImagePlugin
from render_module_geometry_study import Canvas
from test_fader_lancet_ui import ROOT, sources, sha

OUT = ROOT / 'ui_mockups/mod_calque_study'
OUTSIDE = (20, 20, 23)
INK = (244, 244, 244)
GHOST = (177, 174, 184)
SPOT = (181, 154, 232)
VEIL_ALPHA = 210  # 82.35% paper, 17.65% visible underlying module; no blur.
INPUTS = ('tools/render_mod_calque_study.py', 'tools/render_module_geometry_study.py',
          'core/ui/font.cpp', 'platform/3ds/ui_param_lens.cpp',
          'platform/3ds/module_fader.h', 'platform/3ds/menu_style.cpp',
          'platform/3ds/ui_palette.h')


def difference_mask(a, b):
    channels = ImageChops.difference(a, b).split()
    return ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), channels[2]).point(
        lambda value: 255 if value else 0)


def calque(module, lens, paper):
    assert module.size == lens.size == paper.size == (320, 240)
    assert module.mode == lens.mode == paper.mode == 'RGB'
    foreground = difference_mask(lens, paper)
    # Integer native-pixel alpha mix: no resampling, blur or synthesized figure.
    background = Image.frombytes('RGB', module.size, bytes(
        (p * VEIL_ALPHA + b * (255 - VEIL_ALPHA) + 127) // 255
        for p, b in zip(paper.tobytes(), module.tobytes())))
    result = Image.composite(lens, background, foreground)
    assert not ImageChops.multiply(difference_mask(result, lens), foreground).getbbox(), 'native ink changed'
    clear = ImageChops.invert(foreground)
    assert not ImageChops.multiply(difference_mask(result, background), clear).getbbox(), 'incorrect veil'
    changed = difference_mask(result, lens)
    visible = sum(value != 0 for value in changed.tobytes())
    assert visible > 1500, 'background did not meaningfully survive the veil'
    return result, {'native_foreground_pixels': sum(value != 0 for value in foreground.tobytes()),
                    'revealed_background_pixels': visible, 'foreground_exact': True,
                    'background_exact_alpha': True, 'blur': False, 'relayout': False}


def pair(top, bottom):
    assert top.size == (400, 240) and bottom.size == (320, 240)
    image = Image.new('RGB', (400, 496), OUTSIDE)
    image.paste(top, (0, 0))
    image.paste(bottom, (40, 256))
    assert image.crop((0, 0, 400, 240)).tobytes() == top.tobytes()
    assert image.crop((40, 256, 360, 496)).tobytes() == bottom.tobytes()
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=OUT)
    parser.add_argument('--output', type=Path, default=None)
    args = parser.parse_args()
    source = args.input.resolve()
    out = args.output.resolve() if args.output else source
    out.mkdir(parents=True, exist_ok=True)
    assert not (out / 'proposal.json').exists(), 'preserve existing render; supply a new output'
    receipt_path = source / 'capture.json'
    receipt = json.loads(receipt_path.read_text())
    receipt_hash = sha(receipt_path)
    before = sources()
    assert before == receipt['source_sha256'], 'current sources no longer match these captures'
    assert sha(Path(receipt['binary'])) == receipt['binary_sha256'], 'capture binary changed'
    assert receipt['regression_groups_run'] == 0 and receipt['native_surfaces'] == 20
    assert receipt['capture_tool_sha256'] == {name: sha(ROOT / name) for name in receipt['capture_tool_sha256']}
    inputs = {name: sha(ROOT / name) for name in INPUTS}
    raw = source / 'capture'
    assert sha(raw / 'manifest.tsv') == receipt['manifest_sha256']
    with (raw / 'manifest.tsv').open() as stream:
        states = list(csv.DictReader(stream, delimiter='\t'))
    assert len(states) == 4
    native = {}
    for name, digest in receipt['surface_sha256'].items():
        path = raw / name
        assert sha(path) == digest
        with Image.open(path) as image:
            image.load()
            native[name.removesuffix('.ppm')] = image.convert('RGB')
        assert native[name.removesuffix('.ppm')].size == (400 if name.endswith('_top.ppm') else 320, 240)
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text('provenance', 'fresh current native C++ left; authored alpha-only calque right, not implemented; upper and all lens ink exact')
    metadata.add_text('source_sha256', json.dumps(before, sort_keys=True), zip=True)
    metadata.add_text('render_input_sha256', json.dumps(inputs, sort_keys=True), zip=True)
    metadata.add_text('capture_sha256', receipt_hash)
    metadata.add_text('captured_at', receipt['captured_at'])
    metadata.add_text('binary_sha256', receipt['binary_sha256'])
    metadata.add_text('paper_alpha_255', str(VEIL_ALPHA))
    images, records, proposed = {}, {}, {}

    def save(image, name):
        path = out / name
        assert not path.exists(), f'preserve earlier output: {path}'
        enlarged = image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST)
        assert enlarged.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
        enlarged.save(path, pnginfo=metadata)
        with Image.open(path) as decoded:
            decoded.load()
            assert decoded.size == enlarged.size and decoded.tobytes() == enlarged.tobytes()
        images[name] = {'dimensions': list(enlarged.size), 'bytes': path.stat().st_size,
                        'sha256': sha(path), 'nearest_2x': True, 'decode_exact': True}

    for state in states:
        name = state['frame']
        assert int(state['module_objects']) <= 8192 and int(state['lens_objects']) <= 8192
        proposed[name], records[name] = calque(native[name + '_module_bottom'],
                                               native[name + '_lens_bottom'], native[name + '_paper'])
        records[name]['fixture'] = state
        records[name]['upper_unchanged'] = True
        save(pair(native[name + '_lens_top'], proposed[name]), name + '-calque-dual-2x.png')

    for theme in ('graphite', 'pink'):
        board = Canvas(864, 1128, OUTSIDE)
        board.text(16, 12, 'l / modulation', INK, scale=2)
        board.text(848, 16, 'native controls / proposed veil', GHOST, right=True)
        board.text(16, 46, '01 / current page', GHOST)
        board.text(448, 46, '02 / translucent calque', SPOT)
        for row, module in enumerate(('filter', 'rev')):
            name = theme + '_' + module
            y = 80 + row * 524
            top = native[name + '_lens_top']
            for x, bottom in ((16, native[name + '_lens_bottom']), (448, proposed[name])):
                board.text(x, y - 14, module + ' / ' + theme, GHOST)
                dual = pair(top, bottom)
                board.im.paste(dual, (x, y))
                assert board.im.crop((x, y, x + 400, y + 496)).tobytes() == dual.tobytes()
        board.text(16, 1114, 'same frame / same controls / no blur', GHOST)
        board.text(848, 1114, 'visual study / not runtime', GHOST, right=True)
        save(board.im, 'mod-calque-' + theme + '-2x.png')

    # A compact context reference shows exactly what is visible below each veil.
    context = Canvas(704, 584, OUTSIDE)
    context.text(16, 12, 'under the calque / fresh module surfaces', INK)
    for row, module in enumerate(('filter', 'rev')):
        for column, theme in enumerate(('graphite', 'pink')):
            x, y = 16 + column * 352, 48 + row * 268
            context.text(x, y - 14, module + ' / ' + theme, GHOST)
            image = native[theme + '_' + module + '_module_bottom']
            context.im.paste(image, (x, y))
            assert context.im.crop((x, y, x + 320, y + 240)).tobytes() == image.tobytes()
    context.text(16, 570, 'unchanged current c++ / context only', GHOST)
    save(context.im, 'mod-calque-underlay-2x.png')
    assert before == sources() and sha(receipt_path) == receipt_hash
    assert inputs == {name: sha(ROOT / name) for name in inputs}
    assert sha(Path(receipt['binary'])) == receipt['binary_sha256']
    report = {
        'rendered_at': datetime.now(timezone.utc).isoformat(),
        'capture_receipt_sha256': receipt_hash, 'render_inputs': inputs,
        'paper_alpha_255': VEIL_ALPHA, 'underlay_weight_255': 255 - VEIL_ALPHA,
        'states': records, 'images': images, 'production_unchanged': True,
        'native_foreground_unchanged': True, 'native_upper_unchanged': True,
        'source_hashes_rechecked': len(before), 'fresh_native_surfaces_decoded': len(native),
        'render_builds': 0, 'render_test_groups': 0, 'render_capture_calls': 0,
        'boundary': 'design proposal: native controls over an authored static alpha composition; no runtime transparency or input implementation',
    }
    (out / 'proposal.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    for name, item in images.items():
        print(f'{out / name}: {item["dimensions"]} / {item["bytes"]} bytes / {item["sha256"]}')
    print('4 matched native contexts; opaque ink exact; alpha-only background; 7 PNG decode/nearest-2x checks passed')


if __name__ == '__main__':
    main()
