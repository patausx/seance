#!/usr/bin/env python3
"""One static PLAY editor proposal, NOT runtime output or device acceptance.

Uses shipping 6x8 glyphs, the baked 28ppem PLAY heading (OFL, assets/fonts/OFL.txt),
and Pink palette. The waveform is an exact extrema reduction of an in-memory
4-second fixture mixed from shipped CC0 hits, not a decorative invented trace.
Only two PNGs (one dual at native/2x) are exported. No audio files are written.
"""
from array import array
from pathlib import Path
import re
import sys
import wave
from PIL import Image
from render_module_geometry_study import Canvas

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/play_editor_study'
SR = 32000
DURATION = 4 * SR
VIEW = (28000, 60000)       # 0.875 .. 1.875s: exactly 4x
TRIM = (32000, 56000)       # 1.000 .. 1.750s: per-card, NOT per-slice
FADE = (192, 2560)         # proposed 6ms in / 80ms out
CURSOR = 41280             # illustrative audition position, not live telemetry
CUTS = tuple(i * SR // 2 for i in range(8))


def palette(name):
    source = (ROOT / 'platform/3ds/ui_palette.h').read_text()
    match = re.search(r'inline uint32_t ' + name + r'\(\).*?: rgb\((\d+), (\d+), (\d+)\)', source)
    assert match, name
    return tuple(map(int, match.groups()))


INK, SPOT, PAPER, CELL, QUIET, RAIL = map(palette, ('ink', 'spot', 'bg', 'cell', 'menu_ghost', 'branch'))


def blend(a, b, n, d=100):
    return tuple((x * (d - n) + y * n + d // 2) // d for x, y in zip(a, b))


def paper(width):
    c = Canvas(width)
    # Exact static Pink base gradient from ui_palette::paper_color, no fake animation.
    d = 239 ** 3
    for y in range(240):
        n = y * y * (717 - 2 * y)
        rgb = tuple((a * d - b * n + d // 2) // d for a, b in ((228, 17), (194, 7), (207, 1)))
        c.d.line((0, y, width - 1, y), fill=rgb)
    return c


def heading(c, x, y):
    source = (ROOT / 'platform/3ds/menu_labels_data.h').read_text()
    offset, width, height = map(int, re.search(r'\{"play", (\d+), (\d+), (\d+)\}', source).groups())
    data = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', source.split('bitmap[] = {', 1)[1].split('};', 1)[0]))
    mask = Image.new('L', (width, height))
    stride = (width + 7) // 8
    for yy in range(height):
        for xx in range(width):
            if data[offset + yy * stride + xx // 8] & (0x80 >> (xx % 8)):
                mask.putpixel((xx, yy), 255)
    assert x + width <= c.im.width and y + height <= 240
    c.labels.append(((x, y, x + width, y + height), 'play / native heading'))
    c.im.paste(INK, (x, y), mask)


def fixture():
    """Real PCM fixture for this study; not an existing recorded break or app buffer."""
    pcm = [0] * DURATION
    events = {
        'kick_punch': [(0, .70), (1.5, .58), (2, .70), (3.5, .61)],
        'snare_synth': [(1, .70), (3, .70)],
        'hat_closed': [(i / 4, .16 if i % 2 else .11) for i in range(16)],
    }
    for name, hits in events.items():
        with wave.open(str(ROOT / 'sample_library/starter_cc0/wav' / f'{name}.wav'), 'rb') as source:
            assert (source.getframerate(), source.getnchannels(), source.getsampwidth()) == (SR, 1, 2)
            samples = array('h', source.readframes(source.getnframes()))
            if sys.byteorder != 'little':
                samples.byteswap()
        for seconds, gain in hits:
            start = round(seconds * SR)
            for i, value in enumerate(samples[:DURATION - start]):
                pcm[start + i] += round(value * gain)
    assert len(pcm) == DURATION and max(map(abs, pcm)) < 32768
    return pcm


def px(frame, domain, x, width):
    return x + round((frame - domain[0]) * (width - 1) / (domain[1] - domain[0]))


def waveform(c, pcm, domain, x, y, width, height):
    """Exhaustive min/max per bin, fixed full scale shared by overview/detail."""
    zero, amplitude = y + height // 2, height // 2 - 4
    c.d.line((x, zero, x + width - 1, zero), fill=RAIL)
    for col in range(width):
        a = domain[0] + col * (domain[1] - domain[0]) // width
        b = domain[0] + (col + 1) * (domain[1] - domain[0]) // width
        low, high = min(pcm[a:b]), max(pcm[a:b])
        color = INK if TRIM[0] <= (a + b) // 2 < TRIM[1] else blend(QUIET, PAPER, 18)
        c.d.line((x + col, zero - round(high * amplitude / 32768),
                  x + col, zero - round(low * amplitude / 32768)), fill=color)


def bracket(c, a, b, y, color, caps=4):
    c.d.line((a, y + caps, a, y, b, y, b, y + caps), fill=color)


def top(pcm):
    c = paper(400)
    heading(c, 20, 12)
    c.text(90, 23, 'sample editor', QUIET)
    c.text(380, 23, 'ch01 / s00', QUIET, right=True)
    c.text(20, 49, 'cc0_break.wav', INK)
    c.text(380, 49, '4.000s / mono', QUIET, right=True)
    domain, x, w = (0, DURATION), 20, 360
    va, vb = (px(t, domain, x, w) for t in VIEW)
    a, b = (px(t, domain, x, w) for t in TRIM)
    c.d.rectangle((va, 87, vb, 154), fill=blend(PAPER, CELL, 42))
    bracket(c, va, vb, 65, QUIET, 4)
    c.text((va + vb - 48) // 2, 55, 'view /4x', QUIET)
    for i, boundary in enumerate(CUTS):
        at = px(boundary, domain, x, w)
        c.text(at + 5, 76, f'{i + 1:02}', QUIET)
        c.d.line((at, 89, at, 153), fill=blend(RAIL, PAPER, 38))
    waveform(c, pcm, domain, x, 87, w, 68)
    for at in (a, b):
        c.d.line((at, 87, at, 154), fill=SPOT)
    bracket(c, a, b, 155, SPOT, -4)
    cursor = px(CURSOR, domain, x, w)
    c.d.line((cursor, 93, cursor, 150), fill=SPOT)
    c.d.polygon(((cursor - 2, 90), (cursor + 2, 90), (cursor, 93)), fill=SPOT)
    for i in range(5):
        at = px(i * SR, domain, x, w)
        c.d.line((at, 162, at, 165), fill=RAIL)
        c.text(min(at, 380 - 12), 169, f'{i}s', QUIET)
    c.text(20, 190, 'selection / this play', QUIET)
    c.text(20, 204, '0.750s', INK, scale=2)
    c.text(242, 190, 'audition / repeat', SPOT)
    c.text(242, 207, 'through chain', QUIET)
    c.d.line((20, 225, 379, 225), fill=RAIL)
    c.text(20, 232, 'cuts: shared source', QUIET)
    c.text(380, 232, 'original untouched', QUIET, right=True)
    return c.im


def bottom(pcm):
    c = paper(320)
    heading(c, 12, 7)
    for x, label, active in ((84, 'trim', True), (138, 'cuts', False), (192, 'loop', False), (258, 'more', False)):
        c.text(x, 18, label, SPOT if active else QUIET)
        if active:
            c.d.line((x, 31, x + 24, 31), fill=SPOT, width=2)
    c.text(12, 44, 'in 1.000s', SPOT)
    c.text(308, 44, 'out 1.750s', SPOT, right=True)
    x, y, w, h = 12, 62, 296, 99
    a, b = (px(t, VIEW, x, w) for t in TRIM)
    c.d.rectangle((a, y, b, y + h - 1), fill=blend(PAPER, CELL, 55))
    waveform(c, pcm, VIEW, x, y, w, h)
    # Proposed non-destructive edge envelopes. Source PCM trace remains unmodified.
    fi = px(TRIM[0] + FADE[0], VIEW, x, w)
    fo = px(TRIM[1] - FADE[1], VIEW, x, w)
    mid = y + h // 2
    c.d.line((a, mid, fi, y + 7), fill=SPOT)
    c.d.line((fo, y + 7, b, mid), fill=SPOT)
    for at in (fi, fo):
        c.d.rectangle((at - 2, y + 5, at + 2, y + 9), fill=SPOT)
    c.text(a + 9, y + 12, '6ms', SPOT)
    c.text(fo - 30, y + 12, '80ms', SPOT)
    # Separate upper IN and lower OUT grip lanes, at least 24px conceptual touch.
    for at in (a, b):
        c.d.line((at, y, at, y + h - 1), fill=SPOT)
    c.d.rectangle((a - 5, y - 4, a + 6, y - 1), fill=SPOT)
    c.d.rectangle((b - 6, y + h, b + 5, y + h + 3), fill=SPOT)
    cursor = px(CURSOR, VIEW, x, w)
    for yy in range(y + 1, y + h - 2, 5):
        c.d.line((cursor, yy, cursor, yy + 2), fill=SPOT)
    c.d.polygon(((cursor - 3, y - 4), (cursor + 3, y - 4), (cursor, y)), fill=SPOT)
    c.text(14, 176, '-', QUIET)
    c.text(44, 176, '+', SPOT)
    c.text(73, 176, 'fit', QUIET)
    # Relative pan strip locates the exact zoom view in the whole sample.
    c.d.line((114, 180, 258, 180), fill=RAIL)
    va, vb = (px(t, (0, DURATION), 114, 145) for t in VIEW)
    c.d.rectangle((va, 177, vb, 183), fill=SPOT)
    c.text(308, 176, '4x', SPOT, right=True)
    c.text(12, 200, 'snap:zero', SPOT)
    c.text(98, 200, '- 1smp +', QUIET)
    c.text(216, 200, 'undo', INK)
    c.text(272, 200, 'redo', QUIET)
    c.d.line((12, 216, 307, 216), fill=RAIL)
    c.d.polygon(((13, 225), (13, 233), (19, 229)), fill=SPOT)
    c.text(26, 226, 'listen', SPOT)
    c.text(90, 226, 'repeat', SPOT)
    c.d.line((90, 237, 125, 237), fill=SPOT)
    c.text(156, 226, 'stop', QUIET)
    c.text(224, 226, 'save copy', INK)
    return c.im


def main():
    pcm = fixture()
    images = top(pcm), bottom(pcm)
    assert images[0].size == (400, 240) and images[1].size == (320, 240)
    assert TRIM[0] >= VIEW[0] and TRIM[1] <= VIEW[1]
    assert (TRIM[1] - TRIM[0]) / SR == .750
    assert DURATION / (VIEW[1] - VIEW[0]) == 4
    dual = Image.new('RGB', (400, 488), (24, 20, 29))
    dual.paste(images[0], (0, 0))
    dual.paste(images[1], (40, 248))
    OUT.mkdir(parents=True, exist_ok=True)
    dual.save(OUT / 'play_editor_native.png')
    dual.resize((800, 976), Image.Resampling.NEAREST).save(OUT / 'play_editor_2x.png')
    print('static concept only; native glyph/text bounds and pairwise label spacing passed')
    print(OUT / 'play_editor_2x.png')


if __name__ == '__main__':
    main()
