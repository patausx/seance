#!/usr/bin/env python3
"""Render the actual C++ sequencer, not a separately authored mockup.

Runs the real controller regression binary with --dump, converts its 400x240 top
and 320x240 bottom software Citro2D frames to PNG, assembles native dual screens
and an outside-the-screen contact sheet.
The real ui_theme.cpp / core font / draw.cpp produce every screen pixel. Lines
use the documented host approximation of GPU coverage, not a hardware capture.
No network, image generation API, or runtime assets are needed. Requires Pillow.

  python3 tools/render_sequencer_runtime.py
  python3 tools/render_sequencer_runtime.py --no-build --binary /tmp/seance_sequencer_test
"""
from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("/tmp/seance_sequencer_test"))
    parser.add_argument("--output", type=Path, default=ROOT / "ui_mockups/sequencer_runtime")
    parser.add_argument("--no-build", action="store_true")
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    if not args.no_build:
        sources = [
            ROOT / "tools/sequencer_test.cpp",
            ROOT / "platform/3ds/ui_sequencer.cpp",
            ROOT / "platform/3ds/ui_top.cpp",
            ROOT / "platform/3ds/ui_theme.cpp",
            ROOT / "platform/3ds/menu_style.cpp",
            ROOT / "core/ui/draw.cpp",
            ROOT / "core/ui/font.cpp",
            *sorted((ROOT / "core/engine").glob("*.cpp")),
            ROOT / "core/synth/sampler.cpp",
            ROOT / "core/synth/wav_loader.cpp",
            ROOT / "core/synth/wav_writer.cpp",
        ]
        subprocess.run([
            "g++", "-O2", "-std=gnu++17", "-Wall", "-Wextra",
            "-Wl,--wrap=_ZN6seance2ui4textEiiPKcji",
            f"-I{ROOT / 'tools/sequencer_test_stubs'}", f"-I{ROOT / 'core'}",
            *map(str, sources), "-o", str(binary),
        ], cwd=ROOT, check=True)
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(binary), "--dump", str(output)], cwd=ROOT, check=True)
    with (output / "manifest.tsv").open() as stream:
        frames = list(csv.DictReader(stream, delimiter="\t"))
    font = ImageFont.load_default()
    cols, scale, margin, caption = 4, 1, 20, 28
    tile_w, tile_h = 400 * scale, 496 * scale
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (tile_w + margin) + margin,
                             rows * (tile_h + caption + margin) + margin + 40), "#f1eced")
    draw = ImageDraw.Draw(sheet)
    draw.text((margin, margin), "actual c++ sequencer / software citro2d / not a hardware capture", fill="#251a25", font=font)
    for index, frame in enumerate(frames):
        stem = frame["frame"]
        with Image.open(output / f"{stem}.ppm") as source:
            image = source.convert("RGB")
        with Image.open(output / f"{stem}_top.ppm") as source:
            top = source.convert("RGB")
        assert image.size == (320, 240) and top.size == (400, 240)
        image.save(output / f"{stem}.png")
        image.resize((640, 480), Image.Resampling.NEAREST).save(output / f"{stem}_2x.png")
        top.save(output / f"{stem}_top.png")
        top.resize((800, 480), Image.Resampling.NEAREST).save(output / f"{stem}_top_2x.png")
        dual = Image.new("RGB", (400, 496), "#302430")
        dual.paste(top, (0, 0))
        dual.paste(image, (40, 256))
        dual.save(output / f"{stem}_dual.png")
        dual.resize((800, 992), Image.Resampling.NEAREST).save(output / f"{stem}_dual_2x.png")
        enlarged = dual.resize((tile_w, tile_h), Image.Resampling.NEAREST)
        x = margin + (index % cols) * (tile_w + margin)
        y = margin + 40 + (index // cols) * (tile_h + caption + margin)
        draw.text((x, y), f"{stem.replace('_', ' ')} / {frame['shared_objects']} total objects", fill="#251a25", font=font)
        sheet.paste(enlarged, (x, y + caption))
    sheet.save(output / "sequencer_runtime_contact.png")
    metadata = {
        "provenance": "actual ui_sequencer.cpp + ui_top.cpp + ui_theme.cpp + core/ui/draw.cpp + core/ui/font.cpp; host primitive raster, not hardware",
        "bottom_resolution": [320, 240],
        "top_resolution": [400, 240],
        "dual_resolution": [400, 496],
        "max_objects_shared_both_displays": 8192,
        "peak_bottom_objects": max(int(frame["objects"]) for frame in frames),
        "peak_top_objects": max(int(frame["top_objects"]) for frame in frames),
        "peak_shared_objects": max(int(frame["shared_objects"]) for frame in frames),
        "geometry": "per-surface text ink bounds in *_text.tsv; original real text implementation invoked by linker wrap; overlaps count bounding boxes, not arbitrary intentional primitive overlap",
        "frames": frames,
    }
    (output / "render_report.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(output / "sequencer_runtime_contact.png")


if __name__ == "__main__":
    main()
