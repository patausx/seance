#!/usr/bin/env python3
"""Focused ORDER input + frozen dual-screen proof; exact main touch dispatcher.

Private existing host harness, real engine/platform controllers. No inherited
suites unless explicitly listed, no deployment, no old receipt overwritten.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources, functions, masked
from test_mod_minimal_ui import extract_main


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def lossless_indexed(frames):
    """Exact native RGB lookup; Pillow quantize's RGB cache rounds nearby pinks."""
    from PIL import Image
    def pixels(image):
        return image.get_flattened_data() if hasattr(image, 'get_flattened_data') else image.getdata()
    colors = sorted({rgb for frame in frames for rgb in pixels(frame)})
    assert len(colors) <= 256
    palette = sum((list(c) for c in colors), []) + [0] * (768 - 3 * len(colors))
    lookup = {rgb: i for i, rgb in enumerate(colors)}
    indexed = []
    for frame in frames:
        image = Image.frombytes('P', frame.size, bytes(lookup[rgb] for rgb in pixels(frame)))
        image.putpalette(palette)
        assert image.convert('RGB').tobytes() == frame.tobytes(), 'GIF palette must preserve native pixels exactly'
        indexed.append(image)
    return indexed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--output-dir', type=Path, default=Path('release/perform-order-visual'),
                        help='fresh proof destination; historical perform-order receipts are never overwritten')
    args = parser.parse_args()
    out = args.output_dir if args.output_dir.is_absolute() else ROOT / args.output_dir
    out = out.resolve()
    assert out.is_relative_to(ROOT), 'output-dir must be inside this workspace'
    out.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    directory = Path(tempfile.mkdtemp(prefix='seance-order-ui-'))
    order_source = (ROOT / 'platform/3ds/ui_order.cpp').read_text()
    definitions = functions(order_source)
    # Follow all private local drawing callees, not just a hand-picked list.
    # Input/tick may use the wall clock; the entire drawing graph must not.
    drawing = {}
    def inspect(name):
        if name in drawing:
            return
        body = masked(definitions[name][2])
        assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|new|delete|malloc|calloc|realloc|execute)\b', body), name
        drawing[name] = hashlib.sha256(body.encode()).hexdigest()
        for callee in definitions:
            if re.search(r'\b'+re.escape(callee)+r'\s*\(', body):
                inspect(callee)
    inspect('draw')
    inspect('draw_top')
    assert not re.search(r'\bui\s*::\s*frame\s*\(|\brgb\s*\(|\bvisual_graphite\s*\(', masked(order_source)), 'order must use flat theme roles'
    motion_source = masked((ROOT / 'platform/3ds/order_motion.h').read_text())
    assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|new|delete|malloc|calloc|realloc)\b', motion_source), 'motion helper must be pure passed-ms math'
    before = sources()
    for name in ('tools/test_perform_order_ui.py','tools/perform_order_ui_cases.inc',
                 'tools/order_visual_ui_cases.inc','tools/test_mod_minimal_ui.py'):
        before[name] = sha(ROOT / name)
    # Keep all inherited helpers available for the existing raster/audio boundary,
    # but run only the named focused groups in our own main.
    text = (ROOT / 'tools/osc_input_dispatch_test.cpp').read_text()
    text = text.replace('int main(int argc,char** argv) {','int unused_original_order_main(int argc,char** argv) {',1)
    assert 'int unused_original_order_main' in text
    text = text.replace('#include "sequencer_test.cpp"','#include "'+str(ROOT / 'tools/sequencer_test.cpp')+'"')
    text = text.replace('#include "../','#include "'+str(ROOT)+'/')
    driver = directory / 'driver.cpp'
    driver.write_text('#include "'+str(ROOT / 'platform/3ds/ui_order.h')+'"\n#include "'+str(ROOT / 'platform/3ds/order_motion.h')+'"\n#include "'+str(ROOT / 'platform/3ds/inspector_gothic.h')+'"\n#include "'+str(ROOT / 'platform/3ds/ui_palette.h')+'"\n'+text+'\n'+(ROOT / 'tools/perform_order_ui_cases.inc').read_text())
    extracted = extract_main(directory)
    units = make_variable('OSC_INPUT_SOURCES')
    units[units.index('tools/osc_input_dispatch_test.cpp')] = str(driver)
    binary = directory / 'order_ui'
    flags = ['-O1','-g1',*make_variable('SEQUENCER_TEST_FLAGS'),'-DSEANCE_REAL_EDITOR','-I'+str(directory),'-Itools','-I.',*WRAP]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    command = [os.environ.get('CXX','g++'),*flags,*units,'-o',str(binary)]
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    receipt = dict(stamp=stamp, sanitized=args.sanitize, source_sha256=before, extracted_main_sha256=extracted,
                   command=command, binary=str(binary), host_only=True, deployment=False,
                   drawing_functions_sha256=drawing, clock='explicit host_time_ms; no sleeps or host scheduling',
                   scope='14 focused groups: six existing order regressions, six native visual/input groups, Point math and stale-main-DTO add regression; inherited mixer/transition boundaries',
                   limitations=['host native CPU raster, not PICA hardware timing/audio/FPS',
                                'intentional animated card occlusion is not a text-bounding-box collision'])
    def run(cmd, name):
        log = out / (name+'-'+stamp+'.log')
        with log.open('x') as stream:
            result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=300)
        print(log.read_text()[-18000:], flush=True)
        if result.returncode:
            raise RuntimeError(str(log))
        return str(log.relative_to(ROOT))
    receipt['build_log'] = run(command,'ui-build')
    receipt['binary_sha256'] = sha(binary)
    test_command = [str(binary)]
    if args.capture:
        capture = out / ('capture-'+stamp)
        test_command += [str(capture)]
        receipt['capture'] = str(capture.relative_to(ROOT))
    receipt['test_log'] = run(test_command,'ui-check')
    if args.capture:
        from PIL import Image, ImageDraw, PngImagePlugin
        metadata = PngImagePlugin.PngInfo()
        metadata.add_text('provenance', 'actual native order UI host raster; explicit virtual ms; not mockup or hardware FPS')
        ppms = sorted(capture.rglob('*.ppm'))
        assert len(ppms) >= 150 and len(ppms) % 2 == 0
        for ppm in ppms:
            with Image.open(ppm) as image:
                image.load()
                assert image.size == (400 if ppm.stem == 'top' else 320, 240), ppm
                image.save(ppm.with_suffix('.png'), pnginfo=metadata)
        for theme in ('pink', 'graphite', 'grimoire', 'black-mass', 'custom'):
            root = capture / theme
            for state in sorted(p for p in root.iterdir() if p.is_dir()):
                with Image.open(state / 'top.ppm') as top, Image.open(state / 'bottom.ppm') as bottom:
                    dual = Image.new('RGB', (400, 496), (18,18,18))
                    dual.paste(top, (0,0)); dual.paste(bottom, (40,256))
                    dual.save(state / 'dual.png', pnginfo=metadata)
            names = ('idle', 'selected', 'chooser', 'chooser-pressed', 'drag-before', 'drag-middle',
                     'drag-preview', 'drop-start', 'drop-middle', 'drop-settled', 'cancel-before', 'cancel-return')
            strip = Image.new('RGB', (4*328, 3*262), (18,18,18))
            paint = ImageDraw.Draw(strip)
            for i, name in enumerate(names):
                x,y=(i%4)*328,(i//4)*262
                paint.text((x+4,y+2),name,fill=(244,244,244))
                with Image.open(root / name / 'bottom.ppm') as frame:
                    strip.paste(frame, (x+4,y+18))
            strip.save(root / 'native-states.png', pnginfo=metadata)
            frames, frame_times = [], []
            timing = {parts[0]: int(parts[1]) for parts in
                      (line.split('\t') for line in (root / 'timings.tsv').read_text().splitlines()[1:])}
            for state in sorted(root.glob('motion-*')):
                with Image.open(state / 'bottom.ppm') as frame:
                    frames.append(frame.resize((640,480), Image.Resampling.NEAREST))
                    frame_times.append(timing[state.name])
            durations = [max(20, b-a) for a,b in zip(frame_times,frame_times[1:])] + [200]
            # Explicit exact RGB lookup, never Pillow's lossy RGB cache,
            # adaptive quantization, interpolation, or dithering.
            indexed = lossless_indexed(frames)
            indexed[0].save(root / 'native-motion.gif',save_all=True,append_images=indexed[1:],
                            duration=durations,loop=0,disposal=2,optimize=False)
        inventory = {str(p.relative_to(capture)): sha(p) for p in sorted(capture.rglob('*')) if p.is_file()}
        manifest = capture / 'capture.json'
        manifest.write_text(json.dumps(dict(provenance='actual native dual-screen host raster', themes=5,
            native_surfaces=len(ppms), frozen_duals=len(ppms)//2, virtual_clock=True,
            hardware_fps_claim=False, files_sha256=inventory),indent=2)+'\n')
        receipt['capture_manifest_sha256'] = sha(manifest)
        receipt['native_duals'] = len(ppms)//2
    # New files must also be inventoried by source helper or explicit ownership.
    assert all(sha(ROOT / name)==digest for name,digest in before.items())
    receipt['status'] = 'passed'
    path = out / ('ui-proof-'+stamp+'.json')
    path.write_text(json.dumps(receipt,indent=2)+'\n')
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
