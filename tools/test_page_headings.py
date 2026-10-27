#!/usr/bin/env python3
"""Focused page-title typography proof; no full UI/engine build, ARM or FTP.

The font oracle is a fresh Pillow/FreeType 28ppem baseline ('ls') render, not
page_layout or menu_labels-derived pixels. Real C++ menu_style and core draw/font
are rasterized at the Citro2D rectangle boundary. All old headings are checked
against both the OTF and a hash-pinned pre-change C++ heading implementation.
Each run writes only beneath page_headings_runtime/typography; old runs survive.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
from PIL import Image, ImageDraw, ImageFont, __version__ as PILLOW_VERSION

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'ui_mockups/page_headings_runtime/typography'
FONT = ROOT/'assets/fonts/Gossip-MedPixel.otf'
LEGACY = ROOT/'.local-state/backups/20260912_180119_page-headings-before_menu_style.cpp'
LEGACY_SHA = 'b4d572ac4141a3cd4612397ea3e8718ff6277f6214ee9fb08a1c1e166012dead'
FONT_SHA = '6ac1f95bfcf19a501bcd2b228bdae9a22321785d743c7424f36f35fe5138520c'
LABEL_SHA = '4e77c63c71eaee875aaea5d606b6e2b71086365777fcbd879de4455c42f4c8f8'
TITLES = [(0, 'PatchTable'), (1, 'GoTo'), (2, 'AddModule'), (24, 'Sigils'),
          (25, 'Tempo'), (26, 'Swing'), (27, 'Patches'), (28, 'UnsavedChanges'),
          (32, 'Samples'), (40, 'Bounce'), (47, 'ReplaceChain'),
          (48, 'DiscardTake'), (50, 'Perform'), (51, 'Sequencer')]
SOURCES = ['tools/test_page_headings.py', 'tools/page_headings_test.cpp',
           'platform/3ds/page_layout.h', 'platform/3ds/menu_style.h',
           'platform/3ds/menu_style.cpp', 'platform/3ds/menu_labels_data.h',
           'platform/3ds/nav_ink_motion.h', 'platform/3ds/ui_palette.h',
           'core/ui/draw.cpp', 'core/ui/draw.h', 'core/ui/font.cpp', 'core/ui/font.h',
           'tools/sequencer_test_stubs/citro2d.h', 'tools/sequencer_test_stubs/3ds.h']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def references(build, legacy):
    assert sha(FONT) == FONT_SHA, 'original font changed'
    assert sha(legacy) == LEGACY_SHA, 'need the exact pre-page-heading implementation'
    label_file = ROOT/'platform/3ds/menu_labels_data.h'
    assert sha(label_file) == LABEL_SHA, 'all 52 old masks/metrics must remain byte-exact'
    source = label_file.read_text()
    labels = re.findall(r'\{"([^\"]+)", (\d+), (\d+), (\d+)\}', source)
    packed = bytes(int(value, 16) for value in re.findall(
        r'0x([0-9a-f]{2})', source.split('inline constexpr Label')[0]))
    assert len(labels) == 52
    masks, pixels, records = [], [], []
    for index, (text, offset, width, height) in enumerate(labels):
        ppem = 56 if 34 <= index <= 39 else 28
        face = ImageFont.truetype(str(FONT), ppem)
        # Oversized, unclipped baseline canvas. Do not import the production
        # generator or use its bbox positioning/crop to obtain normal masks.
        image = Image.new('L', (512, 192))
        ImageDraw.Draw(image).text((32, 96), text, font=face, fill=255, anchor='ls')
        image = image.point(lambda value: 255 if value >= 128 else 0)
        left, top, right, bottom = image.getbbox()
        ink_bounds = [left-32, top-96, right-32, bottom-96]
        if ppem == 56:
            # Retain the six already-approved hero words' common ascent+8 top,
            # independently expressed in baseline coordinates, not ink-cropped.
            top = 96 - face.getmetrics()[0] + 8
        mask = image.crop((left, top, right, bottom))
        w, h, off = int(width), int(height), int(offset)
        old_pixels = bytes(255 if packed[off+y*((w+7)//8)+x//8] & (128 >> (x % 8))
                           else 0 for y in range(h) for x in range(w))
        assert mask.size == (w, h) and mask.tobytes() == old_pixels, text
        masks.append(mask)
        records.append(dict(index=index, text=text, ppem=ppem, width=w, height=h,
                            ink_bounds_relative_baseline=ink_bounds,
                            mask_top_relative_baseline=top-96,
                            oracle_offset=len(pixels), mask_sha256=sha_bytes(mask.tobytes()),
                            ink_pixels=sum(value != 0 for value in mask.tobytes())))
        pixels.extend(int(value != 0) for value in mask.tobytes())
    header = ['#pragma once', '#include <cstdint>',
              '#include "'+str(ROOT/'platform/3ds/page_layout.h')+'"',
              'namespace page_heading_reference {',
              'struct Mask { const char* text; int width,height,left,top,bottom,ppem,offset; };',
              'inline constexpr uint8_t pixels[] = {']
    header += [','.join(map(str, pixels[i:i+100]))+',' for i in range(0, len(pixels), 100)]
    header += ['};', 'inline constexpr Mask masks[] = {']
    for row in records:
        b = row['ink_bounds_relative_baseline']
        header.append('{'+json.dumps(row['text'])+','+','.join(map(str, (
            row['width'], row['height'], b[0], row['mask_top_relative_baseline'],
            b[3], row['ppem'], row['oracle_offset'])))+'},')
    header += ['};', 'struct Title { int index; seance::platform::ui3ds::page_layout::Title title; };',
               'inline constexpr Title titles[] = {']
    header += ['{'+str(index)+',seance::platform::ui3ds::page_layout::Title::'+name+'},'
               for index, name in TITLES]
    header += ['};', '}']
    (build/'page_headings_reference.h').write_text('\n'.join(header)+'\n')
    old = legacy.read_text()
    legacy_header = '#pragma once\n#include <cstring>\n#include "'+str(label_file)+'"\n'
    legacy_header += 'namespace seance::platform::ui3ds::old_heading_reference {\n'
    for signature in ('const menu_labels::Label* find_label(', 'int heading_width(', 'void heading('):
        legacy_header += function(old, signature)+'\n'
    (build/'page_headings_legacy.h').write_text(legacy_header+'}\n')
    return masks, records


def sha_bytes(data):
    return hashlib.sha256(data).hexdigest()


def paper_row(gray, y):
    if gray:
        return (36, 36, 36)
    d, t = 239**3, y*y*(717-2*y)
    return tuple((a*d + delta*t + d//2)//d for a, delta in ((228, -17), (194, -7), (207, -1)))


def package(run, masks, records):
    tiles = []
    proofs = []
    for index, name in TITLES:
        row = records[index]
        y = 32 + row['ink_bounds_relative_baseline'][1]
        tile = Image.new('RGB', (320, 112), (16, 16, 16))
        ImageDraw.Draw(tile).text((4, 1), f'{row["text"]} | 28ppem | x20 baseline32 y{y}', fill=(244, 244, 244))
        for gray in (False, True):
            theme = 'gray' if gray else 'pink'
            raw = run/'raw'/f'{theme}_{index}.ppm'
            native = Image.open(raw).convert('RGB')
            assert native.size == (320, 240)
            expected = Image.new('RGB', native.size)
            for yy in range(240):
                expected.paste(paper_row(gray, yy), (0, yy, 320, yy+1))
            ink = (244, 244, 244) if gray else (31, 20, 31)
            expected.paste(ink, (20, y, 20+masks[index].width, y+masks[index].height), masks[index])
            assert native.tobytes() == expected.tobytes(), (theme, name)
            path = run/f'{theme}_{index}.png'
            native.save(path)
            tile.paste(native.crop((0, 0, 320, 48)), (0, 16+int(gray)*48))
            proofs.append(dict(theme=theme, title=name, label_index=index, ink_left=20,
                               baseline=32, ink_top=y, width=row['width'], height=row['height'],
                               cpp_ppm_sha256=sha(raw), png_sha256=sha(path),
                               independent_full_native_pixels=320*240))
        tiles.append(tile)
    sheet = Image.new('RGB', (640, 7*112), (16, 16, 16))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, ((i % 2)*320, (i//2)*112))
    sheet.save(run/'contact_sheet.png')
    return dict(actual_cpp_screens=len(proofs), independent_full_native_pixels=len(proofs)*320*240,
                images=proofs, sheet='contact_sheet.png: native actual C++ top48 crops; annotations outside pixels')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--out', type=Path, default=OUT)
    parser.add_argument('--legacy-menu', type=Path, default=LEGACY)
    args = parser.parse_args()
    out = args.out.resolve()
    assert out.is_relative_to(OUT), 'typography proof must not overwrite historical receipts'
    run = out/('run-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ'))
    build = run/'build'
    build.mkdir(parents=True)
    (run/'checks').mkdir()
    (run/'raw').mkdir()
    before = {path: sha(ROOT/path) for path in SOURCES}
    masks, metrics = references(build, args.legacy_menu.resolve())
    (run/'font_metrics.json').write_text(json.dumps(metrics, indent=2)+'\n')
    checks = []

    def execute(command, name, env=None):
        start = time.monotonic()
        result = subprocess.run(list(map(str, command)), cwd=ROOT, text=True,
                                capture_output=True, timeout=180, env=env)
        (run/'checks'/f'{name}.log').write_text(result.stdout+result.stderr)
        record = dict(name=name, command=list(map(str, command)), returncode=result.returncode,
                      seconds=round(time.monotonic()-start, 3))
        (run/'checks'/f'{name}.json').write_text(json.dumps(record, indent=2)+'\n')
        checks.append(record)
        print(result.stdout, end='')
        if result.returncode:
            print(result.stderr)
            print('retained failure:', run/'checks'/f'{name}.log')
        result.check_returncode()
        return result.stdout

    common = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-ffunction-sections',
              '-fdata-sections', '-I'+str(build), '-Itools/sequencer_test_stubs',
              'tools/page_headings_test.cpp', 'platform/3ds/menu_style.cpp',
              'core/ui/draw.cpp', 'core/ui/font.cpp', '-Wl,--gc-sections',
              '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc']
    binary = build/'page_headings_test'
    execute(common+['-O2', '-o', binary], 'build')
    log = execute([binary, run/'raw'], 'focused')
    sanitizer = None
    if args.sanitize:
        san = build/'page_headings_san'
        execute(common+['-O1', '-g', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-o', san], 'sanitize-build')
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        execute([san], 'sanitize', env)
        sanitizer = dict(asan_ubsan_leaks='pass', binary_sha256=sha(san),
                         scope='real menu_style/core draw/font plus pure nav/layout headers, host rectangle boundary')
    capture = package(run, masks, metrics)
    for path, digest in before.items():
        assert sha(ROOT/path) == digest, ('source changed during proof', path)
    summary = re.search(r'pass page-headings: (\d+) checks; (\d+) independent font/native oracle pixels; '
                        r'(\d+) page old/new trace pairs; (\d+) legacy arbitrary-coordinate trace pairs; '
                        r'(\d+) fallback pairs; (\d+) actual native captures; peak page (\d+) / legacy (\d+) rectangles; '
                        r'(\d+) guarded allocations; 0 bounds; 14 titles / 52 masks / both themes', log)
    assert summary, log
    fields = ('checks', 'independent_native_oracle_pixels', 'page_trace_pairs', 'legacy_trace_pairs',
              'fallback_pairs', 'captures', 'peak_page_rectangles', 'peak_legacy_rectangles', 'guarded_allocations')
    receipt = dict(status='pass', run=str(run.relative_to(ROOT)), checks=checks,
                   summary=dict(zip(fields, map(int, summary.groups()))),
                   font=dict(path=str(FONT.relative_to(ROOT)), sha256=sha(FONT), pillow=PILLOW_VERSION,
                             ppem=28, threshold=128, anchor='ls', baseline=32, ink_left=20,
                             normal_crop='tight thresholded ink', old_56ppem_heroes=6,
                             all_52_mask_metrics_pixels_exact=True, page_title_count=len(TITLES)),
                   source_sha256=before, old_mask_header_sha256=LABEL_SHA,
                   legacy=dict(path=str(args.legacy_menu.resolve().relative_to(ROOT)), sha256=LEGACY_SHA),
                   references={path.name: sha(path) for path in (build/'page_headings_reference.h', build/'page_headings_legacy.h')},
                   binary_sha256=sha(binary), sanitizer=sanitizer, capture=capture,
                   limits=['focused font/layout/native heading proof, not whole-page controller or collision coverage',
                           'rectangle submissions/allocation guards are host proof, not GPU frame budget or hardware timing',
                           'no font runtime/dependency, engine/DSP/audio changes or proof',
                           'no broad suites, ARM, FTP, hardware or listening acceptance'])
    text = json.dumps(receipt, indent=2)+'\n'
    (run/'verification.json').write_text(text)
    (out/'verification.json').write_text(text)
    print(out/'verification.json')


if __name__ == '__main__':
    main()
