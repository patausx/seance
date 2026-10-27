#!/usr/bin/env python3
"""Final static PLAY cuts proposal. NOT C++ UI, audio output or device capture.

Reuses the first study's native fonts, Pink base paper and CC0 PCM fixture.
Only exports this new composition at native and 2x, preserving the first study.
See docs/play-editor-final.md for gesture intent and unimplemented boundaries.
"""
import re
from PIL import Image
from render_play_editor_study import (
    ROOT, SR, DURATION, INK, SPOT, PAPER, CELL, QUIET, RAIL,
    blend, paper, heading, fixture, px,
)

OUT = ROOT / 'ui_mockups/play_editor_final'
CUTS = tuple(i * SR // 2 for i in range(8))
SELECTED = 3                        # stable cut ID 04, not a reordered display ID
WINDOW = (CUTS[SELECTED], CUTS[SELECTED + 1])
VIEW = (40000, 72000)               # 1.250..2.250s, exact 4x of four seconds
CURSOR = 55040                      # illustrative audition at 1.720s, not live data
FULL = (0, DURATION)


def digit(c, char, x, baseline):
    """Unscaled shipped Seance Note numeric mask, original 56ppem raster."""
    source = (ROOT / 'platform/3ds/note_display_data.h').read_text()
    match = re.search(r"\{'" + char + r"', false, (\d+), (\d+), (\d+), (-?\d+)\}", source)
    offset, width, height, top = map(int, match.groups())
    data = bytes(int(v, 16) for v in re.findall(
        r'0x([0-9a-f]{2})', source.split('bitmap[] = {', 1)[1].split('};', 1)[0]))
    mask = Image.new('L', (width, height))
    stride = (width + 7) // 8
    for y in range(height):
        for xx in range(width):
            if data[offset + y * stride + xx // 8] & (0x80 >> (xx % 8)):
                mask.putpixel((xx, y), 255)
    box = (x, baseline + top, x + width, baseline + top + height)
    assert 0 <= box[0] < box[2] <= c.im.width and 0 <= box[1] < box[3] <= c.im.height
    for old, label in c.labels:
        assert min(box[2], old[2]) <= max(box[0], old[0]) or min(box[3], old[3]) <= max(box[1], old[1]), label
    c.labels.append((box, 'native digit ' + char))
    c.im.paste(INK, (box[0], box[1]), mask)
    return width


def waveform(c, pcm, domain, x, y, width, height):
    """True extrema of every source frame in each bin. Same fixed scale in both views."""
    zero, amplitude = y + height // 2, height // 2 - 5
    c.d.line((x, zero, x + width - 1, zero), fill=RAIL)
    for col in range(width):
        a = domain[0] + col * (domain[1] - domain[0]) // width
        b = domain[0] + (col + 1) * (domain[1] - domain[0]) // width
        low, high = min(pcm[a:b]), max(pcm[a:b])
        selected = WINDOW[0] <= (a + b) // 2 < WINDOW[1]
        color = INK if selected else blend(QUIET, PAPER, 16)
        c.d.line((x + col, zero - round(high * amplitude / 32768),
                  x + col, zero - round(low * amplitude / 32768)), fill=color)


def playhead(c, x, y, height):
    c.d.polygon(((x - 3, y), (x + 3, y), (x, y + 4)), fill=SPOT)
    for yy in range(y + 6, y + height, 5):
        c.d.line((x, yy, x, min(yy + 2, y + height - 1)), fill=SPOT)


def top(pcm):
    c = paper(400)
    heading(c, 20, 12)
    c.text(90, 16, 'cc0_break.wav', INK)
    c.text(90, 32, 's00 / 4.000s / mono', QUIET)
    c.text(380, 23, 'ch01 / 01', QUIET, right=True)

    x, y, w, h = 20, 76, 360, 82
    va, vb = (px(t, FULL, x, w) for t in VIEW)
    a, b = (px(t, FULL, x, w) for t in WINDOW)
    c.d.rectangle((va, y, vb, y + h - 1), fill=blend(PAPER, CELL, 33))
    c.d.rectangle((a, y, b, y + h - 1), fill=blend(PAPER, CELL, 68))
    # Quiet bracket is viewport; purple bracket is the selected sounding slice.
    c.d.line((va, 56, va, 51, vb, 51, vb, 56), fill=QUIET)
    c.text((va + vb - 12) // 2, 42, '4x', QUIET)
    for i, boundary in enumerate(CUTS):
        at = px(boundary, FULL, x, w)
        active = i == SELECTED
        c.text(at + 5, 64, f'{i + 1:02}', SPOT if active else QUIET)
        c.d.line((at, y, at, y + h - 1), fill=blend(RAIL, PAPER, 30))
    waveform(c, pcm, FULL, x, y, w, h)
    c.d.line((a, y, a, y + h, b, y + h, b, y), fill=SPOT)
    playhead(c, px(CURSOR, FULL, x, w), y - 2, h + 1)
    for second in range(5):
        at = px(second * SR, FULL, x, w)
        c.d.line((at, 163, at, 166), fill=RAIL)
        c.text(min(at, 368), 170, f'{second}s', QUIET)

    at = 20
    for char in f'{SELECTED + 1:02}':
        at += digit(c, char, at, 215) + 3
    c.text(82, 188, 'selected cut', SPOT)
    c.text(82, 204, '0.500s / forward', INK)
    c.text(250, 188, 'audition / repeat', SPOT)
    c.text(250, 204, 'through chain', QUIET)
    c.d.line((20, 226, 379, 226), fill=RAIL)
    c.text(20, 232, 'shared cuts / 8 of 16', QUIET)
    c.text(380, 232, 'original untouched', QUIET, right=True)
    return c.im


def grip(c, at, label, active):
    # Distinct 24px-high seam lane. The visible grip is 22x23; proposed touch
    # ownership is at least 24px wide, independent of line thickness in the wave.
    poly = ((at, 182), (at + 5, 187), (at + 11, 187), (at + 11, 204),
            (at - 11, 204), (at - 11, 187), (at - 5, 187))
    c.d.polygon(poly, fill=SPOT if active else CELL)
    if not active:
        c.d.line(poly + (poly[0],), fill=RAIL)
    c.text(at - 6, 193, label, CELL if active else QUIET)


def bottom(pcm):
    c = paper(320)
    heading(c, 12, 7)
    for x, label, active in ((84, 'trim', False), (138, 'cuts', True),
                              (192, 'loop', False), (266, 'more', False)):
        c.text(x, 18, label, SPOT if active else QUIET)
        if active:
            c.d.line((x, 31, x + 24, 31), fill=SPOT, width=2)
    # Only cuts tools: no trim-fade handles or playback parameter grid here.
    c.text(12, 45, '+ cut', INK)
    c.text(84, 45, 'auto', QUIET)
    c.text(144, 45, 'live', QUIET)
    c.text(222, 45, '-', QUIET)
    c.text(250, 45, '4x', SPOT)
    c.text(296, 45, '+', SPOT)

    x, y, w, h = 12, 61, 296, 120
    a, b = (px(t, VIEW, x, w) for t in WINDOW)
    c.d.rectangle((a, y, b, y + h - 1), fill=blend(PAPER, CELL, 62))
    waveform(c, pcm, VIEW, x, y, w, h)
    for i, boundary in enumerate(CUTS):
        if VIEW[0] <= boundary <= VIEW[1]:
            at = px(boundary, VIEW, x, w)
            c.d.line((at, y, at, 183), fill=SPOT if i in (SELECTED, SELECTED + 1) else RAIL)
    playhead(c, px(CURSOR, VIEW, x, w), y, h - 1)

    c.d.line((x, 195, x + w - 1, 195), fill=RAIL)
    c.d.line((a, 206, b, 206), fill=SPOT)
    for i, boundary in enumerate(CUTS):
        if VIEW[0] <= boundary <= VIEW[1]:
            grip(c, px(boundary, VIEW, x, w), f'{i + 1:02}', i == SELECTED)

    c.d.line((12, 212, 307, 212), fill=RAIL)
    c.d.polygon(((13, 225), (13, 233), (19, 229)), fill=SPOT)
    c.text(26, 226, 'listen', SPOT)
    c.text(98, 226, 'repeat', SPOT)
    c.d.line((98, 237, 133, 237), fill=SPOT)
    c.text(165, 226, 'stop', QUIET)
    c.text(242, 226, 'to seq >', INK)
    return c.im


def main():
    assert WINDOW == (48000, 64000) and WINDOW[1] - WINDOW[0] == SR // 2
    assert DURATION == 4 * (VIEW[1] - VIEW[0])
    assert VIEW[0] < WINDOW[0] < CURSOR < WINDOW[1] < VIEW[1]
    pcm = fixture()
    upper, lower = top(pcm), bottom(pcm)
    assert upper.size == (400, 240) and lower.size == (320, 240)
    dual = Image.new('RGB', (400, 488), (24, 20, 29))
    dual.paste(upper, (0, 0))
    dual.paste(lower, (40, 248))
    OUT.mkdir(parents=True, exist_ok=True)
    dual.save(OUT / 'play_final_native.png')
    dual.resize((800, 976), Image.Resampling.NEAREST).save(OUT / 'play_final_2x.png')
    print('static cuts concept; native label bounds/spacing and shared timeline assertions passed')
    print(OUT / 'play_final_2x.png')


if __name__ == '__main__':
    main()
