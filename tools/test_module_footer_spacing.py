#!/usr/bin/env python3
"""Fast footer-anchor contract check. Not a raster, touch or hardware test."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / 'platform/3ds'
SOURCES = {
    'SamplePlayer': 'play', 'Grain': 'grain', 'Osc': 'osc', 'Filter': 'filter',
    'Vca': 'vca', 'Delay': 'delay', 'Fold': 'fold', 'MicIn': 'mic',
    'Recorder': 'recorder', 'Bitcrush': 'bit', 'Chorus': 'chorus',
    'Reverb': 'reverb', 'Stranular': 'stranular', 'Ghost': 'ghost',
    'Ring': 'ring', 'Tilt': 'tilt', 'Noise': 'noise', 'Fm': 'generators',
    'Motor': 'motor', 'Strain': 'strain', 'Shift': 'shift', 'Bird': 'bird',
    'Fract': 'fract', 'Comp': 'comp', 'Mouth': 'mouth', 'Arp': 'arp',
    'Glitch': 'glitch', 'Inst': 'inst',
}


def main():
    enum = (ROOT / 'core/engine/unit.h').read_text().split('enum class UnitKind', 1)[1].split('};', 1)[0]
    kinds = set(re.findall(r'^\s*(\w+)\s*,', enum, re.M))
    assert kinds == set(SOURCES), ('unreviewed module', kinds ^ set(SOURCES))
    paths = [UI / ('ui_module_focus_' + stem + '.cpp') for stem in SOURCES.values()]
    for path in paths:
        text = path.read_text()
        assert 'm::kTextY' in text or 'm::footer(' in text, path
    paths += [UI / 'module_inspector.cpp', UI / 'ui_play_editor.cpp']
    # Only simple text calls: never reinterpret a baseline-based gothic number,
    # an x coordinate, small_press's multi-argument target or an input threshold.
    stray = re.compile(r'(?<![:\w])(?:ui::text|text|right|text_right|center)\(\s*[^,()\n]*,\s*(22[89]|23\d)\s*,')
    for path in paths:
        assert not stray.search(path.read_text()), ('literal footer anchor', path)
    style = (UI / 'editor_style.h').read_text()
    assert 'using module_footer_layout::kTextY;' in style
    shared = (UI / 'menu_style.cpp').read_text().split('void footer(', 1)[1].split('\n}', 1)[0]
    assert shared.count('module_footer_layout::kTextY') == 2
    play = (UI / 'ui_play_editor.cpp').read_text()
    assert 'b.y0==layout::foot.y0&&b.y1==layout::foot.y1?m::kTextY:' in play
    rec = (UI / 'ui_module_focus_recorder.cpp').read_text()
    assert 'center(160,m::kTextY,"back",m::ink())' in rec
    subprocess.run(['g++', '-std=c++17', '-I.', '-x', 'c++', '-fsyntax-only', '-'],
                   cwd=ROOT, text=True, check=True, input='''
#include "platform/3ds/module_footer_layout.h"
using namespace seance::platform::ui3ds::module_footer_layout;
static_assert(kTextY == 228 && kTextHeight == 8);
static_assert(kTextY + kTextHeight + kBottomInset == kScreenHeight);
static_assert(kBottomInset == 4 && kLegacyBandTop == 226);
''')
    print('module footer spacing: 28 module sources + inspector/play workflow; anchor 228, inset 4; source/constexpr checks passed')


if __name__ == '__main__':
    main()
