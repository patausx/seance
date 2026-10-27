#!/usr/bin/env python3
"""Preview ONLY: Seance sigils ink thread v2, native 320x240 lower screen.

Only WAVE_BOX changes; the approved single-surface frame and upper are reused.
All values are authored design fixtures, NOT device telemetry, PCM or spectra.
Seance Note/Menu bitmap derivatives, Deborah Khodanovich 2025, SIL OFL 1.1,
see assets/fonts/README.md. No runtime, font, binary or device changes.
"""
from dataclasses import asdict, replace
from itertools import combinations
from pathlib import Path
import hashlib
import json
from PIL import Image, ImageDraw

from render_sigils_single_surface import (
    State, lower, upper, sample_at, MODE_LABELS, MODE_NAMES, DIVISIONS,
    hz, effective_raw, output, source_options,
)
from render_sigils_xy_study import WAVE_BOX, curve_points, curve_bottom
from render_sigils_menu_study import CURVE, bead
from render_menu_style_study import Page, paper, GHOST, BRANCH, overlaps
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE, pair, protected_hashes

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/sigils_thread_v2'
X0, X1, Y0, Y1 = 20, 300, 64, 147
ZERO = (Y0 + Y1) // 2
SCALE = (Y1 - Y0 - 6) / 92
LIMITS = (ZERO - round(46 * SCALE), ZERO + round(46 * SCALE))
POINTS = tuple(curve_points())
ALLOWED = set(paper(400).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH, OUTSIDE}
OUTSIDE_CROPS = ((0, 0, 320, 61), (0, 150, 320, 240),
                 (0, 61, 12, 150), (308, 61, 320, 150))


def columns(points):
    """Interpolate the 1/2px X gaps, preserving every traced anchor exactly."""
    assert points
    result = []
    for (ax, ay), (bx, by) in zip(points, points[1:]):
        assert bx > ax
        for x in range(ax, bx):
            result.append((x, round(ay + (by - ay) * (x - ax) / (bx - ax))))
    result.append(points[-1])
    assert all(b[0] == a[0] + 1 for a, b in zip(result, result[1:]))
    assert all(dict(result)[x] == y for x, y in points)
    return tuple(result)


def column_fill(c, points, color):
    """One screen column wide, inclusive y[i]..y[i+1]; never stroke width=2."""
    samples = columns(points)
    for i, (x, y) in enumerate(samples):
        next_y = samples[min(i + 1, len(samples) - 1)][1]
        assert WAVE_BOX[0] <= x < WAVE_BOX[2]
        assert WAVE_BOX[1] <= min(y, next_y) <= max(y, next_y) < WAVE_BOX[3]
        c.d.rectangle((x, min(y, next_y), x, max(y, next_y)), fill=color)


def depth_points(depth):
    assert -99 <= depth <= 99
    return tuple((x, max(LIMITS[0], min(LIMITS[1],
                  ZERO + round((y - ZERO) * depth / 99)))) for x, y in POINTS)


