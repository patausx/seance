#!/usr/bin/env python3
"""Focused Ghost visual/controller proof using the actual current editor/main.

Build a private sanitized driver with the original main renamed and only our
cases appended. Existing harnesses provide fixtures/raster, never test groups.
--capture-only checks the successful source/binary receipt and does zero builds
or regression groups. No ARM, network, audio export or authored/reused UI pixels.
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

from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources as runtime_sources
from test_fader_lancet_ui import functions, masked

OUT = ROOT / 'ui_mockups/ghost_refresh_runtime'
PROOF = OUT / 'sources.json'
OWN = ('tests/test_ghost_refresh_ui.py', 'tests/ghost_refresh_ui_cases.inc')
GHOST = 'platform/3ds/ui_module_focus_ghost.cpp'
HELPER = 'platform/3ds/ghost_memory_visual.h'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sources():
    result = runtime_sources()
    for name in OWN:
        result[name] = sha(ROOT / name)
    return dict(sorted(result.items()))


def run(command, log, timeout):
    print(shlex.join(map(str, command)), flush=True)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with log.open('x') as stream:
        stream.write('$ ' + shlex.join(map(str, command)) + '\n')
        stream.flush()
        result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    text = log.read_text()
    print(text[-18000:], end='', flush=True)
    if result.returncode:
        raise RuntimeError(f'command failed ({result.returncode}); full log: {log}')
    return text


def extract_main(directory):
    """Exact current main slices; no ownership/capture/input substitutions."""
    source = (ROOT / 'platform/3ds/main.cpp').read_text()
    result = {}
    for name, expression in (
        ('main_touch.inc', r'^static void handle_touch\(u32 down, u32 held\) \{.*?^}'),
    ):
        match = re.search(expression, source, re.M | re.S)
        assert match, f'main extraction missing: {name}'
        result[name] = match.group() + '\n'
    for name, start, end in (
        ('main_lens_keys.inc', '        // Held MOD owns entry/release', '        const bool discard_modal_had_frame'),
        ('main_lens_nav.inc', '        // Old-3DS global navigation', '        // START is the sequencer transport everywhere.'),
        ('main_lens_touch.inc', '        if (!stage_skip_touch && !param_skip_touch', '\n\n        if (g_exit_requested)'),
        ('main_trigger.inc', '        if (!stage_modal_frame && !nav_is_open() && (down & KEY_A)', '\n\n        if (g_view == View::Sequencer'),
    ):
        begin = source.index(start)
        result[name] = source[begin:source.index(end, begin)] + '\n'
    for name, text in result.items():
        (directory / name).write_text('// exact main.cpp slice; focused Ghost refresh proof\n' + text)
    return {name: sha(directory / name) for name in result}


def source_guards():
    baseline = json.loads((OUT / 'baseline.json').read_text())['source_hashes']
    unchanged = {}
    for name, digest in baseline.items():
        if name == GHOST:
            continue
        assert sha(ROOT / name) == digest, f'outside Ghost production scope changed: {name}'
        unchanged[name] = digest
    text = (ROOT / GHOST).read_text()
    code = masked(text)
    for retired in ('routing_open', 'draw_tether', 'tether_geometry', 'set_source', 'set_depth', 'GhostModDepth'):
        assert not re.search(r'\b' + retired + r'\b', code), f'old routing survives: {retired}'
    definitions = functions(text)
    checked = {}
    for name, (_, _, body) in definitions.items():
        if name.startswith('draw_') or '_draw' in name or name in ('hero_value', 'value_text', 'right', 'underline', 'memory_traces'):
            body = masked(body)
            assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|malloc|calloc|realloc|new|delete|C3D_TexInit|C2D_SpriteSheetLoad)\b', body), f'live/heap access in {name}'
            checked[name] = hashlib.sha256(body.encode()).hexdigest()
    capture = masked(definitions['ghost_focus_capture'][2])
    assert re.search(r'}\s*ghost_memory_visual::project', capture), 'geometry projection must follow the audio guard scope'
    assert 'AudioOut::Guard' in capture
    helper = masked((ROOT / HELPER).read_text())
    assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|Guard|malloc|calloc|realloc|new|delete|C3D_TexInit|C2D_SpriteSheetLoad)\b', helper), 'visual helper is not pure'
    assert HELPER in sources(), 'new helper missing from inventory'
    return dict(unchanged_production_files=len(unchanged), baseline_sha256=sha(OUT / 'baseline.json'),
                draw_functions=checked, helper_sha256=sha(ROOT / HELPER), retired_routing_absent=True)


def publish(proof, stamp):
    text = json.dumps(proof, indent=2, sort_keys=True) + '\n'
    PROOF.write_text(text)
    (OUT / f'receipt-{stamp}.json').write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--capture-only', action='store_true')
    parser.add_argument('--capture-dir', type=Path)
    args = parser.parse_args()
    assert not (args.capture_only and args.build_only)
    OUT.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    before = sources()
    guards = source_guards()
    if args.no_build or args.capture_only:
        proof = json.loads(PROOF.read_text())
        binary = Path(proof['binary'])
        assert proof['source_sha256'] == before, 'stale source inventory; explicit rebuild required'
        assert sha(binary) == proof['binary_sha256'], 'binary changed since proof'
    else:
        directory = Path(tempfile.mkdtemp(prefix='seance-ghost-refresh-'))
        original = (ROOT / 'tests/osc_input_dispatch_test.cpp').read_text()
        entry = 'int main(int argc,char** argv) {'
        assert original.count(entry) == 1
        driver = directory / 'driver.cpp'
        driver.write_text(original.replace(entry, 'int unused_original_ghost_driver_main(int argc,char** argv) {', 1)
                          + '\n' + (ROOT / OWN[1]).read_text())
        extracted = extract_main(directory)
        binary = directory / 'seance_ghost_refresh_ui_san'
        units = make_variable('OSC_INPUT_SOURCES')
        assert units.count('tests/osc_input_dispatch_test.cpp') == 1
        units[units.index('tests/osc_input_dispatch_test.cpp')] = str(driver)
        command = [os.environ.get('CXX', 'g++'), '-O1', '-g1',
                   *make_variable('SEQUENCER_TEST_FLAGS'), '-DSEANCE_REAL_EDITOR',
                   '-Itests', '-I' + str(directory), '-fsanitize=address,undefined',
                   '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', *WRAP, *units,
                   '-o', str(binary)]
        log = OUT / f'build-{stamp}.log'
        run(command, log, 300)
        assert sources() == before, 'sources changed during build; binary not certified'
        proof = dict(built_at=stamp, binary=str(binary), binary_sha256=sha(binary),
                     source_sha256=before, temporary_driver_sha256=sha(driver),
                     extracted_main_sha256=extracted, source_guards=guards,
                     build_command=command, build_log=str(log.relative_to(ROOT)),
                     checks={'status': 'not run'}, host_only=True, deployment=False,
                     inherited_groups_run=0)
        publish(proof, stamp)
    if args.build_only:
        print(PROOF)
        return
    if args.capture_only:
        assert proof['checks']['status'] == 'passed', 'focused proof must pass before capture'
        destination = (args.capture_dir or OUT / ('capture-' + stamp)).resolve()
        assert not destination.exists(), 'capture destination exists; preserve old artifacts'
        log = OUT / f'capture-{stamp}.log'
        text = run([str(binary), '--capture-only', str(destination)], log, 180)
        assert '0 regression groups' in text
        from PIL import Image
        surfaces = sorted(destination.glob('*.ppm'))
        assert len(surfaces) == 16
        for path in surfaces:
            with Image.open(path) as image:
                image.load()
                assert image.size == (400 if path.name.endswith('_top.ppm') else 320, 240)
        receipt = json.loads((destination / 'capture.json').read_text())
        receipt.update(captured_at=stamp, source_sha256=before,
                       binary_sha256=sha(binary), binary=str(binary),
                       surface_sha256={p.name: sha(p) for p in surfaces},
                       native_surfaces=len(surfaces),
                       manifest_sha256=sha(destination / 'manifest.tsv'),
                       regression_groups_run=0, capture_log=str(log.relative_to(ROOT)))
        (destination / 'capture.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n')
        proof['capture'] = dict(path=str(destination), surfaces=len(surfaces), log=str(log.relative_to(ROOT)))
    else:
        log = OUT / f'check-{stamp}.log'
        proof['checks'] = dict(status='pending', groups=5, log=str(log.relative_to(ROOT)))
        publish(proof, stamp)
        try:
            text = run([str(binary), '--proof-only'], log, 240)
            assert 'ghost refresh UI: 5/5 groups;' in text
        except Exception:
            proof['checks']['status'] = 'failed'
            publish(proof, stamp)
            raise
        proof['checks'].update(status='passed', sanitizer='ASAN/UBSAN/leaks', completed_at=stamp,
                               summary=next(line for line in text.splitlines() if line.startswith('ghost refresh UI:')))
    assert sources() == before and sha(binary) == proof['binary_sha256'], 'sources/binary changed during proof or capture'
    publish(proof, stamp)
    print(PROOF)


if __name__ == '__main__':
    main()
