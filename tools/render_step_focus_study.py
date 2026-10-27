#!/usr/bin/env python3
"""Offline focus concepts over existing native captures, NOT application renders."""
from pathlib import Path
import base64
import hashlib
import io
import json
import math
from PIL import Image, ImageDraw, ImageFont, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/step_focus_study'
ROWS = (45, 83, 121, 159)
NAMES = ('01 / elastic frame', '02 / magnetic corners', '03 / ink plate', '04 / runner')
DURATIONS = (110, 90, 100, 75)
SOURCES = {
    'gray': ROOT / 'ui_mockups/page_headings_runtime/after/gray/sequence/01_live_scope.ppm',
    'pink': ROOT / 'ui_mockups/graphite_accent_runtime/ui/pink/sequence/01_live_scope.ppm',
}
PALETTES = {
    'gray': {'spot': (181,154,232), 'cell': (48,48,48), 'ink': (244,244,244)},
    'pink': {'spot': (91,49,124), 'cell': (242,216,226), 'ink': (31,20,31)},
}
# Neighboring steps, reverse, vertical, distant tap, and retargets faster than motion.
EVENTS = ((0,0,4),(480,0,5),(840,0,6),(1200,0,7),(1560,0,6),
          (1920,1,6),(2280,2,6),(2640,3,12),(3100,3,13),(3150,3,14),
          (3200,3,15),(3600,0,0),(4080,0,4))
LENGTH = 4600


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def identity():
    paths = [p for d in ('core','platform') for p in (ROOT/d).rglob('*')
             if p.is_file() and p.suffix in ('.h','.hpp','.c','.cpp')]
    return {str(p.relative_to(ROOT)):sha(p) for p in sorted(paths)}


def png_url(im):
    b = io.BytesIO(); im.save(b, format='PNG')
    return 'data:image/png;base64,' + base64.b64encode(b.getvalue()).decode()


def substrates(theme):
    p = PALETTES[theme]
    original = Image.open(SOURCES[theme]).convert('RGB')
    im = original.copy(); x, y = 8+4*19, ROWS[0]
    # Exact selected b2 cell: undo ONLY old selection colors and its 9px cap.
    for yy in range(y,y+31):
        for xx in range(x,x+18):
            c = im.getpixel((xx,yy))
            if c == p['spot']:
                im.putpixel((xx,yy), p['cell'] if yy < y+18 else p['ink'])
            elif c == p['cell'] and y+4 <= yy < y+14:
                im.putpixel((xx,yy), p['ink'])
    paper = original.getpixel((0,y-2))
    ImageDraw.Draw(im).rectangle((x+4,y-2,x+12,y-2),fill=paper)
    # Foreground glyph mask: retain the original native letters, marks and octave.
    fg = Image.new('L', (320,240))
    for y in ROWS:
        for xx in range(8,311):
            for yy in range(y,y+31):
                if im.getpixel((xx,yy)) == p['ink']:
                    fg.putpixel((xx,yy),255)
    diff = ImageChops.difference(im,original).getbbox()
    assert diff and diff[0]>=84 and diff[2]<=102 and diff[1]>=43 and diff[3]<=76
    return original,im,fg,diff


def target(kind,row,col):
    x,y = 8+19*col, ROWS[row]
    if kind == 1: return [x,y-1,18,32]
    if kind == 3: return [x+1,y+31,16,2]
    return [x,y,18,18]


def clamp(t): return max(0,min(1,t))

def ease(t): return 1-(1-clamp(t))**3


