#!/usr/bin/env python3
"""Follow-up design study: heavier square retrigger pips, no production edits.

Compare the retained 2x2-pip native study with a new 4x4-pip native capture.
Even pip sizes preserve exact 8.5,8.5 centering inside an 18x18 native badge.
Outer centers move from +/-4 to +/-5 solely to keep one pixel of separation
between three diagonal 4px pips. Flow, open focus, text, grids and controllers
are unchanged from the preceding proposal. This is NOT an installed UI change.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw
import render_score_edit_marks_study as study

ROOT, CACHE = study.ROOT, study.CACHE
PREVIOUS = ROOT/'ui_mockups/score_edit_marks_study/20261005T141732_729257Z'


def replace_once(text, old, new):
    assert text.count(old) == 1, (old, text.count(old))
    return text.replace(old, new)


def heavier_source(previous):
    text = (previous/'native/study_sequencer.cpp').read_text()
    old = '{0, 0}, {-4, -4, 4, 4}, {-4, -4, 0, 0, 4, 4}, {-4, -4, 4, -4, -4, 4, 4, 4}'
    new = '{0, 0}, {-5, -5, 5, 5}, {-5, -5, 0, 0, 5, 5}, {-5, -5, 5, -5, -5, 5, 5, 5}'
    assert text.count(old) == 2
    text = text.replace(old, new)
    text = replace_once(text,
        'ui::rect(cx - 1 + xy[count - 1][i * 2], cy - 1 + xy[count - 1][i * 2 + 1], 2, 2, color);',
        'ui::rect(cx - 2 + xy[count - 1][i * 2], cy - 2 + xy[count - 1][i * 2 + 1], 4, 4, color);')
    text = replace_once(text,
        '// Even 18px cell => center 8.5px, so even 2px pips center EXACTLY.',
        '// Even 18px cell => center 8.5px; 4px squares retain that exact center.')
    text = replace_once(text,
        'ui::rect(x + (CELL_W-2)/2 + xy[count-1][i*2],\n                     y + (BADGE_H-2)/2 + xy[count-1][i*2+1], 2, 2, color);',
        'ui::rect(x + (CELL_W-4)/2 + xy[count-1][i*2],\n                     y + (BADGE_H-4)/2 + xy[count-1][i*2+1], 4, 4, color);')
    return text


def build_capture(out, previous, proof, source_builder=None):
    for name, expected in proof['native']['dependencies'].items():
        assert study.sha(name) == expected, ('source moved since prior study', name)
    for name, expected in proof['native']['cached_objects'].items():
        assert study.sha(name) == expected, ('cached object moved', name)
    assert study.sha(previous/'native/study_sequencer.cpp') == proof['native']['proposal_source_sha256']
    assert study.sha(previous/'native/driver.cpp') == proof['native']['driver_sha256']
    # Driver fixture is exactly the retained study; only its drawing TU changes.
    driver = previous/'native/driver.o'
    assert driver.stat().st_mtime_ns >= (previous/'native/driver.cpp').stat().st_mtime_ns
    driver_pin = study.sha(driver)
    folder = out/'native'
    folder.mkdir()
    source = folder/'study_sequencer.cpp'
    source.write_text((source_builder or heavier_source)(previous))
    receipt = json.loads((CACHE/'receipt.json').read_text())
    flags = receipt['commands'][0][1:receipt['commands'][0].index('-MMD')]
    flags += ['-I'+str(ROOT/'platform/3ds')]
    compiler = receipt['commands'][0][0]
    obj, binary = folder/'study_sequencer.o', folder/'proposal'
    study.run([compiler, *flags, '-c', str(source), '-o', str(obj)], folder/'drawing-build.log')
    objects = [command[command.index('-o')+1] for command in receipt['commands'][2:]]
    study.run([compiler, *flags, str(driver), str(obj), *objects, '-o', str(binary)], folder/'link.log')
    study.run([str(binary), str(out/'proposal')], folder/'capture.log')
    assert study.sha(driver) == driver_pin
    return dict(driver_object=str(driver), driver_object_sha256=driver_pin,
                proposal_source_sha256=study.sha(source), binary_sha256=study.sha(binary),
                previous_receipt_sha256=study.sha(previous/'proposal.json'),
                cached_production_dependencies=len(proof['native']['dependencies']))


def previous_image(previous, proof, theme, case, screen):
    path = previous/f'{theme}-{case}-dual-2x.png'
    saved = next(item for item in proof['outputs'] if ROOT/item['path'] == path)
    assert study.sha(path) == saved['sha256']
    with Image.open(path) as source:
        pair = source.convert('RGB').resize((400,496), Image.Resampling.NEAREST)
    return pair.crop((0,0,400,240) if screen == 'top' else (40,256,360,496))


def verify(out, previous, proof):
    results = []
    positions = {1: ((0,0),), 2: ((-5,-5),(5,5)), 3: ((-5,-5),(0,0),(5,5)),
                 4: ((-5,-5),(5,-5),(-5,5),(5,5))}
    for theme in ('graphite','pink'):
        for case in ('flow','dice','level'):
            for screen in ('top','bottom'):
                before = previous_image(previous,proof,theme,case,screen)
                after = study.load(out,'proposal',theme,case,screen)
                diff = ImageChops.difference(before,after).convert('L')
                bbox = diff.getbbox()
                if case != 'dice' or screen != 'bottom':
                    assert bbox is None, ('unrequested image change',theme,case,screen,bbox)
                else:
                    mask = Image.new('L',(320,240),0);draw = ImageDraw.Draw(mask)
                    for row in (45,83,121,159):
                        for col in range(16):
                            x = 8+col*19
                            draw.rectangle((x+2,row+2,x+15,row+15),fill=255)
                    for x in (100,160,220,280):
                        draw.rectangle((x-7,211,x+6,224),fill=255)
                    assert ImageChops.multiply(diff,ImageChops.invert(mask)).getbbox() is None
                    assert bbox is not None
                    ink = (244,244,244) if theme=='graphite' else (31,20,31)
                    for col, count in ((0,1),(3,2),(4,3),(7,4)):
                        cell = after.crop((8+19*col,45,26+19*col,63))
                        pixels = {(x,y) for y in range(18) for x in range(18) if cell.getpixel((x,y))==ink}
                        expected = {(7+dx+x,7+dy+y) for dx,dy in positions[count] for x in range(4) for y in range(4)}
                        assert pixels == expected, (theme,count)
                        assert len(pixels) == 16*count
                        for axis in (0,1):
                            assert sum(p[axis] for p in pixels)*2 == len(pixels)*17
                            assert min(p[axis] for p in pixels)+max(p[axis] for p in pixels)==17
                results.append(dict(theme=theme,case=case,screen=screen,difference_bbox=bbox))
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--previous',type=Path,default=PREVIOUS)
    ap.add_argument('--out',type=Path)
    args = ap.parse_args()
    previous = args.previous.resolve()
    proof = json.loads((previous/'proposal.json').read_text())
    before = study.protected_hashes()
    assert before == proof['protected_sha256'], 'production changed since the previous study'
    out = (args.out or ROOT/'ui_mockups/score_pip_weight_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')).resolve()
    out.mkdir(parents=True,exist_ok=False)
    (out/'renderer.py').write_bytes(Path(__file__).read_bytes())
    native = build_capture(out,previous,proof)
    checks = verify(out,previous,proof)
    outputs = []
    for theme in ('graphite','pink'):
        image = study.load(out,'proposal',theme,'dice','bottom')
        outputs.append(study.save(out,f'{theme}-retrig-2x.png',image))
        board = study.Canvas(688,326,study.OUTSIDE)
        board.text(16,12,'retrig / a little more weight',study.INK,scale=2)
        for x,title,picture in (
            (16,'before / 2x2',previous_image(previous,proof,theme,'dice','bottom')),
            (352,'thicker / 4x4',image)):
            board.text(x,42,title,study.ACCENT)
            board.im.paste(picture,(x,62))
        board.text(16,310,'only retrig glyphs changed / same open focus, flow and layout',study.MUTED)
        outputs.append(study.save(out,f'{theme}-comparison-2x.png',board.im))
    close = study.Canvas(464,220,study.OUTSIDE)
    close.text(16,12,'square pips / same exact center',study.INK)
    for row,(name,image) in enumerate((
        ('2x2',previous_image(previous,proof,'graphite','dice','bottom')),
        ('4x4',study.load(out,'proposal','graphite','dice','bottom')))):
        y=40+row*94
        for i,col in enumerate((0,3,4,7)):
            cell=image.crop((8+19*col,45,26+19*col,63))
            close.im.paste(cell.resize((72,72),Image.Resampling.NEAREST),(16+i*112,y))
        close.text(16,y-14,name,study.ACCENT)
    outputs.append(study.save(out,'square-pips-closeup.png',close.im))
    assert study.protected_hashes() == before
    report=dict(status='visual thickness experiment; not production or deployment',provenance=__doc__,
                created_at=datetime.now(timezone.utc).isoformat(),native=native,outputs=outputs,checks=checks,
                script_sha256=study.sha(__file__),protected_sha256=before,
                pips=dict(previous_size=2,new_size=4,previous_spread=4,new_spread=5,
                          measured_centroid=[8.5,8.5],measured_center_cases=8),
                preservation='all 6 upper screens and all flow/level surfaces byte-exact to the preceding proposal; dice changes confined to glyph interiors and selector glyphs',
                limitations=['static isolated drawing study, not an approved implementation',
                             'cached current production objects and prior driver reused; no broad suite rerun',
                             'no ARM build, device capture, FTP, audio export or application change'])
    (out/'proposal.json').write_text(json.dumps(report,indent=2)+'\n')
    print(out.relative_to(ROOT));print('5 images / exact centers / flow and open focus unchanged / production unchanged')
    print(out/'graphite-comparison-2x.png')


if __name__=='__main__':main()
