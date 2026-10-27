#!/usr/bin/env python3
"""Minimal swarm proposal, not runtime, audio output or a device benchmark.

Exact current title/numeral masks, native 6x8 text and existing quiet paper.
Stipple is an exhaustive min/max envelope of synthetic PCM, not a spectrum.
Read heads are staged; no engine is executed. One page, six controls, freeze.
"""
from datetime import datetime, timezone
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import re

from PIL import Image, ImageDraw, PngImagePlugin
import render_module_editor_fullscreen_study as skin

base = skin.base
ROOT = Path(__file__).resolve().parents[1]
SPAN, SIZE, POSITION, SPRAY = 1.024, .160, .480, .120
VOICE_CAP = 8  # proposal fixture only; device budget has not been measured
THEMES = ('graphite', 'pink')
STATES = ('live', 'frozen', 'silent')
OUTSIDE = (18, 18, 21)


def bitmap(path):
    src = (ROOT / path).read_text()
    body = src.split('bitmap[] = {', 1)[1].split('};', 1)[0]
    return src, bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', body))


def decode(bits, offset, width, height):
    mask = Image.new('L', (width, height))
    stride = (width + 7) // 8
    for y in range(height):
        for x in range(width):
            if bits[offset + y*stride + x//8] & (128 >> (x % 8)):
                mask.putpixel((x, y), 255)
    return mask


@lru_cache(None)
def title_mask():
    src, bits = bitmap('platform/3ds/module_heading_data.h')
    row = re.search(r'\{UnitKind::Stranular, "swarm", (\d+), (\d+), (\d+)\}', src)
    offset, width, height = map(int, row.groups())
    assert (width, height) == (57, 12)
    return decode(bits, offset, width, height)


@lru_cache(None)
def numeral(char):
    src, bits = bitmap('platform/3ds/note_display_data.h')
    row = re.search(r"\{'" + char + r"', false, (\d+), (\d+), (\d+), (-?\d+)\}", src)
    offset, width, height, top = map(int, row.groups())
    return decode(bits, offset, width, height), top


def number(p, x, baseline, value):
    for char in value:
        mask, dy = numeral(char)
        box = (x, baseline+dy, x+mask.width, baseline+dy+mask.height)
        assert all(not overlap(box, old) for old, _ in p.labels)
        p.labels.append((box, char))
        p.im.paste(p.c['ink'], box[:2], mask)
        x += mask.width + 4
    return x


def overlap(a, b):
    return min(a[2], b[2]) > max(a[0], b[0]) and min(a[3], b[3]) > max(a[1], b[1])


def page(theme, top):
    p = base.Surface(skin.paper(theme, 'top' if top else 'bottom'), theme)
    p.im.paste(p.c['accent'], (14, 8), title_mask())
    p.labels.append(((14, 8, 71, 20), 'swarm / exact private mask'))
    if top:
        p.txt(386, 14, 'ch01 / 02', 'muted', 'right')
        p.txt(14, 35, 'mic /', 'muted')
        p.txt(50, 35, 'swarm', 'accent')
        p.txt(92, 35, '/ rev', 'muted')
        p.line(50, 45, 78, 45, 'accent')
    else:
        p.txt(84, 14, 'ch01 / 02')
        p.txt(244, 14, 'byp')
        p.txt(306, 14, 'x del', 'muted', 'right')
    return p


@lru_cache(None)
def source():
    """Synthetic plucked/noisy input, generated only for this illustration."""
    samples = []
    seed = 0x531ac
    onsets = (.045, .244, .410, .590, .786, .925)
    for i in range(round(SPAN*32000)):
        t = i/32000
        seed = (seed*1664525 + 1013904223) & 0xffffffff
        noise = (seed >> 16)/32767.5 - 1
        sample = 0.
        for j, start in enumerate(onsets):
            age = t-start
            if 0 <= age < .18:
                envelope = min(1., age/.0015)*math.exp(-age/(.032+j*.003))
                sample += envelope*(.53*math.sin(math.tau*(92+j*23)*age) + .17*noise)
        samples.append(max(-1., min(1., sample)))
    return samples


@lru_cache(None)
def envelope_bins():
    pcm = source()
    result = []
    for n in range(129):
        values = pcm[n*len(pcm)//129:(n+1)*len(pcm)//129]
        result.append((min(values), max(values)))
    return result


def grains(state):
    if state == 'silent':
        return []
    frozen = state == 'frozen'
    ages = (.135, .010) if frozen else (.144, .144-1/24, .144-2/24, .144-3/24)
    offsets = (-.080, .068, -.019, .108)
    result = []
    for i, age in enumerate(ages):
        start = SPAN-POSITION+offsets[i]-(0 if frozen else age)
        assert 0 <= start < start+SIZE <= SPAN
        result.append(dict(start=start, end=start+SIZE, head=start+age,
                           phase=age/SIZE, gain=.5-.5*math.cos(math.tau*age/SIZE)))
    return result


def upper(theme, state):
    p = page(theme, True)
    p.txt(386, 64, {'live': 'live', 'frozen': 'frozen', 'silent': 'no input'}[state],
          'accent' if state == 'frozen' else 'muted', 'right')
    nominal = SPAN-POSITION
    # A single sound body. Dots encode only PCM envelope, never voice count.
    # Silence leaves a quiet baseline, with no invented grain activity.
    bins = envelope_bins() if state != 'silent' else [(0., 0.)]*129
    for i, (lo, hi) in enumerate(bins):
        x = 14+round(i*372/128)
        selected = abs(i*SPAN/128-nominal) <= SPRAY
        role = 'accent' if selected and state != 'silent' else 'rule'
        for y in range(132-round(hi*72), 133-round(lo*72), 3):
            p.d.point((x, y), fill=p.c[role])
    for g in grains(state):
        # All read heads share the sound surface: no capsules, lanes or particles.
        x = 14+round(g['head']*372/SPAN)
        color = base.mix(p.im.getpixel((x, 132)), p.c['ink'], round(35+65*g['gain']))
        p.d.line((x, 127, x, 137), fill=color)
    p.txt(14, 228, '-1.024s')
    p.txt(386, 228, 'capture' if state == 'frozen' else 'now', 'muted', 'right')
    return p


def time_control(p, col, name, value, amount, selected=False):
    left, right = (14, 148) if col == 0 else (172, 306)
    role = 'accent' if selected else 'ink'
    p.txt(left, 65, name, role)
    end = number(p, left, 115, value)
    p.txt(end+2, 101, 'ms', 'muted')
    p.line(left, 126, right, 126, 'rule')
    x = left+round(amount*(right-left))
    p.d.polygon(((x, 120), (x+2, 125), (x, 131)), fill=p.c[role])
    p.hit(name+' / proposed horizontal relative drag', (left-6, 56, right+6, 137))


def control(p, slot, name, value, amount):
    x = 48+75*slot
    p.txt(x, 148, name, 'muted', 'center')
    p.line(x, 164, x, 198, 'rule')
    if name == 'pitch':
        p.line(x-3, 181, x+3, 181, 'rule')
    y = 198-round(amount*34)
    # A slender cross-needle, not a knob, box or ornamental track.
    p.d.polygon(((x-6, y), (x+5, y-2), (x+1, y+1)), fill=p.c['ink'])
    p.txt(x, 208, value, 'ink', 'center')
    p.hit(name+' / proposed vertical relative drag', (x-36, 140, x+36, 220))


def lower(theme, state):
    p = page(theme, False)
    frozen = state == 'frozen'
    p.txt(160, 37, 'freeze / '+('on' if frozen else 'off'),
          'accent' if frozen else 'muted', 'center')
    p.line(118, 50, 202, 50, 'accent' if frozen else 'rule')
    p.hit('freeze / proposed capture-only latch', (108, 28, 212, 56))
    time_control(p, 0, 'position', '480', .53)
    time_control(p, 1, 'size', '160', .50, selected=True)
    # Freeze must not silently change rate, sync, mix or the control layout.
    control(p, 0, 'rate', '24/s', .39)
    control(p, 1, 'spray', '120ms', .47)
    control(p, 2, 'pitch', '+0st', .5)
    control(p, 3, 'mix', '50%', .5)
    p.txt(14, 228, 'b back')
    p.txt(306, 228, 'l: mod >', 'accent', 'right')
    return p


def dual(top, bottom):
    image = Image.new('RGB', (400,496), OUTSIDE)
    image.paste(top.im, (0,0)); image.paste(bottom.im, (40,256))
    return image


def save(out, name, image, scale=2):
    path = out/name
    assert not path.exists()
    resized = image.resize((image.width*scale, image.height*scale), Image.Resampling.NEAREST)
    assert resized.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    resized.save(path, pnginfo=meta)
    return dict(path=str(path.relative_to(ROOT)), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    out = ROOT/'ui_mockups/swarm_v2_study'/datetime.now(timezone.utc).strftime('minimal-%Y%m%dT%H%M%S_%fZ')
    before = base.protected()
    layouts, pictures = {}, {}
    for theme in THEMES:
        for state in STATES:
            top, bottom = upper(theme,state), lower(theme,state)
            assert top.im.size == (400, 240) and bottom.im.size == (320, 240)
            assert len(bottom.hits) == 7  # six parameters and one freeze latch
            for surface in (top, bottom):
                for box, label in surface.labels:
                    assert 14 <= box[0] < box[2] <= surface.im.width-14, label
                    assert 8 <= box[1] < box[3] <= 236, label
            assert bottom.im.crop((0, 56, 320, 240)).tobytes() == lower(theme, 'live').im.crop((0, 56, 320, 240)).tobytes()
            layouts[theme+'-'+state] = dict(top_labels=top.labels, bottom_labels=bottom.labels,
                                            proposed_hits=bottom.hits, staged_grains=grains(state))
            pictures[theme+'-'+state] = (top, bottom)
    out.mkdir(parents=True, exist_ok=False)
    outputs = []
    for key, (top,bottom) in pictures.items():
        outputs.append(save(out,key+'-dual.png',dual(top,bottom)))
        outputs.append(save(out,key+'-top-native.png',top.im,1))
        outputs.append(save(out,key+'-bottom-native.png',bottom.im,1))
    board = base.Canvas(848, 566, OUTSIDE)
    board.text(16,12,'swarm / minimal',base.COLORS['graphite']['ink'],scale=2)
    board.text(16,36,'one sound surface / one control page',base.COLORS['graphite']['muted'])
    for i, theme in enumerate(THEMES):
        board.im.paste(dual(*pictures[theme+'-live']), (16+i*416,54))
    board.text(16,554,'design only / not runtime or old 3ds performance proof',base.COLORS['graphite']['muted'])
    outputs.append(save(out,'both-themes.png',board.im))
    strip = base.Canvas(704,306,OUTSIDE)
    strip.text(16,12,'swarm / touch surface',base.COLORS['graphite']['ink'],scale=2)
    for i,theme in enumerate(THEMES):
        x=16+i*352
        strip.text(x,40,theme+' / one page',base.COLORS['graphite']['accent'])
        strip.im.paste(pictures[theme+'-live'][1].im,(x,56))
    outputs.append(save(out,'lower-both-themes.png',strip.im))
    assert before == base.protected(), 'production/assets must remain untouched'
    (out/'proposal.json').write_text(json.dumps(dict(
        status='minimal authored proposal after request for less clutter and a more distinctive lower screen; no runtime approval',
        origin=__doc__, outputs=outputs, layouts=layouts,
        fixture=dict(sample_rate=32000, history_seconds=SPAN, size_seconds=SIZE,
                     position_age_seconds=POSITION, spray_radius_seconds=SPRAY,
                     free_rate=24, sync_proposed_in_this_revision=False,
                     voice_cap_example=VOICE_CAP, hardware_budget_proven=False,
                     waveform='129 exhaustive min/max bins of synthetic PCM, sparse native-pixel stipple',
                     read_markers='staged read-head positions, visibility from staged Hann envelope',
                     silent='input has been zero for a full history and all grain tails have finished'),
        design=dict(freeze='capture only; independent from dry/wet and rate; read heads keep moving',
                    position='480ms age, not negative pitch or sample length; proposed stable time age',
                    lower='two large time readouts with horizontal relative drags, four vertical relative faders',
                    touch='whole parameter cell is a proposed target, not just the thin needle',
                    omitted_from_proposal=['separate grain transport', 'more page', 'free/sync toolbar',
                        'step/fine/zero toolbar', 'reverse/pitch-env/cluster/shape candidates',
                        'grain lanes/capsules', 'voice counters', 'duplicated hero value', 'technical footnotes'],
                    existing_features='no production parameter removed; omitted advanced workflows need explicit design before implementation',
                    typography='exact current57x12 title at14,8; native6x8; unchanged note numeral masks',
                    frame='native400x240/320x240,14px sides,metadata14,footer228'),
        checks=dict(authored_dual_layouts=len(layouts), text_cell_overlap_and_frame_bounds=True,
                    six_controls_plus_freeze=True, lower_parameter_pixels_identical_across_states=True,
                    proposed_body_hit_nonoverlap=True, production_asset_hashes_unchanged=len(before)),
        limitations=['Pillow concept, not C++ capture, Clouds port, audio rendering or measured telemetry',
                     'no native touch/controller, lens/lock/serialization or compatibility implementation',
                     'no ARM build, FTP, device timing, FPS, listening or old3DS acceptance',
                     'voice cap and range laws need real old3DS prototype before implementation'],
        protected_sha256=before), indent=2)+'\n')
    print(out.relative_to(ROOT))
    print('authored layouts:',len(layouts),'unchanged production/assets:',len(before))
    print(out/'both-themes.png')
    print(out/'lower-both-themes.png')


if __name__ == '__main__':
    main()
