#!/usr/bin/env python3
"""Quick picker / design only. No application changes or performance claim.

Visible filter rail + one-tap scrollable list. No preview/hold/inspector/favorites.
User's existing core/ui/font.cpp 6x8 bitmap for all functional text, at integer
1x/2x only. Existing Gossip remains a small title accent. No external body font.
"""
from dataclasses import dataclass
from datetime import datetime, timezone
from functools import lru_cache
import json
import re

from PIL import Image, ImageDraw, ImageChops, PngImagePlugin
from render_bounce_visual_study import Page, Canvas, COLORS, OUTSIDE, ROOT, sha, protected
from render_module_geometry_study import FONT

OUT = ROOT/'ui_mockups/module_picker_study/quick-list-v4-pixel'
ORIGINAL = ROOT/'ui_mockups/module_picker_study/quick-list-v3'
FONT_SOURCES = ('core/ui/font.cpp', 'core/ui/font.h', 'assets/fonts/Gossip-MedPixel.otf')
SOURCE = {'osc', 'noise', 'fm', 'motor', 'inst', 'play', 'gran', 'mic'}
TONE = {'filt', 'vca', 'fold', 'bit', 'ring', 'tilt', 'strain', 'shift', 'comp', 'mouth'}
TIME = {'dly', 'chor', 'strn', 'rev', 'ghost', 'bird', 'fract', 'glitch'}
ALIASES = {'osc': 'oscillator', 'inst': 'instruments', 'filt': 'filter', 'dly': 'delay',
           'rec': 'recorder', 'bit': 'bitcrush', 'chor': 'chorus', 'rev': 'reverb',
           'comp': 'compressor', 'arp': 'arpeggiator'}
GROUP_ORDER = ('sources', 'tone', 'time', 'tools')
INPUTS = [
    'tools/render_module_quick_picker_study.py', 'tools/render_bounce_visual_study.py',
    'tools/render_module_geometry_study.py', 'tools/render_menu_style_study.py',
    'tools/generate_start_menu_labels.py', 'core/ui/font.cpp', 'core/ui/font.h',
    'assets/fonts/Gossip-MedPixel.otf', 'platform/3ds/ui_palette.h',
    'platform/3ds/ui_picker.cpp', 'platform/3ds/menu_style.cpp', 'core/ui/draw.h',
]
READY = ('release/module-usability/seance.3dsx', 'release/sample-load-speed/seance.3dsx')


@dataclass(frozen=True)
class Entry:
    id: str
    short: str
    name: str
    group: str
    user: bool = False


def catalogue():
    text = (ROOT/'platform/3ds/menu_style.cpp').read_text().split('const char* module_name(UnitKind kind)', 1)[1].split(
        'const char* module_description(UnitKind kind)', 1)[0]
    names = dict(re.findall(r'case UnitKind::(\w+):\s+return "([^"]+)";', text))
    picker = (ROOT/'platform/3ds/ui_picker.cpp').read_text().split('PICKER_UNITS[] = {', 1)[1].split('};', 1)[0]
    entries = []
    for kind in re.findall(r'UnitKind::(\w+)', picker):
        short = names[kind]
        group = 'sources' if short in SOURCE else 'tone' if short in TONE else 'time' if short in TIME else 'tools' if short in ('arp', 'rec') else 'other'
        entries.append(Entry('builtin.'+kind, short, ALIASES.get(short, short), group))
    assert len(entries) == len({e.id for e in entries}) == len({e.short for e in entries})
    return sorted(entries, key=lambda e: (e.name, e.id))


BUILTINS = catalogue()
CUSTOM = sorted([*BUILTINS,
    Entry('fiction.velvet', 'velvet echo', 'velvet echo', 'time', True),
    Entry('fiction.paper', 'paper room', 'paper room', 'time', True),
    Entry('fiction.spectral', 'spectral companion', 'spectral companion extended', 'spectral', True),
], key=lambda e: (e.name, e.id))


def intersects(a, b):
    return max(a[0], b[0]) < min(a[2], b[2]) and max(a[1], b[1]) < min(a[3], b[3])


