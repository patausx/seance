#!/usr/bin/env python3
"""Fresh scoped startup ink proof, actual-runtime capture and independent GIF packaging.

O1 intentionally avoids an existing O2 browser snprintf format-truncation diagnostic;
-Wall -Wextra -Werror remain enabled for every compiled translation unit. No production
file, historical receipt or deployment path is written. All media starts as actual
public-API C++ software raster, using the existing static host paper fallback.
"""
from __future__ import annotations
from datetime import datetime, timezone
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'release/menu-text-ink'
SOURCES = ['tests/start_menu_cabinet_test.cpp', 'platform/3ds/ui_start_menu.cpp',
           'platform/3ds/ui_theme.cpp', 'platform/3ds/menu_style.cpp',
           'core/ui/draw.cpp', 'core/ui/font.cpp']
BASE_FLAGS = ['-std=gnu++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
              '-Itests/sequencer_test_stubs', '-Wl,--wrap=_ZN6seance2ui4textEiiPKcji']
SAN_FLAGS = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
THEMES = ('graphite', 'pink')
BASELINE = ROOT / 'ui_mockups/start_menu_text_motion_study/v1/proposal.json'
BASELINE_SHA = 'd5189bbd1dd07b89b0bb431b928470aa03a06eb9b57c0ecf883d75791dba46ec'
PINNED_GEOMETRY = {
    'platform/3ds/start_menu_labels_data.h': '72edd0cd80be7a910b51a6efec75d1951793b1a83c01b4d4ad1b4d1bf4ff5b90',
    'platform/3ds/cabinet_font_data.h': 'e0092c6260170b00ff934d8ef0a16d3f4376d5aa81b8644ee63a519d684c6e23',
    'tests/start_menu_test_support.h': '6dd8bb36e9c16b2873c11e32767971a5e6b755578a92160a41284d308fe5a7cc',
}
EXTRA_INPUTS = ['tests/test_start_menu_ink.py', 'tests/test_start_menu_cabinet.py',
                'tools/generate_cabinet_font.py', 'tools/generate_start_menu_labels.py',
                'tests/generate_wordmark_reference.py', 'tools/pixel_wordmark.py',
                'assets/fonts/Gossip-MedPixel.otf', 'assets/branding/gothic_wordmark_native_mask.png']
LIMITS = ('host software rect/font coverage, not hardware screenshots, GPU performance, FPS, '
          'touch latency or listening. Existing static host paper fallback, no authored replacement '
          'pixels; boot veil is intentionally omitted as for the approved study. '
          '400 actual frames at40ms plus separate16000ms endpoint. GIF uses independent global '
          'palette quantization; native PPM/PNG are lossless. Dual rasters sample upper phases40ms apart; '
          'counts additionally enumerate every distinct integer-ms upper contour over48s. '
          'Lower-state coverage is scoped, not exhaustive of all gesture/time/string combinations.')


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def stamp() -> str:
    return datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')


def hashes(names) -> dict[str, str]:
    return {str(n): sha(ROOT / n) for n in sorted(names)}


def production_guard() -> dict[str, str]:
    paths = [p for folder in ('core', 'platform/3ds', 'assets')
             for p in (ROOT / folder).rglob('*') if p.is_file()]
    paths.append(ROOT / 'Makefile')
    return hashes(p.relative_to(ROOT).as_posix() for p in paths)


def dependencies() -> dict[str, str]:
    text = subprocess.check_output(['g++', '-std=gnu++17', '-Itests/sequencer_test_stubs',
                                    '-MM', *SOURCES], cwd=ROOT, text=True)
    names = set(SOURCES + EXTRA_INPUTS)
    assert sha(BASELINE) == BASELINE_SHA, 'approved study source-capture manifest changed'
    baseline = json.loads(BASELINE.read_text())
    for name, digest in baseline['sources'].items():
        assert sha(ROOT / name) == digest, 'historical runtime baseline changed: ' + name
        names.add(name)
    names.add(BASELINE.relative_to(ROOT).as_posix())
    for name in text.replace('\\\n', ' ').split():
        p = ROOT / name
        if p.is_file():
            names.add(p.resolve().relative_to(ROOT).as_posix())
    return hashes(names)


def save_json(path: Path, data) -> None:
    assert not path.exists(), f'preserve existing receipt: {path}'
    path.write_text(json.dumps(data, indent=2) + '\n')


