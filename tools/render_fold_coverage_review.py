#!/usr/bin/env python3
"""One review sheet of existing Fold/Strain UI, not a Fold 2.0 design.

Reuses archived 2026-09-08 host captures without altering their screen pixels.
Only captions/gap/placement are new. Does not compile, capture, or run tests.
"""
from pathlib import Path
from PIL import Image
from render_module_geometry_study import Canvas
from render_menu_style_study import heading_mask

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/fold_coverage_review/fold-and-strain-2x.png'
INK = (244, 244, 244)
MUTED = (175, 170, 182)
ACCENT = (181, 154, 232)


def main():
    c = Canvas(848, 644, (0, 0, 0))
    title = heading_mask('fold / strain')
    c.im.paste(INK, (16, 13), title)
    c.text(832, 22, 'seance / existing modules', MUTED, right=True)
    c.text(16, 53, 'fold / shape & morph', ACCENT)
    c.text(432, 53, 'strain / saturation & feedback', ACCENT)
    c.text(16, 71, 'drive + wet trim + bias + mix', MUTED)
    c.text(432, 71, 'drive + teeth + body + bias + fdbk + mix', MUTED)

    sources = (
        ('fold_runtime/01_fold_dual_native.png', 16),
        ('strain_runtime/01_default_dual_native.png', 432),
    )
    for relative, x in sources:
        with Image.open(ROOT / 'ui_mockups' / relative) as source:
            source = source.convert('RGB')
            assert source.size == (400, 480), relative
            top = source.crop((0, 0, 400, 240))
            bottom = source.crop((0, 240, 400, 480))
            c.im.paste(top, (x, 92))
            c.im.paste(bottom, (x, 344))
            # Keep captured app pixels exactly; the extra 12px gap is external.
            assert c.im.crop((x, 92, x+400, 332)).tobytes() == top.tobytes()
            assert c.im.crop((x, 344, x+400, 584)).tobytes() == bottom.tobytes()

    c.text(16, 599, 'fold > wrap > diode / continuous morph', ACCENT)
    c.text(432, 599, 'soft saturation / pre-post tone / dc blocking', ACCENT)
    c.text(16, 618, 'saved host captures: 08 sep 2026 / not a new build or a fold 2.0 mockup', MUTED)
    c.text(16, 632, 'different reference inputs; plots are not live audio or an a/b sound comparison', MUTED)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    c.im.resize((1696, 1288), Image.Resampling.NEAREST).save(OUT)
    print(OUT)


if __name__ == '__main__':
    main()