def inside(a, b):
    return b[0] <= a[0] < a[2] <= b[2] and b[1] <= a[1] < a[3] <= b[3]


@lru_cache(None)
def plain_mask(text, size=16, medium=False):
    # Preserve the exact donor bitmaps/advance. No AA, synthetic weight or
    # fractional scaling. The prior size hints now select integer 1x or 2x.
    assert text and all(32 <= ord(ch) < 128 for ch in text)
    scale = 2 if size >= 16 else 1
    image = Image.new('L', (len(text)*6*scale, 8*scale))
    d = ImageDraw.Draw(image)
    for i, ch in enumerate(text):
        for row, bits in enumerate(FONT[ord(ch)-32]):
            for col in range(6):
                if bits & (1 << (5-col)):
                    x, y = (i*6+col)*scale, row*scale
                    d.rectangle((x, y, x+scale-1, y+scale-1), fill=255)
    return image, -8*scale


def fit(text, width, size=16, medium=False):
    if plain_mask(text, size, medium)[0].width <= width:
        return text
    while text and plain_mask(text+'...', size, medium)[0].width > width:
        text = text[:-1]
    assert text
    return text.rstrip()+'...'


class Sheet(Page):
    def plain(self, x, baseline, text, size=16, role='ink', medium=False, align='left'):
        assert text == text.lower()
        mask, dy = plain_mask(text, size, medium)
        if align == 'right':
            x -= mask.width
        elif align == 'center':
            x -= mask.width//2
        box = (x, baseline+dy, x+mask.width, baseline+dy+mask.height)
        assert inside(box, (0, 0, self.im.width, 240)), (text, box)
        assert all(not intersects(box, old) for old, _ in self.labels), (text, box, self.labels)
        self.labels.append((box, text))
        self.im.paste(self.c[role], box[:2], mask)
        return box


def filters(entries):
    available = {e.group for e in entries}
    categories = [g for g in GROUP_ORDER if g in available]
    categories += sorted(available-set(categories))
    return ['all', *categories, 'user']


def subset(entries, group):
    return [e for e in entries if group == 'all' or (group == 'user' and e.user) or e.group == group]


