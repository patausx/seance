#!/usr/bin/env python3
"""Private native preview: small selection fill behind a flow-curve point.

Based explicitly on the user's accepted former event-selection preview, NOT the
latest experimental UI globally. A compact 18x10 chamfered fill follows the
actual selected flow value. Only curve ink under that fill is inverted, like
an event glyph. Top scope, layout, event selection, pips and controls unchanged.
No production edits, device build or upload; host diagonals approximate PICA.
"""
from datetime import datetime, timezone
import json
from pathlib import Path

from PIL import Image, ImageChops, PngImagePlugin
import render_score_selection_return as base

ROOT, CACHE, host = base.ROOT, base.CACHE, base.host
PREVIOUS = ROOT / 'ui_mockups/score_selection_return/20261005T180024_349155Z'

FLOW = '''    // PRIVATE PREVIEW: small fill follows the actual flow value, not row baseline.
    if (g_overlay == Overlay::None && g_view == View::Sequencer &&
        flow_chain(g_seq_chain) && g_seq_mode == SeqMode::Trig) {
        g_step_focus.reset();
        const int x = pattern_x(g_seq_col % PAGE_STEPS), y = PATTERN_ROWS[g_seq_chain];
        const int cy = y + 26 - value_percent(g_seq_chain, g_seq_col) * 23 / 100;
        g_focus_box = {x, cy - 5, CELL_W, 10};
        step_focus::fill(g_focus_box, seq_spot());
        // Original native curve, clipped to the fill; never a synthetic contour.
        g_focus_inverse = true;
        draw_flow_lane(g_seq_chain, first, y);
        g_focus_inverse = false;
    }
'''


def proposal_source():
    source = base.private_source()
    start = source.index('    // Open, immediate edit focus:')
    stop = source.index('    // One moving fill, then inverse-only foreground', start)
    return source[:start] + FLOW + source[stop:]


def driver_source():
    source = base.driver_source()
    split = source.index('namespace selection_return_study {')
    driver = source[split:].replace('selection_return_study', 'flow_selection_plate_study')
    anchor = '  f.mode(1);host_time_ms+=300;f.frame();capture(out,prefix+"note");'
    extra = r'''
  f.mode(0);
  for(int step:{0,3,5,6,7,11,15}) {
   f.touch(1,step);host_time_ms+=300;f.frame();
   capture(out,prefix+"flow_step_"+std::to_string(step));
  }
  for(int step:{0,6,15}) {
   f.touch(3,step);host_time_ms+=300;f.frame();
   capture(out,prefix+"flow_lastrow_"+std::to_string(step));
  }
  for(int value:{0,100}) {
   CHECK(f.engine->set_step_lock(1,8,0,OscUnit::P_GAIN,q15(value*32767/100)));
   f.touch(1,8);host_time_ms+=300;f.frame();
   capture(out,prefix+"flow_extreme_"+std::to_string(value));
  }
'''
    driver = base.replace_once(driver, anchor, anchor + extra)
    capture_end = '  dump_surface(out,name+(top?"_top":"_bottom"));\n }\n}'
    metadata = r'''
  const auto& snap=sequencer_snapshot();
  const int chain=sequencer_selected_chain(),step=sequencer_selected_step();
  const int value=std::min(32767,std::abs(int(snap.sequence_value[chain][step])));
  const int percent=(value*100+16383)/32767;
  std::ofstream state(out/(name+".txt"));
  state<<chain<<' '<<step<<' '<<sequencer_selected_layer()<<' '
       <<layout::X0+(step%16)*layout::PITCH<<' '
       <<layout::ROWS[chain]+26-percent*23/100<<' '
       <<m::spot()<<' '<<col_text_i()<<' '<<before<<'\n';
'''
    driver = base.replace_once(driver, capture_end,
        '  dump_surface(out,name+(top?"_top":"_bottom"));\n }\n' + metadata + '}')
    return source[:split] + driver


def load(out, version, name, screen):
    with Image.open(out / version / f'{name}_{screen}.ppm') as image:
        return image.convert('RGB')


def rgb(value):
    return tuple((value >> shift) & 255 for shift in (0,8,16))


def save(out, name, image, scale=2):
    image = image.resize((image.width*scale,image.height*scale),Image.Resampling.NEAREST)
    metadata = PngImagePlugin.PngInfo();metadata.add_text('provenance',__doc__)
    path = out/name;assert not path.exists()
    image.save(path,pnginfo=metadata)
    with Image.open(path) as decoded:assert decoded.tobytes()==image.tobytes()
    return dict(path=str(path.relative_to(ROOT)),sha256=host.sha(path))


