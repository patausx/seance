#!/usr/bin/env python3
"""Four new startup-home compositions, not an implementation or a hardware capture.

Current/top/clean-paper surfaces: fresh dump-only C++ capture, 20px host paper
fallback (not the console's GPU-filtered texture). Proposals: authored Pillow
native pixels. Existing 28ppem Seance Menu and native 6x8. Example saved slots;
no new project data, fake waveforms, installed fonts, device or audio changes.
Font attribution: assets/fonts/{README.md,OFL.txt}, Deborah Khodanovich 2025.
"""
from datetime import datetime, timezone
from functools import lru_cache
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, PngImagePlugin
from render_module_geometry_study import Canvas
from render_menu_style_study import heading_mask, overlaps
from generate_start_menu_labels import masks

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/start_menu_refresh_study'
OUTSIDE = (20, 20, 22)
COLORS = {
    'graphite': dict(ink=(244,244,244), accent=(181,154,232), muted=(184,184,184), rule=(112,112,112)),
    'pink': dict(ink=(31,20,31), accent=(91,49,124), muted=(126,100,119), rule=(174,130,151)),
}
RECENTS = [('patch 03', '12 min'), ('patch 01', '2 hours'), ('patch 06', 'yesterday')]
INPUTS = ['tools/render_start_menu_refresh_study.py', 'tools/render_module_geometry_study.py',
          'tools/render_menu_style_study.py', 'tools/generate_start_menu_labels.py',
          'core/ui/font.cpp', 'assets/fonts/Gossip-MedPixel.otf', 'platform/3ds/ui_palette.h']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


@lru_cache(None)
def raw(theme, name):
    with Image.open(OUT / 'capture' / theme / (name + '.ppm')) as im:
        im.load()
        expected = (400 if name.endswith('_top') else 320, 240)
        assert im.size == expected
        return im.convert('RGB')


