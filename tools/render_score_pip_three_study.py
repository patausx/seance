#!/usr/bin/env python3
"""User-requested 3x3 square retrigger study; 4x4 was explicitly rejected.

New isolated native drawing TU, all production files untouched. Return to the
original +/-4 pip spacing; flow, open focus, layouts, text and controls remain
byte-exact to the liked 2x2 study outside retrigger glyphs. Odd 3px squares on
an even 18px badge have an unavoidable half-pixel centering quantization: use
the existing integer x+8/y+8 score anchor, NOT a false exact-8.5-center claim.
"""
from datetime import datetime, timezone
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw
import render_score_pip_weight_study as weight

study = weight.study
ROOT = study.ROOT
PREVIOUS = weight.PREVIOUS


def three_source(previous):
    text = (previous/'native/study_sequencer.cpp').read_text()
    text = weight.replace_once(text,
        'ui::rect(cx - 1 + xy[count - 1][i * 2], cy - 1 + xy[count - 1][i * 2 + 1], 2, 2, color);',
        'ui::rect(cx - 1 + xy[count - 1][i * 2], cy - 1 + xy[count - 1][i * 2 + 1], 3, 3, color);')
    text = weight.replace_once(text,
        '// Even 18px cell => center 8.5px, so even 2px pips center EXACTLY.',
        '// Crisp 3px square at native integer center 8; even badge center is 8.5.')
    text = weight.replace_once(text,
        'ui::rect(x + (CELL_W-2)/2 + xy[count-1][i*2],\n                     y + (BADGE_H-2)/2 + xy[count-1][i*2+1], 2, 2, color);',
        'ui::rect(x + (CELL_W-3)/2 + xy[count-1][i*2],\n                     y + (BADGE_H-3)/2 + xy[count-1][i*2+1], 3, 3, color);')
    return text


def verify(out, proof):
    records = []
    points = {1: ((0,0),), 2: ((-4,-4),(4,4)), 3: ((-4,-4),(0,0),(4,4)),
              4: ((-4,-4),(4,-4),(-4,4),(4,4))}
    for theme in ('graphite','pink'):
        for case in ('flow','dice','level'):
            for screen in ('top','bottom'):
                before = weight.previous_image(PREVIOUS,proof,theme,case,screen)
                after = study.load(out,'proposal',theme,case,screen)
                diff = ImageChops.difference(before,after).convert('L')
                if case != 'dice' or screen != 'bottom':
                    assert diff.getbbox() is None, ('unrequested change',theme,case,screen)
                else:
                    mask = Image.new('L',(320,240),0);d = ImageDraw.Draw(mask)
                    for y in (45,83,121,159):
                        for col in range(16):
                            x = 8+19*col;d.rectangle((x+3,y+3,x+13,y+13),fill=255)
                    for x in (100,160,220,280):
                        d.rectangle((x-5,213,x+5,223),fill=255)
                    assert ImageChops.multiply(diff,ImageChops.invert(mask)).getbbox() is None
                    ink = (244,244,244) if theme=='graphite' else (31,20,31)
                    # Inspect ALL four real cell counts and all four lower choices.
                    for count,col in ((1,0),(2,3),(3,4),(4,7)):
                        cell = after.crop((8+19*col,45,26+19*col,63))
                        actual = {(x,y) for y in range(18) for x in range(18) if cell.getpixel((x,y))==ink}
                        expected = {(7+dx+x,7+dy+y) for dx,dy in points[count] for x in range(3) for y in range(3)}
                        assert actual == expected and len(actual)==9*count
                        assert all(sum(p[a] for p in actual)==8*len(actual) for a in (0,1))
                        cx = 100+(count-1)*60
                        selector = after.crop((cx-7,211,cx+8,226))
                        accent = (181,154,232) if theme=='graphite' else (91,49,124)
                        ghost = (184,184,184) if theme=='graphite' else (126,100,119)
                        color = accent if count==4 else ghost
                        actual = {(x,y) for y in range(15) for x in range(15) if selector.getpixel((x,y))==color}
                        expected = {(6+dx+x,6+dy+y) for dx,dy in points[count] for x in range(3) for y in range(3)}
                        assert actual==expected and len(actual)==9*count
                records.append(dict(theme=theme,case=case,screen=screen,difference_bbox=diff.getbbox()))
    return records


def main():
    before = study.protected_hashes()
    proof = json.loads((PREVIOUS/'proposal.json').read_text())
    assert before == proof['protected_sha256']
    out = ROOT/'ui_mockups/score_pip_three_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out.mkdir(parents=True,exist_ok=False)
    (out/'renderer.py').write_bytes(Path(__file__).read_bytes())
    native = weight.build_capture(out,PREVIOUS,proof,source_builder=three_source)
    checks = verify(out,proof)
    outputs = []
    for theme in ('graphite','pink'):
        image = study.load(out,'proposal',theme,'dice','bottom')
        outputs.append(study.save(out,f'{theme}-retrig-3px-2x.png',image))
        board = study.Canvas(688,326,study.OUTSIDE)
        board.text(16,12,'retrig / 3 pixel squares',study.INK,scale=2)
        for x,title,picture in (
            (16,'previous / 2x2',weight.previous_image(PREVIOUS,proof,theme,'dice','bottom')),
            (352,'new / 3x3',image)):
            board.text(x,42,title,study.ACCENT);board.im.paste(picture,(x,62))
        board.text(16,310,'same flow, open focus and layout / no production changes',study.MUTED)
        outputs.append(study.save(out,f'{theme}-comparison-2x.png',board.im))
    close = study.Canvas(464,126,study.OUTSIDE)
    close.text(16,12,'3x3 / square pips',study.INK)
    image = study.load(out,'proposal','graphite','dice','bottom')
    for i,col in enumerate((0,3,4,7)):
        cell=image.crop((8+19*col,45,26+19*col,63))
        close.im.paste(cell.resize((72,72),Image.Resampling.NEAREST),(16+i*112,38))
    outputs.append(study.save(out,'square-pips-3px-closeup.png',close.im))
    assert before == study.protected_hashes()
    report=dict(status='3px visual proposal; not implemented or deployed',provenance=__doc__,native=native,
                created_at=datetime.now(timezone.utc).isoformat(),script_sha256=study.sha(__file__),
                helper_sha256=study.sha(weight.__file__),outputs=outputs,checks=checks,
                protected_sha256=before,pips=dict(size=3,spread=4,cell_center=[8,8],badge_center=[8.5,8.5],
                    cell_alignment='floor to native score anchor; unavoidable -0.5px each axis, no antialiasing',
                    selector_center='existing integer centers',verified_cell_and_selector_counts=16),
                preservation='all upper, flow and level surfaces exact; only retrigger pip raster changes',
                limitations=['isolated draw-only C++ proposal','no controller, ARM, deployment or device tests',
                             'cached current native driver and production objects reused, no broad suites'])
    (out/'proposal.json').write_text(json.dumps(report,indent=2)+'\n')
    print(out.relative_to(ROOT));print('3x3 native pixels; 10/12 surfaces exact; only retrigger pips changed')


if __name__=='__main__':main()
