#!/usr/bin/env python3
"""Filter proposal v2: existing mode-curve profiles instead of gothic word keys.

Authored Pillow design only. All pixels outside the lower mode row are preserved
from the verified first proposal, including the full upper screen and animation.
No production code, touch controller, DSP, build or deployment changes.
"""
from datetime import datetime, timezone
import json
import re

from PIL import Image, ImageChops, ImageDraw
import render_filter_interaction_study as base
from render_bounce_visual_study import Page, sha, protected, dual

ROOT=base.ROOT
PRIOR=base.OUT
OUT=PRIOR/'mode-curves-v2'
ROW=(0,28,320,59)
DUAL_ROW=(40,284,360,315)


def unchanged_outside(a,b,box):
    difference=ImageChops.difference(a,b)
    ImageDraw.Draw(difference).rectangle((box[0],box[1],box[2]-1,box[3]-1),fill=(0,0,0))
    assert difference.getbbox() is None,'changed outside the mode row'


def profiles():
    source=(ROOT/'platform/3ds/ui_module_focus_filter.cpp').read_text()
    match=re.search(r'shapes\[5\]\[5\]=\{(.*?)\};',source)
    assert match,'current production mode icons not found'
    result=[tuple(map(int,row.split(','))) for row in re.findall(r'\{([^{}]+)\}',match.group(1))]
    assert result==[(1,1,2,8,9),(9,8,2,1,1),(9,5,1,5,9),(1,2,9,2,1),(5,5,5,5,5)]
    return result


def lower(theme,state,shapes,pointer=None):
    p=base.lower(theme,state,pointer)
    original=p.im.copy()
    # Replace only this row using the same current static base paper.
    p.im.paste(Page(320,theme).im.crop(ROW),ROW[:2])
    p.labels=[(box,label) for box,label in p.labels if not (ROW[1]<=box[1] and box[3]<=ROW[3])]
    for i,(mode,shape) in enumerate(zip(base.MODES,shapes)):
        cx=36+61*i
        role='accent' if state['mode']==mode else 'muted'
        # Same 5 vertices as current runtime icons; twice the geometry, thin pen.
        points=[(cx-12+6*j,31+2*v) for j,v in enumerate(shape)]
        hit=p.hits[i+1][1]
        assert all(hit[0]<=x<hit[2] and hit[1]<=y<hit[3] for x,y in points)
        p.d.line(points,fill=p.c[role],width=1)
        if state['mode']==mode:p.diamond(cx,55,'accent',True)
    assert not any(label in base.MODES for _,label in p.labels)
    unchanged_outside(original,p.im,ROW)
    return p


def main():
    assert not OUT.exists(),'preserve previous revision'
    prior=json.loads((PRIOR/'proposal.json').read_text())
    for field in ('source_sha256','protected_sha256','ready_artifacts_sha256'):
        assert all(sha(ROOT/name)==h for name,h in prior[field].items()),field
    for item in prior['outputs']:assert sha(ROOT/item['path'])==item['sha256']
    sources=dict(prior['source_sha256'])
    sources['tools/render_filter_icons_study.py']=sha(ROOT/'tools/render_filter_icons_study.py')
    before=protected()
    shapes=profiles()
    # Only the revised keys need new exhaustive mode/theme layout checks.
    for theme in base.COLORS:
        for mode in base.MODES:
            s=base.config(mode=mode)
            old=base.lower(theme,s);new=lower(theme,s,shapes)
            assert old.hits==new.hits
    pairs={}
    for theme in base.COLORS:
        s=base.config()
        pairs[theme]=dual(base.upper(theme,s),lower(theme,s,shapes))
        with Image.open(PRIOR/('filter-response-'+theme+'-dual-2x.png')) as image:
            old=image.convert('RGB').resize((400,496),Image.Resampling.NEAREST)
            unchanged_outside(old,pairs[theme],DUAL_ROW)
    frames=[]
    for i in range(192):
        s,pointer=base.demo(i*.05)
        frames.append(dual(base.upper('graphite',s),lower('graphite',s,shapes,pointer)))
    # Compare EVERY frame against the decoded previous GIF, not a reauthored top.
    with Image.open(PRIOR/'filter-response-motion-2x.gif') as image:
        tick=0
        for i in range(image.n_frames):
            image.seek(i)
            old=image.convert('RGB').resize((400,496),Image.Resampling.NEAREST)
            assert image.info['duration']%50==0
            for _ in range(image.info['duration']//50):
                unchanged_outside(old,frames[tick],DUAL_ROW);tick+=1
        assert tick==192
    assert frames[0].tobytes()==frames[-1].tobytes()
    OUT.mkdir(parents=True)
    # Reuse the exact existing lossless output packagers with a new destination.
    base.OUT=OUT
    outputs=[base.save_png(pairs[theme],'filter-response-'+theme+'-dual-2x.png') for theme in base.COLORS]
    gif,colors=base.save_gif(frames);outputs.append(gif)
    assert protected()==before
    assert all(sha(ROOT/name)==h for name,h in sources.items())
    assert all(sha(ROOT/name)==h for name,h in prior['ready_artifacts_sha256'].items())
    assert all(sha(ROOT/item['path'])==item['sha256'] for item in prior['outputs'])
    report=dict(created_at=datetime.now(timezone.utc).isoformat(),status='design revision requested by user; not runtime implementation',
        origin=__doc__,source_sha256=sources,protected_sha256=before,
        ready_artifacts_sha256=prior['ready_artifacts_sha256'],prior_report_sha256=sha(PRIOR/'proposal.json'),
        change='lower mode word keys replaced by the five existing runtime curve profiles, 2x geometry / 1px pen',
        mode_profiles=shapes,changed_native_dual_box=DUAL_ROW,
        validation=dict(mode_theme_layouts=10,unchanged_targets=True,unchanged_outside_mode_row_frames=192,
                        full_upper_unchanged=True,both_static_duals_unchanged_outside_row=True,
                        exact_indexed_gif_colors=colors,gif_duration_ms=9600,periodic_first_last=True),
        outputs=[dict(path=str(p.relative_to(ROOT)),bytes=p.stat().st_size,sha256=sha(p)) for p in outputs],
        limits=prior['limits'])
    (OUT/'proposal.json').write_text(json.dumps(report,indent=2)+'\n')
    for p in outputs:print(p.relative_to(ROOT),p.stat().st_size,sha(p))
    print('pass: 10 mode/theme layouts, 192 old-GIF comparisons; only lower mode row changed; no app changes')


if __name__=='__main__':main()