class Page(Canvas):
    def __init__(self, theme):
        super().__init__(320)
        self.theme = theme
        self.colors = COLORS[theme]
        self.im = raw(theme, 'paper_bottom').copy()
        self.d = ImageDraw.Draw(self.im)
        self.hits = []

    def text_at(self, x, y, value, role='ink', center=False, right=False, limit=None):
        if limit is not None and len(value) > limit:
            value = value[:limit-3] + '...'
        if center:
            x -= len(value)*3
        self.text(x, y, value, self.colors[role], right=right)

    def title(self, x, y, value, role='ink', center=False, max_width=None):
        mask = heading_mask(value)
        if max_width is not None and mask.width > max_width:
            self.text_at(x, y+8, value, role, center=center, limit=max_width//6)
            return
        if center:
            x -= mask.width//2
        box = (x, y, x+mask.width, y+mask.height)
        assert 0 <= x < box[2] <= 320 and 0 <= y < box[3] <= 240, (value, box)
        assert all(not overlaps(box, old) for old, _ in self.labels), (value, box, self.labels)
        self.labels.append((box, value))
        self.im.paste(self.colors[role], (x,y), mask)

    def line(self, points, role='rule', width=1):
        assert all(0 <= x < 320 and 0 <= y < 240 for x,y in points), points
        self.d.line(points, fill=self.colors[role], width=width)

    def diamond(self, x, y, role='accent', rx=3, ry=5):
        self.line([(x,y-ry),(x+rx,y),(x,y+ry),(x-rx,y),(x,y-ry)],role)

    def hit(self, name, box):
        assert 0 <= box[0] < box[2] <= 320 and 0 <= box[1] < box[3] <= 240, box
        assert box[2]-box[0] >= 40 and box[3]-box[1] >= 24, (name, box)
        assert all(not overlaps(box, old) for _,old in self.hits), (name, box, self.hits)
        self.hits.append((name, box))

    def footer(self, action='a open', settings=False):
        self.text_at(20, 228, 'settings' if settings else action, 'muted')
        self.text_at(300, 228, 'b exit', 'muted', right=True)
        if settings:
            self.hit('settings', (12,216,142,240))


def primary(recent, saved):
    if recent:
        return 'continue', recent[0][0], 'load latest'
    if saved:
        return 'library', f'{saved} saved projects', 'library'
    return 'new patch', 'start your first session', 'new patch'


def folio(theme, recent=RECENTS, saved=3):
    """A page: one clear return, two other recents and two secondary routes."""
    p = Page(theme)
    action, name, target = primary(recent, saved)
    p.text_at(24, 15, 'last session' if recent else 'welcome', 'muted')
    if recent:
        p.text_at(296,15,recent[0][1],'muted',right=True,limit=16)
    p.line([(18,36),(18,77)],'accent')
    p.diamond(18,82,rx=2,ry=4)
    p.title(32,37,action,'accent')
    p.text_at(32,70,name,limit=31)
    p.hit(target,(12,30,308,87))
    p.text_at(24,94,'also recent' if len(recent)>1 else 'recent','muted')
    if len(recent)>1:
        for n,(name,age) in enumerate(recent[1:3]):
            y=111+n*27
            p.line([(25,y+2),(25,y+6)])
            p.text_at(36,y,name,limit=24)
            p.text_at(296,y,age,'muted',right=True,limit=12)
            p.hit('load '+name,(20,y-6,304,y+18))
    else:
        p.text_at(24,120,'no other recent projects' if recent else
                  'no recent history' if saved else 'no projects yet','muted')
    p.line([(24,163),(296,163)])
    p.title(24,174,'new patch')
    p.title(184,174,'library')
    p.text_at(24,205,'start fresh','muted')
    p.text_at(184,205,f'{saved} saved','muted')
    p.hit('new patch',(16,167,154,215))
    p.hit('library',(170,167,304,215))
    p.footer(settings=True)
    return p


def bezier(a,b,c,d):
    points=[]
    for n in range(49):
        t=n/48; u=1-t
        points.append((round(a[0]*u**3+3*b[0]*u*u*t+3*c[0]*u*t*t+d[0]*t**3),
                       round(a[1]*u**3+3*b[1]*u*u*t+3*c[1]*u*t*t+d[1]*t**3)))
    return points


def threshold(theme, recent=RECENTS, saved=3):
    """A pointed opening attached to the load target, not a decorative side lane."""
    p=Page(theme)
    action,name,target=primary(recent,saved)
    p.text_at(20,15,'last session' if recent else 'welcome','muted')
    p.text_at(300,15,f'{saved} saved','muted',right=True)
    # The arch defines the primary target; asymmetric double ink, no icon grid.
    p.line(bezier((57,149),(52,76),(92,57),(160,34)))
    p.line(bezier((160,34),(228,57),(268,76),(263,149)))
    p.line(bezier((65,140),(64,83),(97,66),(152,43)), 'accent')
    p.line(bezier((168,43),(223,66),(256,83),(255,140)))
    p.line([(57,149),(78,149)])
    p.line([(242,149),(263,149)])
    p.text_at(160,62,action,'accent',center=True)
    p.title(160,83,name if recent else 'begin' if not saved else 'your projects',
            'ink',center=True,max_width=166)
    p.text_at(160,121,recent[0][1] if recent else 'start fresh' if not saved else 'browse the library',
              'muted',center=True,limit=25)
    p.diamond(160,148,rx=3,ry=5)
    p.hit(target,(48,34,272,157))
    p.title(84,172,'new patch',center=True)
    p.title(237,172,'library',center=True)
    p.text_at(84,204,'start fresh','muted',center=True)
    p.text_at(237,204,'all projects','muted',center=True)
    p.hit('new patch',(16,164,151,215))
    p.hit('library',(170,164,304,215))
    p.footer(settings=True)
    return p


def cabinet(theme, recent=RECENTS, saved=3):
    """A small index on the left; the formerly decorative lane becomes recents."""
    p=Page(theme)
    p.title(20,24,'new patch')
    p.text_at(20,58,'start fresh','muted')
    p.title(20,102,'library')
    p.text_at(20,136,f'{saved} saved','muted')
    p.title(20,194,'settings','muted')
    p.hit('new patch',(12,16,124,80))
    p.hit('library',(12,94,124,155))
    p.hit('settings',(12,186,124,221))
    p.line([(134,24),(134,211)])
    p.text_at(156,21,'recent','muted')
    if recent:
        for n,(name,age) in enumerate(recent[:3]):
            y=(49,116,159)[n]
            if n==0:
                p.title(156,y,name,'accent',max_width=145)
                p.text_at(156,85,age,'muted',limit=23)
                p.diamond(134,64,rx=3,ry=6)
                p.line([(137,64),(145,64)],'accent')
                p.hit('load '+name,(146,43,308,103))
            else:
                p.text_at(156,y,name,limit=23)
                p.text_at(156,y+14,age,'muted',limit=23)
                p.hit('load '+name,(146,y-6,308,y+30))
    else:
        p.text_at(156,56,'no recent history' if saved else 'no projects yet','muted')
        p.text_at(156,76,'open the library' if saved else 'start a new patch','muted')
    p.text_at(156,205,'all projects','muted')
    p.hit('all projects',(146,193,308,221))
    p.footer('a load' if recent else 'a open')
    return p


def index(theme, recent=RECENTS, saved=3):
    """Four full-width typographic rows. The simplest option; no new illustration."""
    p=Page(theme)
    action,name,target=primary(recent,saved)
    labels=(action,'new patch','library','settings')
    details=(name,'start fresh',f'{saved} saved projects','appearance / sound')
    targets=(target,'new patch','library','settings')
    for i,(label,detail,hit) in enumerate(zip(labels,details,targets)):
        y=20+i*53
        role='accent' if i==0 else 'ink'
        p.title(48,y,label,role)
        p.text_at(48,y+31,detail,'muted',limit=36)
        p.text_at(298,y+7,f'{i+1:02d}','muted',right=True)
        if i<3:
            p.line([(48,y+43),(299,y+43)])
        p.hit(hit,(12,y-6,308,y+46))
    p.line([(24,28),(24,209)])
    p.diamond(24,29,rx=3,ry=6)
    p.line([(48,63),(76,63)],'accent')
    p.footer()
    return p


VARIANTS = [
    ('01-folio','01 / folio','return first / recents always visible',folio),
    ('02-threshold','02 / threshold','one central entry / a quiet pointed arch',threshold),
    ('03-cabinet','03 / cabinet','actions left / remembered projects right',cabinet),
    ('04-index','04 / index','four broad rows / no large illustration',index),
]


def sheet_title(c,x,y,value,role='ink'):
    mask=heading_mask(value)
    box=(x,y,x+mask.width,y+mask.height)
    assert box[2]<=c.im.width and box[3]<=c.im.height
    assert all(not overlaps(box,old) for old,_ in c.labels)
    c.labels.append((box,value))
    c.im.paste(COLORS['graphite'][role],(x,y),mask)


def sheet(theme,pages):
    c=Canvas(704,668,OUTSIDE)
    sheet_title(c,16,14,'seance / a different entrance')
    c.text(16,47,'four lower-screen proposals / native 320 x 240',COLORS['graphite']['muted'])
    for n,(slug,title,subtitle,_) in enumerate(VARIANTS):
        x=16+(n%2)*352; y=75+(n//2)*284
        c.text(x,y,title,COLORS['graphite']['accent'])
        c.im.paste(pages[slug].im,(x,y+18))
        c.text(x,y+268,subtitle,COLORS['graphite']['muted'])
    c.text(16,650,'design study / example slots / unchanged upper logo / not installed',COLORS['graphite']['muted'])
    return c.im


def save(image,name,meta):
    path=OUT/name
    assert not path.exists(), f'preserve existing render: {path}'
    result=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
    assert result.resize(image.size,Image.Resampling.NEAREST).tobytes()==image.tobytes()
    info=PngImagePlugin.PngInfo()
    info.add_text('description',__doc__)
    info.add_text('provenance',json.dumps(meta,sort_keys=True))
    result.save(path,pnginfo=info)
    with Image.open(path) as decoded:
        decoded.load()
        assert decoded.size==result.size and decoded.convert('RGB').tobytes()==result.tobytes()
    return dict(path=str(path.relative_to(ROOT)),size=list(result.size),bytes=path.stat().st_size,sha256=sha(path))


def validate_page(p):
    allowed=set(raw(p.theme,'paper_bottom').get_flattened_data()) | set(p.colors.values())
    assert set(p.im.get_flattened_data()) <= allowed, 'foreign palette/antialiasing'
    # Every declared proposed target is non-overlapping and large enough for the
    # stylus; these are design rectangles, NOT implemented/validated controllers.
    assert p.hits and p.labels


def main():
    source_before={n:sha(ROOT/n) for n in INPUTS}
    capture=json.loads((OUT/'capture.json').read_text())
    protected=json.loads((OUT/'protected-before.json').read_text())
    assert all(sha(ROOT/n)==value for n,value in capture['source_sha256'].items())
    assert sha(capture['binary'])==capture['binary_sha256']
    assert all(sha(OUT/n)==value for n,value in capture['surface_sha256'].items())
    for word,mask in masks():
        fresh=heading_mask(word)
        assert fresh.size==mask.size and fresh.tobytes()==mask.tobytes()
    cases=0; rendered={}
    for theme in COLORS:
        pages={}
        for slug,_,_,render in VARIANTS:
            p=render(theme); validate_page(p); pages[slug]=p; cases+=1
            for recent,saved in (([],0),([],128),(RECENTS[:1],1),
                                  ([('a very long patch title','yesterday'),('another long name','2 hours')],128)):
                test=render(theme,recent,saved); validate_page(test); cases+=1
        rendered[theme]=pages
    meta=dict(status='unselected visual proposals; no runtime changes',
              rendered_at=datetime.now(timezone.utc).isoformat(),
              capture_sha256=sha(OUT/'capture.json'),source_sha256=source_before,
              current='fresh actual C++ render; host 20px paper fallback; not hardware',
              proposals='authored native Pillow layouts using the same captured paper and original fonts',
              project_metadata='three illustrative valid slots; not read from user storage',
              native_lower=[320,240],upper='actual C++ top copied unchanged into each graphite pair',
              interaction='proposed targets only; no new controller or gestures implemented')
    outputs=[]
    # Complete in-memory validation happens before any output PNG is written.
    boards={theme:sheet(theme,pages) for theme,pages in rendered.items()}
    for theme,board in boards.items():
        outputs.append(save(board,f'start-menu-four-{theme}-2x.png',meta))
    for slug,_,_,_ in VARIANTS:
        pair=Image.new('RGB',(400,496),OUTSIDE)
        upper=raw('graphite','closed_top')
        pair.paste(upper,(0,0)); pair.paste(rendered['graphite'][slug].im,(40,256))
        assert pair.crop((0,0,400,240)).tobytes()==upper.tobytes()
        assert pair.crop((40,256,360,496)).tobytes()==rendered['graphite'][slug].im.tobytes()
        outputs.append(save(pair,slug+'-dual-2x.png',dict(meta,variant=slug)))
    assert source_before=={n:sha(ROOT/n) for n in INPUTS}
    assert all(sha(ROOT/n)==value for n,value in protected.items()), 'production/asset source changed'
    report=dict(meta,outputs=outputs,validation=dict(native_layout_states=cases,
                existing_heading_masks_exact=True,text_bounds_and_overlap=True,
                target_bounds_and_overlap=True,minimum_target=[40,24],native_palette=True,
                png_decode_and_nearest2x=True,paired_upper_pixel_equality=True,
                protected_files_unchanged=len(protected)),
                proposals={slug:dict(labels=p.labels,proposed_targets=p.hits)
                           for slug,p in rendered['graphite'].items()})
    (OUT/'proposal.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    for item in outputs:
        print(item['path'],item['size'],item['bytes'],item['sha256'])
    print(f'pass: {cases} native layout states; labels/targets/palette/upper/NN2x/decode; {len(protected)} protected files unchanged')
    print('design only; no app/ARM build, functional suite, animation/audio export or deployment')


if __name__=='__main__':
    main()
