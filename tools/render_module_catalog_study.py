#!/usr/bin/env python3
"""Expandable module catalogue / inspection study, authored Pillow, NOT runtime.

Four proposals: pointer hint, pointer thumbnail, inline row, upper inspector.
Current Gossip 22/28/40 + native 6x8; static paper, not existing frozen blur.
Thumbnail is a verified ARCHIVED C++ host capture, scaled for recognition only.
User-module metadata is fictional, not a plugin ABI or implemented loader.
"""
from dataclasses import dataclass
from datetime import datetime, timezone
from functools import lru_cache
import json
import re
import textwrap

from PIL import Image, PngImagePlugin
from render_bounce_visual_study import Page, Canvas, COLORS, ROOT, OUTSIDE, glyph, sha, protected, dual

OUT = ROOT / 'ui_mockups/module_picker_study/catalog-hover-v2'
ARCHIVE = ROOT / 'ui_mockups/filter_interaction_runtime/raw-final'
VARIANTS = ('hint', 'thumbnail', 'inline', 'upper')
TITLES = {'hint': '01 / nearby hint', 'thumbnail': '02 / nearby screenshot',
          'inline': '03 / open row', 'upper': '04 / upper-screen inspector'}
SOURCE = {'osc', 'fm', 'noise', 'inst', 'play', 'gran', 'motor', 'mic'}
SHAPE = {'filt', 'vca', 'comp', 'tilt', 'fold', 'bit', 'ring', 'mouth', 'strain', 'shift'}
INPUTS = [
    'tools/render_module_catalog_study.py', 'tools/render_bounce_visual_study.py',
    'tools/render_module_geometry_study.py', 'tools/render_menu_style_study.py',
    'tools/generate_start_menu_labels.py', 'assets/fonts/Gossip-MedPixel.otf',
    'core/ui/font.cpp', 'platform/3ds/ui_palette.h', 'platform/3ds/menu_style.cpp',
    'platform/3ds/ui_picker.cpp', 'platform/3ds/ui_picker.h', 'platform/3ds/main.cpp',
    'ui_mockups/filter_interaction_runtime/raw-final/capture.json',
    'ui_mockups/filter_interaction_runtime/raw-final/graphite_low_bottom.ppm',
    'ui_mockups/filter_interaction_runtime/raw-final/pink_low_bottom.ppm',
]
READY = ('release/module-usability/seance.3dsx', 'release/sample-load-speed/seance.3dsx')


@dataclass(frozen=True)
class Entry:
    id: str
    name: str
    description: str
    category: str
    author: str = 'seance'
    user: bool = False
    thumbnail: bool = False


def catalogue():
    source = (ROOT/'platform/3ds/menu_style.cpp').read_text()
    names, descriptions = source.split('const char* module_name(UnitKind kind)', 1)[1].split(
        'const char* module_description(UnitKind kind)', 1)
    rx = r'case UnitKind::(\w+):\s+return "([^"]+)";'
    names, descriptions = dict(re.findall(rx, names)), dict(re.findall(rx, descriptions))
    picker = (ROOT/'platform/3ds/ui_picker.cpp').read_text().split('PICKER_UNITS[] = {', 1)[1].split('};', 1)[0]
    kinds = re.findall(r'UnitKind::(\w+)', picker)
    result = []
    for kind in kinds:
        name = names[kind]
        category = 'source' if name in SOURCE else 'shape' if name in SHAPE else 'control' if name == 'arp' else 'utility' if name == 'rec' else 'time'
        result.append(Entry('builtin.'+kind, name, descriptions[kind], category, thumbnail=name == 'filt'))
    assert len(result) == len({e.id for e in result}) == len({e.name for e in result})
    return sorted(result, key=lambda e: (e.name, e.id))


BUILTINS = catalogue()
USER = Entry('demo.velvet-echo', 'velvet echo', 'soft repeats with drifting pitch', 'time', 'example author', True)
LONG = Entry('demo.long', 'spectral companion with a deliberately long title',
             'a fictional user module with extended descriptive metadata and no supplied preview image',
             'spectral', 'example author', True)
