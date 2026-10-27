#!/usr/bin/env python3
"""Design-only REC workflow proposal: authored pixels, not runtime/device capture.

Existing fullscreen material, Gossip/native 6x8. The 64-point take trace is an
explicit illustration, not microphone data, PCM rendering or engine telemetry.
No app, samples, fonts, build, network, recording or deployment writes.
"""
from datetime import datetime, timezone
from functools import lru_cache
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, PngImagePlugin
import render_module_editor_fullscreen_study as skin

base = skin.base
ROOT = base.ROOT
OUT = ROOT / 'ui_mockups/rec_workflow_study/direct-take-v2'
THEMES = ('graphite', 'pink')
STATES = ('empty', 'recording', 'start queued', 'stop queued', 'unsaved', 'saved')
READY = ('release/gran-perform/seance.3dsx', 'release/gran-perform/build.json',
         'release/gran-perform/deployment-20261003_164513.json')
INPUTS = (
    'tools/render_rec_workflow_study.py', 'tools/render_module_editor_fullscreen_study.py',
    'tools/render_module_editor_sheet_study.py', 'tools/render_module_editor_link_study.py',
    'tools/render_bounce_visual_study.py', 'tools/render_module_geometry_study.py',
    'tools/render_menu_style_study.py', 'tools/generate_start_menu_labels.py',
    'tools/render_filter_interaction_study.py', 'assets/fonts/Gossip-MedPixel.otf',
    'core/ui/font.cpp', 'platform/3ds/ui_module_focus_recorder.cpp',
    'platform/3ds/ui_module_focus_mic.cpp', 'platform/3ds/ui_editor.cpp',
    'platform/3ds/ui_workspace.cpp', 'platform/3ds/ui_sample_library.cpp',
    'platform/3ds/main.cpp', 'core/engine/engine.cpp', 'core/engine/units.cpp',
    'docs/rec-clear.md', 'docs/mic-live.md',
)
PRIOR = ('ui_mockups/rec_workflow_study/direct-take-v1',
         'ui_mockups/rec_runtime', 'ui_mockups/rec_visual_study',
         'ui_mockups/gran_perform_study', 'ui_mockups/gran_perform_runtime',
         'ui_mockups/editor_fullscreen_runtime/capture-20261003T112720_499174Z')


def identities(paths):
    return {p: base.sha(ROOT/p) for p in paths}


def inventory(folder):
    return {str(p.relative_to(ROOT)): base.sha(p)
            for p in sorted((ROOT/folder).rglob('*')) if p.is_file()}


def page(theme, top=False):
    p = base.Surface(skin.paper(theme, 'top' if top else 'bottom'), theme)
    # Quiet paper rules, rather than the old high-contrast inspector divider.
    p.c = dict(p.c, rule=base.mix(base.active_fill(theme), p.c['muted'], 30))
    return p


def tint(p, box, amount=12):
    area = p.im.crop(box)
    p.im.paste(Image.blend(area, Image.new('RGB', area.size, p.c['accent']), amount/255), box[:2])
    p.d = ImageDraw.Draw(p.im)


def action(p, name, box, label, note='', active=True, primary=False):
    p.hit(name, box)
    x0, y0, x1, y1 = box
    role = 'accent' if active and primary else 'ink' if active else 'muted'
    if primary and active:
        tint(p, box, 13)
    assert '+' not in label and '>' not in label, 'Gossip lacks these glyphs; use native symbols'
    p.word(x0+6, y0+23, label, 28, role)
    if label in ('play', 'gran'):
        p.txt(x1-13, y0+13, '>', role, 'right')
    if note:
        p.txt(x0+6, y0+32, note, 'muted')
    p.line(x0+6, y1-2, x1-7, y1-2, 'accent' if primary and active else 'rule')


@lru_cache(None)
def trace():
    """Exactly 64 authored values; no source audio is read/generated/exported."""
    return tuple((.10 + .64*math.exp(-((i/63-.24)/.13)**2)
                       + .46*math.exp(-((i/63-.69)/.17)**2))*math.sin(i*2.37)
                 for i in range(64))


def state_data(state):
    assert state in STATES
    active = state in ('recording', 'stop queued')
    queued = state in ('start queued', 'stop queued')
    take = state in ('unsaved', 'saved')
    return dict(active=active, queued=queued, busy=active or queued,
                take=take, unsaved=state == 'unsaved',
                seconds=2.08 if active else 3.20 if take else 0.0)


