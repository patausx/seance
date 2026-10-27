#!/usr/bin/env python3
"""Design-only clean MIC proposal, not runtime, microphone or device capture.

Native 400/320 paper, existing Gossip + 6x8. A deterministic synthetic 1048-sample
input fixture is reduced to first/last/extrema, never presented as recorded audio.
No new DSP, source routing, recording functions, assets, build or deployment.
"""
from dataclasses import dataclass, replace
from datetime import datetime, timezone
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import struct
from PIL import Image, PngImagePlugin
import render_rec_dictaphone_study as style

base = style.base
ROOT = base.ROOT
OUT = ROOT / 'ui_mockups/mic_clean_study/quiet-input-v1'
THEMES = ('graphite', 'pink')
INPUTS = tuple(dict.fromkeys((
    'tools/render_mic_clean_study.py', *style.INPUTS,
    'platform/3ds/ui_module_focus_mic.cpp', 'platform/3ds/mic_in.cpp',
    'docs/mic-live.md',
)))
READY = ('release/rec-dictaphone/seance.3dsx', 'release/rec-dictaphone/build.json',
         'release/gran-perform/seance.3dsx',
         'release/gran-perform/deployment-20261003_164513.json')
PRIOR = ('ui_mockups/mic_visual_study', 'ui_mockups/mic_runtime',
         'ui_mockups/rec_workflow_study/dictaphone-v3',
         'ui_mockups/gran_perform_study/wave-first-v1')
WAVE_BOX = (20, 70, 380, 185)


@dataclass(frozen=True)
class State:
    name: str = 'open'
    gain: int = 8192
    opened: bool = True
    cut: int = 0
    input_state: str = 'live'
    bypass: bool = False
    chain_mute: bool = False


STATES = (State(), State('silent', input_state='silent'),
          State('source muted', opened=False), State('clip', gain=32767),
          State('waiting', input_state='waiting'), State('no input', input_state='stale'),
          State('unavailable', input_state='unavailable'),
          State('partial', input_state='partial'), State('bypassed', bypass=True),
          State('chain muted', chain_mute=True))


def page(theme, top=False):
    return style.page(theme, top)


@lru_cache(None)
def demo_pcm():
    """Authored audio-shaped numerical fixture; no audio file/export or mic read."""
    values = []
    for i in range(1048):
        t = i / 32728.498
        x = i / 1047
        envelope = .13 + .78*math.exp(-((x-.35)/.19)**2) + .48*math.exp(-((x-.76)/.13)**2)
        values.append(envelope*(math.sin(math.tau*176*t) +
                      .30*math.sin(math.tau*510*t+.6) + .12*math.sin(math.tau*930*t+1.2)))
    scale = 32768*10**(-4/20)/max(abs(v) for v in values)
    return tuple(round(v*scale) for v in values)


def samples(s):
    if s.input_state in ('waiting', 'stale', 'unavailable'):
        return ()
    if s.input_state == 'silent':
        return (0,)*1048
    return demo_pcm()[-96:] if s.input_state == 'partial' else demo_pcm()


def wave(p, s):
    data = samples(s)
    if not data:
        caption = {'waiting': 'waiting for input', 'stale': 'no new input',
                   'unavailable': 'mic unavailable'}[s.input_state]
        p.txt(200, 123, caption, 'muted', 'center')
        return
    missing = 1048-len(data)
    previous = None
    # Same fixed full-scale intent as existing MIC. Bigger upper geometry only;
    # neither gain, source gate nor a silent period normalizes the raw input.
    def py(v):
        return 127-round(v*57/32768)
    for col in range(360):
        a, b = col*1048//360, (col+1)*1048//360
        if b <= missing:
            continue
        block = data[max(a, missing)-missing:b-missing]
        x = 20+col
        if previous is not None:
            p.line(x-1, previous, x, py(block[0]), 'ink')
        p.line(x, py(max(block)), x, py(min(block)), 'ink')
        previous = py(block[-1])


def output_fixture(s):
    data = samples(s)
    if not data:
        return None, False
    if not s.opened or s.bypass or s.chain_mute:
        return 0, False
    # Only illustrative off-cut gain/saturation, not an engine/DSP oracle. The
    # real input/output windows differ; runtime must retain its actual telemetry.
    peak = max(abs(v) for v in data)*s.gain/8192
    return min(32768, peak), peak > 32768


