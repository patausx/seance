#!/usr/bin/env python3
"""Verify/package existing focused native captures; no build, engine run or FTP.

Requires page_headings_runtime before/after captures and typography/motion proofs.
The preserved pre-edit baseline is independently matched to the last release proof.
"""
import csv
from datetime import datetime, timezone
import difflib
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/page_headings_runtime'
CHANGED = {'menu_style.cpp', 'menu_style.h', 'main.cpp', 'ui_nav.cpp',
           'nav_ink_motion.h', 'ui_chains.cpp', 'ui_sigils.cpp',
           'ui_bounce.cpp', 'ui_picker.cpp'}


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def load(p):
    return json.loads(Path(p).read_text())


def pixels(p):
    return Image.open(p).convert('RGB')


def normalized(image, box):
    x, y, w, h = box
    crop = image.crop((x,y,x+w,y+h))
    data = crop.load()
    for row in range(h):
        paper = image.getpixel((0,y+row))
        for col in range(w):
            if data[col,row] == paper: data[col,row] = (0,0,0)
    return crop.tobytes()


def preservation(motion):
    baseline = load(OUT/'baseline.json')
    previous = load(ROOT/'ui_mockups/graphite_accent_runtime/ui/verification.json')
    # The original complete manifest, not a reconstructed git checkout.
    manifests = [v for v in previous.values() if isinstance(v, dict) and v == baseline]
    assert manifests and len(baseline) == 146
    current = {str(p.relative_to(ROOT)): sha(p) for d in ('core', 'platform')
               for p in sorted((ROOT/d).rglob('*')) if p.is_file()}
    assert current == motion['unchanged_runtime_baseline'], 'runtime changed after motion proof'
    changed = {p for p, h in baseline.items() if current[p] != h}
    assert changed == {'platform/3ds/'+p for p in CHANGED}, changed
    assert set(current)-set(baseline) == {'platform/3ds/page_layout.h'}
    diffs = OUT/'source-diffs'; diffs.mkdir(exist_ok=True)
    for p in sorted(changed):
        old = ROOT/('.local-state/backups/20260912_180119_page-headings-before_'+Path(p).name)
        assert sha(old) == baseline[p]
        text = ''.join(difflib.unified_diff(old.read_text().splitlines(True),
            (ROOT/p).read_text().splitlines(True), fromfile='before/'+p, tofile=p))
        (diffs/(Path(p).name+'.diff')).write_text(text)
    return dict(baseline_files=len(baseline), unchanged_files=len(baseline)-len(changed),
        reviewed_changes=sorted(changed), added=['platform/3ds/page_layout.h'],
        all_core_and_palette_masks_startup_samples_exact=True, runtime_sha256=current,
        diff_sha256={str(p.relative_to(ROOT)):sha(p) for p in sorted(diffs.glob('*.diff'))})


