#!/usr/bin/env python3
"""One focused lancet native-UI sanitizer build; no old suites or implicit captures.

Default: preservation guards, one distinct host build, only --fader-lancet-only.
--no-build reuses an exactly source/hash-matched binary. --capture-only additionally
requires its focused proof to have passed and never replays tests. No make target,
ARM build, network, deployment, WAV export, image painting or PNG packaging here.
"""
import argparse
from datetime import datetime, timezone
import fnmatch
import glob
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path('/tmp/seance_fader_lancet_ui_san')
PROOF = BINARY.with_suffix('.sources.json')
OUT = ROOT / 'ui_mockups/fader_lancet_runtime'
BASE = ROOT / '.local-state/backups/20261001_115243_before-lancet-faders_3ds'
PROTECTED = ROOT / '.local-state/backups/lancet-fader-protected-20261001.json'
DRIVER_BASE = ROOT / '.local-state/backups/20261001_120545_before-lancet-ui-harness_osc_input_dispatch_test.cpp'
WRAP = ['-Wl,--wrap=_Z17C2D_DrawRectSolidfffffj',
        '-Wl,--wrap=_ZN6seance8platform5ui3ds20sample_slot_for_unitEPKNS_4UnitE']
DRAW_HELPERS = {
    'rail', 'clean_rail', 'primary', 'field', 'draw_control', 'clean_lower',
    'draw_bottom_frame', 'draw_top_frame', 'draw_tether', 'osc_inspector', 'motor_inspector',
    'marker', 'hollow', 'tint', 'shade',
}
LEX = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|[uUL])?"(?:\\.|[^"\\])*"|(?:[uUL])?\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|(?:0[xX][0-9A-Fa-f]+|\d+)(?:[uUlLfF]+)?|::|->|<<|>>|<=|>=|==|!=|&&|\|\||\+\+|--|\+=|-=|\|=|&=|\*=|/=|[^\s]', re.S)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tokens(source):
    return [m.group() for m in LEX.finditer(source)
            if not m.group().startswith(('//', '/*'))]


def masked(source):
    """Keep offsets/newlines; hide comments and string/character contents."""
    return re.sub(r'//[^\n]*|/\*.*?\*/|(?:u8|[uUL])?"(?:\\.|[^"\\])*"|(?:[uUL])?\'(?:\\.|[^\'\\])*\'',
                  lambda m: ''.join('\n' if c == '\n' else ' ' for c in m.group()), source, flags=re.S)