def package_capture(folder: Path) -> dict:
    from PIL import Image, GifImagePlugin, __version__ as pillow_version
    raw = folder / 'raw'
    native = folder / 'native'
    native.mkdir()
    frames = []
    for i in range(400):
        path = raw / f'{40*i:05d}-bottom.ppm'
        with Image.open(path) as im:
            frame = im.convert('RGB')
        assert frame.size == (320, 240)
        frames.append(frame)
    for path in sorted(raw.glob('*.ppm')):
        target = native / (path.stem + '.png')
        with Image.open(path) as im:
            im.save(target, optimize=False)
            with Image.open(target) as decoded:
                assert decoded.convert('RGB').tobytes() == im.convert('RGB').tobytes()
    # All actual source pixels contribute to one palette, independently of the
    # approved study or production tint implementation. No per-frame palettes.
    contact = Image.new('RGB', (320, 240 * len(frames)))
    for i, frame in enumerate(frames):
        contact.paste(frame, (0, i * 240))
    palette = contact.quantize(colors=256, method=Image.Quantize.MEDIANCUT,
                               dither=Image.Dither.NONE)
    palette_rgb = palette.getpalette()
    palette = Image.new('P', (1, 1))
    palette.putpalette(palette_rgb)
    del contact
    quantized = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
    max_error = 0
    absolute_error = 0
    channel_count = 0
    for original, coded in zip(frames, quantized):
        # Histogram accounting is much smaller than a Python loop over92M channels.
        from PIL import ImageChops
        diff = ImageChops.difference(original, coded.convert('RGB'))
        for channel in diff.split():
            hist = channel.histogram()
            max_error = max(max_error, max(i for i, count in enumerate(hist) if count))
            absolute_error += sum(i * count for i, count in enumerate(hist))
            channel_count += sum(hist)
    outputs = {}
    for scale in (1, 2):
        gif = folder / f'living-ink-{scale}x.gif'
        encoded = quantized if scale == 1 else [frame.resize((640, 480), Image.Resampling.NEAREST) for frame in quantized]
        # Emit each whole frame explicitly. Pillow's ordinary save_all coalesces
        # identical quantized frames and obscures the exact authored40ms cadence.
        with gif.open('wb') as stream:
            for block in GifImagePlugin._get_global_header(encoded[0], {'loop': 0, 'duration': 40}):
                stream.write(block)
            for frame in encoded:
                GifImagePlugin._write_frame_data(stream, frame, (0, 0), {'duration': 40, 'disposal': 1})
            stream.write(b';')
        durations = []
        with Image.open(gif) as decoded:
            assert decoded.n_frames == 400 and decoded.size == (320 * scale, 240 * scale)
            for i, expected in enumerate(encoded):
                decoded.seek(i)
                assert decoded.convert('RGB').tobytes() == expected.convert('RGB').tobytes(), (gif, i)
                durations.append(decoded.info['duration'])
            assert decoded.info.get('loop') == 0
        assert durations == [40] * 400 and sum(durations) == 16000
        outputs[gif.name] = {'sha256': sha(gif), 'bytes': gif.stat().st_size,
                             'decoded_frames': 400, 'duration_ms': 16000,
                             'every_frame_ms': 40, 'scale': scale, 'filter': 'nearest-neighbor'}
    return {'pillow': pillow_version, 'source_frames': 400, 'separate_endpoint_ms': 16000,
            'palette': 'one independent global256-color palette per theme, all source pixels; no dithering',
            'palette_rgb_sha256': hashlib.sha256(bytes(palette_rgb)).hexdigest(),
            'gif_channel_error_max': max_error,
            'gif_channel_error_mean': absolute_error / channel_count,
            'decode_equals_global_palette_quantized_actual_frames': True,
            'native_png_equals_raw_ppm': True, 'gifs': outputs}


def compare_excluded_pages(folder: Path, theme: str) -> dict:
    from PIL import Image
    baseline = json.loads(BASELINE.read_text())
    root = BASELINE.parent / 'source-capture' / theme
    compared = {}
    # The old capture_cabinet() event sequence is unchanged. Upper wordmark and
    # preview, both browser pages, and opaque palette contents must be exact old
    # actual-runtime pixels, not the authored study's replacement frames.
    names = sorted(p.name for p in root.glob('*_top.ppm')) + ['05-browser_bottom.ppm', '06-last-page_bottom.ppm']
    for name in names:
        old = root / name
        assert sha(old) == baseline['sources'][old.relative_to(ROOT).as_posix()]
        assert sha(folder / name) == sha(old), 'excluded screen changed: ' + name
        compared[name] = sha(old)
    with Image.open(root / '08-palette_bottom.ppm') as old, Image.open(folder / '08-palette_bottom.ppm') as new:
        a = old.crop((12, 12, 308, 228)).convert('RGB').tobytes()
        b = new.crop((12, 12, 308, 228)).convert('RGB').tobytes()
        assert a == b, 'opaque palette contents changed'
    return {'baseline_manifest': BASELINE.relative_to(ROOT).as_posix(), 'baseline_sha256': BASELINE_SHA,
            'exact_whole_screens': compared,
            'exact_palette_interior': {'box': [12, 12, 308, 228], 'rgb_sha256': hashlib.sha256(a).hexdigest()}}


