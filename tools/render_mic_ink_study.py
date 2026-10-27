#!/usr/bin/env python3
"""Design-only MIC ink body / ring comparison, not runtime or microphone capture.

Native pixel illustrations driven by a synthetic input fixture plus explicitly
staged deformation poses. Abstract activity, NOT a waveform or a spectrum.
Existing clean lower screen stays pixel-exact; no app, DSP, assets or deployment.
"""
from dataclasses import dataclass, replace
from datetime import datetime, timezone
import json
import math
from PIL import Image, ImageDraw, ImageChops, PngImagePlugin
import render_mic_clean_study as clean

base, ROOT = clean.base, clean.ROOT
OUT = ROOT/'ui_mockups/mic_clean_study/ink-response-v1'
THEMES = ('graphite', 'pink')
VARIANTS = ('body', 'ring')
ART_BOX = (100, 70, 300, 185)
INPUTS = ('tools/render_mic_ink_study.py', *clean.INPUTS)
PRIOR = (*clean.PRIOR, 'ui_mockups/mic_clean_study/quiet-input-v1')


@dataclass(frozen=True)
class Pose:
    name: str
    scale: float
    phase: float
    attack: float


POSES = (Pose('quiet', .05, .15, .0), Pose('voice', 1., .8, .25),
         Pose('attack', 1.4, 1.35, 1.), Pose('rest', 0., 0., 0.))
MAIN = POSES[1]


def fixture(pose, state):
    return tuple(max(-32768, min(32767, round(v*pose.scale))) for v in clean.samples(state))


def features(pose, state):
    data = fixture(pose, state)
    if not data:
        return None
    rms = math.sqrt(sum(v*v for v in data)/len(data))/32768
    peak = max(abs(v) for v in data)/32768
    # Deliberate aesthetic mapping, not a calibrated level or frequency axis.
    activity = min(1., (rms/.30)**.65)
    return dict(rms=rms, peak=peak, activity=activity,
                staged_phase=pose.phase if peak else 0., staged_attack=pose.attack if peak else 0.)


def outline(pose, data, variant, scale=1., offset=0.):
    a, phase, attack = data['activity'], data['staged_phase'], data['staged_attack']
    radius = (13+34*a) if variant == 'body' else (16+33*a)
    result = []
    for i in range(256):
        t = math.tau*i/256
        # Uneven low-order lobes, not FFT bins, a circular waveform, or particle
        # motion. Phase/onset are authored poses; no free-running time parameter.
        deform = a*(.17*math.sin(3*t+phase) + .095*math.sin(5*t-1.2*phase)
                    + .07*math.cos(2*t+.6) + .035*math.sin(8*t+1.4))
        deform += attack*a*.14*max(0., math.cos(7*t+phase))**8
        if variant == 'ring':
            deform *= .83
        r = (radius*(1+deform)+offset)*scale
        x = 200+r*1.10*math.cos(t)
        y = 127+r*.83*math.sin(t)
        assert ART_BOX[0]+2 <= x < ART_BOX[2]-2 and ART_BOX[1]+2 <= y < ART_BOX[3]-2, (pose, x, y)
        result.append((round(x), round(y)))
    return result


def ink(p, pose, state, variant):
    data = features(pose, state)
    if data is None:
        caption = {'waiting': 'waiting for input', 'stale': 'no new input',
                   'unavailable': 'mic unavailable'}[state.input_state]
        p.txt(200, 123, caption, 'muted', 'center')
        return None
    outer = outline(pose, data, variant)
    paper = p.im.copy()
    mask = Image.new('L', p.im.size)
    md = ImageDraw.Draw(mask)
    if variant == 'body':
        md.polygon(outer, fill=255)
        # Matte ink, not a glowing orb. A small edge and two inner folds make a
        # compact liquid silhouette readable without a cloud of particles.
        p.im.paste(base.mix(base.active_fill(p.theme), p.c['ink'], 84), (0, 0), mask)
        p.d = ImageDraw.Draw(p.im)
        p.d.line(outer+[outer[0]], fill=p.c['ink'])
        if data['activity'] > .10:
            for start, end, scale in ((22, 106, .78), (143, 209, .67)):
                path = outline(pose, data, variant, scale)
                p.d.line(path[start:end], fill=base.mix(base.active_fill(p.theme), p.c['accent'], 64))
    else:
        # One irregular band with a preserved paper center. Variable width gives
        # a calligraphic contour; the faint companion is an authored fold, NOT
        # an asserted history buffer or measured decay trail.
        md.polygon(outer, fill=255)
        a = data['activity']
        inner = []
        for i, (x, y) in enumerate(outer):
            t = math.tau*i/256
            thickness = 1.0+a*(1.4+1.2*math.sin(2*t+.8)**2)
            length = max(1., math.hypot(x-200, y-127))
            inner.append((round(x-(x-200)*thickness/length), round(y-(y-127)*thickness/length)))
        md.polygon(inner, fill=0)
        p.im.paste(p.c['ink'], (0, 0), mask)
        p.d = ImageDraw.Draw(p.im)
        if a > .10:
            path = outline(pose, data, variant, .87)
            p.d.line(path[11:103], fill=p.c['accent'])
            p.d.line(path[151:218], fill=base.mix(base.active_fill(p.theme), p.c['muted'], 48))
    difference = ImageChops.difference(paper, p.im).getbbox()
    assert difference and (ART_BOX[0] <= difference[0] < difference[2] <= ART_BOX[2])
    assert ART_BOX[1] <= difference[1] < difference[3] <= ART_BOX[3]
    return dict(**data, shape_bounds=difference, samples=256,
                input_semantics='raw pre-gain/source-gate/cut; not calibrated amplitude geometry')


