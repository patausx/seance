#!/usr/bin/env python3
"""Audit and assemble actual C++ shared-cuts framebuffer pairs (never draw UI in Python).

Native Citro2D host raster is not an LCD/PICA capture. C++ draws both screens
from bounded immutable captured state with g_eng=nullptr. Raw text records have
no visibility/opacity waiver. --compare requires exact normal/sanitized PPMs.
"""
from __future__ import annotations
import argparse
import csv
import json
from pathlib import Path
import subprocess
from PIL import Image
from render_sequencer_slice_runtime import contact, digest, text_audit

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    "01_four_cuts", "02_eight_cuts", "03_sixteen_cuts", "04_reverse_selected",
    "05_sparse_stable_ids", "06_add_armed", "07_insert_hole", "08_replace_confirmation",
    "09_clear_confirmation", "10_no_cuts", "11_quiet_pcm", "12_digital_silence",
    "13_stereo_antiphase", "14_empty_sample", "15_missing_slot", "16_masked_play",
    "17_ring_from_cuts", "18_hold_l", "19_step64_selection",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("/tmp/seance_sequencer_cuts_test"))
    parser.add_argument("--output", type=Path, default=ROOT / "ui_mockups/sequencer_cuts_runtime")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--compare", type=Path)
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output.resolve()
    if not args.no_build:
        subprocess.run(["make", "test-sequencer-cuts"], cwd=ROOT, check=True)
    output.mkdir(parents=True, exist_ok=True)
    run = subprocess.run([str(binary), "--dump-cuts", str(output)], cwd=ROOT, capture_output=True, text=True)
    (output / "capture.log").write_text(run.stdout + run.stderr)
    run.check_returncode()
    with (output / "manifest.tsv").open() as stream:
        frames = list(csv.DictReader(stream, delimiter="\t"))
    assert {f["frame"] for f in frames} == EXPECTED
    by_name = {f["frame"]: f for f in frames}
    for n, name in ((4,"01_four_cuts"),(8,"02_eight_cuts"),(16,"03_sixteen_cuts")):
        assert int(by_name[name]["valid_mask"]) == (1 << n) - 1
    assert int(by_name["05_sparse_stable_ids"]["selected"]) == 16
    assert int(by_name["07_insert_hole"]["selected"]) == 2
    assert int(by_name["08_replace_confirmation"]["confirm"]) == 16
    assert int(by_name["09_clear_confirmation"]["confirm"]) == 255
    assert int(by_name["10_no_cuts"]["valid_mask"]) == 0
    assert int(by_name["11_quiet_pcm"]["wave_peak"]) >= 60
    assert int(by_name["12_digital_silence"]["wave_peak"]) == 0
    assert int(by_name["13_stereo_antiphase"]["pcm_reads"]) == 5632
    for name in ("14_empty_sample", "15_missing_slot", "16_masked_play"):
        assert by_name[name]["loaded"] == "0" and by_name[name]["pcm_reads"] == "0"
    duals, surfaces = {}, {}
    for frame in frames:
        stem = frame["frame"]
        assert 0 < int(frame["shared_objects"]) < 8192
        assert int(frame["shared_objects"]) == int(frame["objects"]) + int(frame["top_objects"])
        assert 0 <= int(frame["pcm_reads"]) <= 5632
        assert int(frame["frame_bytes"]) <= 1128
        images = {}
        for top, width in ((True,400),(False,320)):
            name = stem + ("_top" if top else "")
            ppm = output / (name + ".ppm")
            with Image.open(ppm) as source:
                image = source.convert("RGB")
            assert image.size == (width,240)
            audit = text_audit(output / (name + "_text.tsv"), width)
            # Audit exact shared paper at an untouched outer edge. Never paints UI.
            denominator = 239 ** 3
            for y in range(240):
                n = y*y*(717-2*y)
                paper = tuple((a*denominator+(b-a)*n+denominator//2)//denominator
                              for a,b in ((228,211),(194,187),(207,206)))
                assert image.getpixel((width-1,y)) == paper, (name,y)
            image.save(output / (name + ".png"))
            enlarged = image.resize((width*2,480),Image.Resampling.NEAREST)
            enlarged.save(output / (name + "_2x.png"))
            assert enlarged.resize(image.size,Image.Resampling.NEAREST).tobytes() == image.tobytes()
            if args.compare:
                other = args.compare.resolve() / ppm.name
                assert other.is_file() and digest(other) == digest(ppm), ("native mismatch",ppm,other)
            surfaces[name] = {"sha256_ppm":digest(ppm),"size":list(image.size),"raw_text":audit}
            images[top] = image
        dual = Image.new("RGB",(400,496),"#302430")
        dual.paste(images[True],(0,0));dual.paste(images[False],(40,256))
        assert dual.crop((0,0,400,240)).tobytes() == images[True].tobytes()
        assert dual.crop((40,256,360,496)).tobytes() == images[False].tobytes()
        dual.save(output / (stem + "_dual_native.png"))
        dual.resize((800,992),Image.Resampling.NEAREST).save(output / (stem + "_dual_2x.png"))
        duals[stem] = dual
    # Three standalone REAL PLAY lower captures additionally prove the moved header and
    # frozen adaptive waveform; PLAY keeps its own existing non-menu paper/material.
    with (output / "play_manifest.tsv").open() as stream:
        play_frames = list(csv.DictReader(stream, delimiter="\t"))
    assert {f["frame"] for f in play_frames} == {"20_play_quiet_pcm","21_play_silence","22_play_stereo_antiphase"}
    for frame in play_frames:
        name = frame["frame"]
        assert 0 < int(frame["objects"]) < 8192
        ppm = output / (name + ".ppm")
        with Image.open(ppm) as source:
            image = source.convert("RGB")
        assert image.size == (320,240)
        audit = text_audit(output / (name + "_text.tsv"),320)
        image.save(output / (name + ".png"))
        image.resize((640,480),Image.Resampling.NEAREST).save(output / (name + "_2x.png"))
        if args.compare:
            assert digest(args.compare.resolve() / ppm.name) == digest(ppm), name
        surfaces[name] = {"sha256_ppm":digest(ppm),"size":[320,240],"raw_text":audit}
    contact(duals,sorted(duals),1,4).save(output / "gallery_native.png")
    contact(duals,["02_eight_cuts","03_sixteen_cuts","05_sparse_stable_ids","11_quiet_pcm"],2,2).save(output / "comparison_2x.png")
    report = {
        "provenance":"actual C++ ui_sequencer/ui_top/menu_style/native font; software Citro2D raster, not hardware",
        "screen_pixels_drawn_in_python":False,
        "capture_contract":"one frozen capture; g_eng=nullptr during both renders; focused tests retire PCM storage before redraw",
        "frame_pairs":len(frames),"standalone_play_surfaces":len(play_frames),"native_surfaces":len(surfaces),
        "raw_text_bounds_and_intersections":"zero; strict unfiltered wrapped text boxes, no opacity/visibility waiver",
        "primitive_bounds":"zero clipped rectangles/lines enforced before each native dump",
        "peak_shared_primitives":max(int(f["shared_objects"]) for f in frames),
        "shared_primitive_budget":8192,
        "frame_bytes":max(int(f["frame_bytes"]) for f in frames),
        "pcm":"real fixture attacks/decays; quiet fixture is same PCM /32; silence all zero; stereo antiphase ±512",
        "wave_scan":{"columns":352,"probes_per_column":8,"channels_max":2,"max_reads":5632,
                     "peak_reads":max(int(f["pcm_reads"]) for f in frames),"meaning":"sampled extrema, not exact full-file peaks"},
        "normal_sanitized_comparison":str(args.compare.resolve()) if args.compare else None,
        "nearest_scaling_and_native_crop_assembly":"byte-exact",
        "frames":frames,"play_frames":play_frames,"surfaces":surfaces,
    }
    (output / "report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({"output":str(output),"pairs":len(frames),"peak_shared":report["peak_shared_primitives"],"compared":bool(args.compare)}))


if __name__ == "__main__":
    main()
