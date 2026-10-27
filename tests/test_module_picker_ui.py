#!/usr/bin/env python3
"""Focused approved-v9 picker proof: real picker/chains/workspace + extracted main.

Private copied host boundary, no inherited cases, no ARM/deployment. Sanitized
checks run before optional capture-only (current raw PPMs -> two NN2x PNGs;
seven additional graphite motion duals remain raw for independent review).
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources as common_sources, masked, functions
from test_mod_minimal_ui import extract_main

OUT = ROOT / 'release/module-picker'
PROOF = OUT / 'host-proof.json'
OWN = ('tests/test_module_picker_ui.py', 'tests/module_picker_ui_cases.inc',
       'tools/generate_picker_labels.py', 'tests/test_mod_minimal_ui.py',
       'tests/osc_input_dispatch_test.cpp', 'tests/sequencer_test.cpp')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def sources():
    result = common_sources()
    for name in OWN:
        result[name] = sha(ROOT / name)
    result['assets/fonts/Gossip-MedPixel.otf'] = sha(ROOT / 'assets/fonts/Gossip-MedPixel.otf')
    return dict(sorted(result.items()))


def replace_once(text, old, new=''):
    assert text.count(old) == 1, old
    return text.replace(old, new, 1)


def driver(directory):
    # Remove ONLY obsolete link-boundary stubs from private copies. Production
    # chains/workspace and picker dispatch are linked, not test replacements.
    seq = (ROOT / 'tests/sequencer_test.cpp').read_text()
    for line in (
        'void workspace_shift_slots_right(int,int,int) {++rec_workspace_shifts;}',
        'void chains_context_draw_top() {} // real Chains path is exercised by menu_stage1_test.cpp',
        'const WorkspacePoint& workspace_point(int, int) { static const WorkspacePoint point{}; return point; }',
        'int workspace_camera_x(int) { return 0; }',
        'int workspace_camera_y(int) { return 0; }',
    ):
        seq = replace_once(seq, line)
    seq = seq.replace('#include "../', '#include "' + str(ROOT) + '/')
    (directory / 'sequencer_test.cpp').write_text(seq)
    text = (ROOT / 'tests/osc_input_dispatch_test.cpp').read_text()
    text = replace_once(text, 'bool chains_picker_background() { return false; }')
    text = replace_once(text, 'void workspace_move_slot(int,int,int) { CHECK(false); }')
    for name, args, params in (
        ('picker_touch_down', 'int x,int y', 'x,y'),
        ('picker_touch_hold', 'int x,int y', 'x,y'),
        ('picker_touch_release', '', ''),
        ('chains_view_touch_down', 'int x,int y', 'x,y'),
        ('chains_view_touch_hold', 'int x,int y', 'x,y'),
        ('chains_view_touch_release', 'bool commit', 'commit'),
        ('picker_open', '', ''),
    ):
        pattern = r'^void ' + name + r'\([^\n]*\) \{[^\n]*\}$'
        found = re.findall(pattern, text, re.M)
        assert len(found) == 1, (name, found)
        text = replace_once(text, found[0], 'void %s(%s) { seance::platform::ui3ds::%s(%s); }' % (name, args, name, params))
    text = replace_once(text, 'int main(int argc,char** argv) {', 'int unused_original_picker_main(int argc,char** argv) {')
    text = text.replace('#include "../', '#include "' + str(ROOT) + '/')
    pre = ''.join('#include "' + str(ROOT / ('platform/3ds/' + name)) + '"\n' for name in ('ui_chains.h', 'ui_picker.h', 'picker_motion.h'))
    path = directory / 'driver.cpp'
    path.write_text(pre + text + '\n' + (ROOT / OWN[1]).read_text())
    return path


def run(command, log, timeout=300):
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with log.open('x') as stream:
        stream.write('$ ' + shlex.join(map(str, command)) + '\n'); stream.flush()
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    print(log.read_text()[-26000:], end='', flush=True)
    return result.returncode


def publish(proof, stamp):
    text = json.dumps(proof, indent=2) + '\n'
    PROOF.write_text(text)
    (OUT / ('receipt-' + stamp + '.json')).write_text(text)


def verify(proof):
    assert sources() == proof['source_sha256'], 'stale source inventory'
    assert sha(proof['binary']) == proof['binary_sha256'], 'stale binary'
    for name, digest in proof['compiler_dependencies'].items():
        assert sha(Path(name) if Path(name).is_absolute() else ROOT / name) == digest, name


def draw_guards():
    defs = functions((ROOT / 'platform/3ds/ui_picker.cpp').read_text())
    names = ('picker_draw_content', 'picker_draw', 'gothic', 'native', 'rectangle')
    out = {}
    for name in names:
        body = masked(defs[name][2])
        assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|malloc|calloc|realloc|new|delete|make_unit)\b', body), name
        out[name] = hashlib.sha256(body.encode()).hexdigest()
    return out


def main():
    global OUT, PROOF
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--capture-only', action='store_true')
    parser.add_argument('--capture-dir', type=Path)
    parser.add_argument('--output-dir', type=Path, default=OUT,
                        help='proof/build/check logs directory (default: release/module-picker); also used by --no-build and --capture-only')
    args = parser.parse_args()
    OUT = args.output_dir.resolve()
    PROOF = OUT / 'host-proof.json'
    OUT.mkdir(exist_ok=True, parents=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    before, guards = sources(), draw_guards()
    subprocess.run(['python3', str(ROOT / OWN[2]), '--check'], check=True)
    if args.no_build or args.capture_only:
        proof = json.loads(PROOF.read_text()); verify(proof)
        binary = Path(proof['binary'])
    else:
        directory = Path(tempfile.mkdtemp(prefix='seance-module-picker-'))
        path = driver(directory)
        extracted = extract_main(directory)
        source = (ROOT / 'platform/3ds/main.cpp').read_text()
        begin = source.index('        // START/Y on a local modal')
        end = source.index('        // Old-3DS global navigation', begin)
        stage = directory / 'main_picker_stage.inc'
        stage.write_text(source[begin:end])
        extracted[stage.name] = sha(stage)
        binary = directory / 'module_picker_ui_san'
        units = make_variable('OSC_INPUT_SOURCES')
        units[units.index('tests/osc_input_dispatch_test.cpp')] = str(path)
        for name in ('platform/3ds/ui_chains.cpp', 'platform/3ds/ui_workspace.cpp'):
            if name not in units: units.append(name)
        compiler = os.environ.get('CXX', 'g++')
        flags = ['-O0', '-g1', *make_variable('SEQUENCER_TEST_FLAGS'), '-DSEANCE_REAL_EDITOR',
                 '-I' + str(directory), '-Itests', '-I.', '-fsanitize=address,undefined',
                 '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', *WRAP,
                 '-Wl,--wrap=_ZN6seance9make_unitENS_8UnitKindE']
        command = [compiler, *flags, *units, '-o', str(binary)]
        proof = dict(built_at=stamp, binary=str(binary), source_sha256=before, build_command=command,
                     extracted_main_sha256=extracted, driver_sha256=sha(path), draw_guards=guards,
                     inherited_groups_run=0, host_only=True, deployment=False, checks={'status':'not run'})
        log = OUT / ('host-build-' + stamp + '.log')
        status = run(command, log); proof['build'] = dict(exit=status, log=str(log.relative_to(ROOT)) if log.is_relative_to(ROOT) else str(log))
        publish(proof, stamp)
        if status: raise SystemExit(status)
        assert sources() == before, 'inputs changed during compilation'
        result = subprocess.run([compiler, *[f for f in flags if not f.startswith('-Wl,')], '-MM', *units], cwd=ROOT, capture_output=True, text=True, check=True, timeout=120)
        (OUT / ('dependencies-' + stamp + '.txt')).write_text(result.stdout)
        found = set()
        for line in result.stdout.replace('\\\n',' ').splitlines():
            if ':' in line: found.update(shlex.split(line.split(':',1)[1]))
        deps = {}
        for name in found:
            path = (Path(name) if Path(name).is_absolute() else ROOT/name).resolve()
            deps[str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)] = sha(path)
        proof['compiler_dependencies'] = dict(sorted(deps.items())); proof['binary_sha256'] = sha(binary)
        verify(proof); publish(proof, stamp)
    if args.capture_only:
        assert proof['checks']['status'] == 'passed'
        destination = (args.capture_dir or ROOT / 'ui_mockups/module_picker_runtime' / ('capture-' + stamp)).resolve()
        assert not destination.exists(), 'preserve previous captures'
        log = OUT / ('capture-' + stamp + '.log')
        status = run([str(binary), '--capture-only', str(destination)], log)
        if status: raise SystemExit(status)
        from PIL import Image, PngImagePlugin
        surfaces = sorted(destination.rglob('*.ppm')); assert len(surfaces) == 18
        media = {}
        for theme in ('graphite', 'pink'):
            with Image.open(destination / (theme+'_top.ppm')) as src: top = src.convert('RGB')
            with Image.open(destination / (theme+'_bottom.ppm')) as src: bottom = src.convert('RGB')
            assert top.size == (400,240) and bottom.size == (320,240)
            dual = Image.new('RGB',(400,496),(18,18,21)); dual.paste(top,(0,0)); dual.paste(bottom,(40,256))
            large = dual.resize((800,992),Image.Resampling.NEAREST)
            path = destination / ('picker-'+theme+'-2x.png')
            meta = PngImagePlugin.PngInfo();meta.add_text('provenance','current C++ host raster, raw PPM pair; NN2x only; not hardware')
            large.save(path,pnginfo=meta)
            with Image.open(path) as decoded: assert decoded.convert('RGB').tobytes() == large.tobytes()
            media[path.name] = sha(path)
        proof['capture'] = dict(path=str(destination), ppm_sha256={str(p.relative_to(destination)):sha(p) for p in surfaces}, png_sha256=media, log=str(log.relative_to(ROOT)) if log.is_relative_to(ROOT) else str(log), inherited_groups_run=0)
    elif not args.build_only:
        log = OUT / ('check-' + stamp + '.log')
        status = run([str(binary), '--proof-only'], log)
        proof['checks'] = dict(status='passed' if not status else 'failed', exit=status, log=str(log.relative_to(ROOT)) if log.is_relative_to(ROOT) else str(log), sanitizer='ASAN/UBSAN/leaks')
        if not status: proof['checks']['summary'] = next(line for line in log.read_text().splitlines() if line.startswith('module picker:'))
        publish(proof, stamp)
        if status: raise SystemExit(status)
    verify(proof); publish(proof, stamp); print(PROOF)


if __name__ == '__main__':
    main()