def run_one(mode: str, output: Path, capture: bool) -> Path:
    out = output / f'{mode}-{stamp()}'
    out.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    guard_before = production_guard()
    inputs = dependencies()
    assert all(inputs.get(k) == v for k, v in PINNED_GEOMETRY.items()), 'approved native geometry/support changed'
    for name in inputs:
        dest = out / 'inputs' / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / name, dest)
    assert inputs == {n: sha(out / 'inputs' / n) for n in inputs}, 'retained dependency mismatch'
    flags = BASE_FLAGS + (SAN_FLAGS if mode == 'sanitized' else [])
    binary = out / 'start-menu-ink'
    command = ['g++', *flags, *SOURCES, '-o', str(binary)]
    results = []
    packages = {}
    log_path = out / 'check.log'
    with log_path.open('w') as log:
        def run(cmd, theme=None):
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
            if theme:
                env['SEANCE_TEST_THEME'] = theme
            log.write('$ ' + ' '.join(cmd) + (' theme=' + theme if theme else '') + '\n')
            log.flush()
            result = subprocess.run(cmd, cwd=ROOT, env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            print(result.stdout, end='', flush=True)
            log.write(result.stdout)
            log.flush()
            results.append({'command': cmd, 'theme': theme, 'exit': result.returncode, 'stdout': result.stdout})
            result.check_returncode()
            return result.stdout
        try:
            for generator in ('generate_start_menu_labels.py', 'generate_cabinet_font.py', 'generate_wordmark_reference.py'):
                run(['python3', 'tests/' + generator, '--check'])
            run(command)
            for theme in THEMES:
                old = run([str(binary)], theme)
                new = run([str(binary), '--ink'], theme)
                assert len(re.findall(r'^ok ', old, re.M)) == 7
                assert len(re.findall(r'^ok ', new, re.M)) == 4
                if capture:
                    folder = out / 'runtime' / theme
                    raw = folder / 'raw'
                    run([str(binary), '--ink-capture', str(raw)], theme)
                    packages[theme] = package_capture(folder)
                    exclusion = folder / 'exclusion-current'
                    run([str(binary), '--capture', str(exclusion)], theme)
                    packages[theme]['excluded_pages'] = compare_excluded_pages(exclusion, theme)
            guard_after = production_guard()
            after = hashes(inputs)
            assert guard_before == guard_after, 'production changed during proof/captures: retain as historical, rerun final'
            assert inputs == after, 'build dependencies changed during proof/capture'
        except BaseException as exc:
            save_json(out / 'failed.json', {'at': datetime.now(timezone.utc).isoformat(),
                                          'error': repr(exc), 'inputs_before': inputs,
                                          'guard_before': guard_before, 'results': results,
                                          'limits': 'failed or historical attempt; not a current passing receipt'})
            raise
    media = {p.relative_to(out).as_posix(): sha(p) for p in sorted((out / 'runtime').rglob('*')) if p.is_file()} if capture else {}
    # A final rehash after media encoding is as important as the build-time check.
    assert guard_before == production_guard() and inputs == hashes(inputs)
    receipt = {'at': datetime.now(timezone.utc).isoformat(), 'mode': mode,
               'duration_seconds': time.monotonic() - started,
               'command': command, 'compiler': subprocess.check_output(['g++', '--version'], text=True).splitlines()[0],
               'optimization': 'O1; avoids pre-existing unrelated O2 format-truncation warning without relaxing -Werror',
               'dependencies_before': inputs, 'dependencies_after': after,
               'production_before': guard_before, 'production_after': guard_after,
               'retained_inputs_sha256': {n: sha(out / 'inputs' / n) for n in inputs},
               'binary': {'path': binary.relative_to(out).as_posix(), 'sha256': sha(binary)},
               'log': {'path': log_path.relative_to(out).as_posix(), 'sha256': sha(log_path)},
               'scope': '7 unchanged cabinet groups +4 new scoped living-ink groups, each graphite/pink; no inherited legacy suite',
               'results': results, 'runtime': packages, 'media': media, 'limits': LIMITS}
    save_json(out / 'receipt.json', receipt)
    print('receipt:', out / 'receipt.json', flush=True)
    return out / 'receipt.json'


def compare_pair(a: Path, b: Path, output: Path) -> Path:
    first, second = (json.loads(path.read_text()) for path in (a, b))
    assert first['dependencies_before'] == second['dependencies_before'], 'pair uses different dependencies'
    assert first['production_before'] == second['production_before'], 'pair uses different production guard'
    assert first['media'] == second['media'], 'optimized/sanitized actual rasters or packaged media differ'
    assert first['dependencies_before'] == hashes(first['dependencies_before'])
    assert first['production_before'] == production_guard()
    out = output / f'pair-{stamp()}.json'
    save_json(out, {'at': datetime.now(timezone.utc).isoformat(),
                    'receipts': {str(path.relative_to(ROOT)): sha(path) for path in (a, b)},
                    'pair_identical_media': len(first['media']),
                    'pair_identical_dependencies': len(first['dependencies_before']),
                    'pair_identical_production': len(first['production_before']),
                    'native_and_encoded_media_exact': True, 'limits': LIMITS})
    print('pair:', out)
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=['optimized', 'sanitized', 'both'], default='both')
    parser.add_argument('--output', type=Path, default=OUT)
    parser.add_argument('--no-capture', action='store_true', help='checks only; no runtime media claim')
    args = parser.parse_args()
    output = args.output.resolve()
    modes = ('optimized', 'sanitized') if args.mode == 'both' else (args.mode,)
    receipts = [run_one(mode, output, not args.no_capture) for mode in modes]
    if len(receipts) == 2:
        compare_pair(*receipts, output)


if __name__ == '__main__':
    main()
