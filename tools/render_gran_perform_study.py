#!/usr/bin/env python3
"""Authored GRAN wave-first proposal, NOT runtime, hardware or audio telemetry.

Real local source PCM, bounded sampled extrema and explicitly staged grains.
Existing fullscreen material/Gossip/native6x8; no new font, DSP, capture or build.
Flow is the current v2 sound mode; gate/burst/hann are proposed, not implemented.
"""
from array import array
from datetime import datetime, timezone
from functools import lru_cache
import json
import math
from pathlib import Path
import sys
import wave

from PIL import Image, ImageDraw, PngImagePlugin
import render_module_editor_fullscreen_study as skin

base = skin.base
ROOT = base.ROOT
OUT = ROOT / 'ui_mockups/gran_perform_study/wave-first-v1'
WAV = ROOT / 'local_sample_library/sdcard/3ds/seance/samples/granular/gran_dark_atmos_01.wav'
THEMES = ('graphite', 'pink')
MODES = ('flow', 'gate', 'burst')
PAGES = ('grain', 'cloud', 'setup')
SIZE = .4
SPRAY = .2
DENSITY = 10
ZOOM = 4
OFFSETS = (-.12, .10, -.05, .14)
PARAMS = {
    'grain': [('position', '50.0%', .5), ('size', '400ms', 370/470),
              ('spray', '+/-200ms', .4/6.7), ('speed', '+1.00x', .625),
              ('gain', '0.0db', 1.), ('scan', 'hold', .5)],
    'cloud': [('density', '10/s', (8000-3200)/7680), ('jitter', '0%', 0.),
              ('voices', '8', 1.), ('attack', '25%', .5),
              ('release', '25%', .5), ('p.dev', '0st', 0.)],
}
READY = ('release/editor-transition/seance.3dsx', 'release/editor-transition/build.json',
         'release/editor-transition/deployment-20261003_152403.json')
PRIOR_DIRS = ('ui_mockups/gran_classic_study', 'ui_mockups/gran_fade_study',
              'ui_mockups/gran_runtime', 'ui_mockups/module_editor_link_study',
              'ui_mockups/editor_fullscreen_runtime/capture-20261003T112720_499174Z')
INPUTS = (
    'tools/render_gran_perform_study.py', 'tools/render_module_editor_fullscreen_study.py',
    'tools/render_module_editor_sheet_study.py', 'tools/render_module_editor_link_study.py',
    'tools/render_bounce_visual_study.py', 'tools/render_module_geometry_study.py',
    'tools/render_menu_style_study.py', 'tools/generate_start_menu_labels.py',
    'tools/render_filter_interaction_study.py', 'core/ui/font.cpp',
    'assets/fonts/Gossip-MedPixel.otf', 'platform/3ds/ui_module_focus_grain.cpp',
    'platform/3ds/module_inspector.cpp', 'platform/3ds/ui_editor.cpp',
    'platform/3ds/main.cpp', 'platform/3ds/editor_style.h', 'platform/3ds/menu_style.cpp',
    'core/engine/units.h', 'core/engine/units.cpp', 'core/engine/engine.cpp',
    'core/engine/patch_io.cpp', str(WAV.relative_to(ROOT)),
)


def inventory(folder):
    return {str(p.relative_to(ROOT)): base.sha(p)
            for p in sorted((ROOT/folder).rglob('*')) if p.is_file()}


def identities(paths):
    return {p: base.sha(ROOT/p) for p in paths}


@lru_cache(None)
def source():
    with wave.open(str(WAV), 'rb') as f:
        assert f.getsampwidth() == 2 and f.getnchannels() == 1
        rate, frames = f.getframerate(), f.getnframes()
        pcm = array('h', f.readframes(frames))
    if sys.byteorder != 'little':
        pcm.byteswap()
    assert rate == 32000 and frames == 214400
    return pcm, rate, frames/rate


def viewport(position=.5, zoom=ZOOM):
    duration = source()[2]
    span = duration/zoom
    start = min(max(position*duration-span/2, 0), duration-span)
    return start, start+span