def upper(theme, s):
    p = page(theme, True)
    style.word(p, 20, 36, 'mic', 28, 'accent')
    p.txt(380, 20, 'ch01 / 01', align='right')
    p.txt(20, 59, 'live input')
    status = 'bypassed' if s.bypass else 'chain muted' if s.chain_mute else \
             'source muted' if not s.opened else ''
    if status:
        p.txt(380, 59, status, align='right')
    wave(p, s)
    peak, clip = output_fixture(s)
    p.txt(20, 204, 'out')
    p.line(58, 208, 272, 208)
    p.line(272, 206, 272, 210, 'muted')
    if peak:
        db = 20*math.log10(peak/32768)
        at = 58+round(214*(max(-60., db)+60)/60)
        p.line(58, 208, at, 208, 'accent')
        p.line(at, 205, at, 211, 'accent')
        label = f'{db:.1f} dbfs'
    else:
        label = '--' if peak is None else 'silent'
    p.txt(380, 204, label, 'ink' if peak else 'muted', 'right')
    if clip:
        p.txt(380, 225, 'clip', 'accent', 'right')
    assert not p.hits
    return p


def lower(theme, s):
    p = page(theme)
    p.txt(14, 10, 'ch01 / 01')
    p.txt(242, 10, 'byp', 'accent' if s.bypass else 'muted')
    p.txt(276, 10, 'x del')
    p.hit('existing bypass / owned release', (230, 0, 270, 28))
    style.word(p, 20, 65, 'gain', 28, 'accent')
    gain_db = 20*math.log10(s.gain/8192) if s.gain else None
    text = f'{abs(gain_db):.1f}' if gain_db is not None else 'silent'
    box = style.word(p, 276, 66, text, 40 if gain_db is not None else 28, 'ink', 'right')
    if gain_db and abs(gain_db) >= .05:
        # Gossip lacks +/-; keep the native symbol, not a blank font glyph.
        p.txt(box[0]-12, 53, '+' if gain_db > 0 else '-', 'ink')
    p.txt(286, 56, 'db')
    p.hit('gain name / select or remove lock', (12, 32, 100, 73))
    p.hit('gain value / select, relative drag', (104, 32, 308, 73))
    p.hit('gain rail / relative drag, no tap jump', (12, 74, 308, 113))
    p.line(20, 94, 299, 94)
    unity = 20+round(279*8192/32767)
    p.line(unity, 92, unity, 100, 'muted')
    p.txt(unity, 103, '0', align='center')
    at = 20+round(279*s.gain/32767)
    p.line(at, 87, at, 100, 'accent')
    p.d.polygon(((at, 86), (at+2, 90), (at+1, 94), (at, 100),
                 (at-1, 94), (at-2, 90)), fill=p.c['accent'])
    p.txt(20, 138, 'input')
    p.hit('input name / select or remove lock', (12, 122, 104, 164))
    for label, center, box, selected in (
        ('open', 155, (110, 122, 207, 164), s.opened),
        ('mute', 264, (213, 122, 308, 164), not s.opened)):
        style.word(p, center, 152, label, 28, 'accent' if selected else 'muted', 'center')
        p.hit('source '+label+' / explicit choice / owned release', box)
    p.txt(20, 191, 'low-cut')
    p.hit('low-cut name / select or remove lock', (12, 176, 104, 217))
    for i, (label, center, box) in enumerate((
        ('off', 138, (108, 176, 170, 217)),
        ('80', 207, (176, 176, 238, 217)),
        ('160', 279, (244, 176, 308, 217)))):
        style.word(p, center, 203, label, 28, 'accent' if s.cut == i else 'muted', 'center')
        p.hit('existing low-cut '+label+' / owned release', box)
    p.txt(14, 232, 'b back')
    p.txt(306, 232, 'l: mod >', align='right')
    return p


def dual(top, bottom):
    return style.dual(top, bottom)


def state_board():
    c = base.Canvas(848, 584, base.OUTSIDE)
    c.text(20, 14, 'mic / quiet input', base.COLORS['graphite']['ink'], scale=2)
    c.text(20, 42, 'same raw wave when muted or clipped / no recording controls', base.COLORS['graphite']['muted'])
    for i, s in enumerate((STATES[1], STATES[2], STATES[3], STATES[5])):
        x, y = 16+(i%2)*416, 68+(i//2)*252
        c.text(x+4, y, s.name, base.COLORS['graphite']['accent'])
        c.im.paste(upper('graphite', s).im, (x, y+12))
    c.text(20, 572, 'design only / synthetic input fixture / not a hardware capture', base.COLORS['graphite']['muted'])
    return c.im


def save(image, name):
    path = OUT/name
    assert not path.exists()
    big = image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST)
    assert big.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    meta.add_text('scope', 'static proposal; synthetic demo, not live mic or C++ capture')
    big.save(path, pnginfo=meta)
    with Image.open(path) as actual:
        assert actual.convert('RGB').tobytes() == big.tobytes()
    return dict(path=str(path.relative_to(ROOT)), dimensions=list(big.size),
                bytes=path.stat().st_size, sha256=base.sha(path))