def output_fixture(pose, state):
    data = fixture(pose, state)
    if not data:
        return None, False
    if not state.opened or state.bypass or state.chain_mute:
        return 0., False
    peak = max(abs(v) for v in data)*state.gain/8192
    return min(32768., peak), peak > 32768


def upper(theme, pose=MAIN, variant='body', state=clean.State()):
    p = clean.page(theme, True)
    clean.style.word(p, 20, 36, 'mic', 28, 'accent')
    p.txt(380, 20, 'ch01 / 01', align='right')
    p.txt(20, 59, 'live input')
    status = 'bypassed' if state.bypass else 'chain muted' if state.chain_mute else 'source muted' if not state.opened else ''
    if status:
        p.txt(380, 59, status, align='right')
    data = ink(p, pose, state, variant)
    peak, clip = output_fixture(pose, state)
    p.txt(20, 204, 'out')
    p.line(58, 208, 272, 208)
    p.line(272, 206, 272, 210, 'muted')
    if peak:
        db = 20*math.log10(peak/32768)
        at = 58+round(214*(max(-60., db)+60)/60)
        p.line(58, 208, at, 208, 'accent')
        p.line(at, 205, at, 211, 'accent')
        text = f'{db:.1f} dbfs'
    else:
        text = '--' if peak is None else 'silent'
    p.txt(380, 204, text, 'ink' if peak else 'muted', 'right')
    if clip:
        p.txt(380, 225, 'clip', 'accent', 'right')
    assert not p.hits
    return p, data


def dual(theme, variant):
    return clean.dual(upper(theme, MAIN, variant)[0], clean.lower(theme, clean.State()))


def comparison():
    c = base.Canvas(848, 584, base.OUTSIDE)
    colors = base.COLORS['graphite']
    c.text(20, 14, 'mic / ink response', colors['ink'], scale=2)
    c.text(20, 42, 'two visual directions / the same three controls', colors['muted'])
    for i, variant in enumerate(VARIANTS):
        x = 16+416*i
        c.text(x+4, 66, '01 / ink body' if i == 0 else '02 / ink ring', colors['accent'])
        c.im.paste(dual('graphite', variant), (x, 80))
    c.text(20, 576, 'static design / synthetic input + staged poses / not live mic', colors['muted'])
    return c.im


def responses():
    c = base.Canvas(848, 410, base.OUTSIDE)
    colors = base.COLORS['graphite']
    c.text(20, 14, 'quiet / voice / attack / rest', colors['ink'], scale=2)
    c.text(20, 42, 'proposed response poses / rest stays still / no spectral meaning', colors['muted'])
    for row, variant in enumerate(VARIANTS):
        y = 68+158*row
        c.text(20, y, 'ink '+variant, colors['accent'])
        for col, pose in enumerate(POSES):
            x = 16+208*col
            p, _ = upper('graphite', pose, variant)
            c.text(x+4, y+18, pose.name, colors['muted'])
            c.im.paste(p.im.crop(ART_BOX), (x, y+34))
    c.text(20, 394, 'abstract activity only / output meter and clip remain separate', colors['muted'])
    return c.im


def save(image, name):
    path = OUT/name
    assert not path.exists()
    big = image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST)
    assert big.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text('provenance', __doc__)
    big.save(path, pnginfo=metadata)
    with Image.open(path) as actual:
        assert actual.convert('RGB').tobytes() == big.tobytes()
    return dict(path=str(path.relative_to(ROOT)), dimensions=list(big.size),
                bytes=path.stat().st_size, sha256=base.sha(path))


