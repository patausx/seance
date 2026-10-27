#!/usr/bin/env python3
"""One static open PLAY menu, not runtime/controller/audio output.

Native and 2x versions of the SAME dual-screen image only. Earlier PNGs and
runtime sources stay untouched; fonts, palette and PCM fixture are reused.
"""
from PIL import Image
from render_play_editor_study import (
    ROOT, INK, SPOT, QUIET, RAIL, paper, heading, fixture,
)
from render_play_surface_study import top, KEEP, VIEW, SR, DURATION

OUT = ROOT / 'ui_mockups/play_menu_study'


def thread(c, label, first, last):
    """Quiet grouping without panels, tiles or individual row borders."""
    c.text(12, first, label, QUIET)
    c.d.line((57, first + 4, 65, first + 4, 65, last + 4), fill=RAIL)
    for y in range(first, last + 1, 24):
        c.d.line((65, y + 4, 72, y + 4), fill=RAIL)


def row(c, y, name, value, action=False):
    c.text(80, y, name, INK)
    c.text(308, y, value, SPOT if action else QUIET, right=True)


def bottom():
    c = paper(320)
    heading(c, 12, 7)
    c.text(81, 17, 'cc0_break.wav >', INK)
    c.text(308, 17, 'close', SPOT, right=True)

    # These are NOT menu entries: the same persistent source/playback header.
    c.text(12, 44, 'speed', QUIET)
    c.text(49, 44, '1.00x', INK)
    c.text(109, 44, 'gain', QUIET)
    c.text(141, 44, '0.0db', INK)
    c.text(308, 44, 'one shot v', INK, right=True)
    c.d.line((12, 60, 307, 60), fill=RAIL)

    c.text(12, 70, 'trim', SPOT)
    c.d.line((12, 81, 35, 81), fill=SPOT, width=2)
    c.text(66, 70, 'slices', QUIET)
    c.text(132, 70, 'scrub', QUIET)
    c.text(308, 70, 'menu', SPOT, right=True)

    # A flat, unframed sheet occupies only the lower wave/tool area. Source
    # overview remains intact above. Five rows, 24px centre-to-centre; the
    # larger middle gap separates current PLAY edits from copy-only options.
    thread(c, 'shape', 98, 122)
    row(c, 98, 'fades', 'on wave >', action=True)
    row(c, 122, 'reverse', 'off')

    thread(c, 'copy', 150, 198)
    row(c, 150, 'normalize', 'off')
    row(c, 174, 'remove dc', 'off')
    row(c, 198, 'save wav', 'selection >', action=True)

    # Preview choices are direct controls, not a nested audition page. The
    # still is stopped. SOURCE is intentional: no simulated through-chain PCM.
    c.d.line((12, 213, 307, 213), fill=RAIL)
    c.d.polygon(((15, 224), (15, 232), (21, 228)), fill=SPOT)
    c.text(30, 224, 'listen', SPOT)
    c.text(117, 224, 'source v', INK)
    c.text(308, 224, 'repeat off', QUIET, right=True)
    return c.im


def main():
    assert DURATION == 4 * (VIEW[1] - VIEW[0])
    assert KEEP[1] - KEEP[0] == SR * 3 // 4
    pcm = fixture()
    upper, lower = top(pcm), bottom()
    assert upper.size == (400, 240) and lower.size == (320, 240)
    dual = Image.new('RGB', (400, 488), (24, 20, 29))
    dual.paste(upper, (0, 0))
    dual.paste(lower, (40, 248))
    OUT.mkdir(parents=True, exist_ok=True)
    dual.save(OUT / 'play_menu_native.png')
    dual.resize((800, 976), Image.Resampling.NEAREST).save(OUT / 'play_menu_2x.png')
    print('static menu concept; native bounds/label spacing and shared source geometry checked')
    print(OUT / 'play_menu_2x.png')


if __name__ == '__main__':
    main()