def upper(theme, state='unsaved', monitor=False, notice=''):
    d = state_data(state)
    p = page(theme, True)
    p.word(14, 36, 'rec', 28, 'accent')
    p.txt(386, 16, 'ch01 / 02', 'muted', 'right')
    p.txt(88, 23, 'mic > rec', 'muted')
    label = 'take ready' if d['take'] else 'ready to record' if state == 'empty' else state
    p.txt(14, 53, label, 'accent')
    p.word(14, 103, f"{d['seconds']:.2f}", 40, 'ink')
    p.txt(124, 91, 'seconds', 'muted')
    p.txt(386, 59, 's17 / mono / 32k', 'muted', 'right')
    p.txt(386, 82, 'monitor '+('on' if monitor else 'off'), 'muted', 'right')
    p.txt(386, 101, 'wave x1', 'muted', 'right')
    p.line(14, 145, 385, 145)
    if d['seconds']:
        points = [(14+round(i*371/63), 145-round(v*30)) for i, v in enumerate(trace())]
        p.d.line(points, fill=p.c['accent' if d['active'] else 'ink'])
    else:
        p.txt(200, 126, 'no recorded audio yet', 'muted', 'center')
    for x in (14, 385):
        p.line(x, 173, x, 176)
    p.txt(14, 182, '0.00s', 'muted')
    p.txt(386, 182, f"{d['seconds']:.2f}s", 'muted', 'right')
    if d['busy']:
        # Progress is a separate target-length rail; never a fake take playhead.
        p.line(14, 204, 192, 204)
        x = 14+round(178*d['seconds']/5.02)
        p.line(14, 204, x, 204, 'accent')
        p.line(x, 201, x, 207, 'accent')
        p.txt(386, 200, 'limit 5.02s', 'muted', 'right')
    else:
        status = 'wav not saved' if d['unsaved'] else 'wav saved' if d['take'] else 'input ready / no take'
        p.txt(14, 206, status, 'accent' if d['unsaved'] else 'muted')
        p.txt(386, 206, 'project changed', 'muted', 'right')
    hint = notice or ('send saves wav, then opens editor' if d['unsaved'] else
            'wav saved / save project separately' if d['take'] else
            'stop waits for the chosen clock boundary' if d['queued'] and d['active'] else
            'stop cancels queued start' if d['queued'] else
            'stop early or wait for the length limit' if d['active'] else
            'monitor off mutes listening, not recording')
    p.txt(14, 232, hint, 'muted')
    return p


def header(p, clear=False):
    p.txt(14, 9, 'ch01 / 02')
    p.txt(108, 9, 'clear', 'ink' if clear else 'muted')
    p.txt(242, 9, 'byp')
    p.txt(276, 9, 'x del')
    p.hit('clear / valid lift / confirm stopped take', (96, 0, 154, 24))
    p.hit('bypass / unchanged semantics', (230, 0, 266, 24))
    # X remains a physical-key hint, not a new delete touch target.


def lower(theme, state='unsaved', monitor=False, notice=''):
    d = state_data(state)
    p = page(theme)
    header(p, d['take'])
    p.txt(14, 32, 'input')
    p.word(14, 61, 'mic', 22, 'ink')
    p.txt(52, 52, '>', 'ink')
    p.hit('input / open actual upstream mic / no routing mutation', (8, 27, 202, 68))
    p.txt(230, 32, 'monitor')
    p.txt(230, 54, 'on' if monitor else 'off', 'ink' if not d['busy'] else 'muted')
    p.line(230, 65, 306, 65)
    p.hit('monitor name / select or remove lock', (216, 27, 312, 51))
    p.hit('monitor / valid lift / frozen while recording', (216, 51, 312, 75))
    p.txt(14, 81, 'max length', 'accent')
    p.txt(184, 81, '5.02s', 'accent', 'right')
    base.rail(p, 14, 184, 109, .25)
    p.hit('length / relative / no jump / frozen while busy', (8, 77, 208, 121))
    p.txt(264, 81, 'workspace', 'muted', 'center')
    p.txt(234, 101, '<', 'muted' if d['busy'] else 'ink')
    p.txt(254, 101, 's17', 'ink')
    p.txt(294, 101, '>', 'muted' if d['busy'] else 'ink')
    p.hit('previous workspace / existing save-discard-cancel', (220, 93, 252, 121))
    p.hit('next workspace / existing save-discard-cancel', (280, 93, 312, 121))
    primary = 'stop' if d['busy'] else 'new take' if d['take'] else 'record'
    note = 'stop queued' if state == 'stop queued' else 'cancel start' if state == 'start queued' else \
           'at clock boundary' if d['active'] else 'keep / discard choice' if d['unsaved'] else \
           'saved wav stays' if d['take'] else 'maximum 5.02s'
    action(p, primary+' / valid lift', (8, 132, 156, 174), primary, note,
           active=state != 'stop queued', primary=True)
    action(p, 'save wav / existing export', (164, 132, 312, 174), 'save wav',
           'not project save', active=d['take'])
    for i, name in enumerate(('play', 'gran')):
        action(p, 'send '+name+' / resolve destination / save / open', (8+156*i, 182, 156+156*i, 224),
               name, 'save + open' if d['unsaved'] else 'open editor' if d['take'] else 'after stop',
               active=d['take'])
    p.txt(14, 232, notice or 'b back')
    p.txt(306, 232, 'l: mod >', 'muted', 'right')
    # The inherited 16px footer target is documented separately; never inflate it
    # over the last action row for the mockup's minimum-height rule.
    return p