def page(theme='graphite', entries=BUILTINS, group='all', selected_id=None,
         module_start=None, filter_start=None, replace=False):
    p = Sheet(320, theme)
    options = filters(entries)
    assert group in options
    view = subset(entries, group)
    focused = None
    if view:
        ids = [e.id for e in view]
        assert len(ids) == len(set(ids))
        focused = ids.index(selected_id) if selected_id in ids else 0
    if module_start is None:
        module_start = max(0, min((focused or 0)-3, len(view)-7))
    module_start = max(0, min(module_start, max(0, len(view)-7)))
    active_filter = options.index(group)
    if filter_start is None:
        filter_start = max(0, min(active_filter-3, len(options)-7))
    filter_start = max(0, min(filter_start, max(0, len(options)-7)))
    p.word(14, 27, 'replace' if replace else 'modules', 22, 'muted')
    p.plain(112, 26, fit('all modules' if group == 'all' else group, 142, 12), 12, 'muted')
    p.plain(284, 26, str(len(view)), 12, 'muted', align='right')
    p.line(12, 37, 307, 37)
    p.line(96, 44, 96, 211)
    filter_rows = []
    tint = (49, 44, 59) if theme == 'graphite' else (214, 177, 201)
    for row, name in enumerate(options[filter_start:filter_start+7]):
        y = 44+row*24
        box = (8, y, 92, y+24)
        active = name == group
        if active:
            p.d.rectangle((8, y+1, 87, y+22), fill=tint)
            p.line(8, y+4, 8, y+19, 'accent', 2)
        label = p.plain(18, y+17, fit(name, 68, 13, active), 13,
                       'accent' if active else 'muted', active)
        assert inside(label, box)
        p.hit('filter:'+name, box)
        filter_rows.append((name, box))
    if len(options) > 7:
        # This is a second scrollable viewport, not a fixed seven-category cap.
        thumb_y = 46+round(140*filter_start/max(1, len(options)-7))
        p.line(93, 46, 93, 209)
        p.line(93, thumb_y, 93, min(209, thumb_y+20), 'muted')
    rows = []
    if not view:
        p.plain(112, 99, 'no modules here', 16, 'muted')
        p.plain(112, 122, 'choose another filter', 11, 'muted')
    for row, entry in enumerate(view[module_start:module_start+7]):
        y = 44+row*24
        box = (104, y, 288, y+24)
        active = module_start+row == focused
        if active:
            p.line(104, y+5, 104, y+18, 'accent', 2)
        label = p.plain(112, y+18, fit(entry.name, 169, 16, active), 16,
                       'accent' if active else 'ink', active)
        assert inside(label, box), (entry, label, box)
        p.hit(entry.id, box)
        rows.append((entry.id, box))
    if len(view) > 7:
        p.line(306, 47, 306, 208)
        h = max(8, round(162*7/len(view)))
        y = 47+round((162-h)*module_start/(len(view)-7))
        p.d.rectangle((305, y, 307, y+h-1), fill=p.c['muted'])
        p.hit('module scrollbar', (288, 44, 320, 212))
    p.line(12, 215, 307, 215)
    p.plain(14, 232, 'b close', 12, 'muted')
    p.hit('close', (4, 216, 92, 240))
    p.plain(112, 232, 'choose a filter' if not view else 'tap / a replace' if replace else 'tap / a add', 12, 'muted')
    p.plain(306, 232, ('0' if not view else str(module_start+1)+'-'+str(min(module_start+7, len(view))))+'/'+str(len(view)),
           10, 'muted', align='right')
    return p, dict(labels=p.labels, targets=p.hits, group=group, count=len(view),
                   filter_rows=filter_rows, module_rows=rows, module_start=module_start,
                   filter_start=filter_start, selected_id=view[focused].id if focused is not None else None)


def board(theme, cells, title='quick picker / one layout'):
    c = Canvas(24+len(cells)*344, 360, OUTSIDE)
    c.text(24, 15, title, COLORS['graphite']['ink'], scale=2)
    c.text(24, 43, 'visible filters / one-tap entries / no preview', COLORS['graphite']['muted'])
    for i, (label, p) in enumerate(cells):
        x = 24+i*344
        c.text(x, 76, label, COLORS['graphite']['accent'])
        c.im.paste(p.im, (x, 96))
    c.text(24, 346, 'seance / design only / native lcd x2 / not implemented', COLORS['graphite']['muted'])
    return c.im


def save(image, name):
    path = OUT/name
    assert not path.exists()
    big = image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    meta.add_text('fonts', 'exact current core/ui/font.cpp 6x8 bitmaps at integer 1x/2x, no antialias; existing Gossip22 small page title only')
    big.save(path, pnginfo=meta)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes() == big.tobytes()
        assert decoded.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    return dict(path=str(path.relative_to(ROOT)), size=list(big.size), bytes=path.stat().st_size, sha256=sha(path))


