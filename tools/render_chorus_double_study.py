#!/usr/bin/env python3
"""Preview only: two-voice mono chorus, NOT the current DSP or a hardware capture.

The three traces share one synthetic source, with sine-modulated delays for the
proposed two wet voices. No invented FFT/audio telemetry. Existing native bitmap
font and menu paper; real saved inspector pixels reused with a proposal caption.
Font provenance: assets/fonts/README.md (SIL OFL 1.1).
"""
from pathlib import Path
import json
import math
from PIL import Image, ImageDraw
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import INK, SPOT, OUTSIDE, pair

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/chorus_double_study'
DEPTH = 171 / 32  # 5.34375 ms; same depth fixture as the captured inspector.
RATE = 0.50
MIX = 0.51
FRAME_COUNT = 40
PX_PER_MS = 3.8
X0, X1 = 65, 229
SOURCE_PEAK_MS = 7.0


def source(t_ms):
    # One explicit periodic source, not three independently drawn sine waves.
    p = (t_ms - SOURCE_PEAK_MS) * math.tau / 15.0
    return (math.cos(p) + .27 * math.cos(2 * p) - .12 * math.cos(3 * p)) / 1.15


def delay(phase, voice):
    return 3.0 + DEPTH * (1.0 + math.sin(math.tau * (phase + voice * .25))) / 2.0


def bead(c, x, y, radius, color, hollow=False):
    x, y = round(x), round(y)
    box = (x-radius, y-radius, x+radius, y+radius)
    if hollow:
        # Restore the true gradient under the ring, not a flat paper patch.
        mask = Image.new('L', c.im.size, 0)
        ImageDraw.Draw(mask).ellipse(box, fill=255)
        c.im.paste(paper(c.im.width), (0, 0), mask)
        c.d.ellipse(box, outline=color)
    else:
        c.d.ellipse(box, fill=color)


def curve(c, points, color):
    # Cubic connectors, symbolic routing only; trace centers share a time axis.
    a, b, d, e = points
    pts = []
    for i in range(25):
        t = i / 24
        u = 1-t
        pts.append((round(u**3*a[0]+3*u*u*t*b[0]+3*u*t*t*d[0]+t**3*e[0]),
                    round(u**3*a[1]+3*u*u*t*b[1]+3*u*t*t*d[1]+t**3*e[1])))
    c.d.line(pts, fill=color)


def lower(phase):
    c = Page()
    c.text(14, 10, 'chorus', SPOT)
    c.text(70, 10, 'ch01 / 01', GHOST)
    c.text(257, 10, 'byp', GHOST)
    c.text(292, 10, 'x', GHOST)
    c.line((14, 23, 49, 23), SPOT)

    # Fixed parameter touch regions, deliberately outside moving traces.
    c.text(14, 39, 'rate', GHOST)
    c.text(50, 39, '0.50hz', INK)
    c.line((109, 43, 230, 43), BRANCH)
    bead(c, 153, 43, 5, INK)
    c.text(257, 39, '2 mono', GHOST)
    c.hit('rate / fixed horizontal relative drag', (10, 28, 241, 59))

    # All three traces are the SAME source, shifted right by their delay in ms.
    # Separate baselines are display lanes, never stereo channels or amplitude.
    centers = (83, 117, 151)
    delays = (0.0, delay(phase, 0), delay(phase, 1))
    colors = (INK, SPOT, GHOST)
    c.text(14, 79, 'dry', INK)
    c.text(14, 113, '1', SPOT)
    c.text(14, 147, '2', GHOST)
    # One split and one sum: the wet signals are parallel copies, not echoes
    # feeding into one another. A 100% wet implementation must hide dry ink.
    c.line((39, centers[0], X0, centers[0]), INK)
    for lane in (1, 2):
        y = centers[lane]
        curve(c, ((47,83),(56,83),(52,y),(X0,y)), BRANCH)
        curve(c, ((X1,y),(250,y),(250,83),(270,83)), colors[lane])
    c.line((X1,83,302,83), INK)
    c.line((298,80,302,83,298,86), INK)
    bead(c, 47, 83, 2, INK)
    bead(c, 270, 83, 4, INK, hollow=True)
    c.line((267,83,273,83), INK)
    c.line((270,80,270,86), INK)

    reference_x = X0 + SOURCE_PEAK_MS * PX_PER_MS
    for y in range(66, 163, 4):
        c.d.point((round(reference_x), y), fill=BRANCH)
    for lane, (y, lag, color) in enumerate(zip(centers, delays, colors)):
        c.line((X0,y,X1,y), BRANCH)
        # A shared edge aperture joins each trace to its symbolic signal path;
        # the interior waveform/crest delay relationship remains unchanged.
        pts = [(x, round(y - 10 * min(1.0, (x-X0)/12, (X1-x)/12)
                         * source((x-X0)/PX_PER_MS-lag)))
               for x in range(X0, X1+1)]
        c.d.line(pts, fill=color)
        # One identical source crest marked in all lanes. Its rightward travel
        # is the delay itself, not an unrelated animated LFO dot.
        peak_x = reference_x + lag * PX_PER_MS
        bead(c, peak_x, y-10, 3, color, hollow=lane != 1)
        if lane:
            c.line((round(reference_x),y+15,round(peak_x),y+15), color)
            c.line((round(peak_x),y+13,round(peak_x),y+17), color)
    c.text(151, 97, f'{delays[1]:.2f}ms', SPOT)
    c.text(151, 131, f'{delays[2]:.2f}ms', GHOST)

    # Depth control stays still throughout LFO motion: no chasing live nodes.
    c.text(14, 178, 'depth', SPOT)
    c.text(14, 191, f'{DEPTH:.2f}ms', INK)
    c.line((68,175,68,209), BRANCH)
    c.line((65,175,71,175), BRANCH)
    c.line((65,209,71,209), BRANCH)
    bead(c, 68, 192, 6, SPOT)
    c.hit('depth / fixed vertical relative drag', (8, 170, 87, 217))
    c.text(102, 183, 'same voice', GHOST)
    c.text(102, 196, 'moving copies', GHOST)
    c.text(258, 178, 'mix', GHOST)
    c.text(258, 191, '51%', INK)
    c.line((299,112,299,164), BRANCH)
    bead(c, 299, round(112 + 52*MIX), 6, INK)
    c.hit('mix / fixed vertical relative drag', (278, 96, 319, 169))
    c.hit('mix / value drag', (246, 170, 316, 217))
    c.line((14,220,306,220), BRANCH)
    c.footer('b back', 'mod / temp >')
    return c


