#!/usr/bin/env python3
"""Visual proposal only: authored GRAN UI, not C++/hardware or DSP telemetry.

Real viewport PCM extrema from an existing local WAV; four staged source windows
and cursors illustrate the proposed classic granular visualization. No audio is
processed, no runtime source, patch, font, sample or installed artifact is edited.
"""
from array import array
import hashlib
import json
from pathlib import Path
import sys
import wave
from PIL import Image
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import INK, SPOT, OUTSIDE
from render_fold_wave_study import numeric, rail

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/gran_classic_study'
WAV = ROOT / 'local_sample_library/sdcard/3ds/seance/samples/granular/gran_dark_atmos_01.wav'
PROTECTED = (
    'core/engine/units.cpp', 'core/engine/units.h', 'core/engine/engine.cpp',
    'core/engine/engine.h', 'core/engine/patch_io.cpp',
    'platform/3ds/ui_module_focus_grain.cpp', 'platform/3ds/module_inspector.cpp',
    'platform/3ds/ui_editor.cpp', 'platform/3ds/ui_param_lens.cpp',
    'platform/3ds/main.cpp', 'platform/3ds/ui_top.cpp',
    'platform/3ds/note_display_data.h', 'core/ui/font.cpp',
    'assets/fonts/Gossip-MedPixel.otf', 'release/samples-go-to/seance.3dsx',
)
X0, X1, ZERO, AMP = 16, 304, 115, 26
SIZE = .4
# Explicit illustrative fixture, NOT extracted active voices. Unit speed=1x,
# attack/release each 25%, density=10/s, jitter=15%, PDEV=0, voices limit=8.
# Start offsets from the nominal position, plus illustrative lifetime phases.
GRAINS = ((-.30, .88), (-.17, .63), (.06, .38), (.30, .13))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity():
    return {p: sha(ROOT / p) for p in PROTECTED} | {str(WAV.relative_to(ROOT)): sha(WAV)}