def verify_font():
    checked = 0
    for scale in (1, 2):
        for code in range(32, 128):
            mask, dy = plain_mask(chr(code), 8*scale)
            assert mask.size == (6*scale, 8*scale) and dy == -8*scale
            assert set(mask.tobytes()) <= {0, 255}
            for y in range(8*scale):
                for x in range(6*scale):
                    expected = 255 if FONT[code-32][y//scale] & (1 << (5-x//scale)) else 0
                    assert mask.getpixel((x, y)) == expected
            checked += 1
    return checked


def verify_font_only(pages, layouts):
    old = json.loads((ORIGINAL/'proposal.json').read_text())
    surfaces = {}
    for theme in COLORS:
        with Image.open(ORIGINAL/('quick-picker-flow-'+theme+'-2x.png')) as image:
            native = image.convert('RGB').resize((712, 360), Image.Resampling.NEAREST)
        for i, mode in enumerate(('all', 'time')):
            x = 24+i*344
            surfaces[theme+'/'+mode] = native.crop((x, 96, x+320, 336))
    with Image.open(ORIGINAL/'quick-picker-extension-2x.png') as image:
        native = image.convert('RGB').resize((1056, 360), Image.Resampling.NEAREST)
    for i, key in enumerate(('user', 'new-category', 'empty')):
        x = 24+i*344
        surfaces[key] = native.crop((x, 96, x+320, 336))
    for key, page_image in surfaces.items():
        previous, current = old['layouts'][key], layouts[key]
        # The list/filter geometry, counts, identities and input regions are exact.
        for field in ('targets', 'group', 'count', 'filter_rows', 'module_rows',
                      'module_start', 'filter_start', 'selected_id'):
            assert previous[field] == json.loads(json.dumps(current[field])), (key, field)
        allowed = Image.new('L', (320, 240))
        draw = ImageDraw.Draw(allowed)
        for box, _ in previous['labels']+current['labels']:
            draw.rectangle((box[0], box[1], box[2]-1, box[3]-1), fill=255)
        difference = ImageChops.difference(page_image, pages[key].im)
        difference.paste((0, 0, 0), mask=allowed)
        assert difference.getbbox() is None, 'non-font pixel changed: '+key
        assert page_image.crop((0, 0, 96, 37)).tobytes() == pages[key].im.crop((0, 0, 96, 37)).tobytes(), 'gothic title changed'
    return len(surfaces)


def main():
    assert not OUT.exists(), 'preserve previous proposals; choose a fresh revision'
    before = protected(); before['Makefile'] = sha(ROOT/'Makefile')
    inputs = {n: sha(ROOT/n) for n in INPUTS}
    fonts = {n: sha(ROOT/n) for n in FONT_SOURCES}
    glyph_checks = verify_font()
    ready = {n: sha(ROOT/n) for n in READY}
    prior = {str(p.relative_to(ROOT)): sha(p) for p in OUT.parent.rglob('*') if p.is_file()}
    cases = 0
    for theme in COLORS:
        for entry in BUILTINS:
            for group in ('all', entry.group):
                _, layout = page(theme, group=group, selected_id=entry.id)
                assert entry.id in dict(layout['module_rows'])
                cases += 1
        for group in filters(CUSTOM):
            page(theme, CUSTOM, group); cases += 1
        for entry in (e for e in CUSTOM if e.user):
            page(theme, CUSTOM, 'user', entry.id); cases += 1
        page(theme, group='user'); page(theme, replace=True); cases += 2
    sizes = []
    for n in (0, 1, 6, 7, 8, 28, 31, 150, 300):
        entries = [Entry('probe.'+str(i), 'module '+str(i), 'module '+str(i), 'group '+str(i%12), i%2 == 0) for i in range(n)]
        for at in sorted({0, n//2, max(0, n-1)}):
            entry = entries[at] if entries else None
            for group in ('all', entry.group if entry else 'user'):
                _, layout = page('graphite', entries, group, entry.id if entry else None)
                assert len(layout['module_rows']) <= 7 and len(layout['filter_rows']) <= 7
                if entry and group != 'user':
                    assert entry.id in dict(layout['module_rows'])
                sizes.append((n, at, group)); cases += 1
    # Same label is not an identity, even though author disambiguation needs UI design.
    duplicate = [Entry('one', 'echo', 'echo', 'time', True), Entry('two', 'echo', 'echo', 'time', True)]
    page('graphite', duplicate, 'user', 'two'); cases += 1
    OUT.mkdir(parents=True)
    outputs = []; layouts = {}; pages = {}
    for theme in COLORS:
        normal, layouts[theme+'/all'] = page(theme, selected_id='builtin.Filter')
        filtered, layouts[theme+'/time'] = page(theme, group='time', selected_id='builtin.Ghost')
        pages[theme+'/all'], pages[theme+'/time'] = normal, filtered
        outputs.append(save(normal.im, 'quick-picker-'+theme+'-2x.png'))
        outputs.append(save(board(theme, [('all / 28 modules', normal), ('tap time / 8 modules', filtered)]),
                            'quick-picker-flow-'+theme+'-2x.png'))
    user, layouts['user'] = page('graphite', CUSTOM, 'user', 'fiction.velvet')
    empty, layouts['empty'] = page('graphite', group='user')
    extra, layouts['new-category'] = page('graphite', CUSTOM, 'spectral', 'fiction.spectral')
    pages['user'], pages['empty'], pages['new-category'] = user, empty, extra
    font_only_cases = verify_font_only(pages, layouts)
    outputs.append(save(board('graphite', [('user / fictional examples', user), ('new metadata category', extra), ('empty user filter', empty)],
                              'same layout / growing catalogue'), 'quick-picker-extension-2x.png'))
    after = protected(); after['Makefile'] = sha(ROOT/'Makefile')
    assert before == after
    assert inputs == {n: sha(ROOT/n) for n in INPUTS}
    assert fonts == {n: sha(ROOT/n) for n in FONT_SOURCES}
    assert ready == {n: sha(ROOT/n) for n in READY}
    assert all(sha(ROOT/n) == h for n, h in prior.items())
    report = dict(created_at=datetime.now(timezone.utc).isoformat(), status='unapproved visual proposal only',
        origin=__doc__, fonts_sha256=fonts, source_sha256=inputs, protected_sha256=before,
        prior_sha256=prior, ready_sha256=ready, outputs=outputs, layouts=layouts,
        display_aliases=ALIASES, catalogue=[e.__dict__ for e in BUILTINS],
        fictional_user_entries=[e.__dict__ for e in CUSTOM if e.user],
        validation=dict(native_layout_cases=cases, size_probes=sizes, max_visible_modules=7,
            module_target=[184,24], filter_target=[84,24], labels_and_targets_bounded=True,
            labels_and_targets_nonoverlapping=True, nearest2x_exact_decode=True,
            exact_donor_glyphs_scales=glyph_checks, font_only_revision_cases=font_only_cases),
        proposed_interaction=dict(
            filter='always visible left rail; one tap changes filter, never adds a module',
            module='valid physical lift adds immediately and closes catalogue; A adds keyboard focus',
            touch='no hover, hold threshold, inspect mode or second confirmation; drag cancels add until next down',
            scroll='module list and filter rail each scroll their own viewport; focus follows stable ID',
            keyboard='up/down in current column; left/right switches column; A applies filter or adds module; B closes',
            reopen='remember filter, scroll and focus for session; do not live-resort during input',
            upper='keep existing frozen chain/destination context; no upper inspector or thumbnail',
            data='no preview decode, waveform scan, module construction or DSP on focus; creation only at commit'),
        limits=[
            'not runtime, input test, performance measurement or on-device readability proof',
            'body pixels come from existing 6x8 donor font; application font sources/assets unchanged',
            'integer 2x names and 1x metadata use existing font capability; no imported system fonts or runtime TTF loading',
            'display aliases only; stable UnitKind/short names/serialized data/other screens are unchanged',
            'grouping is navigation metadata, not a DSP classification; future categories and user modules not implemented',
            'long row names are ellipsized; full-name/duplicate-author identification without hover remains to finalize',
            'existing bitmap font is ASCII-only; arbitrary Unicode and malformed metadata policy remains to design',
            'static current paper is not a screenshot or replacement recipe for existing frozen dual backdrop',
            'seven rows are viewport capacity, never total registry capacity; two panes can scroll independently',
            'no favorites, tooltips, screenshot cache, plugin ABI, loader, application build, FTP, CIA or user-data changes',
        ])
    (OUT/'proposal.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    for output in outputs:
        print(output['path'], output['size'], output['sha256'])
    print('pass:', cases, 'native layout cases;', len(BUILTINS), 'built-ins;', len(before), 'production/assets/Makefile unchanged;', len(prior), 'previous proposal files unchanged')


if __name__ == '__main__':
    main()
