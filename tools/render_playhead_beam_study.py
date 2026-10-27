#!/usr/bin/env python3
"""Offline continuous beam on the preceding native study substrates; no runtime.

Does not call legacy.main/substrate or overwrite any preceding study materials.
"""
from pathlib import Path
import json
from PIL import Image, ImageChops, ImageDraw, ImageFont
import render_playhead_study as legacy

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/playhead_beam_study'
OLD = ROOT / 'ui_mockups/playhead_study'
TICK, LENGTH = 20, 6400
X0, TRACK, PITCH, UNIT = 8, 304, 19, 20
BEAM_WIDTH, BEAM_HEIGHT = 22, 2
# Constant clocks for a seamless 6.4s loop. Synthetic, not the engine's transport.
# Forward / reverse / offset forward / half-speed forward.
LANES = ((0, 1, 200), (15, -1, 200), (8, 1, 200), (0, 1, 400))


def center_q(row, ms):
    offset, direction, period = LANES[row]
    travel = direction * (ms * PITCH * UNIT // period)
    return ((9 + offset * PITCH) * UNIT + travel) % (TRACK * UNIT)


def beam_ops(base, theme, ms):
    ops = []
    for row, y in enumerate(legacy.ROWS):
        center = X0 * UNIT + center_q(row, ms)
        left = center - BEAM_WIDTH * UNIT // 2
        # Wrapped copies clip at the row edges, never sweep back across the row.
        for shift in (-TRACK * UNIT, 0, TRACK * UNIT):
            lo, hi = left + shift, left + shift + BEAM_WIDTH * UNIT
            for x in range(max(X0, lo // UNIT), min(X0 + TRACK, (hi + UNIT - 1) // UNIT)):
                cover = max(0, min((x + 1) * UNIT, hi) - max(x * UNIT, lo))
                assert 0 < cover <= UNIT
                for yy in range(y + 31, y + 31 + BEAM_HEIGHT):
                    bg = base.getpixel((x, yy))
                    fg = legacy.PALETTES[theme]['play']
                    color = tuple((a * (UNIT - cover) + b * cover + UNIT // 2) // UNIT
                                  for a, b in zip(bg, fg))
                    ops.append([x, yy, 1, 1, *color])
    return ops


def previous_files():
    return {str(p.relative_to(ROOT)): legacy.sha(p) for p in sorted(OLD.rglob('*')) if p.is_file()}


def check_phase():
    checks = 0
    for row, (_, direction, period) in enumerate(LANES):
        expected = direction * TICK * PITCH * UNIT // period
        assert abs(expected) > 0
        for ms in range(0, LENGTH, TICK):
            a, b = center_q(row, ms), center_q(row, ms + TICK)
            delta = (b - a + TRACK * UNIT // 2) % (TRACK * UNIT) - TRACK * UNIT // 2
            assert delta == expected, (row, ms, delta, expected)
            # Independent unwrapped linear trajectory, checked modulo lane width.
            offset = LANES[row][0]
            reference = (9 + offset * PITCH + direction * ms * PITCH / period) % TRACK
            assert abs(a / UNIT - reference) < 1e-9
            checks += 1
        assert center_q(row, 0) == center_q(row, LENGTH)
    return checks


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    protected = legacy.protected()
    historical = previous_files()
    old_receipt = json.loads((OLD / 'finalization.json').read_text())
    for path, h in old_receipt['evidence_sha256'].items():
        assert legacy.sha(ROOT / path) == h, path
    for path, h in old_receipt['protected_unchanged'].items():
        assert legacy.sha(ROOT / path) == h, path
    counts = dict(phase_checks=check_phase(), frames=0, unchanged_pixels=0, coverage_rows=0)
    sources = {str(Path(__file__).relative_to(ROOT)): legacy.sha(Path(__file__)),
               'tools/render_playhead_study.py': legacy.sha(ROOT / 'tools/render_playhead_study.py')}
    artifacts = {}
    contact = Image.new('RGB', (680, 556), (20, 20, 23))
    draw = ImageDraw.Draw(contact)
    font = ImageFont.truetype('/usr/share/fonts/TTF/IBMPlexMono-Regular.ttf', 12)
    kept = {}
    for ti, theme in enumerate(('gray', 'pink')):
        for mi, mode in enumerate(('trig', 'note')):
            source = OLD / f'{theme}_{mode}_base.png'
            sources[str(source.relative_to(ROOT))] = legacy.sha(source)
            base = Image.open(source).convert('RGB')
            assert base.size == (320, 240)
            mask = Image.new('L', base.size, 0)
            md = ImageDraw.Draw(mask)
            for y in legacy.ROWS:
                md.rectangle((X0, y + 31, X0 + TRACK - 1, y + 32), fill=255)
                for yy in (y + 31, y + 32):
                    assert all(base.getpixel((x, yy)) == base.getpixel((0, yy))
                               for x in range(X0, X0 + TRACK))
            original_bytes = base.tobytes()
            frames = []
            previous = None
            for ms in range(0, LENGTH, TICK):
                ops = beam_ops(base, theme, ms)
                im = legacy.apply(base, ops)
                # Replace only the eight allowed scan lines, then require a full
                # byte-exact image. All native glyphs/focus/wave/editor stay fixed.
                restored = Image.composite(base, im, mask)
                assert restored.tobytes() == original_bytes
                counts['unchanged_pixels'] += 320 * 240 - TRACK * BEAM_HEIGHT * 4
                counts['frames'] += 1
                for row, y in enumerate(legacy.ROWS):
                    # Independent per-pixel circular interval coverage oracle.
                    cq = center_q(row, ms)
                    for yy in (y + 31, y + 32):
                        energy = 0
                        for x in range(X0, X0 + TRACK):
                            a = (x - X0) * UNIT
                            coverage = sum(max(0, min(a + UNIT, cq + BEAM_WIDTH * UNIT // 2 + shift)
                                               - max(a, cq - BEAM_WIDTH * UNIT // 2 + shift))
                                           for shift in (-TRACK * UNIT, 0, TRACK * UNIT))
                            energy += coverage
                            bg = base.getpixel((x, yy)); fg = legacy.PALETTES[theme]['play']
                            want = tuple((bg[c] * (UNIT - coverage) + fg[c] * coverage + UNIT // 2) // UNIT
                                         for c in range(3))
                            assert im.getpixel((x, yy)) == want
                        assert energy == BEAM_WIDTH * UNIT  # no pulse, shrink or wrap loss
                        counts['coverage_rows'] += 1
                    crop = im.crop((X0, y + 31, X0 + TRACK, y + 33)).tobytes()
                    if previous is not None:
                        assert crop != previous[row], (theme, mode, row, ms, 'stopped frame')
                previous = [im.crop((X0, y + 31, X0 + TRACK, y + 33)).tobytes() for y in legacy.ROWS]
                frames.append(im)
            name = f'{theme}_{mode}.gif'
            artifacts[name] = legacy.save_gif(frames, OUT / name, scale=2)
            kept[(theme, mode)] = frames
            contact.paste(frames[35], (12 + 336 * ti, 28 + 274 * mi))
            draw.text((12 + 336 * ti, 8 + 274 * mi), f'{theme} / {mode} / continuous beam', font=font, fill=(205, 192, 224))
            print(name, artifacts[name]['bytes'], flush=True)
    contact.save(OUT / 'contact_sheet.png')
    # Enlarged native strip makes the constant-width translation easy to inspect.
    strip_frames = [im.crop((0, 41, 320, 82)).resize((960, 123), Image.Resampling.NEAREST)
                    for im in kept[('gray', 'note')]]
    artifacts['gray_note_strip.gif'] = legacy.save_gif(strip_frames, OUT / 'gray_note_strip.gif')
    assert protected == legacy.protected()
    assert historical == previous_files()
    artifacts['contact_sheet.png'] = dict(sha256=legacy.sha(OUT / 'contact_sheet.png'))
    result = dict(status='pass', scope='offline visual suggestion only; not selected or implemented',
                  duration_ms=LENGTH, tick_ms=TICK, beam_px=[BEAM_WIDTH, BEAM_HEIGHT],
                  lanes=LANES, counts=counts, sources=sources, artifacts=artifacts,
                  protected_unchanged=protected, preceding_study_unchanged=historical,
                  limitations=['silent synthetic continuous clocks; fixed substrate, not running app',
                               '20ms GIF sampling is not console FPS or GPU budget evidence',
                               'reverse/offset/half-speed shown; ping-pong/jumps/swing not modeled',
                               'runtime, builds, console and manual press animation untouched'])
    (OUT / 'verification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(counts), flush=True)


if __name__ == '__main__':
    main()