def guides(c):
    # +/-99 use the original 46px amplitude and the EXACT existing window scale.
    for y in LIMITS:
        c.line((X0, y, X1, y), BRANCH)
    for x in (X0, (X0 + X1) // 2, X1):
        c.line((x, ZERO - 1, x, ZERO + 1), BRANCH)
    for i in range(9):
        x = X0 + (X1 - X0) * i // 8
        c.line((x, Y1 - 2, x, Y1), BRANCH)


def step_y(phase):
    held_phase = (min(15, int(phase * 16)) + .5) / 16
    return ZERO + round((sample_at(held_phase) - 94) * SCALE)


def comet_points(s):
    index = round((len(POINTS) - 1) * s.phase)
    points = POINTS[max(0, index - 24):index + 1]
    if s.mode == 2:
        # Preserve the source's held-step cursor rather than suggest interpolation.
        points = tuple((x, step_y((x - X0) / (X1 - X0))) for x, _ in points)
    return points


def thread_lower(s):
    """Compose only inside the existing draw hit. No tabs/controls are rebuilt."""
    assert 0 <= s.mode < len(MODE_NAMES) and 0 <= s.phase <= 1
    c = lower(s)
    # The existing random-mode caption survives byte-for-byte, including paper.
    captions = [(box, c.im.crop(box)) for box, _ in c.labels if overlaps(box, WAVE_BOX)]
    assert all(WAVE_BOX[0] <= b[0] < b[2] <= WAVE_BOX[2] and
               WAVE_BOX[1] <= b[1] < b[3] <= WAVE_BOX[3] for b, _ in captions)
    c.im.paste(paper(320).crop(WAVE_BOX), WAVE_BOX[:2])
    guides(c)
    random = s.mode in (3, 4)
    if not random:
        # Random ignores the drawing. Follow has no PLAY in this exact fixture:
        # show a zero output instead of inventing a following source/position.
        column_fill(c, depth_points(0 if s.mode == 5 else s.depth), BRANCH)
    column_fill(c, POINTS, GHOST if random else INK)
    if s.mode == 2:
        # These sixteen ledges already exist in lower(); geometry/ink unchanged.
        for i in range(16):
            a = X0 + (X1 - X0) * i // 16
            b = X0 + (X1 - X0) * (i + 1) // 16
            y = step_y((i + .5) / 16)
            c.line((a, y, b - 1, y), SPOT)
    if s.mode not in (3, 4, 5):
        trail = comet_points(s)
        column_fill(c, trail, SPOT)
        x, y = trail[-1]
        bead(c, x, y, 3, SPOT, True)
        # Same SPOT and one-pixel outline weight, not a competing bold marker.
        c.line((x - 2, Y1, x + 2, Y1), SPOT)
    for box, image in captions:
        c.im.paste(image, box[:2])
    return c


def check_geometry(c):
    # Labels intentionally live INSIDE their own touch targets. Check labels
    # against labels and hits against hits, not the impossible cross-product.
    for boxes in ([box for box, _ in c.labels], [box for _, box in c.hits]):
        assert all(0 <= a < b <= c.im.width and 0 <= y < z <= c.im.height
                   for a, y, b, z in boxes)
        assert all(not overlaps(a, b) for a, b in combinations(boxes, 2))


def check_frame(s):
    old, new, top = lower(s), thread_lower(s), upper(s)
    assert new.im.size == (320, 240) and top.im.size == (400, 240)
    assert new.hits == old.hits and new.labels == old.labels
    for box in OUTSIDE_CROPS:
        assert new.im.crop(box).tobytes() == old.im.crop(box).tobytes(), (s, box)
    for c in (new, top):
        check_geometry(c)
        assert set(c.im.get_flattened_data()) <= ALLOWED
    assert set(MODE_LABELS) <= {name for _, name in new.labels}
    assert -99 <= output(s) <= 99
    if s.mode in (3, 4, 5):
        assert SPOT not in set(new.im.crop(WAVE_BOX).get_flattened_data())
    if s.mode in (3, 4):
        assert INK not in set(new.im.crop(WAVE_BOX).get_flattened_data())
        for box, _ in old.labels:
            if overlaps(box, WAVE_BOX):
                assert new.im.crop(box).tobytes() == old.im.crop(box).tobytes()
    return top, new


def save(im, name, saved):
    assert set(im.get_flattened_data()) <= ALLOWED, name
    big = im.resize((im.width * 2, im.height * 2), Image.Resampling.NEAREST)
    assert set(big.get_flattened_data()) <= ALLOWED, name
    assert big.resize(im.size, Image.Resampling.NEAREST).tobytes() == im.tobytes(), name
    for suffix, image in (('native', im), ('2x', big)):
        path = OUT / f'{name}_{suffix}.png'
        image.save(path)
        with Image.open(path) as decoded:
            assert decoded.convert('RGB').tobytes() == image.tobytes()
        saved.append(path.name)


def prior_hashes():
    paths = [ROOT / 'tools' / name for name in (
        'render_sigils_single_surface.py', 'render_sigils_xy_study.py',
        'render_sigils_menu_study.py', 'render_menu_style_study.py',
        'render_module_geometry_study.py')]
    for folder in ('sigils_menu_study', 'sigils_single_surface', 'sigils_xy_study'):
        paths.extend(p for p in (ROOT / 'ui_mockups' / folder).rglob('*') if p.is_file())
    return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


def check_raster():
    assert WAVE_BOX == (12, 61, 308, 150)
    assert POINTS == tuple((20 + round((x - 31) * 280 / 256),
                            ZERO + round((y - 94) * SCALE)) for x, y in CURVE)
    assert POINTS[0][0] == X0 and POINTS[-1][0] == X1
    # Independently check each filled column: one inclusive interval, no dilation.
    mask = Canvas(320, bg=OUTSIDE)
    column_fill(mask, POINTS, INK)
    sampled = columns(POINTS)
    for i, (x, y) in enumerate(sampled):
        next_y = sampled[min(i + 1, len(sampled) - 1)][1]
        expected = set(range(min(y, next_y), max(y, next_y) + 1))
        actual = {v for v in range(240) if mask.im.getpixel((x, v)) == INK}
        assert actual == expected
    # Four-neighbour connected, even on steep slopes and across expanded X gaps.
    pixels = {(x, y) for x in range(320) for y in range(240)
              if mask.im.getpixel((x, y)) == INK}
    pending = [next(iter(pixels))]
    remaining = pixels - set(pending)
    while pending:
        x, y = pending.pop()
        for point in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if point in remaining:
                remaining.remove(point)
                pending.append(point)
    assert not remaining
    # Quiet zero means precisely three 3px ticks in the GUIDES layer; no dots.
    g = Page(320)
    guides(g)
    for x in range(X0, X1 + 1):
        for y in range(ZERO - 1, ZERO + 2):
            assert (g.im.getpixel((x, y)) == BRANCH) == (x in (20, 160, 300))
    for y in LIMITS:
        assert all(g.im.getpixel((x, y)) == BRANCH for x in range(X0, X1 + 1))
    for i in range(9):
        x = X0 + (X1 - X0) * i // 8
        assert all(g.im.getpixel((x, y)) == BRANCH for y in range(145, 148))
    assert depth_points(99) == POINTS
    assert all(y == ZERO for _, y in depth_points(0))
    for (_, positive), (_, negative) in zip(depth_points(64), depth_points(-64)):
        assert positive + negative == 2 * ZERO
    # Bead must leave all non-outline pixels untouched, including its interior.
    dot = Canvas(320, bg=OUTSIDE)
    bead(dot, 100, 100, 3, SPOT, True)
    assert dot.im.getpixel((100, 100)) == OUTSIDE
    assert dot.im.getpixel((100, 97)) == SPOT
    assert sum(dot.im.getpixel((x, 100)) == SPOT for x in range(97, 104)) == 2
    trail = comet_points(State())
    assert len(trail) == 25 and trail == POINTS[135:160]
    main = thread_lower(State()).im
    x, _ = trail[-1]
    assert all(main.getpixel((v, 147)) == SPOT for v in range(x - 2, x + 3))
    # No full-height playhead hidden under another layer.
    assert all(not all(main.getpixel((x, y)) in (INK, SPOT, GHOST)
                       for y in range(Y0, Y1 + 1)) for x in range(X0, X1 + 1))
    # Zero-depth output is a solid 281px BRANCH thread before ink occlusion.
    zero = thread_lower(State(depth=0)).im
    visible = sum(zero.getpixel((x, ZERO)) == BRANCH for x in range(X0, X1 + 1))
    assert visible >= .85 * (X1 - X0 + 1), visible
    return {'column_intervals_exact_and_4_connected': True,
            'traced_anchors_and_extent_exact': True, 'quiet_zero_three_ticks_only': True,
            'nine_bottom_ticks_unchanged': True, 'clip_limit_rows': LIMITS,
            'depth_positive_negative_mirror_exact': True, 'zero_depth_visible_branch_pixels': visible,
            'zero_depth_total_columns': X1 - X0 + 1, 'hollow_bead_outline_only': True,
            'comet_trailing_intervals': len(trail) - 1, 'axis_tick_spot_1px': True,
            'no_full_height_playhead': True}


def main():
    before, prior = protected_hashes(), prior_hashes()
    OUT.mkdir(parents=True, exist_ok=True)
    raster_checks = check_raster()
    fixtures = {
        'main': State(),
        'depth_pos48': State(depth=48),
        'depth_neg64': State(depth=-64),
        'depth_zero': State(depth=0),
        'mode_step': State(mode=2),
        'mode_rand': State(mode=3),
        'mode_smooth': State(mode=4),
        'mode_follow': State(mode=5),
    }
    saved, frames = [], {}
    for name, s in fixtures.items():
        top, bottom = check_frame(s)
        save(top.im, name + '_top', saved)
        save(bottom.im, name + '_bottom', saved)
        save(pair(top.im, bottom.im), name + '_dual', saved)
        frames[name] = {'state': asdict(s), 'mode': MODE_NAMES[s.mode],
                        'fixture_output': output(s), 'fixture_free_hz': hz(effective_raw(s)),
                        'controls': bottom.hits}
    # Exact treatment 02, with the original a/b/c/d header (not the XY proposal).
    old = curve_bottom('thread', phase=State().phase, paired_tabs=False)
    new = thread_lower(State())
    assert old.hits == new.hits and old.labels == new.labels
    for box in OUTSIDE_CROPS:
        assert old.im.crop(box).tobytes() == new.im.crop(box).tobytes()
    reference = ROOT / 'ui_mockups/sigils_xy_study/curve_thread_native.png'
    with Image.open(reference) as image:
        assert old.im.tobytes() == image.convert('RGB').tobytes()
    comparison = Canvas(656, 272, OUTSIDE)
    caption_ink = paper(1).getpixel((0, 0))
    comparison.text(16, 10, 'current 02 / ink thread', caption_ink)
    comparison.text(352, 10, 'v2 / ink thread', caption_ink)
    comparison.im.paste(old.im, (0, 32))
    comparison.im.paste(new.im, (336, 32))
    save(comparison.im, 'comparison', saved)
    # Convenient lower-only overview; captions are outside every native screen.
    gallery = Canvas(1328, 544, OUTSIDE)
    for i, (name, s) in enumerate(fixtures.items()):
        x, y = (i % 4) * 336, (i // 4) * 272
        caption = f'depth {s.depth:+d}' if i < 4 else 'mode / ' + MODE_LABELS[s.mode]
        gallery.text(x + 16, y + 10, caption, caption_ink)
        gallery.im.paste(thread_lower(s).im, (x, y + 32))
    save(gallery.im, 'all_frames', saved)
    # Frame identity for every bipolar depth; INK itself never resizes with depth.
    ink = {i for i, pixel in enumerate(new.im.crop(WAVE_BOX).get_flattened_data()) if pixel == INK}
    for depth in range(-99, 100):
        _, b = check_frame(State(depth=depth))
        assert {i for i, pixel in enumerate(b.im.crop(WAVE_BOX).get_flattened_data()) if pixel == INK} == ink
    combinations_checked = 0
    for sigil in 'abcd':
        for mode in range(6):
            for sync in (False, True):
                for source in source_options(sigil):
                    check_frame(State(sigil=sigil, mode=mode, sync=sync, mod_source=source))
                    combinations_checked += 1
    for mode in range(6):
        for phase in (0, .01, .1, .25, .5, .62, .9, .99, 1):
            check_frame(State(mode=mode, phase=phase))
    for division in range(len(DIVISIONS)):
        check_frame(State(sync=True, division=division, focus='rate'))
    for focus in ('rate', 'depth', 'mod', 'output'):
        check_frame(replace(State(), focus=focus))
    assert before == protected_hashes(), 'protected runtime/assets/binary changed'
    assert prior == prior_hashes(), 'imported tools or prior study artifacts changed'
    limitations = [
        'preview only: no runtime/controller/GPU/build/deployment or hardware readability validation',
        'all phase/rate/depth/output values are authored fixtures; no device telemetry, PCM or spectra',
        'shape is a screenshot raster trace, not the original Q15 table; integer-pixel projection is approximate',
        'loop/shot ghost is the depth-scaled drawing; step retains original held ledges and a quantized comet, while its faint contour is the continuous depth projection',
        'random modes ignore the drawing: stored GHOST ink/caption only, no output contour or fake marker',
        'follow fixture has no PLAY: ghost is zero and no phase marker is fabricated',
        'random caption retains its exact paper patch over the upper clip guide to avoid striking through text',
        'at +99 the ghost coincides with stored ink and is intentionally occluded; the supplied curve does not reach the clip limits',
        'zero-depth visibility is pixel-checked; BRANCH contrast still needs real LCD review',
        'labels overlap their own touch targets by design; label-label and hit-hit overlaps are forbidden',
    ]
    report = {
        'status': 'preview only / curve-window-only / no runtime changes / no FTP',
        'font': 'Seance Note/Menu bitmap derivatives, Deborah Khodanovich 2025, SIL OFL 1.1; assets/fonts/README.md',
        'geometry': {'wave_box': WAVE_BOX, 'curve_window': [X0, X1, Y0, Y1], 'zero_y': ZERO},
        'checks': dict(raster_checks, outside_wave_box_pixel_identical=True,
                       upper_reused_without_changes=True, labels_and_hits_identical_to_lower=True,
                       labels_and_hits_separately_in_bounds_nonoverlapping=True,
                       exact_palette_native_and_2x=True, nearest_2x_roundtrip_and_png_readback=True,
                       all_199_depths_ink_pixel_invariant=True, state_combinations=combinations_checked,
                       all_six_modes_nine_phases=True, all_six_divisions=True,
                       random_and_follow_no_fake_marker=True, comparison_original02_pixel_exact=True,
                       protected_hashes_unchanged=True, protected_file_count=len(before),
                       prior_tools_and_artifacts_unchanged=True, prior_file_count=len(prior)),
        'protected_sha256': before, 'prior_sha256': prior,
        'frames': frames, 'files': saved, 'limitations': limitations,
    }
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    (OUT / 'README.txt').write_text(__doc__ + '\n'
        'run: python3 tools/render_sigils_thread_v2.py\n'
        'main_bottom_native.png: native 320x240, depth +99 / default State().\n'
        'main_dual_2x.png: unchanged upper paired with the new lower via pair().\n'
        'comparison_2x.png: exact current 02 versus v2, same original a/b/c/d page frame.\n'
        'all_frames_2x.png: four depths and step/rand/smooth/follow.\n'
        'every lower/top/dual and sheet has native plus exact nearest 2x output.\n'
        'changes: column fill; three zero ticks; depth ghost; +/-99 clip guides;\n'
        '24-interval one-pixel comet + hollow bead; matching one-pixel axis tick.\n\n'
        'limitations:\n- ' + '\n- '.join(limitations) + '\n')
    print(OUT / 'main_dual_2x.png')
    print(OUT / 'comparison_2x.png')
    print(f'pass: {len(fixtures)} fixtures, 199 depths, {combinations_checked} state combinations, '
          f'{len(saved)} PNGs; frame/hits/palette/nearest2x/protected hashes unchanged')


if __name__ == '__main__':
    main()