def geometry(kind,start,end,t,amount=.7):
    if t >= 1: return list(end)
    if t <= 0: return list(start)
    e = ease(t)
    out = [a+(b-a)*e for a,b in zip(start,end)]
    dx,dy = end[0]-start[0],end[1]-start[1]
    pulse = math.sin(math.pi*t)*(1-t)*amount
    if kind in (0,3):
        # The trailing edge lags; elongation is capped, even for a distant tap.
        sx = min(abs(dx),16)*pulse*(1.4 if kind==0 else 1.9)
        sy = min(abs(dy),12)*pulse if kind==0 else 0
        out[0] -= sx if dx>0 else 0; out[2] += sx
        out[1] -= sy if dy>0 else 0; out[3] += sy
    elif kind == 1:
        # A bounded landing squeeze, not a long spring oscillation.
        squeeze = 2*pulse
        out[0]+=squeeze/2;out[1]+=squeeze/2
        out[2]-=squeeze;out[3]-=squeeze
    else:
        stretch = min(abs(dx),12)*pulse
        squash = 3*pulse
        out[0]-=stretch/2;out[2]+=stretch
        out[1]+=squash/2;out[3]-=squash
    return out


def states(kind,times,amount=.7,scale=1):
    end = target(kind,0,4); start=end[:]; at=0; j=1; result=[]
    for now in times:
        while j<len(EVENTS) and EVENTS[j][0]<=now:
            when,row,col=EVENTS[j]
            start=geometry(kind,start,end,(when-at)/(DURATIONS[kind]*scale),amount)
            end=target(kind,row,col);at=when;j+=1
        result.append(geometry(kind,start,end,(now-at)/(DURATIONS[kind]*scale),amount))
    return result


def raster(kind,rect):
    im=Image.new('L',(320,240));d=ImageDraw.Draw(im)
    x,y,w,h=[math.floor(v+.5) for v in rect];w=max(3,w);h=max(2,h)
    if kind == 0:
        d.line((x+2,y,x+w-3,y),fill=255)
        d.line((x,y+2,x,y+h-3),fill=255)
        d.line((x+w-1,y+2,x+w-1,y+h-3),fill=255)
        d.line((x+2,y+h-1,x+w-3,y+h-1),fill=255)
        for xx,yy in ((x+1,y+1),(x+w-2,y+1),(x+1,y+h-2),(x+w-2,y+h-2)):
            d.point((xx,yy),fill=255)
    elif kind == 1:
        for xx,yy,sx,sy in ((x,y,1,1),(x+w-1,y,-1,1),(x,y+h-1,1,-1),(x+w-1,y+h-1,-1,-1)):
            d.line((xx,yy,xx+sx*5,yy),fill=255)
            d.line((xx,yy,xx,yy+sy*4),fill=255)
    elif kind == 2:
        d.rectangle((x+2,y,x+w-3,y+h-1),fill=255)
        d.rectangle((x+1,y+1,x+w-2,y+h-2),fill=255)
        d.rectangle((x,y+2,x+w-1,y+h-3),fill=255)
    else:
        d.rectangle((x,y,x+w-1,y+1),fill=255)
        d.rectangle((x,y-2,x+1,y-1),fill=255)
        d.rectangle((x+w-2,y-2,x+w-1,y-1),fill=255)
    return im


def frame(base,fg,theme,kind,rect):
    im=base.copy();mask=raster(kind,rect)
    im.paste(PALETTES[theme]['spot'],(0,0,320,240),mask)
    # Glyphs stay fixed. Opaque moving plate gets inverse text only where covered.
    if kind==2:
        covered=ImageChops.multiply(mask,fg)
        im.paste(PALETTES[theme]['cell'],(0,0,320,240),covered)
    return im


