#!/usr/bin/env python3
"""Authored cabinet-v3 motion mockup, NOT a running app or hardware recording.

Only a GIF and review stills are generated. No app/controller, font atlas, project
storage, settings or audio changes. Native 320x240, stationary thresholded glyphs.
Font: Gossip Med Pixel, Deborah Khodanovich 2025, SIL OFL (assets/fonts/).
"""
from datetime import datetime, timezone
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import re

from PIL import Image, ImageChops, ImageDraw, ImageFont, PngImagePlugin
from render_module_geometry_study import FONT
from render_menu_style_study import heading_mask, overlaps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/start_menu_refresh_study/cabinet-smoke-v3'
SIZE = (320, 240)
FRAME_MS, DURATION = 40, 12000
INK, ACCENT, MUTED = (244, 244, 244), (181, 154, 232), (184, 184, 184)
FACE_PATH = ROOT / 'assets/fonts/Gossip-MedPixel.otf'
BASELINES = (54, 118, 182)
PROJECTS = [('patch 03', '12 min ago'), ('patch 01', '2 hours ago'),
            ('patch 06', 'yesterday'), ('patch 02', '3 days ago'), ('patch 04', 'last week')]
# Only existing settings from ui_start_menu.cpp; no invented master-volume DSP.
SETTINGS = [('menu sound', 'on'), ('logo wave', 'on'), ('startup', 'menu'),
            ('palette editor', 'gray')]
EVENTS = [(800, 'library'), (3400, 'settings'), (8400, 'library'), (10800, None)]
MODAL_OPEN, MODAL_CLOSE = 5600, 7400


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def ease(p):
    p = max(0., min(1., p))
    return p*p*(3-2*p)


def mix(a, b, p):
    return tuple(math.floor(x+(y-x)*p+.5) for x, y in zip(a, b))


@lru_cache(None)
def glyph(text, size):
    if not size:
        m = Image.new('L', (len(text)*6, 8))
        d = ImageDraw.Draw(m)
        for i, ch in enumerate(text):
            for y, bits in enumerate(FONT[ord(ch)-32]):
                for x in range(6):
                    if bits & (1 << (5-x)):
                        d.point((i*6+x, y), fill=255)
        return m, 0
    face = ImageFont.truetype(str(FACE_PATH), size)
    m = Image.new('L', (640, 80))
    ImageDraw.Draw(m).text((4, 40), text, font=face, fill=255, anchor='ls')
    m = m.point(lambda v: 255 if v >= 128 else 0)
    box = m.getbbox()
    if size == 28:
        assert m.crop(box).tobytes() == heading_mask(text).tobytes()
    return m.crop(box), box[1]-40


class Layer:
    def __init__(self):
        self.im = Image.new('RGBA', SIZE)
        self.labels, self.hits = [], []

    def word(self, x, y, text, size=22, color=INK):
        mask, dy = glyph(text, size)
        y += dy
        box = (x, y, x+mask.width, y+mask.height)
        assert 0 <= x < box[2] <= 320 and 0 <= y < box[3] <= 240, (text, box)
        assert all(not overlaps(box, old) for old, _ in self.labels), (text, self.labels)
        self.labels.append((box, text))
        self.im.paste((*color, 255), (x, y), mask)
        return box

    def hit(self, name, box):
        assert 0 <= box[0] < box[2] <= 320 and 0 <= box[1] < box[3] <= 240
        assert box[2]-box[0] >= 32 and box[3]-box[1] >= 24
        assert all(not overlaps(box, old) for _, old in self.hits)
        self.hits.append((name, box))


LEFT = Layer()
for i, text in enumerate(('new patch', 'library', 'settings')):
    LEFT.word(20, BASELINES[i], text, 28)
    LEFT.hit(text, (12, 26+i*64, 132, 88+i*64))

LIBRARY = Layer()
for i, (name, age) in enumerate(PROJECTS):
    baseline = 49+i*36
    LIBRARY.word(158, baseline, name)
    LIBRARY.word(158, baseline+9, age, 0, MUTED)
    LIBRARY.hit(name, (150, 30+i*36, 308, 66+i*36))

PREFERENCES = Layer()
for i, (name, value) in enumerate(SETTINGS):
    baseline = 49+i*44
    PREFERENCES.word(158, baseline, name)
    PREFERENCES.word(158, baseline+9, value, 0, MUTED)
    PREFERENCES.hit(name, (150, 26+i*44, 308, 70+i*44))