CUSTOM = sorted([*BUILTINS, USER, LONG, Entry('demo.room', 'paper room', 'small resonant room', 'time', 'example author', True)], key=lambda e: (e.name, e.id))
FILT = next(e for e in BUILTINS if e.name == 'filt')


def overlaps(a, b):
    return max(a[0], b[0]) < min(a[2], b[2]) and max(a[1], b[1]) < min(a[3], b[3])


def inside(a, b):
    return b[0] <= a[0] < a[2] <= b[2] and b[1] <= a[1] < a[3] <= b[3]


def fit_word(text, width, size):
    if glyph(text, size)[0].width <= width:
        return text
    while text and glyph(text+'...', size)[0].width > width:
        text = text[:-1]
    assert text
    return text.rstrip()+'...'


def wrap(text, columns, lines):
    values = textwrap.wrap(text, width=columns, break_long_words=True, break_on_hyphens=False)
    if len(values) > lines:
        values = values[:lines]
        values[-1] = values[-1][:columns-3].rstrip()+'...'
    return values


def paragraph(p, x, y, text, columns, lines, role='muted', pitch=13):
    values = wrap(text, columns, lines)
    for i, value in enumerate(values):
        p.txt(x, y+i*pitch, value, role)
    return y+len(values)*pitch


@lru_cache(None)
def thumbnail(theme):
    receipt = json.loads((ARCHIVE/'capture.json').read_text())
    path = ARCHIVE/(theme+'_low_bottom.ppm')
    assert sha(path) == receipt['surface_sha256'][path.name], 'archived surface changed'
    image = Image.open(path).convert('RGB')
    assert image.size == (320, 240)
    return image


