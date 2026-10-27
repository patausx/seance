#!/usr/bin/env python3
"""One static Mouth concept, not runtime, measured response or audio capture."""
from pathlib import Path
import math
from PIL import Image
from render_module_geometry_study import Canvas
from render_menu_style_study import heading_mask
from render_theme_picker_study import Page, GRAY, rail, mix

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/mouth_study/mouth-v1-2x.png'
T = .34  # fraction of the first a -> e segment, not the full vowel rail
RESONANCE = .62
# Illustrative vowel anchors, not an approved DSP/phonetic specification.
A, E = (730, 1090, 2440), (530, 1840, 2480)
FORMANTS = tuple(a * (e/a)**T for a, e in zip(A, E))


def upper():
    c = Page(400, GRAY)
    c.title(20, 12, 'mouth')
    c.txt(380, 25, 'ch01 / 02', c.muted, right=True)
    c.line((20, 45, 380, 45))
    c.txt(20, 56, 'a > e / 34%', c.accent)
    c.txt(380, 56, 'formant model', c.muted, right=True)
    x0, x1, zero, height = 30, 372, 169, 80
    def freq(x):
        return 100 * 50 ** ((x-x0)/(x1-x0))
    def px(f):
        return x0 + round(math.log(f/100, 50)*(x1-x0))
    # Log-frequency Gaussian envelopes: only a design illustration, not an
    # implemented filter magnitude response. Fixed scale, no peak normalization.
    width = .18 - .10*RESONANCE
    weights = (1., .83, .65)
    curves = []
    for f, weight in zip(FORMANTS, weights):
        curves.append([weight*math.exp(-.5*(math.log(freq(x)/f)/width)**2)
                       for x in range(x0, x1+1)])
    envelope = [min(1.15, sum(v)) for v in zip(*curves)]
    shade = mix(c.paper, c.accent, .22)
    for i, value in enumerate(envelope):
        x = x0+i
        if x % 3 == 0:
            c.line((x, zero-round(value*height), x, zero), shade)
    c.line((x0, zero, x1, zero))
    for f, label in ((100,'100'),(500,'500'),(1000,'1k'),(5000,'5k')):
        x = px(f)
        c.line((x, zero-2, x, zero+3), c.muted)
        c.txt(x-len(label)*3, 180, label, c.muted)
    for n, (f, values) in enumerate(zip(FORMANTS, curves), 1):
        x = px(f)
        y = zero-round(max(values)*height)
        for yy in range(y+9, zero-3, 5):
            c.d.point((x, yy), fill=c.rule)
        c.txt(x-6, y-15, 'f'+str(n), c.muted)
    pts = [(x0+i, zero-round(value*height)) for i, value in enumerate(envelope)]
    c.d.line(pts, fill=c.accent, width=1)
    for f in FORMANTS:
        x = px(f)
        y = zero-round(envelope[x-x0]*height)
        c.d.polygon(((x,y-2),(x+2,y),(x,y+2),(x-2,y)), fill=c.accent)
    for x, n, f in zip((20, 148, 276), (1,2,3), FORMANTS):
        c.txt(x, 206, f'f{n} {round(f)}hz', c.ink)
    c.txt(20, 230, 'model / not audio', c.muted)
    c.txt(380, 230, 'size 1.00x', c.muted, right=True)
    return c.im


def lower():
    c = Page(320, GRAY)
    c.title(16, 10, 'vowel', c.accent)
    c.txt(304, 23, 'a > e / 34%', c.accent, right=True)
    # Five anchor letters belong to one continuous control, not five mode tabs.
    for i, letter in enumerate('aeiou'):
        mask = heading_mask(letter)
        mask = mask.resize((mask.width*2, mask.height*2), Image.Resampling.NEAREST)
        x = 32+64*i
        color = c.accent if i < 2 else c.muted
        c.im.paste(color, (x-mask.width//2, 89-mask.height), mask)
    c.line((32, 103, 288, 103), c.ink)
    for x in range(32, 289, 64):
        c.line((x, 101, x, 106), c.muted)
    cursor = round(32+64*T)
    c.d.polygon(((cursor,98),(cursor+5,103),(cursor,108),(cursor-5,103)), fill=c.accent)
    c.line((cursor, 112, cursor, 116), c.accent)
    c.txt(16, 121, 'continuous morph', c.muted)
    c.txt(304, 121, 'sigil: none', c.muted, right=True)
    c.line((16, 141, 304, 141))
    for x, label, value, amount in ((16,'size','1.00x',.5),
                                    (116,'resonance','62%',RESONANCE),
                                    (216,'mix','100%',1.)):
        c.txt(x, 155, label, c.muted)
        c.txt(x, 173, value)
        rail(c, x, 194, 88, amount)
    c.txt(16, 209, 'pitch unchanged', c.muted)
    c.txt(304, 209, 'byp', c.muted, right=True)
    c.line((16, 223, 304, 223))
    c.txt(16, 231, 'b back', c.muted)
    c.txt(304, 231, 'l: mod >', c.muted, right=True)
    return c.im


def main():
    sheet = Canvas(440, 580, (17,17,20))
    sheet.text(20, 16, 'seance / mouth / formant filter', (181,154,232))
    sheet.im.paste(upper(), (20, 40))
    sheet.im.paste(lower(), (60, 296))
    sheet.text(20, 554, 'concept only / native screens / no runtime changes', (170,170,178))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    sheet.im.resize((880,1160), Image.Resampling.NEAREST).save(OUT)
    print(OUT)


if __name__ == '__main__':
    main()
