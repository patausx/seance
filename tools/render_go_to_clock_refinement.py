#!/usr/bin/env python3
"""Three GO TO clock header studies; Python concepts, not runtime/LCD captures.

Only the old project-clock proposal's right header is replaced. Its project
entry, seven navigation bands, footer, title and all other pages stay intact.
Numbers/routes are illustrative; none of these controls is implemented.
Existing 6x8 bitmap type and menu paper; no new font or device writes.
"""
from dataclasses import replace
from pathlib import Path
import hashlib
import json
from PIL import Image, ImageDraw, ImageChops
from render_menu_style_study import paper, GHOST, BRANCH, overlaps
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, HI
from render_project_clock_study import nav, TEMPO, SWING

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/go_to_clock_refinement'
HEADER = (148, 0, 320, 58)
VARIANTS = [('01_inline', '01 / one line', 'no panel / values beside title'),
            ('02_branch', '02 / small branch', 'same language as navigation'),
            ('03_columns', '03 / quiet columns', 'short labels / no slider rails')]


def protected():
    paths = [p for folder in ('core', 'platform', 'assets', 'release',
                              'ui_mockups/project_clock_study',
                              'ui_mockups/chain_workflow_study')
             for p in (ROOT / folder).rglob('*') if p.is_file()]
    paths += list(ROOT.glob('seance.*'))
    paths += [ROOT / p for p in ('Makefile', 'tools/render_project_clock_study.py',
              'tools/render_menu_style_study.py', 'tools/render_module_geometry_study.py')]
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(set(paths)) if p.is_file()}


def bare(tempo, swing):
    c = nav(tempo, swing)
    c.im.paste(paper(320).crop(HEADER), HEADER[:2])
    c.d = ImageDraw.Draw(c.im)
    # Remove only the replaced old-header metadata; keep all menu/title labels.
    c.labels = [(box, label) for box, label in c.labels if not overlaps(box, HEADER)]
    c.hits = [(name, box) for name, box in c.hits if not overlaps(box, HEADER)]
    return c


def route(c, x, y, clock):
    if clock.source != 'none':
        c.text(x, y, clock.source, SPOT)


def draw(key, tempo=TEMPO, swing=SWING):
    c = bare(tempo, swing)
    if key == '01_inline':
        c.text(162, 28, str(tempo.base), INK)
        c.text(186, 28, 'bpm', GHOST)
        route(c, 214, 28, tempo)
        c.text(234, 28, f'{swing.base}%', INK)
        c.text(258, 28, 'swing', GHOST)
        route(c, 298, 28, swing)
        c.hit('tempo base / relative drag', (154, 8, 226, 56))
        c.hit('swing base / relative drag', (228, 8, 316, 56))
    elif key == '02_branch':
        c.line((174, 16, 174, 42), BRANCH)
        for clock, y, box in ((tempo, 12, (168, 2, 316, 28)),
                              (swing, 38, (168, 30, 316, 56))):
            c.line((174, y+4, 183, y+4), BRANCH)
            c.text(190, y, clock.name, GHOST)
            c.text(244, y, str(clock.base) + ('%' if clock.name == 'swing' else ''), INK)
            if clock.name == 'tempo':
                c.text(268, y, 'bpm', GHOST)
            route(c, 300, y, clock)
            c.hit(clock.name+' base / relative drag', box)
    elif key == '03_columns':
        for clock, x, sx, box in ((tempo, 170, 214, (156, 6, 230, 56)),
                                  (swing, 248, 300, (234, 6, 316, 56))):
            c.text(x, 14, 'bpm' if clock.name == 'tempo' else 'swing', GHOST)
            c.text(x, 34, str(clock.base) + ('%' if clock.name == 'swing' else ''), INK)
            route(c, sx, 34, clock)
            c.hit(clock.name+' base / relative drag', box)
    else:
        raise ValueError(key)
    return c


def validate(c, reference, pixels=True):
    assert c.im.size == (320, 240)
    for _, box in c.hits:
        assert box[2]-box[0] >= 22 and box[3]-box[1] >= 22
    assert [box for name, box in c.hits if name.startswith('navigate ')] == [
        (38, 58+i*22, 300, 80+i*22) for i in range(7)]
    if pixels:
        diff = ImageChops.difference(c.im, reference.im)
        diff.paste((0, 0, 0), HEADER)
        assert diff.getbbox() is None, 'pixels outside header changed'
        assert all(p == bg or p in {INK, SPOT, GHOST, BRANCH}
                   for p, bg in zip(c.im.get_flattened_data(), paper(320).get_flattened_data()))


