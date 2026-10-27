#!/usr/bin/env python3
"""One static menu-over-sequencer composition using an existing host frame.

This does not run the C++ renderer or claim a new device/runtime capture.
Clock digits remain the illustrative 012:03 from the prior design proposal.
"""
from pathlib import Path
from PIL import Image
from render_go_to_overlay_study import dim
from render_go_to_unified_study import upper, lower
from render_main_screen_centered_study import caption, OUTSIDE
from render_main_screen_unified_info_study import blur

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/go_to_overlay_study'
SOURCE = ROOT / ('ui_mockups/playhead_press_runtime/ui/'
                 'run-20260912T204915.032802Z/gray_play_1_free.png')


def main():
    with Image.open(SOURCE) as source:
        source = source.convert('RGB')
        assert source.size == (400, 488), 'expected native dual host frame with 8px gap'
        top_source = source.crop((0, 0, 400, 240))
        bottom_source = source.crop((40, 248, 360, 488))
    # Use the native host pixels before applying the common background effect;
    # adapt only the presentation gap. No re-authored sequence grid.
    shared = Image.new('RGB', (400, 496), (36, 36, 36))
    shared.paste(top_source, (0, 0))
    shared.paste(bottom_source, (40, 256))
    # Approved study recipe: half-size BOX, separable [1,2,1], 24% paper,
    # bilinear restore. One common blur/veil; sharp foreground is drawn later.
    shared = dim(blur(shared))
    top = upper(shared.crop((0, 0, 400, 240)), 'center')
    bottom = lower(shared.crop((40, 256, 360, 496)), current='sequencer')
    sheet = Image.new('RGB', (432, 590), OUTSIDE)
    caption(sheet, 16, 12, 'navigation / opened from sequencer')
    caption(sheet, 16, 29, 'uniform background blur / sharp navigation')
    sheet.paste(top, (16, 48))
    sheet.paste(bottom, (56, 304))
    caption(sheet, 16, 557, 'same blur + shade / no local text halos')
    caption(sheet, 16, 575, 'static composite / example clock / not runtime')
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / 'go-to-from-sequencer-blur-2x.png'
    sheet.resize((864, 1180), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