def validate():
    cases = []
    for theme in THEMES:
        old_lower = clean.lower(theme, clean.State())
        for variant in VARIANTS:
            for pose in POSES:
                p, data = upper(theme, pose, variant)
                assert p.im.size == (400, 240)
                lower = clean.lower(theme, clean.State())
                assert lower.im.tobytes() == old_lower.im.tobytes()
                cases.append(dict(theme=theme, variant=variant, pose=pose.__dict__, art=data,
                                  top_labels=p.labels, bottom_labels=lower.labels, hits=lower.hits))
            reference = upper(theme, MAIN, variant)[0].im.crop(ART_BOX).tobytes()
            for state in (replace(clean.State(), opened=False), replace(clean.State(), gain=32767),
                          replace(clean.State(), cut=2), replace(clean.State(), bypass=True),
                          replace(clean.State(), chain_mute=True)):
                assert upper(theme, MAIN, variant, state)[0].im.crop(ART_BOX).tobytes() == reference
            for status in ('waiting', 'stale', 'unavailable'):
                p, data = upper(theme, MAIN, variant, replace(clean.State(), input_state=status))
                assert data is None
                cases.append(dict(theme=theme, variant=variant, missing=status, top_labels=p.labels))
            # Zero input cannot keep a noisy deformation just because a staged
            # attack/phase exists. These two rest renders must be identical.
            quiet = upper(theme, POSES[3], variant)[0].im.crop(ART_BOX).tobytes()
            changed = upper(theme, replace(POSES[3], phase=3., attack=1.), variant)[0].im.crop(ART_BOX).tobytes()
            assert quiet == changed
    return cases


def main():
    assert not OUT.exists(), 'preserve earlier renders; choose a new revision'
    identities, inventory = clean.style.prior.identities, clean.style.prior.inventory
    protected = dict(base.protected(), Makefile=base.sha(ROOT/'Makefile'))
    inputs, ready = identities(INPUTS), identities(clean.READY)
    prior = {k: v for folder in PRIOR for k, v in inventory(folder).items()}
    cases = validate()
    board, poses = comparison(), responses()
    OUT.mkdir(parents=True)
    outputs = [save(board, 'mic-ink-comparison-2x.png')]
    for theme in THEMES:
        for variant in VARIANTS:
            outputs.append(save(dual(theme, variant), f'mic-ink-{variant}-{theme}-2x.png'))
    outputs.append(save(poses, 'mic-ink-response-poses-2x.png'))
    assert inputs == identities(INPUTS)
    assert protected == dict(base.protected(), Makefile=base.sha(ROOT/'Makefile'))
    assert ready == identities(clean.READY)
    assert all(base.sha(ROOT/path) == digest for path, digest in prior.items())
    report = dict(created_at=datetime.now(timezone.utc).isoformat(), origin=__doc__,
        status='design only / await visual selection / no runtime approval or deployment',
        variants=list(VARIANTS), poses=[p.__dict__ for p in POSES], cases=cases,
        semantics=dict(art='abstract raw-input activity, not exact amplitude/waveform/spectrum',
            source='existing synthetic1048sample fixture scaled for quiet/voice/attack/rest',
            shape='RMS mapped aesthetically to size; phase and attack expressly staged, not measured',
            silence='small settled stationary shape; no independent idle oscillator',
            missing='explicit waiting/stale/unavailable text, not a pretend silent signal',
            mute='raw art stays visible; source gate closes downstream REC; output meter is separate',
            controls='clean-v1 lower pixel-exact; no style picker or extra user controls proposed'),
        validation=dict(layout_cases=len(cases), native_upper=[400, 240], native_lower=[320, 240],
            native_font_bounds_and_nonoverlap=True, all_shape_pixels_bounded=True,
            unchanged_lower_pixels_and_targets=True, gain_cut_gate_bypass_independent_art=True,
            settled_zero_independent_of_staged_attack=True, nearest_neighbor_2x_lossless=True),
        limitations=['static Pillow authoring, no C++ runtime/GPU/microphone capture',
            'no time sequence: no animation, response speed, smoothing, attack detection or decay demonstrated',
            'single frame scalar output illustration, no DSP/filter/resampler/clip-hold oracle',
            'implementation would need bounded copied raw-input features, smoothing/freshness and draw-only DTO',
            'no sound, hardware latency, CPU/FPS, touch or runtime acceptance claim',
            'no app/core/assets/font/source routing/project/sample writes or ARM/CIA/FTP'],
        inputs_sha256=inputs, protected_sha256=protected, ready_sha256=ready,
        previous_sha256=prior, outputs=outputs)
    (OUT/'proposal.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    for item in outputs:
        print(item['path'], item['dimensions'], item['sha256'])
    print('pass:', len(cases), 'static layout cases;', len(protected), 'production files;', len(prior), 'prior files exact')


if __name__ == '__main__':
    main()
