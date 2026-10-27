#!/usr/bin/env python3
"""MIC design-only alternatives after the filled-blob / ring proposal was rejected.

Three genuinely different native-pixel studies: depth-sorted filament, layered
relief, and flowing hatch. Static authored poses, NOT live audio, FFT, history,
engine output or runtime capture. Clean lower controls remain pixel-exact.
"""
from datetime import datetime, timezone
import json
import math
from PIL import Image, ImageDraw, ImageChops, PngImagePlugin
import render_mic_ink_study as previous

clean = previous.clean
base, ROOT = clean.base, clean.ROOT
OUT = ROOT/'ui_mockups/mic_clean_study/line-materials-v1'
VARIANTS = ('filament', 'relief', 'hatch')
THEMES = ('graphite', 'pink')
ART = (14, 70, 386, 193)
INPUTS = ('tools/render_mic_line_materials_study.py', *previous.INPUTS)
PRIOR = (*previous.PRIOR, 'ui_mockups/mic_clean_study/ink-response-v1')


def color(p, amount=100, accent=False):
    return base.mix(base.active_fill(p.theme), p.c['accent' if accent else 'ink'], amount)


def polyline(p, points, amount=100, accent=False):
    points = [(round(x), round(y)) for x, y in points]
    assert len(points) > 1
    assert all(ART[0] <= x < ART[2] and ART[1] <= y < ART[3] for x, y in points), (min(points), max(points))
    p.d.line(points, fill=color(p, amount, accent))


def filament(p, f):
    """An open-looking bundle of depth-ordered strands, not a circle silhouette."""
    a, phase, attack = f['activity'], f['staged_phase'], f['staged_attack']
    span = .23+.77*a
    segments = []
    for strand in range(7):
        offset = (strand-3)*.034
        points = []
        for i in range(641):
            t = math.tau*i/640
            # Explicitly authored harmonic geometry; these are NOT measured
            # frequencies, a pitch detector, or an XY plot of mic channels.
            x = (115*math.sin(t)+39*math.sin(2*t+.28+phase*.18))*span
            y = (29*math.cos(3*t+phase*.3+offset) + 8*math.sin(5*t+.6)
                 + 8*math.sin(t))*span
            z = .68*math.cos(t+.2)+.32*math.sin(2*t+1.)
            x += strand*.65-2
            y += (strand-3)*.65
            y += attack*4*math.sin(7*t+phase)
            points.append((200+x, 129+y, z))
        for left, right in zip(points, points[1:]):
            depth = (left[2]+right[2])/2
            segments.append((depth, strand, left[:2], right[:2]))
    for depth, strand, left, right in sorted(segments):
        amount = round(31+60*(depth+1)/2)
        polyline(p, [left, right], amount, accent=strand in (0, 6))
    return dict(kind='7 thin depth-sorted strands', points=4487,
                phase='authored, not a free-running idle rotation')


def relief(p, f):
    """Perspective contours without axes/grid; authored ridges, not a spectrum."""
    a, phase, attack = f['activity'], f['staged_phase'], f['staged_attack']
    paper = p.im.copy()
    for row in range(15):
        depth = row/14
        width = 240+108*depth
        baseline = 105+row*5.65
        points = []
        for i in range(321):
            u = i/320
            drift = .035*math.sin(row*.35+phase)
            envelope = (.70*math.exp(-((u-.27-drift)/.115)**2)
                        + 1.00*math.exp(-((u-.61+drift)/.14)**2)
                        + .28*math.exp(-((u-.84)/.07)**2))
            folds = .72+.28*math.sin(u*33-row*.42+phase)
            peak = (24+8*depth)*a*envelope*folds
            peak += attack*5*math.exp(-((u-.48)/.045)**2)
            x = 200-width/2+width*u+(1-depth)*13
            y = baseline-peak
            points.append((x, y))
        # Restore original material below this nearer ridge to make crossings
        # into real occlusions. No gray rectangular chart or filled mountain.
        mask = Image.new('L', p.im.size)
        ImageDraw.Draw(mask).polygon([(round(x), round(y)) for x,y in points] +
            [(round(points[-1][0]), 192), (round(points[0][0]), 192)], fill=255)
        p.im.paste(paper, (0, 0), mask)
        p.d = ImageDraw.Draw(p.im)
        polyline(p, points, round(32+65*depth), accent=row in (5, 12))
    return dict(kind='15 perspective contours with paper-preserving occlusion',
                history='authored repeated ridges, not stored input windows')


