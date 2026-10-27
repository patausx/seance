#!/usr/bin/env python3
"""Audit and assemble REAL C++ GO TO clock frames; never redraw instrument UI.

Uses existing menu-stage1 real controller/portable engine/native raster shim.
The top is chains_context_draw_top, not full main/top_draw dispatch. Native line
coverage approximates Citro2D; no target/LCD/stylus/audio-performance claim.
"""
import argparse
import csv
import hashlib
import json
import os
import subprocess
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
HEADER = (148, 0, 320, 58)
CURRENT_NAV_ROWS = 7
APPROVED = ROOT / 'ui_mockups/go_to_clock_refinement/02_branch_no_routes_native.png'
BASELINE = ROOT / 'ui_mockups/menu_stage1_runtime'
INKS = {(31, 20, 31), (91, 49, 124), (126, 100, 119), (174, 145, 165)}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def paper_row(y):
    d, n = 239**3, y*y*(717-2*y)
    return tuple((a*d+(b-a)*n+d//2)//d for a, b in zip((228, 194, 207), (211, 187, 206)))


def native(path, width):
    im = Image.open(path).convert('RGB')
    assert im.size == (width, 240), (path, 'native size')
    for y in range(240):
        row = list(im.crop((0, y, width, y+1)).get_flattened_data())
        bg = paper_row(y)
        assert all(p == bg or p in INKS for p in row), (path, y, 'palette/paper')
        assert all(row[x] == bg for x in (0, 1, width-2, width-1)), (path, y, 'paper edge')
    return im


def raw_text(path):
    records = list(csv.DictReader(path.open(), delimiter='\t'))
    groups = {}
    for record in records:
        key = (record['name'], record['screen'])
        box = tuple(int(record[v]) for v in ('x', 'y', 'w', 'h'))
        x, y, w, h = box
        assert x >= 0 and y >= 0 and x+w <= (400 if key[1] == 'top' else 320) and y+h <= 240, record
        for old in groups.setdefault(key, []):
            ox, oy, ow, oh = old
            assert not (x < ox+ow and ox < x+w and y < oy+oh and oy < y+h), (record, old)
        groups[key].append(box)
        assert record['text'] not in ('a', 'b', 'project', 'stress'), ('fictitious clock route/page or retired item', record)
    return len(records)


def save(im, path):
    im.save(path.with_name(path.name+'_native.png'))
    big = im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST)
    assert big.resize(im.size, Image.Resampling.NEAREST).tobytes() == im.tobytes()
    big.save(path.with_name(path.name+'_2x.png'))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, default=Path('/tmp/seance_nav_clock_test'))
    ap.add_argument('--out', type=Path, default=ROOT / 'ui_mockups/nav_clock_runtime')
    ap.add_argument('--compare-dir', type=Path, help='byte-compare all focused and old-mode PPMs to normal output')
    args = ap.parse_args()
    out = args.out.resolve()
    assert out != BASELINE.resolve(), 'never overwrite previous authoritative captures'
    out.mkdir(parents=True, exist_ok=True)
    protected = {str(p): sha(p) for p in [APPROVED, *BASELINE.glob('*.ppm')]}
    env = os.environ.copy()
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=1:halt_on_error=1')
    env.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
    subprocess.run([str(args.binary.resolve()), '--dump-nav-clock', str(out)], cwd=ROOT, env=env, check=True)
    regression = out / 'old_menu_regression'
    subprocess.run([str(args.binary.resolve()), '--dump', str(regression)], cwd=ROOT, env=env, check=True)
    regression_rows = list(csv.DictReader((regression / 'frames.tsv').open(), delimiter='\t'))
    nav_names = {row['name'] for row in regression_rows if row['name'].startswith('nav_')}
    assert nav_names == {f'nav_{i}' for i in range(CURRENT_NAV_ROWS)}, 'current seven-destination capture required'
    rows = list(csv.DictReader((out / 'frames.tsv').open(), delimiter='\t'))
    summary = json.loads((out / 'summary.json').read_text())
    assert summary['suites'] == 10 and summary['max_paired_objects'] < 8192
    assert summary['raw_text_overlaps'] == summary['raw_text_outside'] == summary['primitive_outside'] == 0
    text_count = raw_text(out / 'raw_text.tsv')
    cards = {}
    for row in rows:
        name = row['name']
        assert int(row['out_of_bounds']) == 0
        assert int(row['top_objects']) + int(row['bottom_objects']) < 8192
        pair = Image.new('RGB', (400, 496), (37, 27, 36))
        for screen, width, x, y in (('top', 400, 0, 0), ('bottom', 320, 40, 256)):
            im = native(out / f'{name}_{screen}.ppm', width)
            save(im, out / f'{name}_{screen}')
            pair.paste(im, (x, y))
        save(pair, out / f'{name}_dual')
        cards[name] = pair
    approved = Image.open(APPROVED).convert('RGB')
    actual = Image.open(out / '01_approved_idle_bottom.ppm').convert('RGB')
    assert ImageChops.difference(actual.crop(HEADER), approved.crop(HEADER)).getbbox() is None, 'approved no-routes header not exact'
    assert (out / '04_tempo_pending_bottom.ppm').read_bytes() == (out / '05_pending_frozen_bottom.ppm').read_bytes()
    old_checks, unchanged, historical_nav = {}, [], []
    for old in sorted(BASELINE.glob('*.ppm')):
        # Archived nav rows predate bounce/stress removal. Their row bodies and
        # indices are not a current-layout oracle; preserve them, including any
        # retired nav_7 pair. Current navigation is checked by the C++ harness.
        is_nav = old.name.startswith('nav_')
        nav_name = old.name.rsplit('_', 1)[0]
        if is_nav and (old.name.endswith('_bottom.ppm') or nav_name not in nav_names):
            historical_nav.append(old.name)
            old_checks[old.name] = 'historical navigation layout; preserved, not current pixel proof'
            continue
        new = regression / old.name
        assert new.exists(), ('missing old frame', old.name)
        assert old.read_bytes() == new.read_bytes(), ('unrelated old frame changed', old.name)
        unchanged.append(old.name)
        old_checks[old.name] = 'byte exact'
    sanitized = 0
    if args.compare_dir:
        other = args.compare_dir.resolve()
        for p in sorted(out.rglob('*.ppm')):
            assert p.read_bytes() == (other / p.relative_to(out)).read_bytes(), ('normal/sanitizer pixel mismatch', p)
            sanitized += 1
        assert (out / 'raw_text.tsv').read_bytes() == (other / 'raw_text.tsv').read_bytes()
    chosen = ['01_approved_idle', '02_tempo_focus', '03_swing_focus', '04_tempo_pending']
    overview = Image.new('RGB', (4*416, 528), (37, 27, 36))
    draw = ImageDraw.Draw(overview)
    for i, name in enumerate(chosen):
        draw.text((i*416+8, 4), name, fill=(228, 194, 207))
        overview.paste(cards[name], (i*416+8, 24))
    save(overview, out / 'comparison')
    gallery = Image.new('RGB', (4*416, ((len(cards)+3)//4)*528), (37, 27, 36))
    draw = ImageDraw.Draw(gallery)
    for i, (name, card) in enumerate(cards.items()):
        x, y = i%4*416+8, i//4*528+4
        draw.text((x, y), name, fill=(228, 194, 207))
        gallery.paste(card, (x, y+20))
    save(gallery, out / 'gallery')
    comparison = Image.new('RGB', (656, 86), (37, 27, 36))
    draw = ImageDraw.Draw(comparison)
    for x, name, im in ((4, 'approved 02 / no routes', approved), (332, 'actual c++ / exact header', actual)):
        draw.text((x, 3), name, fill=(228, 194, 207))
        comparison.paste(im.crop((0, 0, 320, 58)), (x, 23))
    save(comparison, out / 'approved_header_comparison')
    sources = ['platform/3ds/ui_nav.cpp', 'platform/3ds/ui_nav.h', 'platform/3ds/main.cpp',
               'tools/menu_stage1_test.cpp', 'tools/nav_clock_test.h', 'tools/render_nav_clock_runtime.py',
               'tools/menu_stage1_test_stubs/3ds.h', 'tools/sequencer_test_stubs/3ds.h', 'Makefile']
    report = {
        'source': 'real C++ nav controller/engine/render, no Python instrument redraw',
        'checks': {'approved_no_routes_header_pixel_exact': True, 'approved_header_box_half_open': HEADER,
                   'raw_text_records': text_count, 'raw_text_bounds_and_overlaps_no_opacity_waivers': True,
                   'per_row_paper_palette': True, 'native_and_nearest_2x': True,
                   'frozen_pending_null_engine_audio_exact': True, 'cpp': summary,
                   'old_frames_byte_exact': len(unchanged), 'historical_nav_frames_not_compared': len(historical_nav),
                   'current_nav_rows': CURRENT_NAV_ROWS,
                   'normal_sanitizer_ppms_exact': sanitized},
        'frames': rows, 'old_mode_regression': old_checks,
        'max_gallery_paired_objects': max(int(r['top_objects'])+int(r['bottom_objects']) for r in rows),
        'sources_sha256': {p: sha(ROOT / p) for p in sources},
        'limits': ['host software raster, not LCD capture', 'top calls chains_context_draw_top directly, not full main dispatch',
                   'single-thread host locks are not concurrency stress or guarded-read instrumentation',
                   'button input edges supplied by harness; real stylus/button ergonomics untested',
                   'same legacy clock setter rescheduling; no phase-continuity claim',
                   'no project pages, global clock modulation, audio/DSP/serializer changes, upload or target performance claim']}
    assert protected == {p: sha(Path(p)) for p in protected}, 'previous reference modified'
    (out / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(out / 'comparison_2x.png')
    print(f'pass: {len(rows)} C++ pairs; approved exact; {len(unchanged)} old PPMs exact; {len(historical_nav)} historical nav PPMs preserved, not compared; {text_count} raw text records; sanitizer PPMs={sanitized}; peak={summary["max_paired_objects"]}/8192')


if __name__ == '__main__':
    main()
