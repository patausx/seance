#!/usr/bin/env python3
"""Design-only text motion. Fresh C++ glyph pixels; authored animation, not runtime.

Existing font derivative: Deborah Khodanovich 2025 / Seance contributors,
SIL OFL 1.1, see assets/fonts/OFL.txt. No font or application edits.
"""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import math
from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/start_menu_text_motion_study/v1'
MS, DURATION = 40, 16000
EVENTS = [(2000, 'library'), (5200, None), (6400, 'settings'),
          (9600, 'library'), (12400, 'settings'), (14800, None)]
COLORS = {
    'graphite': {'ink': (244, 244, 244), 'accent': (181, 154, 232), 'ghost': (184, 184, 184)},
    'pink': {'ink': (31, 20, 31), 'accent': (91, 49, 124), 'ghost': (126, 100, 119)},
}

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def ease(x):
    x = max(0., min(1., x))
    return x*x*(3-2*x)

def lerp(a, b, t):
    return tuple(round(x+(y-x)*t) for x, y in zip(a, b))

def state(t):
    old, new, at = None, None, -10000
    for when, panel in EVENTS:
        if t < when:
            break
        old, new, at = new, panel, when
    return old, new, t-at

class Study:
    def __init__(self, theme):
        self.theme, self.colors = theme, COLORS[theme]
        folder = OUT/'source-capture'/theme
        self.sources = {name: Image.open(folder/(n+'_bottom.ppm')).convert('RGB')
                        for name, n in [('home', '01-home'), ('library', '03-library'), ('settings', '07-settings')]}
        # Capture mode intentionally freezes paper. Sample its empty last column.
        self.paper = Image.new('RGB', (320, 240))
        d = ImageDraw.Draw(self.paper)
        for y in range(240):
            d.line((0, y, 319, y), fill=self.sources['home'].getpixel((319, y)))
        self.labels = []
        for i, (name, y, w, h) in enumerate([('new', 35, 87, 26), ('library', 99, 59, 26), ('settings', 166, 72, 22)]):
            pixels = []
            for py in range(y, y+h):
                for px in range(20, 20+w):
                    if self.sources['home'].getpixel((px, py)) != self.paper.getpixel((px, py)):
                        assert self.sources['home'].getpixel((px, py)) == self.colors['ink']
                        pixels.append((px, py))
            on = set(pixels)
            self.labels.append((name, y, w, [(x, yy, not all(p in on for p in ((x-1, yy), (x+1, yy), (x, yy-1), (x, yy+1)))) for x, yy in pixels]))
        self.panels = {}
        for name in ['library', 'settings']:
            pixels = []
            image = self.sources[name]
            for y in range(240):
                for x in range(144, 320):
                    color = image.getpixel((x, y))
                    if color == self.paper.getpixel((x, y)):
                        continue
                    assert color in [self.colors['ink'], self.colors['ghost']], (theme, name, x, y, color)
                    row = min(5, max(0, (y-28)//36)) if name == 'library' else min(2, max(0, (y-28)//44))
                    meta = color == self.colors['ghost'] and x >= 168 if name == 'library' else color == self.colors['ghost']
                    rank = .38+.16*math.sin(x*.135+row*.83)+.13*math.cos(y*.22-x*.035)+.07*math.sin(x*.041+y*.11)
                    pixels.append((x, y, color, row, meta, rank))
            self.panels[name] = pixels
        # Endpoints use exact current masks, spacing, diamonds and 3 settings.
        for name, index, uy in [('library', 1, 127), ('settings', 2, 191)]:
            im = self.paper.copy()
            for i, (_, _, _, pixels) in enumerate(self.labels):
                for x, y, _ in pixels:
                    im.putpixel((x, y), self.colors['accent'] if i == index else self.colors['ink'])
            ImageDraw.Draw(im).line((20, uy, 20+self.labels[index][2]-1, uy), fill=self.colors['accent'])
            for x, y, color, *_ in self.panels[name]:
                im.putpixel((x, y), color)
            assert im.tobytes() == self.sources[name].tobytes(), ('native endpoint mismatch', theme, name)
        self.allowed = Image.new('L', (320, 240))
        a = ImageDraw.Draw(self.allowed)
        for _, y, w, pixels in self.labels:
            for x, yy, _ in pixels:
                a.point((x, yy), fill=255)
        for i, y in enumerate([63, 127, 191]):
            a.line((20, y, 20+self.labels[i][2]-1, y), fill=255)
        for pixels in self.panels.values():
            for x, y, *_ in pixels:
                a.point((x, y), fill=255)

    def frame(self, t, validate=True):
        im = self.paper.copy()
        px = im.load()
        paper = self.paper.load()
        old, new, age = state(t)
        phase = math.tau*t/8000
        focus = ease(age/180)
        for i, (name, top, w, pixels) in enumerate(self.labels):
            selected = (1-focus if name == old else 0)+(focus if name == new else 0)
            press = math.sin(math.pi*min(1., age/520)) if name == new and 0 <= age < 520 else 0
            for x, y, edge in pixels:
                # Broad coherent moving density, not sparkling/random pixels.
                pool = (.5+.5*math.sin((x-20)*.056-(y-top)*.065-phase+i*1.27))**2
                second = .5+.5*math.sin((x-20)*.095+(y-top)*.11+phase*.5+i*2.1)
                density = (1-.24*pool-.04*second) if edge else (1-.10*pool)
                tint = .06+.20*pool
                tint = tint*(1-selected)+selected*(.57+.43*second)
                tint = max(0., tint-.48*press*pool)
                color = lerp(self.colors['ink'], self.colors['accent'], tint)
                px[x, y] = lerp(paper[x, y], color, density)
            if selected > 0:
                uy = [63, 127, 191][i]
                for x in range(20, 20+w):
                    px[x, uy] = lerp(paper[x, uy], self.colors['accent'], selected)
        for name, outgoing in [(old, True), (new, False)]:
            if not name:
                continue
            if outgoing and age >= 260:
                continue
            delay = 110 if old and not outgoing else 0
            for x, y, color, row, meta, rank in self.panels[name]:
                if outgoing:
                    a = 1-ease((age/260*1.5-(1-rank)*.55)/.62)
                else:
                    local = age-delay-row*24-(55 if meta else 0)
                    a = ease((local/420*1.35-rank)/.39)
                if a <= 0:
                    continue
                # Metadata settles solid; only headings carry a faint ink breath.
                wave = .5+.5*math.sin(x*.052-y*.035-phase+row*.7)
                density = 1 if meta else 1-.07*wave*wave
                px[x, y] = lerp(px[x, y], color, a*density)
        if validate:
            changed = ImageChops.difference(im, self.paper).convert('L').point(lambda a: 255 if a else 0)
            assert not ImageChops.subtract(changed, self.allowed).getbbox(), 'motion escaped native glyph/underline pixels'
        return im


def render(theme):
    study = Study(theme)
    native = [study.frame(t) for t in range(0, DURATION, MS)]
    assert study.frame(0).tobytes() == study.frame(DURATION).tobytes(), 'loop seam'
    assert native[0].tobytes() != native[25].tobytes(), 'idle must visibly animate'
    big = [im.resize((640, 480), Image.Resampling.NEAREST) for im in native]
    # One global palette, no temporal adaptive palettes and no dither speckles.
    atlas = Image.new('RGB', (320*8, 240*5))
    for i, im in enumerate(native[::10]):
        atlas.paste(im, ((i%8)*320, (i//8)*240))
    palette = atlas.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    frames = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in big]
    path = OUT/(theme+'-text-motion-2x.gif')
    assert not path.exists(), 'preserve earlier media'
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=MS, loop=0, optimize=False, disposal=1,
                   comment=b'design-only text animation; native glyph masks, authored motion; not a 3ds recording')
    durations = 0
    worst = 0
    with Image.open(path) as gif:
        assert gif.n_frames == len(frames), (gif.n_frames, len(frames))
        for i in range(gif.n_frames):
            gif.seek(i)
            decoded = gif.convert('RGB')
            assert decoded.tobytes() == frames[i].convert('RGB').tobytes(), ('gif decode', i)
            assert decoded.resize((320, 240), Image.Resampling.NEAREST).resize((640, 480), Image.Resampling.NEAREST).tobytes() == decoded.tobytes()
            durations += gif.info.get('duration', 0)
            worst = max(worst, max(v[1] for v in ImageChops.difference(decoded, big[i]).getextrema()))
    assert durations == DURATION
    for name, t in [('idle', 800), ('library-opening', 2240), ('library', 3200), ('settings-opening', 6640), ('settings', 7600)]:
        study.frame(t).resize((640, 480), Image.Resampling.NEAREST).save(OUT/(theme+'-'+name+'.png'))
    board = Image.new('RGB', (320*3+32, 240+16), (20, 20, 22))
    for i, t in enumerate([800, 3200, 7600]):
        board.paste(study.frame(t), (8+i*328, 8))
    board.resize((board.width*2, board.height*2), Image.Resampling.NEAREST).save(OUT/(theme+'-review.png'))
    return {'theme': theme, 'gif': str(path.relative_to(ROOT)), 'sha256': sha(path), 'frames': len(frames),
            'duration_ms': durations, 'worst_palette_channel_error': worst,
            'native_endpoints_match_current_cpp': True, 'motion_outside_glyphs_or_original_underlines': 0}


def main():
    before = json.loads((OUT/'source-before.json').read_text())
    assert all(sha(ROOT/p) == digest for p, digest in before.items()), 'production changed before render'
    results = [render(theme) for theme in COLORS]
    assert all(sha(ROOT/p) == digest for p, digest in before.items()), 'production changed during render'
    manifest = {'at': datetime.now(timezone.utc).isoformat(), 'kind': 'authored visual study, NOT runtime implementation',
                'script_sha256': sha(__file__), 'protected_inputs_unchanged': len(before), 'results': results,
                'sources': {str(p.relative_to(ROOT)): sha(p) for p in (OUT/'source-capture').rglob('*.ppm')},
                'scope': 'lower menu text only; exact current native masks/layout/3 settings; dummy fixture patch names',
                'limits': 'paper deliberately frozen to isolate text; no new top-logo motion; no hardware/FPS/input/audio/deploy proof',
                'events_ms': EVENTS, 'opening_ms': 420, 'row_stagger_ms': 24, 'subtitle_delay_ms': 55, 'closing_ms': 260}
    (OUT/'proposal.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
    (OUT/'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>seance / living ink</title><style>body{margin:24px;background:#141416;color:#c8c5ce;font:15px/1.6 system-ui}main{max-width:1320px;margin:auto}h1{font-size:20px;font-weight:500}section{display:flex;gap:24px;flex-wrap:wrap}figure{margin:0;max-width:640px}img{max-width:100%;height:auto;image-rendering:pixelated}p{max-width:850px;color:#a7a0b0}a{color:#b59ae8}</style><main><h1>seance / living ink — text motion study</h1><p>idle → library → close → settings → library → settings → close. 16 seconds, 25 frames/s. letters and rows stay in place.</p><section><figure><figcaption>graphite</figcaption><img src="graphite-text-motion-2x.gif" alt="text, library and settings animation in graphite"></figure><figure><figcaption>pink</figcaption><img src="pink-text-motion-2x.gif" alt="the same study in pink"></figure></section><p>fonts and layout come from a fresh software capture of the current menu. motion is a prototype; the background is frozen to isolate the letters. not a 3ds recording. the application, sound and projects are unchanged.</p></main></html>''')
    print(json.dumps(results, indent=2))

if __name__ == '__main__':
    main()
