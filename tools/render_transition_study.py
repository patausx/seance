#!/usr/bin/env python3
"""Authored transition-page layout only, NOT runtime / audio / hardware output.

Two lower LCD examples: neutral FX at 50%, then holding echo at 72% transition.
Native menu paper, 6x8 text, existing gothic digits/headings; no new font assets.
"""
from pathlib import Path
import hashlib
import json
from PIL import Image
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import INK, SPOT
from render_project_clock_study import DIGITS
from render_upper_menu_study import reserve

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/transition_study'
PROTECTED = (
    'core/engine/engine.h', 'core/engine/engine.cpp', 'core/engine/units.cpp',
    'core/engine/patch_io.cpp', 'platform/3ds/ui_performance.cpp',
    'platform/3ds/ui_sequencer.cpp', 'platform/3ds/ui_top.cpp',
    'platform/3ds/main.cpp', 'platform/3ds/note_display_data.h',
    'core/ui/font.cpp', 'assets/fonts/Gossip-MedPixel.otf', '.local-state/project.toml',
    'release/sequence-scope/seance.3dsx',
)


def identity():
    return {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in PROTECTED}


def bank(c, cx, value, color):
    mask, _ = DIGITS[str(value)]
    enlarged = mask.resize((mask.width * 2, mask.height * 2), Image.Resampling.NEAREST)
    x, y = cx - enlarged.width // 2, 41
    reserve(c, (x, y, x + enlarged.width, y + enlarged.height), 'bank ' + str(value))
    c.im.paste(color, (x, y), enlarged)
    c.text(cx - 12, 29, 'bank', GHOST)
    c.text(cx - 12, 115, 'hold', GHOST)


def diamond(c, x, y, color):
    c.d.polygon(((x, y - 4), (x + 4, y), (x, y + 4), (x - 4, y)), fill=color)


def lower(morph, echo):
    c = Page()
    c.text(8, 8, 'perform', INK)
    c.text(170, 8, 'mixer', GHOST)
    c.text(224, 8, 'transition', SPOT)
    c.line((224, 20, 283, 20), SPOT)
    c.hit('return to existing mixer; no automatic apply/capture', (160, 0, 216, 25))

    bank(c, 36, 1, INK)
    bank(c, 276, 3, SPOT)
    c.hit('hold left endpoint; physical up/down proposes bank 1..4', (8, 26, 68, 124))
    c.hit('hold right endpoint; physical up/down proposes bank 1..4', (244, 26, 312, 124))
    for x, word, box in (
        (84, 'rhythm', (76, 28, 130, 59)),
        (143, 'sound', (132, 28, 182, 59)),
        (195, 'both', (184, 28, 236, 59)),
    ):
        c.text(x, 41, word, SPOT if word == 'both' else GHOST)
        if word == 'both':
            c.line((x, 53, x + 23, 53), SPOT)
        c.hit('proposed morph scope ' + word, box)

    value = str(morph) + '%'
    c.text(160 - len(value) * 6, 73, value, SPOT, scale=2)
    c.text(124, 100, 'a > b linked', GHOST)
    # Position only, never equal-power audio weights or a live mixed pattern.
    c.line((20, 134, 300, 134), BRANCH)
    thumb = round(20 + 280 * morph / 100)
    c.line((20, 134, thumb, 134), SPOT)
    for x in (20, 90, 160, 230, 300):
        c.line((x, 131, x, 137), BRANCH)
    c.d.rectangle((thumb - 3, 128, thumb + 3, 140), fill=SPOT)
    c.line((thumb, 130, thumb, 138), INK)
    c.hit('relative morph rail; touch selects without jumping', (8, 124, 312, 146))

    # Three fixed 96x68 gesture areas. Open layout, no pad/card backgrounds.
    # Readouts are authored control positions, not Hz/IR/PCM/tail estimates.
    for i, (name, value) in enumerate((
        ('filter', 'dry'), ('echo', f'1/4 / {echo}%'), ('roll', 'off / 1/16'),
    )):
        x = 8 + i * 104
        active = name == 'echo' and echo > 0
        color = SPOT if active else INK
        c.heading(x + 8, 153, name, color)
        c.text(x + 8, 183, value, color if active else GHOST)
        x0, x1, y = x + 8, x + 88, 203
        c.line((x0, y, x1, y), BRANCH)
        if name == 'filter':
            for at in (x0, x + 48, x1):
                c.line((at, y - 3, at, y + 3), GHOST)
            diamond(c, x + 48, y, INK)
        elif name == 'echo':
            at = round(x0 + (x1 - x0) * echo / 100)
            if echo:
                c.line((x0, y, at, y), SPOT)
                c.line((x + 8, 145, x + 88, 145), SPOT)
            diamond(c, at, y, color)
        else:
            # Three discrete rhythmic positions, selected division stays visible
            # while the pad is released; off means no current roll application.
            for at in (x0, x + 48, x1):
                c.line((at, y - 3, at, y + 3), BRANCH)
            c.d.rectangle((x + 46, y - 2, x + 50, y + 2), outline=GHOST)
        c.hit('hold + horizontal relative slide / temporary ' + name,
              (x, 148, x + 96, 216))
    c.text(8, 229, 'release: echo tail' if echo else 'hold + slide / temp', GHOST)
    c.text(312, 229, 'release fx', SPOT, right=True)
    c.hit('release temporary FX only; preserve morph and mixer levels', (232, 218, 312, 240))
    return c