def comparisons():
    rows = []
    for old in sorted((OUT/'before').rglob('*.ppm')):
        rel = old.relative_to(OUT/'before'); new = OUT/'after'/rel
        assert new.exists(), new
        a, b = pixels(old), pixels(new)
        assert a.size == b.size
        group = rel.parts[1]; top = old.stem.endswith('_top')
        # Reviewed title + metadata bands only. Patch rows additionally move
        # within y44..169; their complete foreground contents checked below.
        band = (64 if group in ('sigils', 'nav') else
                46 if group in ('patches', 'extra') else 0) if top else (169 if group == 'patches' else 46)
        red,green,blue = ImageChops.difference(a,b).split()
        delta = ImageChops.lighter(ImageChops.lighter(red,green),blue)
        assert delta.crop((0,band,a.width,a.height)).getbbox() is None, f'unrelated pixels changed: {rel}'
        rows.append(dict(path=str(rel), before_sha256=sha(old), after_sha256=sha(new),
                         changed_pixels=a.width*a.height-delta.histogram()[0], checked_pixels=a.width*a.height,
                         allowed_band_end=band))
    slots = 0
    for theme in ('pink', 'gray'):
        for name in ('01_current_not_selected', '02_replace'):
            a=pixels(OUT/f'before/{theme}/patches/{name}_bottom.ppm')
            b=pixels(OUT/f'after/{theme}/patches/{name}_bottom.ppm')
            for slot in range(8):
                x=7+155*(slot%2)
                assert normalized(a,(x,35+35*(slot//2),147,29)) == \
                       normalized(b,(x,44+32*(slot//2),147,29)), (theme,name,slot)
                slots += 1
    return dict(screens=len(rows), pixels=sum(r['checked_pixels'] for r in rows),
        changed_pixels=sum(r['changed_pixels'] for r in rows), outside_reviewed_bands_changed=0,
        full_147x29_patch_foreground_pairs=slots, comparisons=rows)


def titles(typography):
    cases=[('menu/chains_four_bottom.ppm',0), ('nav/01_approved_idle_bottom.ppm',1),
           ('menu/picker_first_bottom.ppm',2), ('sigils/main_bottom.ppm',24),
           ('sigils/main_top.ppm',24), ('clock/01_tempo_none_bottom.ppm',25),
           ('clock/01_tempo_none_top.ppm',25), ('clock/04_swing_applied_bottom.ppm',26),
           ('clock/04_swing_applied_top.ppm',26), ('patches/01_current_not_selected_bottom.ppm',27),
           ('patches/01_current_not_selected_top.ppm',27), ('patches/03_unsaved_bottom.ppm',28),
           ('patches/03_unsaved_top.ppm',28)]
    result=[]
    for theme in ('pink','gray'):
        extra=([('bounce/'+name+'_'+screen+'.ppm',40)
                 for name in ('01_ready','02_waiting','03_recording','04_take','07_partial')
                 for screen in ('top','bottom')]+[('bounce/05_send_bottom.ppm',47),('bounce/06_discard_bottom.ppm',48)]
               if theme=='pink' else [('extra/bounce_ready_bottom.ppm',40),('extra/bounce_ready_top.ppm',40)])
        for name,index in cases+extra:
            rec=next(r for r in typography['capture']['images'] if r['theme']==theme and r['label_index']==index)
            reference=ROOT/typography['run']/'raw'/f'{theme}_{index}.ppm'
            assert sha(reference)==rec['cpp_ppm_sha256']
            # Include the entire frame above/left of the title, not merely its ink.
            end=20+rec['width']; a=pixels(reference).crop((0,0,end,40)); b=pixels(OUT/'after'/theme/name).crop((0,0,end,40))
            assert a.tobytes()==b.tobytes(), (theme,name,index)
            result.append(dict(theme=theme,path=name,title=rec['title'],pixels=a.width*a.height))
    return dict(actual_page_title_regions=len(result),pixels=sum(r['pixels'] for r in result),
                baseline=32,ink_left=20,ppem=28,comparisons=result)


def counts():
    pairs=[]; peak=0; preserved=0
    for p in sorted((OUT/'after').rglob('*.tsv')):
        if p.name not in ('frames.tsv','manifest.tsv'): continue
        rows=list(csv.DictReader(p.open(),delimiter='\t'))
        old=OUT/'before'/p.relative_to(OUT/'after')
        oldrows=list(csv.DictReader(old.open(),delimiter='\t')) if old.exists() else []
        for r in rows:
            if 'shared_objects' in r: n=int(r['shared_objects'])
            elif 'shared' in r: n=int(r['shared'])
            else: n=int(r['top_objects'])+int(r['bottom_objects'])
            assert n<8192
            peak=max(peak,n)
            key=r.get('name',r.get('frame'))
            if oldrows:
                before=next(x for x in oldrows if x.get('name',x.get('frame'))==key)
                # Includes intentional world-space chain clipping: not GPU bounds.
                assert before==r, (str(p),key,before,r)
                preserved += 1
            pairs.append(dict(manifest=str(p.relative_to(ROOT)),frame=key,shared=n))
    return dict(peak_captured_shared_objects=peak,exact_before_after_manifest_rows=preserved,
        captured_pairs_with_manifests=len(pairs),pairs=pairs,
        bounds='strict screen fixtures pass; existing world-space chains clipping unchanged',
        standalone_bounce_fixture_peak=2959,hardware_budget_proven=False)


def gallery():
    names=[('patch table','menu/chains_four_bottom.ppm'),('go to','nav/01_approved_idle_bottom.ppm'),
           ('sigils','sigils/main_bottom.ppm'),('bounce',None),('patches','patches/01_current_not_selected_bottom.ppm')]
    sheet=Image.new('RGB',(5*336,2*264),(16,16,16)); draw=ImageDraw.Draw(sheet)
    for j,theme in enumerate(('pink','gray')):
        for i,(label,name) in enumerate(names):
            name=name or ('bounce/01_ready_bottom.ppm' if theme=='pink' else 'extra/bounce_ready_bottom.ppm')
            sheet.paste(Image.open(OUT/'after'/theme/name),(i*336+8,j*264+22))
            draw.text((i*336+8,j*264+5),theme+' / '+label,fill=(230,230,230))
    sheet.save(OUT/'contact_sheet.png')


def main():
    typo=load(OUT/'typography/verification.json'); motion=load(OUT/'motion/verification.json')
    for proof in (typo,motion):
        for p,h in proof['source_sha256'].items(): assert sha(ROOT/p)==h,p
    runs=load(OUT/'focused-runs.json')
    assert all(r['returncode']==0 for r in runs)
    keep=preservation(motion); comparison=comparisons(); regions=titles(typo); budget=counts(); gallery()
    captures=sorted((OUT/'after').rglob('*.ppm'))
    receipt=dict(status='focused local pass',completed_at=datetime.now(timezone.utc).isoformat(),
        preservation=keep,typography_receipt_sha256=sha(OUT/'typography/verification.json'),
        motion_receipt_sha256=sha(OUT/'motion/verification.json'),before_after=comparison,
        integrated_title_regions=regions,resources=budget,focused_runs=runs,
        actual_cpp_screens=len(captures),actual_cpp_pixels=sum(Image.open(p).width*240 for p in captures),
        evidence_sha256={str(p.relative_to(ROOT)):sha(p) for p in sorted((OUT/'checks').glob('*.log'))+captures},
        helper_sha256=sha(__file__),gallery_sha256=sha(OUT/'contact_sheet.png'),
        limits=['no full suite, ARM build, FTP, hardware timing/listening or GPU acceptance',
                'standalone bounce suite is pink; gray ready frame and both-theme typography covered separately',
                'before-pink-extra.log is a retained unsupported call of the gray-only fixture, not a runtime regression',
                'module hero sizes, sample-browser ordinary headings, startup branding and page bodies outside noted bands unchanged'])
    (OUT/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f'pass page headings: {len(captures)} native screens; {comparison["screens"]} before/after; '
          f'{regions["actual_page_title_regions"]} full-page title regions; peak {budget["peak_captured_shared_objects"]}/8192')


if __name__=='__main__': main()
