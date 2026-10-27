#!/usr/bin/env python3
"""Selected cabinet concept v2: shared baselines, stationary interactive words.

Design-only browser prototype + lossless GIF demonstration; no production edit,
new application compile/capture or console deployment. Reuses the explicitly
hash-verified raw C++ capture from the four-option study, not old proposal pixels.
Existing font license/attribution: assets/fonts/{README.md,OFL.txt}.
"""
from datetime import datetime, timezone
from io import BytesIO
import base64
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, PngImagePlugin
from render_module_geometry_study import Canvas, FONT
from render_menu_style_study import FACE, heading_mask, overlaps
from render_start_menu_refresh_study import raw, COLORS, OUTSIDE, sha

ROOT=Path(__file__).resolve().parents[1]
PRIOR=ROOT/'ui_mockups/start_menu_refresh_study'
OUT=PRIOR/'cabinet-v2'
JS=ROOT/'tools/start_menu_cabinet_preview.js'
BASELINES=(54,118,182)


def ease(v):
    v=max(0,min(1,v)); return v*v*(3-2*v)


def mix(a,b,t):
    return tuple(math.floor(x+(y-x)*t+.5) for x,y in zip(a,b))


def glyph(text,gothic=True):
    if gothic:
        # A genuine baseline, not top-aligning differently cropped ink boxes.
        scratch=Image.new('L',(640,80))
        ImageDraw.Draw(scratch).text((4,40),text,font=FACE,fill=255,anchor='ls')
        scratch=scratch.point(lambda v:255 if v>=128 else 0)
        box=scratch.getbbox(); mask=scratch.crop(box)
        assert mask.tobytes()==heading_mask(text).tobytes()
        return mask,box[1]-40
    mask=Image.new('L',(len(text)*6,8)); d=ImageDraw.Draw(mask)
    for n,ch in enumerate(text):
        for y,bits in enumerate(FONT[ord(ch)-32]):
            for x in range(6):
                if bits & (1 << (5-x)): d.point((n*6+x,y),fill=255)
    return mask,-7


def make_label(id,title,column,row,subtitle,action,gothic=True):
    x=20 if column==0 else 158
    baseline=BASELINES[row] if row<3 else 235
    mask,offset=glyph(title,gothic)
    assert mask.width<= (112 if column==0 else 144)
    columns=[]
    for px in range(mask.width):
        runs=[]; start=None
        for py in range(mask.height+1):
            on=py<mask.height and mask.getpixel((px,py))!=0
            if on and start is None: start=py
            if not on and start is not None:
                runs.append([start,py-start]); start=None
        columns.append(runs)
    y=baseline+offset
    hit=[12 if column==0 else 151,26+64*row,132 if column==0 else 308,88+64*row]
    if row==3: hit=[151,216,308,240]
    return dict(id=id,title=title,x=x,y=y,w=mask.width,h=mask.height,
                baseline=baseline,underline=baseline+(10 if row<3 else 3),
                column=column,row=row,subtitle=subtitle,action=action,hit=hit,columns=columns)


LABELS=[make_label(*args) for args in (
    ('new','new patch',0,0,'start fresh','new project'),
    ('library','library',0,1,'3 saved','library'),
    ('settings','settings',0,2,'look / sound','settings'),
    ('recent-0','patch 03',1,0,'12 min','open patch 03'),
    ('recent-1','patch 01',1,1,'2 hours','open patch 01'),
    ('recent-2','patch 06',1,2,'yesterday','open patch 06'),
    ('all','all projects',1,3,'','all projects',False),
)]


