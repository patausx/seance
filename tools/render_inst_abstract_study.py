#!/usr/bin/env python3
"""INST study 02: abstract responses, not pictures of musical instruments.

One fresh native-resolution Pillow sheet. The prior study's UI code is reused,
not its PNG or instrument artwork. No measured audio, animation, FFT, physical
model, controller, runtime edit, app build or device operation is involved.
Font provenance/license: assets/fonts/{README.md,OFL.txt}.
"""
from pathlib import Path
import hashlib
import math
from PIL import Image, PngImagePlugin
import render_inst_study as base

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / 'ui_mockups/inst_study/inst-abstract-v2-2x.png'
PREVIOUS = ROOT / 'ui_mockups/inst_study/inst-piano-violin-drums-v1-2x.png'


def sources():
    names = (*base.INPUTS, 'tools/render_inst_study.py',
             'tools/render_inst_abstract_study.py')
    return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in names}


def impulse(c):
    """A struck contour leaves shrinking oblique afterimages; no keys/strings."""
    f = base.Figure(c)
    # These seven leaves are authored temporal echoes, NOT spectral bins/voices.
    for i in reversed(range(7)):
        cx, cy = 169 + i * 29, 143
        height = 62 * .74**i
        width = 11 * .82**i
        lean = 19 * .74**i
        pts = []
        for j in range(65):
            t = math.tau * j / 64
            x = cx + lean * math.sin(t) + width * math.cos(t)
            y = cy + height * math.sin(t)
            pts.append((x, y))
        role = 'spot' if i == 1 else 'ink' if i == 0 else 'ghost' if i < 4 else 'rail'
        f.polygon(pts[:-1], role, 'cell' if i < 3 else 'paper')
        # A cut in each leaf turns the outline into a curved, open ink stroke.
        f.line(pts[7:27], 'paper', 2)
        f.line(pts[32:56], role)
        if i < 3:
            spine = [(cx + lean * math.sin(t) - width * .4 * math.cos(t),
                      cy + height * .87 * math.sin(t))
                     for t in (math.pi * (.22 + j * 1.55 / 32) for j in range(33))]
            f.line(spine, 'spot' if i == 1 else 'rail')
    # Small hard attack, followed by an increasingly quiet residue.
    f.polygon(((146, 126), (153, 134), (146, 155), (144, 142)), 'spot', 'spot')
    for x, y in ((363, 143), (370, 144), (377, 144)):
        f.line(((x, y), (x + 1, y)), 'rail')


def ribbon_point(t, u):
    width = 35 * math.sin(math.pi * t)**.85
    angle = math.radians(-30 + 210 * t)
    return (143 + 232 * t + u * width * math.sin(angle),
            142 + 31 * math.sin(math.tau * t + .4) * math.sin(math.pi * t)**.5
            + u * width * math.cos(angle))


def tension(c):
    """A suspended twisted ribbon: sustained tension, no bow or body silhouette."""
    f = base.Figure(c)
    boundary = [ribbon_point(j / 80, -1) for j in range(81)]
    boundary += [ribbon_point(j / 80, 1) for j in reversed(range(81))]
    f.polygon(boundary, 'rail', 'cell')
    # A few oblique stitches reveal the fold without a grid or an oscilloscope axis.
    for t in (.16, .23, .30, .37, .64, .71, .78, .85):
        f.line([ribbon_point(t, u) for u in (-1, -.5, 0, .5, 1)], 'rail')
    for i in range(11):
        u = (i - 5) / 5
        role = 'ink' if i in (0, 10) else 'spot' if i in (4, 5) else 'ghost'
        f.line([ribbon_point(j / 96, u) for j in range(97)], role)
    # One highlighted crossing, not an eye/pupil, endpoint handle or playhead.
    f.line([ribbon_point(.44 + j * .09 / 12, 0) for j in range(13)], 'spot', 2)