def main():
    before=identity();OUT.mkdir(parents=True,exist_ok=True)
    times=list(range(0,LENGTH,20)) # GIF's exact 20ms ticks, not a hardware FPS claim.
    paths={};data={'durations':DURATIONS,'events':EVENTS,'length':LENGTH,'themes':{},'tests':[]}
    font=ImageFont.truetype('/usr/share/fonts/TTF/IBMPlexMono-Regular.ttf',12)
    bounds={};outside=0
    for kind in range(4):
        for start_cell in ((0,0),(0,15),(3,0),(3,15)):
            for end_cell in ((0,0),(0,15),(3,0),(3,15)):
                a=target(kind,*start_cell);b=target(kind,*end_cell)
                assert geometry(kind,a,b,0)==a and geometry(kind,a,b,1)==b
                for t in (0,.13,.37,.73,1):
                    g=geometry(kind,a,b,t)
                    assert all(math.isfinite(v) for v in g) and g[2]>0 and g[3]>0
                    data['tests'].append([kind,a,b,t,g])
    for theme in SOURCES:
        original,base,fg,diff=substrates(theme)
        original.save(OUT/f'{theme}_current.png');base.save(OUT/f'{theme}_base.png')
        fg.save(OUT/f'{theme}_foreground.png')
        data['themes'][theme]={'base':png_url(base),'foreground':png_url(fg),
                               'palette':PALETTES[theme]}
        paths[theme]={'path':str(SOURCES[theme].relative_to(ROOT)),
                      'sha256':sha(SOURCES[theme]),'removed_selection_bbox':diff}
        variants=[]
        for kind in range(4):
            frames=[];rects=states(kind,times)
            for rect in rects:
                im=frame(base,fg,theme,kind,rect)
                bb=ImageChops.difference(im,base).getbbox()
                assert bb and 0<=bb[0] and bb[2]<=320 and bb[1]>=41 and bb[3]<=192
                # Wave and panel are frozen native pixels for this concept.
                assert im.crop((0,0,320,41)).tobytes()==base.crop((0,0,320,41)).tobytes()
                assert im.crop((0,194,320,240)).tobytes()==base.crop((0,194,320,240)).tobytes()
                frames.append(im)
            variants.append(frames)
            big=[im.resize((640,480),Image.Resampling.NEAREST) for im in frames]
            big[0].save(OUT/f'{theme}_{kind+1:02d}.gif',save_all=True,
                         append_images=big[1:],duration=20,loop=0,optimize=True,disposal=1)
            frames[0].resize((640,480),Image.Resampling.NEAREST).save(OUT/f'{theme}_{kind+1:02d}_still.png')
        comparisons=[]
        for n in range(len(times)):
            sheet=Image.new('RGB',(664,550),(20,20,23));d=ImageDraw.Draw(sheet)
            for kind in range(4):
                x=8+(kind%2)*328;y=8+(kind//2)*271
                d.text((x+3,y+2),NAMES[kind]+' / '+str(DURATIONS[kind])+'ms',font=font,fill=(205,192,224))
                sheet.paste(variants[kind][n],(x,y+24))
            comparisons.append(sheet)
        comparisons[0].save(OUT/f'{theme}_comparison.gif',save_all=True,
                             append_images=comparisons[1:],duration=20,loop=0,optimize=True,disposal=1)
        comparisons[0].save(OUT/f'{theme}_comparison.png')
    (OUT/'study_data.js').write_text('window.studyData = '+json.dumps(data,separators=(',',':'))+';\n')
    assert before==identity(), 'runtime changed during offline study'
    report={'status':'offline concepts, not runtime or hardware proof',
            'runtime_unchanged':before,'sources':paths,'durations_ms':DURATIONS,
            'timeline_ms':LENGTH,'gif_tick_ms':20,'frames_per_variant':len(times),
            'concept_frames':len(times)*8,'geometry_test_vectors':len(data['tests']),
            'protected_pixels':'wave y0..40 and panel y194..239 exact for every concept frame',
            'limitations':['native background frozen; editor readout does not follow conceptual cursor',
                            'no audio, application input behavior, flow-mode or page-change simulation',
                            'no runtime resource/FPS claims; no build or deployment'],
            'generator_sha256':sha(Path(__file__))}
    (OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'created 4 concepts x 2 themes; {len(times)*8} frames; runtime {len(before)} files unchanged')


if __name__=='__main__': main()
