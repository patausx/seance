#!/usr/bin/env python3
"""Offline beam + temporary afterimage; does not modify the app or older previews."""
from pathlib import Path
from fractions import Fraction
import json
from PIL import Image, ImageDraw, ImageFont
import render_playhead_beam_study as beam
import render_playhead_study as legacy

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/playhead_trace_study'
LIFE_MS = 480
STRENGTH = Fraction(3, 4)
U, W, H = beam.UNIT, beam.TRACK * beam.UNIT, beam.BEAM_WIDTH * beam.UNIT // 2


def field(row, ms):
    """Exact integral over each pixel: unchanged solid head, quadratic fading wake.

    For these constant clocks distance behind the rear edge determines the time
    since that location was left. This is not a proposed runtime clock/history API.
    """
    cq = beam.center_q(row, ms)
    _, direction, period = beam.LANES[row]
    length = LIFE_MS * beam.PITCH * U // period
    den = 3 * STRENGTH.denominator * length * length
    head, combined = [], []
    for x in range(beam.TRACK):
        a, b = x * U, (x + 1) * U
        cover = sum(max(0, min(b, cq + H + shift) - max(a, cq - H + shift))
                    for shift in (-W, 0, W))
        lo, hi = sorted((direction * (cq - a) - H, direction * (cq - b) - H))
        numerator = cover * den
        for shift in (-W, 0, W):
            near, far = max(0, lo + shift), min(length, hi + shift)
            if near < far:
                numerator += STRENGTH.numerator * ((length - near) ** 3 - (length - far) ** 3)
        alpha = (numerator + den // 2) // den
        assert 0 <= cover <= alpha <= U
        head.append(cover); combined.append(alpha)
    return head, combined


def oracle(row, ms):
    # Independent physical intervals and antiderivative, with rational arithmetic.
    # Unlike field(), construct the tail as an actual interval on the x axis.
    _, direction, period = beam.LANES[row]
    length = Fraction(LIFE_MS * beam.PITCH * U, period)
    cq = beam.center_q(row, ms)
    rear = cq - direction * H
    result = []
    primitive = lambda t: STRENGTH * (t - t * t / length + t ** 3 / (3 * length * length))
    for x in range(beam.TRACK):
        a, b = x * U, (x + 1) * U
        total = Fraction(0)
        for shift in (-W, 0, W):
            total += max(0, min(b, cq + H + shift) - max(a, cq - H + shift))
            start, end = sorted((rear + shift, rear + shift - direction * length))
            left, right = max(a, start), min(b, end)
            if left < right:
                near, far = sorted((abs(left - rear - shift), abs(right - rear - shift)))
                total += primitive(far) - primitive(near)
        result.append(int(total + Fraction(1, 2)))
    return result


def render(base, theme, alphas):
    im = base.copy()
    for row, y in enumerate(legacy.ROWS):
        for yy in (y + 31, y + 32):
            for x, alpha in enumerate(alphas[row], beam.X0):
                if alpha:
                    bg, fg = base.getpixel((x, yy)), legacy.PALETTES[theme]['play']
                    im.putpixel((x, yy), tuple((a * (U - alpha) + b * alpha + U // 2) // U
                                              for a, b in zip(bg, fg)))
    return im


def preserved():
    result = legacy.protected()
    for folder in ('playhead_study', 'playhead_beam_study'):
        for p in sorted((ROOT / 'ui_mockups' / folder).rglob('*')):
            if p.is_file():
                result[str(p.relative_to(ROOT))] = legacy.sha(p)
    for p in ('tools/render_playhead_study.py', 'tools/render_playhead_beam_study.py',
              'docs/playhead-study.md', 'docs/playhead-beam-study.md'):
        result[p] = legacy.sha(ROOT / p)
    return result


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    before = preserved()
    for folder in ('playhead_study', 'playhead_beam_study'):
        receipt = json.loads((ROOT / 'ui_mockups' / folder / 'finalization.json').read_text())
        for group in ('evidence_sha256', 'protected_unchanged'):
            for p, h in receipt[group].items():
                assert legacy.sha(ROOT / p) == h, p
    stats = dict(phase_checks=beam.check_phase(), independent_oracle_pixels=0, frames=0,
                 unchanged_pixels=0, exact_beam_baselines=0)
    fields, heads = {}, {}
    for ms in range(0, beam.LENGTH, beam.TICK):
        heads[ms], fields[ms] = [], []
        for row in range(4):
            head, full = field(row, ms)
            heads[ms].append(head); fields[ms].append(full)
            assert any(a > h for a, h in zip(full, head)), 'missing trace'
            if ms in (0, 20, 200, 700, 1500, 3180, 3200, 6380):
                assert full == oracle(row, ms), (row, ms)
                stats['independent_oracle_pixels'] += beam.TRACK
    for row in range(4):
        assert field(row, 0) == field(row, beam.LENGTH)
        for ms in range(0, beam.LENGTH, beam.TICK):
            assert fields[ms][row] != fields[(ms + beam.TICK) % beam.LENGTH][row]
    # A departed point decays with elapsed time, not with frames or BPM.
    decay = [STRENGTH * max(Fraction(0), 1 - Fraction(age, LIFE_MS)) ** 2
             for age in (0, 120, 240, 360, 480, 600)]
    assert decay == [Fraction(3, 4), Fraction(27, 64), Fraction(3, 16), Fraction(3, 64), 0, 0]
    mask = Image.new('L', (320, 240), 0)
    md = ImageDraw.Draw(mask)
    for y in legacy.ROWS:
        md.rectangle((beam.X0, y + 31, beam.X0 + beam.TRACK - 1, y + 32), fill=255)
    artifacts, sources, kept = {}, {}, {}
    contact = Image.new('RGB', (680, 556), (20, 20, 23))
    cd = ImageDraw.Draw(contact)
    font = ImageFont.truetype('/usr/share/fonts/TTF/IBMPlexMono-Regular.ttf', 12)
    for ti, theme in enumerate(('gray', 'pink')):
        for mi, mode in enumerate(('trig', 'note')):
            source = ROOT / 'ui_mockups/playhead_study' / f'{theme}_{mode}_base.png'
            sources[str(source.relative_to(ROOT))] = legacy.sha(source)
            base = Image.open(source).convert('RGB')
            frames = []
            for ms in range(0, beam.LENGTH, beam.TICK):
                im = render(base, theme, fields[ms])
                assert Image.composite(base, im, mask).tobytes() == base.tobytes()
                stats['frames'] += 1
                stats['unchanged_pixels'] += 320 * 240 - 4 * beam.TRACK * 2
                # Turning off only the trace gives the original ray exactly.
                baseline = render(base, theme, heads[ms])
                assert baseline.tobytes() == legacy.apply(base, beam.beam_ops(base, theme, ms)).tobytes()
                stats['exact_beam_baselines'] += 1
                frames.append(im)
            name = f'{theme}_{mode}.gif'
            artifacts[name] = legacy.save_gif(frames, OUT / name, scale=2)
            kept[(theme, mode)] = frames
            contact.paste(frames[35], (12 + ti * 336, 28 + mi * 274))
            cd.text((12 + ti * 336, 8 + mi * 274), f'{theme} / {mode} / 480ms trace', font=font, fill=(205, 192, 224))
            print(name, artifacts[name]['bytes'], flush=True)
    contact.save(OUT / 'contact_sheet.png')
    strips = [im.crop((0, 41, 320, 82)).resize((960, 123), Image.Resampling.NEAREST)
              for im in kept[('gray', 'note')]]
    artifacts['gray_note_strip.gif'] = legacy.save_gif(strips, OUT / 'gray_note_strip.gif')
    # Synchronized row comparison, not an extra app screen or gallery.
    comparisons = []
    base = Image.open(ROOT / 'ui_mockups/playhead_study/gray_note_base.png').convert('RGB')
    for i, ms in enumerate(range(0, beam.LENGTH, beam.TICK)):
        im = Image.new('RGB', (960, 304), (20, 20, 23))
        d = ImageDraw.Draw(im); d.fontmode = '1'
        d.text((12, 8), 'beam / no trace', font=font, fill=(205, 192, 224))
        d.text((12, 160), 'beam / 480ms fading trace', font=font, fill=(205, 192, 224))
        off = render(base, 'gray', heads[ms]).crop((0, 41, 320, 82)).resize((960, 123), Image.Resampling.NEAREST)
        im.paste(off, (0, 29)); im.paste(strips[i], (0, 181))
        comparisons.append(im)
    artifacts['gray_note_comparison.gif'] = legacy.save_gif(comparisons, OUT / 'gray_note_comparison.gif')
    artifacts['contact_sheet.png'] = dict(sha256=legacy.sha(OUT / 'contact_sheet.png'))
    assert before == preserved()
    sources[str(Path(__file__).relative_to(ROOT))] = legacy.sha(Path(__file__))
    result = dict(status='pass', scope='offline temporary trace suggestion; not selected or implemented',
                  lifetime_ms=LIFE_MS, peak_opacity=str(STRENGTH), fade='quadratic by elapsed time since beam rear left',
                  duration_ms=beam.LENGTH, tick_ms=beam.TICK, lanes=beam.LANES, counts=stats,
                  sources=sources, artifacts=artifacts, protected_unchanged=before,
                  limitations=['synthetic silent constant transport; not recording running app',
                               'steady-state wrap includes the tail from the previous cycle',
                               'head stays 22x2; fractional rear pixels also contain trace coverage',
                               '20-level alpha palette; no claim of device FPS, GPU cost or audio behavior',
                               'no start/stop, tempo jumps, swing or ping-pong simulation',
                               'runtime, device, old studies and manual press response untouched'])
    (OUT / 'verification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(stats), flush=True)


if __name__ == '__main__':
    main()