def save(im, stem):
    im.save(OUT / (stem+'_native.png'))
    big = im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST)
    assert big.resize(im.size, Image.Resampling.NEAREST).tobytes() == im.tobytes()
    big.save(OUT / (stem+'_2x.png'))


def main():
    before = protected()
    OUT.mkdir(parents=True, exist_ok=True)
    previous = nav()
    saved_previous = Image.open(ROOT / 'ui_mockups/project_clock_study/01_go_to_bottom_native.png').convert('RGB')
    assert saved_previous.tobytes() == previous.im.tobytes(), 'old proposal source/raster mismatch'
    frames = [('00_previous', '00 / previous proposal', 'reference / other pages unchanged', previous)]
    for key, title, description in VARIANTS:
        c = draw(key)
        validate(c, previous)
        frames.append((key, title, description, c))
        # Additional clean states retain both controls without fictitious routes.
        t, s = replace(TEMPO, source='none'), replace(SWING, source='none')
        clean = draw(key, t, s)
        validate(clean, nav(t, s))
        save(clean.im, key+'_no_routes')
    sheet = Canvas(664, 560, OUTSIDE)
    headers = Canvas(664, 184, OUTSIDE)
    for i, (key, title, description, c) in enumerate(frames):
        x, y = 8+(i%2)*328, 8+(i//2)*280
        sheet.text(x+4, y, title, HI)
        sheet.text(x+4, y+13, description, HI)
        sheet.im.paste(c.im, (x, y+30))
        hx, hy = 8+(i%2)*328, 8+(i//2)*88
        headers.text(hx+4, hy, title, HI)
        headers.im.paste(c.im.crop((0, 0, 320, 58)), (hx, hy+18))
        save(c.im, key)
    save(sheet.im, 'comparison')
    save(headers.im, 'headers')
    probes = 0
    for key, _, _ in VARIANTS:
        for bpm in (20, 120, 300):
            for amount in (50, 58, 75):
                for a, b in zip(('none', 'a', 'b', 'c', 'd'), ('none', 'd', 'c', 'b', 'a')):
                    t, s = replace(TEMPO, base=bpm, source=a), replace(SWING, base=amount, source=b)
                    validate(draw(key, t, s), nav(t, s), pixels=False)
                    probes += 1
    assert before == protected(), 'protected input changed'
    report = {
        'status': 'offline visual proposals only; no implementation/build/upload',
        'scope': 'only x148..319/y0..57 of old GO TO proposal',
        'user_correction': 'project/tempo/swing pages are fine; refine GO TO appearance only',
        'checks': {'outside_header_pixel_exact': True, 'old_proposal_reproduced_exactly': True,
                   'native': [320, 240], 'scale': 'nearest 2x', 'palette_per_row': True,
                   'labels_and_hits_bounds_no_overlap': True, 'minimum_hit_size': 22,
                   'boundary_source_layout_probes': probes,
                   'seven_menu_bands_exact': True, 'protected_files_unchanged': len(before)},
        'variants': {key: {'title': title, 'description': description,
                          'labels': c.labels, 'proposed_hits': c.hits}
                     for key, title, description, c in frames},
        'limits': ['synthetic 120bpm / 58% / sigil a and b, not live engine values',
                   'project entry and clock modulation remain earlier unimplemented proposals',
                   'no new input/controller/GPU/audio or device testing',
                   'route letters are status; detailed route editing remains on clock pages',
                   'focus, dragging and keyboard behavior are not implemented'],
        'protected_inputs': before}
    (OUT/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    (OUT/'README.txt').write_text(__doc__+'\nrun: python3 tools/render_go_to_clock_refinement.py\n'
        'comparison_2x.png: previous GO TO and three header-only alternatives.\n'
        'headers_2x.png: enlarged comparison of only the changed header region.\n'
        'all other clock screens, sources, assets and installed binary untouched.\n')
    print(OUT/'comparison_2x.png')
    print(f'pass: 3 header variants; {probes} layout probes; {len(before)} protected files unchanged')


if __name__ == '__main__':
    main()
