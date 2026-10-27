#!/usr/bin/env python3
"""Authored module-picker proposals, NOT C++ or hardware screenshots.

Native 400x240 / 320x240, current Gossip 22/28/40 and native 6x8.
Static current paper, not a capture of the existing frozen dual-screen blur.
No module construction, DSP, sample I/O, application build or deployment.
"""
from datetime import datetime, timezone
import json
import re

from PIL import Image, PngImagePlugin
from render_bounce_visual_study import Page, Canvas, COLORS, OUTSIDE, ROOT, sha, protected, dual

OUT = ROOT / 'ui_mockups/module_picker_study'
# Familiar shipping abbreviations; rows are spatial neighbours, not new DSP types.
ROWS = (
    ('osc', 'fm', 'noise', 'inst'),
    ('play', 'gran', 'motor', 'mic'),
    ('filt', 'vca', 'comp', 'tilt'),
    ('fold', 'bit', 'ring', 'mouth'),
    ('strain', 'shift', 'bird', 'arp'),
    ('dly', 'chor', 'rev', 'ghost'),
    ('strn', 'fract', 'glitch', 'rec'),
)
FAMILIES = {
    'source': ('osc', 'fm', 'noise', 'inst', 'play', 'gran', 'motor', 'mic'),
    'shape': ('filt', 'vca', 'comp', 'tilt', 'fold', 'bit', 'ring', 'mouth', 'strain', 'shift'),
    'time': ('dly', 'chor', 'rev', 'ghost', 'strn', 'bird', 'fract', 'glitch', 'arp', 'rec'),
}
# Navigation shelves only: e.g. recorder/arp in time are NOT audio effects.
ALL = tuple(label for row in ROWS for label in row)
INPUTS = (
    'tools/render_module_picker_study.py', 'tools/render_bounce_visual_study.py',
    'tools/render_module_geometry_study.py', 'tools/render_menu_style_study.py',
    'tools/generate_start_menu_labels.py', 'core/ui/font.cpp',
    'assets/fonts/Gossip-MedPixel.otf', 'platform/3ds/ui_palette.h',
    'platform/3ds/ui_picker.cpp', 'platform/3ds/ui_picker.h',
    'platform/3ds/menu_style.cpp', 'platform/3ds/ui_chains.cpp',
)
READY = ('release/module-usability/seance.3dsx', 'release/sample-load-speed/seance.3dsx')


def catalogue():
    source = (ROOT / 'platform/3ds/menu_style.cpp').read_text()
    names, descriptions = source.split('const char* module_name(UnitKind kind)', 1)[1].split(
        'const char* module_description(UnitKind kind)', 1)
    rx = r'case UnitKind::(\w+):\s+return "([^"]+)";'
    names, descriptions = dict(re.findall(rx, names)), dict(re.findall(rx, descriptions))
    picker = (ROOT / 'platform/3ds/ui_picker.cpp').read_text().split('PICKER_UNITS[] = {', 1)[1].split('};', 1)[0]
    kinds = re.findall(r'UnitKind::(\w+)', picker)
    assert len(kinds) == len(set(kinds)) == len(ALL) == len(set(ALL)) == 28
    assert {names[k] for k in kinds} == set(ALL)
    groups = tuple(n for values in FAMILIES.values() for n in values)
    assert len(groups) == len(set(groups)) == 28 and set(groups) == set(ALL)
    return {names[k]: descriptions[k] for k in kinds}


DESCRIPTIONS = catalogue()


def inside(a, b):
    return b[0] <= a[0] < a[2] <= b[2] and b[1] <= a[1] < a[3] <= b[3]