def main():
    assert not OUT.exists(), 'preserve previews; use a new revision folder'
    identities, inventory = style.prior.identities, style.prior.inventory
    protected = dict(base.protected(), Makefile=base.sha(ROOT/'Makefile'))
    inputs, ready = identities(INPUTS), identities(READY)
    previous = {k: v for folder in PRIOR for k, v in inventory(folder).items()}
    layouts = []
    for theme in THEMES:
        for s in (*STATES, replace(STATES[0], name='cut80', cut=1), replace(STATES[0], name='cut160', cut=2)):
            top, bottom = upper(theme, s), lower(theme, s)
            assert top.im.size == (400, 240) and bottom.im.size == (320, 240)
            layouts.append(dict(theme=theme, state=s.__dict__, top_labels=top.labels,
                                bottom_labels=bottom.labels, hits=bottom.hits))
        # Raw input remains byte-exact across gain, source gate, cut and bypass.
        reference = upper(theme, STATES[0]).im.crop(WAVE_BOX).tobytes()
        for s in (STATES[2], STATES[3], STATES[8], STATES[9], replace(STATES[0], cut=2)):
            assert upper(theme, s).im.crop(WAVE_BOX).tobytes() == reference
    OUT.mkdir(parents=True)
    outputs = [save(dual(upper(t, STATES[0]), lower(t, STATES[0])), f'mic-clean-{t}-2x.png') for t in THEMES]
    outputs.append(save(state_board(), 'mic-clean-states-2x.png'))
    assert protected == dict(base.protected(), Makefile=base.sha(ROOT/'Makefile'))
    assert inputs == identities(INPUTS) and ready == identities(READY)
    assert all(base.sha(ROOT/p) == digest for p, digest in previous.items())
    report = dict(created_at=datetime.now(timezone.utc).isoformat(), origin=__doc__,
        status='design only / await feedback / no runtime or deployment',
        fixture=dict(samples=1048, nominal_rate=32728.498, synthetic=True,
            pcm_sha256=hashlib.sha256(struct.pack('<1048h', *demo_pcm())).hexdigest(),
            input_peak_dbfs=-4, fixed_scale='57px per full scale, 360 columns, all sample extrema',
            main_gain_raw=8192, main_cut='off', gate='open', no_audio_export=True),
        contract=dict(controls=['gain', 'input open/mute', 'low-cut off/80/160'],
            wave='raw input before resampling/cut/gain/gate; top only, passive',
            output='existing actual post-processing card peak + saturation hold in runtime; scalar fixture here',
            source_mute='closes source to downstream REC, not monitor-only',
            diagnostics='ordinary upper overview; existing inspector retained for L and p-lock context',
            gestures='proposed owned release choices + relative gain; retain lock removal and base/live indication',
            navigation='A/B/L/X, bypass and validated REC-return retained; no record/send/save buttons'),
        validation=dict(authored_layouts=len(layouts), native_text_glyph_bounds_nonoverlap=True,
            disjoint_control_targets=True, passive_upper=True, waveform_independent_of_gain_gate_cut=True,
            no_action_underlines=True, nn_2x_png_roundtrip=True),
        limitations=['Pillow authored static proposal, not C++/GPU/device capture or sound/latency/FPS evidence',
            'synthetic fixed input; actual runtime waveform and output peak come from different windows',
            'no DSP or source routing changes; gain remains raw-linear, unity8192, cut law unchanged',
            'lock/lens/modulated base-vs-live and transient REC-return layouts are retained by contract, not drawn here',
            'ordinary new upper routing and relocated touch ownership still require implementation after approval'],
        inputs_sha256=inputs, protected_sha256=protected, ready_sha256=ready,
        previous_sha256=previous, layouts=layouts, outputs=outputs)
    (OUT/'proposal.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    for item in outputs:
        print(item['path'], item['dimensions'], item['sha256'])
    print(f'pass: {len(layouts)} authored layouts; {len(protected)} production/assets; {len(previous)} prior files preserved')


if __name__ == '__main__':
    main()