def preview(p, entry, box):
    x, y, x1, y1 = box
    assert 0 <= x < x1 <= p.im.width and 0 <= y < y1 <= 240
    if entry.thumbnail:
        # Area reduction keeps thin curve lines that nearest decimation drops.
        # The final LCD-sized composition is still enlarged with exact nearest2x.
        im = thumbnail(p.theme).resize((x1-x, y1-y), Image.Resampling.BOX)
        p.im.paste(im, (x, y))
        # One archived image rectangle, not a set of live interactive controls.
        p.labels.append((box, 'archived filter preview'))
        return 'archived filter host capture, area reduced for thumbnail'
    p.line(x, y, x1-1, y)
    p.line(x, y1-1, x1-1, y1-1)
    p.word((x+x1)//2, y+(y1-y)//2-2, 'no image', 28, 'muted', 'center')
    p.txt((x+x1)//2, y+(y1-y)//2+15, 'description still available', 'muted', 'center')
    return 'explicit no-image fallback; no fake screenshot'


def context_top(theme, entry):
    p = Page(400, theme)
    p.word(20, 37, 'add module', 28)
    p.txt(380, 22, 'ch01 / 03', 'muted', 'right')
    p.txt(20, 70, 'destination', 'muted')
    p.word(20, 117, 'play', 28)
    p.txt(109, 105, '>')
    p.word(144, 117, 'ghost', 28)
    p.txt(256, 105, '>')
    p.word(296, 117, 'new', 28, 'accent')
    p.txt(20, 143, 'append after ghost')
    p.line(20, 180, 379, 180)
    p.txt(20, 196, 'inspect on the lower screen')
    p.txt(20, 225, 'a add / b close')
    p.txt(380, 225, 'not added yet', 'muted', 'right')
    return p


def inspector_top(theme, entry):
    p = Page(400, theme)
    p.word(16, 33, 'module preview', 28)
    p.txt(384, 19, 'ch01 / 03', 'muted', 'right')
    preview(p, entry, (16, 48, 240, 216))
    if len(entry.name) > 18:
        # Full long name stays available even when the catalogue row truncates.
        y = paragraph(p, 252, 53, entry.name, 22, 3, 'ink', 12)+8
    else:
        p.word(252, 76, fit_word(entry.name, 132, 28), 28, 'accent')
        y = 92
    p.txt(252, y, entry.category+' / '+('user' if entry.user else 'built-in'))
    if entry == FILT:
        lines = ('4 curves + off', 'cutoff / res', 'drive / morph')
        for i, line in enumerate(lines):
            p.txt(252, y+23+i*14, line, 'ink')
    else:
        paragraph(p, 252, y+22, entry.description, 22, 3, 'ink', 12)
    p.txt(252, 180, 'by '+entry.author)
    p.txt(252, 201, 'static image' if entry.thumbnail else 'no preview supplied')
    p.txt(16, 227, 'ch01 / append after ghost')
    p.txt(384, 227, 'not added yet', 'muted', 'right')
    return p


def row_box(y, height=28):
    return (8, y, 280, y+height)


def pointer(p, point, held=True):
    x, y = point
    p.d.ellipse((x-4, y-4, x+4, y+4), outline=p.c['accent'])
    if held:
        p.d.point((x, y), fill=p.c['accent'])


def popup_box(point, label_box, width, height):
    px, py = point
    # Prefer below/right of the contact, flip to the other side when necessary.
    # The selected word and contact remain visible; otherwise use the upper LCD.
    y = max(44, min(212-height, py+12))
    candidates = [(min(px+12, 288-width), y), (px-width-12, y)]
    x = max(8, min(288-width, px+12))
    candidates += [(x, label_box[3]+10), (x, label_box[1]-height-10)]
    contact = (px-8, py-8, px+9, py+9)
    for x, y in candidates:
        box = (x, y, x+width, y+height)
        if inside(box, (8, 40, 288, 214)) and not overlaps(box, label_box) and not overlaps(box, contact):
            return box
    return None


def draw_popup(theme, entry, kind, point, label_box):
    image_mode = kind == 'thumbnail' and entry.thumbnail
    width, height = (176, 156) if image_mode else (178, 82)
    box = popup_box(point, label_box, width, height)
    if box is None:
        return None, None, None
    x, y, x1, y1 = box
    p = Page(320, theme)
    face = (43, 41, 48) if theme == 'graphite' else (233, 201, 215)
    p.d.rectangle((x, y, x1-1, y1-1), fill=face, outline=p.c['rule'])
    p.word(x+10, y+22, fit_word(entry.name, width-(76 if image_mode else 20), 22), 22, 'accent')
    if image_mode:
        p.txt(x1-9, y+12, 'preview', 'muted', 'right')
        preview(p, entry, (x+8, y+28, x+168, y+148))
    else:
        p.txt(x+10, y+31, entry.category+' / '+('user' if entry.user else 'built-in'))
        if entry == FILT:
            p.txt(x+10, y+49, '4 curves + off', 'ink')
            p.txt(x+10, y+63, 'cutoff / res / drive', 'ink')
        else:
            paragraph(p, x+10, y+49, entry.description, 26, 2, 'ink', 13)
    assert all(inside(label, box) for label, _ in p.labels)
    return p.im.crop(box), box, p.labels


def lower(theme, variant, entries=BUILTINS, selected=FILT, start=None,
          inspect=True, point=None, held=True, group='all', scroll=False):
    p = Page(320, theme)
    p.word(16, 26, 'modules', 22)
    p.txt(180, 16, str(len(entries)), 'muted', 'right')
    p.word(204, 26, fit_word(group, 84, 22), 22, 'accent')
    p.txt(300, 17, 'v')
    p.hit('filter', (196, 4, 312, 36))
    p.txt(16, 228, 'choose a filter' if not entries else 'scroll / lift cancels' if scroll else 'tap again / a add' if inspect else 'tap add / hold info')
    p.txt(304, 228, 'b close', 'muted', 'right')
    p.hit('close', (244, 216, 312, 240))
    layers = []
    if not entries:
        p.word(160, 113, 'nothing here', 28, 'muted', 'center')
        p.txt(160, 139, 'try another filter', 'muted', 'center')
        return p, dict(rows=[], popup=None, layers=[], fallback=False, count=0)
    ids = [e.id for e in entries]
    assert len(ids) == len(set(ids)), 'stable IDs, not display names'
    selected_index = ids.index(selected.id)
    if start is None:
        start = max(0, min(selected_index-2, len(entries)-6))
    start = max(0, min(start, max(0, len(entries)-1)))
    if inspect and variant == 'inline':
        # Expanded last rows need an explicit re-anchor; this movement is one
        # disadvantage of inline inspection compared with the fixed upper LCD.
        start = max(start, selected_index-3)
    visible = []
    y = 44
    focus_box = None
    focus_label = None
    for entry in entries[start:]:
        expanded = inspect and variant == 'inline' and entry.id == selected.id
        height = 84 if expanded else 28
        if y+height > 212:
            break
        box = row_box(y, height)
        label = p.word(32, y+21, fit_word(entry.name, 240, 28), 28,
                       'accent' if entry.id == selected.id else 'ink')
        assert inside(label, box)
        p.hit(entry.id, box)
        if entry.id == selected.id:
            p.diamond(16, y+13, filled=True)
            focus_box, focus_label = box, label
        if expanded:
            p.txt(32, y+35, entry.category+' / '+('by '+entry.author if entry.user else 'built-in'))
            paragraph(p, 32, y+52, '4 curves + off / cutoff, resonance, drive, morph' if entry == FILT else entry.description,
                      39, 2, 'ink', 13)
            p.line(32, y+height-2, 271, y+height-2)
        visible.append((entry.id, box))
        y += height
    p.line(307, 47, 307, 208)
    total_height = len(entries)*28+(56 if inspect and variant == 'inline' else 0)
    thumb_height = max(7, min(162, round(162*168/max(168, total_height))))
    thumb_y = 47+round((162-thumb_height)*start/max(1, len(entries)-len(visible)))
    thumb_y = min(209-thumb_height, thumb_y)
    p.d.rectangle((306, thumb_y, 308, thumb_y+thumb_height-1), fill=p.c['muted'])
    p.hit('scrollbar', (288, 44, 320, 212))
    box = None
    fallback = False
    if inspect and variant in ('hint', 'thumbnail') and focus_label:
        if point is None:
            point = (max(100, min(278, focus_label[2]+20)), (focus_box[1]+min(focus_box[3], focus_box[1]+28))//2)
        im, box, labels = draw_popup(theme, selected, variant, point, focus_label)
        if im is not None:
            p.im.paste(im, box[:2])
            layers.append(dict(rect=box, labels=labels, interactive=False))
        else:
            fallback = True
    if held and not scroll and focus_box:
        if point is None:
            point = (max(100, min(278, focus_label[2]+20)), focus_box[1]+14)
        pointer(p, point)
    return p, dict(rows=visible, popup=box, layers=layers, fallback=fallback,
                   count=len(entries), selected_id=selected.id, start=start, labels=p.labels, targets=p.hits)


def scene(theme, variant, entries=BUILTINS, selected=FILT, **kwargs):
    bottom, layout = lower(theme, variant, entries, selected, **kwargs)
    top = inspector_top(theme, selected) if variant == 'upper' or layout['fallback'] else context_top(theme, selected)
    layout['upper_labels'] = top.labels
    return top, bottom, layout


def filter_page(theme, entries=CUSTOM):
    p = Page(320, theme)
    p.word(16, 28, 'browse', 28)
    categories = sorted({e.category for e in entries})
    choices = [('all', len(entries)), ('favorites', 3), ('user', sum(e.user for e in entries))]
    choices += [(name, sum(e.category == name for e in entries)) for name in categories]
    for i, (name, count) in enumerate(choices[:7]):
        y = 42+i*24
        p.word(30, y+17, name, 22, 'accent' if name == 'all' else 'ink')
        p.txt(276, y+8, str(count), 'muted', 'right')
        p.hit('filter '+name, (8, y, 288, y+24))
    p.line(307, 44, 307, 208)
    p.d.rectangle((306, 44, 308, 173), fill=p.c['muted'])
    p.hit('scroll filters', (288, 42, 320, 210))
    p.txt(16, 227, 'categories from metadata')
    p.txt(304, 227, 'b back', 'muted', 'right')
    p.hit('back', (248, 216, 312, 240))
    return p


def board(title, subtitle, cells, cols=2, cell_kind='dual'):
    cw, ch = (400, 496) if cell_kind == 'dual' else (320, 240)
    gap = 24
    rows = (len(cells)+cols-1)//cols
    pitch = ch+66
    c = Canvas(gap+cols*(cw+gap), 74+rows*pitch+10, OUTSIDE)
    c.text(24, 14, title, COLORS['graphite']['ink'], scale=2)
    c.text(24, 42, subtitle, COLORS['graphite']['muted'])
    for i, (label, image, note) in enumerate(cells):
        x, y = gap+(i%cols)*(cw+gap), 78+(i//cols)*pitch
        c.text(x, y, label, COLORS['graphite']['accent'])
        c.im.paste(image, (x, y+20))
        c.text(x, y+ch+32, note, COLORS['graphite']['muted'])
    c.text(24, c.im.height-15, 'design proposal / static paper / not implemented', COLORS['graphite']['muted'])
    return c.im


def save(im, name):
    path = OUT/name
    assert not path.exists(), 'preserve output '+str(path)
    big = im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    meta.add_text('thumbnail', 'archived C++ host filter editor; BOX-area downscaled, final composition exact nearest2x; not device/live interactive preview')
    big.save(path, pnginfo=meta)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes() == big.tobytes()
        assert decoded.resize(im.size, Image.Resampling.NEAREST).tobytes() == im.tobytes()
    return dict(path=str(path.relative_to(ROOT)), size=list(big.size), bytes=path.stat().st_size, sha256=sha(path))


def gesture_frames():
    specs = [
        ('touch down / no preview yet', 350, dict(inspect=False, held=True)),
        ('hold 350ms / inspect only', 1000, dict(inspect=True, held=True)),
        ('lift / still not added', 1000, dict(inspect=True, held=False)),
        ('drag / hide preview, scroll', 800, dict(inspect=False, held=False, scroll=True, start=6)),
        ('lift / ready for a new tap', 650, dict(inspect=False, held=False)),
    ]
    frames, durations = [], []
    for title, ms, kw in specs:
        p, _ = lower('graphite', 'thumbnail', **kw)
        c = Canvas(352, 294, OUTSIDE)
        c.text(16, 10, title, COLORS['graphite']['accent'])
        c.im.paste(p.im, (16, 30))
        c.text(16, 278, 'scripted mock / no module is created', COLORS['graphite']['muted'])
        frames.append(c.im.resize((704, 588), Image.Resampling.NEAREST))
        durations.append(ms)
    return frames, durations


def save_gif(frames, durations):
    colors = sorted(set().union(*(set(im.get_flattened_data()) for im in frames)))
    assert len(colors) <= 256
    values = {c: i for i, c in enumerate(colors)}
    palette = [v for c in colors for v in c]+[0]*(768-len(colors)*3)
    indexed = []
    for im in frames:
        p = Image.new('P', im.size)
        p.putpalette(palette)
        p.putdata([values[c] for c in im.get_flattened_data()])
        assert p.convert('RGB').tobytes() == im.tobytes()
        indexed.append(p)
    path = OUT/'hold-inspect-storyboard-2x.gif'
    assert not path.exists()
    indexed[0].save(path, save_all=True, append_images=indexed[1:], duration=durations,
                    loop=0, disposal=2, optimize=False)
    with Image.open(path) as decoded:
        assert decoded.n_frames == len(frames)
        for i, im in enumerate(frames):
            decoded.seek(i)
            assert decoded.info['duration'] == durations[i]
            assert decoded.convert('RGB').tobytes() == im.tobytes()
    return dict(path=str(path.relative_to(ROOT)), size=list(frames[0].size), bytes=path.stat().st_size,
                sha256=sha(path), frames=len(frames), duration_ms=sum(durations), exact_colors=len(colors))


def main():
    assert not OUT.exists(), 'new revision required; old renders remain untouched'
    before = protected(); before['Makefile'] = sha(ROOT/'Makefile')
    ready = {n: sha(ROOT/n) for n in READY}
    inputs = {n: sha(ROOT/n) for n in INPUTS}
    previous = {str(p.relative_to(ROOT)): sha(p) for p in OUT.parent.iterdir() if p.is_file()}
    layouts = {}; cases = 0
    for theme in COLORS:
        for v in VARIANTS:
            for entry in BUILTINS:
                # Keep a selected item away from bottom when inline expansion is active.
                scene(theme, v, selected=entry)
                cases += 1
            for entry in (USER, LONG):
                scene(theme, v, CUSTOM, entry)
                cases += 1
            lower(theme, v, [], FILT, group='favorites')
            scene(theme, v, inspect=False)
            cases += 2
        filter_page(theme)
        cases += 1
    # Arbitrary catalogue sizes. Only a viewport is drawn; stable IDs disambiguate names.
    scalability = []
    for n in (1, 6, 7, 28, 31, 150, 300):
        entries = [Entry('test.'+str(i), 'module '+str(i), 'test metadata only', 'test') for i in range(n)]
        for at in sorted({0, n//2, n-1}):
            for v in VARIANTS:
                _, _, layout = scene('graphite', v, entries, entries[at])
                assert len(layout['rows']) <= 6
                assert entries[at].id in [key for key, _ in layout['rows']], (n, at, v)
                scalability.append((n, at, v)); cases += 1
    # Popovers stay inside native safe area or deliberately fall back to top screen.
    placements = []
    for kind in ('hint', 'thumbnail'):
        for x in (16, 100, 160, 280, 309):
            for y in (50, 86, 114, 170, 202):
                _, box, _ = draw_popup('graphite', FILT, kind, (x, y), (32, y-10, 59, y+9))
                if box:
                    assert inside(box, (8, 40, 288, 214))
                    assert not overlaps(box, (x-8, y-8, x+9, y+9))
                placements.append(dict(kind=kind, point=[x, y], box=box, upper_fallback=box is None))
    frames, durations = gesture_frames()
    OUT.mkdir(parents=True)
    outputs = []; scenes = {}
    for theme in COLORS:
        for v in VARIANTS:
            top, bottom, layout = scene(theme, v)
            scenes[theme, v] = dual(top, bottom)
            layouts[theme+'/'+v] = layout
            outputs.append(save(scenes[theme, v], v+'-'+theme+'-dual-2x.png'))
        notes = {'hint': 'small hint / some list occlusion', 'thumbnail': 'visual recognition / tiny text',
                 'inline': 'no overlay / fewer rows visible', 'upper': 'large preview / list stays still'}
        for group, variants in [('near-pointer', ('hint', 'thumbnail')), ('no-overlap', ('inline', 'upper'))]:
            cells = [(TITLES[v], scenes[theme, v], notes[v]) for v in variants]
            outputs.append(save(board('module catalogue / '+group.replace('-', ' '),
                'same expandable list / same focus / different inspection', cells), group+'-'+theme+'-2x.png'))
    service_cells = []
    for v, entry, label in [('hint', USER, 'user metadata / nearby hint'),
                            ('upper', USER, 'user module / no image'),
                            ('upper', LONG, 'long name / full title above')]:
        top, bottom, _ = scene('graphite', v, CUSTOM, entry)
        service_cells.append((label, dual(top, bottom), 'fictional metadata, not installed plugins'))
    outputs.append(save(board('catalogue / extension cases', '28 built-ins + 3 fictional user entries / optional images',
                              service_cells, cols=3), 'extension-cases-graphite-2x.png'))
    service = [('filter / generated categories', filter_page('graphite').im, 'the filter list itself can scroll'),
               ('empty favorites', lower('graphite', 'upper', [], FILT, group='favorites')[0].im, 'empty view, not an error'),
               ('list / no inspection', lower('graphite', 'upper', inspect=False, held=False)[0].im, 'quick tap does not wait for a tooltip')]
    outputs.append(save(board('catalogue / navigation', 'alphabetical view, categories, favorites, user / no fixed total',
                              service, cols=3, cell_kind='lower'), 'catalogue-navigation-2x.png'))
    outputs.append(save_gif(frames, durations))
    after = protected(); after['Makefile'] = sha(ROOT/'Makefile')
    assert before == after
    assert inputs == {n: sha(ROOT/n) for n in INPUTS}
    assert ready == {n: sha(ROOT/n) for n in READY}
    assert previous == {str(p.relative_to(ROOT)): sha(p) for p in OUT.parent.iterdir() if p.is_file()}
    report = dict(created_at=datetime.now(timezone.utc).isoformat(), origin=__doc__,
        status='new design proposals, not chosen or implemented; fixed 28-word grid rejected by user',
        source_sha256=inputs, protected_sha256=before, ready_sha256=ready, previous_proposal_sha256=previous,
        outputs=outputs, layouts=layouts, catalogue=[e.__dict__ for e in BUILTINS],
        fictional_user_entries=[USER.__dict__, LONG.__dict__],
        validation=dict(native_layout_cases=cases, catalogue_size_probes=scalability,
                        popup_placements=placements, lossless_png_nn2x=True, lossless_gif_decode=True,
                        labels_nonoverlapping_within_layer=True, proposed_targets_nonoverlapping=True,
                        index_is_dynamic=True, unchanged_production_makefile=len(before)),
        interaction_proposal=dict(
            quick_tap='valid release below hold threshold adds without waiting',
            inspect='stationary hold ~350ms enters inspect-only, lift does NOT add; card persists until next action',
            commit_after_inspect='new complete tap or A, never the original inspection lift',
            scroll='vertical drag threshold ~6px cancels pending add and closes inspection until a fresh down',
            focus='d-pad follows list and reveals help; no module creation on focus; A explicit commit',
            cancel='B first closes latched inspection, another B closes catalogue; physical-key frame cancels touch',
            ownership='frozen item stable ID and registry generation, engine/target identity, capture and modal validity',
            no_pen_hover='native 3ds pen has no proximity hover; mouse hover is desktop-only analogy',
            popover='noninteractive info, never a second Add menu; avoid word/contact/scrollbar, clamp/flip or use upper LCD; touching the card cancels, never activates covered rows',
            metadata='categories/authors/description/optional static thumbnail; loaded in catalogue phase not on focus',
            state='remember filter/scroll/focus per session, manual favorites, stable sort while browsing'),
        limits=[
            'proposed input timings and states only; no C++ controller, hardware touch or performance proof',
            'current app has hardcoded UnitKind; no registry/ABI/loader/third-party DSP execution implemented',
            'fictional custom module names and authors are labelled examples, not existing functionality',
            'thumbnail is old verified C++ HOST filter capture, BOX-area reduced; not a new capture, device screenshot or live unit',
            '160x120 mini image and 224x168 upper image lose text detail; recognition only, not interactive knobs',
            'only filter has a provided thumbnail; missing images use metadata fallback without blocking selection',
            'static paper is not a new recipe for current frozen dual-screen chain backdrop',
            'row expansion shifts following names and may require viewport anchoring, hence not preferred for speed',
            'ASCII proposal font coverage; Unicode user metadata policy and bounded decoding are future design tasks',
            'no app/core/assets/build/FTP/CIA/device/audio/user-data changes',
        ],
        reference=dict(url='https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/browser.htm',
            principle='separate preview/help from loading; manual describes images, preview panel, favorites and pop-out file names; no copied artwork'))
    (OUT/'proposal.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    for item in outputs:
        print(item['path'], item['size'], item['sha256'])
    print('pass:', cases, 'native cases;', len(placements), 'popup placement cases;', len(before), 'production/assets/Makefile unchanged')


if __name__ == '__main__':
    main()