def close_brace(text, at):
    depth = 1
    for i in range(at + 1, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if not depth:
                return i + 1
    raise AssertionError('unbalanced source brace')


def functions(source):
    """Extract definitions, including compact same-line helpers, not control blocks."""
    text = masked(source)
    pattern = re.compile(r'\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?\{')
    result = {}
    through = -1
    for match in pattern.finditer(text):
        name = match.group(1)
        if match.start() < through or name in {'if', 'for', 'while', 'switch', 'catch'}:
            continue
        brace = match.end() - 1
        end = close_brace(text, brace)
        start = max(text.rfind(';', 0, match.start()), text.rfind('}', 0, match.start()),
                    text.rfind('{', 0, match.start())) + 1
        # A function's return type is part of the preserved signature.
        if name in result:
            raise AssertionError(f'ambiguous source definition: {name}')
        result[name] = (start, end, source[start:end])
        through = end
    return result


def strip_spans(source, spans):
    for start, end in sorted(spans, reverse=True):
        source = source[:start] + source[end:]
    return source


def frame_span(source):
    text = masked(source)
    match = re.search(r'\bstruct\s+Frame(?:\s*:\s*\w+)?\s*\{', text)
    return None if match is None else (match.start(), close_brace(text, match.end() - 1))


def is_subsequence(old, new):
    it = iter(new)
    return all(any(candidate == value for candidate in it) for value in old)


def normalize_capture(source, filename):
    # These additions are frozen display copies ONLY. Removing exactly these
    # statements must recover every old capture token (including DSP/model work).
    source = re.sub(r'frame\.mod_links\s*=\s*focus_mod_links\([^;]+\)\s*;', '', source)
    if filename in {'ui_module_focus_ghost.cpp', 'ui_module_focus_stranular.cpp'}:
        source = re.sub(r'frame\.mute\s*=\s*g_eng->chains\[g_edit_chain\]\.mute\s*;', '', source)
    if filename == 'ui_module_focus_delay.cpp':
        source = re.sub(r'frame\.effective\[p\]\s*=\s*u\.params\[p\]\.effective\s*;', '', source)
    if filename == 'ui_module_focus_play.cpp':
        source = re.sub(r'frame\.live\[p\]\s*=\s*u\.params\[p\]\.effective\s*;', '', source)
    if filename == 'ui_param_lens.cpp':
        source = re.sub(r'drawn_quiet\s*=\s*u->bypass\s*\|\|\s*g_eng->chains\[chain\]\.mute\s*;', '', source)
    # Adding a second scalar copy can brace a previously single-statement loop.
    source = re.sub(r'(for\s*\([^;{}]*;[^;{}]*;[^){}]*\))\s*\{\s*([^;{}]*;)\s*\}', r'\1\2', source)
    return source


def draw_name(name):
    return name in DRAW_HELPERS or '_draw' in name or name.startswith('draw_')


def preservation():
    assert BASE.is_dir() and PROTECTED.is_file() and DRIVER_BASE.is_file(), 'working-tree baseline missing'
    protected = json.loads(PROTECTED.read_text())
    assert len(protected) == 129, 'unexpected protected manifest inventory'
    for name, digest in protected.items():
        assert (ROOT / name).is_file() and sha(ROOT / name) == digest, f'protected source changed: {name}'
    result = {'protected_files': len(protected), 'protected_manifest_sha256': sha(PROTECTED),
              'baseline': str(BASE.relative_to(ROOT)), 'unchanged_files': {},
              'preserved_functions': {}, 'capture_copies': [], 'drawing_functions': [],
              'legend_only_functions': []}
    for before in sorted(BASE.rglob('*')):
        if not before.is_file():
            continue
        relative = before.relative_to(BASE)
        current = ROOT / 'platform/3ds' / relative
        assert current.is_file(), f'baseline platform file removed: {relative}'
        if before.read_bytes() == current.read_bytes():
            result['unchanged_files'][str(relative)] = sha(current)
            continue
        filename = relative.name
        allowed = filename in {'module_inspector.cpp', 'ui_param_lens.cpp', 'ui_module_focus_internal.h'} or (
            filename.startswith('ui_module_focus_') and filename.endswith('.cpp') and filename != 'ui_module_focus_common.cpp')
        assert allowed, f'non-module platform source changed: {relative}'
        old, new = before.read_text(), current.read_text()
        old_functions, new_functions = functions(old), functions(new)
        assert old_functions or filename == 'ui_module_focus_internal.h', f'no preservation functions extracted: {filename}'
        for name, (_, _, body) in old_functions.items():
            qualified = f'{filename}:{name}'
            if draw_name(name):
                result['drawing_functions'].append(qualified)
                if name in new_functions:
                    changed = new_functions[name][2]
                    if tokens(body) != tokens(changed):
                        assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|malloc|calloc|realloc|new|delete)\b', masked(changed)), f'live/heap access in changed draw: {qualified}'
                continue
            assert name in new_functions, f'non-draw function removed: {qualified}'
            other = new_functions[name][2]
            if name.endswith('_capture'):
                assert tokens(normalize_capture(body, filename)) == tokens(normalize_capture(other, filename)), f'non-display capture changed: {qualified}'
                if tokens(body) != tokens(other):
                    assert 'AudioOut::Guard' in other, f'new capture outside existing audio guard: {qualified}'
                    result['capture_copies'].append(qualified)
            else:
                if qualified == 'ui_module_focus_comp.cpp:modulation_status':
                    # Only the old marker-placement legend changes. Every branch,
                    # source/mute/lock condition and numeric expression stays exact.
                    old_legend, new_legend = '"stored / live bead below"', '"outline edit / fill live"'
                    assert body.count(old_legend) == 1 and other.count(new_legend) == 1
                    other = other.replace(new_legend, old_legend, 1)
                    result['legend_only_functions'].append(qualified)
                assert tokens(body) == tokens(other), f'controller/mapping/model function changed: {qualified}'
            result['preserved_functions'][qualified] = hashlib.sha256(' '.join(tokens(body)).encode()).hexdigest()
        for name in new_functions.keys() - old_functions.keys():
            assert draw_name(name) or (filename == 'ui_module_focus_internal.h' and name == 'focus_mod_links'), f'unexpected new function: {filename}:{name}'
        old_spans = [(a, b) for a, b, _ in old_functions.values()]
        new_spans = [(a, b) for a, b, _ in new_functions.values()]
        old_frame, new_frame = frame_span(old), frame_span(new)
        if old_frame:
            assert new_frame, f'private Frame removed: {filename}'
            assert is_subsequence(tokens(old[slice(*old_frame)]), tokens(new[slice(*new_frame)])), f'old frozen data changed, not just additions: {filename}'
            old_spans.append(old_frame);new_spans.append(new_frame)
        # Outer declarations preserve hit rectangles, Control/Gesture structs,
        # parameter ordering, axes, extents, neutral laws and input state exactly.
        a, b = strip_spans(old, old_spans), strip_spans(new, new_spans)
        b = re.sub(r'^\s*#include\s+"module_fader.h"\s*$', '', b, flags=re.M)
        if filename == 'ui_param_lens.cpp':
            b = re.sub(r'bool\s+drawn_quiet\s*=\s*false\s*;', '', b)
        # Frame size guards may grow for a tiny scalar copy, never input structs.
        for variable in ('a', 'b'):
            value = a if variable == 'a' else b
            value = re.sub(r'static_assert\s*\(sizeof\(Frame\).*?;', '', value, flags=re.S)
            if variable == 'a':a = value
            else:b = value
        assert tokens(a) == tokens(b), f'non-draw declarations/hit geometry changed: {filename}'
    mouth = (ROOT / 'platform/3ds/ui_module_focus_mouth.cpp').read_text()
    assert '"solid edit / bead live"' not in mouth and '"outline edit / fill live"' in mouth
    renderer = (ROOT / 'platform/3ds/module_fader.h').read_text()
    assert not re.search(r'\b(?:g_eng|g_audio|osGetTime|Guard|malloc|calloc|realloc|new|delete)\b', masked(renderer)), 'renderer must be engine/clock/heap free'
    assert 'paper_rect' not in masked(renderer), 'approved hollow marker must not erase underlying paper'
    helper = functions((ROOT / 'platform/3ds/ui_module_focus_internal.h').read_text())['focus_mod_links'][2]
    assert not re.search(r'\b(?:new|delete|osGetTime)\b', masked(helper))
    assert 'mod_enabled' in helper and 'ModSource::None' in helper
    driver = (ROOT / 'tests/osc_input_dispatch_test.cpp').read_text()
    driver = driver.replace('#include "fader_lancet_ui_test.h"\n', '', 1)
    driver = driver.replace('    if(argc==2 && std::string(argv[1])=="--fader-lancet-only") return fader_lancet_cases::run();\n', '', 1)
    driver = driver.replace('    if(argc==3 && std::string(argv[1])=="--capture-fader-lancet") return fader_lancet_cases::capture(argv[2]);\n', '', 1)
    assert driver == DRIVER_BASE.read_text(), 'existing driver changed beyond one include/two CLI branches'
    result['driver_baseline_sha256'] = sha(DRIVER_BASE)
    return result


