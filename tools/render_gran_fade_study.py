#!/usr/bin/env python3
"""Authored motion study, NOT running GRAN telemetry or captured DSP output.

Reuses the approved study's controls, font, inspector and real source waveform.
Only grain presentation changes: shallow windows and envelope-driven alpha.
The original tall-contour render remains available for comparison.
"""
import json
from PIL import Image, ImageDraw
import render_gran_classic_study as base
from render_menu_style_study import paper, GHOST, BRANCH
from render_module_geometry_study import INK, SPOT, OUTSIDE

OUT = base.ROOT / 'ui_mockups/gran_fade_study'
CYCLE_MS, SIZE_MS, STEP_MS = 800, 400, 20
# Staged launch sequence, 10/s, eight possible launch positions, not engine data.
OFFSETS = (-.30, -.07, .28, -.22, .10, .34, -.14, .21)


def envelope(age_ms):
    if age_ms < 0 or age_ms >= SIZE_MS:
        return 0.
    return min(1., age_ms/100., (SIZE_MS-age_ms)/100.)


def voices(time_ms):
    result = []
    for i, offset in enumerate(OFFSETS):
        age = (time_ms-i*100) % CYCLE_MS
        if age < SIZE_MS:
            result.append((offset, age))
    assert len(result) == 4
    return sorted(result, key=lambda v: -v[1])  # oldest below newer grains


def backdrop(page, extrema, duration):
    # The classic study's plot is isolated from labels/controls. Restore exactly
    # this paper region, retain timestamps and every control/font pixel outside.
    box = (0, 76, 320, 149)
    page.im.paste(paper(320).crop(box), box[:2])
    page.d = ImageDraw.Draw(page.im)
    x0, x1, zero, amp = base.X0, base.X1, base.ZERO, base.AMP
    def x(t):
        return round(x0+(x1-x0)*(t-duration/4)/(duration/2))
    pos = duration/2
    left, middle, right = x(pos-.4), x(pos), x(pos+.4)
    # One quiet spawn-range bracket, not another enclosing grain rectangle.
    page.line((left, 80, right, 80), BRANCH)
    page.line((left, 80, left, 82), BRANCH)
    page.line((right, 80, right, 82), BRANCH)
    page.d.polygon(((middle-3, 76), (middle+3, 76), (middle, 79)), fill=INK)
    page.line((x0, zero, x1, zero), BRANCH)
    for i, (low, high) in enumerate(extrema):
        top, bottom = zero-round(high*amp/32768), zero-round(low*amp/32768)
        page.line((x0+i, top, x0+i, bottom), INK if left <= x0+i <= right else GHOST)
    for xx in (x0, middle, x1):
        page.line((xx, 145, xx, 147), BRANCH)
    return page.im.copy()


def moving(still, duration, time_ms):
    image = still.convert('RGBA')
    def x(t):
        return round(base.X0+(base.X1-base.X0)*(t-duration/4)/(duration/2))
    for offset, age in voices(time_ms):
        opacity = round(190*envelope(age))
        if opacity == 0:
            continue
        start = duration/2+offset
        a, b, head = x(start), x(start+.4), x(start+age/1000)
        overlay = Image.new('RGBA', image.size)
        d = ImageDraw.Draw(overlay)
        color = (*SPOT, opacity)
        # 12px high instead of the old 57px curtain. Source-time widths and
        # 25% linear attack/release proportions stay the same across all grains.
        d.line((a, 102, x(start+.1), 90, x(start+.3), 90, b, 102), fill=color)
        # Small actual-position-style cursor, no full-height vertical fence.
        d.line((head, 102, head, 122), fill=color)
        d.polygon(((head-2, 101), (head+2, 101), (head, 104)), fill=color)
        image = Image.alpha_composite(image, overlay)
    return image.convert('RGB')


def main():
    before = base.identity()
    extrema, duration, _, _ = base.source()
    OUT.mkdir(parents=True, exist_ok=True)
    old_lower = base.lower('grain', extrema, duration)
    old_image = old_lower.im.copy()
    still = backdrop(old_lower, extrema, duration)
    # All visible labels, controls, timestamps and exact native font pixels stay
    # unchanged; only the explicitly owned plot region is replaced.
    for box in ((0, 0, 320, 76), (0, 149, 320, 240)):
        assert old_image.crop(box).tobytes() == still.crop(box).tobytes()
    assert [round(190*envelope(t)) for t in (0, 50, 100, 200, 300, 350, 400)] == [0, 95, 190, 190, 190, 95, 0]
    frames = []
    for t in range(0, CYCLE_MS, STEP_MS):
        frame = moving(still, duration, t)
        # Top-of-wave marker stays bounded, no stale trail accumulator: each
        # frame starts from the same source, never the previous composed frame.
        assert frame.crop((0,149,320,240)).tobytes() == still.crop((0,149,320,240)).tobytes()
        frames.append(frame.resize((640,480), Image.Resampling.NEAREST))
    assert moving(still,duration,0).tobytes() == moving(still,duration,CYCLE_MS).tobytes()
    frames[0].save(OUT/'grain_fade_bottom_2x.gif', save_all=True,
                   append_images=frames[1:], duration=STEP_MS, loop=0,
                   disposal=2, optimize=False)
    with Image.open(OUT/'grain_fade_bottom_2x.gif') as gif:
        assert gif.n_frames == len(frames) and gif.size == (640,480)
    selected = moving(still,duration,360)
    selected.save(OUT/'grain_fade_bottom_native.png')
    selected.resize((640,480),Image.Resampling.NEAREST).save(OUT/'grain_fade_bottom_2x.png')
    pair = Image.new('RGB',(400,480),OUTSIDE)
    pair.paste(base.upper('grain').im,(0,0)); pair.paste(selected,(40,240))
    pair.resize((800,960),Image.Resampling.NEAREST).save(OUT/'grain_fade_dual_2x.png')
    assert base.identity() == before
    receipt = dict(
        origin='authored Pillow motion study, NOT runtime/hardware/DSP capture',
        inherited_waveform='same real dark_atmos WAV; centered source zoom x2; fixed +/- full scale',
        voice_count=4, voice_limit=8, density_per_second=10,
        size_ms=SIZE_MS, attack_ms=100, release_ms=100,
        jitter_percent=0, pitch_deviation_st=0, speed=1,
        appearance='12px shallow trapezoids; 20px read cursors; alpha 0..190/255 follows staged linear attack/release',
        spawn_spread='one quiet bracket instead of an enclosing dotted rectangle',
        source_time_bounds='same projected source widths; no reduction of sound density',
        gif=dict(frames=len(frames), frame_ms=STEP_MS, loop_ms=CYCLE_MS, pixels=[640,480]),
        checks=['same runtime/sample/font/artifact identities', 'exact unchanged pixels outside plot',
                'envelope birth/attack/sustain/release/death endpoints', 'four staged voices in each frame',
                'no accumulated trails', 'exact loop seam', 'GIF frame count and size'],
        limits=['illustrative voices only, not actual engine telemetry', 'no runtime or DSP changes',
                'opacity is UI-only; no new audio envelope', 'no ARM or deployment',
                'runtime must use each grain own latched envelope, not current knob values'],
        preserved_sha256=before)
    (OUT/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('pass: 40-frame looping visual proposal; unchanged controls/WAV/runtime; four staged fading voices')
    print(OUT/'grain_fade_bottom_2x.gif')


if __name__ == '__main__':
    main()
