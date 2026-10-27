#!/usr/bin/env python3
"""Preview only: three rose-grey module bodies over the saved real Chains frame.

The base is a native C++ host raster (not a hardware screenshot). Only the
58x36 node interiors receive Python concept ink. Original foreground pixels,
labels, sockets, wires, screen chrome and installed software stay untouched.
No controller, hit-area, hardware, GPU or audio validation is implied.
"""
from pathlib import Path
import hashlib
import json
import math
from PIL import Image, ImageDraw, ImageChops
from render_module_geometry_study import Canvas, INK, SPOT
from render_menu_style_study import paper

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/module_shell_study'
REF = ROOT / 'ui_mockups/menu_stage1_runtime'
OUTSIDE = (37, 27, 36)
CAPTION = (228, 194, 207)
QUIET = (180, 154, 174)
EDGE = (157, 127, 147)
CHOSEN_EDGE = (132, 96, 130)
ROSE_LIGHT = (245, 222, 231)
GREY_ROSE = (171, 150, 168)
NODES = [('fm', 16, 86, False), ('strn', 172, 94, True),
         ('ghost', 80, 160, False), ('rev', 188, 160, False)]
VARIANTS = [('current', '00 / current', 'text and sockets only'),
            ('cut', '01 / cut contour', 'rose-grey edge / no panel fill'),
            ('capsule', '02 / soft capsule', 'soft body / lifted selection'),
            ('family', '03 / family silhouettes', 'one material / different bodies')]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def protected_inputs():
    paths = [p for folder in ('core', 'platform', 'assets', 'release')
             for p in (ROOT / folder).rglob('*') if p.is_file()]
    paths += [p for p in ROOT.glob('seance.*') if p.is_file()]
    paths += [ROOT / 'Makefile', ROOT / 'tools/render_module_geometry_study.py',
              ROOT / 'tools/render_menu_style_study.py',
              REF / 'chains_four_bottom_native.png', REF / 'chains_four_top_native.png']
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted(set(paths))}


def blend(a, b, amount):
    return tuple(round(x * (1 - amount) + y * amount) for x, y in zip(a, b))


def shell_mask(variant, module):
    """All bodies stay within the existing 58x36 node; no implied hit changes."""
    mask = Image.new('L', (58, 36))
    d = ImageDraw.Draw(mask)
    if variant == 'cut':
        d.polygon([(8, 2), (49, 2), (55, 8), (55, 27),
                   (49, 33), (8, 33), (2, 27), (2, 8)], fill=255)
    elif variant == 'capsule':
        d.rounded_rectangle((2, 2, 55, 33), radius=15, fill=255)
    elif module == 'fm':
        d.ellipse((2, 1, 55, 34), fill=255)
    elif module == 'strn':
        # A small spindle/lens, echoing the live-grain editor's longer figure.
        points = []
        for i in range(55):
            x = 2 + i
            half = 15 * math.sin(math.pi * i / 54) ** .65
            points.append((x, round(17.5 - half)))
        for i in reversed(range(55)):
            x = 2 + i
            half = 15 * math.sin(math.pi * i / 54) ** .65
            points.append((x, round(17.5 + half)))
        d.polygon(points, fill=255)
    elif module == 'ghost':
        # Quiet four-lobed crown/foot, not a cartoon ghost or made-up meter.
        d.rounded_rectangle((2, 7, 55, 29), radius=7, fill=255)
        for x in (5, 16, 27, 38):
            d.ellipse((x, 2, x + 14, 33), fill=255)
    else:
        # Receding chamber silhouette without tiny inner pictograms.
        d.polygon([(10, 2), (47, 2), (47, 5), (52, 5), (52, 9),
                   (55, 9), (55, 26), (52, 26), (52, 30), (47, 30),
                   (47, 33), (10, 33), (10, 30), (5, 30), (5, 26),
                   (2, 26), (2, 9), (5, 9), (5, 5), (10, 5)], fill=255)
    assert mask.getbbox() and set(mask.get_flattened_data()) <= {0, 255}
    return mask


def boundary(mask):
    # Native one-column scan boundary: no antialiasing and no thick blur halo.
    edge = Image.new('L', mask.size)
    p, e = mask.load(), edge.load()
    for y in range(36):
        for x in range(58):
            if p[x, y] and any(nx < 0 or nx >= 58 or ny < 0 or ny >= 36 or
                              not p[nx, ny] for nx, ny in
                              ((x-1, y), (x+1, y), (x, y-1), (x, y+1))):
                e[x, y] = 255
    return edge


