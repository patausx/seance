#!/usr/bin/env python3
"""Four drawing-only fader proposals on freshly captured current module screens.

No production, controller, DSP, format, font or device changes. Backgrounds come
from the hash-verified existing host executable in dump-only mode. Only bounded
rail pixels are repainted. The violet modulation poses are authored examples,
not measured sigil output; the captured module diagrams are left untouched.
Font provenance/license: assets/fonts/{README.md,OFL.txt}.
"""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess

from PIL import Image, ImageChops, ImageDraw, PngImagePlugin
from render_module_geometry_study import Canvas
from render_menu_style_study import heading_mask, overlaps
from test_reverb_dense_ui import BINARY, sources as runtime_sources, sha

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/fader_gothic_study'
OUTSIDE = (20, 20, 22)
# Existing graphite palette roles; branch ink is intentionally subdued on rails.
COLORS = dict(paper=(36, 36, 36), ink=(244, 244, 244), spot=(181, 154, 232),
              ghost=(184, 184, 184), rail=(112, 112, 112))
INPUTS = (
    'tools/render_fader_gothic_study.py', 'tools/render_module_geometry_study.py',
    'tools/render_menu_style_study.py', 'tools/generate_start_menu_labels.py',
    'tools/test_reverb_dense_ui.py', 'core/ui/font.cpp',
    'assets/fonts/Gossip-MedPixel.otf', 'platform/3ds/ui_palette.h',
    'platform/3ds/menu_style.cpp', 'platform/3ds/ui_module_focus_filter.cpp',
    'platform/3ds/ui_module_focus_reverb.cpp', 'tools/filter_ui_test.h',
    'tools/rev_visual_test.h',
)
STYLES = (
    ('lancet', 'single hairline / pointed window'),
    ('quill', 'oblique cut / calligraphic nib'),
    ('seal', 'double hairline / faceted seal'),
    ('thorn', 'forked ends / open thorn clasp'),
)


def input_hashes():
    return {name: sha(ROOT / name) for name in INPUTS}


