#!/usr/bin/env python3
"""Exactly two matched 8s ferrofluid GIFs: native ASCII and coarse pixels.

Same geometry, frames, envelope, backdrop and navigation as the prior proposal.
Only the object is restyled; no runtime edits or real audio analysis.
"""
from pathlib import Path
from PIL import Image, ImageChops
from render_go_to_ferrofluid_motion_study import liquid, FPS, SECONDS
from render_go_to_upper_states_study import background
from render_go_to_unified_study import lower
from render_module_geometry_study import FONT
from render_main_screen_centered_study import caption, OUTSIDE

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/go_to_overlay_study'
ROI = (80, 0, 320, 240)
CHARS = '.:-+*#@'
ASCII_GRAY = [(v,v,min(255,v+6)) for v in (108,128,148,170,194,218,242)]
ASCII_SPOT = [(round(181*s),round(154*s),round(232*s)) for s in (.65,.71,.77,.83,.89,.95,1.)]
PIXEL_GRAY = [(14,14,19),(28,27,35),(45,43,54),(69,67,80),
              (101,98,113),(141,137,155),(186,184,203),(234,234,244)]
PIXEL_SPOT = [tuple(round(a*.65+b*.35) for a,b in zip(color,(181,154,232)))
              for color in PIXEL_GRAY]


def glyph(char):
    mask = Image.new('L',(6,8))
    for row,bits in enumerate(FONT[ord(char)-32]):
        for col in range(6):
            if bits & (1 << (5-col)):
                mask.putpixel((col,row),255)
    return mask


GLYPHS = [glyph(c) for c in CHARS]


def tone(color):
    r,g,b,_ = color
    luminance = (54*r+183*g+19*b)/256
    value = max(0.,min(1.,(luminance-12)/175))**.72
    violet = r-g > 3 and b-r > 10
    return value,violet


class Ascii:
    def __init__(self):
        self.levels = [-1]*(40*30)

    def apply(self, base, grid, draw=True):
        image = base.copy() if draw else None
        for i,color in enumerate(grid.getdata()):
            previous = self.levels[i]
            # Silhouette entry/exit hysteresis, not random symbol substitution.
            threshold = 56 if previous >= 0 else 96
            if color[3] < threshold:
                self.levels[i] = -1
                continue
            value,violet = tone(color)
            raw = value*(len(CHARS)-1)
            chosen = max(0,min(len(CHARS)-1,round(raw)))
            if previous >= 0 and previous-.72 <= raw <= previous+.72:
                chosen = previous
            self.levels[i] = chosen
            if draw:
                color = (ASCII_SPOT if violet else ASCII_GRAY)[chosen]
                image.paste(color,(80+(i%40)*6,(i//40)*8),GLYPHS[chosen])
        return image


def pixels(base, layer):
    small = layer.crop(ROI).resize((60,60),Image.Resampling.BOX)
    mapped = Image.new('RGBA',small.size)
    values = []
    for color in small.getdata():
        if color[3] < 100:
            values.append((0,0,0,0))
        else:
            value,violet = tone(color)
            chosen = max(0,min(7,round(value*7)))
            values.append((*((PIXEL_SPOT if violet else PIXEL_GRAY)[chosen]),255))
    mapped.putdata(values)
    mapped = mapped.resize((240,240),Image.Resampling.NEAREST)
    image = base.copy()
    image.paste(mapped,(80,0),mapped)
    return image


def layout(bottom, kind):
    sheet = Image.new('RGB',(432,590),OUTSIDE)
    title = '01 / magnetic ink / ascii' if kind == 'ascii' else '02 / magnetic ink / coarse pixels'
    caption(sheet,16,12,title)
    caption(sheet,16,29,'same rotation, spikes, blur and lower menu')
    sheet.paste(bottom,(56,304))
    caption(sheet,16,557,'native 6x8 glyphs / silhouette only' if kind == 'ascii'
            else 'native 4x4 blocks / eight ink tones')
    caption(sheet,16,575,'8s demo / synthetic envelope / not runtime')
    return sheet


def verify(path, expected_bottom=None):
    with Image.open(path) as result:
        first = result.convert('RGB')
        bottom = first.crop((112,608,752,1088)).tobytes()
        if expected_bottom is not None:
            assert bottom == expected_bottom, 'identical menu between treatments'
        duration,changing = 0,False
        for index in range(result.n_frames):
            result.seek(index)
            duration += result.info.get('duration',0)
            difference = ImageChops.difference(first,result.convert('RGB')).getbbox()
            if difference:
                changing = True
                assert 192 <= difference[0] < difference[2] <= 672
                assert 96 <= difference[1] < difference[3] <= 576
        assert changing and duration == 8000 and result.info['loop'] == 0
        count = result.n_frames
    print(f'{path.relative_to(ROOT)} / {duration}ms / {count} frames / {path.stat().st_size} bytes')
    return bottom


def main():
    shared = background()
    base = shared.crop((0,0,400,240))
    bottom = lower(shared.crop((40,256,360,496)),current='sequencer')
    clear = Image.new('RGBA',(400,240),(0,0,0,0))
    # Render once, feed EXACTLY the same unquantized surface to both variants.
    layers = [liquid(clear,(index/FPS+.8)%SECONDS) for index in range(FPS*SECONDS)]
    grids = [layer.crop(ROI).resize((40,30),Image.Resampling.BOX) for layer in layers]
    layouts = {kind:layout(bottom,kind) for kind in ('ascii','pixels')}

    # One global palette shared by both files keeps the background/menu equal.
    atlas = Image.new('RGB',(864,590+4*240),OUTSIDE)
    for column,kind in enumerate(('ascii','pixels')):
        atlas.paste(layouts[kind],(column*432,0))
        ascii_sample = Ascii()
        for row,index in enumerate((0,20,57,130)):
            top = ascii_sample.apply(base,grids[index]) if kind == 'ascii' else pixels(base,layers[index])
            atlas.paste(top,(column*432+16,590+row*240))
    quantized = atlas.quantize(colors=208,method=Image.Quantize.MEDIANCUT)
    fixed = ASCII_GRAY+ASCII_SPOT+PIXEL_GRAY+PIXEL_SPOT
    fixed += [(244,244,244),(181,154,232),(184,184,184),(94,94,94),OUTSIDE,(184,180,192)]
    fixed = (fixed+[OUTSIDE]*48)[:48]
    palette = Image.new('P',(1,1))
    palette.putpalette(quantized.getpalette()[:208*3]+[v for color in fixed for v in color])

    OUT.mkdir(parents=True,exist_ok=True)
    expected_bottom = None
    for kind in ('ascii','pixels'):
        ascii_state = Ascii()
        # Warm a full cycle of glyph state without exporting additional media.
        if kind == 'ascii':
            for grid in grids:
                ascii_state.apply(base,grid,draw=False)
        frames = []
        for index,layer in enumerate(layers):
            top = ascii_state.apply(base,grids[index]) if kind == 'ascii' else pixels(base,layer)
            frame = layouts[kind].copy()
            frame.paste(top,(16,48))
            frame = frame.quantize(palette=palette,dither=Image.Dither.NONE)
            frames.append(frame.resize((864,1180),Image.Resampling.NEAREST))
        path = OUT/f'go-to-ferrofluid-{kind}-2x.gif'
        frames[0].save(path,save_all=True,append_images=frames[1:],duration=50,loop=0,disposal=1,optimize=False)
        expected_bottom = verify(path,expected_bottom)


if __name__ == '__main__':
    main()
