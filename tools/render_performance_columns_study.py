#!/usr/bin/env python3
"""One authored performance concept sheet, not runtime or hardware telemetry.

Native 400x240 / 320x240; existing Gossip28 + 6x8, nearest 2x.
Only example states. Existing transition FX stay on their separate page.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_universal_sigils_study import caption, OUTSIDE
from render_main_screen_tiles_study import surface
from render_menu_style_study import heading_mask, overlaps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/performance_columns_study'
ROUTES = (('play', 'filt', 'comp'), ('arp', 'fm', 'dly'),
          ('bird', 'fold', 'rev'), ('play', 'fract', 'dly'))
LENGTHS = (16, 32, 12, 64)
STEPS = (4, 20, 8, 36)
LEVELS = (.88, .73, .81, .63)  # Native quadratic fader travel, not signal peaks.
PEAKS = (.61, .42, 0, 0)  # Illustrative post-volume observations only.


def center(c, x, y, text, role='ink'):
    c.txt(x - len(text) * 3, y, text, role)


def paper(width):
    c = surface(width)
    # Quiet fixed paper, shared coordinates across the hinge; not animated.
    for y in range(240):
        gy = y + (256 if width == 320 else 0)
        for x in range(width):
            gx = x + (40 if width == 320 else 0)
            value = 35 + round(1.2 * math.sin(gx / 91 + gy / 133)
                               + .7 * math.cos(gy / 107 - gx / 170))
            c.im.putpixel((x, y), (value, value, value))
    c.d = ImageDraw.Draw(c.im)
    return c


def columns(c, top):
    offset = 40 if top else 0
    y0, y1 = (39, 239) if top else (0, 184)
    for i in range(4):
        x = offset + 4 + 78 * i
        if i == 1:
            c.d.rectangle((x + 1, y0, x + 74, y1), fill=(41, 38, 46))
        role = 'spot' if i == 1 else 'rail'
        c.line((x, y0, x, y1), role)
        c.line((x + 75, y0, x + 75, y1), role)
        if top:
            c.line((x, y0, x + 75, y0), role)


def upper(playing_transition):
    c = paper(400)
    c.heading(40, 8, 'perform')
    c.txt(359, 9, '124 bpm / play', 'ink', right=True)
    c.txt(359, 24, 'bank 2 / beat 3', 'ghost', right=True)
    columns(c, True)
    for i, route in enumerate(ROUTES):
        x, cx = 44 + 78 * i, 81 + 78 * i
        role = 'spot' if i == 1 else 'ghost' if i == 2 else 'ink'
        digit = str(i + 1)
        c.heading(cx - heading_mask(digit).width // 2, 47, digit, role)
        # Actual route order in the example, with room for all six modules.
        for j, name in enumerate(route):
            y = 83 + 13 * j
            center(c, cx, y, name, 'ghost' if i == 2 else 'ink')
            if j + 1 < len(route):
                c.line((cx, y + 9, cx, y + 11), 'rail')
        center(c, cx, 162, f'{STEPS[i] + 1:02}/{LENGTHS[i]:02} >', 'ghost')
        # Fixed 64-step ruler in four rows; not a fabricated note-on pattern.
        # Hollow mark = playback position retained while this chain is muted.
        for s in range(64):
            gx, gy = x + 6 + (s % 16) * 4, 181 + (s // 16) * 8
            if s == STEPS[i]:
                c.d.rectangle((gx - 1, gy - 1, gx + 2, gy + 3),
                              outline=c.p['spot'],
                              fill=None if i == 2 else c.p['spot'])
            else:
                color = c.p['ghost' if s < LENGTHS[i] else 'rail']
                c.d.rectangle((gx, gy + 1, gx + 1, gy + (2 if s < LENGTHS[i] else 1)), fill=color)
        state = ('live', 'live', 'on@bar' if playing_transition else 'muted', 'quiet')[i]
        center(c, cx, 222, state, 'spot' if i == 2 else 'ghost')
        if i == 1:
            # Single sampled trigger accent, not an audio waveform.
            c.d.rectangle((cx - 2, 211, cx + 2, 213), fill=c.p['spot'])
    return c.im


def lower(playing_transition):
    c = paper(320)
    columns(c, False)
    c.txt(10, 7, 'mixer', 'spot')
    c.line((10, 20, 38, 20), 'spot')
    c.txt(310, 7, 'transition >', 'ghost', right=True)
    for i, position in enumerate(LEVELS):
        x, cx = 4 + 78 * i, 41 + 78 * i
        muted = i == 2
        center(c, cx, 32, 'trig', 'spot' if i == 1 else 'ghost')
        if i == 1:
            c.line((cx - 11, 43, cx + 10, 43), 'spot')
        db = 20 * math.log10(position * position)
        center(c, cx, 54, f'{db:.1f}db', 'ghost' if muted else 'ink')
        # Same 83px travel as the runtime mixer, not smaller ornamental sliders.
        top, bottom = 71, 154
        rail_x = cx - 6
        thumb = bottom - round(position * (bottom - top))
        c.line((rail_x, top, rail_x, bottom), 'rail')
        for t in range(5):
            y = bottom - round(t * (bottom - top) / 4)
            c.line((rail_x - 12, y, rail_x - 8, y), 'rail')
        c.line((rail_x, bottom, rail_x, thumb), 'ghost' if muted else 'spot')
        fill = c.p['ghost' if muted else 'spot' if i == 1 else 'ink']
        c.d.rectangle((rail_x - 14, thumb - 4, rail_x + 14, thumb + 5), fill=fill)
        c.d.line((rail_x - 10, thumb, rail_x + 10, thumb), fill=(36, 36, 36))
        meter_x = x + 64
        c.line((meter_x, top, meter_x, bottom), 'rail')
        peak = PEAKS[i] + (.1 if playing_transition and i == 1 else 0)
        if peak:
            c.d.rectangle((meter_x - 1, bottom - round(peak * 83), meter_x + 1, bottom), fill=c.p['spot'])
        remembered = (.77, .66, .23, 0)[i]
        if remembered:
            y = bottom - round(remembered * 83)
            c.line((meter_x - 4, y, meter_x + 4, y), 'ghost' if muted else 'ink')
        if muted:
            if playing_transition:
                # Still muted; an outlined/pending action must not look applied.
                for px in range(x + 5, x + 71, 6):
                    c.line((px, 165, min(px + 2, x + 70), 165), 'spot')
                    c.line((px, 183, min(px + 2, x + 70), 183), 'spot')
                center(c, cx, 171, 'cancel', 'spot')
            else:
                c.d.rectangle((x + 4, 165, x + 71, 183), fill=c.p['spot'])
                center(c, cx, 171, 'muted', 'inverse')
        else:
            center(c, cx, 171, 'mute', 'ghost')
            c.line((x + 9, 183, x + 66, 183), 'rail')
    c.line((8, 188, 311, 188), 'rail')
    c.txt(8, 195, 'mute bar', 'spot')
    c.chevron(64, 199, 'spot')
    c.txt(124, 195, 'a>b 4 bars', 'ink')
    c.chevron(193, 199, 'ghost')
    c.txt(311, 195, 'pause' if playing_transition else 'go >', 'spot', right=True)
    pos = .42 if playing_transition else 0
    center(c, 160, 213, f'morph {round(pos * 100)}%', 'spot')
    for x, label in ((8, 'a set'), (282, 'b set')):
        c.txt(x, 213, label, 'spot')
        c.txt(x + 3, 227, 'hold', 'ghost')
    c.line((58, 230, 260, 230), 'rail')
    for x in (58, 109, 159, 210, 260):
        c.line((x, 227, x, 233), 'rail')
    thumb = 58 + round(202 * pos)
    c.d.rectangle((thumb - 2, 225, thumb + 2, 235), fill=c.p['spot'])
    # Coarse proposed targets only; this is not runtime touch routing.
    hits = [('transition', (220, 0, 320, 25))]
    for i in range(4):
        x = 4 + 78 * i
        hits += [(f'trig{i}', (x, 26, x + 75, 50)),
                 (f'fader{i}', (x, 50, x + 75, 163)),
                 (f'mute{i}', (x, 163, x + 75, 187))]
    hits += [('quantize', (0, 188, 106, 210)),
             ('duration', (112, 188, 218, 210)),
             ('auto', (254, 188, 320, 210)),
             ('a', (0, 210, 46, 240)), ('b', (276, 210, 320, 240)),
             ('morph', (50, 210, 272, 240))]
    for k, (name, box) in enumerate(hits):
        assert 0 <= box[0] < box[2] <= 320 and 0 <= box[1] < box[3] <= 240, name
        assert all(not overlaps(box, other) for _, other in hits[:k]), name
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (864, 620), OUTSIDE)
    caption(sheet, 16, 14, 'seance / perform / four continuous strips')
    for x, active, title in ((16, False, '01 / mixer / chain 3 muted'),
                              (448, True, '02 / chain 3 queued on / timed a>b')):
        caption(sheet, x, 43, title)
        sheet.paste(upper(active), (x, 64))
        sheet.paste(lower(active), (x + 40, 320))
    caption(sheet, 16, 579, 'tap trig / proposed hold solo / mute now, step or bar / auto a>b: 1,2,4,8 bars')
    caption(sheet, 16, 599, 'static concept / example states, not telemetry / native screens / no runtime changes')
    path = OUT / 'perform-columns-v1-2x.png'
    sheet.resize((1728, 1240), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))
    print('one sheet; native text and proposed target bounds/nonoverlap passed')


if __name__ == '__main__':
    main()
