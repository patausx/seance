#!/usr/bin/env python3
"""Compare retained before/after nonmodule rasters outside the migrated controls."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def boxes(name):
    name=name.split('_',1)[1]
    if name.startswith('mixer_'):
        return []  # Explicit user exception: EVERY mixer pixel must remain original.
    if name.startswith('banks_'):return [(17,128,304,141)]+[(13+104*c,198,100+104*c,209) for c in range(3)]
    if name.startswith('patchbay_'):return [(77,198,258,210)]
    if name.startswith('chains_'):
        if name in ('chains_live','chains_link_equal'):return [(51,122,189,133)]
        if name in ('chains_disclosure_cache','chains_picker_fade'):return [(7,85,153,104),(7,148,153,167)]
        return [(11,89,149,100),(11,152,149,163)]
    if name.startswith('sigil_'):
        # Depth/mod-depth gesture underlines moved from marker tips y158/y225
        # to y160/y227; phase underline stays y179. These are render-only
        # feedback annotations, not changed rail anchors or touch geometry.
        result=[(205,148,304,161),(107,168,304,180),(187,215,304,228)]
        return [(a-4,b-4,c+4,d+4) for a,b,c,d in result] if 'disclosure_cache' in name else result
    if name.startswith('score_6_'):return [(157,213,319,224)]
    if name.startswith(('score_1_','score_2_','score_5_')):return [(35,213,286,224)]
    if name in ('score_no_target','score_discrete'):return []
    raise AssertionError(name)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('before');ap.add_argument('after');args=ap.parse_args()
    proofs=[json.loads(Path(p).read_text()) for p in (args.before,args.after)]
    assert all(p['status']=='passed' and p['run_exit']==0 for p in proofs)
    roots=[Path(p['capture']) for p in proofs]
    names=[{p.name for p in d.glob('*.ppm')} for d in roots];assert names[0]==names[1]
    result=[]
    for name in sorted(names[0]):
        with Image.open(roots[0]/name) as before,Image.open(roots[1]/name) as after:
            assert before.size==after.size
            difference=ImageChops.difference(before,after);mask=Image.new('1',before.size)
            draw=ImageDraw.Draw(mask)
            for a,b,c,d in boxes(Path(name).stem):draw.rectangle((a,b,c-1,d-1),fill=1)
            outside=Image.composite(Image.new('RGB',before.size),difference,mask)
            assert not outside.getbbox(),('non-fader pixels changed',name,outside.getbbox())
            result.append({'image':name,'allowed':boxes(Path(name).stem),'changed_bbox':difference.getbbox()})
    pulse_checks=[]
    for theme,spot in (('graphite',(181,154,232)),('pink',(91,49,124))):
        # The existing edited fixture consumes pending feedback on capture, so
        # all three pulses are active here. Compare with same-position static
        # rails already checked against the native canonical helper in C++.
        with Image.open(roots[1]/(theme+'_sigil_edited.ppm')) as pulse,Image.open(roots[1]/(theme+'_sigil_extrema_32767.ppm')) as still:
            for y in (153,173,220):
                box=((205 if y==153 else 107 if y==173 else 187),y-5,304,y+6)
                assert ImageChops.difference(pulse.crop(box),still.crop(box)).getbbox() is None,('pulse overwrote lancet',theme,y)
            for y in (160,179,227):
                assert all(pulse.getpixel((x,y))==spot for x in range(297,304)),('missing real gesture pulse',theme,y)
            pulse_checks.append({'theme':theme,'canonical_rails_unchanged_with_active_pulses':[153,173,220],'actual_pulse_rows':[160,179,227]})
    folder=ROOT/'release/lancet-everywhere/nonmodule'/('comparison-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ'));folder.mkdir()
    proof={'status':'passed','before':args.before,'before_sha256':sha(args.before),'after':args.after,'after_sha256':sha(args.after),'tool_sha256':sha(__file__),'frames':len(result),'pulsing_lancet_checks':pulse_checks,'details':result,'scope':'every native pixel outside migrated rail envelopes unchanged; return squares, meters, history, texts, progress and unrelated controls byte-exact'}
    (folder/'receipt.json').write_text(json.dumps(proof,indent=2)+'\n');print(folder/'receipt.json')
if __name__=='__main__':main()