def treatment(base, variant, foreground, allowed):
    image = base.copy()
    if variant == 'current':
        return image
    for module, x, y, selected in NODES:
        mask = shell_mask(variant, module)
        fill = image.crop((x, y, x+58, y+36))
        p = fill.load()
        if variant != 'cut':
            strength = (.38 if selected else .20) if variant == 'capsule' else (.27 if selected else .09)
            tint = ROSE_LIGHT if selected else GREY_ROSE
            for yy in range(36):
                for xx in range(58):
                    p[xx, yy] = blend(p[xx, yy], tint, strength)
            image.paste(fill, (x, y), mask)
        image.paste(CHOSEN_EDGE if selected else EDGE, (x, y), boundary(mask))
        # Tiny inner necks connect the body to the existing external sockets.
        d = ImageDraw.Draw(image)
        row = [i for i in range(58) if mask.getpixel((i, 18))]
        for a, b in ((0, min(row)), (max(row), 57)):
            d.line((x+a, y+18, x+b, y+18), fill=CHOSEN_EDGE if selected else EDGE)
    # The original actual C++ foreground is the authority, not duplicated text.
    image.paste(base, (0, 0), foreground)
    diff = ImageChops.difference(base, image).convert('L').point(lambda p: 255 if p else 0)
    assert ImageChops.subtract(diff, allowed).getbbox() is None
    assert ImageChops.multiply(ImageChops.difference(base, image).convert('L'), foreground).getbbox() is None
    return image


def save_image(image, stem):
    image.save(OUT / f'{stem}_native.png')
    big = image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST)
    big.save(OUT / f'{stem}_2x.png')
    assert ImageChops.difference(big.resize(image.size, Image.Resampling.NEAREST), image).getbbox() is None


def main():
    before = protected_inputs()
    OUT.mkdir(parents=True, exist_ok=True)
    report = json.loads((REF / 'report.json').read_text())
    # Ensure the saved node renderer/layout really are the current sources.
    for path in ('platform/3ds/ui_chains.cpp', 'platform/3ds/ui_workspace.cpp'):
        assert sha(ROOT / path) == report['sources'][path], path
    base = Image.open(REF / 'chains_four_bottom_native.png').convert('RGB')
    top = Image.open(REF / 'chains_four_top_native.png').convert('RGB')
    assert base.size == (320, 240) and top.size == (400, 240)
    bg = paper(320)
    assert ImageChops.difference(base.crop((0, 0, 10, 240)), bg.crop((0, 0, 10, 240))).getbbox() is None
    foreground = Image.new('L', base.size)
    foreground.putdata([255 if a != b else 0 for a, b in
                        zip(base.get_flattened_data(), bg.get_flattened_data())])
    allowed = Image.new('L', base.size)
    for _, x, y, _ in NODES:
        ImageDraw.Draw(allowed).rectangle((x, y, x+57, y+35), fill=255)
    pages = {key: treatment(base, key, foreground, allowed) for key, _, _ in VARIANTS}
    checks = {}
    for key, image in pages.items():
        save_image(image, key + '_bottom')
        dual = Image.new('RGB', (400, 496), OUTSIDE)
        dual.paste(top, (0, 0))
        dual.paste(image, (40, 256))
        save_image(dual, key + '_dual')
        checks[key] = {'changed_pixels': sum(a != b for a, b in zip(base.get_flattened_data(), image.get_flattened_data())),
                       'outside_node_changes': 0, 'original_foreground_changes': 0}
    # Same exact positions and real capture in every tile; captions outside LCD.
    sheet = Canvas(668, 560, OUTSIDE)
    for i, (key, title, subtitle) in enumerate(VARIANTS):
        x, y = 8 + (i % 2) * 332, (i // 2) * 280
        sheet.text(x, y + 7, title, CAPTION)
        sheet.text(x, y + 21, subtitle, QUIET)
        sheet.im.paste(pages[key], (x, y + 36))
    save_image(sheet.im, 'comparison')
    # More legible optional single-screen 3x inspection; integer scale only.
    for key, image in pages.items():
        image.resize((960, 720), Image.Resampling.NEAREST).save(OUT / f'{key}_bottom_3x.png')
    assert before == protected_inputs(), 'protected runtime/assets/releases changed'
    result = {'status': 'visual proposals only; no runtime edit/build/deployment',
              'base': 'saved real C++ host fixture chains_four; not LCD capture',
              'changes': 'only added silhouette/fill inside four existing 58x36 bodies',
              'states': 'same illustrative FM/STRN/Ghost/REV fixture, STRN selected',
              'variants': {key: {'title': title, 'description': desc} for key, title, desc in VARIANTS},
              'checks': checks, 'native': [320, 240], 'nearest_scales': [2, 3],
              'protected_inputs': before,
              'limits': ['no controller/gesture test', 'no hardware contrast or GPU budget measurement',
                         'not all twenty module shapes designed', 'no bypass/moving/overlap coverage',
                         'original sockets, labels and selection underline preserved pixel-exact']}
    (OUT / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
    (OUT / 'README.txt').write_text(__doc__ + '\nrun: python3 tools/render_module_shell_study.py\n'
        'comparison_2x.png: current / cut contour / soft capsule / family silhouettes.\n'
        'all changes are concept additions over a saved host frame; not shipping UI.\n')
    print(OUT / 'comparison_2x.png')
    print(json.dumps(checks, indent=2))
    print(f'pass: {len(before)} protected files unchanged; foreground/outside-node pixels exact')


if __name__ == '__main__':
    main()
