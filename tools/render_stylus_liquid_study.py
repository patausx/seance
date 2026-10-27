#!/usr/bin/env python3
"""One authored liquid stylus animation, not an input replay or 3DS benchmark.

Pillow only. A local supersampled scalar field merges four moving kernels into
one soft contour, colored per pixel by the underlying screen luminance.
Native 320x240 composition, nearest x2 presentation, fixed GIF palette. The x6
detail is a crop of the SAME native composition, not a prettier substitute.
"""
from pathlib import Path
import math
from PIL import Image, ImageChops, ImageDraw
from render_module_geometry_study import Canvas

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/stylus_liquid_study'
SOURCE = ROOT / 'ui_mockups/graphite_accent_runtime/ui/gray/osc/02_tri_saw_mod_bottom.ppm'
TARGET = OUT / 'stylus-liquid-adaptive-v2-2x.gif'
DT, COUNT = .02, 450
SIDE, SS = 64, 2
OUTSIDE = (24, 20, 29)
PAPER = (36, 36, 36)
INK, MUTED, SPOT = (226, 216, 232), (155, 145, 164), (181, 154, 232)


def smooth(a, b, x):
    u = max(0., min(1., (x-a)/(b-a)))
    return u*u*(3-2*u)


def bezier(points, t):
    u = smooth(0, 1, t)
    weights = ((1-u)**3, 3*(1-u)**2*u, 3*(1-u)*u*u, u**3)
    return tuple(sum(p[axis]*w for p, w in zip(points, weights)) for axis in range(2))


def contact(t):
    if t < 1.3:
        return 72., 112.
    if t < 3.3:
        return bezier(((72, 112), (80, 54), (230, 68), (240, 148)), (t-1.3)/2)
    if t < 3.8:
        return 240., 148.
    if t < 5.8:
        return bezier(((240, 148), (222, 207), (89, 182), (156, 148)), (t-3.8)/2)
    return 156., 148.


def stage(t):
    if t < .5 or t >= 7.65:
        return 'pause'
    if t < .75:
        return 'touch'
    if 1.3 <= t < 3.3 or 3.8 <= t < 5.8:
        return 'flow'
    if t >= 7.3:
        return 'release'
    return 'settle'