def dialog(theme, kind='new take', saved=False, free=True):
    p = page(theme)
    p.txt(14, 9, 'rec / ch01 / 02')
    if kind == 'destination':
        p.word(14, 47, 'send to gran', 28, 'accent')
        p.txt(14, 65, 'ch01: another source after rec', 'ink')
        p.txt(14, 81, 'do not place gran before that source')
        if free:
            action(p, 'explicit empty destination chain', (8, 103, 312, 145), 'ch02 / empty',
                   'new chain / its own level + fx', primary=True)
        else:
            p.txt(14, 116, 'no empty chain / patch unchanged', 'ink')
            p.txt(14, 132, 'free a chain, then try again')
        action(p, 'cancel destination', (8, 167, 312, 209), 'cancel', 'keep take and patch')
        p.txt(14, 232, 'save + open only after destination choice')
    elif kind == 'clear':
        p.word(14, 47, 'clear this take?', 28, 'accent')
        p.txt(14, 69, 'saved wav and sent copies stay' if saved else 'unsaved audio will be discarded', 'ink')
        action(p, 'explicit clear', (8, 103, 312, 145), 'clear', 'no new recording', primary=True)
        action(p, 'cancel clear', (8, 167, 312, 209), 'cancel', 'keep this take')
        p.txt(14, 232, 'b cancel / failed allocation keeps take')
    else:
        p.word(14, 47, 'keep this take?', 28, 'accent')
        p.txt(14, 68, '3.20s / s17 / wav not saved', 'ink')
        action(p, 'save then fresh record / transactional COW', (8, 88, 312, 130), 'save then record',
               'save wav, then start a fresh take', primary=True)
        action(p, 'explicit discard then fresh record', (8, 136, 312, 178), 'discard then record',
               'sent sources are never overwritten')
        action(p, 'cancel fresh take', (8, 184, 312, 222), 'cancel', '')
        p.txt(14, 232, 'b cancel / failed save keeps this take')
    return p


def dual(top, bottom):
    im = Image.new('RGB', (400, 496), base.OUTSIDE)
    im.paste(top.im, (0, 0)); im.paste(bottom.im, (40, 256))
    return im


def board(title, subtitle, cells):
    c = base.Canvas(24+344*len(cells), 348, base.OUTSIDE)
    c.text(24, 14, title, base.COLORS['graphite']['ink'], scale=2)
    c.text(24, 40, subtitle, base.COLORS['graphite']['muted'])
    for i, (label, p) in enumerate(cells):
        x = 24+344*i
        c.text(x, 61, label, base.COLORS['graphite']['accent'])
        c.im.paste(p.im, (x, 80))
    c.text(24, 332, 'authored proposal / illustrative take / not runtime or device capture', base.COLORS['graphite']['muted'])
    return c.im


def save(im, name):
    path = OUT/name
    assert not path.exists(), path
    big = im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST)
    assert big.resize(im.size, Image.Resampling.NEAREST).tobytes() == im.tobytes()
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    big.save(path, pnginfo=meta)
    with Image.open(path) as check:
        assert check.convert('RGB').tobytes() == big.tobytes()
    return dict(path=str(path.relative_to(ROOT)), size=list(big.size),
                bytes=path.stat().st_size, sha256=base.sha(path))


