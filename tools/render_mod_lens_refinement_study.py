#!/usr/bin/env python3
"""static visual refinement of the existing mod/temp lens.

uses the actual current c++ vca modulation top frame and authors only a proposed
lower layout. no runtime, controller, routing, dsp, persistence, build or upload.
"""
from pathlib import Path
import hashlib
import json
from PIL import Image

from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import Canvas, INK, SPOT, OUTSIDE

ROOT = Path(__file__).resolve().parents[1]
CURRENT = ROOT / "ui_mockups/module_inspector_runtime"
RAW = CURRENT / "raw"
OUT = ROOT / "ui_mockups/mod_lens_refinement_study"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bead(c, x, y, color=SPOT, hollow=False):
    if hollow:
        c.d.rectangle((x - 2, y - 2, x + 2, y + 2), outline=color)
    else:
        c.d.rectangle((x - 2, y - 2, x + 2, y + 2), fill=color)


def rail(c, x0, x1, y, value, bipolar=False):
    c.line((x0, y, x1, y), BRANCH)
    if bipolar:
        center = (x0 + x1) // 2
        c.line((center, y - 4, center, y + 4), GHOST)
        x = center + round((x1 - x0) * value / 200)
        c.line((center, y, x, y), SPOT)
    else:
        x = x0 + round((x1 - x0) * value / 100)
        c.line((x0, y, x, y), SPOT)
    bead(c, x, y)


def lower(source="sg.b", depth=75, temporary=False):
    c = Page(320)
    c.heading(16, 7, "modulation")
    c.text(304, 17, "vca / atk 02/03", GHOST, right=True)

    c.text(18, 47, "<", GHOST)
    c.text(302, 47, ">", GHOST, right=True)
    c.text(94, 47, "attack / nominal time", INK)
    c.line((18, 61, 302, 61), BRANCH)

    # One complete route sentence: source -> destination -> signed amount.
    c.text(24, 72, source, SPOT)
    c.line((54, 77, 115, 77), BRANCH)
    c.marker(119, 77, SPOT)
    c.text(132, 72, "attack", INK)
    c.text(296, 72, f"{depth:+d}%", SPOT, right=True)
    c.text(24, 88, "source", GHOST)
    c.text(132, 88, "destination", GHOST)
    c.text(296, 88, "route depth", GHOST, right=True)

    base_text = "temp 582.06ms" if temporary else "base 269.16ms"
    c.text(24, 108, base_text, SPOT if temporary else INK)
    c.text(296, 108, "stored 269.16ms" if temporary else "parameter value", GHOST, right=True)
    rail(c, 24, 296, 129, 58 if temporary else 27)

    c.text(24, 146, "modulation depth", INK)
    c.text(296, 146, f"{depth:+d}% / bipolar", SPOT, right=True)
    rail(c, 24, 296, 166, depth, True)

    sources = ("off", "a", "b", "c", "d", "env")
    xs = (24, 77, 126, 175, 224, 272)
    active = source.split(".")[-1]
    for x, label in zip(xs, sources):
        chosen = label == active
        c.text(x, 185, label, SPOT if chosen else GHOST)
        if chosen:
            c.line((x - 2, 198, x + len(label) * 6 + 1, 198), SPOT)

    if source == "env":
        c.text(24, 208, "env c1 / body / unipolar", GHOST)
        c.text(296, 208, "tap env to configure", SPOT, right=True)
    elif temporary:
        c.text(24, 208, "release l / return to stored base", SPOT)
    else:
        c.text(24, 208, "hold l / audition temporary base", GHOST)
    c.text(20, 228, "b return", GHOST)
    c.text(300, 228, "drag rails / tap source", GHOST, right=True)
    return c.im


def dual(top, bottom):
    image = Image.new("RGB", (400, 480), OUTSIDE)
    image.paste(top, (0, 0))
    image.paste(bottom, (40, 240))
    return image


def save(image, name):
    image.save(OUT / f"{name}_native.png")
    image.resize((image.width * 2, image.height * 2), Image.Resampling.NEAREST).save(
        OUT / f"{name}_2x.png"
    )


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    current_top = Image.open(RAW / "vca_mod_top.ppm").convert("RGB")
    temp_top = Image.open(RAW / "vca_temp_top.ppm").convert("RGB")
    current_bottom = Image.open(RAW / "vca_mod_bottom.ppm").convert("RGB")

    current = dual(current_top, current_bottom)
    sigil = dual(current_top, lower())
    temp = dual(temp_top, lower(temporary=True))
    save(sigil, "01_sigil_route")
    save(temp, "02_temporary_base")

    sheet = Image.new("RGB", (1240, 516), OUTSIDE)
    labels = Canvas(1240, 36, OUTSIDE)
    labels.text(16, 14, "current / actual c++", (228, 194, 207))
    labels.text(430, 14, "proposal / sigil route", (228, 194, 207))
    labels.text(844, 14, "proposal / hold-l temporary", (228, 194, 207))
    sheet.paste(labels.im, (0, 0))
    for i, image in enumerate((current, sigil, temp)):
        sheet.paste(image, (414 * i, 36))
    save(sheet, "comparison")

    report = {
        "status": "static lower-screen proposal; not implemented or deployed",
        "actual_cpp_source": {
            "top": str(RAW / "vca_mod_top.ppm"),
            "bottom": str(RAW / "vca_mod_bottom.ppm"),
            "top_sha256": sha(RAW / "vca_mod_top.ppm"),
            "bottom_sha256": sha(RAW / "vca_mod_bottom.ppm"),
        },
        "retained_behavior": [
            "selected parameter and previous/next navigation",
            "editable physical base value and signed modulation depth",
            "off, sigil a-d and envelope follower sources",
            "hold-l temporary base audition and release restoration",
            "b return, relative gestures, route persistence and dirty state",
        ],
        "visual_changes": [
            "route reads as source to destination to signed amount",
            "gothic modulation heading and clearer selected parameter identity",
            "base and depth remain separate full-width controls",
            "source chooser remains one row with a stronger selected state",
            "envelope chain/response remains on the same page but appears contextually only for env",
            "temporary state keeps the stored return value visible",
        ],
        "explicit_non_changes": [
            "no inline overlay or new hold gesture",
            "no actual blur, framebuffer effect or new animation",
            "no sigil screen, dsp, engine, route law, storage or module sound changes",
        ],
    }
    (OUT / "proposal.json").write_text(json.dumps(report, indent=2) + "\n")
    print(OUT / "comparison_2x.png")


if __name__ == "__main__":
    main()