def word_target(p, label, box, selected=False, size=22):
    x0, y0, x1, y1 = box
    ink = p.word(x0+14, y0+(17 if size == 22 else 21), label, size,
                 'accent' if selected else 'ink')
    assert inside(ink, box), (label, ink, box)
    if selected:
        p.diamond(x0+5, (y0+y1)//2, filled=True)
    p.hit(label, box)


def header(p, replace=False):
    p.word(18, 25, 'replace' if replace else 'modules', 22)
    p.txt(304, 15, 'ch01 / 02' if replace else 'ch01 / 03', 'muted', 'right')


def footer(p, replace=False):
    p.txt(16, 227, 'tap replace / a' if replace else 'tap add / a')
    p.txt(304, 227, 'b close', 'muted', 'right')
    p.hit('close', (244, 216, 312, 240))


def index_page(theme, selected='ghost', replace=False):
    p = Page(320, theme)
    header(p, replace)
    for r, row in enumerate(ROWS):
        y = 40+r*24+(4 if r >= 2 else 0)
        for c, label in enumerate(row):
            x = 8+c*76
            word_target(p, label, (x, y, x+72, y+24), selected == label)
    # One quiet source/processing break; no boxes or invented category icons.
    p.line(22, 89, 298, 89, 'rule')
    footer(p, replace)
    return p


def family_page(theme, family='time', selected='ghost', replace=False):
    assert selected in FAMILIES[family]
    p = Page(320, theme)
    header(p, replace)
    for i, label in enumerate(FAMILIES):
        x = 8+i*102
        box = (x, 34, x+100, 64)
        ink = p.word(x+50, 55, label, 22,
                     'accent' if label == family else 'muted', 'center')
        assert inside(ink, box)
        p.hit('family '+label, box)
        if label == family:
            p.line(x+37, 61, x+62, 61, 'accent')
    for i, label in enumerate(FAMILIES[family]):
        r, c = divmod(i, 2)
        x, y = 8+c*156, 72+r*28
        word_target(p, label, (x, y, x+148, y+28), selected == label, 28)
    footer(p, replace)
    return p


def upper(theme, selected='ghost', replace=False):
    p = Page(400, theme)
    p.word(20, 37, 'replace module' if replace else 'add module', 28)
    p.txt(380, 22, 'ch01 / 02' if replace else 'ch01 / 03', 'muted', 'right')
    p.word(200, 108, selected, 40, 'accent', 'center')
    p.txt(200, 128, DESCRIPTIONS[selected], 'ink', 'center')
    p.txt(200, 172, 'replaces filt' if replace else 'after filt / end of chain', 'muted', 'center')
    p.txt(200, 195, 'play > '+selected if replace else 'play > filt > '+selected, 'muted', 'center')
    p.txt(20, 226, 'a replace / b cancel' if replace else 'a add / b cancel')
    p.txt(380, 226, 'not added yet', 'muted', 'right')
    return p


def board(title, subtitle, cells):
    width = 24+len(cells)*344
    c = Canvas(width, 360, OUTSIDE)
    c.text(24, 15, title, COLORS['graphite']['ink'], scale=2)
    c.text(24, 43, subtitle, COLORS['graphite']['muted'])
    for i, (label, p) in enumerate(cells):
        x = 24+i*344
        c.text(x, 76, label, COLORS['graphite']['accent'])
        c.im.paste(p.im, (x, 96))
    c.text(24, 346, 'seance / design only / static paper, not runtime blur', COLORS['graphite']['muted'])
    return c.im


def save(image, name):
    path = OUT / name
    assert not path.exists(), 'preserve previous proposal: '+str(path)
    out = image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    out.save(path, pnginfo=meta)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes() == out.tobytes()
        assert decoded.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    return dict(path=str(path.relative_to(ROOT)), size=list(out.size), bytes=path.stat().st_size, sha256=sha(path))


def main():
    assert not OUT.exists(), 'choose a new output directory to preserve the previous study'
    before = protected()
    before['Makefile'] = sha(ROOT / 'Makefile')
    ready = {s: sha(ROOT/s) for s in READY}
    inputs = {s: sha(ROOT/s) for s in INPUTS}
    cases = 0
    for theme in COLORS:
        for label in ALL:
            index_page(theme, label)
            family = next(k for k, v in FAMILIES.items() if label in v)
            family_page(theme, family, label)
            upper(theme, label)
            cases += 3
        for fn in (index_page, family_page, upper):
            fn(theme, replace=True)
            cases += 1
    OUT.mkdir(parents=True)
    outputs = []
    layouts = {}
    for theme in COLORS:
        flat = index_page(theme)
        families = family_page(theme)
        outputs.append(save(board('module picker / two directions',
            '01 all 28, one tap / 02 three shelves, larger words',
            [('01 / fixed word index', flat), ('02 / source, shape, time', families)]),
            'picker-directions-'+theme+'-2x.png'))
        outputs.append(save(dual(upper(theme), flat), 'picker-index-'+theme+'-dual-2x.png'))
        layouts[theme+'/index'] = dict(labels=flat.labels, targets=flat.hits)
        layouts[theme+'/family'] = dict(labels=families.labels, targets=families.hits)
    outputs.append(save(board('02 / all three shelves',
        '8 sources / 10 shape / 10 time, memory and utilities',
        [(k, family_page('graphite', k, FAMILIES[k][0])) for k in FAMILIES]),
        'picker-families-graphite-2x.png'))
    after = protected()
    after['Makefile'] = sha(ROOT / 'Makefile')
    assert before == after, 'production/assets changed'
    assert ready == {s: sha(ROOT/s) for s in READY}, 'ready artifacts changed'
    assert inputs == {s: sha(ROOT/s) for s in INPUTS}, 'render inputs changed'
    report = dict(created_at=datetime.now(timezone.utc).isoformat(),
        status='unselected design proposals; recommendation is 01, not user approval',
        origin=__doc__, source_sha256=inputs, protected_sha256=before,
        ready_artifact_sha256=ready, outputs=outputs, layouts=layouts,
        index_rows=ROWS, families=FAMILIES, descriptions=DESCRIPTIONS,
        interaction_proposed=dict(
            index='fixed 4x7 words; one tap from open picker; no scrolling, sorting or extra confirm',
            family='one tap in visible shelf, two if changing shelf; no nested submenus or list scrolling',
            focus='d-pad geometry and a commit; b/touch close; touch-down updates upper, valid physical lift commits',
            cancel='leaving original target or capture/context loss cancels until next down; no slide-to-add',
            reopen='keep last keyboard cursor for session; do not reorder words; no new patch setting',
            return_to='existing chain after placement; do not automatically open editor',
            performance='draw labels from immutable masks; no allocation or module creation on focus; preserve existing frozen backdrop mechanism'),
        validation=dict(native_layout_cases=cases, exact_current_catalogue=28,
            labels_bounded_nonoverlapping=True, labels_inside_targets=True,
            proposed_targets_bounded_nonoverlapping=True, min_target_size=[32, 24],
            index_target_size=[72, 24], family_target_size=[148, 28],
            png_nn2x_lossless_decode=True, unchanged_production_and_makefile=len(before)),
        limits=[
            'authored static paper is not a capture or replacement recipe for current frozen dual-screen blur',
            'mock chain play > filt is illustrative, not user project data; focused candidate is not installed',
            'families are navigation shelves, not source/effect semantics; time includes arp and recorder',
            'no input implementation, runtime timing, hardware readability or touch acceptance measured',
            'instant visual access is not a promise that constructing heavy modules or recorder staging has zero latency',
            'no application, core, shared assets, makefile, DSP, storage or ready binary changes',
            'no application build, C++ capture, audio export, FTP, CIA or device launch',
            'future implementation must preserve target/engine/topology ownership, true-lift quarantine, PLAY normal default and REC staging/error safety',
        ])
    (OUT/'proposal.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    for out in outputs:
        print(out['path'], out['size'], out['sha256'])
    print('pass:', cases, 'native layout cases;', len(before), 'production/assets/Makefile unchanged; 2 ready artifacts unchanged')


if __name__ == '__main__':
    main()