for panel in (LIBRARY, PREFERENCES):
    assert all(not overlaps(a, b) for _, a in LEFT.hits for _, b in panel.hits)
    for i, (_, hit) in enumerate(panel.hits):
        for box, text in panel.labels[2*i:2*i+2]:
            assert hit[0] <= box[0] < box[2] <= hit[2] and hit[1] <= box[1] < box[3] <= hit[3], (text, box, hit)

# Use the actual 24 swatch RGB constants, not an invented color collection.
SWATCHES = [tuple(map(int, match)) for match in re.findall(
    r'rgb\((\d+),(\d+),(\d+)\)',
    (ROOT/'platform/3ds/theme_config.h').read_text().split('swatches[kSwatchCount] = {')[1])]
assert len(SWATCHES) == 24


def modal_layer():
    p = Layer()
    d = ImageDraw.Draw(p.im)
    d.rectangle((12, 12, 307, 227), fill=(34, 33, 37, 255), outline=(67, 63, 73, 255))
    p.word(26, 43, 'palette', 28)
    p.word(232, 31, 'saved gray', 0, MUTED)
    presets = [('pink', ((226,190,206), (31,20,31), (91,49,124))),
               ('gray', ((36,36,36), INK, ACCENT)),
               ('old grimoire', ((232,220,192), (81,57,40), (127,29,44))),
               ('black mass', ((39,35,31), (223,205,164), (236,105,85))),
               ('custom', ((232,220,192), (81,57,40), (127,29,44)))]
    for i, (name, paints) in enumerate(presets):
        x, y = 26+(i%2)*142, 61+(i//2)*21
        for j, color in enumerate(paints):
            d.rectangle((x+7*j, y-1, x+7*j+5, y+8), fill=(*color,255))
        p.word(x+28, y, name, 0, ACCENT if name == 'gray' else INK)
        if name == 'gray':
            d.line((x+28, y+11, x+50, y+11), fill=(*ACCENT,255))
    for i, (name, color) in enumerate(zip(('paper', 'ink', 'accent'), ((36,36,36), INK, ACCENT))):
        x = 26+i*92
        d.rectangle((x, 126, x+16, 139), fill=(*color,255), outline=(112,112,112,255))
        p.word(x+24, 130, name, 0, ACCENT if i == 2 else MUTED)
    for i, color in enumerate(SWATCHES):
        x, y = 26+(i%8)*34, 151+(i//8)*17
        d.rectangle((x,y,x+28,y+11), fill=(*color,255))
        if color == ACCENT:
            d.polygon(((x+14,y+2),(x+17,y+5),(x+14,y+8),(x+11,y+5)), fill=(31,20,31,255))
    p.word(26, 209, 'cancel', 0, MUTED)
    p.word(270, 209, 'save', 0, ACCENT)
    return p


MODAL = modal_layer()


def lattice(x, y):
    n = (x*374761393+y*668265263+419) & 0xffffffff
    n = ((n ^ (n >> 13))*1274126177) & 0xffffffff
    return ((n ^ (n >> 16)) & 65535)/32767.5-1


def noise(x, y):
    ix, iy = math.floor(x), math.floor(y)
    u, v = ease(x-ix), ease(y-iy)
    a = lattice(ix,iy)*(1-u)+lattice(ix+1,iy)*u
    b = lattice(ix,iy+1)*(1-u)+lattice(ix+1,iy+1)*u
    return a*(1-v)+b*v


@lru_cache(128)
def paper(bucket):
    # Same deterministic noise/warp recipe as patch_paper.h, but a small circular
    # phase excursion makes this short design GIF seamless. NOT a C++/GPU capture.
    # Startup graphite uses the actual 40% field strength, rather than host tiles.
    angle = math.tau*(bucket*100 % DURATION)/DURATION
    s, c = .25*math.sin(angle), 1+.25*(math.cos(angle)-1)
    grid = Image.new('RGB', (64,64))
    colors = []
    for y in range(64):
        py = (y+.5)*496/64-2
        for x in range(64):
            px = (x+.5)*400/64-2
            qx = px+14*math.sin(px/71-.24*s)+18*s+31*math.sin(py/83+.32*s)
            qy = py+27*math.sin(px/91+.30*s)+18*(c-1)
            n = (.65*noise(qx/115+2,qy/115+7)
                 + .25*noise(qx/53+9+.12*s,qy/53+3+.10*(c-1))
                 + .10*noise(qx/24+5-.10*s,qy/24+11))
            light = max(0,min(255,math.floor((.5+1.45*n)*255+.5)))/255
            colors.append(mix((36,36,36), mix((17,18,22),(66,61,75),light), .4))
    grid.putdata(colors)
    return grid.resize((400,496), Image.Resampling.BILINEAR).crop((40,256,360,496))


@lru_cache(64)
def smoke_mask(age, duration=320):
    if age <= 0:
        return Image.new('L', SIZE), Image.new('L', SIZE)
    if age >= duration:
        return Image.new('L', SIZE, 255), Image.new('L', SIZE)
    p = age/duration
    opacity, vapor = Image.new('L',(40,60)), Image.new('L',(40,60))
    alphas, shades = [], []
    for y in range(60):
        yy = (y+.5)*4
        # A very short 0..32ms spatial row delay, not a serial list cascade.
        row_delay = min(32,max(0,(yy-26)/5))
        local = max(0,min(1,(age-row_delay)/(duration-row_delay)))
        for x in range(40):
            xx = 144+(x+.5)*4
            qx = xx+12*math.sin(yy/47+p*1.8)-10*p
            qy = yy+8*math.sin(xx/39-p*1.4)+7*p
            n = .72*noise(qx/43+5,qy/53+9)+.28*noise(qx/19+2,qy/25+7)
            alpha = ease(local+.62*math.sin(math.pi*local)*n)
            edge = ease((xx-144)/18)*ease((304-xx)/18)*ease((yy-12)/22)*ease((226-yy)/22)
            haze = math.sin(math.pi*p)*edge*(.35+.65*(1-abs(n)))*.14
            alphas.append(round(alpha*255))
            shades.append(round(haze*255))
    opacity.putdata(alphas)
    vapor.putdata(shades)
    mask, mist = Image.new('L',SIZE), Image.new('L',SIZE)
    mask.paste(opacity.resize((160,240),Image.Resampling.BILINEAR),(144,0))
    mist.paste(vapor.resize((160,240),Image.Resampling.BILINEAR),(144,0))
    return mask, mist


def composite(im, layer, mask=None, opacity=1.):
    alpha = layer.getchannel('A')
    if mask is not None:
        alpha = ImageChops.multiply(alpha, mask)
    if opacity < 1:
        alpha = alpha.point(lambda a: round(a*max(0,opacity)))
    im.paste(layer.convert('RGB'), (0,0), alpha)


def active(t):
    previous, selected, at = None, None, -1000
    for when, name in EVENTS:
        if t < when:
            break
        previous, selected, at = selected, name, when
    return previous, selected, at


def draw(t):
    im = paper(int(t//100)).copy()
    old, selected, at = active(t)
    age = t-at
    layers = {'library': LIBRARY, 'settings': PREFERENCES}
    if old and age < 160:
        composite(im, layers[old].im, opacity=1-ease(age/160))
    if selected:
        # 80ms overlap only on page changes; first disclosure responds at once.
        delay = 80 if old else 0
        mask, haze = smoke_mask(max(0,age-delay))
        im.paste((88,78,101),(0,0,320,240),haze)
        composite(im,layers[selected].im,mask)
    # Fixed 28ppem left-column glyphs; focus tint and a short ink response only.
    for i, name in enumerate(('new patch','library','settings')):
        f = ease(age/120) if name == selected else 1-ease(age/120) if name == old else 0
        mask, dy = glyph(name,28)
        color = mix(INK,ACCENT,f)
        im.paste(color,(20,BASELINES[i]+dy),mask)
        if name == selected and 0 < age < 320:
            stripe = Image.new('RGB',mask.size,color)
            d = ImageDraw.Draw(stripe)
            center = -16+(mask.width+32)*ease(age/320)
            for x in range(mask.width):
                strength = max(0,1-abs(x-center)/15)*math.sin(math.pi*age/320)*.6
                d.line((x,0,x,mask.height-1),fill=mix(color,INK,strength))
            im.paste(stripe,(20,BASELINES[i]+dy),mask)
        if f > 0:
            # Short stationary underline; no large divider or footer chrome.
            color = mix(tuple(im.getpixel((20,BASELINES[i]+9))),ACCENT,f)
            ImageDraw.Draw(im).line((20,BASELINES[i]+9,20+mask.width-1,BASELINES[i]+9),fill=color)
    # Scripted palette-button focus, not a working settings edit.
    if selected == 'settings' and MODAL_OPEN-240 <= t < MODAL_CLOSE+160:
        m, dy = glyph('palette editor',22)
        f = ease((t-(MODAL_OPEN-240))/120)
        im.paste(mix(INK,ACCENT,f),(158,181+dy),m)
    modal = ease((t-MODAL_OPEN)/200)*(1-ease((t-MODAL_CLOSE)/160))
    if modal > 0:
        im = Image.blend(im,Image.new('RGB',SIZE,(12,12,14)),.62*modal)
        composite(im,MODAL.im,opacity=modal)
    return im


def save_png(im, name, meta):
    path = OUT/name
    big = im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
    info = PngImagePlugin.PngInfo()
    info.add_text('provenance',json.dumps(meta,sort_keys=True))
    big.save(path,pnginfo=info)
    with Image.open(path) as decoded:
        decoded.load()
        assert decoded.tobytes() == big.tobytes()
        assert decoded.resize(im.size,Image.Resampling.NEAREST).tobytes() == im.tobytes()
    return path


def encode_gif(frames):
    # One global nondithered palette. Pin settled text and real swatches; remap
    # exact RGBs ourselves to avoid Pillow's coarse nearest-palette cache.
    samples = frames[::12]+[draw(t) for t in (880,960,1040,3520,3600,5640,5680,5760,7440,7520)]
    atlas = Image.new('RGB',(320,240*len(samples)))
    for i,im in enumerate(samples):
        atlas.paste(im,(0,i*240))
    p = atlas.quantize(colors=216,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
    raw = p.getpalette()
    colors = list(dict.fromkeys([tuple(raw[i:i+3]) for i in range(0,216*3,3)]
                 + [INK,ACCENT,MUTED,(34,33,37),(67,63,73),(112,112,112),(31,20,31)] + SWATCHES))
    assert len(colors) <= 256
    distinct = set()
    for im in frames:
        distinct.update(im.get_flattened_data())
    lookup = {c:i for i,c in enumerate(colors)}
    max_error = 0
    for c in distinct:
        if c not in lookup:
            idx = min(range(len(colors)),key=lambda i:sum((a-b)**2 for a,b in zip(c,colors[i])))
            lookup[c] = idx
            max_error = max(max_error,max(abs(a-b) for a,b in zip(c,colors[idx])))
    palette = [v for c in colors for v in c]+[0]*(768-len(colors)*3)
    indexed = []
    for im in frames:
        frame = Image.frombytes('P',SIZE,bytes(lookup[c] for c in im.get_flattened_data()))
        frame.putpalette(palette)
        indexed.append(frame.resize((640,480),Image.Resampling.NEAREST))
    path = OUT/'cabinet-smoke-v3-2x.gif'
    indexed[0].save(path,save_all=True,append_images=indexed[1:],duration=FRAME_MS,
                    loop=0,optimize=False,disposal=1)
    with Image.open(path) as decoded:
        assert decoded.size == (640,480) and decoded.info['loop'] == 0
        tick = 0
        for n in range(decoded.n_frames):
            decoded.seek(n)
            duration = decoded.info['duration']
            assert duration % FRAME_MS == 0
            for _ in range(duration//FRAME_MS):
                assert decoded.convert('RGB').tobytes() == indexed[tick].convert('RGB').tobytes()
                tick += 1
        assert tick == len(frames)
        stored_frames = decoded.n_frames
    return path,dict(global_palette_colors=len(colors),maximum_rgb_quantization_error=max_error,
                     decoded_ticks=tick,stored_frames=stored_frames,duration_ms=tick*FRAME_MS,
                     fixed_palette=True,dither=False,decoded_frames_exact_to_indexed=True)


def main():
    assert not OUT.exists(), 'preserve prior review; choose a new output directory'
    protected = json.loads((ROOT/'ui_mockups/start_menu_cabinet_runtime/baseline.json').read_text())
    assert all(sha(ROOT/n) == h for n,h in protected.items())
    previous = {str(p.relative_to(ROOT)):sha(p) for p in (ROOT/'ui_mockups/start_menu_refresh_study').rglob('*') if p.is_file()}
    sources = ['tools/render_start_menu_smoke_study.py','tools/render_module_geometry_study.py',
               'tools/render_menu_style_study.py','assets/fonts/Gossip-MedPixel.otf','core/ui/font.cpp',
               'platform/3ds/patch_paper.h','platform/3ds/menu_style.cpp','platform/3ds/paper_presence.h',
               'platform/3ds/theme_config.h','platform/3ds/ui_start_menu.cpp']
    before = {n:sha(ROOT/n) for n in sources}
    meta = dict(status='authored motion mockup; not an interactive prototype or app capture',
                authored_at=datetime.now(timezone.utc).isoformat(),sources=before,
                native_size=list(SIZE),scale='nearest 2x',duration_ms=DURATION,frame_ms=FRAME_MS,
                layout='fixed left 28ppem; right 22ppem; library five rows at 36px; no divider/header/footer',
                timing='smoke reveal 320ms, old panel exit160ms / incoming overlap80ms; modal200ms/160ms',
                paper='Python scalar model of existing noise recipe; 40% startup strength; small closed phase excursion for GIF loop; NOT exact GPU output or runtime clock',
                content='five illustrative projects with last-open ages; only existing menu settings; no storage read/write',
                palette_modal='illustrative window with actual five presets and 24 swatches; controls not implemented',
                limitations=['no scrolling/long-name/empty-state interaction in GIF','no arbitrary-name font atlas in app',
                             'no hardware, input, audio, performance or controller testing','no production/build/deploy changes'])
    frames = [draw(t) for t in range(0,DURATION,FRAME_MS)]
    assert draw(DURATION).tobytes() == frames[0].tobytes(), 'periodic home endpoint'
    # Before disclosure, the entire right area is just the uninterrupted paper.
    assert frames[0].crop((144,0,320,240)).tobytes() == paper(0).crop((144,0,320,240)).tobytes()
    # Stable fully-opaque glyphs after every transition; no translations or blur.
    for t,panel in ((1400,LIBRARY),(4000,PREFERENCES),(9200,LIBRARY)):
        frame = draw(t)
        for box,_ in panel.labels:
            for y in range(box[1],box[3]):
                for x in range(box[0],box[2]):
                    pixel = panel.im.getpixel((x,y))
                    if pixel[3] == 255:
                        assert frame.getpixel((x,y)) == pixel[:3]
    OUT.mkdir(parents=True)
    paths = []
    review_times = (400,1400,960,4000,6000,7600)
    sheet = Image.new('RGB',(640,720))
    for i,t in enumerate(review_times):
        sheet.paste(draw(t),((i%2)*320,(i//2)*240))
    paths.append(save_png(sheet,'cabinet-smoke-v3-review-2x.png',dict(meta,review_times_ms=review_times)))
    for name,t in (('home',400),('library',1400),('settings',4000),('palette',6000)):
        paths.append(save_png(draw(t),f'cabinet-smoke-v3-{name}-2x.png',meta))
    gif,validation = encode_gif(frames)
    paths.insert(0,gif)
    assert all(sha(ROOT/n) == h for n,h in protected.items())
    assert all(sha(ROOT/n) == h for n,h in previous.items())
    assert before == {n:sha(ROOT/n) for n in sources}
    report = dict(meta,validation=dict(validation,protected_unchanged=len(protected),
                  previous_files_unchanged=len(previous),bounds_nonoverlap=True,
                  no_right_content_before_activation=True,stable_settled_glyphs=True,
                  exact_periodic_home_endpoint=True),
                  events=EVENTS,modal_interval_ms=[MODAL_OPEN,MODAL_CLOSE],
                  proposed_targets=dict(home=LEFT.hits,library=LIBRARY.hits,settings=PREFERENCES.hits),
                  outputs=[dict(path=str(p.relative_to(ROOT)),bytes=p.stat().st_size,sha256=sha(p)) for p in paths])
    (OUT/'proposal.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
    for item in report['outputs']:
        print(item['path'],item['bytes'],item['sha256'])
    print('pass:',json.dumps(report['validation'],sort_keys=True))


if __name__ == '__main__':
    main()
