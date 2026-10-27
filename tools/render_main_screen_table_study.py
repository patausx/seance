#!/usr/bin/env python3
"""One static lower-screen study, three states; no production changes.
Native 320x240, project Gossip28/6x8; font license assets/fonts/OFL.txt.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_universal_sigils_study import Surface, caption, OUTSIDE
from render_menu_style_study import heading_mask
from render_main_screen_focus_study import reference_blur

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_table_study'
NODES = {'gran': (38, 99), 'ring': (164, 85), 'chor': (243, 144), 'dly': (108, 169)}
PORTS = {'gran': ((24, 110), (92, 110)), 'ring': ((150, 96), (215, 96)),
         'chor': ((229, 155), (295, 155)), 'dly': ((94, 180), (148, 180))}


def curve(c, points, color, width=1):
    pts = []
    for i in range(101):
        t, u = i/100, 1-i/100
        pts.append(tuple(round(u*u*u*points[0][k]+3*u*u*t*points[1][k]
                               +3*u*t*t*points[2][k]+t*t*t*points[3][k]) for k in (0, 1)))
    c.d.line(pts, fill=color, width=width)


def arrow(c, x, y, color):
    c.d.line((x-3, y-2, x, y, x-3, y+2), fill=color)


def port(c, point, color, output=False, target=False):
    x, y = point
    c.d.ellipse((x-3, y-3, x+3, y+3), outline=color,
                fill=color if output else (36, 36, 36))
    if target:
        c.d.ellipse((x-7, y-7, x+7, y+7), outline=color)
        c.d.rectangle((x-1, y-1, x+1, y+1), fill=color)


def node(c, name, selected=False, target=False, details=True):
    x, y = NODES[name]
    left, right = PORTS[name]
    w, h = heading_mask(name).size
    role = 'spot' if selected else 'ink'
    color = c.p[role]
    # A short stem separates each large touch target from the draggable word.
    c.line((left[0]+4, left[1], x-5, left[1]), 'spot' if selected else 'rail')
    c.line((x+w+5, right[1], right[0]-4, right[1]), 'spot' if selected else 'rail')
    port(c, left, c.p['spot'] if selected or target else c.p['ghost'], target=target)
    port(c, right, color if selected else c.p['ghost'], output=True)
    c.heading(x, y, name, role)
    if selected:
        c.line((x, y+h+5, x+w-1, y+h+5), 'spot')
    if details:
        c.txt(x, y-13, f'{list(NODES).index(name)+1:02}', 'spot' if selected else 'ghost')
        if selected and name == 'ring':
            c.txt(x, y+h+12, 'a > freq', 'spot')


def chrome(c, state):
    c.heading(16, 10, 'patch table')
    c.txt(304, 23, 'patch02 *', 'ghost', right=True)
    c.txt(20, 51, 'chain', 'ghost')
    for i, x in enumerate((98, 146, 194, 242), 1):
        c.txt(x, 51, str(i), 'spot' if i == 4 else 'ghost')
        if i == 4:
            c.line((x-2, 64, x+8, 64), 'spot')
    c.txt(304, 76, '4 / 6', 'ghost', right=True)
    c.line((16, 216, 304, 216))
    if state == 'wire':
        c.txt(16, 227, 'release / order', 'spot')
        c.txt(304, 227, 'empty / cancel', 'ghost', right=True)
    else:
        c.txt(16, 227, 'hold move / tap actions', 'ghost')
        c.txt(304, 227, '+ add', 'spot', right=True)


def table(state='normal'):
    c = Surface(320, 'graphite')
    chrome(c, state)
    wire = state == 'wire'
    quiet = (65, 65, 65) if wire else (99, 99, 99)
    curve(c, ((92,110), (111,110), (124,96), (150,96)), quiet)
    curve(c, ((215,96), (260,96), (188,155), (229,155)), quiet)
    curve(c, ((295,155), (317,155), (309,204), (258,204)), quiet)
    curve(c, ((258,204), (179,209), (63,199), (94,180)), quiet)
    c.d.line((148,180, 184,180), fill=quiet)
    arrow(c, 182, 180, quiet)
    c.txt(193, 176, 'out', 'ghost')
    if wire:
        # Proposed drop moves gran immediately before chor in the SERIAL chain.
        # This is a tentative cable, not a new parallel route or committed change.
        curve(c, ((92,110), (118,133), (174,155), (229,155)), c.p['spot'], 2)
        arrow(c, 189, 152, c.p['spot'])
        c.txt(20, 195, 'gran > chor', 'spot')
    for name in NODES:
        node(c, name, selected=name == ('gran' if wire else 'ring'),
             target=wire and name == 'chor')
    return c.im


def actions(base):
    c = Surface(320, 'graphite')
    c.im = reference_blur(base)
    c.d = ImageDraw.Draw(c.im)
    # Redraw only the selected word/ports; no menu card or moved anchor.
    node(c, 'ring', selected=True, details=False)
    c.line((174, 117, 174, 212))
    for i, name in enumerate(('edit', 'bypass', 'copy', 'paste', 'delete', 'add')):
        y = 128 + i*16
        role = 'spot' if i == 0 else 'ghost' if name == 'paste' else 'ink'
        c.line((174, y+4, 185, y+4), 'spot' if i == 0 else 'rail')
        c.txt(193, y, name, role)
        if i == 0:
            c.line((193, y+12, 216, y+12), 'spot')
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    base = table()
    images = (('01 / free patch table', base),
              ('02 / pull from the output', table('wire')),
              ('03 / anchored actions', actions(base)))
    sheet = Image.new('RGB', (1024, 292), OUTSIDE)
    for i, (title, image) in enumerate(images):
        x = 16 + i*336
        caption(sheet, x, 10, title)
        sheet.paste(image, (x, 28))
    caption(sheet, 16, 278, 'seance / lower-screen concept / native 320x240 / not implemented')
    path = OUT / 'main-table-v3-2x.png'
    sheet.resize((sheet.width*2, sheet.height*2), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
