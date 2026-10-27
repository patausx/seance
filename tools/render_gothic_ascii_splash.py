#!/usr/bin/env python3
"""A splash concept using the actual wordmark in gothic print study 01.

The study was generated artwork, NOT an installed or distributable typeface.
Extract a binary wordmark from that artwork, then animate real ASCII glyphs
inside it. No shipping assets or platform/3ds/ui_splash.cpp are modified.

Run: python3 tools/render_gothic_ascii_splash.py
Requires Pillow and the existing reference-direction PNG. After the first
run the exported source mask can be reused without the full reference sheet.
"""
from math import cos, pi
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter
from render_logo_directions import font

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups' / 'gothic_ascii_splash'
SOURCE = ROOT / 'ui_mockups' / 'logo_gothic_print' / 'seance_reference_direction.png'
CROP = (55, 575, 510, 719)  # Only the wordmark in column 01, no emblem.
W, H = 400, 240
PAPER = (226, 190, 206)
INK = (31, 20, 31)
VIOLET = (120, 45, 176)
VIOLET_DEEP = (88, 35, 127)
FRAME_MS = 50
FRAMES = 80
REST = 10
WORD_W = 344
RADIUS = 78

# Actual ASCII punctuation rendered as a tiny 3x5 bitmap face. At 3DS native
# resolution these are deliberately simpler than the application's 6x8 font.
ASCII = {
    '#': ('101', '111', '101', '111', '101'),
    '+': ('000', '010', '111', '010', '000'),
    ':': ('000', '010', '000', '010', '000'),
    '/': ('001', '001', '010', '100', '100'),
    '*': ('101', '010', '111', '010', '101'),
    '=': ('000', '111', '000', '111', '000'),
    '%': ('101', '001', '010', '100', '101'),
    '.': ('000', '000', '000', '000', '010'),
}


def mix(a, b, t):
    return tuple(round(x + (y-x)*t) for x, y in zip(a, b))


def remove_specks(mask, minimum=6):
    """Drop tiny detached generation/compression flecks, not letter counters."""
    pixels = mask.load()
    seen = set()
    for y in range(mask.height):
        for x in range(mask.width):
            if not pixels[x, y] or (x, y) in seen:
                continue
            seen.add((x, y))
            pending = [(x, y)]
            component = []
            while pending:
                xx, yy = pending.pop()
                component.append((xx, yy))
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    point = (xx+dx, yy+dy)
                    nx, ny = point
                    if (0 <= nx < mask.width and 0 <= ny < mask.height and
                            point not in seen and pixels[nx, ny]):
                        seen.add(point)
                        pending.append(point)
            if len(component) < minimum:
                for point in component:
                    pixels[point] = 0
    return mask


def source_mask():
    exported = OUT / 'wordmark_01_source_mask.png'
    if SOURCE.exists():
        with Image.open(SOURCE) as image:
            mask = image.convert('L').crop(CROP).point(lambda v: 255 if v < 115 else 0)
        mask = remove_specks(mask)
        bounds = mask.getbbox()
        if bounds is None:
            raise ValueError('No wordmark found in the reference crop')
        mask = mask.crop(bounds)
        mask.save(exported)
        return mask
    if exported.exists():
        with Image.open(exported) as image:
            return image.convert('L')
    raise FileNotFoundError(f'Need {SOURCE} or {exported}')


def title_mask():
    original = source_mask()
    height = round(original.height * WORD_W / original.width)
    native = original.resize((WORD_W, height), Image.Resampling.LANCZOS)
    native = native.point(lambda v: 255 if v >= 128 else 0)
    # Optical upward offset rather than adding a baseline/subtitle to the splash.
    x, y = (W-WORD_W)//2, (H-height)//2-3
    mask = Image.new('L', (W, H), 0)
    mask.paste(native, (x, y))
    mask.save(OUT / 'wordmark_01_native_mask.png')
    alpha = Image.new('RGBA', (W, H), INK)
    alpha.putalpha(mask)
    alpha.save(OUT / 'wordmark_01_native_transparent.png')
    return mask


def progress(frame):
    if frame < REST:
        return 0.0
    if frame >= FRAMES-REST:
        return 1.0
    t = (frame-REST)/(FRAMES-2*REST-1)
    return t*t*(3-2*t)


def ascii_texture(tick):
    """Stable cells with slow discrete substitutions, never random flashing."""
    texture = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(texture)
    symbols = '#+/:*=#%:+/.'
    for row, y in enumerate(range(0, H, 6)):
        for column, x in enumerate(range(0, W, 4)):
            char = symbols[(column*7 + row*3 + tick) % len(symbols)]
            for gy, bits in enumerate(ASCII[char]):
                for gx, bit in enumerate(bits):
                    if bit == '1':
                        d.point((x+gx, y+gy), fill=255)
    return texture


