#!/usr/bin/env python3
"""Static v3: floating word-only menus, full-surface blur, smooth random shape.
Preserves v1/v2. No runtime changes or animation/GPU-performance claims.
"""
from PIL import Image, ImageDraw, ImageFilter
from render_universal_sigils_study import Surface, caption, OUTSIDE, THEMES, OUT
from render_universal_sigils_words import editor


def opened(theme, kind):
    underlying = editor(theme)
    c = Surface(320, theme)
    # One uniform blur/tint across the whole background. No local panel mask,
    # opaque fill, outline, shadow, or rectangular replacement behind the list.
    c.im = Image.blend(underlying.filter(ImageFilter.GaussianBlur(1.8)), c.im, .24)
    c.d = ImageDraw.Draw(c.im)
    if kind == 'sigil':
        anchor = (138, 8, 183, 38)
        items = tuple('sigil ' + letter for letter in 'abcdefgh')
        text_x, first_y, row_h, branch_x = 157, 55, 18, 143
    else:
        anchor = (57, 39, 114, 59)
        items = ('drawn', 'sine', 'triangle', 'saw', 'square', 'random', 'smooth')
        text_x, first_y, row_h, branch_x = 76, 78, 20, 62
    # Only the invoking word stays sharp, linking the disclosure to its origin.
    c.im.paste(underlying.crop(anchor), anchor[:2])
    c.line((branch_x, first_y + 4, branch_x, first_y + (len(items) - 1) * row_h + 4))
    for i, item in enumerate(items):
        y = first_y + i * row_h
        role = 'spot' if i == 0 else 'ink'
        c.line((branch_x, y + 4, text_x - 7, y + 4), role if i == 0 else 'rail')
        c.txt(text_x, y, item, role)
        if i == 0:
            c.line((text_x, y + 12, text_x + len(item) * 6 - 1, y + 12), 'spot')
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (688, 554), OUTSIDE)
    for row, theme in enumerate(THEMES):
        y = 12 + row * 266
        for col, kind in enumerate(('sigil', 'shape')):
            x = 16 + col * 336
            caption(sheet, x, y, theme + ' / ' + kind + ' / no panel')
            sheet.paste(opened(theme, kind), (x, y + 16))
    caption(sheet, 16, 542, 'v3 / random + smooth / blur and words / static concept')
    path = OUT / 'word-menus-v3-2x.png'
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(path)
    print(path)


if __name__ == '__main__':
    main()