def main():
    before = identity()
    OUT.mkdir(parents=True, exist_ok=True)
    cases = (('01_transition', 50, 0), ('02_echo_held', 72, 60))
    frames = []
    expected_hits = None
    allowed = set(paper(320).get_flattened_data()) | {INK, SPOT, GHOST, BRANCH}
    for name, morph, echo in cases:
        c = lower(morph, echo)
        if expected_hits is None:
            expected_hits = c.hits
        assert c.hits == expected_hits
        assert set(c.im.get_flattened_data()) <= allowed
        c.im.save(OUT / f'{name}_bottom.png')
        enlarged = c.im.resize((640, 480), Image.Resampling.NEAREST)
        assert enlarged.resize(c.im.size, Image.Resampling.NEAREST).tobytes() == c.im.tobytes()
        enlarged.save(OUT / f'{name}_bottom_2x.png')
        frames.append({'name': name, 'morph_position_percent': morph,
                       'echo_amount_percent': echo, 'labels': c.labels,
                       'proposed_hit_regions': c.hits})
    assert before == identity()
    (OUT / 'verification.json').write_text(json.dumps({
        'origin': 'two authored Pillow lower LCD layouts; NOT native runtime/hardware captures',
        'scope': 'separate transition page next to unchanged performance mixer',
        'native_size': [320, 240],
        'fixture': 'bank1 to bank3, both rhythm and sound selected; compatible sound scenes a/b assumed',
        'semantics': 'banks contain patterns, not independent instruments/scenes; percentage is gesture position, not audio gain',
        'checks': ['native label bounds/nonoverlap', 'proposed hit bounds/nonoverlap',
                   'identical fixed touch regions in both states', 'native palette and exact nearest2x',
                   'protected runtime/font/artifact hashes unchanged'],
        'limits': ['no UI/audio/scheduler/persistence implementation',
                   'no computed intermediate rhythm or effect response',
                   'single-touch LCD: one pad at a time; morph stays at its previous setting',
                   'endpoint commit/cancel, mode switching, unequal lengths/directions/locks remain unresolved',
                   'temporary master FX placement, release ramps/tail and navigation lifecycle still need implementation design',
                   'no runtime tests/build/deployment or hardware readability/GPU-cost claim'],
        'preserved_source_sha256': before, 'frames': frames,
    }, indent=2) + '\n')
    print('pass: two authored 320x240 lower layouts; native fonts/paper, fixed nonoverlapping targets and labels')
    print('13 runtime/font/config/artifact identities unchanged; no runtime/audio implementation')
    print(OUT)


if __name__ == '__main__':
    main()