def mask(t, followers):
    x, y = contact(t)
    origin = (math.floor(x)-SIDE//2, math.floor(y)-SIDE//2)
    blank = Image.new('RGBA', (SIDE, SIDE))
    if t < .5 or t >= 7.65:
        return blank, origin
    arrive = smooth(.5, .72, t)
    release = smooth(7.3, 7.65, t)
    scale = (.25+.75*arrive) * (1-.85*release)
    breath = 1+.035*math.sin(t*math.tau/1.7)
    kernels = [(x, y, 4.3*breath, 1.)]
    previous = (x, y)
    for i, (fx, fy) in enumerate(followers):
        # Lag belongs only to the shell. The primary kernel is always at contact.
        dx, dy = fx-x, fy-y
        distance = math.hypot(dx, dy)
        if distance > 23:
            dx, dy = dx*23/distance, dy*23/distance
        phase = t*(2.1-.25*i)+i*2.1
        orbit = (3.4, 4.1, 3.3)[i]
        cx, cy = x+dx+orbit*math.cos(phase), y+dy+orbit*math.sin(phase)
        # Adjacent masses remain connected even during a fast turn: no loose dot.
        gap = math.hypot(cx-previous[0], cy-previous[1])
        if gap > 5.8:
            cx = previous[0]+(cx-previous[0])*5.8/gap
            cy = previous[1]+(cy-previous[1])*5.8/gap
        kernels.append((cx, cy, (3.6, 3.1, 2.6)[i], (.86, .76, .62)[i]))
        previous = (cx, cy)
    # Contract the entire connected shape, not four independent fading dots.
    kernels = [(x+(cx-x)*scale, y+(cy-y)*scale, sigma*scale, weight)
               for cx, cy, sigma, weight in kernels]
    n = SIDE*SS
    axes = []
    for cx, cy, sigma, weight in kernels:
        inverse = 1/(2*sigma*sigma)
        xs = [weight*math.exp(-((origin[0]+(p+.5)/SS-cx)**2)*inverse) for p in range(n)]
        ys = [math.exp(-((origin[1]+(p+.5)/SS-cy)**2)*inverse) for p in range(n)]
        axes.append((xs, ys))
    pixels = []
    opacity = arrive*(1-release)
    for py in range(n):
        row = [(xs, ys[py]) for xs, ys in axes]
        for px in range(n):
            field = sum(xs[px]*wy for xs, wy in row)
            halo = .14*smooth(.025, .42, field)
            body = .55*smooth(.35, .76, field)
            core = .16*smooth(.85, 1.8, field)
            alpha = round(255*(halo+body+core)*opacity)
            if not alpha:
                pixels.append((0, 0, 0, 0))
                continue
            dense = smooth(.3, 1.7, field)
            color = tuple(round(a+(b-a)*dense) for a, b in zip((133, 103, 180), (209, 188, 243)))
            pixels.append((*color, alpha))
    high = Image.new('RGBA', (n, n))
    high.putdata(pixels)
    return high.resize((SIDE, SIDE), Image.Resampling.BOX), origin


def reactive_color(color, background):
    # Local pigment inversion: no whole-cursor threshold switch or flashing.
    luminance = .2126*background[0]+.7152*background[1]+.0722*background[2]
    amount = smooth(70, 178, luminance)
    return tuple(round(c+(dark-c)*amount) for c, dark in zip(color, (43, 24, 62)))


def adapt(layer, origin, source):
    result = layer.copy()
    pixels = result.load()
    background = source.load()
    for y in range(SIDE):
        for x in range(SIDE):
            r, g, b, a = pixels[x, y]
            if a:
                bg = background[origin[0]+x, origin[1]+y]
                pixels[x, y] = (*reactive_color((r, g, b), bg), a)
    return result


def board():
    c = Canvas(1120, 620, OUTSIDE)
    c.text(24, 22, 'seance / adaptive ink', INK, scale=2)
    c.text(24, 58, '2x / touch screen', MUTED)
    c.text(704, 58, '6x / same native crop', MUTED)
    c.text(704, 494, 'soft contour / one connected body', SPOT)
    c.text(704, 516, 'dark paper > light pigment', MUTED)
    c.text(704, 536, 'light marks > dark pigment', MUTED)
    c.text(24, 578, 'motion concept / scripted gesture / controls unchanged', MUTED)
    c.text(24, 598, 'local field / graphite accent / not a console capture', MUTED)
    return c.im


def palette(background, base):
    # Preserve the host screen verbatim. Fill remaining slots with the ink ramp
    # over both dark paper and bright UI so alpha crossing a rail stays smooth.
    colors = list(dict.fromkeys(list(background.get_flattened_data()) + list(base.get_flattened_data())))
    required = len(colors)
    backgrounds = list(dict.fromkeys(source for source in background.get_flattened_data()))
    steps = max(2, (256-required)//len(backgrounds))
    for bg in backgrounds:
        for step in range(steps):
            field = step/(steps-1)
            alpha = .85*field
            rgb = tuple(round(a+(b-a)*field) for a, b in zip((133, 103, 180), (209, 188, 243)))
            rgb = reactive_color(rgb, bg)
            c = tuple(round(a*(1-alpha)+b*alpha) for a, b in zip(bg, rgb))
            if c not in colors:
                colors.append(c)
    assert required < 50 and len(colors) <= 256, (required, len(colors))
    colors += [OUTSIDE]*(256-len(colors))
    result = Image.new('P', (1, 1))
    result.putpalette([channel for rgb in colors for channel in rgb])
    return result


def main():
    if TARGET.exists():
        raise SystemExit('preserve the existing study; choose a new version')
    source = Image.open(SOURCE).convert('RGB')
    assert source.size == (320, 240)
    base = board()
    pal = palette(source, base)
    frames = []
    followers = [list(contact(0)) for _ in range(3)]
    previous = None
    max_local_size = 0
    for i in range(COUNT):
        t = i*DT
        x, y = contact(t)
        for point, tau in zip(followers, (.055, .105, .16)):
            k = 1-math.exp(-DT/tau)
            point[0] += (x-point[0])*k
            point[1] += (y-point[1])*k
        layer, origin = mask(t, followers)
        assert layer.getchannel('A').getextrema()[1] <= 217
        assert layer.getchannel('A').crop((0, 0, SIDE, 1)).getbbox() is None
        layer = adapt(layer, origin, source)
        native = source.convert('RGBA')
        native.alpha_composite(layer, dest=origin)
        native = native.convert('RGB')
        diff = ImageChops.difference(native, source).getbbox()
        if diff:
            assert origin[0] <= diff[0] <= diff[2] <= origin[0]+SIDE
            assert origin[1] <= diff[1] <= diff[3] <= origin[1]+SIDE
            max_local_size = max(max_local_size, diff[2]-diff[0], diff[3]-diff[1])
        detail = (Image.new('RGB', (SIDE, SIDE), PAPER) if stage(t) == 'pause' else
                  native.crop((*origin, origin[0]+SIDE, origin[1]+SIDE)))
        im = base.copy()
        im.paste(native.resize((640, 480), Image.Resampling.NEAREST), (24, 82))
        im.paste(detail.convert('RGB').resize((384, 384), Image.Resampling.NEAREST), (704, 82))
        label = Canvas(384, 16, OUTSIDE)
        label.text(0, 0, stage(t), SPOT, scale=2)
        im.paste(label.im, (704, 24))
        quantized = im.quantize(palette=pal, dither=Image.Dither.NONE)
        # Frozen text/background must not flicker due to adaptive GIF palettes.
        assert quantized.convert('RGB').crop((24, 82, 80, 100)).tobytes() == source.crop((0, 0, 28, 9)).resize((56, 18), Image.Resampling.NEAREST).tobytes()
        if previous is not None and quantized.tobytes() == previous.tobytes():
            frames[-1][1] += 20
        else:
            frames.append([quantized, 20])
        previous = quantized
    assert frames[0][0].tobytes() == frames[-1][0].tobytes()
    assert sum(ms for _, ms in frames) == 9000
    OUT.mkdir(parents=True, exist_ok=True)
    frames[0][0].save(TARGET, save_all=True, append_images=[im for im, _ in frames[1:]],
                       duration=[ms for _, ms in frames], loop=0, optimize=False, disposal=1)
    with Image.open(TARGET) as gif:
        duration = 0
        for i in range(gif.n_frames):
            gif.seek(i)
            duration += gif.info['duration']
        assert duration == 9000 and gif.size == base.size
        print(f'{TARGET}\n{gif.n_frames} stored frames / {duration}ms / max local extent {max_local_size}px / {TARGET.stat().st_size} bytes')


if __name__ == '__main__':
    main()
