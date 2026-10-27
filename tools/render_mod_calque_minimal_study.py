#!/usr/bin/env python3
"""Minimal held-L design v2: two rails, source letters and one link control.

Uses this task's fresh, exact-state C++ PPMs, not previous proposal PNGs. The
ordinary module top is retained; lower composition is authored with the native
6x8 font and exact current lancet/source-row stencils. No app edits or capture,
compilation, suite, device, animation or audio operations.
"""
import csv
from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, PngImagePlugin
from render_module_geometry_study import Canvas
from render_mod_calque_study import difference_mask, pair, OUTSIDE, INK, GHOST, SPOT
from test_fader_lancet_ui import ROOT, sources, sha

SOURCE = ROOT / 'ui_mockups/mod_calque_study'
OUT = SOURCE / 'minimal-v2'
INPUTS = ('tools/render_mod_calque_minimal_study.py', 'tools/render_mod_calque_study.py',
          'tools/render_module_geometry_study.py', 'core/ui/font.cpp',
          'platform/3ds/module_fader.h', 'platform/3ds/ui_param_lens.cpp')
PANEL = (8, 44, 312, 205)
OUTSIDE_ALPHA = 170
PANEL_ALPHA = 234


def role(lens, paper, box):
    foreground = difference_mask(lens.crop(box), paper.crop(box))
    pixels = lens.crop(box).tobytes()
    colors = Counter(tuple(pixels[i * 3:i * 3 + 3]) for i, active in enumerate(foreground.tobytes()) if active)
    assert len(colors) == 1, (box, colors)
    return next(iter(colors))


def mix(module, paper, alpha):
    return Image.frombytes('RGB', module.size, bytes(
        (p * alpha + b * (255 - alpha) + 127) // 255
        for p, b in zip(paper.tobytes(), module.tobytes())))


def native_ink(canvas, lens, paper, box, destination):
    ink = lens.crop(box)
    mask = difference_mask(ink, paper.crop(box))
    x, y = destination
    assert 0 <= x < x + ink.width <= 320 and 0 <= y < y + ink.height <= 240
    canvas.im.paste(ink, destination, mask)
    result = canvas.im.crop((x, y, x + ink.width, y + ink.height))
    assert not ImageChops.multiply(difference_mask(result, ink), mask).getbbox(), 'native control ink changed'
    return {'source_rect': list(box), 'destination': list(destination),
            'native_foreground_exact': True, 'masked_pixels': sum(bool(v) for v in mask.tobytes())}


def page(name, state, native, labels):
    module, paper, lens = (native[name + suffix] for suffix in ('_module_bottom', '_paper', '_lens_bottom'))
    ink = role(lens, paper, (24, 108, 48, 116))
    ghost = role(lens, paper, (212, 108, 237, 116))
    spot = role(lens, paper, (81, 184, 87, 192))
    underlying = mix(module, paper, OUTSIDE_ALPHA)
    sheet = mix(module, paper, PANEL_ALPHA)
    underlying.paste(sheet.crop(PANEL), PANEL[:2])
    c = Canvas(320)
    c.im = underlying
    c.d = ImageDraw.Draw(c.im)
    # The only standalone action. Color + short underline denote enabled link;
    # this is not a new off/disconnect command or a rewritten routing contract.
    c.text(272, 55, 'link', spot)
    c.d.line((272, 66, 293, 66), fill=spot)
    base_lines = [row['text'] for row in labels if row['text'].startswith('base ')]
    assert len(base_lines) == 1
    base_text = base_lines[0][5:]
    depth = int(state['depth'])
    # One consistent signed percent in the proposal (old REV omits its % sign).
    depth_text = f'{int(depth * 100 / 32767):+d}%'
    c.text(24, 80, 'base', ghost)
    c.text(296, 80, base_text, ink, right=True)
    c.text(24, 125, 'depth', ghost)
    c.text(296, 125, depth_text, spot, right=True)
    stencils = {
        'base': native_ink(c, lens, paper, (22, 123, 298, 136), (22, 96)),
        'depth': native_ink(c, lens, paper, (22, 160, 298, 173), (22, 141)),
        'sources': native_ink(c, lens, paper, (8, 177, 314, 201), (8, 177)),
    }
    # Proposed touch space, not an implemented input system. The letters retain
    # all eight current sigils plus OFF/ENV, and native spacing remains intact.
    hits = [('link', (260, 46, 306, 74)), ('base', (14, 74, 306, 117)),
            ('depth', (14, 119, 306, 166))]
    hits += [(source, (8 + i * 31, 177, 8 + (i + 1) * 31, 201))
             for i, source in enumerate(('off', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'env'))]
    for index, (label, box) in enumerate(hits):
        assert 0 <= box[0] < box[2] <= 320 and 0 <= box[1] < box[3] <= 240
        for other, previous in hits[:index]:
            assert min(box[2], previous[2]) <= max(box[0], previous[0]) or min(box[3], previous[3]) <= max(box[1], previous[1]), (label, other)
    assert {text for _, text in c.labels} == {'link', 'base', 'depth', base_text, depth_text}
    return c.im, {'stencils': stencils, 'labels': c.labels, 'proposed_hits': hits,
                  'base_text_from_native_caption': base_text, 'depth_text': depth_text,
                  'palette_from_native': {'ink': ink, 'ghost': ghost, 'spot': spot},
                  'upper': 'unchanged ordinary module, not the lens diagnostic top',
                  'fixture': state}


