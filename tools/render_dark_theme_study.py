#!/usr/bin/env python3
"""Palette-only design study, NOT a new dark-theme runtime capture.

Recolors saved native C++ UI rasters by exact known color roles. No authored
screen geometry, resampling, text replacement, or fuzzy color classification.
Contact-sheet labels are outside the screen. Runtime/source captures stay intact.
"""
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/dark_theme_study'
SOURCES = {
    'osc_top': 'ui_mockups/osc_gothic_runtime/raw/02_tri_saw_mod_top.ppm',
    'osc_bottom': 'ui_mockups/osc_gothic_runtime/raw/02_tri_saw_mod_bottom.ppm',
    'go_to': 'ui_mockups/nav_ink_runtime/substrates/nav/01_approved_idle_bottom.ppm',
    'sequencer': 'ui_mockups/sequence_scope_runtime/sequence/01_live_scope.ppm',
}
VARIANTS = [('a', 'graphite', 36), ('b', 'gray', 60), ('c', 'ash', 84)]
INK, SPOT, GHOST, BRANCH, CELL = (31, 20, 31), (91, 49, 124), (126, 100, 119), (174, 145, 165), (242, 216, 226)
WHITE = (255, 255, 255)
MAT = (16, 16, 16)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def runtime_hashes():
    return {str(p.relative_to(ROOT)): sha(p) for parent in ('core', 'platform')
            for p in sorted((ROOT / parent).rglob('*')) if p.is_file()}


def gray(value):
    return (value,) * 3


def paper_colors():
    # Exact original menu_style.cpp integer gradient, including rounding.
    d = 239 ** 3
    return {tuple((a * d + (b - a) * y * y * (717 - 2 * y) + d // 2) // d
                  for a, b in ((228, 211), (194, 187), (207, 206)))
            for y in range(240)}


def recolor(source, bg):
    palette = {p: gray(bg) for p in paper_colors()}
    palette.update({INK: gray(244), SPOT: WHITE, GHOST: gray(184),
                    BRANCH: WHITE, CELL: gray(bg + 12)})
    pixels = list(source.get_flattened_data())
    unknown = set(pixels) - palette.keys()
    assert not unknown, f'unmapped colors: {unknown}'
    output = Image.new('RGB', source.size)
    mapped = [palette[p] for p in pixels]
    output.putdata(mapped)
    # Every original paper/nonpaper location is preserved, including rail holes.
    papers = paper_colors()
    assert all((old in papers) == (new == gray(bg)) for old, new in zip(pixels, mapped))
    assert all(r == g == b for r, g, b in mapped)
    assert all(new == WHITE for old, new in zip(pixels, mapped) if old == BRANCH)
    return output


def font(size):
    return ImageFont.truetype('/usr/share/fonts/TTF/IBMPlexSans-Regular.ttf', size)


def save_pair(image, stem):
    image.save(OUT / f'{stem}_native.png')
    image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST).save(OUT / f'{stem}_2x.png')


def contact(frames, rows, filename):
    # Screen pixels enlarge by exact integer 2x; all headings live in margins.
    pad, width, gap = 24, 640, 24
    header, row_title, height = 78, 38, 480
    canvas = Image.new('RGB', (pad * 2 + width * 3 + gap * 2,
                              header + len(rows) * (row_title + height + gap) + 38), MAT)
    draw = ImageDraw.Draw(canvas)
    for col, (key, name, bg) in enumerate(VARIANTS):
        x = pad + col * (width + gap)
        draw.text((x, 14), f'{key} / {name}', font=font(25), fill=WHITE)
        draw.text((x, 47), f'background #{bg:02x}{bg:02x}{bg:02x} / white tracks', font=font(18), fill=gray(170))
        for row, (source, title) in enumerate(rows):
            y = header + row * (row_title + height + gap)
            draw.text((x, y), title, font=font(20), fill=gray(200))
            canvas.paste(frames[key][source].resize((width, height), Image.Resampling.NEAREST), (x, y + row_title))
    draw.text((pad, canvas.height - 32), 'palette study / saved native ui geometry / flat neutral backgrounds / no runtime changes',
              font=font(18), fill=gray(150))
    canvas.save(OUT / filename)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    before = runtime_hashes()
    identities = {key: sha(ROOT / path) for key, path in SOURCES.items()}
    originals = {key: Image.open(ROOT / path).convert('RGB') for key, path in SOURCES.items()}
    frames = {}
    for key, name, bg in VARIANTS:
        frames[key] = {screen: recolor(image, bg) for screen, image in originals.items()}
        for screen, image in frames[key].items():
            save_pair(image, f'{key}_{name}_{screen}')
        dual = Image.new('RGB', (400, 488), MAT)
        dual.paste(frames[key]['osc_top'], (0, 0))
        dual.paste(frames[key]['osc_bottom'], (40, 248))
        save_pair(dual, f'{key}_{name}_osc_dual')
    contact(frames, [('osc_bottom', 'osc / controls + waveform')], '01_controls_comparison.png')
    contact(frames, [('go_to', 'go to'), ('sequencer', 'sequencer / live scope')], '02_navigation_comparison.png')
    assert before == runtime_hashes(), 'runtime files changed during preview'
    assert identities == {key: sha(ROOT / path) for key, path in SOURCES.items()}
    manifest = {
        'kind': 'palette-only design study over saved native C++ host captures',
        'not': ['dark theme implementation', 'fresh C++ dark-theme render', 'hardware contrast acceptance'],
        'sources': {key: {'path': path, 'sha256': identities[key]} for key, path in SOURCES.items()},
        'variants': [{'id': key, 'name': name, 'background': bg, 'cell': bg + 12} for key, name, bg in VARIANTS],
        'shared_palette': {'ink': 244, 'spot': 255, 'secondary_text_raw_wave': 184, 'tracks': 255},
        'checks': {'all_original_colors_explicitly_mapped': True, 'all_output_pixels_neutral_gray': True,
                   'paper_foreground_masks_exact': True, 'all_original_branch_pixels_white': True,
                   'runtime_files_unchanged': len(before), 'source_captures_unchanged': True},
        'outputs': {p.name: sha(p) for p in sorted(OUT.glob('*.png'))},
    }
    (OUT / 'verification.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'{OUT}: 3 palettes / 4 source screens / exact foreground masks / {len(before)} runtime files untouched')


if __name__ == '__main__':
    main()