def source():
    with wave.open(str(WAV)) as wav:
        assert wav.getsampwidth() == 2 and wav.getnchannels() == 1
        rate = wav.getframerate()
        pcm = array('h', wav.readframes(wav.getnframes()))
        if sys.byteorder != 'little':
            pcm.byteswap()
    extrema = []
    view = pcm[len(pcm)//4:3*len(pcm)//4]  # centered source-time zoom x2
    for i in range(X1-X0+1):
        chunk = view[i*len(view)//(X1-X0+1):(i+1)*len(view)//(X1-X0+1)]
        extrema.append((min(chunk), max(chunk)))
    return extrema, len(pcm)/rate, rate, len(pcm)


def plot(c, extrema, duration):
    def x(t):
        return round(X0 + (X1-X0)*(t-duration/4)/(duration/2))
    pos = duration/2
    left, middle, right = x(pos-.4), x(pos), x(pos+.4)
    # Nominal spawn spread, not a trim/playback gate. Grain windows can extend
    # beyond this band. No handles: plot is view-only, controls live below.
    for xx in range(left, right+1, 4):
        c.d.point((xx, 81), fill=BRANCH)
        c.d.point((xx, 143), fill=BRANCH)
    for yy in range(81, 144, 4):
        c.d.point((left, yy), fill=BRANCH)
        c.d.point((right, yy), fill=BRANCH)
    c.line((X0, ZERO, X1, ZERO), BRANCH)
    # Full extrema for this offline viewport, fixed +/- full scale, no AGC.
    for i, (low, high) in enumerate(extrema):
        xx = X0+i
        top = ZERO-round(high*AMP/32768)
        bottom = ZERO-round(low*AMP/32768)
        assert 83 <= top <= bottom <= 143
        c.line((xx, top, xx, bottom), INK if left <= xx <= right else GHOST)
    # Trapezoids match the existing linear attack/sustain/release family, not
    # Hann bells that would falsely suggest a new window function.
    for i, (offset, phase) in enumerate(GRAINS):
        start = pos + offset
        a, b = x(start), x(start+SIZE)
        top, base = 84+i*3, 141-i*2
        c.line((a, base, x(start+SIZE*.25), top,
                x(start+SIZE*.75), top, b, base), SPOT)
        head = x(start+SIZE*phase)
        c.line((head, top+2, head, base), SPOT)
        c.d.polygon(((head-2, top-2), (head+2, top-2), (head, top+1)), fill=SPOT)
    c.d.polygon(((middle-3, 77), (middle+3, 77), (middle, 80)), fill=INK)
    for xx in (X0, middle, X1):
        c.line((xx, 145, xx, 147), BRANCH)
    c.text(X0, 149, f'{duration/4:.2f}s', GHOST)
    c.text(middle-15, 149, f'{pos:.2f}s', GHOST)
    c.text(X1, 149, f'{duration*3/4:.2f}s', GHOST, right=True)


def control(c, index, name, value, position, selected=False, config=False):
    x = 14+(index%3)*104
    y = 165+(index//3)*34
    color = SPOT if selected else INK
    c.text(x, y, name, color)
    c.text(x+84, y+12, value, color if selected else GHOST, right=True)
    if config:
        c.text(x, y+12, '>', GHOST)
    else:
        rail(c, x, x+84, y+25, position, color)
    c.hit(name + (' / cycle configuration' if config else ' / relative horizontal drag'),
          (x-6, y-4, x+90, y+29))


def lower(mode, extrema, duration):
    c = Page()
    c.text(14, 9, 'ch01 / 01', GHOST)
    c.text(242, 9, 'byp', GHOST)
    c.text(306, 9, 'x del', GHOST, right=True)
    c.hit('bypass', (230, 0, 264, 25))
    c.heading(14, 29, 'gran', INK)
    for x, name in ((194, 'grain'), (254, 'cloud')):
        color = SPOT if mode == name else GHOST
        c.text(x, 40, name, color)
        if mode == name:
            c.line((x, 52, x+28, 52), SPOT)
        c.hit(name+' controls', (x-7, 28, x+43, 58))
    c.text(14, 64, 's03 / dark atmos', INK)
    c.text(306, 64, '4/8  zoom x2', GHOST, right=True)
    c.hit('existing sample browser', (8, 59, 190, 76))
    plot(c, extrema, duration)
    if mode == 'grain':
        values = (
            ('position', '50%', .5), ('size', '400ms', (400-30)/470),
            ('spray', '+/-400ms', .8/duration), ('speed', '1.00x >', .625),
            ('gain', '0db', 1), ('clock', 'free', 0),
        )
        selected = 'size'
    else:
        values = (
            ('density', '10/s', (8000-3200)/7680), ('jitter', '15%', .15),
            ('voices', '8', 1), ('attack', '25%', .5),
            ('release', '25%', .5), ('p.dev', '0st', 0),
        )
        selected = 'density'
    for index, (name, value, pos) in enumerate(values):
        control(c, index, name, value, pos, name == selected, name == 'clock')
    c.text(14, 231, 'view only / drag controls', GHOST)
    c.text(306, 231, 'mod / temp >', SPOT, right=True)
    c.hit('selected parameter mod / temp', (222, 229, 314, 240))
    return c


def upper(mode):
    c = Page(400)
    c.text(14, 10, 'patch01', GHOST)
    c.text(386, 10, 'chain 01 / slot 01', GHOST, right=True)
    c.text(14, 33, 'gran', SPOT)
    c.line((14, 45, 36, 45), SPOT)
    c.text(54, 33, '/ filter / rev / out', GHOST)
    is_size = mode == 'grain'
    c.text(14, 55, 'size / grain duration' if is_size else 'density / launch rate', SPOT)
    numeric(c, '400' if is_size else '10')
    c.text(18, 146, 'milliseconds' if is_size else 'grains / second', GHOST)
    c.text(14, 174, 'base / no step lock', GHOST)
    c.line((238, 75, 238, 165, 246, 165), BRANCH)
    c.text(254, 76, 'live', GHOST)
    c.text(254, 92, '400ms' if is_size else '10/s', SPOT, scale=2)
    c.text(254, 125, 'modulation', GHOST)
    c.text(254, 141, 'off', INK)
    c.text(254, 161, 'base = live', GHOST)
    low, high = ('30ms', '500ms') if is_size else ('4/s', '100/s')
    c.text(14, 194, low, GHOST)
    c.text(386, 194, high, GHOST, right=True)
    # Legal range rail follows actual stored mapping, not a fake linear rate axis.
    rail(c, 14, 386, 212, (400-30)/470 if is_size else (8000-3200)/7680, SPOT)
    c.text(14, 230, '4/8 voices / free clock', GHOST)
    c.text(386, 230, 'grain selected' if is_size else 'cloud selected', GHOST, right=True)
    return c


def main():
    before = identity()
    extrema, duration, rate, frames = source()
    OUT.mkdir(parents=True, exist_ok=True)
    receipt = {
        'origin': 'authored Pillow visual proposal, NOT runtime/hardware capture',
        'waveform': {'source': str(WAV.relative_to(ROOT)), 'sha256': sha(WAV),
                     'frames': frames, 'rate': rate, 'seconds': duration,
                     'method': 'center-half viewport per-column min/max PCM, source zoom x2, fixed +/- full scale, no normalization'},
        'grain_fixture': {'telemetry': False, 'size_ms': 400, 'speed': 1,
                          'attack_percent': 25, 'release_percent': 25,
                          'density_per_second': 10, 'jitter_percent': 15,
                          'pitch_spread_st': 0, 'voice_limit': 8,
                          'position_seconds': duration/2, 'spray_radius_seconds': .4,
                          'start_offsets_from_position_seconds_and_lifetime_phases': GRAINS},
        'semantics': ['real source-time waveform viewport (centered zoom x2), no invented spectrum or decorative dust',
                      'dotted band is nominal spawn spread, not trim; triangle is position',
                      'outlined trapezoids are staged source extents / linear grain windows',
                      'vertical lines are staged read positions, not modulation oscillation',
                      'runtime implementation would need bounded per-grain extent telemetry; current positions alone are insufficient',
                      'size/attack/release shown are nominal; short samples can shorten actual grains',
                      'grain/cloud swap controls only; image/sample/position do not change'],
        'checks': ['native text bounds/nonoverlap', 'proposed hit bounds/nonoverlap',
                   'existing palette/fonts', 'exact nearest-neighbor scaling',
                   'protected runtime/sample/artifact hashes unchanged'],
        'limits': ['no runtime UI or DSP changes', 'no synthesized audio or grain scheduler proof',
                   'no ARM build/deploy', 'no hardware or GPU-budget acceptance'],
        'frames': [], 'preserved_sha256': before,
    }
    sheet = Image.new('RGB', (816, 480), OUTSIDE)
    for index, mode in enumerate(('grain', 'cloud')):
        bottom, top = lower(mode, extrema, duration), upper(mode)
        pair = Image.new('RGB', (400, 480), OUTSIDE)
        pair.paste(top.im, (0, 0)); pair.paste(bottom.im, (40, 240))
        name = f'{index+1:02d}_{mode}'
        for surface, page in (('bottom', bottom), ('top', top)):
            allowed = set(paper(page.im.width).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH}
            assert set(page.im.get_flattened_data()) <= allowed
            page.im.save(OUT / f'{name}_{surface}_native.png')
            page.im.resize((page.im.width*2, 480), Image.Resampling.NEAREST).save(OUT / f'{name}_{surface}_2x.png')
        pair.save(OUT / f'{name}_dual_native.png')
        large = pair.resize((800, 960), Image.Resampling.NEAREST)
        assert large.resize(pair.size, Image.Resampling.NEAREST).tobytes() == pair.tobytes()
        large.save(OUT / f'{name}_dual_2x.png')
        sheet.paste(pair, (index*416, 0))
        receipt['frames'].append({'name': name, 'top_labels': top.labels,
                                  'bottom_labels': bottom.labels, 'proposed_hits': bottom.hits})
    sheet.save(OUT / 'modes_native.png')
    sheet.resize((1632, 960), Image.Resampling.NEAREST).save(OUT / 'modes_2x.png')
    assert identity() == before
    (OUT / 'verification.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('pass: two authored classic GRAN duals; real local WAV, staged grains; native label/hit/palette checks')
    print('runtime/DSP/sample/font/deployed artifact unchanged; no ARM/deploy')
    print(OUT / 'modes_2x.png')


if __name__ == '__main__':
    main()