def make_variable(name):
    """Expand only the three make constructs used by current host dependency lists."""
    text = (ROOT / 'tests/Makefile').read_text().replace('\\\n', ' ')
    values = dict(re.findall(r'^(\w+)\s*:=\s*(.*)$', text, re.M))
    def expand(value, depth=0):
        assert depth < 20, 'recursive make variable'
        while '$(' in value:
            def replace(match):
                expression = match.group(1)
                if expression.startswith('wildcard '):
                    return ' '.join(sorted(glob.glob(expression[9:], root_dir=ROOT)))
                if expression.startswith('filter-out '):
                    patterns, words = expression[11:].split(',', 1)
                    return ' '.join(word for word in shlex.split(words) if not any(
                        fnmatch.fnmatchcase(word, pattern.replace('%', '*')) for pattern in patterns.split()))
                assert expression in values, f'unsupported make expansion: {expression}'
                return expand(values[expression], depth + 1)
            changed = re.sub(r'\$\(([^()]*)\)', replace, value)
            assert changed != value, f'unexpanded make expression: {value}'
            value = changed
        return value
    return shlex.split(expand(values[name]))


def sources():
    paths = [p for parent in ('core', 'platform/3ds') for p in (ROOT / parent).rglob('*')
             if p.is_file() and p.suffix in {'.h', '.cpp', '.inc'}]
    paths += list((ROOT / 'tests').glob('*.h'))
    paths += [p for name in ('tests/sequencer_test_stubs', 'tests/menu_stage1_test_stubs')
              for p in (ROOT / name).rglob('*') if p.is_file()]
    paths += [ROOT / name for name in ('Makefile', 'tests/sequencer_test.cpp',
              'tests/osc_input_dispatch_test.cpp', 'tests/extract_osc_input.py',
              'tests/test_fader_lancet_ui.py')]
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted(set(paths))}