def main():
    assert not OUT.exists(), 'preserve earlier v2; choose a new explicit revision instead of overwriting'
    before = sources()
    receipt_path = SOURCE / 'capture.json'
    receipt = json.loads(receipt_path.read_text())
    assert before == receipt['source_sha256']
    assert sha(Path(receipt['binary'])) == receipt['binary_sha256']
    assert receipt['capture_tool_sha256'] == {name: sha(ROOT / name) for name in receipt['capture_tool_sha256']}
    assert receipt['native_surfaces'] == 20 and receipt['regression_groups_run'] == 0
    raw = SOURCE / 'capture'
    assert sha(raw / 'manifest.tsv') == receipt['manifest_sha256']
    with (raw / 'manifest.tsv').open() as stream:
        states = list(csv.DictReader(stream, delimiter='\t'))
    native = {}
    for name, digest in receipt['surface_sha256'].items():
        assert sha(raw / name) == digest
        with Image.open(raw / name) as image:
            image.load()
            native[name.removesuffix('.ppm')] = image.convert('RGB')
        assert native[name.removesuffix('.ppm')].size == (400 if name.endswith('_top.ppm') else 320, 240)
    inputs = {name: sha(ROOT / name) for name in INPUTS}
    text_hashes = {}
    proposed, details = {}, {}
    for state in states:
        name = state['frame']
        text_path = raw / (name + '_lens_bottom_text.tsv')
        text_hashes[text_path.name] = sha(text_path)
        with text_path.open() as stream:
            labels = list(csv.DictReader(stream, delimiter='\t'))
        proposed[name], details[name] = page(name, state, native, labels)
    OUT.mkdir(parents=True)
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text('provenance', 'minimal l proposal v2; authored lower layout with exact native lancet/source stencils over this task\'s fresh C++ module; ordinary upper unchanged; not implemented')
    metadata.add_text('source_sha256', json.dumps(before, sort_keys=True), zip=True)
    metadata.add_text('render_input_sha256', json.dumps(inputs, sort_keys=True), zip=True)
    metadata.add_text('capture_receipt_sha256', sha(receipt_path))
    metadata.add_text('capture_binary_sha256', receipt['binary_sha256'])
    metadata.add_text('captured_at', receipt['captured_at'])
    files = {}

    def save(image, filename):
        path = OUT / filename
        assert not path.exists()
        enlarged = image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST)
        assert enlarged.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
        enlarged.save(path, pnginfo=metadata)
        with Image.open(path) as decoded:
            decoded.load()
            assert decoded.size == enlarged.size and decoded.tobytes() == enlarged.tobytes()
        files[filename] = {'dimensions': list(enlarged.size), 'bytes': path.stat().st_size, 'sha256': sha(path)}

    overview = Canvas(704, 618, OUTSIDE)
    overview.text(16, 12, 'l / minimal', INK, scale=2)
    overview.text(688, 17, 'two rails / sources / link', GHOST, right=True)
    duals = Canvas(864, 1110, OUTSIDE)
    duals.text(16, 12, 'l / minimal', INK, scale=2)
    duals.text(848, 17, 'ordinary module above / small layer below', GHOST, right=True)
    for row, theme in enumerate(('graphite', 'pink')):
        for column, module in enumerate(('filter', 'rev')):
            name = theme + '_' + module
            x, y = 16 + 352 * column, 64 + 282 * row
            overview.text(x, y - 14, module + ' / ' + theme, GHOST)
            overview.im.paste(proposed[name], (x, y))
            assert overview.im.crop((x, y, x + 320, y + 240)).tobytes() == proposed[name].tobytes()
            dual = pair(native[name + '_module_top'], proposed[name])
            save(dual, name + '-minimal-dual-2x.png')
            x, y = 16 + 432 * column, 60 + 518 * row
            duals.text(x, y - 14, module + ' / ' + theme, GHOST)
            duals.im.paste(dual, (x, y))
            assert duals.im.crop((x, y, x + 400, y + 496)).tobytes() == dual.tobytes()
    overview.text(16, 602, 'no title / no parameter row / no route diagram', GHOST)
    duals.text(16, 1094, 'fresh native context / authored minimal layout / not runtime', GHOST)
    save(overview.im, 'mod-minimal-v2-lower-2x.png')
    save(duals.im, 'mod-minimal-v2-dual-2x.png')
    assert before == sources() and inputs == {name: sha(ROOT / name) for name in inputs}
    assert text_hashes == {name: sha(raw / name) for name in text_hashes}
    assert sha(Path(receipt['binary'])) == receipt['binary_sha256']
    report = {
        'rendered_at': datetime.now(timezone.utc).isoformat(), 'render_input_sha256': inputs,
        'capture_receipt_sha256': sha(receipt_path), 'caption_source_sha256': text_hashes,
        'ordinary_upper_unchanged': True, 'paper_alpha': {'outside': OUTSIDE_ALPHA, 'panel': PANEL_ALPHA},
        'panel_rect': PANEL, 'states': details, 'images': files, 'current_source_count': len(before),
        'new_host_builds': 0, 'new_capture_calls': 0, 'regression_groups': 0,
        'checks': 'native stencils exact; caption/target bounds; 13 nonoverlapping proposed targets; palette; six PNG decode; crop equality; native nearest-2x; source guards',
        'limits': 'design only; no input implementation, env-expanded state, shortcut/reset decisions, animation or hardware acceptance',
        'supersedes': 'v1 changed opacity only; user clarified a real minimalist layout redesign',
    }
    (OUT / 'proposal.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    for name, item in files.items():
        print(f'{OUT / name}: {item["dimensions"]} / {item["bytes"]} bytes / {item["sha256"]}')
    print('v2: 4 contexts / 6 decoded PNGs; native rail/source stencils exact; ordinary top unchanged; no new capture or build')


if __name__ == '__main__':
    main()