def layout(p):
    return dict(labels=p.labels, proposed_hits=p.hits)


def main():
    assert not OUT.exists(), 'preserve previous study; use a new version directory'
    inputs = identities(INPUTS)
    protected = dict(base.protected(), Makefile=base.sha(ROOT/'Makefile'))
    ready = identities(READY)
    previous = {k:v for folder in PRIOR for k,v in inventory(folder).items()}
    fixtures = []
    for theme in THEMES:
        for state in STATES:
            for monitor in (False, True):
                top, bottom = upper(theme, state, monitor), lower(theme, state, monitor)
                assert top.im.size == (400,240) and bottom.im.size == (320,240)
                fixtures.append(dict(theme=theme, state=state, monitor=monitor,
                                     top=layout(top), bottom=layout(bottom)))
        for kind, saved, free in (('new take',False,True),('clear',False,True),
                                  ('clear',True,True),('destination',False,True),
                                  ('destination',False,False)):
            p = dialog(theme,kind,saved,free)
            fixtures.append(dict(theme=theme,dialog=kind,saved=saved,free_chain=free,**layout(p)))
        for notice in ('wav save failed / take kept', 'no memory / take kept', 'chain muted'):
            top = upper(theme, notice=notice)
            fixtures.append(dict(theme=theme,notice=notice,top=layout(top)))
    OUT.mkdir(parents=True)
    outputs = [save(dual(upper(theme),lower(theme)), 'rec-workflow-'+theme+'-2x.png') for theme in THEMES]
    outputs += [save(board('record / stop / open', 'one primary action / send saves wav, then opens the chosen editor', [
        ('01 / ready for mic',lower('graphite','empty')),
        ('02 / recording',lower('graphite','recording')),
        ('03 / stopped take',lower('graphite','unsaved'))]),'rec-workflow-states-2x.png')]
    outputs += [save(board('no lost takes / no hidden sources', 'new take needs consent / a source after rec needs another destination', [
        ('01 / new take, unsaved',dialog('graphite')),
        ('02 / downstream source exists',dialog('graphite','destination')),
        ('03 / no empty chain',dialog('graphite','destination',free=False))]),'rec-workflow-consent-2x.png')]
    assert inputs == identities(INPUTS)
    assert protected == dict(base.protected(), Makefile=base.sha(ROOT/'Makefile'))
    assert ready == identities(READY)
    assert all(base.sha(ROOT/p) == sha for p,sha in previous.items())
    report = dict(created_at=datetime.now(timezone.utc).isoformat(), origin=__doc__,
        status='design only / awaiting visual and workflow approval / no implementation or deployment',
        inputs_sha256=inputs, protected_sha256=protected, installed_release_sha256=ready,
        prior_sha256=previous, outputs=outputs, fixtures=fixtures,
        validation=dict(authored_layout_cases=len(fixtures), text_bounds_and_nonoverlap=True,
            proposed_touch_bounds_and_nonoverlap=True, native_top=[400,240],native_bottom=[320,240],
            output_nearest_neighbor_2x=True, waveform_points=64,
            waveform='authored normalized values; NOT PCM, MIC, runtime capture, or an extrema envelope',
            visible_sample_number='s17 = underlying zero-based slot16 / proposed alignment with gran',
            recording_clock_fixture='beat / existing launch quantize; not a new REC parameter'),
        contract='see README.md and docs/rec-workflow-proposal.md',
        limitations=['not implemented; no controller tests, C++ capture, audio tests or hardware proof',
            'ordinary recorder and consent states only; lens/locks keep existing inspector by proposal',
            'new source-editor shortcut and send editor handoff require owned navigation tokens',
            'send destination safety is not provided by the current insert-at-rec+1 implementation',
            'no live input meter invented; recorded take only, progress has a separate target rail',
            'monitor off is this fixture, not a change to current on-by-default REC',
            'no new audition/trim/resampling or automatic mic/rec insertion in this proposal'])
    (OUT/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for item in outputs: print(item['path'],item['size'],item['sha256'])
    print('pass:',len(fixtures),'authored layouts;',len(protected),'production/assets;',len(previous),'prior files unchanged')


if __name__ == '__main__':
    main()
