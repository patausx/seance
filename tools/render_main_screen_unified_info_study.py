#!/usr/bin/env python3
"""v9 static study. Analytic example signals, NOT engine telemetry or runtime.
Native Gossip28/6x8 (assets/fonts/OFL.txt), 400x240 + 320x240, nearest 2x.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw
from render_universal_sigils_study import caption, OUTSIDE
from render_main_screen_tiles_study import surface, control
from render_main_screen_tiles_menus_study import columns, FULL
from render_main_screen_accordion_study import center_text
from render_main_screen_unified_study import small_graph, ACTIVE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_tiles_study'


def bands(c, top):
    for name, a, b in columns(FULL):
        a, b = a + (40 if top else 0), b + (40 if top else 0)
        chosen = name == 'ring'
        y0, y1 = (40, 239) if top else (0, 197)
        color = c.p['spot'] if chosen else c.p['rail']
        if chosen:
            c.d.rectangle((a + 2, y0, b - 3, y1), fill=ACTIVE)
        c.d.line((a + 2, y0, a + 2, y1), fill=color)
        c.d.line((b - 3, y0, b - 3, y1), fill=color)
        y = y0 if top else y1
        c.d.line((a + 2, y, b - 3, y), fill=color)


def upper():
    c = surface(400)
    c.heading(40, 8, 'patch table')
    bands(c, True)
    for name, a, b in columns(FULL):
        cx = 40 + (a + b) // 2
        if name == 'ring':
            c.heading(86, 47, 'ring', 'spot')
        else:
            center_text(c, cx, 55, name, 'ink')
            small_graph(c, name, cx)
    c.txt(86, 81, 'input', 'ghost')
    c.txt(218, 81, 'output', 'ink', right=True)
    c.line((86, 126, 217, 126))
    ins, outs, values = [], [], []
    for i in range(132):
        t = .012 * i / 131
        src = .62 * math.sin(math.tau * 220 * t) + .14 * math.sin(math.tau * 660 * t)
        wet = src * (.25 + .75 * math.sin(math.tau * 440 * t))
        out = .32 * src + .68 * wet
        ins.append((86 + i, 126 - round(src * 35)))
        outs.append((86 + i, 126 - round(out * 35)))
        values.append(out)
    c.d.line(ins, fill=(112, 112, 112))
    c.d.line(outs, fill=c.p['ink'])
    peak = 20 * math.log10(max(abs(v) for v in values))
    c.txt(86, 159, f'pk {peak:.1f}db', 'ghost')
    c.txt(218, 159, 'clip -', 'ghost', right=True)
    c.line((86, 176, 218, 176))
    c.txt(86, 185, 'a > freq', 'spot')
    c.txt(218, 185, '+24%', 'spot', right=True)
    c.line((86, 205, 217, 205))
    for x in (86, 152, 217):
        c.line((x, 202, x, 208))
    c.line((152, 205, 177, 205), 'spot')
    c.d.ellipse((174, 202, 180, 208), outline=c.p['spot'], fill=ACTIVE)
    c.txt(86, 222, 'now +0.38', 'spot')
    return c.im


def lower():
    c = surface()
    bands(c, False)
    c.txt(46, 22, 'edit >', 'spot')
    c.line((46, 35, 81, 35), 'spot')
    c.d.arc((137, 20, 149, 32), 305, 235, fill=c.p['spot'], width=1)
    c.line((143, 18, 143, 26), 'spot')
    c.txt(166, 22, '...', 'ghost')
    control(c, 46, 77, 132, 'freq', '440hz', .43, gap=24)
    control(c, 46, 143, 132, 'mix', '68%', .68, gap=24)
    for i, x in enumerate((18, 54, 90, 126), 1):
        c.txt(x, 219, str(i), 'spot' if i == 4 else 'ghost')
        if i == 4:
            c.line((x - 5, 233, x + 10, 233), 'spot')
    c.txt(304, 219, 'patch02 *', 'ghost', right=True)
    return c.im


def blur(image):
    # Host visual approximation, not an implemented dual-screen runtime cache.
    # Same native-pixel radius on both LCDs: 200x120 top, 160x120 bottom.
    w, h = image.width // 2, image.height // 2
    small = image.resize((w, h), Image.Resampling.BOX)
    for axis in (0, 1):
        src = small.load()
        dest = Image.new('RGB', (w, h))
        px = dest.load()
        for y in range(h):
            for x in range(w):
                a = src[max(0, x - 1), y] if axis == 0 else src[x, max(0, y - 1)]
                b = src[x, y]
                z = src[min(w - 1, x + 1), y] if axis == 0 else src[x, min(h - 1, y + 1)]
                px[x, y] = tuple((a[k] + 2 * b[k] + z[k] + 2) // 4 for k in range(3))
        small = dest
    small = small.point(lambda v: (v * 76 + 36 * 24 + 50) // 100)
    return small.resize(image.size, Image.Resampling.BILINEAR)


def opened(top, bottom):
    t, b = surface(400), surface()
    for c, base in ((t, top), (b, bottom)):
        c.im = blur(base)
        c.d = ImageDraw.Draw(c.im)
    t.heading(86, 47, 'ring', 'spot')
    b.txt(166, 22, '...', 'spot')
    b.line((175, 36, 202, 36, 202, 133), 'spot')
    for i, label in enumerate(('replace', 'copy', 'paste', 'delete')):
        y = 53 + i * 25
        role = 'spot' if i == 0 else 'ghost' if label == 'paste' else 'ink'
        b.line((202, y + 4, 212, y + 4), 'spot' if i == 0 else 'rail')
        b.txt(220, y, label, role)
        if i == 0:
            b.line((220, y + 12, 261, y + 12), 'spot')
    b.txt(16, 219, 'b close', 'ghost')
    return t.im, b.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    top, bottom = upper(), lower()
    menu_top, menu_bottom = opened(top, bottom)
    sheet = Image.new('RGB', (864, 604), OUTSIDE)
    caption(sheet, 16, 12, 'seance / v9 / unified columns + useful upper context')
    for x, label, t, b in ((16, '01 / names above / signal + modulation', top, bottom),
                            (448, '02 / both screens blur / sharp anchors', menu_top, menu_bottom)):
        caption(sheet, x, 38, label)
        sheet.paste(t, (x, 57))
        sheet.paste(b, (x + 40, 313))
    caption(sheet, 16, 569, 'upper: example input/output + sigil / lower: edit, bypass, stored controls')
    caption(sheet, 16, 587, 'static concept / analytic example signals, not engine telemetry / no runtime changes')
    path = OUT / 'main-unified-info-v9-2x.png'
    sheet.resize((1728, 1208), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
