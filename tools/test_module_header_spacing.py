#!/usr/bin/env python3
"""Fixed module identity inset. Default: fast source/constexpr check.

--capture: one current optimized native-host UI capture, not historical suites,
ARM, hardware or deployment. Reuses only the existing composed host boundary.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'platform/3ds'
OUT = ROOT / 'release/module-header-spacing'
ORIGIN = (14, 8)


def source_check():
    from test_module_footer_spacing import SOURCES
    files = [UI / ('ui_module_focus_' + stem + '.cpp') for stem in SOURCES.values()]
    files += [UI / n for n in ('module_inspector.cpp', 'fm_context.cpp', 'ui_play_editor.cpp')]
    kinds = set()
    calls = 0
    for path in files:
        text = path.read_text()
        assert 'module_heading::header(' in text, path
        assert 'module_heading::draw(' not in text, ('per-page title coordinates', path)
        calls += text.count('module_heading::header(')
        kinds.update(re.findall(r'module_heading::header\(UnitKind::(\w+)', text))
    assert kinds == set(SOURCES) and calls == 53, (kinds, calls)
    for path in UI.glob('*.cpp'):
        assert 'module_heading::draw(' not in path.read_text(), path
    subprocess.run(['g++', '-std=c++17', '-I.', '-x', 'c++', '-fsyntax-only', '-'],
                   cwd=ROOT, check=True, text=True, input='''
#include "platform/3ds/module_heading.h"
using namespace seance::platform::ui3ds;
static_assert(module_heading::kX == 14 && module_heading::kY == 8);
static_assert(module_heading::max_height() == 25);
static_assert(module_heading::kRouteY == 35);
static_assert(module_heading::kRouteUnderlineY == 45);
static_assert(module_heading::kDetailY >= module_heading::kRouteY + 12);
constexpr bool fits() {
    for (const auto& label : module_heading_data::labels) {
        if (module_heading::kX + label.width > 320) return false;
        if (module_heading::kY + label.height + 2 > module_heading::kRouteY) return false;
    }
    return true;
}
static_assert(fits());
''')
    print('header spacing: 28 kinds / 31 render sources / 53 fixed calls; (14,8), route y35; source/constexpr passed')


def capture():
    import test_module_headings_ui as host
    import verify_module_identity_current as masks
    from PIL import Image, ImageDraw
    # Historical fixtures/receipts stay unchanged. Narrow their private composed
    # driver to default+lock routes for all kinds, long/holed routes for the
    # moved breadcrumbs, and the existing workflow/selected-view touch cases.
    text = masks.current_cases()
    old = '''for(const auto&item:f::catalogue){state(item.kind,p,"default");if(changed(item.kind))for(const char*v:{"bypass","long6","holes","live","lens","lock"})state(item.kind,p,v);
   else if(workflow(item.kind))for(const char*v:{"lens","lock"})state(item.kind,p,v);}'''
    new = '''for(const auto&item:f::catalogue){state(item.kind,p,"default");state(item.kind,p,"lock");}
  for(auto k:{UnitKind::Chorus,UnitKind::Delay,UnitKind::Ghost,UnitKind::Reverb,UnitKind::Stranular,UnitKind::Ring,UnitKind::Fm})
   for(const char*v:{"long6","holes"})state(k,p,v);'''
    assert text.count(old) == 1
    text = text.replace(old, new)
    OUT.mkdir(parents=True, exist_ok=True)
    cases = OUT / ('cases-' + host.stamp() + '.inc')
    with cases.open('x') as f:
        f.write(text)
    host.OUT = OUT
    host.CASES = cases
    host.supplement = lambda: {}  # No historical source copying or old oracle.
    receipt, proof = host.build(ROOT, 'inset-optimized', capture=True)
    rows = proof['surfaces']
    assert all(r['outside_rects'] == '0' and r['outside_lines'] == '0' for r in rows)
    assert all(r['text_overlaps'] == '0' for r in rows), [r for r in rows if r['text_overlaps'] != '0']
    defaults = [r for r in rows if r['variant'] == 'default']
    assert len(defaults) == 112 and len({r['module'] for r in defaults}) == 28
    labels = masks.load_masks()
    folder = Path(proof['capture'])
    verified = []
    for r in rows:
        name = masks.ALIASES.get(r['module'], r['module'])
        theme = r['id'].split('_', 1)[0]
        image = folder / (r['id'] + '_' + r['surface'] + '.ppm')
        checked = masks.exact_title(image, labels[name], masks.SPOT_RGB[theme], origin=ORIGIN)
        checked.update(module=r['module'], variant=r['variant'])
        verified.append(checked)
    # Compose real native header strips only; no simulated application drawing.
    for theme in masks.SPOT_RGB:
        selected = [r for r in defaults if r['surface'] == 'top' and r['id'].startswith(theme + '_')]
        board = Image.new('RGB', (1632, 7 * 144), (18, 18, 21))
        draw = ImageDraw.Draw(board)
        for i, r in enumerate(selected):
            x, y = i % 4 * 408, i // 4 * 144
            draw.text((x + 6, y + 2), r['module'], fill='white')
            for surface, dx, dy in (('top', 0, 18), ('bottom', 40, 80)):
                with Image.open(folder / (r['id'] + '_' + surface + '.ppm')) as im:
                    board.paste(im.crop((0, 0, im.width, 60)), (x + dx, y + dy))
        board.save(receipt.parent / (theme + '-headers.png'))
    result = {'status': 'passed', 'origin': ORIGIN, 'receipt': str(receipt),
              'receipt_sha256': host.sha(receipt), 'tool_sha256': host.sha(__file__),
              'native_duals': len(rows) // 2, 'default_surfaces': len(defaults),
              'exact_title_surfaces': len(verified), 'title_checks': verified,
              'text_overlaps': 0, 'outside': 0,
              'limits': ['current optimized software host only; no device or ARM build',
                         'synthetic state and workflow fixtures, not full audio/gesture regression',
                         'MOD lens overlay not covered by this spacing capture']}
    host.json_write(receipt.parent / 'header-verification.json', result)
    print(json.dumps({k: v for k, v in result.items() if k != 'title_checks'}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', action='store_true')
    args = parser.parse_args()
    source_check()
    if args.capture:
        capture()