def capture():
    """Never consume an old study PNG or silently use a stale executable."""
    current = runtime_sources()
    proof = json.loads(BINARY.with_suffix('.sources.json').read_text())
    assert current == proof['source_sha256'], 'current sources differ from host executable'
    assert sha(BINARY) == proof['binary_sha256'], 'host executable changed'
    receipt_path = OUT / 'capture.json'
    raw = OUT / 'capture'
    if receipt_path.exists():
        receipt = json.loads(receipt_path.read_text())
        assert receipt['source_sha256'] == current
        assert receipt['binary_sha256'] == sha(BINARY)
        assert all(sha(raw / name) == digest for name, digest in receipt['surface_sha256'].items())
        return receipt
    assert not raw.exists(), 'preserve an incomplete capture rather than overwrite it'
    raw.mkdir(parents=True)
    env = dict(os.environ, SEANCE_TEST_THEME='graphite',
               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    calls = []
    for folder, flag, count, message in (
            ('rev', '--capture-rev-dense', 16, 'rev captures: eight fresh actual native C++ duals'),
            ('filter', '--dump-filter', 28, 'sequencer: 0 tests, 0 failed;')):
        destination = raw / folder
        command = [str(BINARY), flag, str(destination)]
        result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        (OUT / f'capture-{folder}.log').write_text(result.stdout)
        assert result.returncode == 0 and message in result.stdout, result.stdout
        assert len(list(destination.glob('*.ppm'))) == count
        calls.append(dict(command=command, regression_groups=0))
    surfaces = sorted(raw.rglob('*.ppm'))
    assert len(surfaces) == 44
    for path in surfaces:
        with Image.open(path) as image:
            image.load()
            assert image.size == (400 if path.name.endswith('_top.ppm') else 320, 240)
    assert current == runtime_sources(), 'production changed during capture'
    receipt = dict(captured_at=datetime.now(timezone.utc).isoformat(), calls=calls,
                   source_sha256=current, binary_sha256=sha(BINARY),
                   surface_sha256={str(p.relative_to(raw)): sha(p) for p in surfaces},
                   host_only=True, regression_groups_run=0, new_application_compilations=0)
    receipt_path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n')
    return receipt


class Board(Canvas):
    def __init__(self, width, height):
        super().__init__(width, height, OUTSIDE)

    def label(self, x, y, text, role='ghost', right=False):
        self.text(x, y, text, COLORS[role], right=right)

    def title(self, x, y, text, role='ink'):
        mask = heading_mask(text)
        box = (x, y, x + mask.width, y + mask.height)
        assert 0 <= x < box[2] <= self.im.width and 0 <= y < box[3] <= self.im.height
        assert all(not overlaps(box, previous) for previous, _ in self.labels)
        self.labels.append((box, text))
        self.im.paste(COLORS[role], (x, y), mask)


class Pen:
    """u follows the value axis; v follows its perpendicular. Native pixels only."""
    def __init__(self, image, x, y, vertical=False):
        self.image, self.draw = image, ImageDraw.Draw(image)
        self.x, self.y, self.vertical = x, y, vertical
        self.points = []

    def mapped(self, points):
        mapped = [(self.x + v, self.y - u) if self.vertical else (self.x + u, self.y + v)
                  for u, v in points]
        assert all(0 <= x < self.image.width and 0 <= y < self.image.height for x, y in mapped), mapped
        self.points.extend(mapped)
        return mapped

    def line(self, points, role='rail'):
        self.draw.line(self.mapped(points), fill=COLORS[role], width=1)

    def poly(self, points, role='ink', filled=False):
        self.draw.polygon(self.mapped(points), fill=COLORS[role if filled else 'paper'],
                          outline=COLORS[role])


def handle(pen, style, at, live=False, role=None):
    role = role or ('spot' if live else 'ink')
    if style == 'lancet':
        points = [(0, -3), (2, 0), (0, 3), (-2, 0)] if live else [
            (0, -5), (3, 0), (0, 5), (-3, 0)]
    elif style == 'quill':
        points = [(2, -3), (2, 0), (-2, 3), (-1, -1)] if live else [
            (2, -5), (4, -3), (1, 1), (-2, 5), (-4, 3), (-1, -1)]
    elif style == 'seal':
        points = [(0, -4), (1, -2), (1, 2), (0, 4), (-1, 2), (-1, -2)] if live else [
            (-4, -2), (-2, -4), (2, -4), (4, -2), (4, 2), (2, 4), (-2, 4), (-4, 2)]
    elif style == 'thorn':
        if live:
            points = [(0, -4), (2, -2), (1, 0), (2, 2), (0, 4), (-1, 1), (-1, -1)]
        else:
            # Open, mirrored angular brackets; no center fill resembling a live marker.
            pen.poly([(at - 4, -5), (at + 4, -5), (at + 4, 5), (at - 4, 5)], 'paper', True)
            for side in (-1, 1):
                pen.line([(at + side * u, v) for u, v in
                          ((4, -5), (2, -4), (1, -2), (3, 0), (1, 2), (2, 4), (4, 5))], role)
            return
    else:
        raise AssertionError(style)
    pen.poly([(at + u, v) for u, v in points], role, live)


def fader(image, style, x, y, length, base, live=None, linked=False,
          vertical=False, neutral=None, muted=False):
    assert 0 <= base <= 32767 and (live is None or 0 <= live <= 32767)
    pen = Pen(image, x, y, vertical)
    edit_at = base * length // 32767
    live_at = edit_at if live is None else live * length // 32767
    if style == 'seal':
        pen.line([(0, -1), (length, -1)])
        pen.line([(0, 1), (length, 1)])
        for end, side in ((0, -1), (length, 1)):
            pen.line([(end, -1), (end + side * 2, 0), (end, 1)])
    else:
        pen.line([(0, 0), (length, 0)])
        for end, side in ((0, -1), (length, 1)):
            if style == 'lancet':
                pen.line([(end, -2), (end + side * 2, 0), (end, 2)])
            elif style == 'quill':
                pen.line([(end - 1, 2), (end + 1, -2)])
            else:
                pen.line([(end + side * 2, -3), (end, -1), (end, 1), (end + side * 2, 3)])
                pen.line([(end, 0), (end + side * 3, 0)])
    if neutral is not None:
        at = neutral * length // 32767
        pen.line([(at, -3), (at, 3)])
    if linked and not muted and edit_at != live_at:
        # Instantaneous displacement, NOT a potential-depth range or waveform.
        pen.line([(edit_at, 0), (live_at, 0)], 'spot')
    handle(pen, style, edit_at, role='ghost' if muted else 'ink')
    if linked and not muted:
        if abs(live_at - edit_at) <= 4:
            # Close/crossing values retain the edit outline and an exact live hairline.
            pen.line([(live_at, -2), (live_at, 2)], 'spot')
        else:
            handle(pen, style, live_at, live=True)
    return dict(base_px=edit_at, live_px=live_at,
                bounds=[min(x for x, _ in pen.points), min(y for _, y in pen.points),
                        max(x for x, _ in pen.points) + 1, max(y for _, y in pen.points) + 1])


def original(module):
    frame = '02_dense_tail' if module == 'rev' else '07_morph_mid'
    path = OUT / 'capture' / module / f'{frame}_bottom.ppm'
    with Image.open(path) as source:
        source.load()
        return source.convert('RGB')


# Every anchor/length/edit value comes from the selected current C++ fixture.
# Only REV mix and FILTER drive receive authored live poses. Neither drives the
# captured before-mix wet-tail figure or the filter's post-drive linear plot.
CONTROLS = {
    'rev': (
        dict(name='size', x=12, y=48, length=138, base=18000),
        dict(name='decay', x=170, y=48, length=138, base=20000),
        dict(name='damp', x=12, y=206, length=138, base=9000),
        dict(name='mix', x=170, y=206, length=138, base=9000, live=16864, linked=True),
    ),
    'filter': (
        dict(name='res', x=297, y=148, length=70, base=4096, vertical=True),
        dict(name='drive', x=18, y=197, length=126, base=0, live=8519, linked=True),
        dict(name='morph', x=174, y=197, length=126, base=16384, neutral=16384),
    ),
}


def context(style, module):
    native = original(module)
    result = native.copy()
    allowed = Image.new('L', native.size, 0)
    allowed_draw = ImageDraw.Draw(allowed)
    draw = ImageDraw.Draw(result)
    records = []
    for control in CONTROLS[module]:
        settings = dict(control)
        name = settings.pop('name')
        x, y, length = settings['x'], settings['y'], settings['length']
        if settings.get('vertical'):
            box = (x - 7, y - length - 4, x + 8, y + 5)
        else:
            box = (x - 6, y - 7, x + length + 7, y + 8)
        assert 0 <= box[0] < box[2] <= 320 and 0 <= box[1] < box[3] <= 240
        draw.rectangle((box[0], box[1], box[2] - 1, box[3] - 1), fill=COLORS['paper'])
        allowed_draw.rectangle((box[0], box[1], box[2] - 1, box[3] - 1), fill=255)
        geometry = fader(result, style, **settings)
        bounds = geometry['bounds']
        assert box[0] <= bounds[0] <= bounds[2] <= box[2]
        assert box[1] <= bounds[1] <= bounds[3] <= box[3]
        records.append(dict(name=name, mask=box, **settings, **geometry))
    difference = ImageChops.difference(native, result)
    assert difference.getbbox(), 'concept unexpectedly identical to current rails'
    outside = Image.new('RGB', native.size)
    outside.paste(difference, mask=ImageChops.invert(allowed))
    assert outside.getbbox() is None, 'changed pixels outside the rail masks'
    return result, records


def specimen(style, base=13762, live=22281):
    # Exactly the REV rail length; this crop is magnified, not a larger hit target.
    image = Image.new('RGB', (154, 24), COLORS['paper'])
    fader(image, style, 8, 12, 138, base, live, linked=True)
    return image


def endpoint_validation():
    count = 0
    for style, _ in STYLES:
        for length in (48, 70, 126, 138):
            for vertical in (False, True):
                for base, live, linked, muted in (
                        (0, 32767, True, False), (32767, 0, True, False),
                        (16384, 16384, True, False), (16384, 17000, True, False),
                        (16384, None, False, False), (16384, 24000, True, True)):
                    image = Image.new('RGB', (length + 20, length + 20), COLORS['paper'])
                    fader(image, style, 10, length + 10 if vertical else 10, length,
                          base, live, linked, vertical, 16384, muted)
                    assert set(image.get_flattened_data()) <= set(COLORS.values())
                    count += 1
    return count


def option_board(index, style, subtitle, rev, filt):
    board = Board(704, 478)
    board.title(16, 14, f'{index:02} / {style}')
    board.label(688, 25, subtitle, right=True)
    board.label(16, 62, 'base 42% / sg.a -> 68%', 'ink')
    board.im.paste(specimen(style).resize((308, 48), Image.Resampling.NEAREST), (16, 79))
    board.label(16, 139, '2x detail / same native rail')
    # Small controls and a sign-changing parameter use the same glyph family.
    for label, y, settings in (
            ('no link', 73, dict(base=19005)),
            ('sg.a at base', 105, dict(base=16384, live=16384, linked=True)),
            ('bipolar / -', 137, dict(base=16384, live=7864, linked=True, neutral=16384))):
        board.label(368, y - 4, label)
        tile = Image.new('RGB', (184, 20), COLORS['paper'])
        fader(tile, style, 7, 10, 168, **settings)
        board.im.paste(tile, (504, y - 10))
    board.label(16, 169, 'rev / mix modulation pose', 'spot')
    board.label(368, 169, 'filter / drive modulation pose', 'spot')
    board.im.paste(rev, (16, 186))
    board.im.paste(filt, (368, 186))
    board.label(16, 442, 'white outline = edit / violet = current result', 'ink')
    board.label(16, 459, 'drawing study / fresh c++ backgrounds / only rail pixels replaced')
    return board.im


def save_png(image, name, provenance, scale=2):
    path = OUT / name
    assert not path.exists(), 'preserve a prior render; use a new version or output directory'
    native = image
    if scale != 1:
        image = native.resize((native.width * scale, native.height * scale), Image.Resampling.NEAREST)
        assert image.resize(native.size, Image.Resampling.NEAREST).tobytes() == native.tobytes()
    info = PngImagePlugin.PngInfo()
    info.add_text('description', __doc__)
    info.add_text('provenance', json.dumps(provenance, sort_keys=True))
    info.add_text('source_sha256', json.dumps(input_hashes(), sort_keys=True))
    image.save(path, pnginfo=info)
    with Image.open(path) as decoded:
        decoded.load()
        assert decoded.size == image.size and decoded.convert('RGB').tobytes() == image.tobytes()
    return dict(path=str(path.relative_to(ROOT)), size=list(image.size),
                bytes=path.stat().st_size, sha256=sha(path))


def main():
    before = input_hashes()
    runtime_before = runtime_sources()
    OUT.mkdir(parents=True, exist_ok=True)
    assert not (OUT / 'fader-variants-v1-2x.png').exists(), 'preserve prior study'
    receipt = capture()
    cases = endpoint_validation()
    common = dict(status='drawing-only concept; not implemented or selected',
                  captured_at=receipt['captured_at'], binary_sha256=receipt['binary_sha256'],
                  current_source_inputs=len(runtime_before), native_module_size=[320, 240],
                  poses='authored instantaneous sigil examples; not captured mod telemetry',
                  untouched='all pixels outside seven bounded rail masks, including text and module figures',
                  production_changed=False, device=False, regression_groups_run=0)
    board = Board(704, 800)
    board.title(16, 14, 'one fader / four cuts')
    board.label(688, 25, 'seance / drawing study 01', right=True)
    board.label(16, 46, 'white outline: edit / violet: current sigil result', 'ink')
    outputs, geometry = [], {}
    for i, (style, subtitle) in enumerate(STYLES):
        rev, rev_records = context(style, 'rev')
        filt, filter_records = context(style, 'filter')
        geometry[style] = dict(rev=rev_records, filter=filter_records)
        option = option_board(i + 1, style, subtitle, rev, filt)
        outputs.append(save_png(option, f'{i + 1:02}-{style}-2x.png', dict(common, style=style)))
        x, y = 16 + (i % 2) * 352, 77 + (i // 2) * 346
        board.title(x, y, f'{i + 1:02} / {style}')
        board.im.paste(specimen(style).resize((308, 48), Image.Resampling.NEAREST), (x, y + 34))
        board.im.paste(rev, (x, y + 94))
    board.label(16, 783, 'same rev screen / only fader drawing changes / illustrative modulation poses')
    outputs.insert(0, save_png(board.im, 'fader-variants-v1-2x.png', common))
    assert before == input_hashes() and runtime_before == runtime_sources(), 'source changed during render'
    report = dict(common, outputs=outputs, source_sha256=before, controls=geometry,
                  validation=dict(endpoint_geometry_cases=cases, masks_only=True,
                                  unchanged_anchors_and_edit_positions=True,
                                  native_shapes=True, palette=True, text_bounds=True,
                                  nearest_2x_and_png_decode=True))
    (OUT / 'proposal.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    for record in outputs:
        print(f"{record['path']} / {record['size']} / {record['bytes']} bytes / {record['sha256']}")
    print(f'pass: {cases} drawing-only endpoint cases, native bounds, seven rail masks, labels, nearest2x/decode')
    print('fresh verified backgrounds; no app compilation, regression groups, audio export, deployment or runtime edits')


if __name__ == '__main__':
    main()
