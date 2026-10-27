#!/usr/bin/env python3
"""real c++ sigils single-surface/thread-v2 captures; authored tests, not hardware shots."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import subprocess
from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUTSIDE = (36, 27, 39)
INK, SPOT, GHOST, BRANCH = (31, 20, 31), (91, 49, 124), (126, 100, 119), (174, 145, 165)
INKS = {INK, SPOT, GHOST, BRANCH}
WAVE = (12, 61, 308, 150)
OUTSIDE_WAVE = ((0, 0, 320, 61), (0, 150, 320, 240),
                (0, 61, 12, 150), (308, 61, 320, 150))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def paper_row(y):
    den = 239 ** 3
    n = y * y * (717 - 2 * y)
    return tuple((a * den + (b - a) * n + den // 2) // den
                 for a, b in zip((228, 194, 207), (211, 187, 206)))


def check_native(im, width):
    assert im.size == (width, 240)
    for y in range(240):
        row = list(im.crop((0, y, width, y + 1)).get_flattened_data())
        assert set(row) <= INKS | {paper_row(y)}, (y, set(row) - INKS - {paper_row(y)})
        assert row[:2] == [paper_row(y)] * 2


def check_text(path, width):
    rows = list(csv.DictReader(path.open(), delimiter='\t'))
    boxes = []
    for row in rows:
        x, y, w, h = (int(row[k]) for k in ('x', 'y', 'w', 'h'))
        assert 0 <= x < x + w <= width and 0 <= y < y + h <= 240, row
        box = (x, y, x + w, y + h)
        for other in boxes:
            assert not (box[0] < other[2] and box[2] > other[0]
                        and box[1] < other[3] and box[3] > other[1]), (path, box, other)
        boxes.append(box)
    return len(rows)


def save_native_and_2x(im, path):
    im.save(path.with_name(path.name + '_native.png'))
    big = im.resize((im.width * 2, im.height * 2), Image.Resampling.NEAREST)
    big.save(path.with_name(path.name + '_2x.png'))
    assert big.resize(im.size, Image.Resampling.NEAREST).tobytes() == im.tobytes()
    for suffix, expected in (('_native.png', im), ('_2x.png', big)):
        with Image.open(path.with_name(path.name + suffix)) as decoded:
            assert decoded.convert('RGB').tobytes() == expected.tobytes()


def approved_crops(main, out):
    """Compare existing PNGs only; never import/re-render the Python proposal UI."""
    reference = ROOT / 'ui_mockups/sigils_thread_v2/main_bottom_native.png'
    if not reference.exists():
        return {'status': 'unavailable', 'reason': str(reference.relative_to(ROOT)) + ' not found'}
    ref = Image.open(reference).convert('RGB')
    comparisons = []
    for box in OUTSIDE_WAVE:
        diff = ImageChops.difference(main.crop(box), ref.crop(box))
        different = sum(pixel != (0, 0, 0) for pixel in diff.get_flattened_data())
        comparisons.append({'crop': box, 'different_pixels': different})
    if any(row['different_pixels'] for row in comparisons):
        ImageChops.difference(main, ref).save(out / 'approved_difference_diagnostic.png')
    assert all(row['different_pixels'] == 0 for row in comparisons), comparisons
    return {'status': 'pass', 'reference_sha256': sha(reference), 'crops': comparisons,
            'scope': 'entire lower frame outside fixed draw hit; authored table and actual top metadata excluded'}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', default='/tmp/seance_sigils_menu_test')
    ap.add_argument('--output', default='ui_mockups/sigils_menu_runtime')
    ap.add_argument('--skip-tests', action='store_true', help='dump only; report explicitly records no test invocation')
    args = ap.parse_args()
    binary = Path(args.binary).resolve()
    out = (ROOT / args.output).resolve()
    out.mkdir(parents=True, exist_ok=True)
    test_result = None
    if not args.skip_tests:
        result = subprocess.run([str(binary), '--sigils-only'], check=True, cwd=ROOT,
                                text=True, capture_output=True)
        (out / 'focused_tests.log').write_text(result.stdout + result.stderr)
        test_result = {'command': [str(binary), '--sigils-only'], 'exit_code': result.returncode,
                       'summary': result.stdout.strip().splitlines()[-1]}
        print(result.stdout, end='')
    dump_result = subprocess.run([str(binary), '--dump-sigils-menu', str(out)], check=True,
                                 cwd=ROOT, text=True, capture_output=True)
    (out / 'dump.log').write_text(dump_result.stdout + dump_result.stderr)
    rows = list(csv.DictReader((out / 'manifest.tsv').open(), delimiter='\t'))
    assert rows and len({r['frame'] for r in rows}) == len(rows)
    duals, bottoms, text_counts = {}, {}, {}
    for row in rows:
        name = row['frame']
        top, bottom, shared = (int(row[k]) for k in ('top_objects', 'bottom_objects', 'shared_objects'))
        assert 0 < top and 0 < bottom and top + bottom == shared < 8192, row
        screens = []
        for screen, width in (('top', 400), ('bottom', 320)):
            im = Image.open(out / f'{name}_{screen}.ppm').convert('RGB')
            check_native(im, width)
            text_counts[name + '_' + screen] = check_text(out / f'{name}_{screen}_text.tsv', width)
            save_native_and_2x(im, out / f'{name}_{screen}')
            screens.append(im)
        bottoms[name] = screens[1]
        dual = Image.new('RGB', (400, 496), OUTSIDE)
        dual.paste(screens[0], (0, 0))
        dual.paste(screens[1], (40, 256))
        save_native_and_2x(dual, out / f'{name}_dual')
        duals[name] = dual
    reference_checks = approved_crops(bottoms['main'], out)
    for name in ('mode_rand', 'mode_smooth', 'follow_no_play', 'follow_muted', 'shot_stopped', 'undrawn'):
        colors = set(bottoms[name].crop(WAVE).get_flattened_data())
        assert SPOT not in colors, (name, 'fabricated active marker')
        if name in ('mode_rand', 'mode_smooth'):
            assert INK not in colors, (name, 'random should retain ghost stored ink, not active ink')
    assert SPOT in set(bottoms['follow_active'].crop(WAVE).get_flattened_data())
    mask = lambda im: bytes(px == INK for px in im.crop(WAVE).get_flattened_data())
    for name in ('depth_pos48', 'depth_neg64', 'depth_zero'):
        assert mask(bottoms[name]) == mask(bottoms['main']), (name, 'stored ink changed with depth')
    visible_zero = sum(bottoms['depth_zero'].getpixel((x, 105)) == BRANCH for x in range(20, 301))
    assert visible_zero > 230, visible_zero
    chosen = ['main', 'depth_pos48', 'depth_neg64', 'depth_zero',
              'mode_step', 'mode_rand', 'mode_smooth', 'follow_active']
    sheet = Image.new('RGB', (4 * 416, 2 * 526), OUTSIDE)
    captions = ImageDraw.Draw(sheet)
    for i, name in enumerate(chosen):
        x, y = (i % 4) * 416, (i // 4) * 526
        captions.text((x + 8, y + 4), name + ' / test', fill=(228, 194, 207))
        sheet.paste(duals[name], (x, y + 22))
    save_native_and_2x(sheet, out / 'comparison')
    lower_sheet = Image.new('RGB', (4 * 336, 2 * 268), OUTSIDE)
    captions = ImageDraw.Draw(lower_sheet)
    for i, name in enumerate(chosen):
        x, y = (i % 4) * 336, (i // 4) * 268
        captions.text((x + 8, y + 4), name + ' / test', fill=(228, 194, 207))
        lower_sheet.paste(bottoms[name], (x, y + 22))
    save_native_and_2x(lower_sheet, out / 'thread_gallery')
    report = {
        'status': __doc__, 'frames': len(rows), 'focused_tests': test_result,
        'capture_command': [str(binary), '--dump-sigils-menu', str(out)],
        'binary_sha256': sha(binary), 'peak_top': max(int(r['top_objects']) for r in rows),
        'peak_bottom': max(int(r['bottom_objects']) for r in rows),
        'peak_shared': max(int(r['shared_objects']) for r in rows), 'shared_budget': 8192,
        'checks': {'actual_cpp_screens_no_python_ui': True, 'exact_integer_menu_paper': True,
                   'four_inks_only': True, 'native_text_bounds_no_overlap': True,
                   'native_and_nearest2x_readback': True, 'no_invented_random_inactive_marker': True,
                   'authored_depth_ink_mask_invariant': True, 'visible_zero_columns': visible_zero,
                   'real_engine_follow_marker': True, 'approved_outside_wave': reference_checks},
        'geometry': {'top': [400, 240], 'bottom': [320, 240], 'wave': WAVE, 'zero_y': 105,
                     'output_limit_y': [67, 143]},
        'source_sha256': {p: sha(ROOT / p) for p in (
            'platform/3ds/ui_sigils.cpp', 'platform/3ds/ui_sigils.h', 'platform/3ds/ui_top.cpp',
            'platform/3ds/main.cpp', 'platform/3ds/menu_style.cpp', 'tools/sequencer_test.cpp',
            'tools/sigils_menu_test.h', 'tools/render_sigils_menu_runtime.py', 'Makefile')},
        'text_counts': text_counts, 'fixtures': rows,
        'limitations': ['software Citro2D line coverage is approximate, not PICA/LCD acceptance',
                        '128-point asymmetric curve, initial value/phase and random endpoints are authored test data',
                        'follow fixture uses the actual engine and a deterministic 32000-frame test sample',
                        'only exact existing reference PNG frame crops are compared; waveform is not a screenshot trace',
                        'no FTP/deployment, CIA, hardware timing or audio-quality claim'],
    }
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    (out / 'README.txt').write_text(__doc__ + '\n'
        'run: make test-sigils-menu && python3 tools/render_sigils_menu_runtime.py\n'
        'sanitize separately: make sanitize-sigils-menu\n'
        'all native instrument pixels and text come from runtime C++ and real engine storage.\n'
        'python only validates PPMs, converts PNGs and arranges screenshots/captions.\n'
        'authored fixture values are not live hardware telemetry. only follow uses running PLAY.\n'
        'capture/read-only/null-engine/bounds/budget assertions live in tools/sigils_menu_test.h.\n'
        + '\n'.join(report['limitations']) + '\n')
    print(out / 'comparison_2x.png')
    print(out / 'thread_gallery_2x.png')
    print(f"pass: {len(rows)} real c++ pairs, peak {report['peak_shared']}/8192, native/nearest2x, exact frame crops")


if __name__ == '__main__':
    main()