def render(mask, frame):
    image = Image.new('RGB', (W, H), PAPER)
    image.paste(INK, (0, 0), mask)
    t = progress(frame)
    if t in (0.0, 1.0):
        return image
    bounds = mask.getbbox()
    x0, y0, x1, y1 = bounds
    center = x0-RADIUS + t*(WORD_W+2*RADIUS)
    # A 1px rim keeps the gothic silhouette legible during the breakup.
    interior = mask.filter(ImageFilter.MinFilter(3))
    ascii_bits = ascii_texture(frame//3)
    alpha = mask.load()
    inside = interior.load()
    texture = ascii_bits.load()
    out = image.load()
    for x in range(max(x0, int(center-RADIUS)), min(x1, int(center+RADIUS)+1)):
        distance = abs(x-center)/RADIUS
        if distance >= 1:
            continue
        wave = .5+.5*cos(pi*distance)
        # Leading/trailing shoulders tint before any ink disappears.
        tinted = mix(INK, VIOLET_DEEP, min(1.0, wave*1.8))
        dissolve = max(0.0, min(1.0, (wave-.24)/.58))
        for y in range(y0, y1):
            if not alpha[x, y]:
                continue
            if not inside[x, y]:
                out[x, y] = mix(INK, VIOLET_DEEP, wave*.9)
            elif texture[x, y]:
                out[x, y] = mix(tinted, VIOLET, dissolve)
            else:
                # Remove the flat fill; the remaining ink really is ASCII.
                out[x, y] = mix(tinted, PAPER, dissolve)
    return image


def dual(top):
    image = Image.new('RGB', (400, 496), (20, 16, 24))
    image.paste(top, (0, 0))
    image.paste(Image.new('RGB', (320, 240), PAPER), (40, 256))
    return image


def palette():
    # One deterministic palette across frames prevents GIF colour flicker.
    colors = [PAPER, INK, VIOLET, VIOLET_DEEP, (20, 16, 24)]
    for a, b in ((INK, VIOLET_DEEP), (VIOLET_DEEP, PAPER),
                 (VIOLET_DEEP, VIOLET), (INK, PAPER), (INK, VIOLET)):
        colors.extend(mix(a, b, i/47) for i in range(48))
    colors += [PAPER]*(256-len(colors))
    image = Image.new('P', (1, 1))
    image.putpalette([channel for color in colors for channel in color])
    return image


def save_gif(frames, path):
    pal = palette()
    converted = []
    for frame in frames:
        indexed = frame.quantize(palette=pal, dither=Image.Dither.NONE)
        # Pillow's fast palette lookup can map the exact paper RGB to a nearby
        # ramp entry. Pin unchanged paper pixels to palette index 0 explicitly.
        delta = ImageChops.difference(frame, Image.new('RGB', frame.size, PAPER))
        r, g, b = delta.split()
        paper_mask = ImageChops.lighter(ImageChops.lighter(r, g), b).point(
            lambda value: 255 if value == 0 else 0)
        indexed.paste(0, (0, 0), paper_mask)
        converted.append(indexed)
    converted[0].save(path, save_all=True, append_images=converted[1:],
                      duration=FRAME_MS, loop=0, disposal=2, optimize=False)


def phase_sheet(frames):
    sheet = Image.new('RGB', (880, 698), (242, 216, 226))
    d = ImageDraw.Draw(sheet)
    d.text((24, 15), 'seance / gothic → ascii → gothic', font=font(24), fill=INK)
    labels = ('01 / print', '02 / wave enters', '03 / ascii inside', '04 / restored')
    chosen = (0, 26, 40, 54)
    for i, (index, label) in enumerate(zip(chosen, labels)):
        x, y = 24+(i % 2)*432, 70+(i//2)*302
        d.text((x, y), label, font=font(16), fill=VIOLET_DEEP)
        sheet.paste(frames[index], (x, y+29))
    d.text((24, 675), '400 × 240 · blank lower screen · 4-second loop · study, not runtime',
           font=font(13), fill=VIOLET_DEEP)
    sheet.save(OUT / 'gothic_ascii_phases.png')


def verify(mask, frames):
    assert {color for _, color in mask.getcolors(256)} <= {0, 255}
    assert ImageChops.difference(frames[0], frames[-1]).getbbox() is None
    outside = ImageChops.invert(mask)
    for frame in frames:
        assert frame.size == (400, 240)
        difference = ImageChops.difference(frame, frames[0]).convert('L')
        assert ImageChops.multiply(difference, outside).getbbox() is None, 'Effect leaked outside letters'
    mid = frames[40]
    assert ImageChops.difference(mid, frames[0]).getbbox() is not None
    assert VIOLET in {color for _, color in mid.getcolors(W*H)}, 'No violet ASCII pixels'
    print('verified: binary mask; effect clipped to letters; purple ASCII; seamless endpoints')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    mask = title_mask()
    frames = [render(mask, frame) for frame in range(FRAMES)]
    verify(mask, frames)
    frames[0].save(OUT / 'gothic_splash_static.png')
    frames[40].save(OUT / 'gothic_ascii_mid.png')
    frames[40].resize((800, 480), Image.Resampling.NEAREST).save(OUT / 'gothic_ascii_mid_2x.png')
    Image.new('RGB', (320, 240), PAPER).save(OUT / 'gothic_splash_bottom.png')
    save_gif(frames, OUT / 'gothic_ascii_top.gif')
    save_gif([dual(f) for f in frames], OUT / 'gothic_ascii_dual.gif')
    save_gif([f.resize((800, 480), Image.Resampling.NEAREST) for f in frames], OUT / 'gothic_ascii_top_2x.gif')
    save_gif([dual(f).resize((800, 992), Image.Resampling.NEAREST) for f in frames],
             OUT / 'gothic_ascii_dual_2x.gif')
    phase_sheet(frames)
    print(OUT / 'gothic_ascii_top_2x.gif')


if __name__ == '__main__':
    main()
