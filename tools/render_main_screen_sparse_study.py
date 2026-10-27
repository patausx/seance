#!/usr/bin/env python3
"""Three static sparse-chain proposals; example scalars, not runtime/telemetry.

Native 400x240 + 320x240, graphite, existing Gossip28 / 6x8, nearest 2x.
Run this file only: imported study modules do not execute their generators.
"""
from pathlib import Path
from PIL import Image
from render_universal_sigils_study import caption, OUTSIDE
from render_main_screen_tiles_study import surface, control, handle, ACTIVE, RANGE
from render_main_screen_accordion_study import center_text

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/main_screen_sparse_study'

# name, stored label, stored position, effective label, effective position.
PLAY = (
    ('position', '38%', .38, '46%', .46),
    ('speed', '1.00x', .50, '1.00x', .50),
    ('gain', '75%', .75, '75%', .75),
    ('start', '0%', .0, '0%', .0),
)
MOUTH = (
    ('vowel', 'e', .25, 'e > i', .34),
    ('size', '1.00x', .50, '1.00x', .50),
    ('mix', '72%', .72, '72%', .72),
)


def band(c, a, b, top=False, active=False):
    offset = 40 if top else 0
    a, b = a + offset, b + offset
    y0, y1 = (40, 239) if top else (0, 197)
    role = 'spot' if active else 'rail'
    if active:
        c.d.rectangle((a + 2, y0, b - 3, y1), fill=ACTIVE)
    c.line((a + 2, y0, a + 2, y1), role)
    c.line((b - 3, y0, b - 3, y1), role)
    y = y0 if top else y1
    c.line((a + 2, y, b - 3, y), role)


def live_param(c, x, y, w, data, mod=False):
    name, stored, base, effective, live = data
    c.txt(x, y, name, 'ghost')
    c.txt(x + w, y, effective, 'spot' if mod else 'ink', right=True)
    ry = y + 22
    if mod:
        c.d.rectangle((x + round(.18*w), ry-2, x + round(.65*w), ry+2), fill=RANGE)
    c.line((x, ry, x + w - 1, ry))
    for tx in (x, x+w-1):
        c.line((tx, ry-2, tx, ry+2))
    bx, lx = x + round(base*(w-1)), x + round(live*(w-1))
    c.line((bx, ry-5, bx, ry+5), 'ghost')
    c.d.rectangle((lx-1, ry-3, lx+1, ry+3), fill=c.p['spot'] if mod else c.p['ink'])
    c.txt(x, y + 33, 'target', 'ghost')
    c.txt(x+w, y + 33, effective, 'ghost', right=True)


def route(c, x, w, name, source):
    c.line((x, 181, x+w-1, 181))
    c.txt(x, 192, source + ' > ' + name, 'spot')
    c.txt(x, 208, 'amount +18%', 'ghost')
    c.txt(x, 224, 'source +0.44', 'spot')