def background(theme):
    p=Canvas(320); p.im=raw(theme,'paper_bottom').copy(); p.d=ImageDraw.Draw(p.im)
    color=COLORS[theme]
    p.text(20,12,'start',color['muted'])
    p.text(158,12,'recent',color['muted'])
    p.text(300,12,'03',color['muted'],right=True)
    p.d.line((140,26,140,210),fill=color['rule'])
    for l in LABELS:
        if l['subtitle']: p.text(l['x'],l['baseline']+14,l['subtitle'],color['muted'])
    p.text(20,228,'a open / b exit',color['muted'])
    for l in LABELS:
        box=(l['x'],l['y'],l['x']+l['w'],l['y']+l['h'])
        assert all(not overlaps(box,old) for old,_ in p.labels),(l['title'],p.labels)
        p.labels.append((box,l['title']))
        b=l['hit']; assert 0<=b[0]<b[2]<=320 and 0<=b[1]<b[3]<=240
        assert b[2]-b[0]>=40 and b[3]-b[1]>=24
        assert b[0]<=box[0] and b[1]<=box[1] and box[2]<=b[2] and box[3]<=b[3]
        assert l['underline']<240 and l['x']+l['w']+4<=320
    for i,a in enumerate(LABELS):
        for b in LABELS[i+1:]: assert not overlaps(a['hit'],b['hit'])
    im=Image.new('RGB',(400,496),OUTSIDE)
    im.paste(raw(theme,'closed_top'),(0,0)); im.paste(p.im,(40,256))
    return im


BACKGROUNDS={theme:background(theme) for theme in COLORS}


def draw(theme='graphite',weights=None,pulse=None,t=0):
    weights=weights or {'recent-0':1}
    im=BACKGROUNDS[theme].copy(); d=ImageDraw.Draw(im); colors=COLORS[theme]
    for l in LABELS:
        f=weights.get(l['id'],0); active=pulse and pulse['id']==l['id']
        fade=(1-ease((t-pulse['release'])/220) if t>=pulse['release'] else 1) if active else 0
        age=max(0,t-pulse['started']) if active else 0
        origin=pulse['x'] if active else l['x']
        reach=max(origin-l['x'],l['x']+l['w']-origin)+24
        front=reach*ease(age/300)
        for x,col in enumerate(l['columns']):
            band=max(0,1-abs(abs(l['x']+x-origin)-front)/21)*fade if active else 0
            color=mix(colors['ink'],colors['accent'],math.floor(f*(1-band)*31+.5)/31)
            for y,h in col:
                d.line((40+l['x']+x,256+l['y']+y,40+l['x']+x,256+l['y']+y+h-1),fill=color)
        if f>.01:
            color=mix(colors['rule'],colors['accent'],math.floor(f*31+.5)/31)
            width=max(1,math.floor((l['w']+4)*f+.5))
            d.line((40+l['x'],256+l['underline'],40+l['x']+width-1,256+l['underline']),fill=color)
    assert im.crop((0,0,400,240)).tobytes()==raw(theme,'closed_top').tobytes()
    return im