@lru_cache(None)
def extrema(width, start, end):
    """Same eight probes/bin policy as current UI; not exhaustive peaks/AGC."""
    pcm, rate, duration = source()
    assert 0 <= start < end <= duration
    first, last = round(start*rate), min(len(pcm), round(end*rate))
    result = []
    for x in range(width):
        a = first + x*(last-first)//width
        b = first + (x+1)*(last-first)//width
        count = min(8, max(1, b-a))
        values = [pcm[min(len(pcm)-1, a+(b-a-1)*j//max(1, count-1))]
                  for j in range(count)]
        result.append((min(values), max(values)))
    return tuple(result)


def envelope(phase, window='linear'):
    if not 0 < phase < 1:
        return 0.
    if window == 'hann':
        return .5-.5*math.cos(math.tau*phase)
    return min(1., phase/.25, (1-phase)/.25)


def grains(mode='flow', activity=True):
    """Illustrative latched +1x/400ms heads, never obtained from audio engine."""
    if mode == 'gate' and not activity:
        return []  # settled closed state after every earlier head has finished
    t = .32 if mode == 'burst' else .36
    center = source()[2]/2
    return [dict(start=center+offset, end=center+offset+SIZE,
                 phase=(t-i*.1)/SIZE, age=t-i*.1)
            for i, offset in enumerate(OFFSETS) if 0 <= t-i*.1 < SIZE]


def page(theme, top):
    return base.Surface(skin.paper(theme, 'top' if top else 'bottom'), theme)


def target(p, name, box):
    # Existing short utility/status strip remains 24px. Main controls are >=30px.
    x0, y0, x1, y1 = box
    assert 0 <= x0 < x1 <= 320 and 0 <= y0 < y1 <= 240, (name, box)
    assert x1-x0 >= 32 and y1-y0 >= 24, (name, box)
    for other_name, other in p.hits:
        assert (min(x1, other[2]) <= max(x0, other[0]) or
                min(y1, other[3]) <= max(y0, other[1])), (name, other_name)
    p.hits.append((name, box))


def tint(p, box, amount=12):
    x0, y0, x1, y1 = box
    area = p.im.crop(box)
    p.im.paste(Image.blend(area, Image.new('RGB', area.size, p.c['accent']), amount/255), (x0, y0))
    p.d = ImageDraw.Draw(p.im)


def plot(p, top, mode='flow', window='linear', activity=True,
         position=.5, zoom=ZOOM):
    duration = source()[2]
    lo, hi = (0., duration) if top else viewport(position, zoom)
    x0, x1 = (14, 386) if top else (14, 306)
    zero, amp = (112, 30) if top else (115, 26)
    left, right = viewport(position, zoom)
    project = lambda t: x0 + round((t-lo)*(x1-x0)/(hi-lo))
    center = position*duration
    spr0, spr1 = max(x0, project(center-SPRAY)), min(x1, project(center+SPRAY))
    if top:
        tint(p, (project(left), 69, project(right)+1, 149), 14)
        # Overview crop bracket, separate from the nominal spray bracket below.
        for x in (project(left), project(right)):
            p.line(x, 70, x, 148, 'rule')
    p.line(x0, zero, x1, zero, 'rule')
    for i, (low, high) in enumerate(extrema(x1-x0+1, lo, hi)):
        x = x0+i
        role = 'ink' if spr0 <= x <= spr1 else 'muted'
        p.line(x, zero-round(high*amp/32768), x, zero-round(low*amp/32768), role)
    bracket = 65 if top else 85
    p.line(spr0, bracket, spr1, bracket, 'accent')
    for x in (spr0, spr1):
        p.line(x, bracket, x, bracket+2, 'accent')
    cx = project(center)
    if x0 <= cx <= x1:
        p.d.polygon(((cx-2, bracket-5), (cx+2, bracket-5), (cx, bracket-2)), fill=p.c['accent'])
    # All alpha and source extents come from this declared staged fixture.
    # Real runtime must use head-latched shape/phase, including reverse clipping.
    layer = Image.new('RGBA', p.im.size)
    d = ImageDraw.Draw(layer)
    contour_base, height = (99, 12) if top else (104, 12)
    paths = []
    for g in grains(mode, activity):
        assert 0 <= g['start'] < g['end'] <= duration
        alpha = round(190*envelope(g['phase'], window))
        color = (*p.c['accent'], alpha)
        points = [(project(g['start']+SIZE*j/32),
                   contour_base-round(height*envelope(j/32, window))) for j in range(33)]
        d.line(points, fill=color)
        cursor = project(g['start']+g['age'])
        if x0 <= cursor <= x1:
            d.line((cursor, contour_base, cursor, contour_base+20), fill=color)
            d.polygon(((cursor-2, contour_base-1), (cursor+2, contour_base-1),
                       (cursor, contour_base+2)), fill=color)
        paths.append(dict(**g, alpha=alpha, cursor=cursor))
    clip = Image.new('L', p.im.size)
    ImageDraw.Draw(clip).rectangle((x0, contour_base-height, x1, contour_base+21), fill=255)
    from PIL import ImageChops
    layer.putalpha(ImageChops.multiply(layer.getchannel('A'), clip))
    p.im = Image.alpha_composite(p.im.convert('RGBA'), layer).convert('RGB')
    p.d = ImageDraw.Draw(p.im)
    if x0 <= cx <= x1:
        p.diamond(cx, 145 if top else 139, 'ink', False)  # stored position, not grain
    tick_y, text_y = (154, 160) if top else (145, 149)
    for i, t in enumerate((lo, (lo+hi)/2, hi)):
        x = x0+round(i*(x1-x0)/2)
        p.line(x, tick_y, x, tick_y+2)
        label = f'{t:.2f}s'
        if not top and i == 1:
            label += f' / x{zoom}'
        p.txt(x, text_y, label, 'muted', 'left' if i == 0 else 'right' if i == 2 else 'center')
    return dict(span=[lo, hi], lower_crop=[left, right], nominal_spawn=center,
                stored_anchor=center, spray_radius=SPRAY, staged_heads=paths,
                pcm_probes_max=8*(x1-x0+1), vertical_scale='fixed full scale')


def upper(theme, mode='flow', selected='size', window='linear', activity=True):
    p = page(theme, True)
    p.heading(14, 10, 'gran', 'accent')
    p.txt(92, 20, 'sample cloud')
    p.txt(386, 20, 'ch01 / 01', 'muted', 'right')
    p.txt(14, 47, 's03 / dark atmos', 'ink')
    state = {'flow': 'continuous', 'gate': 'held' if activity else 'waiting', 'burst': '4/4 launched'}[mode]
    p.txt(386, 47, mode+' / '+state, 'accent', 'right')
    data = plot(p, True, mode, window, activity)
    p.txt(14, 177, 'source / 6.70s')
    p.txt(386, 177, 'shade: lower zoom x4', 'muted', 'right')
    p.txt(14, 193, selected+' / base', 'accent')
    value, suffix = ('400', 'ms') if selected == 'size' else ('10', '/s')
    bounds = p.word(14, 230, value, 40, 'ink')
    p.txt(bounds[2]+8, 217, suffix)
    p.txt(174, 193, 'window')
    points = [(174+j*2, 224-round(15*envelope(j/32, window))) for j in range(33)]
    p.d.line(points, fill=p.c['accent'])
    p.txt(250, 215, window, 'muted')
    p.txt(386, 193, 'live '+value+suffix, 'accent', 'right')
    p.txt(386, 207, 'mod off', 'muted', 'right')
    p.txt(386, 231, f'{len(grains(mode, activity))}/8 grains / scan hold', 'muted', 'right')
    assert not p.hits, 'upper is passive'
    return p, data


def control(p, i, name, value, amount, selected=False, dormant=False):
    x, y = 14+(i%3)*104, 165+(i//3)*34
    role = 'muted' if dormant else 'accent' if selected else 'ink'
    p.txt(x, y, name, role)
    p.txt(x+84, y+12, 'stored' if dormant else value,
          'accent' if selected and not dormant else 'muted', 'right')
    p.line(x, y+25, x+84, y+25)
    at = x+round(amount*84)
    if name in ('speed', 'scan'):
        ref = x+round((.625 if name == 'speed' else .5)*84)
        p.line(ref, y+23, ref, y+27, 'rule')
    if dormant:
        p.d.rectangle((at-2, y+23, at+2, y+27), outline=p.c['muted'])
    else:
        p.line(at, y+21, at, y+29, 'accent' if selected else 'ink')
        p.d.rectangle((at-1, y+24, at+1, y+26), fill=p.c['accent' if selected else 'ink'])
    target(p, name+' / relative drag'+(' / dormant for hann' if dormant else ''),
           (x-6, y-4, x+90, y+29))


def setup(p, mode, window):
    entries = [('window', window+' >'), ('burst count', '4' if mode == 'burst' else '4 / stored'),
               ('zoom', 'x4 >'), ('clock', 'free >')]
    for i, (name, value) in enumerate(entries):
        x, y = 14+(i%2)*156, 165+(i//2)*34
        p.txt(x, y, name, 'muted' if name == 'burst count' and mode != 'burst' else 'ink')
        p.txt(x+136, y+12, value, 'accent' if name == 'window' else 'muted', 'right')
        p.line(x, y+25, x+136, y+25)
        target(p, name+' / configuration' if name != 'zoom' else 'zoom / view only',
               (x-6, y-4, x+142, y+29))


def lower(theme, mode='flow', tab='grain', window='linear', activity=True):
    p = page(theme, False)
    p.txt(14, 9, 'ch01 / 01')
    p.txt(104, 9, 'scan 0')
    p.txt(164, 9, 'wide')
    p.txt(242, 9, 'byp')
    p.txt(276, 9, 'x del')
    for name, box in [('scan0 / existing stored scan hold', (82,0,146,24)),
                      ('wide / existing fine toggle', (150,0,226,24)),
                      ('bypass', (230,0,266,24))]:
        target(p, name, box)
    for i, name in enumerate(MODES):
        x = 14+i*62
        p.word(x, 49, name, 22, 'accent' if mode == name else 'muted')
        if mode == name:
            p.line(x, 54, x+28, 54, 'accent')
        target(p, name+' / proposed mode / valid lift', (x-6,26,x+52,56))
    pad = (214,26,312,56)
    if mode == 'gate' and activity:
        tint(p, pad, 22)
    label = {'flow': 'restart', 'gate': 'hold', 'burst': 'burst'}[mode]
    p.word(263, 49, label, 22, 'accent', 'center')
    p.line(220, 54, 306, 54, 'accent' if mode == 'gate' and activity else 'rule')
    target(p, 'selected gran only / '+label, pad)
    p.txt(14, 65, 's03 / dark atmos', 'ink')
    target(p, 'existing sample browser', (8,58,178,82))
    for i, name in enumerate(PAGES):
        x = 188+42*i
        p.txt(x, 65, name, 'accent' if tab == name else 'muted')
        if tab == name:
            p.line(x, 77, x+28, 77, 'accent')
        target(p, name+' page / valid lift', (x-6,58,x+34,82))
    data = plot(p, False, mode, window, activity)
    target(p, 'wave / relative position only / no gate', (8,84,312,158))
    if tab == 'setup':
        setup(p, mode, window)
    else:
        for i, (name, value, amount) in enumerate(PARAMS[tab]):
            control(p, i, name, value, amount, name == ('size' if tab == 'grain' else 'density'),
                    window == 'hann' and name in ('attack', 'release'))
    hint = 'wave: position / pad: '+label
    if window == 'hann' and tab == 'cloud':
        hint = 'hann / atk + rel stored'
    p.txt(14, 232, hint)
    p.txt(306, 232, 'l: mod >', 'muted', 'right')
    return p, data


def pages(theme='graphite', mode='flow', tab='grain', window='linear', activity=True):
    top, td = upper(theme, mode, 'density' if tab == 'cloud' else 'size', window, activity)
    bottom, bd = lower(theme, mode, tab, window, activity)
    assert top.im.size == (400,240) and bottom.im.size == (320,240)
    return (top.im, bottom.im), dict(top=td, bottom=bd, top_labels=top.labels,
            bottom_labels=bottom.labels, proposed_hits=bottom.hits,
            mode=mode, page=tab, window=window, activity=activity)


def dual(images):
    result = Image.new('RGB', (400,496), base.OUTSIDE)
    result.paste(images[0], (0,0))
    result.paste(images[1], (40,256))
    return result


def board(title, subtitle, cells):
    c = base.Canvas(24+344*len(cells), 348, base.OUTSIDE)
    c.text(24, 14, title, base.COLORS['graphite']['ink'], scale=2)
    c.text(24, 40, subtitle, base.COLORS['graphite']['muted'])
    for i, (label, image) in enumerate(cells):
        x = 24+i*344
        c.text(x, 61, label, base.COLORS['graphite']['accent'])
        c.im.paste(image, (x,80))
    c.text(24, 332, 'authored proposal / staged grains / not runtime or device capture', base.COLORS['graphite']['muted'])
    return c.im


def save(image, name):
    path = OUT/name
    assert not path.exists(), path
    big = image.resize((image.width*2,image.height*2), Image.Resampling.NEAREST)
    assert big.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text('provenance', __doc__)
    metadata.add_text('source', str(WAV.relative_to(ROOT)))
    metadata.add_text('grain_state', 'authored fixed fixture; not engine telemetry or rendered audio')
    big.save(path, pnginfo=metadata)
    with Image.open(path) as actual:
        assert actual.convert('RGB').tobytes() == big.tobytes()
    return dict(path=str(path.relative_to(ROOT)), dimensions=list(big.size),
                bytes=path.stat().st_size, sha256=base.sha(path))


def validate():
    cases = 0
    for theme in THEMES:
        for mode in MODES:
            for window in ('linear','hann'):
                shared = None
                for tab in PAGES:
                    images, data = pages(theme,mode,tab,window)
                    wave_pixels = images[1].crop((8,84,312,158)).tobytes()
                    if shared is not None:
                        assert wave_pixels == shared, 'page changes must not change source/heads'
                    shared = wave_pixels
                    assert len(data['bottom']['staged_heads']) == 4
                    assert data['top']['nominal_spawn'] == data['bottom']['nominal_spawn']
                    cases += 1
        _, waiting = pages(theme,'gate',activity=False)
        assert not waiting['bottom']['staged_heads']
        cases += 1
    bounds_cases = 0
    for position in (0.,.02,.5,.98,1.):
        for zoom in (1,2,4,8,16):
            lo, hi = viewport(position,zoom)
            assert 0 <= lo < hi <= source()[2]
            assert lo <= position*source()[2] <= hi
            assert len(extrema(293,lo,hi)) == 293
            bounds_cases += 1
    for window in ('linear','hann'):
        assert envelope(0,window) == envelope(1,window) == 0
        assert envelope(.5,window) == 1
        for i in range(101):
            assert 0 <= envelope(i/100,window) <= 1
            assert abs(envelope(i/100,window)-envelope(1-i/100,window)) < 1e-12
    names = [name for group in PARAMS.values() for name,_,_ in group]
    assert len(names) == len(set(names)) == 12
    return dict(authored_dual_cases=cases, viewport_cases=bounds_cases,
                native_text_and_hit_bounds_nonoverlap=True, preserved_parameter_controls=names,
                upper_passive=True, page_invariant_wave=True,
                source_probes_upper_max=2984, source_probes_lower_max=2344,
                numerical_envelope_fixture_only=True, png_nearest_neighbor_roundtrip=True)


def main():
    assert not OUT.exists(), 'keep earlier outputs; choose a new revision'
    before = dict(base.protected(), Makefile=base.sha(ROOT/'Makefile'))
    inputs = identities(INPUTS)
    ready = identities(READY)
    previous = {p:h for folder in PRIOR_DIRS for p,h in inventory(folder).items()}
    validation = validate()
    OUT.mkdir(parents=True)
    outputs, layouts = [], {}
    for theme in THEMES:
        images, layouts[theme] = pages(theme)
        outputs.append(save(dual(images), f'gran-wave-first-{theme}-2x.png'))
    mode_cells = []
    for mode in MODES:
        images, _ = pages(mode=mode)
        caption = {'flow':'flow / continuous + restart', 'gate':'gate / hold pad; release tails',
                   'burst':'burst / four scheduled launches'}[mode]
        mode_cells.append((caption,images[1]))
    outputs.append(save(board('gran / three ways to launch',
        'same source and controls / performance pad stays separate from position editing', mode_cells),
        'gran-launch-modes-2x.png'))
    page_cells = []
    for tab in PAGES:
        window = 'hann' if tab == 'setup' else 'linear'
        images, _ = pages(mode='burst',tab=tab,window=window)
        caption = {'grain':'grain / position and motion', 'cloud':'cloud / density and envelope',
                   'setup':'setup / optional hann example'}[tab]
        page_cells.append((caption, images[1]))
    outputs.append(save(board('gran / all controls remain',
        'grain + cloud keep all 12 parameters / setup is configuration, not a new parameter bank', page_cells),
        'gran-control-pages-2x.png'))
    assert before == dict(base.protected(), Makefile=base.sha(ROOT/'Makefile'))
    assert inputs == identities(INPUTS)
    assert ready == identities(READY)
    assert previous == {p:h for folder in PRIOR_DIRS for p,h in inventory(folder).items()}
    report = dict(created_at=datetime.now(timezone.utc).isoformat(), provenance=__doc__,
        status='design only; await feedback before runtime implementation/build/deployment',
        source=dict(path=str(WAV.relative_to(ROOT)), sha256=base.sha(WAV), sample_rate=32000,
                    frames=214400, seconds=6.7, channels=1, waveform='up to8 real PCM probes/bin; fixed full scale'),
        staged_fixture=dict(telemetry=False, output_audio=False, position_seconds=3.35,
             size_ms=400, source_speed=1, scan=0, spray_radius_seconds=.2, density_per_second=10,
             jitter=0, pitch_deviation=0, attack_percent=25, release_percent=25,
             voice_limit=8, heads=4, source_start_offsets=OFFSETS, zoom=4,
             flow_gate_time_seconds=.36, burst_time_seconds=.32, burst_count=4,
             window='linear main; full hann in explicitly labelled setup example'),
        semantics=dict(flow='existing continuous generation, pad rewinds selected gran scan/scheduler; no rng reseed',
            gate='proposed pad-owned spawn permission; release stops new launches, latched heads finish',
            burst='proposed4 launches at density10/s, not4 simultaneous voices; no held-pad repeats',
            pad_scope='selected gran only, not whole-chain A; audibility still depends on downstream chain',
            waveform='relative position gesture only, never gate; freeze source/view scale during gesture',
            scan='independent audio-clock scan, hold is stored0; gate release does not stop scan',
            hann='full-grain window proposal, existing attack/release stored+dormant, latched per head',
            setup='mode/window/count future saved config, never extra13th mod/p-lock parameter; zoom view-only',
            top='new proposed overview + compact selected base/live; existing mod lens still separate',
            keys='A existing quantized whole-chain onset; B back; X confirmed delete; L lens; no new mapping'),
        implementation_gaps=['no gran note-off/gate lifetime API exists; engine event ownership needed',
            'sequencer accepted duration applies only to modern PLAY today; no UI pulse/BPM surrogate',
            'onset-only A/sigil/live-roll gate policy not designed/implemented; no held-A claim',
            'cancel/lift/lens/nav/source/engine teardown must release own gate token only',
            'finite burst retrigger should replace remaining launch budget, no unbounded accumulation',
            'all12 params occupied; separate config serialization/version/clone compatibility needs design',
            'new upper capture/draw route, bounded telemetry and lens/lock fallback required',
            'old cards remain legacy, flow/linear default; opening never converts old sound',
            'mode/window/count/sample/clock config blocked in p-lock; waveform/12 params keep lock semantics'],
        limitations=['static Pillow pixels, no C++ capture/input FSM/engine scheduling/PCM synthesis proof',
            'no hardware, listening, touch feel, performance or FPS claim',
            'only graphite and pink ordinary proposed states; locks/lens/customthemes not rendered',
            'reference used for design ideas only; no source copied and no bundled foreign samples',
            'no runtime, DSP, assets, font, project, sample, ARM, CIA or FTP changes'],
        validation=validation, layouts=layouts, input_sha256=inputs, protected_sha256=before,
        ready_sha256=ready, previous_sha256=previous, outputs=outputs)
    (OUT/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for item in outputs:
        print(item['path'], item['dimensions'], item['sha256'])
    print('pass:',validation['authored_dual_cases'],'dual layouts;',validation['viewport_cases'],
          'viewport fixtures;',len(before),'production/assets;',len(previous),'prior files preserved')


if __name__ == '__main__':
    main()
