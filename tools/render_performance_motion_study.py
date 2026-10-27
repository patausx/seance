#!/usr/bin/env python3
"""Authored 18s motion study; no runtime, audio capture or gesture implementation.

Exact paper() from the approved centered-paper reference, at 10Hz. UI at 20Hz.
Only one portrait GIF is written. A fixed palette prevents text/rail flicker.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_main_screen_centered_motion_study import paper, palette as paper_palette
from render_main_screen_centered_study import canvas
from render_performance_columns_study import ROUTES, LENGTHS, LEVELS, STEPS, columns, center
from render_universal_sigils_study import caption, OUTSIDE
from render_menu_style_study import heading_mask

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/performance_columns_study'
FRAME_MS, FRAMES = 50, 360
PULSE = [tuple(round(a + (b-a)*i/11) for a, b in zip((94, 94, 94), (181, 154, 232)))
         for i in range(12)]
PATTERNS = ((0, 4, 8, 12), (0, 3, 6, 8, 10, 14), (0, 5, 9), ())
EVENTS = []
for chain in range(4):
    hits = []
    for tick in range(144):
        t = tick / 8  # 120 bpm, sixteenth notes.
        if (tick + STEPS[chain]) % (12 if chain == 2 else 16) in PATTERNS[chain]:
            if chain != 2 or 4 <= t < 16:
                hits.append((t, (.78, .64, .57, 0)[chain] * (1 - .09*(tick % 3))))
    EVENTS.append(hits)


def state(t):
    pending = 2.5 <= t < 4 or 14.5 <= t < 16
    muted = not 4 <= t < 16
    queue_start = 2.5 if t < 4 else 14.5
    remaining = (t - queue_start) / 1.5 if pending else 0
    ages, levels = [], []
    for chain in range(4):
        hit = next(((at, amplitude) for at, amplitude in reversed(EVENTS[chain]) if at <= t), None)
        age, amp = (t-hit[0], hit[1]) if hit else (100, 0)
        ages.append(age)
        # Scripted envelope drives the example meter; it is NOT sampled PCM/RMS.
        levels.append(0 if chain == 2 and muted else amp*math.exp(-age/(.17, .30, .45, 1)[chain]))
    return dict(pending=pending, muted=muted, queue=remaining, ages=ages, levels=levels,
                morph=max(0, min(1, (t-4)/8)), running=4 <= t < 12)


def pulse(c, age):
    amount = max(0, 1-age/.15)
    c.p['pulse'] = PULSE[round(amount*11)]
    return amount


def upper(bg, t, s):
    c = canvas(bg.crop((0, 0, 400, 240)))
    c.heading(40, 8, 'perform')
    c.txt(359, 9, '120 bpm / play', 'ink', right=True)
    c.txt(359, 24, f'bank 2 / beat {int(t*2)%4+1}', 'ghost', right=True)
    columns(c, True)
    for i, route in enumerate(ROUTES):
        x, cx = 44+78*i, 81+78*i
        muted = i == 2 and s['muted']
        role = 'spot' if i == 1 else 'ghost' if muted else 'ink'
        digit = str(i+1)
        c.heading(cx-heading_mask(digit).width//2, 47, digit, role)
        if pulse(c, s['ages'][i]) > 0 and not muted:
            c.line((cx-11, 76, cx+11, 76), 'pulse')
        for j, name in enumerate(route):
            y = 83+13*j
            center(c, cx, y, name, 'ghost' if muted else 'ink')
            if j+1 < len(route):
                c.line((cx, y+9, cx, y+11), 'rail')
        step = (STEPS[i]+int(t*8)) % LENGTHS[i]
        center(c, cx, 162, f'{step+1:02}/{LENGTHS[i]:02} >', 'ghost')
        for k in range(64):
            gx, gy = x+6+(k%16)*4, 181+(k//16)*8
            if k == step:
                c.d.rectangle((gx-1, gy-1, gx+2, gy+3), outline=c.p['spot'],
                              fill=None if muted else c.p['spot'])
            else:
                c.d.rectangle((gx, gy+1, gx+1, gy+(2 if k < LENGTHS[i] else 1)),
                              fill=c.p['ghost' if k < LENGTHS[i] else 'rail'])
        label = 'quiet' if i == 3 else 'live'
        if i == 2:
            label = ('on@bar' if muted else 'off@bar') if s['pending'] else 'muted' if muted else 'live'
        center(c, cx, 222, label, 'spot' if i == 2 and (muted or s['pending']) else 'ghost')
        if i == 2:
            age = t-(16 if t >= 16 else 4)
            if 0 <= age < .2:
                pulse(c, age)
                c.line((cx-19, 234, cx+19, 234), 'pulse')
    return c.im


def lower(bg, t, s, peaks):
    c = canvas(bg.crop((40, 256, 360, 496)))
    columns(c, False)
    c.txt(10, 7, 'mixer', 'spot')
    c.line((10, 20, 38, 20), 'spot')
    c.txt(310, 7, 'transition >', 'ghost', right=True)
    for i, position in enumerate(LEVELS):
        x, cx = 4+78*i, 41+78*i
        muted = i == 2 and s['muted']
        amount = pulse(c, s['ages'][i]) if not muted else 0
        center(c, cx, 32, 'trig', 'spot' if i == 1 else 'ghost')
        if amount > 0 or i == 1:
            c.line((cx-11, 43, cx+10, 43), 'pulse' if amount > 0 else 'spot')
        db = 20*math.log10(position*position)
        center(c, cx, 54, f'{db:.1f}db', 'ghost' if muted else 'ink')
        top, bottom, rail_x = 71, 154, cx-6
        thumb = bottom-round(position*83)
        c.line((rail_x, top, rail_x, bottom), 'rail')
        for k in range(5):
            y = bottom-round(k*83/4)
            c.line((rail_x-12, y, rail_x-8, y), 'rail')
        c.line((rail_x, bottom, rail_x, thumb), 'ghost' if muted else 'spot')
        fill = c.p['ghost' if muted else 'spot' if i == 1 else 'ink']
        c.d.rectangle((rail_x-14, thumb-4, rail_x+14, thumb+5), fill=fill)
        c.d.line((rail_x-10, thumb, rail_x+10, thumb), fill=(36, 36, 36))
        meter_x = x+64
        c.line((meter_x, top, meter_x, bottom), 'rail')
        height = round(s['levels'][i]*83)
        if height:
            c.d.rectangle((meter_x-1, bottom-height, meter_x+1, bottom), fill=c.p['spot'])
        if peaks[i] > 0:
            y = bottom-round(peaks[i]*83)
            c.line((meter_x-4, y, meter_x+4, y), 'ghost' if muted else 'ink')
        if i == 2 and s['pending']:
            for px in range(x+5, x+71, 6):
                c.line((px, 165, min(px+2, x+70), 165), 'spot')
            c.line((x+5, 183, x+70, 183), 'rail')
            c.line((x+5, 183, x+5+round(65*s['queue']), 183), 'spot')
            center(c, cx, 171, 'cancel', 'spot')
        elif muted:
            c.d.rectangle((x+4, 165, x+71, 183), fill=c.p['spot'])
            center(c, cx, 171, 'muted', 'inverse')
        else:
            center(c, cx, 171, 'mute', 'ghost')
            c.line((x+9, 183, x+66, 183), 'rail')
    c.line((8, 188, 311, 188), 'rail')
    c.txt(8, 195, 'mute bar', 'spot')
    c.chevron(64, 199, 'spot')
    c.txt(124, 195, 'a>b 4 bars', 'ink')
    c.chevron(193, 199, 'ghost')
    action = 'pause' if s['running'] else 'done' if t >= 12 else 'go >'
    c.txt(311, 195, action, 'spot', right=True)
    center(c, 160, 213, f"morph {round(s['morph']*100)}%", 'spot')
    for x, label in ((8, 'a set'), (282, 'b set')):
        c.txt(x, 213, label, 'spot')
        c.txt(x+3, 227, 'hold', 'ghost')
    c.line((58, 230, 260, 230), 'rail')
    for x in (58, 109, 159, 210, 260):
        c.line((x, 227, x, 233), 'rail')
    thumb = 58+round(202*s['morph'])
    c.d.rectangle((thumb-2, 225, thumb+2, 235), fill=c.p['spot'])
    return c.im


def palette():
    raw = paper_palette().getpalette()
    colors = list(dict.fromkeys(tuple(raw[i:i+3]) for i in range(0, len(raw), 3)))
    colors = list(dict.fromkeys(colors + PULSE + [(41, 38, 46)]))
    assert len(colors) <= 256
    colors.extend([OUTSIDE]*(256-len(colors)))
    pal = Image.new('P', (1, 1))
    pal.putpalette([v for color in colors for v in color])
    return pal


def main():
    pal, frames = palette(), []
    peaks, held_until = [0.]*4, [0.]*4
    bg = None
    for frame in range(FRAMES):
        t = frame*FRAME_MS/1000
        if frame % 2 == 0:
            bg = paper(frame/FRAMES)  # Identical approved 18s field, held at 10Hz.
        s = state(t)
        for i, value in enumerate(s['levels']):
            if value >= peaks[i] and value > 0:
                peaks[i], held_until[i] = value, t+1
            elif t >= held_until[i]:
                peaks[i] = max(value, peaks[i]-.5*FRAME_MS/1000)
        sheet = Image.new('RGB', (432, 590), OUTSIDE)
        caption(sheet, 16, 12, 'perform / living paper + restrained motion')
        caption(sheet, 16, 29, 'same layout / steady labels and fader positions')
        sheet.paste(upper(bg, t, s), (16, 48))
        sheet.paste(lower(bg, t, s, peaks), (56, 304))
        if s['pending']:
            note = 'demo / chain 3 queued ' + ('on' if s['muted'] else 'off') + ' for the next bar'
        elif s['running']:
            note = 'demo / chain 3 live + timed sound a>b'
        elif t >= 12:
            note = 'demo / sound b reached / fader levels preserved'
        else:
            note = 'demo / short trigger accents + peak memory'
        caption(sheet, 16, 557, note)
        caption(sheet, 16, 575, 'authored motion / no audio / not a runtime capture')
        frames.append(sheet.quantize(palette=pal, dither=Image.Dither.NONE)
                      .resize((864, 1180), Image.Resampling.NEAREST))
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'perform-motion-v2-2x.gif'
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=FRAME_MS,
                   loop=0, disposal=1, optimize=False)
    with Image.open(path) as result:
        assert result.n_frames == FRAMES and result.size == (864, 1180)
        assert result.info['duration'] == FRAME_MS and result.info['loop'] == 0
    print(f'{path.relative_to(ROOT)} / 18s / {FRAMES} frames / {path.stat().st_size} bytes')
    print('native text bounds/nonoverlap and GIF dimensions/frame count passed')


if __name__ == '__main__':
    main()
