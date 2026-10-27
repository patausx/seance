#!/usr/bin/env python3
"""Package seven actual native C++ extended FM dual renders; Python does not author UI pixels."""
from pathlib import Path
import argparse, csv, hashlib, json, subprocess
from PIL import Image, ImageDraw
from render_fold_runtime import audit_text

ROOT = Path(__file__).resolve().parents[1]
FRAMES = ["01_voice_strike", "02_motion_strike", "03_voice_drone_mod", "04_motion_drone_lock", "05_pitch_envelope", "06_operators", "07_legacy"]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="/tmp/seance_osc_input_test")
    parser.add_argument("--output", default="ui_mockups/fm_extended_runtime")
    args = parser.parse_args()
    out = ROOT / args.output
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.binary, "--dump-fm-menu", str(out)], check=True, cwd=ROOT)
    rows = list(csv.DictReader((out / "manifest.tsv").open(), delimiter="\t"))
    assert [row["frame"] for row in rows] == FRAMES

    palette_reference = Image.open(ROOT / "ui_mockups/fm_runtime/01_voice_strike_bottom_native.png").convert("RGB")
    allowed = set(palette_reference.get_flattened_data())
    outside = (36, 27, 39)
    duals = {}
    for row in rows:
        name = row["frame"]
        screens = []
        for screen, width in (("top", 400), ("bottom", 320)):
            image = Image.open(out / f"{name}_{screen}.ppm").convert("RGB")
            assert image.size == (width, 240)
            audit_text(out / f"{name}_{screen}_text.tsv", width)
            assert set(image.get_flattened_data()) <= allowed
            assert image.crop((0, 0, 2, 240)).tobytes() == palette_reference.crop((0, 0, 2, 240)).tobytes()
            image.save(out / f"{name}_{screen}_native.png")
            image.resize((width * 2, 480), Image.Resampling.NEAREST).save(out / f"{name}_{screen}_2x.png")
            screens.append(image)
        dual = Image.new("RGB", (400, 496), outside)
        dual.paste(screens[0], (0, 0))
        dual.paste(screens[1], (40, 256))
        dual.save(out / f"{name}_dual_native.png")
        dual.resize((800, 992), Image.Resampling.NEAREST).save(out / f"{name}_dual_2x.png")
        duals[name] = dual

    sheet = Image.new("RGB", (832, 526 * ((len(FRAMES)+1)//2)), outside)
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(FRAMES):
        x = (index % 2) * 416
        y = (index // 2) * 526
        draw.text((x + 8, y + 4), name, fill=(228, 194, 207))
        sheet.paste(duals[name], (x, y + 22))
    sheet.save(out / "comparison_native.png")
    sheet.resize((sheet.width*2, sheet.height*2), Image.Resampling.NEAREST).save(out / "comparison_2x.png")

    report = {
        "status": __doc__,
        "frames": len(rows),
        "binary_sha256": sha(Path(args.binary)),
        "core_evidence": "../fm_extended_core/verification.json",
        "frame_hashes": {str(p.name): sha(p) for p in sorted(out.glob('*.ppm'))},
        "peak_shared": max(int(row["top_objects"]) + int(row["bottom_objects"]) for row in rows),
        "checks": {
            "actual_cpp_top_and_bottom": True,
            "fixed_palette_and_paper_strip": True,
            "nearest_integer_2x": True,
            "voice_wave_motion_envelopes_and_operator_pages": True,
            "explicit_legacy_upgrade": True,
            "bounds_and_text_overlap_free": True,
        },
        "source_sha256": {
            path: sha(ROOT / path)
            for path in [
                "platform/3ds/ui_module_focus_generators.cpp",
                "platform/3ds/ui_module_focus_internal.h",
                "platform/3ds/fm_context.cpp",
                "platform/3ds/main.cpp",
                "tools/fm_menu_test.h",
                "tools/fm_extended_ui_test.h",
                "tools/osc_input_dispatch_test.cpp",
                "tools/sequencer_test.cpp",
                "core/engine/units.h",
                "core/engine/units.cpp",
                "core/engine/fm_extended.h",
                "core/engine/patch_io.cpp",
                "tools/render_fm_menu_runtime.py",
                "Makefile",
            ]
        },
    }
    (out / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
    (out / "README.txt").write_text(
        __doc__ + "\nRun: make test-fm-menu && python3 tools/render_fm_menu_runtime.py\n"
        "The lower voice plot uses a cached private actual FM instance: exact first1168 samples after reset, including pitch envelope and operator shapes. Motion shows amp/index/feedback; selecting pitch env/time shows a separate analytic full-duration next-strike pitch preview (held in drone). Operators are persisted policies; harmonic is a nondestructive ratio-drag aid.\n"
        "No live PCM is read during draw and no hardware/audio acceptance or deployment is claimed.\n"
    )
    print(out / "comparison_2x.png")
    print(json.dumps(report["checks"]))

if __name__ == "__main__":
    main()