def run_logged(command, log, timeout):
    print(shlex.join([str(x) for x in command]), flush=True)
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with log.open('w') as stream:
        stream.write('$ ' + shlex.join([str(x) for x in command]) + '\n');stream.flush()
        result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    content = log.read_text()
    print(content[-20000:], end='', flush=True)
    if result.returncode:
        raise RuntimeError(f'command failed ({result.returncode}); full log: {log}')
    return content


def write_proof(proof):
    text = json.dumps(proof, indent=2, sort_keys=True) + '\n'
    PROOF.write_text(text)
    (OUT / 'sources.json').write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--guards-only', action='store_true')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--capture-only', action='store_true', help='requires prior focused pass; no tests or build')
    parser.add_argument('--capture-dir', type=Path, default=OUT / 'capture')
    args = parser.parse_args()
    assert not (args.capture_only and args.build_only), 'capture-only and build-only are exclusive'
    guard = preservation()
    print(f'preservation: {guard["protected_files"]} protected files; {len(guard["preserved_functions"])} non-draw functions; input/mapping declarations exact', flush=True)
    if args.guards_only:
        print(json.dumps(guard, indent=2, sort_keys=True));return
    OUT.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    before = sources()
    if args.no_build or args.capture_only:
        proof = json.loads(PROOF.read_text())
        assert proof['source_sha256'] == before and proof['binary_sha256'] == sha(BINARY), 'stale binary/source proof; explicitly rebuild'
    else:
        extract_log = OUT / f'extract-{stamp}.log'
        run_logged([sys.executable, 'tests/extract_osc_input.py'], extract_log, 30)
        command = [os.environ.get('CXX', 'g++'), '-O1', '-g1', *make_variable('SEQUENCER_TEST_FLAGS'),
                   '-DSEANCE_REAL_EDITOR', '-I/tmp/seance_osc_input_native', '-fsanitize=address,undefined',
                   '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', *WRAP,
                   *make_variable('OSC_INPUT_SOURCES'), '-o', str(BINARY)]
        build_log = OUT / f'build-{stamp}.log'
        run_logged(command, build_log, 300)
        assert sources() == before, 'sources changed during build; binary not certified'
        proof = {'built_at': stamp, 'source_sha256': before, 'binary_sha256': sha(BINARY),
                 'preservation': guard, 'build_command': command, 'build_log': str(build_log.relative_to(ROOT)),
                 'checks': {'status': 'not run'}, 'host_only': True, 'deployment': False}
        write_proof(proof)
    if args.build_only:
        print(PROOF);return
    if args.capture_only:
        assert proof['checks']['status'] == 'passed', 'focused groups must pass before a fresh capture'
        destination = args.capture_dir.resolve()
        assert not destination.exists(), 'preserve existing capture; choose a fresh capture-dir explicitly'
        log = OUT / f'capture-{stamp}.log'
        content = run_logged([str(BINARY), '--capture-fader-lancet', str(destination)], log, 180)
        assert '0 regression groups' in content
        from PIL import Image
        surfaces = sorted(destination.glob('*.ppm'))
        assert len(surfaces) >= 56 and len(surfaces) % 2 == 0
        for path in surfaces:
            with Image.open(path) as image:
                image.load()
                assert image.size == (400 if path.name.endswith('_top.ppm') else 320, 240)
        assert sources() == before and sha(BINARY) == proof['binary_sha256'], 'sources/binary moved during capture'
        receipt = json.loads((destination / 'capture.json').read_text())
        receipt.update(captured_at=stamp, source_sha256=before, binary_sha256=sha(BINARY),
                       surface_sha256={p.name: sha(p) for p in surfaces}, regression_groups_run=0)
        (destination / 'capture.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n')
        proof['capture'] = {'path': str(destination), 'log': str(log.relative_to(ROOT)), 'surfaces': len(surfaces)}
    else:
        log = OUT / f'check-{stamp}.log'
        proof['checks'] = {'status': 'pending', 'log': str(log.relative_to(ROOT)), 'groups': 4}
        write_proof(proof)
        try:
            content = run_logged([str(BINARY), '--fader-lancet-only'], log, 240)
            assert 'lancet UI: 4/4 groups;' in content, 'missing focused group completion'
            assert sources() == before and sha(BINARY) == proof['binary_sha256'], 'sources/binary moved during proof'
        except Exception:
            proof['checks']['status'] = 'failed';write_proof(proof);raise
        proof['checks'].update(status='passed', sanitizer='ASAN/UBSAN/leaks', completed_at=stamp,
                               summary=next(line for line in content.splitlines() if line.startswith('lancet UI:')))
    write_proof(proof)
    print(PROOF)


if __name__ == '__main__':
    main()
