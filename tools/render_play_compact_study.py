#!/usr/bin/env python3
"""Requested static PLAY proposal, NOT runtime, interaction or audio proof.

Two boards only, drawn at 400x240 / 320x240 and enlarged nearest-neighbour.
Reuses shipping glyphs, Pink and a real in-memory CC0-hit PCM fixture.
No sample files, application sources, earlier studies or releases are changed.
"""
from PIL import Image
from render_module_geometry_study import Canvas
from render_play_editor_study import (
    ROOT, SR, DURATION, INK, SPOT, PAPER, CELL, QUIET, RAIL,
    blend, paper, heading, fixture, px,
)
from render_play_editor_final import digit

OUT = ROOT / 'ui_mockups/play_compact_study'
OUTSIDE = (24, 20, 29)
CAPTION = (226, 190, 206)
FULL = (0, DURATION)
VIEW = (28000, 60000)
TRIM = (32000, 56000)
CUTS = tuple(i * SR // 2 for i in range(9))
SLICE = (CUTS[2], CUTS[3])
HEAD = TRIM[0] + round((TRIM[1] - TRIM[0]) * .46)


def line(c, x1, y1, x2, y2, color=RAIL):
    c.d.line((x1, y1, x2, y2), fill=color)


def text(c, x, y, label, color=INK, right=False):
    c.text(x, y, label, color, right=right)


def wave(c, pcm, domain, box, selected=TRIM, slices=False, head=None):
    """Every source frame contributes to extrema; same amplitude on both screens."""
    x, y, w, h = box
    zero, amplitude = y + h // 2, h // 2 - 4
    line(c, x, zero, x + w - 1, zero)
    for col in range(w):
        a = domain[0] + col * (domain[1] - domain[0]) // w
        b = domain[0] + (col + 1) * (domain[1] - domain[0]) // w
        low, high = min(pcm[a:b]), max(pcm[a:b])
        ink = INK if selected[0] <= (a + b) // 2 < selected[1] else blend(QUIET, PAPER, 24)
        line(c, x + col, zero - round(high * amplitude / 32768),
             x + col, zero - round(low * amplitude / 32768), ink)
    if slices:
        for n, frame in enumerate(CUTS[:-1]):
            if domain[0] <= frame < domain[1]:
                at = px(frame, domain, x, w)
                active = n == 2
                line(c, at, y, at, y + h - 1, SPOT if active else RAIL)
                text(c, min(x + w - 15, at + 4), y - 13, f'{n + 1:02}', SPOT if active else QUIET)
    for frame in selected:
        if domain[0] <= frame <= domain[1]:
            at = px(frame, domain, x, w)
            line(c, at, y, at, y + h - 1, SPOT)
            line(c, at - 3, y + h, at + 3, y + h, SPOT)
    if head is not None:
        at = px(head, domain, x, w)
        line(c, at, y - 1, at, y + h, SPOT)
        c.d.polygon(((at - 3, y - 5), (at + 3, y - 5), (at, y - 1)), fill=SPOT)


def big(c, label, x, baseline):
    """Existing native numeric glyphs, not a replacement UI font."""
    for char in label:
        if char == '.':
            c.d.rectangle((x, baseline - 5, x + 2, baseline - 2), fill=INK)
            x += 9
        else:
            x += digit(c, char, x, baseline) + 3
    return x


def top(pcm, mode):
    c = paper(400)
    heading(c, 20, 10)
    text(c, 91, 16, 'cc0_break.wav')
    text(c, 91, 32, '4.000s / mono', QUIET)
    text(c, 380, 16, 'ch01 / 01', QUIET, right=True)
    text(c, 380, 32, 'sequence stopped', QUIET, right=True)
    va, vb = (px(t, FULL, 20, 360) for t in VIEW)
    line(c, va, 56, vb, 56, QUIET)
    line(c, va, 56, va, 60, QUIET)
    line(c, vb, 56, vb, 60, QUIET)
    text(c, 380, 55, 'detail 4x', QUIET, right=True)
    selection = SLICE if mode == 'slices' else TRIM
    wave(c, pcm, FULL, (20, 81, 360, 70), selection,
         slices=mode == 'slices', head=HEAD if mode == 'scratch' else None)
    for n in range(5):
        at = px(n * SR, FULL, 20, 360)
        line(c, at, 156, at, 159)
        text(c, min(at, 368), 164, f'{n}s', QUIET)
    if mode == 'sample':
        text(c, 20, 177, 'trim length', QUIET)
        end = big(c, '0.750', 20, 221)
        text(c, end + 3, 211, 's', QUIET)
        text(c, 230, 187, 'start  1.000s')
        text(c, 230, 204, 'end    1.750s')
        footer = ('non-destructive trim', 'source unchanged')
    elif mode == 'slices':
        text(c, 20, 177, 'selected slice', QUIET)
        end = big(c, '03', 20, 221)
        text(c, end + 5, 211, '/ 08', QUIET)
        text(c, 230, 187, '1.000 .. 1.500s')
        text(c, 230, 204, '0.500s / forward')
        footer = ('slice map shared by source', 'trim per play')
    else:
        text(c, 20, 177, 'finger / trim position', SPOT)
        end = big(c, '46', 20, 221)
        text(c, end + 3, 211, '%', QUIET)
        text(c, 230, 187, 'source audition', SPOT)
        text(c, 230, 204, 'patch unchanged', QUIET)
        footer = ('still = silent', 'lift = stop')
    line(c, 20, 226, 379, 226)
    text(c, 20, 232, footer[0], QUIET)
    text(c, 380, 232, footer[1], QUIET, right=True)
    return c.im


def header(c, mode):
    # The brand/source overview belongs upstairs; no second heavy PLAY heading.
    text(c, 12, 13, 'cc0_break.wav >')
    text(c, 308, 13, 'tools', SPOT, right=True)
    for x, label in ((12, 'sample'), (84, 'slices'), (156, 'scratch')):
        text(c, x, 39, label, SPOT if label == mode else QUIET)
        if label == mode:
            line(c, x, 51, x + len(label) * 6 - 2, 51, SPOT)
    text(c, 12, 64, 'rate' if mode == 'slices' else 'speed', QUIET)
    text(c, 49, 64, '1.00x')
    text(c, 112, 64, 'gain', QUIET)
    text(c, 143, 64, '100%')
    text(c, 308, 64, 'one shot v', INK, right=True)


def pan(c):
    text(c, 13, 177, '-', QUIET)
    rail_x, rail_w = 44, 178
    line(c, rail_x, 181, rail_x + rail_w - 1, 181)
    a, b = (px(t, FULL, rail_x, rail_w) for t in VIEW)
    c.d.line((a, 181, b, 181), fill=SPOT, width=3)
    text(c, 244, 177, '4x', SPOT)  # tap the amount for fit; same as existing app
    text(c, 298, 177, '+')


def footer(c, scratch=False):
    line(c, 12, 216, 307, 216)
    if scratch:
        text(c, 12, 227, 'drag wave to play', SPOT)
        text(c, 308, 227, 'to seq >', QUIET, right=True)
    else:
        text(c, 12, 227, 'listen', SPOT)
        text(c, 100, 227, 'undo')
        text(c, 150, 227, 'redo', QUIET)
        text(c, 308, 227, 'to seq >', QUIET, right=True)


def bottom(pcm, mode):
    c = paper(320)
    header(c, mode)
    if mode == 'sample':
        a, b = (px(t, VIEW, 12, 296) for t in TRIM)
        text(c, a - 12, 87, 'start', SPOT)
        text(c, b - 6, 87, 'end', SPOT)
        wave(c, pcm, VIEW, (12, 102, 296, 62))
        pan(c)
        text(c, 12, 199, 'reverse off', QUIET)
        text(c, 140, 199, 'edit >')
        text(c, 308, 199, 'crop >', QUIET, right=True)
    elif mode == 'slices':
        wave(c, pcm, VIEW, (12, 102, 296, 62), SLICE, slices=True)
        pan(c)
        text(c, 12, 199, '< 03 >', SPOT)
        text(c, 87, 199, 'reverse', QUIET)
        text(c, 173, 199, 'slice v')
        text(c, 308, 199, '+ / edit', QUIET, right=True)
    else:
        # No draggable trim handles in scratch. Source geometry stays identical.
        wave(c, pcm, VIEW, (12, 88, 296, 76), TRIM, head=HEAD)
        at = px(HEAD, VIEW, 12, 296)
        # Small open touch marker, not a made-up finger obscuring the waveform.
        c.d.ellipse((at - 7, 121, at + 7, 135), outline=SPOT)
        pan(c)
        text(c, 12, 199, 'finger 46%', SPOT)
        text(c, 308, 199, 'seq scrub off >', QUIET, right=True)
    footer(c, mode == 'scratch')
    return c.im


def sheet_header(title, detail):
    c = paper(320)
    text(c, 12, 15, title, SPOT)
    text(c, 308, 15, 'close', INK, right=True)
    text(c, 12, 35, detail, QUIET)
    line(c, 12, 51, 307, 51)
    return c


def tools():
    c = sheet_header('play / tools', 'cc0_break.wav / selection retained')
    entries = (
        ('edit sample', 'edges / fades / loop'),
        ('slice tools', 'add / auto / live / merge'),
        ('playback', 'gate / loop / pitch'),
        ('listen options', 'source / chain / repeat'),
        ('save copy', 'wav / process / make unique'),
    )
    for i, (name, description) in enumerate(entries):
        y = 62 + i * 30
        text(c, 12, y, name)
        text(c, 24, y + 12, description, QUIET)
        text(c, 308, y + 6, '>', SPOT, right=True)
    line(c, 12, 216, 307, 216)
    text(c, 12, 227, 'undo')
    text(c, 76, 227, 'redo', QUIET)
    text(c, 308, 227, 'back to wave', QUIET, right=True)
    return c.im


def row(c, y, name, value, active=False):
    text(c, 12, y, name, INK)
    text(c, 308, y, value, SPOT if active else QUIET, right=True)


def playback():
    c = sheet_header('play / playback', 'musical voice / not preview repeat')
    row(c, 65, 'trigger', 'one shot v', True)
    row(c, 90, 'gate length', '1 step')
    row(c, 115, 'loop', 'off')
    row(c, 140, 'pitch snap', 'off')
    row(c, 165, 'bypass', 'off')
    row(c, 190, 'loop edges', 'edit >', True)
    line(c, 12, 216, 307, 216)
    text(c, 12, 227, 'listen', SPOT)
    text(c, 308, 227, '< tools', QUIET, right=True)
    return c.im


def copy():
    c = sheet_header('play / save copy', 'trim 1.000 .. 1.750s / 0.750s')
    row(c, 65, 'normalize', 'off')
    row(c, 90, 'remove dc', 'off')
    row(c, 115, 'bake fades', 'off')
    row(c, 140, 'bake reverse', 'off')
    row(c, 169, 'save selection', 'new wav >', True)
    row(c, 195, 'make unique', 'full raw copy >')
    line(c, 12, 216, 307, 216)
    text(c, 12, 227, 'original never overwritten', QUIET)
    return c.im


def compose():
    pcm = fixture()
    board = Canvas(1264, 540, OUTSIDE)
    for i, (mode, caption) in enumerate((
        ('sample', '01 / play the sample'),
        ('slices', '02 / play the slices'),
        ('scratch', '03 / play the wave'),
    )):
        x = 16 + i * 416
        text(board, x, 10, caption, CAPTION)
        upper, lower = top(pcm, mode), bottom(pcm, mode)
        assert upper.size == (400, 240) and lower.size == (320, 240)
        board.im.paste(upper, (x, 28))
        board.im.paste(lower, (x + 40, 276))
    text(board, 16, 528, 'seance / compact play study / native screen geometry / static proposal, not runtime', CAPTION)
    sheets = Canvas(1024, 298, OUTSIDE)
    for i, (label, image) in enumerate((
        ('tools / five clear destinations', tools()),
        ('playback / patch controls', playback()),
        ('copy / file operations only', copy()),
    )):
        x = 16 + i * 336
        text(sheets, x, 12, label, CAPTION)
        sheets.im.paste(image, (x, 32))
    text(sheets, 16, 284, 'rare operations leave the wave clear / no pcm, patch or application changes', CAPTION)
    return board.im, sheets.im


def main():
    main_board, tools_board = compose()
    paths = [OUT / 'play_compact_2x.png', OUT / 'play_tools_2x.png']
    # Existing approved renders are never overwritten. Use a new study directory.
    if any(p.exists() for p in paths):
        raise SystemExit('output exists; preserve it before rerendering')
    OUT.mkdir(parents=True, exist_ok=True)
    for path, image in zip(paths, (main_board, tools_board)):
        image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST).save(path)
        print(path)
    print('static only / native label bounds and non-overlap checked / real fixture extrema')


if __name__ == '__main__':
    main()
