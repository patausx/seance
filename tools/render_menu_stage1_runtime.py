#!/usr/bin/env python3
"""Render the REAL first-stage C++ menu UI; no replacement Python layout.

The host raster approximates Citro2D line coverage, not a hardware screenshot.
Run make test-menu-stage1 first; this wrapper does not run broad tests/builds.
"""
import argparse
import csv
import hashlib
import json
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageChops
from generate_menu_labels import masks

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', type=Path, default=Path('/tmp/seance_menu_stage1_test'))
    ap.add_argument('--out', type=Path, default=ROOT / 'ui_mockups/menu_stage1_runtime')
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(args.binary.resolve()), '--dump', str(out)], cwd=ROOT, check=True)
    rows = list(csv.DictReader((out / 'frames.tsv').open(), delimiter='\t'))
    summary = json.loads((out / 'summary.json').read_text())
    cards = []
    for row in rows:
        name = row['name']
        pair = Image.new('RGB', (400, 496), (37, 27, 36))
        for screen, width, x, y in (('top', 400, 0, 0), ('bottom', 320, 40, 256)):
            image = Image.open(out / f'{name}_{screen}.ppm').convert('RGB')
            assert image.size == (width, 240)
            image.save(out / f'{name}_{screen}_native.png')
            big = image.resize((width * 2, 480), Image.Resampling.NEAREST)
            assert ImageChops.difference(big.resize(image.size, Image.Resampling.NEAREST), image).getbbox() is None
            big.save(out / f'{name}_{screen}_2x.png')
            pair.paste(image, (x, y))
        pair.save(out / f'{name}_dual_native.png')
        pair.resize((800, 992), Image.Resampling.NEAREST).save(out / f'{name}_dual_2x.png')
        cards.append((name, pair))
    # Exact C++ heading pixels must match independent original-font raster masks.
    for name, text, xy in (('nav_0', 'go to', (38, 20)), ('picker_first', 'add module', (38, 20)),
                           ('chains_four', 'patch table', (24, 14))):
        if not (out / f'{name}_bottom_native.png').exists():
            continue
        im = Image.open(out / f'{name}_bottom_native.png').convert('RGB')
        mask = dict(masks())[text]
        crop = im.crop((*xy, xy[0] + mask.width, xy[1] + mask.height))
        actual = Image.new('L', mask.size)
        actual.putdata([255 if p == (31, 20, 31) else 0 for p in crop.get_flattened_data()])
        assert ImageChops.difference(mask, actual).getbbox() is None, (name, 'heading mismatch')
    cols = 4
    sheet = Image.new('RGB', (cols * 416, ((len(cards) + cols - 1) // cols) * 530), (37, 27, 36))
    d = ImageDraw.Draw(sheet)
    for i, (name, pair) in enumerate(cards):
        x, y = (i % cols) * 416 + 8, (i // cols) * 530 + 8
        d.text((x, y), name, fill=(228, 194, 207))
        sheet.paste(pair, (x, y + 20))
    sheet.save(out / 'contact_native.png')
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(out / 'contact_2x.png')
    # A compact first-stage overview, using the same captured C++ pairs only.
    overview = Image.new('RGB', (4 * 416, 520), (37, 27, 36))
    od = ImageDraw.Draw(overview)
    by_name = dict(cards)
    for i, name in enumerate(('nav_0', 'picker_fm', 'chains_four', 'chains_branch')):
        od.text((i * 416 + 8, 4), name, fill=(228, 194, 207))
        overview.paste(by_name[name], (i * 416 + 8, 24))
    overview.resize((overview.width * 2, 1040), Image.Resampling.NEAREST).save(out / 'comparison_2x.png')
    files = ['platform/3ds/ui_nav.cpp', 'platform/3ds/ui_picker.cpp', 'platform/3ds/ui_chains.cpp',
             'platform/3ds/ui_workspace.cpp', 'platform/3ds/menu_style.cpp',
             'platform/3ds/menu_labels_data.h', 'platform/3ds/ui_chains_top.cpp',
             'tools/menu_stage1_test.cpp', 'tools/menu_stage1_test_stubs/3ds.h',
             'tools/render_menu_stage1_runtime.py', 'Makefile']
    report = {'source': 'real C++ controllers and draw functions with real engine fixtures',
              'limits': ['not hardware capture', 'software line coverage approximates Citro2D',
                         'top calls real chains_context_draw_top directly, not top_draw/main dispatch',
                         'clipped world-space primitives are counted, not misreported as all in-bounds',
                         'no application launch, target performance or touch ergonomics claim'],
              'fixture': 'actual FM drone / STRN / Ghost / REV chain; world positions in C++ harness',
              'checks': {'exact_heading_masks': True, 'nearest_2x': True,
                         'cpp_summary': summary},
              'frames': rows,
              'max_shared_objects': summary['max_shared_with_underlay'],
              'max_gallery_objects': max(int(r['top_objects']) + int(r['bottom_objects']) for r in rows),
              'sources': {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in files}}
    assert report['max_shared_objects'] < 8192
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    (out / 'README.txt').write_text(__doc__ + '\nActual C++ fixtures, no Python UI redraw.\n'
        'Native 400x240 top, 320x240 bottom; nearest2x and dual/contact PNGs.\n'
        'Reproduce: make test-menu-stage1 && python3 tools/render_menu_stage1_runtime.py\n')
    print(out / 'contact_2x.png')
    print(f'pass: {len(rows)} real UI pairs, headings / 2x; max shared {report["max_shared_objects"]}/8192')


if __name__ == '__main__':
    main()