def upper():
    # Do not redesign the recently approved parameter inspector.
    c = Page(400)
    capture = ROOT / 'ui_mockups/module_inspector_runtime/raw/chorus_param_1_top.ppm'
    c.im = Image.open(capture).convert('RGB')
    c.d = ImageDraw.Draw(c.im)
    # The concept keeps depth semantics/range, but changes the one-voice DSP.
    # Replace only the narrow module name and the obsolete explanatory caption.
    for box in ((0, 0, 80, 26), (0, 228, 400, 240)):
        c.im.paste(paper(400).crop(box), box[:2])
    c.text(14, 10, 'chorus', SPOT)
    c.line((14,22,48,22), SPOT)
    c.text(14, 230, 'two mono copies / sine delay / shared source', GHOST)
    return c


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    top = upper().im
    bottom = lower(.375).im
    dual = pair(top, bottom)
    top.save(OUT / 'top_native.png')
    bottom.save(OUT / 'bottom_native.png')
    dual.save(OUT / 'chorus_dual_native.png')
    dual.resize((800, 992), Image.Resampling.NEAREST).save(OUT / 'chorus_dual_2x.png')
    bottom.resize((960, 720), Image.Resampling.NEAREST).save(OUT / 'chorus_bottom_3x.png')
    frames = [pair(top, lower(i/FRAME_COUNT).im).resize((800,992), Image.Resampling.NEAREST)
              for i in range(FRAME_COUNT)]
    # A shared exact palette prevents flickering paper and GIF color drift.
    colors = sorted(set(paper(400).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH, OUTSIDE})
    assert len(colors) <= 256
    pal = Image.new('P', (1,1))
    flat = [v for color in colors for v in color]
    pal.putpalette(flat + [0] * (768-len(flat)))
    gifs = [im.quantize(palette=pal, dither=Image.Dither.NONE) for im in frames]
    gifs[0].save(OUT / 'chorus_motion.gif', save_all=True, append_images=gifs[1:],
                 duration=50, loop=0, disposal=2, optimize=False)
    (OUT / 'concept.json').write_text(json.dumps({
        'status': 'preview only, proposed two-voice mono chorus; not current DSP or LCD capture',
        'source': 'one frozen synthetic harmonic waveform, x axis 3.8px/ms; repeated with exact delay offsets',
        'motion': '0.5Hz sine delays, quarter-cycle voice offset; one 2-second loop',
        'offset_ms': 3, 'excursion_ms': DEPTH, 'mix_percent': 51,
        'upper': 'existing C++ inspector capture; only module name and bottom explanation replaced',
        'touch_regions': lower(0).hits,
        'not_implemented': ['second voice', 'sine LFO', 'fractional delay', 'new lower UI'],
        'runtime_files_changed': False,
    }, indent=2) + '\n')
    print(OUT / 'chorus_dual_2x.png')
    print(OUT / 'chorus_motion.gif')


if __name__ == '__main__':
    main()
