#!/usr/bin/env python3
"""Preview the requested return of the former event-step selection plate.

Private drawing TU only; do not change production or deploy. Recover the actual
previous focus implementation from its pinned backup, retaining current square
pips and the complete current flow appearance. New designs are not a UI baseline.
"""
from datetime import datetime, timezone
import json
from pathlib import Path

from PIL import Image, ImageChops
import render_score_edit_marks_study as scenes
import render_arp_contour_study as host
import test_fader_lancet_ui as code

ROOT = host.ROOT
CACHE = host.CACHE
SOURCE = ROOT / 'platform/3ds/ui_sequencer.cpp'
HISTORY = ROOT / 'ui_mockups/score_edit_marks_study/20261005T141732_729257Z'


def replace_once(source, old, new):
    assert source.count(old) == 1, old[:120]
    return source.replace(old, new)


def private_source():
    pin = json.loads((ROOT / 'release/score-edit-marks/before.json').read_text())
    backup = ROOT / pin['source_backup']
    assert host.sha(backup) == pin['source_backup_sha256']
    previous, current = backup.read_text(), SOURCE.read_text()
    source = previous
    old_functions, functions = code.functions(previous), code.functions(current)
    # Keep the approved compact square geometry, but route the shared glyph
    # through the original inverse foreground sink for selection replay.
    glyph = functions['draw_retrig_dots'][2].replace('ui::rect(', 'score_ink::rect(')
    source = replace_once(source, old_functions['draw_retrig_dots'][2], glyph)
    start = source.index('    case SeqMode::Retrig: {', source.index('void draw_event_content('))
    end = source.index('    case SeqMode::When:', start)
    now_start = current.index('    case SeqMode::Retrig:', current.index('void draw_event_content('))
    now_end = current.index('    case SeqMode::When:', now_start)
    source = source[:start] + current[now_start:now_end] + source[end:]
    source = replace_once(source,
        'score_ink::point(x, yy, selected ? 3 : 1, selected ? seq_spot() : seq_ghost());',
        'score_ink::point(x, yy, 1, selected ? seq_spot() : seq_ghost());')
    # Flow was explicitly kept separate: preserve its small point and open focus.
    # Only regular event selection recovers the former plate and motion logic.
    a = current.index('    // Open, immediate edit focus:')
    b = current.index('    for(int local=0;local<PAGE_STEPS;++local)', a)
    flow_focus = current[a:b]
    flow_focus = replace_once(flow_focus,
        'if (g_overlay == Overlay::None && g_view == View::Sequencer) {',
        'if (g_overlay == Overlay::None && g_view == View::Sequencer &&\n'
        '        flow_chain(g_seq_chain) && g_seq_mode == SeqMode::Trig) {\n'
        '        g_step_focus.reset();')
    start = source.index('    // One moving fill, then inverse-only foreground')
    stop = source.index('    for(int local=0;local<PAGE_STEPS;++local)', start)
    plate = source[start:stop]
    plate = replace_once(plate,
        '    if (g_overlay == Overlay::None && g_view == View::Sequencer) {',
        '    else if (g_overlay == Overlay::None && g_view == View::Sequencer) {')
    source = source[:start] + flow_focus + plate + source[stop:]
    # Everything outside these known rendering fragments is the pinned old TU;
    # its non-rendering logic already equals current apart from focus resets.
    return source.replace('#include "../../', '#include "' + str(ROOT) + '/')


def driver_source():
    text = (CACHE / 'driver.cpp').read_text()
    marker = 'int main(int argc,char**argv)'
    assert text.count(marker) == 1
    driver = scenes.DRIVER.replace('score_mark_study', 'selection_return_study')
    anchor = '  f.mode(2);f.touch(1,4);host_time_ms+=300;f.frame();capture(out,prefix+"level");'
    driver = replace_once(driver, anchor, anchor + '\n'
        '  f.mode(0);f.touch(0,4);host_time_ms+=300;f.frame();capture(out,prefix+"trig");\n'
        '  f.mode(1);host_time_ms+=300;f.frame();capture(out,prefix+"note");')
    return text.replace(marker, 'int unused_score_capture_main(int argc,char**argv)') + '\n' + driver


