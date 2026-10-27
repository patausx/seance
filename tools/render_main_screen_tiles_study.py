#!/usr/bin/env python3
"""Three lower-screen concepts on one local sheet; no image API or runtime edits.

Same v5 upper in all columns, 400x240 / 320x240, nearest 2x presentation.
Project Gossip28 + native 6x8 (assets/fonts/OFL.txt). Values are examples.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_universal_sigils_study import Surface, caption, OUTSIDE
from render_menu_style_study import heading_mask
from render_main_screen_accordion_study import center_text
from render_main_screen_accordion_six_study import upper, ORDER

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_tiles_study'
CELL = (29, 29, 29)
ACTIVE = (41, 38, 46)
RANGE = (65, 55, 79)


def surface(width=320):
    c = Surface(width, 'graphite')
    c.p = dict(c.p, inverse=(36, 36, 36))
    return c


def chrome():
    c = surface()
    c.heading(16, 10, 'patch table')
    c.txt(304, 23, 'patch02 *', 'ghost', right=True)
    c.txt(20, 51, 'chain', 'ghost')
    for i, x in enumerate((98, 146, 194, 242), 1):
        c.txt(x, 51, str(i), 'spot' if i == 4 else 'ghost')
        if i == 4:
            c.line((x - 2, 64, x + 8, 64), 'spot')
    c.txt(304, 76, '6 / 6', 'ghost', right=True)
    c.line((16, 216, 304, 216))
    return c


def tile(c, box, chosen=False, solid=False):
    c.d.rounded_rectangle(box, radius=3,
                          fill=c.p['spot'] if solid else ACTIVE if chosen else CELL,
                          outline=c.p['spot'] if chosen else c.p['rail'], width=1)


def handle(c, cx, y, role='rail'):
    # A visual header grip only: gesture ownership is not implemented here.
    for dx in (-5, 0, 5):
        c.d.rectangle((cx + dx, y, cx + dx + 1, y + 1), fill=c.p[role])


def rail(c, x, y, width, stored, mod=False):
    end = x + width - 1
    if mod:
        c.d.rectangle((x + round(width * .26), y - 2,
                       x + round(width * .70), y + 2), fill=RANGE)
    # Opaque range first, crisp integer rail/ticks/handle on top.
    c.line((x, y, end, y))
    for tx in (x, end):
        c.line((tx, y - 2, tx, y + 2))
    knob = x + round((width - 1) * stored)
    c.d.rectangle((knob - 1, y - 4, knob + 1, y + 4), fill=c.p['spot'])
    if mod:
        live = min(end, knob + round(width * .15))
        c.line((live, y - 3, live, y + 3), 'ghost')


def control(c, x, y, width, name, value, stored, mod=False, gap=19):
    c.txt(x, y, name, 'ghost')
    c.txt(x + width, y, value, 'ink', right=True)
    if mod:
        c.txt(x + len(name) * 6 + 7, y, 'a', 'spot')
    rail(c, x, y + gap, width, stored, mod)


def selectors():
    """All six visible, equal-size identity/state/sigil tiles; no inline values."""
    c = chrome()
    for i, name in enumerate(ORDER):
        x = 16 + i * 48
        chosen = name == 'ring'
        tile(c, (x, 90, x + 43, 199), chosen, solid=chosen)
        role = 'inverse' if chosen else 'ink'
        center_text(c, x + 22, 102, f'{i + 1:02}', 'inverse' if chosen else 'ghost')
        handle(c, x + 22, 94, 'inverse' if chosen else 'rail')
        c.heading(x + 22 - heading_mask(name).width // 2, 123, name, role)
        center_text(c, x + 22, 161, 'on', 'inverse' if chosen else 'ghost')
        if name in ('ring', 'chor'):
            center_text(c, x + 22, 182, 'a' if name == 'ring' else 'b',
                        'inverse' if chosen else 'spot')
        else:
            c.line((x + 20, 186, x + 23, 186))
    c.txt(16, 227, 'bypass', 'ghost')
    c.txt(304, 227, 'edit >', 'spot', right=True)
    return c.im


def working_tiles():
    """Exact physical column alignment to v5, with edge peeks on narrower LCD.

    Upper strip: x16..384, width368. Lower LCD is centered with a +40px
    sheet offset, so crop strip x24..344: lower x = upper x - 40.
    This is real geometric alignment, not normalized/rough matching.
    """
    c = chrome()
    strip = surface(368)
    left = 0
    boundaries = []
    for i, name in enumerate(ORDER):
        chosen = name == 'ring'
        width = 168 if chosen else 40
        right = left + width
        boundaries.append((left, right))
        tile(strip, (left + 2, 86, right - 3, 209), chosen)
        if chosen:
            strip.heading(left + 14, 95, name, 'spot')
            strip.txt(right - 14, 102, 'on', 'ghost', right=True)
            handle(strip, (left + right) // 2, 89)
            control(strip, left + 14, 124, width - 28, 'freq', '440hz', .43, mod=True)
            control(strip, left + 14, 166, width - 28, 'mix', '68%', .68)
        elif left >= 24 and right <= 344:
            cx = (left + right) // 2
            handle(strip, cx, 90)
            center_text(strip, cx, 107, f'{i + 1:02}')
            center_text(strip, cx, 140, name, 'ink')
            center_text(strip, cx, 169, 'on')
            if name == 'chor':
                center_text(strip, cx, 192, 'b', 'spot')
        left = right
    assert left == 368 and boundaries[1] == (40, 208)
    # Paste only the content band, preserving identical headers and footers.
    c.im.paste(strip.im.crop((24, 84, 344, 211)), (0, 84))
    c.d = ImageDraw.Draw(c.im)
    # Don't crop words into unreadable fragments: partial edge tiles carry cues.
    c.txt(4, 141, '<', 'ghost')
    c.txt(310, 141, '>', 'ghost')
    c.txt(16, 227, 'bypass', 'ghost')
    c.txt(304, 227, 'all params >', 'spot', right=True)
    # Selected visual area lands at lower x16..184, upper x56..224.
    assert boundaries[1][0] - 24 + 40 == 56
    assert boundaries[1][1] - 24 + 40 == 224
    return c.im


def pages():
    """Large page, four existing RING controls, sticky short names at peek edges."""
    c = chrome()
    # The page frame continues outside the viewport; no off-screen text buffers.
    tile(c, (-196, 86, 34, 209))
    tile(c, (286, 86, 516, 209))
    for cx, name, number in ((16, 'gran', '01'), (304, 'chor', '03')):
        center_text(c, cx, 112, number)
        center_text(c, cx, 140, name, 'ink')
        center_text(c, cx, 169, 'on')
    tile(c, (46, 86, 274, 209), chosen=True)
    handle(c, 160, 90)
    c.heading(60, 95, 'ring', 'spot')
    c.txt(260, 103, '02 / on', 'ghost', right=True)
    # Existing engine params (RingUnit): FREQ, DEPTH, FDBK, DIODE, MIX, FOLLOW.
    # Four main controls here; the other settings stay behind 'more'.
    control(c, 60, 127, 87, 'freq', '440hz', .43, mod=True)
    control(c, 173, 127, 87, 'depth', '75%', .75)
    control(c, 60, 168, 87, 'fdbk', '+0%', .5)
    control(c, 173, 168, 87, 'mix', '68%', .68)
    c.txt(16, 227, 'bypass', 'ghost')
    for i in range(6):
        x = 135 + i * 9
        c.d.rectangle((x, 229, x + 3, 232), fill=c.p['spot'] if i == 1 else c.p['rail'])
    c.txt(304, 227, 'more >', 'spot', right=True)
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (1296, 644), OUTSIDE)
    caption(sheet, 16, 12, 'seance / three lower-screen concepts / same upper / six modules')
    common_top = upper('ring')
    columns = (
        ('01 / selectors', selectors(), 'all six / name + state + sigil',
         'selection sync / separate editor'),
        ('02 / working tiles', working_tiles(), 'two controls / columns aligned',
         'same x as top / edge tiles peek'),
        ('03 / pages', pages(), 'four controls / one large page',
         'selection sync / neighbours peek'),
    )
    for i, (title, bottom, note1, note2) in enumerate(columns):
        x = 16 + i * 432
        if i == 1:
            ImageDraw.Draw(sheet).line((x, 34, x + 398, 34), fill=(181, 154, 232), width=1)
        caption(sheet, x, 43, title)
        sheet.paste(common_top, (x, 64))
        sheet.paste(bottom, (x + 40, 328))
        caption(sheet, x + 40, 581, note1)
        caption(sheet, x + 40, 599, note2)
        # The upper really is unchanged across the three columns.
        assert sheet.crop((x, 64, x + 400, 304)).tobytes() == common_top.tobytes()
    caption(sheet, 16, 628, 'native 400x240 + 320x240 / static examples / upper is display only / no runtime changes')
    path = OUT / 'main-tiles-three-v6-2x.png'
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