def verify(out):
    records = []
    state_paths = sorted((out/'baseline').glob('*.txt'))
    for path in state_paths:
        name = path.stem
        assert path.read_bytes() == (out/'proposal'/path.name).read_bytes(), ('state changed', name)
        chain, step, layer, x, cy, accent, inverse, _ = map(int, path.read_text().split())
        flow = layer == 0 and chain in (1,3)
        for screen in ('top','bottom'):
            old, new = (load(out, version, name, screen) for version in ('baseline','proposal'))
            delta = ImageChops.difference(old,new)
            bbox = delta.getbbox()
            if not flow or screen == 'top':
                assert old.tobytes() == new.tobytes(), (name,screen,'unrelated pixels changed')
            else:
                # Union of old 13x13 corners and new 18x10 plate; no whole-row fill.
                delta.paste((0,0,0),(x,cy-6,x+18,cy+7))
                assert delta.getbbox() is None, (name,'outside selected point changed')
                region = new.crop((x,cy-5,x+18,cy+5))
                assert sum(p==rgb(accent) for p in region.getdata()) >= 100, (name,'fill missing')
                assert sum(p==rgb(inverse) for p in region.getdata()) >= 3, (name,'inverse curve missing')
                # The actual selected point is centered on the unchanged value.
                assert new.getpixel((x+8,cy)) == rgb(inverse), (name,'selected point not inverted')
                assert old.crop((0,0,320,40)).tobytes() == new.crop((0,0,320,40)).tobytes()
            records.append(dict(scene=name,screen=screen,changed_bbox=bbox,outside_changes=0))
    # Original ten dual scenes remain exactly the accepted previous preview.
    proof = json.loads((PREVIOUS/'preview.json').read_text())
    for output in proof['outputs']:
        p = ROOT/output['path'];assert host.sha(p)==output['sha256']
        if '-dual-' not in p.name:
            continue
        theme,case = p.name.split('-')[:2]
        with Image.open(p) as image:
            old = image.convert('RGB').resize((400,496),Image.Resampling.NEAREST)
        assert old.crop((0,0,400,240)).tobytes()==load(out,'baseline',theme+'_'+case,'top').tobytes()
        assert old.crop((40,256,360,496)).tobytes()==load(out,'baseline',theme+'_'+case,'bottom').tobytes()
    return records


def main():
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out = ROOT/'ui_mockups/flow_selection_plate_study'/stamp
    native = out/'native';native.mkdir(parents=True,exist_ok=False)
    (out/'renderer.py').write_bytes(Path(__file__).read_bytes())
    before = host.protected_hashes()
    previous = json.loads((PREVIOUS/'preview.json').read_text())
    assert before == previous['protected'], 'working source drift since accepted preview'
    assert host.sha(base.__file__) == previous['runner_sha256']
    assert host.sha(PREVIOUS/'native/selection_preview.cpp') == previous['private_source_sha256']
    baseline = base.private_source()
    assert baseline == (PREVIOUS/'native/selection_preview.cpp').read_text()
    receipt = json.loads((CACHE/'receipt.json').read_text())
    assert receipt['status']=='passed'
    assert all(host.sha(p)==h for p,h in receipt['dependencies'].items())
    commands=receipt['commands'];first=commands[0]
    compiler, flags=first[0],first[1:first.index('-MMD')]+['-I'+str(ROOT/'platform/3ds')]
    objects=[Path(c[c.index('-o')+1]) for c in commands]
    objects=objects[2:] # fresh driver, fresh accepted draw TU and fresh proposed draw TU
    pins={str(p):host.sha(p) for p in objects}
    sources={'driver':driver_source(),'baseline':baseline,'proposal':proposal_source()}
    for name,source in sources.items():
        path=native/(name+'.cpp');path.write_text(source)
        host.run([compiler,*flags,'-c',str(path),'-o',str(native/(name+'.o'))],native/(name+'-build.log'))
    for name in ('baseline','proposal'):
        binary=native/name
        host.run([compiler,*flags,str(native/'driver.o'),str(native/(name+'.o')),*map(str,objects),'-o',str(binary)],native/(name+'-link.log'))
        host.run([str(binary),str(out/name)],native/(name+'-capture.log'))
    checks=verify(out);outputs=[]
    for theme in ('graphite','pink'):
        name=theme+'_flow'
        top,bottom=(load(out,'proposal',name,s) for s in ('top','bottom'))
        outputs.append(save(out,f'{theme}-flow-bottom-2x.png',bottom))
        outputs.append(save(out,f'{theme}-flow-dual-2x.png',host.pair(top,bottom)))
        # Three enlarged, REAL raster excerpts show rise, level and fall.
        board=Image.new('RGB',(180*3,112),host.BG)
        for i,case in enumerate(('flow','flow_step_5','flow_step_7')):
            image=load(out,'proposal',theme+'_'+case,'bottom')
            state=list(map(int,(out/'proposal'/f'{theme}_{case}.txt').read_text().split()))
            x,cy=state[3:5]
            crop=image.crop((x-13,cy-12,x+31,cy+14)).resize((176,104),Image.Resampling.NEAREST)
            board.paste(crop,(i*180,4))
        # Already 4x native; save without another enlargement.
        outputs.append(save(out,f'{theme}-flow-closeup-4x.png',board,scale=1))
    assert before==host.protected_hashes()
    assert all(host.sha(p)==h for p,h in receipt['dependencies'].items())
    assert pins=={str(p):host.sha(p) for p in objects}
    proof=dict(status='visual proposal only; no production changes or deployment',
        basis='accepted former event selection preview; not a new global UI baseline',
        geometry=dict(width=18,height=10,anchor='selected flow value',corner='existing 2px chamfer'),
        ink='existing theme inverse: charcoal on graphite, light on pink, matching step selection',
        checks=checks,outputs=outputs,protected=before,cached_objects=pins,
        source_hashes={n:host.sha(native/(n+'.cpp')) for n in sources},
        runner_sha256=host.sha(__file__),base_sha256=host.sha(base.__file__),
        previous_preview_sha256=host.sha(PREVIOUS/'preview.json'),
        cache_receipt_sha256=host.sha(CACHE/'receipt.json'),dependencies=receipt['dependencies'],
        production_changes=[],hardware=False,deployed=False,inherited_suites_run=False,
        limitations=['static native host preview; diagonal PICA coverage not measured','no new hover input; existing step selection only'])
    (out/'proposal.json').write_text(json.dumps(proof,indent=2,sort_keys=True)+'\n')
    print(out);print(f'{len(checks)} raster pairs checked; upper, scope, event selection and pips unchanged')


if __name__=='__main__':main()