def demo(t):
    weights={'recent-0':1}; previous='recent-0'; pulse=None
    for at,id in ((800,'new'),(2100,'library'),(3400,'recent-1'),(4700,'settings'),(6000,'recent-0')):
        if t<at: break
        f=ease((t-at)/120); weights={previous:1-f,id:f}; previous=id
        if t>=at+200:
            l=next(l for l in LABELS if l['id']==id)
            pulse=dict(id=id,x=l['x']+l['w']//3,started=at+200,release=at+500)
    return draw(weights=weights,pulse=pulse,t=t)


def data_url(im):
    stream=BytesIO(); im.save(stream,format='PNG')
    return 'data:image/png;base64,'+base64.b64encode(stream.getvalue()).decode()


def save_png(im,name,meta):
    path=OUT/name; assert not path.exists()
    big=im.resize((im.width*2,im.height*2),Image.Resampling.NEAREST)
    info=PngImagePlugin.PngInfo(); info.add_text('provenance',json.dumps(meta,sort_keys=True))
    big.save(path,pnginfo=info)
    with Image.open(path) as decoded:
        decoded.load(); assert decoded.tobytes()==big.tobytes()
        assert decoded.resize(im.size,Image.Resampling.NEAREST).tobytes()==im.tobytes()
    return path


HTML='''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>seance / cabinet v2 — interactive prototype</title>
<style>
:root{color-scheme:dark;font:14px/1.6 ui-monospace,monospace;background:#141416;color:#b8b8b8}
body{margin:0;padding:24px}main{max-width:840px;margin:auto}h1{font:inherit;color:#f4f4f4;margin:0 0 8px}
p{margin:6px 0 16px}a{color:#b59ae8}#stage{position:relative;width:min(800px,100%);margin:18px auto}
canvas{display:block;width:100%;height:auto;image-rendering:pixelated}
#targets{position:absolute;inset:0}#targets button{position:absolute;background:none;border:0;padding:0;margin:0;
color:transparent;cursor:pointer;outline:none;touch-action:none;-webkit-tap-highlight-color:transparent}
.toolbar{display:flex;gap:10px;flex-wrap:wrap}.toolbar button{font:inherit;background:#242424;border:1px solid #555;
color:#b8b8b8;padding:4px 12px;cursor:pointer}.toolbar button[aria-pressed=true]{color:#b59ae8;border-color:#b59ae8}
.toolbar button:focus-visible{outline:2px solid #b59ae8}#status{min-height:3.2em;color:#b59ae8}
small{display:block;max-width:800px}.hint{color:#888}
</style><main>
<h1>seance / 03 — cabinet, aligned grid</h1>
<p>hover or press the words. selection and ink waves leave the letters stationary.</p>
<div class="toolbar"><button data-theme="graphite" aria-pressed="true">graphite</button><button data-theme="pink" aria-pressed="false">pink</button></div>
<div id="stage"><canvas id="screen" width="400" height="496" aria-label="seance dual-screen visual prototype"></canvas><div id="targets" aria-label="prototype actions"></div></div>
<p id="status" role="status" aria-live="polite">interactive prototype — projects are not created or loaded.</p>
<small>mouse / touch: release inside a row. leaving the row cancels the press.<br>tab / arrows: select · enter / space / a: show action · b / escape: prototype exit.</small>
<p class="hint">the upper screen and paper use the original software capture. block shadows are its simplified background, not a new design. this is not a 3ds build.</p>
</main><script>window.CABINET=__DATA__;</script><script>__JS__</script></html>
'''


def main():
    assert not OUT.exists(),'preserve previous version; select a new directory'
    capture=json.loads((PRIOR/'capture.json').read_text())
    for n,h in capture['source_sha256'].items(): assert sha(ROOT/n)==h,n
    for n,h in capture['surface_sha256'].items(): assert sha(PRIOR/n)==h,n
    protected=json.loads((PRIOR/'protected-before.json').read_text())
    assert all(sha(ROOT/n)==h for n,h in protected.items())
    inputs=['tools/render_start_menu_cabinet_study.py','tools/start_menu_cabinet_preview.js',
            'tools/render_start_menu_refresh_study.py','tools/render_menu_style_study.py',
            'tools/render_module_geometry_study.py','assets/fonts/Gossip-MedPixel.otf','core/ui/font.cpp']
    before={n:sha(ROOT/n) for n in inputs}
    old={str(p.relative_to(ROOT)):sha(p) for p in PRIOR.glob('*.png')}
    meta=dict(status='selected composition 03; refined browser design prototype, not runtime',
              authored_at=datetime.now(timezone.utc).isoformat(),source_sha256=before,
              prior_capture_sha256=sha(PRIOR/'capture.json'),new_compilations=0,new_captures=0,
              baselines=list(BASELINES),row_pitch=64,heading_columns=[20,158],divider=140,
              interaction='stationary exact glyph masks; focus120ms; press ink front300ms; release220ms',
              data='illustrative example slots; click reports an action only; never opens/saves real projects',
              limitations='no hardware/timing/3ds input validation; host block paper unchanged')
    # Three pairs have exactly the same baseline, meta offset and row pitch.
    for row,baseline in enumerate(BASELINES):
        pair=[l for l in LABELS if l['row']==row]
        assert len(pair)==2 and all(l['baseline']==baseline for l in pair)
    frames=[demo(i*50) for i in range(144)]
    assert frames[0].tobytes()==frames[-1].tobytes(), 'loop seam'
    native_colors=set().union(*(set(im.get_flattened_data()) for im in frames))
    assert len(native_colors)<=256,len(native_colors)
    # Frozen label masks: only ink colors/short underlines vary, never positions.
    allowed=Image.new('1',(400,496)); ad=ImageDraw.Draw(allowed)
    for l in LABELS:
        ad.rectangle((40+l['x'],256+l['y'],40+l['x']+l['w']-1,256+l['y']+l['h']-1),fill=1)
        ad.line((40+l['x'],256+l['underline'],40+l['x']+l['w']+3,256+l['underline']),fill=1)
    mask=allowed.tobytes()
    from PIL import ImageChops
    for im in frames:
        diff=ImageChops.difference(im,BACKGROUNDS['graphite'])
        diff.paste((0,0,0),mask=allowed.convert('L'))
        assert diff.getbbox() is None,'motion outside fixed word masks/underlines'
    OUT.mkdir(parents=True)
    paths=[save_png(draw(theme),f'cabinet-v2-{theme}-dual-2x.png',meta) for theme in COLORS]
    paths.append(save_png(draw().crop((40,256,360,496)),'cabinet-v2-lower-2x.png',meta))
    palette=Image.new('P',(1,1)); values=sorted(native_colors)
    palette.putpalette([v for c in values for v in c]+[0]*(768-3*len(values)))
    indices={color:i for i,color in enumerate(values)}
    indexed=[]
    for im in frames:
        # Pillow's nearest-palette cache coalesces nearby RGBs. Direct indices
        # preserve every exact native ink shade, even only one channel apart.
        p=Image.new('P',im.size); p.putpalette(palette.getpalette())
        p.putdata([indices[color] for color in im.get_flattened_data()])
        assert p.convert('RGB').tobytes()==im.tobytes()
        indexed.append(p.resize((800,992),Image.Resampling.NEAREST))
    gif=OUT/'cabinet-v2-ink-2x.gif'
    indexed[0].save(gif,save_all=True,append_images=indexed[1:],duration=50,loop=0,disposal=1,optimize=False)
    with Image.open(gif) as decoded:
        tick=0
        for i in range(decoded.n_frames):
            decoded.seek(i); duration=decoded.info['duration']; assert duration%50==0
            for _ in range(duration//50):
                assert decoded.convert('RGB').tobytes()==indexed[tick].convert('RGB').tobytes()
                tick+=1
        assert tick==144
    paths.append(gif)
    data=dict(labels=LABELS,colors=COLORS,backgrounds={k:data_url(v) for k,v in BACKGROUNDS.items()})
    html=HTML.replace('__DATA__',json.dumps(data,ensure_ascii=False,separators=(',',':'))).replace('__JS__',JS.read_text())
    webpage=OUT/'index.html'; webpage.write_text(html); paths.append(webpage)
    assert before=={n:sha(ROOT/n) for n in inputs}
    assert all(sha(ROOT/n)==h for n,h in protected.items())
    assert old=={str(p.relative_to(ROOT)):sha(p) for p in PRIOR.glob('*.png')}
    report=dict(meta,labels=LABELS,validation=dict(native_frames=144,duration_ms=7200,
            exact_gif_palette=len(native_colors),stationary_word_masks=True,common_baselines=True,
            nonoverlapping_targets=7,native_upper_preserved=True,protected_files=len(protected),
            old_comparison_files_preserved=len(old),browser_smoke='pending'),
            outputs=[dict(path=str(p.relative_to(ROOT)),bytes=p.stat().st_size,sha256=sha(p)) for p in paths])
    (OUT/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for p in paths: print(p.relative_to(ROOT),p.stat().st_size,sha(p))
    print(f'pass: common baselines; native bounds; 7 proposed targets; 144 stationary-mask frames; lossless GIF palette {len(native_colors)}, 7200ms decode; previous files preserved')


if __name__=='__main__':
    main()