def verify(out):
    checks = []
    cells = {'dice': (141, 45, 159, 63), 'level': (84, 83, 102, 101),
             'trig': (84, 45, 102, 63), 'note': (84, 45, 102, 63)}
    for theme in ('graphite', 'pink'):
        for case in ('flow', 'dice', 'level', 'trig', 'note'):
            for screen in ('top', 'bottom'):
                a, b = (scenes.load(out, version, theme, case, screen) for version in ('current', 'preview'))
                diff = ImageChops.difference(a, b)
                bbox = diff.getbbox()
                if screen == 'top' or case == 'flow':
                    assert a.tobytes() == b.tobytes(), (theme, case, screen)
                else:
                    assert bbox is not None, (theme, case, 'selection not shown')
                    diff.paste((0, 0, 0), cells[case])
                    assert diff.getbbox() is None, (theme, case, 'outside selection changed')
                if case == 'level' and screen == 'bottom':
                    with Image.open(HISTORY / 'current' / f'{theme}_level_bottom.ppm') as old:
                        assert old.convert('RGB').tobytes() == b.tobytes(), 'not the previous selection'
                checks.append(dict(theme=theme, case=case, screen=screen, changed_bbox=bbox, outside_selection=0))
            if case == 'dice':
                after = scenes.load(out, 'preview', theme, case, 'bottom')
                x, y = 141, 45
                # Original inverse ink is the selected palette color, not a
                # recolored pip size or position. All 16 actual square pixels.
                positions = {(5+dx, 5+dy) for dx in (0, 1, 6, 7) for dy in (0, 1, 6, 7)}
                colors = {after.getpixel((x+xx, y+yy)) for xx, yy in positions}
                assert len(colors) == 1, (theme, 'inconsistent selected pip color', colors)
                pip_color = next(iter(colors))
                actual = {(xx, yy) for yy in range(18) for xx in range(18)
                          if after.getpixel((x+xx, y+yy)) == pip_color}
                assert actual == positions, (theme, 'selected pip geometry changed', actual)
    return checks


def main():
    out = ROOT / 'ui_mockups/score_selection_return' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    native = out / 'native';native.mkdir(parents=True, exist_ok=False)
    (out / 'renderer.py').write_bytes(Path(__file__).read_bytes())
    before = host.protected_hashes()
    receipt = json.loads((CACHE / 'receipt.json').read_text())
    assert receipt['status'] == 'passed'
    for p, expected in receipt['dependencies'].items():
        assert host.sha(p) == expected, ('stale dependency', p)
    drawing, driver = native / 'selection_preview.cpp', native / 'driver.cpp'
    drawing.write_text(private_source());driver.write_text(driver_source())
    first = receipt['commands'][0]
    compiler, flags = first[0], first[1:first.index('-MMD')] + ['-I' + str(ROOT / 'platform/3ds')]
    objects = [Path(c[c.index('-o')+1]) for c in receipt['commands']]
    assert objects[1].name == '01_ui_sequencer.o'
    object_pins = {str(p): host.sha(p) for p in objects[1:]}
    driver_o, drawing_o = native / 'driver.o', native / 'selection_preview.o'
    host.run([compiler, *flags, '-c', str(driver), '-o', str(driver_o)], native / 'driver-build.log')
    host.run([compiler, *flags, '-c', str(drawing), '-o', str(drawing_o)], native / 'drawing-build.log')
    for version in ('current', 'preview'):
        links = [driver_o, drawing_o if version == 'preview' else objects[1], *objects[2:]]
        binary = native / version
        host.run([compiler, *flags, *map(str, links), '-o', str(binary)], native / (version + '-link.log'))
        host.run([str(binary), str(out / version)], native / (version + '-capture.log'))
    checks = verify(out)
    outputs = []
    for theme in ('graphite', 'pink'):
        for case in ('flow', 'dice', 'level', 'trig', 'note'):
            top, bottom = (scenes.load(out, 'preview', theme, case, screen) for screen in ('top', 'bottom'))
            outputs.append(host.save(out, f'{theme}-{case}-dual-2x.png', host.pair(top, bottom)))
            outputs.append(host.save(out, f'{theme}-{case}-bottom-2x.png', bottom))
    assert before == host.protected_hashes()
    assert object_pins == {str(p): host.sha(p) for p in objects[1:]}
    assert all(host.sha(p) == h for p, h in receipt['dependencies'].items())
    proof = dict(status='native preview only; no production rollback or deployment',
        selected_change='former event selection plate', flow='entire current flow appearance preserved',
        pips='current compact square pips preserved, selected glyph inverse restored',
        production_changes=[], hardware=False, deployed=False, inherited_suites_run=False,
        source_sha256=host.sha(SOURCE), private_source_sha256=host.sha(drawing),
        runner_sha256=host.sha(__file__), driver_sha256=host.sha(driver),
        cache_receipt_sha256=host.sha(CACHE/'receipt.json'), cached_objects=object_pins,
        dependencies=receipt['dependencies'], protected=before, checks=checks, outputs=outputs)
    (out / 'preview.json').write_text(json.dumps(proof, indent=2, sort_keys=True) + '\n')
    print(out)
    print('20 native rasters: all upper/flow exact; only event selection differs; level exact historical plate')


if __name__ == '__main__':
    main()