def upper_panel(c, a, b, name, values, source, expanded=False):
    x, w = 40+a+14, b-a-28
    c.heading(x, 49, name, 'spot' if name == 'play' else 'ink')
    if expanded:
        col = (w-24)//2
        for i, item in enumerate(values):
            live_param(c, x+(i%2)*(col+24), 87+(i//2)*47, col, item, i == 0)
    else:
        for i, item in enumerate(values[:2]):
            live_param(c, x, 87+i*47, w, item, i == 0)
    route(c, x, w, values[0][0], source)


def tiny_top(c, a, b):
    cx = 40 + (a+b)//2
    center_text(c, cx, 56, 'mout', 'ink')
    for dx, value in ((-5, .34), (5, .5)):
        c.line((cx+dx, 103, cx+dx, 156))
        y = 156-round(value*53)
        c.line((cx+dx-2, y, cx+dx+2, y), 'spot')
    center_text(c, cx, 193, 'b', 'spot')


def actions(c, a, b):
    c.txt(a+14, 20, 'edit >', 'spot')
    c.line((a+14, 33, a+49, 33), 'spot')
    px = b-45
    c.d.arc((px-6, 18, px+6, 30), 305, 235, fill=c.p['ghost'])
    c.line((px, 16, px, 23), 'ghost')
    c.txt(b-28, 20, '...', 'ghost')


def lower_panel(c, a, b, values, expanded=False, three=False):
    actions(c, a, b)
    x, w = a+14, b-a-28
    if expanded:
        col = (w-24)//2
        for i, item in enumerate(values):
            control(c, x+(i%2)*(col+24), 68+(i//2)*70, col, *item[:3], gap=24)
    else:
        count = 3 if three else 2
        for i, item in enumerate(values[:count]):
            control(c, x, (55+i*45) if three else (77+i*66), w, *item[:3], gap=22)
    handle(c, (a+b)//2, 187, 'ghost')


def footer(c):
    for i, x in enumerate((18, 50, 82, 114), 1):
        c.txt(x, 219, str(i), 'spot' if i == 4 else 'ghost')
        if i == 4:
            c.line((x-5, 233, x+10, 233), 'spot')
    c.txt(159, 219, '+ add', 'spot')
    c.txt(305, 219, 'patch02 *', 'ghost', right=True)


def chain_zone(t, b):
    # A proposed utility zone, not a third module; no invented audio meters.
    x, end = 246, 346
    t.heading(x, 49, 'chain')
    t.txt(end, 57, '4', 'spot', right=True)
    t.txt(x, 88, '2 / 6 modules', 'ghost')
    t.line((x, 108, end, 108))
    t.txt(x, 121, 'output', 'ghost')
    t.txt(end, 121, '-6db', 'ink', right=True)
    t.txt(x, 145, 'mute', 'ghost')
    t.txt(end, 145, 'off', 'ink', right=True)
    t.line((x, 181, end, 181))
    t.txt(x, 192, 'quick control', 'ghost')
    t.txt(x, 208, 'mouth > vowel', 'spot')
    t.txt(x, 224, 'stored e', 'ghost')
    x, w = 206, 100
    b.txt(x, 20, 'mute', 'ink')
    b.txt(x+w, 20, 'off', 'ghost', right=True)
    control(b, x, 77, w, 'level', '-6db', .67, gap=22)
    b.txt(x, 122, 'mouth', 'spot')
    control(b, x, 143, w, 'vowel', 'e', .25, gap=22)


def render(kind):
    top, bottom = surface(400), surface()
    top.heading(40, 8, 'patch table')
    top.txt(359, 18, '2 / 6', 'ghost', right=True)
    if kind == 'equal':
        columns = ((0, 160), (160, 320))
    elif kind == 'focus':
        columns = ((0, 288), (288, 320))
    else:
        columns = ((0, 160), (160, 192))
    assert columns[0][0] == 0 and columns[0][1] == columns[1][0]
    assert columns[-1][1] + (128 if kind == 'chain' else 0) == 320
    for i, (a, b) in enumerate(columns):
        band(top, a, b, top=True, active=i == 0)
        band(bottom, a, b, active=i == 0)
    upper_panel(top, *columns[0], 'play', PLAY, 'a', kind == 'focus')
    lower_panel(bottom, *columns[0], PLAY, kind == 'focus', kind == 'equal')
    if kind == 'equal':
        upper_panel(top, *columns[1], 'mouth', MOUTH, 'b')
        lower_panel(bottom, *columns[1], MOUTH, three=True)
    else:
        tiny_top(top, *columns[1])
        bottom.chevron(sum(columns[1])//2, 94)
        handle(bottom, sum(columns[1])//2, 187)
    if kind == 'chain':
        chain_zone(top, bottom)
    footer(bottom)
    return top.im, bottom.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for kind, title, note in (
        ('equal', '01 / two open instruments', '160 + 160 / three controls on each'),
        ('focus', '02 / focus fills the space', '288 + 32 / four controls on selection'),
        ('chain', '03 / chain utility zone', '160 + 32 + 128 / level + quick control'),
    ):
        top, bottom = render(kind)
        sheet = Image.new('RGB', (432, 590), OUTSIDE)
        caption(sheet, 16, 12, title)
        caption(sheet, 16, 29, 'same patch / play > mouth / graphite')
        sheet.paste(top, (16, 48))
        sheet.paste(bottom, (56, 304))
        caption(sheet, 16, 557, note)
        caption(sheet, 16, 575, 'static concept / example values / not runtime')
        path = OUT / f'{kind}-2x.png'
        sheet.resize((864, 1180), Image.Resampling.NEAREST).save(path)
        print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
