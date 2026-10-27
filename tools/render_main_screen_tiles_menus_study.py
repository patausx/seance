#!/usr/bin/env python3
"""v7: shared 320px strip and anchored menus. Static design only, no runtime.
Native 400x240/320x240, project Gossip28 + 6x8, nearest 2x.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_universal_sigils_study import caption, OUTSIDE
from render_menu_style_study import heading_mask
from render_main_screen_accordion_study import center_text, contour, detail
from render_main_screen_tiles_study import surface, control
from render_main_screen_focus_study import reference_blur

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_tiles_study'
FULL = ('gran', 'ring', 'chor', 'dly', 'filt', 'vca')
FREE = FULL[:-1] + (None,)


def columns(names, selected='ring'):
    x = 0
    for name in names:
        width = 160 if name == selected else 32
        yield name, x, x + width
        x += width
    assert x == 320


def upper(names):
    c = surface(400)
    c.heading(16, 12, 'chain 4')
    c.txt(384, 23, 'patch02 *', 'ghost', right=True)
    for i, (name, left, right) in enumerate(columns(names)):
        left, right = left + 40, right + 40
        cx = (left + right) // 2
        chosen = name == 'ring'
        center_text(c, cx, 51, f'{i + 1:02}', 'spot' if chosen else 'ghost')
        if name is None:
            center_text(c, cx, 130, '--', 'ghost')
            continue
        contour(c, left, right, chosen)
        if chosen:
            c.heading(cx - heading_mask(name).width // 2, 78, name, 'spot')
            detail(c, name, left, right)
        else:
            center_text(c, cx, 130, name, 'ink')
    c.line((16, 218, 384, 218))
    c.txt(16, 228, f'{sum(n is not None for n in names):02} / 06 modules', 'ghost')
    c.txt(384, 228, 'serial > out', 'ghost', right=True)
    return c.im


def add_anchor(c):
    # Same coordinates before/after menu opening; no moving anchor.
    c.txt(298, 130, '+', 'spot', scale=2)
    center_text(c, 304, 153, 'add', 'spot')


def ring_anchor(c, opened=False):
    c.heading(46, 92, 'ring', 'spot')
    c.txt(168, 100, '...', 'spot' if opened else 'ghost')


def lower(names):
    c = surface()
    c.heading(16, 10, 'patch table')
    c.txt(304, 23, 'patch02 *', 'ghost', right=True)
    c.txt(20, 51, 'chain', 'ghost')
    for i, x in enumerate((98, 146, 194, 242), 1):
        c.txt(x, 51, str(i), 'spot' if i == 4 else 'ghost')
        if i == 4:
            c.line((x - 2, 64, x + 8, 64), 'spot')
    for name, left, right in columns(names):
        chosen = name == 'ring'
        if chosen:
            # An open tile on the common paper, no filled card or full border.
            c.line((left + 3, 82, right - 4, 82), 'spot')
            ring_anchor(c)
            control(c, left + 14, 127, 132, 'freq', '440hz', .43, mod=True)
            control(c, left + 14, 170, 132, 'mix', '68%', .68)
            c.line((left + 3, 206, right - 4, 206), 'spot')
        else:
            cx = (left + right) // 2
            if left:
                c.line((left, 103, left, 184))
            if name is None:
                add_anchor(c)
            else:
                center_text(c, cx, 135, name, 'ink')
                if name == 'chor':
                    center_text(c, cx, 161, 'b', 'spot')
    c.line((16, 216, 304, 216))
    c.txt(16, 230, 'bypass', 'ghost')
    c.txt(304, 230, 'all params >', 'spot', right=True)
    return c.im


def blurred(base):
    c = surface()
    c.im = reference_blur(base)
    c.d = ImageDraw.Draw(c.im)
    return c


def add_menu(base):
    c = blurred(base)
    c.heading(16, 80, 'add module', 'spot')
    c.txt(16, 105, 'sources', 'ghost')
    c.txt(82, 105, 'effects', 'spot')
    c.line((82, 116, 123, 116), 'spot')
    c.txt(270, 105, 'slot 06', 'ghost', right=True)
    add_anchor(c)
    # The branch joins the unchanged plus to a two-column word picker.
    c.line((296, 138, 283, 138, 283, 92, 131, 92), 'spot')
    groups = (('filter', 'ring', 'chor', 'dly', 'rev', 'comp'),
              ('fold', 'fract', 'shift', 'ghost', 'strain', 'vca'))
    for col, names in enumerate(groups):
        x = 32 + col * 124
        c.line((x - 11, 128, x - 11, 212))
        for i, name in enumerate(names):
            y = 128 + i * 16
            chosen = name == 'vca'
            role = 'spot' if chosen else 'ink'
            c.line((x - 11, y + 4, x - 5, y + 4), role if chosen else 'rail')
            c.txt(x, y, name, role)
            if chosen:
                c.line((x, y + 11, x + len(name) * 6 - 1, y + 11), 'spot')
    c.txt(16, 230, 'b cancel', 'ghost')
    c.txt(304, 230, 'a add', 'spot', right=True)
    return c.im


def actions(base):
    c = blurred(base)
    ring_anchor(c, opened=True)
    c.line((177, 113, 198, 113, 198, 211), 'spot')
    labels = ('all params', 'bypass', 'replace', 'copy', 'paste', 'delete')
    for i, label in enumerate(labels):
        y = 122 + i * 17
        role = 'spot' if i == 0 else 'ghost' if label == 'paste' else 'ink'
        c.line((198, y + 4, 206, y + 4), 'spot' if i == 0 else 'rail')
        c.txt(214, y, label, role)
        if i == 0:
            c.line((214, y + 11, 273, y + 11), 'spot')
    c.txt(16, 230, 'b close', 'ghost')
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    base_full, base_free = lower(FULL), lower(FREE)
    sheet = Image.new('RGB', (1296, 644), OUTSIDE)
    caption(sheet, 16, 12, 'seance / v7 / shared 320px columns + anchored word menus')
    states = (
        ('01 / six modules / aligned', upper(FULL), base_full,
         '160 + 5 x 32 px / all six visible', 'lighter tiles / two quick controls'),
        ('02 / add / one free slot', upper(FREE), add_menu(base_free),
         '5 modules + add / choose the sixth', 'sharp + anchor / frozen blurred table'),
        ('03 / module actions', upper(FULL), actions(base_full),
         'sharp ring + menu / no popup card', 'replace uses the same module picker'),
    )
    for i, (title, top, bottom, note1, note2) in enumerate(states):
        x = 16 + i * 432
        caption(sheet, x, 43, title)
        sheet.paste(top, (x, 64))
        sheet.paste(bottom, (x + 40, 328))
        caption(sheet, x + 40, 581, note1)
        caption(sheet, x + 40, 599, note2)
    caption(sheet, 16, 628, 'native 400x240 + 320x240 / static proposal / example data / not runtime')
    path = OUT / 'main-tiles-menus-v7-2x.png'
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
