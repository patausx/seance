#!/usr/bin/env python3
"""One static PLAY rethink requested after device feedback; NOT running UI.

New native/2x pair only. Reuses shipping Pink/text/heading assets and the earlier
in-memory CC0 fixture. No source WAV, runtime, older concept or binary is changed.
"""
from PIL import Image
from render_play_editor_study import (
    ROOT, SR, DURATION, INK, SPOT, PAPER, CELL, QUIET, RAIL,
    blend, paper, heading, fixture, px,
)
from render_play_editor_final import digit

OUT = ROOT / 'ui_mockups/play_surface_open_study'  # preserve the earlier boxed PNGs
FULL = (0, DURATION)
VIEW = (28000, 60000)       # 0.875..1.875 seconds; 4x
KEEP = (32000, 56000)       # 1.000..1.750 seconds; crop preview, not committed


def wave(c, pcm, domain, x, y, width, height):
    """Exhaustive source min/max; selection tint, no invented audio trace."""
    a, b = (px(t, domain, x, width) for t in KEEP)
    # Selection lives in waveform ink, not in a filled rectangular container.
    zero, amp = y + height // 2, height // 2 - 5
    c.d.line((x, zero, x + width - 1, zero), fill=RAIL)
    for col in range(width):
        start = domain[0] + col * (domain[1] - domain[0]) // width
        end = domain[0] + (col + 1) * (domain[1] - domain[0]) // width
        low, high = min(pcm[start:end]), max(pcm[start:end])
        color = INK if KEEP[0] <= (start + end) // 2 < KEEP[1] else blend(QUIET, PAPER, 35)
        c.d.line((x + col, zero - round(high * amp / 32768),
                  x + col, zero - round(low * amp / 32768)), fill=color)
    for at in (a, b):
        c.d.line((at, y, at, y + height - 1), fill=SPOT)
    return a, b


def top(pcm):
    c = paper(400)
    heading(c, 20, 12)
    c.text(91, 17, 'cc0_break.wav', INK)
    c.text(91, 33, '4.000s / mono', QUIET)
    c.text(380, 22, 'ch01 / 01', QUIET, right=True)

    x, w, y, h = 20, 360, 72, 78
    va, vb = (px(t, FULL, x, w) for t in VIEW)
    c.text(20, 55, 'source', QUIET)
    c.d.line((va, 66, va, 61, vb, 61, vb, 66), fill=QUIET)
    c.text((va + vb - 12) // 2, 49, '4x', SPOT)
    wave(c, pcm, FULL, x, y, w, h)
    for second in range(5):
        at = px(second * SR, FULL, x, w)
        c.d.line((at, 155, at, 158), fill=RAIL)
        c.text(min(at, 368), 162, f'{second}s', QUIET)

    # Native note-display numerals, never a downloaded substitute font.
    c.text(20, 182, 'keep', SPOT)
    at = 20
    for char in '0.750':
        if char == '.':
            c.d.rectangle((at, 215, at + 3, 219), fill=INK)
            at += 10
        else:
            at += digit(c, char, at, 220) + 3
    c.text(at + 3, 212, 's', QUIET)
    c.text(213, 186, 'start  1.000s', INK)
    c.text(213, 202, 'end    1.750s', INK)
    c.d.line((20, 228, 379, 228), fill=RAIL)
    c.text(20, 232, 'cut keeps selection', SPOT)
    c.text(380, 232, 'source wav preserved', QUIET, right=True)
    return c.im


def handle(c, at, label):
    # The proposed touch target stays generous; visible ink is only a label
    # and a small pointer, not a badge. Hit testing is not implemented here.
    c.text(at - len(label) * 3, 90, label, SPOT)
    c.d.polygon(((at - 2, 101), (at + 2, 101), (at, 104)), fill=SPOT)


def bottom(pcm):
    c = paper(320)
    heading(c, 12, 7)
    c.text(81, 17, 'cc0_break.wav >', INK)
    c.text(308, 17, 'menu', QUIET, right=True)

    # Direct touch/drag fields; no playback/source submenu.
    c.text(12, 44, 'speed', QUIET)
    c.text(49, 44, '1.00x', INK)
    c.text(109, 44, 'gain', QUIET)
    c.text(141, 44, '0.0db', INK)
    c.text(308, 44, 'one shot v', INK, right=True)
    c.d.line((12, 60, 307, 60), fill=RAIL)

    c.text(12, 70, 'trim', SPOT)
    c.d.line((12, 81, 35, 81), fill=SPOT, width=2)
    c.text(66, 70, 'slices', QUIET)
    c.text(156, 70, 'snap:zero', QUIET)
    c.text(308, 70, 'undo', INK, right=True)

    a, b = wave(c, pcm, VIEW, 12, 104, 296, 76)
    handle(c, a, 'start')
    handle(c, b, 'end')

    # Position slider, not another gain/zoom amount control. Thumb occupies
    # exactly the visible fraction of the entire source and has its own lane.
    c.text(19, 194, '-', QUIET)
    rail_x, rail_w = 49, 177
    c.d.line((rail_x, 198, rail_x + rail_w - 1, 198), fill=RAIL)
    va, vb = (px(t, FULL, rail_x, rail_w) for t in VIEW)
    assert abs((vb - va) / (rail_w - 1) - .25) < .01
    c.d.line((va, 198, vb, 198), fill=SPOT, width=3)
    c.text(247, 194, '4x', SPOT)
    c.text(296, 194, '+', INK)

    c.d.line((12, 213, 307, 213), fill=RAIL)
    c.d.polygon(((15, 224), (15, 232), (21, 228)), fill=SPOT)
    c.text(30, 224, 'listen', SPOT)
    c.text(115, 224, 'keep 0.750s', QUIET)
    c.text(266, 224, 'cut', SPOT)
    c.d.line((266, 236, 283, 236), fill=SPOT)
    return c.im


def main():
    assert VIEW[0] < KEEP[0] < KEEP[1] < VIEW[1]
    assert DURATION == 4 * (VIEW[1] - VIEW[0])
    assert KEEP[1] - KEEP[0] == SR * 3 // 4
    pcm = fixture()
    upper, lower = top(pcm), bottom(pcm)
    assert upper.size == (400, 240) and lower.size == (320, 240)
    dual = Image.new('RGB', (400, 488), (24, 20, 29))
    dual.paste(upper, (0, 0))
    dual.paste(lower, (40, 248))
    OUT.mkdir(parents=True, exist_ok=True)
    dual.save(OUT / 'play_surface_native.png')
    dual.resize((800, 976), Image.Resampling.NEAREST).save(OUT / 'play_surface_2x.png')
    print('static proposal only; label bounds/spacing, source geometry and proportional thumb checked')
    print(OUT / 'play_surface_2x.png')


if __name__ == '__main__':
    main()
