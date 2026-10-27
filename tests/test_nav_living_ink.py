#!/usr/bin/env python3
"""Fresh focused GO TO popup/ink tests. No generic suite, ARM build or deployment.

Every attempt retains individual compile logs, all compiler dependency hashes,
input snapshots, raster digest trace and a success/failure receipt. The pixel
oracle uses real public drawing/controller APIs, never a private replacement TU.
"""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    paths = []
    for folder in ('platform/3ds', 'core', 'assets'):
        paths += [p for p in (ROOT/folder).rglob('*') if p.is_file() and p.suffix in
                  ('.h', '.hpp', '.c', '.cpp', '.png', '.ttf', '.wav', '.bin')]
    paths += [ROOT/p for p in ('Makefile', 'tests/nav_popup_test.cpp',
                               'tests/nav_living_ink_test.h', 'tests/test_nav_living_ink.py')]
    for name in ('seance.3dsx', 'seance.elf', 'seance.smdh'):
        if (ROOT/name).exists():
            paths.append(ROOT/name)
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted(set(paths))}


def execute(command, log, cwd=ROOT, timeout=300, env=None):
    with log.open('x') as stream:
        stream.write('$ '+shlex.join(map(str, command))+'\n');stream.flush()
        result = subprocess.run(list(map(str, command)), cwd=cwd, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=timeout, env=env)
    if result.returncode:
        raise RuntimeError(f'{log.name}: exit {result.returncode}\n{log.read_text()[-6000:]}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--compare', type=Path, help='prior optimized/sanitized checks.tsv, exact raster transcript')
    args = parser.parse_args()
    mode = 'sanitized' if args.sanitize else 'optimized'
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out = ROOT/'release/go-to-living-ink'/f'focused-{mode}-{stamp}'
    (out/'objects').mkdir(parents=True, exist_ok=False)
    (out/'logs').mkdir()
    before = inputs()
    (out/'before.json').write_text(json.dumps(before, indent=2)+'\n')
    receipt = {'kind': 'fresh focused host production GO TO/living ink; not device acceptance',
               'mode': mode, 'status': 'failed', 'argv': vars(args).copy(),
               'started': stamp, 'protected_inputs': before, 'fresh_objects': 0}
    receipt['argv']['compare'] = str(args.compare) if args.compare else None
    started = time.monotonic()
    try:
        # Preserve the old popup runner's integration/fence assertions.
        source = (ROOT/'platform/3ds/main.cpp').read_text()
        offsets = []; position = 0
        for token in ('        nav_capture();',
                      '        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) continue;',
                      '        nav_gpu_frame();', '        sigils_gpu_frame();',
                      '        if (g_nav_ink.frame.active)', '        nav_remember_outputs();',
                      '        C3D_FrameEnd(0);'):
            position = source.index(token, position)+len(token);offsets.append(position)
        assert offsets == sorted(offsets)
        nav = (ROOT/'platform/3ds/ui_nav.cpp').read_text()
        assert 'nav_ink::draw_bottom(' not in source
        for forbidden in ('draw_nav_preview', 'capture_sigil_wave', 'snapshot.scope', 'g_eng->scope'):
            assert forbidden not in nav
        sources = [ROOT/'tests/nav_popup_test.cpp']
        sources += [ROOT/('platform/3ds/'+p+'.cpp') for p in (
            'ui_nav', 'ui_picker', 'ui_chains', 'ui_workspace', 'menu_style',
            'project_names', 'ui_chains_top', 'ui_theme', 'ui_transition')]
        sources += [ROOT/'core/ui/draw.cpp', ROOT/'core/ui/font.cpp', *sorted((ROOT/'core/engine').glob('*.cpp'))]
        sources += [ROOT/('core/'+p) for p in ('synth/sampler.cpp', 'synth/wav_loader.cpp', 'synth/wav_writer.cpp')]
        cxx = os.environ.get('CXX', 'g++')
        flags = ['-std=gnu++17', '-O1' if args.sanitize else '-O2', '-g1', '-Wall', '-Wextra',
                 '-ffunction-sections', '-fdata-sections']
        sanitizers = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie'] if args.sanitize else []
        includes = ['-I'+str(ROOT/p) for p in ('tests/menu_stage1_test_stubs', 'tests/sequencer_test_stubs', 'core')]
        def compile_one(pair):
            number, path = pair
            obj = out/'objects'/f'{number:02d}-{path.stem}.o'
            execute([cxx, *flags, *sanitizers, *includes, '-MMD', '-MF', obj.with_suffix('.d'),
                     '-c', path, '-o', obj], out/'logs'/f'build-{number:02d}.log')
            return obj
        with ThreadPoolExecutor(max_workers=max(1, min(8, args.jobs))) as pool:
            objects = list(pool.map(compile_one, enumerate(sources)))
        receipt['fresh_objects'] = len(objects)
        dependencies = set()
        for dep in (out/'objects').glob('*.d'):
            text = dep.read_text().replace('\\\n', ' ')
            for token in shlex.split(text.split(':', 1)[1]):
                p = Path(token).resolve()
                if p.is_relative_to(ROOT):dependencies.add(p)
        deps = {str(p.relative_to(ROOT)): sha(p) for p in sorted(dependencies)}
        assert all(sha(ROOT/p) == digest for p, digest in before.items()), 'input changed while building'
        receipt['dependencies'] = deps
        for path in deps:
            saved = out/'inputs'/path;saved.parent.mkdir(parents=True, exist_ok=True);shutil.copy2(ROOT/path, saved)
        execute([cxx, *sanitizers, *objects, '-Wl,--gc-sections',
                 '-Wl,--wrap=_ZN6seance2ui4textEiiPKcji', '-o', out/'test'], out/'logs/link.log')
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        execute([out/'test', out/'checks.tsv'], out/'logs/run.log', cwd=out, timeout=300, env=env)
        runlog = (out/'logs/run.log').read_text()
        summary = re.search(r'^living-ink summary: (.+)$', runlog, re.M)
        assert summary, 'missing focused summary'
        receipt['summary'] = summary.group(1)
        receipt['pass_lines'] = [line for line in runlog.splitlines() if line.startswith('pass ')]
        if args.compare:
            previous = args.compare.resolve()
            assert previous.read_bytes() == (out/'checks.tsv').read_bytes(), 'optimized/sanitized raster transcript differs'
            receipt['exact_transcript_match'] = {'path': str(previous), 'sha256': sha(previous)}
        execute(['make', 'test-module-frame'], out/'logs/quick-frame.log')
        execute(['python3', 'tests/test_nav_instant.py'], out/'logs/quick-nav.log')
        assert inputs() == before, 'protected inputs/artifacts changed during focused run'
        assert all(sha(ROOT/p) == digest for p, digest in deps.items()), 'dependency changed during focused run'
        receipt['status'] = 'passed'
    except Exception as exc:
        receipt['error'] = str(exc)
    finally:
        receipt['seconds'] = round(time.monotonic()-started, 3)
        receipt['limits'] = ('host raster + ASan/UBSan when selected; shared authored shade, independent bitmap/color/alpha oracle; '
                             'two-quad command count is an estimate, not GPU/FPS; no allocation instrumentation, generic fullsuite, ARM, FTP or listening')
        receipt['evidence'] = {str(p.relative_to(out)): sha(p) for p in sorted(out.rglob('*')) if p.is_file()}
        (out/'verification.json').write_text(json.dumps(receipt, indent=2)+'\n')
        print(out)
        print(json.dumps({k: receipt[k] for k in ('status', 'seconds', 'fresh_objects', 'summary', 'error') if k in receipt}))
    if receipt['status'] != 'passed':raise SystemExit(1)


if __name__ == '__main__':
    main()
