#!/usr/bin/env python3
"""User chose 2x2 pips; compact 2/4 only, keep the original diagonal 3.

Isolated native drawing study only. All counts retain the same 2x2 squares;
1/3 are unchanged, 2/4 move one pixel inward from +/-4 to +/-3.
Exact 8.5,8.5 cell center is preserved. No production, layout, flow, focus,
controller, audio or installed-application changes.
"""
from datetime import datetime, timezone
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw
import render_score_pip_weight_study as weight

study = weight.study
ROOT, PREVIOUS = study.ROOT, weight.PREVIOUS
POSITIONS = {1: ((0,0),), 2: ((-3,-3),(3,3)),
             3: ((-4,-4),(0,0),(4,4)), 4: ((-3,-3),(3,-3),(-3,3),(3,3))}


def compact_source(previous):
    source = (previous/'native/study_sequencer.cpp').read_text()
    old = '{0, 0}, {-4, -4, 4, 4}, {-4, -4, 0, 0, 4, 4}, {-4, -4, 4, -4, -4, 4, 4, 4}'
    new = '{0, 0}, {-3, -3, 3, 3}, {-4, -4, 0, 0, 4, 4}, {-3, -3, 3, -3, -3, 3, 3, 3}'
    assert source.count(old) == 2
    return source.replace(old, new)


def verify(out, proof):
    records, centers = [], []
    for theme in ('graphite', 'pink'):
        for case in ('flow', 'dice', 'level'):
            for screen in ('top', 'bottom'):
                before = weight.previous_image(PREVIOUS,proof,theme,case,screen)
                after = study.load(out,'proposal',theme,case,screen)
                diff = ImageChops.difference(before,after)
                # Per-channel max avoids hiding a small color difference in luma.
                r,g,b = diff.split();mask_diff = ImageChops.lighter(ImageChops.lighter(r,g),b)
                if case != 'dice' or screen != 'bottom':
                    assert before.tobytes() == after.tobytes(), (theme,case,screen)
                else:
                    allowed = Image.new('L',(320,240),0);d = ImageDraw.Draw(allowed)
                    for y in (45,83,121,159):
                        for col in range(16):
                            x=8+col*19;d.rectangle((x+4,y+4,x+13,y+13),fill=255)
                    for cx in (100,160,220,280):
                        d.rectangle((cx-5,213,cx+4,222),fill=255)
                    assert ImageChops.multiply(mask_diff,ImageChops.invert(allowed)).getbbox() is None
                    ink = (244,244,244) if theme=='graphite' else (31,20,31)
                    ghost = (184,184,184) if theme=='graphite' else (126,100,119)
                    accent = (181,154,232) if theme=='graphite' else (91,49,124)
                    for count,col in ((1,0),(2,3),(3,4),(4,7)):
                        cell = after.crop((8+19*col,45,26+19*col,63))
                        actual = {(x,y) for y in range(18) for x in range(18) if cell.getpixel((x,y))==ink}
                        expected = {(8+dx+x,8+dy+y) for dx,dy in POSITIONS[count] for x in range(2) for y in range(2)}
                        assert actual == expected and len(actual)==4*count
                        if count in (1,3):
                            assert cell.tobytes()==before.crop((8+19*col,45,26+19*col,63)).tobytes()
                        for a in (0,1):
                            assert sum(p[a] for p in actual)*2==17*len(actual)
                            assert min(p[a] for p in actual)+max(p[a] for p in actual)==17
                        cx=100+(count-1)*60
                        selector=after.crop((cx-7,211,cx+8,226))
                        color=accent if count==4 else ghost
                        actual={(x,y) for y in range(15) for x in range(15) if selector.getpixel((x,y))==color}
                        expected={(6+dx+x,6+dy+y) for dx,dy in POSITIONS[count] for x in range(2) for y in range(2)}
                        assert actual==expected and len(actual)==4*count
                        if count in (1,3):
                            assert selector.tobytes()==before.crop((cx-7,211,cx+8,226)).tobytes()
                        for a in (0,1):
                            assert sum(p[a] for p in actual)*2==13*len(actual)
                        centers.append(dict(theme=theme,count=count,cell_center=[8.5,8.5],selector_center=[cx-.5,217.5]))
                records.append(dict(theme=theme,case=case,screen=screen,difference_bbox=mask_diff.getbbox()))
    return records,centers


def main():
    before=study.protected_hashes()
    proof=json.loads((PREVIOUS/'proposal.json').read_text())
    assert before==proof['protected_sha256']
    out=ROOT/'ui_mockups/score_pip_compact_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out.mkdir(parents=True,exist_ok=False)
    (out/'renderer.py').write_bytes(Path(__file__).read_bytes())
    native=weight.build_capture(out,PREVIOUS,proof,source_builder=compact_source)
    checks,centers=verify(out,proof)
    outputs=[]
    for theme in ('graphite','pink'):
        image=study.load(out,'proposal',theme,'dice','bottom')
        outputs.append(study.save(out,f'{theme}-compact-2x.png',image))
        board=study.Canvas(688,326,study.OUTSIDE)
        board.text(16,12,'retrig / compact 2 and 4',study.INK,scale=2)
        for x,title,picture in (
            (16,'before / wider spacing',weight.previous_image(PREVIOUS,proof,theme,'dice','bottom')),
            (352,'2/4 closer / 3 unchanged',image)):
            board.text(x,42,title,study.ACCENT);board.im.paste(picture,(x,62))
        board.text(16,310,'same flow, open focus and layout / only pip spacing changed',study.MUTED)
        outputs.append(study.save(out,f'{theme}-comparison-2x.png',board.im))
    close=study.Canvas(464,220,study.OUTSIDE)
    close.text(16,12,'2x2 / compact 2 and 4, original 3',study.INK)
    for row,(name,image) in enumerate((
        ('before',weight.previous_image(PREVIOUS,proof,'graphite','dice','bottom')),
        ('2 / 4 compact',study.load(out,'proposal','graphite','dice','bottom')))):
        y=40+row*94
        close.text(16,y-14,name,study.ACCENT)
        for i,col in enumerate((0,3,4,7)):
            cell=image.crop((8+19*col,45,26+19*col,63))
            close.im.paste(cell.resize((72,72),Image.Resampling.NEAREST),(16+i*112,y))
    outputs.append(study.save(out,'square-pips-compact-closeup.png',close.im))
    assert before==study.protected_hashes()
    report=dict(status='compact 2x2 visual proposal only; not implemented or deployed',provenance=__doc__,
                created_at=datetime.now(timezone.utc).isoformat(),native=native,outputs=outputs,checks=checks,
                centers=centers,protected_sha256=before,script_sha256=study.sha(__file__),
                helper_sha256=study.sha(weight.__file__),
                pips=dict(size=2,old_spread=4,new_spread_by_count={1:0,2:3,3:4,4:3},
                          old_multi_bbox=[10,10],new_two_four_bbox=[8,8],three_bbox=[10,10],
                          old_two_four_axis_gap=6,new_two_four_axis_gap=4,diagonal_three_axis_gap=2),
                preservation='10/12 complete rasters exact; all upper, flow and level screens unchanged; 1/3 cell and selector exact original; no difference outside retrigger glyphs',
                limitations=['static native host drawing study; not device acceptance',
                             'cached current native driver and production objects reused, no broad suites',
                             'no ARM build, FTP or application/controller/audio changes'])
    (out/'proposal.json').write_text(json.dumps(report,indent=2)+'\n')
    print(out.relative_to(ROOT));print('2x2 / compact 2 and 4 / original 3 / 16 pixel checks / all production unchanged')


if __name__=='__main__':main()