def hatch(p, f):
    """A pleated, bowed line field; no enclosing blob or particle system."""
    a, phase, attack = f['activity'], f['staged_phase'], f['staged_attack']
    for i in range(92):
        x0 = 34+332*i/91
        q = i/91
        envelope = .12+.80*math.exp(-((q-.34)/.22)**2)+.53*math.exp(-((q-.73)/.18)**2)
        half = 9+33*a*min(1., envelope)
        center = 127+9*a*math.sin(q*math.tau*1.4+phase)
        points = []
        for j in range(41):
            u = j/20-1
            twist = math.sin(u*2.6+q*7+phase)
            x = x0+17*a*twist*(1-u*u)+8*a*u
            y = center+u*half+attack*3*math.sin(q*19+u)
            points.append((x, y))
        depth = (.5+.5*math.cos(q*8+phase))
        amount = round(38+58*depth)
        polyline(p, points, amount, accent=i%19 == 0)
    return dict(kind='92 bowed hatching strokes / 41 points each',
                field='authored line material, not a calibrated waveform')


DRAW = dict(filament=filament, relief=relief, hatch=hatch)


def upper(theme, variant, pose=previous.MAIN, state=clean.State()):
    # Reuse header, actual proposed output-meter position and error messages;
    # replace only the bounded visualization region from the rejected study.
    p, _ = previous.upper(theme, pose, 'ring', state)
    p.im.paste(clean.page(theme, True).im.crop(ART), ART[:2])
    p.d = ImageDraw.Draw(p.im)
    f = previous.features(pose, state)
    paper = p.im.copy()
    if f is None:
        text = {'waiting':'waiting for input', 'stale':'no new input', 'unavailable':'mic unavailable'}[state.input_state]
        # The original helper already registered this label before its pixels
        # were erased. Redraw without registering a duplicate bounds record.
        p.labels = [(box, label) for box, label in p.labels if label != text]
        p.txt(200, 123, text, 'muted', 'center')
        detail = None
    else:
        detail = dict(**f, **DRAW[variant](p, f))
    delta = ImageChops.difference(paper, p.im).getbbox()
    if delta:
        assert ART[0] <= delta[0] < delta[2] <= ART[2]
        assert ART[1] <= delta[1] < delta[3] <= ART[3]
    assert not p.hits
    return p, detail


def dual(theme, variant):
    return clean.dual(upper(theme, variant)[0], clean.lower(theme, clean.State()))


def board(theme):
    c = base.Canvas(432, 888, base.OUTSIDE)
    palette = base.COLORS['graphite']
    c.text(16, 14, 'mic / line materials', palette['ink'], scale=2)
    c.text(16, 42, 'three directions / lower controls unchanged', palette['muted'])
    captions = ('01 / filament', '02 / relief', '03 / flowing hatch')
    for i, (variant, caption) in enumerate(zip(VARIANTS, captions)):
        y = 64+270*i
        c.text(20, y, caption, palette['accent'])
        c.im.paste(upper(theme, variant)[0].im, (16, y+16))
    c.text(16, 876, 'static design / not live mic or a spectrum', palette['muted'])
    return c.im


def save(image, name):
    path = OUT/name
    assert not path.exists()
    large = image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST)
    assert large.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    large.save(path, pnginfo=meta)
    with Image.open(path) as actual:
        assert actual.convert('RGB').tobytes() == large.tobytes()
    return dict(path=str(path.relative_to(ROOT)), dimensions=list(large.size),
                bytes=path.stat().st_size, sha256=base.sha(path))


