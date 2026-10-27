#!/usr/bin/env python3
"""Package REAL C++ Slice framebuffer captures; no UI pixels are drawn in Python.

The portable Citro2D shim executes the real ui_sequencer/ui_top/menu_style/font.
Lines use the documented host approximation, not a hardware/LCD capture. Each
upper/lower pair is drawn with g_eng=nullptr from one bounded pre-draw capture.
Use --compare OUTPUT to require byte-exact normal vs sanitized native PPMs.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    "01_four_cuts", "02_eight_cuts", "03_sixteen_cuts", "04_reverse_cut",
    "05_reverse_negative_forward", "06_uneven_tiny_sparse", "07_missing_tiny_slice",
    "08_base_slice", "09_start_length_window", "10_lock_full", "11_muted",
    "12_eighth_mode_chooser", "13_hold_l", "14_page_four_step64", "15_no_chops",
    "16_no_sample", "17_missing_sample_slot", "18_masked_play_no_source", "19_bypassed_play",
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def text_audit(path: Path, width: int) -> dict:
    with path.open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    for row in rows:
        x, y, w, h = (int(row[k]) for k in ("x", "y", "w", "h"))
        assert x >= 0 and y >= 0 and x + w <= width and y + h <= 240, (path, row)
    overlap = []
    for i, a in enumerate(rows):
        ax, ay, aw, ah = (int(a[k]) for k in ("x", "y", "w", "h"))
        for b in rows[i + 1:]:
            bx, by, bw, bh = (int(b[k]) for k in ("x", "y", "w", "h"))
            if ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by:
                overlap.append([a["text"], b["text"]])
    assert not overlap, (path, overlap)
    return {"records": len(rows), "out_of_bounds": 0, "overlaps": 0}


def contact(duals: dict[str, Image.Image], names: list[str], scale: int, columns: int) -> Image.Image:
    # Labels/gaps are outside each untouched native framebuffer rectangle.
    margin, caption = 16, 24
    tw, th = 400 * scale, 496 * scale
    rows = (len(names) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * (tw + margin) + margin, rows * (th + caption + margin) + margin), "#f1eced")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()
    for n, stem in enumerate(names):
        x = margin + n % columns * (tw + margin)
        y = margin + n // columns * (th + caption + margin)
        draw.text((x, y), stem.replace("_", " "), fill="#37283d", font=font)
        enlarged = duals[stem].resize((tw, th), Image.Resampling.NEAREST)
        sheet.paste(enlarged, (x, y + caption))
        assert sheet.crop((x, y + caption, x + tw, y + caption + th)).tobytes() == enlarged.tobytes()
    return sheet


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("/tmp/seance_sequencer_slices_test"))
    parser.add_argument("--output", type=Path, default=ROOT / "ui_mockups/sequencer_slice_runtime")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--compare", type=Path, help="require identical native PPMs from another build (e.g. normal/sanitized)")
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    if not args.no_build:
        subprocess.run(["make", "test-sequencer-slices"], cwd=ROOT, check=True)
        assert binary.is_file(), "use --no-build with a nondefault binary"
    output.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(binary), "--dump-slices", str(output)], cwd=ROOT, check=True, text=True, capture_output=True)
    (output / "capture.log").write_text(result.stdout + result.stderr)
    with (output / "manifest.tsv").open() as stream:
        frames = list(csv.DictReader(stream, delimiter="\t"))
    assert {f["frame"] for f in frames} == EXPECTED
    by_name = {f["frame"]: f for f in frames}
    for name, cuts in (("01_four_cuts", 4), ("02_eight_cuts", 8), ("03_sixteen_cuts", 16)):
        assert int(by_name[name]["valid_mask"]) == (1 << cuts) - 1
    assert by_name["04_reverse_cut"]["reverse"] == "1"
    assert by_name["05_reverse_negative_forward"]["reverse"] == "0"
    assert by_name["07_missing_tiny_slice"]["requested"] == "4" and by_name["07_missing_tiny_slice"]["applied"] == "0"
    assert by_name["08_base_slice"]["locked"] == "0" and by_name["09_start_length_window"]["requested"] == "0"
    assert by_name["15_no_chops"]["loaded"] == "1" and by_name["15_no_chops"]["valid_mask"] == "0"
    for name in ("16_no_sample", "17_missing_sample_slot", "18_masked_play_no_source", "19_bypassed_play"):
        assert by_name[name]["loaded"] == "0" and by_name[name]["pcm_reads"] == "0"
    duals, surfaces, counts = {}, {}, []
    for frame in frames:
        stem = frame["frame"]
        for key in ("clipped_primitives", "top_clipped_primitives", "text_overlaps", "top_text_overlaps"):
            assert int(frame[key]) == 0, (stem, key, frame[key])
        assert 0 < int(frame["shared_objects"]) < 8192
        assert int(frame["shared_objects"]) == int(frame["objects"]) + int(frame["top_objects"])
        assert int(frame["frame_bytes"]) <= 1024
        assert 0 <= int(frame["pcm_reads"]) <= 352 * 8 * 2
        images = {}
        for top, width in ((False, 320), (True, 400)):
            name = stem + ("_top" if top else "")
            ppm = output / (name + ".ppm")
            with Image.open(ppm) as source:
                image = source.convert("RGB")
            assert image.size == (width, 240)
            image.save(output / (name + ".png"))
            enlarged = image.resize((width * 2, 480), Image.Resampling.NEAREST)
            enlarged.save(output / (name + "_2x.png"))
            assert enlarged.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
            # Integer paper oracle audits edge pixels; it never paints a screen.
            denominator = 239 ** 3
            for y in range(240):
                n = y * y * (717 - 2 * y)
                paper = tuple((a * denominator + (b - a) * n + denominator // 2) // denominator
                              for a, b in ((228, 211), (194, 187), (207, 206)))
                assert image.getpixel((width - 1, y)) == paper, (name, y)
            audit = text_audit(output / (name + "_text.tsv"), width)
            surfaces[name] = {"sha256_ppm": digest(ppm), "size": list(image.size), "text": audit}
            if args.compare:
                other = args.compare.resolve() / ppm.name
                assert other.is_file() and digest(other) == digest(ppm), ("native mismatch", ppm, other)
            images[top] = image
        dual = Image.new("RGB", (400, 496), "#302430")
        dual.paste(images[True], (0, 0)); dual.paste(images[False], (40, 256))
        assert dual.crop((0, 0, 400, 240)).tobytes() == images[True].tobytes()
        assert dual.crop((40, 256, 360, 496)).tobytes() == images[False].tobytes()
        dual.save(output / (stem + "_dual_native.png"))
        dual.resize((800, 992), Image.Resampling.NEAREST).save(output / (stem + "_dual_2x.png"))
        duals[stem] = dual
        counts.append(int(frame["shared_objects"]))
    contact(duals, sorted(duals), 1, 4).save(output / "gallery_native.png")
    comparison = ["01_four_cuts", "03_sixteen_cuts", "06_uneven_tiny_sparse", "12_eighth_mode_chooser"]
    contact(duals, comparison, 2, 2).save(output / "comparison_2x.png")
    report = {
        "provenance": "actual C++ ui_sequencer.cpp/ui_top.cpp/menu_style.cpp/core ui font and draw; software Citro2D raster, not LCD/PICA capture",
        "screen_pixels_drawn_in_python": False,
        "capture_contract": "one frozen pre-GPU DTO per pair; g_eng=nullptr during both draws; tests separately remove PCM storage before redraw",
        "frame_pairs": len(frames), "native_surfaces": len(surfaces),
        "bounds": "all actual primitive and wrapped raw native-text bounds in-bounds; zero text box intersections, no alpha/visibility waiver",
        "pcm_fixture": "real deterministic eight varied attacks with quadratic decays, identically cut into four/eight/sixteen intervals; not a waveform drawing or loaded external asset",
        "shared_primitive_budget": 8192, "peak_shared_primitives": max(counts),
        "peak_top_primitives": max(int(f["top_objects"]) for f in frames),
        "peak_bottom_primitives": max(int(f["objects"]) for f in frames),
        "frame_bytes": max(int(f["frame_bytes"]) for f in frames),
        "wave_scan": {"columns": 352, "probes_per_column": 8, "max_channels": 2,
                      "max_possible_pcm_reads": 5632, "peak_pcm_reads": max(int(f["pcm_reads"]) for f in frames),
                      "meaning": "bounded sampled envelope, not exact full-file peaks"},
        "nearest_scaling_and_assembly": "byte-exact native crops / integer nearest 2x verified",
        "normal_sanitized_comparison": str(args.compare.resolve()) if args.compare else None,
        "frames": frames, "surfaces": surfaces,
    }
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"output": str(output), "pairs": len(frames), "peak_shared": max(counts), "compared": bool(args.compare)}))


if __name__ == "__main__":
    main()
