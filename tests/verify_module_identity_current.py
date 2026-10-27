#!/usr/bin/env python3
"""Verify a dated current-only native-host capture, never build or rewrite old proof.

Decode the C++ packed title bitmaps independently and compare the complete
foreground pattern at exactly (14, 2) on all 112 default LCD surfaces. Named
PLAY/GRAN workflow pages and MOUTH selected-detail states are audited apart
from that unconditional default contract. Historical MOD lens collisions remain
visible; no blanket clean-lens claim.
"""
from pathlib import Path
from PIL import Image
from datetime import datetime, timezone
import hashlib
import json
import re
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'release/module-identity-current'
HISTORICAL = ROOT / 'release/module-headings/current-optimized-20261004T054122_916843Z/receipt.json'
LABELS = ROOT / 'platform/3ds/module_heading_data.h'
ALIASES = {'chorus':'chor', 'delay':'dly'}
SPOT_RGB = {'graphite':(181,154,232), 'pink':(91,49,124)}
# These are extra fixture views; every default pair remains unconditional.
EXTRA = {'play':{'stretch','scratch','hold','cuts','loop','config'},
         'gran':{'cloud','setup','legacy','scan-touch','fine-touch'},
         'mouth':{'size','out','out-bypass'}}
ORIGIN = (14,2)
CASES = ROOT / 'tests/module_headings_ui_cases.inc'
EXTRA_CASES = ROOT / 'tests/module_identity_current_extra_cases.inc'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_masks():
    """Use packed C++ bytes and row stride, not an OCR/search or a font redraw."""
    source = LABELS.read_text()
    data = bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',
                 source.split('bitmap[] = {',1)[1].split('};',1)[0]))
    labels = {}
    rows = re.findall(r'\{UnitKind::(\w+), "([^"]+)", (\d+), (\d+), (\d+)\}', source)
    for kind,name,offset,width,height in rows:
        off,w,h = map(int,(offset,width,height)); stride=(w+7)//8
        assert 0<w<=64 and 0<h<=26 and 0<=off and off+stride*h<=len(data), (kind,name)
        bits=bytes(bool(data[off+y*stride+x//8] & (128>>(x&7)))
                   for y in range(h) for x in range(w))
        assert 20<=sum(bits)<w*h, (kind,name)
        assert name not in labels
        labels[name]=(w,h,bits,kind)
    assert len(labels)==len(rows)==28
    assert len({r[3] for r in labels.values()})==28
    return labels


def exact_title(image, mask, color, origin=ORIGIN):
    """Check *all* pixels in the fixed mask rectangle, including clear bits."""
    x,y=origin;w,h,bits,_=mask
    with Image.open(image) as im:
        assert im.mode=='RGB' and im.size in ((400,240),(320,240)), image
        assert x>=0 and y>=0 and x+w<=im.width and y+h<=im.height
        actual=list(im.crop((x,y,x+w,y+h)).getdata())
    wrong=[(i%w,i//w, bool(bits[i]),rgb) for i,rgb in enumerate(actual)
           if (rgb==color)!=bool(bits[i])]
    assert not wrong, ('wrong exact title pixels',str(image),origin,'count',len(wrong),'first',wrong[:8])
    return {'image':str(image),'origin':[x,y],'size':[w,h],
            'foreground_rgb':list(color),'ink_pixels':sum(bits),'checked_pixels':w*h}


def current_cases():
    """Compose private current-only additions; never patch the historic case file."""
    text=CASES.read_text();extension=EXTRA_CASES.read_text()
    before='\nint run(';call='  variants(p);\n'
    assert text.count(before)==text.count(call)==1
    assert extension.count('void extra_identity_states(')==1
    return ('#include "'+str(ROOT/'platform/3ds/gran_ui.h')+'"\n'+text.replace(
        before,'\n'+extension+'\nint run(').replace(
        call,'  variants(p);extra_identity_states(p);\n'))


def inspect(receipt_path):
    receipt_path=Path(receipt_path).resolve()
    current=json.loads(receipt_path.read_text())
    assert current['status']=='passed' and current['run']['exit']==0
    assert current['root']==str(ROOT) and current['tag']=='current-optimized'
    assert sha(current['binary'])==current['binary_sha256']
    assert current['case_sha256']==hashlib.sha256(current_cases().encode()).hexdigest()
    folder=Path(current['capture']);assert folder.parent==receipt_path.parent
    assert sha(receipt_path.parent/'cases-source.inc')==current['case_sha256']
    # Fail closed if either production or any compiled transitive input changed.
    for name,digest in current['source_sha256'].items():
        assert sha(ROOT/name)==digest, ('source changed since capture',name)
    for name,digest in current['dependencies'].items():
        assert sha(name)==digest, ('dependency changed since capture',name)
    for name,digest in current.get('media',{}).items():
        assert sha(folder/name)==digest, ('capture changed since receipt',name)
    rows=current['surfaces'];assert len(rows)==712 and current['native_duals']==356
    assert len({(r['id'],r['surface']) for r in rows})==len(rows)
    assert all(r['outside_rects']=='0' and r['outside_lines']=='0' for r in rows)
    defaults=[r for r in rows if r['variant']=='default']
    assert len(defaults)==112
    assert {(r['module'],r['surface'],r['id'].split('_',1)[0]) for r in defaults}=={
        (name,surface,theme) for name in {r['module'] for r in defaults}
        for surface in ('top','bottom') for theme in SPOT_RGB}
    assert len({r['module'] for r in defaults})==28
    assert all(r['text_overlaps']=='0' for r in rows if r['variant']!='lens')

    historical=json.loads(HISTORICAL.read_text())
    old={(r['id'],r['surface']):int(r['text_overlaps']) for r in historical['surfaces']}
    deltas=[];remaining=[]
    for r in rows:
        key=(r['id'],r['surface']);now=int(r['text_overlaps'])
        if r['variant']=='lens':
            assert key in old and now<=old[key],(key,old.get(key),now)
            if now!=old[key]:deltas.append({'id':r['id'],'surface':r['surface'],'historical':old[key],'current':now})
            if now:remaining.append({'id':r['id'],'surface':r['surface'],'overlaps':now})
    # Lens text is rendered under a translucent shared MOD pane; do not mark
    # those inherited collisions as clean or silently erase them from the report.
    masks=load_masks();verified=[]
    for r in defaults:
        theme=r['id'].split('_',1)[0];assert theme in SPOT_RGB
        name=ALIASES.get(r['module'],r['module']);assert name in masks,(r['module'],name)
        path=folder/(r['id']+'_'+r['surface']+'.ppm')
        item=exact_title(path,masks[name],SPOT_RGB[theme])
        item.update(module=r['module'],theme=theme,surface=r['surface'],variant='default')
        verified.append(item)
    assert len(verified)==112
    assert {r['module'] for r in verified}=={r['module'] for r in defaults}

    # PLAY/GRAN named workflow pages, and MOUTH selected-response views (not
    # separate pages), are additional paired checks, never default exemptions.
    extra_rows=[r for r in rows if r['module'] in EXTRA and r['variant'] in EXTRA[r['module']]]
    expected={(theme,name,variant,surface) for theme in SPOT_RGB for name,variants in EXTRA.items()
              for variant in variants for surface in ('top','bottom')}
    assert {(r['id'].split('_',1)[0],r['module'],r['variant'],r['surface']) for r in extra_rows}==expected
    assert len(extra_rows)==len(expected)==56
    extra=[]
    for r in extra_rows:
        theme=r['id'].split('_',1)[0];path=folder/(r['id']+'_'+r['surface']+'.ppm')
        item=exact_title(path,masks[ALIASES.get(r['module'],r['module'])],SPOT_RGB[theme])
        item.update(module=r['module'],theme=theme,surface=r['surface'],variant=r['variant'])
        extra.append(item)
    gestures=[r for r in rows if r['module']=='comp' and r['variant']=='back-controls']
    assert {(r['id'].split('_',1)[0],r['surface']) for r in gestures}=={
        (theme,surface) for theme in SPOT_RGB for surface in ('top','bottom')}
    checked_gestures=[]
    for r in gestures:
        theme=r['id'].split('_',1)[0]
        item=exact_title(folder/(r['id']+'_'+r['surface']+'.ppm'),masks['comp'],SPOT_RGB[theme])
        item.update(module='comp',theme=theme,surface=r['surface'],variant='back-controls')
        checked_gestures.append(item)
    log=Path(current['run']['log']).read_text()
    assert log.count('identity gesture checks: comp back twice; gran scan reset/no-op; fine toggle/revert')==2
    report={'status':'passed','scope':'current-only composed native-host exact C++ title pixels at fixed (14,2)',
            'receipt':str(receipt_path),'receipt_sha256':sha(receipt_path),
            'current_cases_sha256':current['case_sha256'],'cpp_bitmap_sha256':sha(LABELS),
            'historical_overlap_oracle':str(HISTORICAL),'historical_sha256':sha(HISTORICAL),
            'native_composed_duals':current['native_duals'],'surfaces':len(rows),
            'default_surfaces':112,'default_top':56,'default_bottom':56,
            'private_mask_exact_in_default_surfaces':len(verified),'default_title_checks':verified,
            'extra_play_gran_workflows_mouth_selected_views':extra,
            'comp_back_controls_title_checks':checked_gestures,
            'gesture_checks':'actual COMP back twice, GRAN scan reset/no-op and fine toggle/revert passed in native host',
            'nonlens_text_overlaps':0,'lens_text_overlaps_remaining':len(remaining),
            'lens_collisions':remaining,'historical_lens_overlap_improvements':deltas,
            'bounds_outside':0,
            'limits':['shared MOD lens can retain translucent underlay text collisions; no clean-lens claim',
                      'historical receipt read only as lens overlap oracle, never rerun or rewritten',
                      'MOUTH extra views are selected inspector details, not separate workflow subpages',
                      'not a frozen-body equivalence or complete functional/sound regression',
                      'software host synthetic fixtures; not hardware FPS/touch, ARM or deployment']}
    dest=OUT/('verified-current-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')+'-'+sha(receipt_path)[:12]+'.json')
    with dest.open('x') as f:f.write(json.dumps(report,indent=2,sort_keys=True)+'\n')
    return dest,report


if __name__=='__main__':
    pointer=json.loads((OUT/'current-optimized-latest.json').read_text())
    path=Path(sys.argv[1]) if len(sys.argv)>1 else Path(pointer['receipt'])
    dest,result=inspect(path)
    print(dest)
    print('default masks at (14,2):',result['private_mask_exact_in_default_surfaces'],'/112;',
          'extra workflow/selected surfaces:',len(result['extra_play_gran_workflows_mouth_selected_views']),
          'non-lens text collisions:',result['nonlens_text_overlaps'],
          'inherited MOD lens underlays still collide:',result['lens_text_overlaps_remaining'])