def impact(c):
    """A compressed irregular core and detached seams; no circular drum heads."""
    f = base.Figure(c)
    cx, cy = 253, 140
    radii = (25, 18, 28, 16, 23, 19, 29, 17, 26, 19, 23, 15)

    def point(i, scale=1., offset=0.):
        t = math.tau * (i % 12) / 12 + .12 + offset
        r = radii[i % 12] * scale
        return (cx + r * math.cos(t) * 1.32, cy + r * math.sin(t))

    # Broken traces never close into concentric membranes or a speaker icon.
    for scale, shift, role in ((2.5, -.09, 'rail'), (1.9, .05, 'ghost'), (1.42, -.04, 'ink')):
        for start, length in ((0, 2), (3, 1), (5, 2), (8, 1), (10, 1)):
            f.line([point(i, scale, shift) for i in range(start, start + length + 1)], role)
    core = [point(i, .92) for i in range(12)]
    f.polygon(core, 'ink', 'cell')
    # Uneven cut planes: a brief fracture rather than continuous held material.
    for a, b, tip in ((0, 1, (255, 136)), (3, 4, (250, 139)),
                      (6, 7, (251, 145)), (9, 10, (258, 142))):
        f.polygon((point(a, .92), point(b, .92), tip), 'ghost', 'paper')
    f.polygon(((243, 138), (253, 128), (267, 139), (257, 149)), 'spot', 'spot')
    f.polygon(((250, 138), (256, 135), (259, 142), (254, 145)), 'paper', 'paper')
    for i, scale in ((0, 2.9), (2, 2.55), (5, 2.8), (7, 2.9), (10, 2.8)):
        a = point(i, scale)
        b = point(i, scale + .13)
        f.line((a, b), 'ghost')
    f.line((point(5, 1.4), point(6, 1.4)), 'spot')
    f.line((point(10, 1.9), point(11, 1.9)), 'spot')


VARIANTS = (
    ('impulse / trace', impulse,
     ('impulse / oblique afterimages', 'a sharp beginning, a diminishing residue')),
    ('tension / drift', tension,
     ('tension / continuous folded ribbon', 'held material rather than a moving object')),
    ('impact / break', impact,
     ('impact / fractured contour', 'a dense core and short detached traces')),
)


def main():
    if TARGET.exists():
        raise SystemExit('preserve the existing study; choose a new version')
    before = sources()
    old_hash = hashlib.sha256(PREVIOUS.read_bytes()).hexdigest()
    sheet = Image.new('RGB', (1280, 656), base.OUTSIDE)
    base.caption(sheet, 20, 14, 'seance / inst / traces, not objects', 'ink')
    base.caption(sheet, 1260, 14, 'visual study 02 / graphite', right=True)
    counts = []
    geometry = None
    for i, (original, (family, figure, captions)) in enumerate(zip(base.DATA, VARIANTS)):
        s = dict(original, family=family, figure=figure)
        x = 20 + i * 420
        base.caption(sheet, x, 40, f"0{i + 1} / {s['name']}", 'spot')
        top, bottom = base.upper(s), base.lower(s)
        reference = base.lower(original)  # Fresh same-layout UI, not old image pixels.
        assert bottom.im.tobytes() == reference.im.tobytes()
        assert bottom.hits == reference.hits
        boxes = [box for _, box in bottom.hits]
        assert len(boxes) == 13
        assert geometry is None or boxes == geometry
        geometry = boxes
        assert top.im.size == (400, 240) and bottom.im.size == (320, 240)
        assert (set(top.im.get_flattened_data()) | set(bottom.im.get_flattened_data())) <= set(base.COLORS.values())
        sheet.paste(top.im, (x, 60))
        sheet.paste(bottom.im, (x + 40, 320))
        for row, text in enumerate(captions):
            assert len(text) * 6 <= 400
            base.caption(sheet, x, 582 + row * 17, text)
        counts.append(top.art_count)
    base.caption(sheet, 20, 631, 'abstract visual concept / illustrative states / native 2x / not live audio or a physical model')
    assert before == sources(), 'source changed during rendering'
    assert old_hash == hashlib.sha256(PREVIOUS.read_bytes()).hexdigest(), 'previous sheet changed'
    result = sheet.resize((2560, 1312), Image.Resampling.NEAREST)
    assert result.resize(sheet.size, Image.Resampling.NEAREST).tobytes() == sheet.tobytes()
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text('description', __doc__)
    metadata.add_text('source_sha256', '\n'.join(f'{key} {value}' for key, value in before.items()))
    result.save(TARGET, pnginfo=metadata)
    with Image.open(TARGET) as decoded:
        assert decoded.size == result.size and decoded.convert('RGB').tobytes() == result.tobytes()
    print(TARGET.relative_to(ROOT))
    print('pass: native text/figure bounds; same lower pixels and 13 target positions; graphite; nearest2x/decode; v1 preserved')
    print('one authored sheet; no old PNG pixels or literal-instrument drawing functions used')
    print('pillow primitives, not GPU cost:', counts)
    print('bytes:', TARGET.stat().st_size)
    print('sha256:', hashlib.sha256(TARGET.read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