def main():
    assert not OUT.exists(), 'preserve older/rejected designs'
    identities, inventory = clean.style.prior.identities, clean.style.prior.inventory
    inputs = identities(INPUTS)
    protected = dict(base.protected(), Makefile=base.sha(ROOT/'Makefile'))
    ready = identities(clean.READY)
    prior = {k:v for folder in PRIOR for k,v in inventory(folder).items()}
    cases = []
    for theme in THEMES:
        for variant in VARIANTS:
            for pose in previous.POSES:
                top, detail = upper(theme, variant, pose)
                lower = clean.lower(theme, clean.State())
                assert lower.im.tobytes() == clean.lower(theme, clean.State()).im.tobytes()
                cases.append(dict(theme=theme, variant=variant, pose=pose.__dict__, art=detail,
                                  labels=top.labels, unchanged_lower_hits=lower.hits))
            reference = upper(theme, variant)[0].im.crop(ART).tobytes()
            for state in (clean.State(opened=False), clean.State(gain=32767), clean.State(cut=2),
                          clean.State(bypass=True), clean.State(chain_mute=True)):
                assert upper(theme, variant, state=state)[0].im.crop(ART).tobytes() == reference
            for status in ('waiting', 'stale', 'unavailable'):
                p, detail = upper(theme, variant, state=clean.State(input_state=status))
                assert detail is None
                cases.append(dict(theme=theme, variant=variant, missing=status, labels=p.labels))
    boards = {theme:board(theme) for theme in THEMES}
    OUT.mkdir(parents=True)
    outputs = [save(boards[theme], f'mic-line-directions-{theme}-2x.png') for theme in THEMES]
    for variant in VARIANTS:
        outputs.append(save(dual('graphite', variant), f'mic-{variant}-graphite-2x.png'))
    assert inputs == identities(INPUTS)
    assert protected == dict(base.protected(), Makefile=base.sha(ROOT/'Makefile'))
    assert ready == identities(clean.READY)
    assert all(base.sha(ROOT/path) == digest for path,digest in prior.items())
    report = dict(created_at=datetime.now(timezone.utc).isoformat(), origin=__doc__,
        status='new static alternatives / await visual feedback / not runtime approval',
        previous_feedback='user rejected ink body and ring; keep them as history, do not implement',
        variants=list(VARIANTS), cases=cases,
        contract=dict(upper='abstract input visualization only, no new controls',
            lower='same clean-v1 native320x240 pixels and targets',
            level='separate illustrative post-gain peak and clip; geometry is not calibrated',
            mapping='synthetic RMS -> size/depth; harmonic phases and attack pose are authored',
            mute='raw activity remains visible while source closes downstream REC',
            missing='explicit missing-input text; not silently replaced with idle decoration'),
        validation=dict(authored_layout_cases=len(cases), bounded_art_pixels=True,
            native_font_bounds_nonoverlap=True, gate_gain_cut_bypass_independent_art=True,
            native_upper=[400,240], native_lower=[320,240], nearest_neighbor_2x_lossless=True),
        limitations=['all static Pillow illustrations, no live mic/FFT/spectral pitch/history proof',
            'no runtime drawing budget/FPS/latency/response-speed/touch or sound acceptance',
            'no temporal frames: smoothing/decay/proposed animation still need design and implementation',
            'code is a design generator, not intended to run these floating-point loops on 3DS',
            'p-lock/lens/base-live/rec-return retained by prior contract but not shown',
            'no production/DSP/assets/fonts/user files/ARM/CIA/FTP changes'],
        inputs_sha256=inputs, protected_sha256=protected, ready_sha256=ready,
        previous_sha256=prior, outputs=outputs)
    (OUT/'proposal.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    for item in outputs:
        print(item['path'], item['dimensions'], item['sha256'])
    print('pass:',len(cases),'authored layouts;',len(protected),'production;',len(prior),'prior files preserved')


if __name__ == '__main__':
    main()
