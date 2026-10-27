#!/usr/bin/env python3
"""REV center-only visual proposal; not runtime/hardware or measured reverb response.

Uses param_0 native surfaces emitted BEFORE the historical dump_rev stereo-buffer
failure. Does not run that unsafe dump path. No shipping source/build/upload.
"""
from pathlib import Path
import csv
import hashlib
import json
from PIL import Image, ImageDraw, ImageChops
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/rev_visual_study'
CURRENT = OUT / 'current'
BOX = (0, 58, 320, 184)


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def guard():
    receipt = json.loads((ROOT / 'release/strain-clear/verification.json').read_text())
    actual = {p: sha(ROOT / p) for p in receipt['sources']}
    assert actual == receipt['sources'], 'shipping sources differ from deployed receipt'
    artifact = ROOT / receipt['artifact']['path']
    assert sha(artifact) == receipt['artifact']['sha256']
    actual[str(artifact.relative_to(ROOT))] = sha(artifact)
    return actual


def loop_ms(raw):
    scale = 16384 + raw
    return tuple(((a + ((b * scale) >> 16)) * 1000 + 16000) // 32000
                 for a, b in ((733,1499), (997,1901), (1201,2309)))


def draw_center():
    c = Page()
    # Actual unmodulated param_0 fixture: size=4000, remaining ctor defaults.
    # Heights are a nominal feedback-only illustration, NOT summed/filtered PCM.
    times = loop_ms(4000)
    fb = (6000 + ((20000 * 24000) >> 15)) / 32768
    c.text(14, 62, 'three returns', SPOT)
    c.text(306, 62, 'schematic', GHOST, right=True)
    x0, x1, duration = 76, 302, 360
    c.text(14, 80, 'dry', GHOST)
    c.line((x0, 84, x1, 84), BRANCH)
    c.line((x0, 78, x0, 88), INK)
    c.line((x1-3, 81, x1, 84, x1-3, 87), GHOST)
    for i, (ms, y) in enumerate(zip(times, (109, 133, 157))):
        c.text(14, y-4, f'{ms}ms', GHOST)
        c.line((x0, y, x1, y), BRANCH)
        prev = None
        for n in range(1, duration // ms + 1):
            x = x0 + round(ms*n*(x1-x0)/duration)
            h = max(1, round(17 * fb**(n-1)))
            color = SPOT if n <= 3 else GHOST
            if prev:
                # Fine upper thread connects symbolic return markers, not samples.
                c.line((prev[0], prev[1], x, y-h), BRANCH)
            c.line((x, y-h, x, y), color)
            c.line((x-1, y-h, x+1, y-h), color)
            prev = (x, y-h)
        c.line((x0-4, y-3, x0-1, y, x0-4, y+3), BRANCH)
    for ms in (0, 120, 240, 360):
        x = x0 + round(ms*(x1-x0)/duration)
        c.line((x, 165, x, 168), BRANCH)
        c.text(x if ms != 360 else x-29, 172,
               str(ms) if ms != 360 else '360ms', GHOST)
    return c, times, fb


def save(im, name):
    im.save(OUT / f'{name}_native.png')
    im.resize((im.width*2, im.height*2), Image.Resampling.NEAREST).save(OUT / f'{name}_2x.png')


def main():
    before = guard()
    top = Image.open(CURRENT / 'param_0_top.ppm').convert('RGB')
    old = Image.open(CURRENT / 'param_0_bottom.ppm').convert('RGB')
    assert top.size == (400,240) and old.size == (320,240)
    labels = list(csv.DictReader((CURRENT/'param_0_bottom_text.tsv').open(), delimiter='\t'))
    assert {'37-60ms','63.0%','27%'}.issubset({r['text'] for r in labels})
    center, times, fb = draw_center()
    assert times == (37,50,60)
    new = old.copy()
    new.paste(center.im.crop(BOX), BOX[:2])
    for box in ((0,0,320,58),(0,184,320,240)):
        assert not ImageChops.difference(old.crop(box),new.crop(box)).getbbox()
    for box, name in center.labels:
        assert BOX[1] <= box[1] < box[3] <= BOX[3], name
    allowed = set(paper(320).get_flattened_data()) | {INK,SPOT,GHOST,BRANCH}
    assert set(new.get_flattened_data()) <= allowed
    save(pair(top,new), 'proposal_dual')
    save(new, 'proposal_bottom')
    board = Canvas(664,272,OUTSIDE)
    for x, label, im in ((8,'current / native c++',old),(336,'proposal / center only',new)):
        board.text(x+6,8,label,BRANCH)
        board.im.paste(im,(x,26))
    save(board.im,'before_after')
    assert before == guard()
    report = {
        'status':'authored center-only proposal, awaiting visual approval',
        'runtime_implemented':False, 'dsp_changed':False, 'uploaded':False,
        'composition':'upper + lower outside y58..183 are untouched native C++ pixels',
        'native_capture':'param_0 files completed before dump_rev Engine::render failure; no complete dump pass claim',
        'capture_failure':'historical host dump uses 512 output samples for 512 stereo frames (needs1024); gdb points to Engine::render; no shipping bug inferred, not fixed in this preview',
        'diagram':'three nominal independent loop-return lanes; fixed0..360ms, marker height from nominal feedback only, no amplitude axis',
        'limits':'not actual summed/coupled/damped reverb IR, PCM, RT60 or live tail; damp/mix not modeled; no private DSP proposed by this sketch',
        'fixture':{'size':4000,'nominal_loop_ms':times,'nominal_feedback':fb},
        'preserved':'all controller zones/gestures/freeze/bypass/locks/mod-temp, top inspector, four rails/header/footer; center remains view-only',
        'followup':'if approved: bounded scalar schematic; freeze should indicate held/transition rather than fabricate a reset tail; retain existing energy/freeze telemetry without PCM reads',
        'protected':before,
        'files':{p.name:sha(p) for p in sorted(OUT.glob('*.png'))},
        'source_sha256':sha(Path(__file__)),
        'checks':['source+artifact identity','native bands pixel-identical','central labels in bounds/nonoverlap','four menu inks+paper','nominal loop mapping']
    }
    (OUT/'concept.json').write_text(json.dumps(report,indent=2)+'\n')
    print('preview checks passed; no runtime changes')
    print(OUT/'before_after_2x.png')


if __name__ == '__main__':
    main()
