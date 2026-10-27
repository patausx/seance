#!/usr/bin/env python3
"""Ghost visual study: fresh C++ baseline and explicitly authored proposals.

The existing current-source host binary is hash-verified, then used in dump-only
mode (zero regression groups). No old images, runtime edits, audio exports,
application build or device operation. Proposed contours are a visual metaphor,
not measured per-well levels, PCM, spectrum, or a new physical model.
Font provenance/license: assets/fonts/{README.md,OFL.txt}.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, PngImagePlugin
from render_inst_study import COLORS, GLYPHS, Page, caption
from test_reverb_dense_ui import sources as runtime_sources, sha

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/ghost_refresh_study'
BINARY = Path('/tmp/seance_rev_dense_ui_san')
OUTSIDE = (20, 20, 22)
INPUTS = (
    'tools/render_ghost_refresh_study.py', 'tools/render_inst_study.py',
    'tools/render_module_geometry_study.py', 'tools/render_menu_style_study.py',
    'tools/generate_start_menu_labels.py', 'tools/test_reverb_dense_ui.py',
    'core/ui/font.cpp', 'platform/3ds/note_display_data.h',
    'platform/3ds/inspector_gothic.h', 'platform/3ds/ui_palette.h',
    'platform/3ds/ui_module_focus_ghost.cpp', 'platform/3ds/ui_param_lens.cpp',
    'assets/fonts/Gossip-MedPixel.otf',
)
# Exact values of the fresh existing dump's param_1 state. No independent audio
# simulation is used in the proposal. Only the visible scalar settings match;
# the proposed active and held contour poses are deliberately illustrative.
VALUES = (3500, 6800, 12000, 4096, 0, 16384, 0, 0)
BASELINE = 'param_1'


def input_hashes():
    return {name: sha(ROOT / name) for name in INPUTS}


def capture():
    """Reuse a verified binary, never a stale render. Retain this task's receipt."""
    raw = OUT / 'capture'
    receipt = OUT / 'capture.json'
    proof = json.loads(BINARY.with_suffix('.sources.json').read_text())
    current = runtime_sources()
    assert current == proof['source_sha256'], 'current inputs differ from the host binary'
    assert sha(BINARY) == proof['binary_sha256'], 'host binary changed'
    if receipt.exists():
        data = json.loads(receipt.read_text())
        assert data['source_sha256'] == current
        assert data['binary_sha256'] == sha(BINARY)
        assert all(sha(raw / name) == value for name, value in data['surface_sha256'].items())
        return data
    assert not raw.exists(), 'preserve an incomplete/earlier capture instead of overwriting it'
    raw.mkdir(parents=True)
    env = dict(os.environ, SEANCE_TEST_THEME='graphite',
               ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    command = [str(BINARY), '--dump-ghost-menu', str(raw)]
    result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    (OUT / 'capture.log').write_text(result.stdout)
    if result.returncode:
        raise RuntimeError('fresh native dump failed; see capture.log')
    assert 'sequencer: 0 tests, 0 failed;' in result.stdout, result.stdout
    assert current == runtime_sources(), 'runtime changed during capture'
    paths = sorted(raw.glob('*.ppm'))
    assert len(paths) == 52, len(paths)
    for path in paths:
        with Image.open(path) as im:
            im.load()
            assert im.size == (400 if path.name.endswith('_top.ppm') else 320, 240)
    data = dict(captured_at=datetime.now(timezone.utc).isoformat(), command=command,
                source_sha256=current, binary_sha256=sha(BINARY),
                surface_sha256={path.name: sha(path) for path in paths},
                regression_groups_run=0, host_only=True,
                boundary='fresh current C++ dump; not device footage; no app build or audio export')
    receipt.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')
    return data


def native(name, surface):
    with Image.open(OUT / 'capture' / f'{name}_{surface}.ppm') as source:
        source.load()
        return source.convert('RGB')


def percent(value):
    return value * 100 // 32767


def nominal_root(tone, fine=0):
    assert fine == 0  # This matched fixture uses exact unity; no invented pitch law.
    scale = 32767 + (32767 - max(0, tone)) * 3
    delay_q8 = (149 * scale * 256) >> 15
    return (32000 * 256000 + delay_q8 // 2) // delay_q8


def hz_label():
    hz = nominal_root(VALUES[0])
    return f'{hz // 1000}.{hz % 1000 // 10:02}hz'


def note_label():
    note = round(69 + 12 * math.log2(nominal_root(VALUES[0]) / 440000))
    return ('c', 'c#', 'd', 'd#', 'e', 'f', 'f#', 'g', 'g#', 'a', 'a#', 'b')[note % 12] + str(note // 12 - 1)


def numeric(c, x, baseline, text, suffix, role='ink'):
    """Current baked numeric masks and spacing, not an approximate display face."""
    for char in text:
        mask, top = GLYPHS[char, False]
        x = c.mask(x, baseline + top, mask, char, role) + 2
    c.txt(x + 3, baseline - 12, suffix, role, scale=2)


def hollow(c, x, y, role='ink'):
    c.d.rectangle((x - 2, y - 3, x + 2, y + 3), outline=COLORS[role])


def rail(c, start, end, value, role='ink', vertical=False, bipolar=False):
    c.line((start, end), 'rail')
    amount = (value + 32767) / 65534 if bipolar else value / 32767
    amount = min(1, max(0, amount))
    if vertical:
        x, y = start[0], round(end[1] - (end[1] - start[1]) * amount)
    else:
        x, y = round(start[0] + (end[0] - start[0]) * amount), start[1]
    hollow(c, x, y, role)
    return x, y


def traces(c, box, held=False, quiet=False):
    """Four asymmetric imprints; their relative sizes are NOT per-well meters.

    A future runtime would drive shared strength from existing wet energy and
    shared displacement from real drift. Here both poses are authored. Each main
    contour is closed; only its short colored edge is an overlaid open segment.
    There are no cable/source dots, waveform axes, random noise, or UI-clock LFO.
    The center is kept clear for the existing lower freeze control.
    """
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    wells = ((.12, .53, .047, .30, .022),
             (.29, .49, .055, .43, .032),
             (.73, .50, .053, .38, -.028),
             (.90, .55, .043, .28, -.016))
    all_points = []
    for index, (cx, cy, width, height, lean) in enumerate(wells):
        for echo in (2, 1, 0):
            scale = 1 - echo * .105
            shift_x = echo * (.010 if index < 2 else -.010)
            shift_y = echo * .027
            # Sealed memory gathers the staggered returns into a static imprint.
            bend = lean * (.28 if held else 1)
            points = []
            for j in range(81):
                t = math.tau * j / 80
                xx = cx + shift_x + scale * (width * math.sin(t) * (1 + .16 * math.cos(t)) + bend * math.cos(t))
                yy = cy + shift_y + scale * height * math.cos(t)
                points.append((round(x0 + xx * w), round(y0 + yy * h)))
            assert all(x0 <= x <= x1 and y0 <= y <= y1 for x, y in points), points
            role = 'rail' if quiet or echo == 2 else 'ghost' if echo else 'ink'
            c.line(points, role)
            all_points.extend(points)
            if not quiet and echo == 0:
                a, b = (26, 42) if held else (40 - index * 2, 56 - index * 2)
                c.line(points[a:b], 'spot')
        if held:
            # A compact seam, not a new touch target or per-line read head.
            sx, sy = round(x0 + cx * w), round(y0 + (cy + height) * h)
            c.line(((sx - 2, sy), (sx + 2, sy)), 'spot')
    c.art_count += len(wells) * 4
    return (min(x for x, _ in all_points), min(y for _, y in all_points),
            max(x for x, _ in all_points), max(y for _, y in all_points))


def upper(held=False):
    c = Page(400)
    c.txt(14, 10, 'ghost', 'spot')
    c.line(((14, 22), (42, 22)), 'spot')
    c.txt(58, 10, '/ out', 'ghost')
    c.txt(386, 10, 'ch01 / 01', 'ghost', right=True)
    c.heading(14, 41, 'decay')
    c.txt(14, 73, 'memory feedback', 'ghost')
    numeric(c, 14, 130, str(percent(VALUES[1])), '%', 'ghost' if held else 'ink')
    c.txt(14, 146, 'held / long' if held else 'base / long', 'ghost')
    c.txt(14, 172, 'mod off', 'ghost')
    c.txt(14, 188, 'l: modulation', 'spot')
    c.txt(386, 45, 'memory held' if held else 'tail active', 'spot', right=True)
    traces(c, (177, 68, 380, 182), held=held)
    c.txt(278, 192, 'four memory traces', 'ghost', right=False)
    # Same physical values as the fresh native reference, without diagnostic raw.
    c.txt(14, 210, 'root ' + hz_label(), 'ghost')
    c.txt(160, 210, 'fine +0.00c', 'ghost')
    c.txt(386, 210, 'cycle 2.5s', 'ghost', right=True)
    c.txt(14, 231, 'sealed input / mix remains live' if held else 'four wells / fading memory', 'ghost')
    return c


def lower(held=False):
    c = Page(320)
    # All existing scalar controls remain at their installed anchor coordinates.
    # This is not a controller implementation or a claim of approved hit geometry.
    c.txt(12, 7, 'ghost', 'spot')
    c.txt(58, 7, 'ch01 / 01', 'ghost')
    c.txt(132, 7, 'note', 'spot')
    c.txt(205, 7, 'byp', 'ghost')
    c.txt(246, 7, 'long', 'spot')
    c.txt(308, 7, 'x del', 'ghost', right=True)
    dormant = 'ghost' if held else 'ink'
    c.txt(82, 28, 'root held' if held else '- ' + note_label() + ' ' + hz_label() + ' +', dormant)
    c.txt(93, 43, 'root', 'ghost')
    rail(c, (82, 54), (232, 54), VALUES[0], dormant)
    c.txt(87, 64, 'fine held' if held else 'fine +0.00c', dormant)
    c.txt(216, 67, 'zero', 'ghost')
    rail(c, (90, 76), (210, 76), VALUES[6], dormant, bipolar=True)
    c.line(((150, 72), (150, 80)), 'rail')
    hollow(c, 150, 76, dormant)
    traces(c, (82, 85, 240, 169), held=held)
    rail(c, (61, 92), (61, 174), VALUES[1], 'ghost' if held else 'spot', vertical=True)
    rail(c, (254, 59), (254, 111), VALUES[3], dormant, vertical=True)
    rail(c, (111, 183), (225, 183), VALUES[2], dormant)
    rail(c, (294, 116), (294, 181), VALUES[5], 'ink', vertical=True)
    for name, value, x, y in (('decay', VALUES[1], 31, 181),
                              ('smear', VALUES[2], 137, 193),
                              ('drift', VALUES[3], 229, 43),
                              ('mix', VALUES[5], 267, 190)):
        label = name + ' ' + ('held' if held and name != 'mix' else str(percent(value)) + '%')
        role = 'spot' if name == 'decay' and not held else 'ink' if name == 'mix' else 'ghost'
        c.txt(x, y, label, role)
    c.txt(142, 106, 'freeze', 'spot' if held else 'ink')
    c.txt(148 if held else 151, 122, 'held' if held else 'off', 'spot' if held else 'ghost')
    if held:
        c.line(((145, 135), (175, 135)), 'spot')
    c.txt(64, 207, 'cycle held' if held else 'cycle 2.5s', dormant)
    c.txt(190, 207, 'slow >', 'ghost')
    rail(c, (64, 220), (250, 220), VALUES[7], dormant)
    c.txt(12, 231, 'b back', 'ghost')
    c.txt(308, 231, 'l: mod >', 'spot', right=True)
    return c


def save_png(image, path, provenance):
    assert not path.exists(), 'preserve an earlier proposal; choose another version'
    info = PngImagePlugin.PngInfo()
    info.add_text('description', __doc__)
    info.add_text('provenance', json.dumps(provenance, sort_keys=True))
    info.add_text('source_sha256', json.dumps(input_hashes(), sort_keys=True))
    image.save(path, pnginfo=info)
    with Image.open(path) as decoded:
        decoded.load()
        assert decoded.size == image.size and decoded.convert('RGB').tobytes() == image.tobytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-only', action='store_true')
    args = parser.parse_args()
    before = input_hashes()
    runtime_before = runtime_sources()
    OUT.mkdir(parents=True, exist_ok=True)
    receipt = capture()
    if args.capture_only:
        print(OUT / 'capture')
        return
    target = OUT / 'ghost-refresh-v1-2x.png'
    assert not target.exists(), 'preserve prior proposal'
    proposal = [(upper(False), lower(False)), (upper(True), lower(True))]
    for pair in proposal:
        for page in pair:
            assert set(page.im.get_flattened_data()) <= set(COLORS.values())
    sheet = Image.new('RGB', (1296, 652), OUTSIDE)
    caption(sheet, 16, 14, 'seance / ghost / memory traces', 'ink')
    caption(sheet, 1280, 14, 'visual study 01 / graphite', right=True)
    states = [
        ('current / fresh c++ capture', native(BASELINE, 'top'), native(BASELINE, 'bottom'),
         'existing beads + separate mod', 'untouched native runtime pixels'),
        ('proposal / active tail', proposal[0][0].im, proposal[0][1].im,
         'four fading imprints; one l-mod entry', 'same eight parameters / no sound change'),
        ('proposal / frozen memory', proposal[1][0].im, proposal[1][1].im,
         'freeze seals the memory; mix stays live', 'authored held pose, not measured audio'),
    ]
    for i, (title, top, bottom, line1, line2) in enumerate(states):
        x = 16 + i * 432
        caption(sheet, x, 42, title, 'spot')
        assert top.size == (400, 240) and bottom.size == (320, 240)
        sheet.paste(top, (x, 60))
        sheet.paste(bottom, (x + 40, 316))
        caption(sheet, x, 580, line1)
        caption(sheet, x, 598, line2)
    caption(sheet, 16, 632, 'left: current runtime / center-right: visual proposal only / native 2x / not device footage')
    result = sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST)
    assert result.resize(sheet.size, Image.Resampling.NEAREST).tobytes() == sheet.tobytes()
    assert before == input_hashes() and runtime_before == runtime_sources(), 'source changed during render'
    provenance = dict(capture_at=receipt['captured_at'], binary_sha256=receipt['binary_sha256'],
                      baseline=BASELINE, baseline_sha256={name: value for name, value in receipt['surface_sha256'].items()
                                                        if name.startswith(BASELINE + '_')},
                      proposal='authored static C++-style visual concept; illustrative active/held shapes',
                      parameters=list(VALUES), production_changed=False, hardware=False, native_2x=True)
    save_png(result, target, provenance)
    dual = Image.new('RGB', (400, 496), OUTSIDE)
    dual.paste(proposal[0][0].im, (0, 0))
    dual.paste(proposal[0][1].im, (40, 256))
    save_png(dual.resize((800, 992), Image.Resampling.NEAREST), OUT / 'ghost-proposed-dual-2x.png', provenance)
    (OUT / 'proposal.json').write_text(json.dumps(dict(provenance, source_sha256=before,
        sheet_sha256=sha(target), dimensions=list(result.size),
        validation='text/figure bounds; palette; native sizing; nearest2x; PNG decode; unchanged current sources'),
        indent=2, sort_keys=True) + '\n')
    print(target.relative_to(ROOT))
    print('pass: text/figure bounds, graphite, native2x/decode, production unchanged; no regression suite')
    print(f'{result.width}x{result.height} / {target.stat().st_size} bytes / sha256 {sha(target)}')


if __name__ == '__main__':
    main()
