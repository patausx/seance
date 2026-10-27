#!/usr/bin/env python3
"""Author three bounded GO TO motion studies from existing native C++ captures.

This is deliberately an offline compositor: it does not invoke a build, redraw UI,
or modify runtime sources. Every settled top/bottom endpoint is copied pixel-exact
from an existing 400x240 + 320x240 software-raster capture. Intermediate frames use
only integer clipping/translation with one fixed palette; no alpha, scaling or blur.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "ui_mockups/navigation_motion_study"
CARBON = (31, 20, 31)
VIOLET = (91, 49, 124)
FRAMES = 10
BLACKLETTER_FRAMES = 11
BLACKLETTER_STEMS = (24, 33, 42, 51, 57, 66)

TARGETS = (
    ("chains", 0, 69,
     ROOT / "ui_mockups/menu_stage1_runtime/chains_four_dual_native.png"),
    ("sigils", 1, 91,
     ROOT / "ui_mockups/sigils_menu_runtime/main_dual_native.png"),
    ("sequencer", 2, 113,
     ROOT / "ui_mockups/sequence_scope_runtime/sequence_01_live_scope_dual.png"),
    ("perform", 3, 135,
     ROOT / "ui_mockups/performance_feedback_runtime/mixer_01_capture_half_peak_hold_dual.png"),
)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def smooth(t: float) -> float:
    return t * t * (3.0 - 2.0 * t)


def surfaces(dual: Image.Image) -> tuple[Image.Image, Image.Image]:
    assert dual.size == (400, 496)
    return dual.crop((0, 0, 400, 240)), dual.crop((40, 256, 360, 496))


def compose(top: Image.Image, bottom: Image.Image) -> Image.Image:
    assert top.size == (400, 240) and bottom.size == (320, 240)
    dual = Image.new("RGB", (400, 496), CARBON)
    dual.paste(top, (0, 0))
    dual.paste(bottom, (40, 256))
    return dual


def slide_surface(old: Image.Image, new: Image.Image, t: float) -> Image.Image:
    """Rigid integer page push: old leaves upward, destination follows."""
    w, h = old.size
    shift = round(smooth(t) * h)
    frame = Image.new("RGB", (w, h), old.getpixel((0, h - 1)))
    frame.paste(old, (0, -shift))
    frame.paste(new, (0, h - shift))
    return frame


def wipe_surface(old: Image.Image, new: Image.Image, t: float) -> Image.Image:
    """Fixed destination pixels appear behind one bottom-up paper cut."""
    if t <= 0.0:
        return old.copy()
    if t >= 1.0:
        return new.copy()
    w, h = old.size
    edge = h - round(smooth(t) * h)
    frame = old.copy()
    frame.paste(new.crop((0, edge, w, h)), (0, edge))
    if 0 < edge < h:
        ImageDraw.Draw(frame).line((0, edge, w - 1, edge), fill=VIOLET, width=1)
    return frame


def disclose_surface(old: Image.Image, new: Image.Image, t: float, anchor: int) -> Image.Image:
    """Fixed destination pixels are disclosed outward from the chosen branch."""
    if t <= 0.0:
        return old.copy()
    if t >= 1.0:
        return new.copy()
    w, h = old.size
    radius = math.ceil(smooth(t) * max(anchor, h - anchor))
    y0, y1 = max(0, anchor - radius), min(h, anchor + radius + 1)
    frame = old.copy()
    frame.paste(new.crop((0, y0, w, y1)), (0, y0))
    draw = ImageDraw.Draw(frame)
    if y0 > 0:
        draw.line((0, y0, w - 1, y0), fill=VIOLET, width=1)
    if y1 < h:
        draw.line((0, y1 - 1, w - 1, y1 - 1), fill=VIOLET, width=1)
    return frame


def blank_paper(surface: Image.Image) -> Image.Image:
    """Recover the exact row-wise paper field already present in a settled capture."""
    w, h = surface.size
    blank = Image.new("RGB", (w, h))
    draw = ImageDraw.Draw(blank)
    for y in range(h):
        draw.line((0, y, w - 1, y), fill=surface.getpixel((1, y)))
    return blank


def blackletter_curtain(surface: Image.Image, coverage: float, anchor: int) -> Image.Image:
    """Cover a page with pointed paper lancets spaced like real gothic glyph stems."""
    if coverage <= 0.0:
        return surface.copy()
    blank = blank_paper(surface)
    if coverage >= 1.0:
        return blank
    w, h = surface.size
    centers = [round(16 + (stem - BLACKLETTER_STEMS[0]) /
                     (BLACKLETTER_STEMS[-1] - BLACKLETTER_STEMS[0]) * (w - 32))
               for stem in BLACKLETTER_STEMS]
    mask = Image.new("L", (w, h), 0)
    pixels = mask.load()
    max_x_distance = max(min(abs(x - center) for center in centers) for x in range(w))
    max_extent = max(anchor, h - 1 - anchor) + max_x_distance * 2.4 + 1.0
    extent = smooth(coverage) * max_extent
    for x in range(w):
        dx = min(abs(x - center) for center in centers)
        reach = extent - dx * 2.4
        if reach <= 0.0:
            continue
        y0, y1 = max(0, int(anchor - reach)), min(h - 1, int(anchor + reach))
        for y in range(y0, y1 + 1):
            pixels[x, y] = 255
    frame = surface.copy()
    frame.paste(blank, (0, 0), mask)
    # One hard carbon pixel around the nib points makes the motion read as ink,
    # not as mixed or partially transparent framebuffer content.
    border = ImageChops.subtract(mask.filter(ImageFilter.MaxFilter(3)), mask)
    frame.paste(Image.new("RGB", (w, h), CARBON), (0, 0), border)
    return frame


def blackletter_transition(source: Image.Image, destination: Image.Image) -> list[Image.Image]:
    """Close pointed blank-paper nibs over source, then reopen onto destination."""
    old_top, old_bottom = surfaces(source)
    new_top, new_bottom = surfaces(destination)
    result = []
    for index in range(BLACKLETTER_FRAMES):
        t = index / (BLACKLETTER_FRAMES - 1)
        if t < 0.5:
            top, bottom, coverage = old_top, old_bottom, t * 2.0
        else:
            top, bottom, coverage = new_top, new_bottom, (1.0 - t) * 2.0
        result.append(compose(blackletter_curtain(top, coverage, 42),
                              blackletter_curtain(bottom, coverage, 30)))
    assert surfaces(result[0]) == surfaces(source)
    assert surfaces(result[-1]) == surfaces(destination)
    return result


def verify_blackletter_stems(destination: Image.Image) -> None:
    """Tie the lancet spacing to actual columns in the settled SIGILS heading."""
    _, bottom = surfaces(destination)
    columns = [x for x in range(100)
               if any(bottom.getpixel((x, y)) in (CARBON, VIOLET) for y in range(40))]
    runs: list[list[int]] = []
    for x in columns:
        if not runs or x > runs[-1][-1] + 1:
            runs.append([x])
        else:
            runs[-1].append(x)
    centers = tuple((run[0] + run[-1]) // 2 for run in runs)
    assert centers == BLACKLETTER_STEMS, (centers, BLACKLETTER_STEMS)


def transition(kind: str, source: Image.Image, destination: Image.Image,
               anchor: int) -> list[Image.Image]:
    old_top, old_bottom = surfaces(source)
    new_top, new_bottom = surfaces(destination)
    result = []
    for index in range(FRAMES):
        t = index / (FRAMES - 1)
        if kind == "page_slide":
            top = slide_surface(old_top, new_top, t)
            bottom = slide_surface(old_bottom, new_bottom, t)
        elif kind == "paper_wipe":
            top = wipe_surface(old_top, new_top, t)
            bottom = wipe_surface(old_bottom, new_bottom, t)
        elif kind == "local_disclosure":
            top = disclose_surface(old_top, new_top, t, 120)
            bottom = disclose_surface(old_bottom, new_bottom, t, anchor)
        else:
            raise ValueError(kind)
        result.append(compose(top, bottom))
    # Settled display surfaces must be exact existing C++ pixels.
    assert surfaces(result[0]) == surfaces(source)
    assert surfaces(result[-1]) == surfaces(destination)
    return result


def fixed_palette(images: list[Image.Image]) -> Image.Image:
    colors = sorted({pixel for image in images for pixel in image.get_flattened_data()})
    assert len(colors) <= 256, len(colors)
    palette = Image.new("P", (1, 1))
    flat = [value for color in colors for value in color]
    palette.putpalette(flat + [0] * (768 - len(flat)))
    return palette


def save_gif(images: list[Image.Image], path: Path, durations: list[int], scale: int = 1) -> None:
    if scale != 1:
        images = [image.resize((image.width * scale, image.height * scale), Image.Resampling.NEAREST)
                  for image in images]
    palette = fixed_palette(images)
    frames = [image.quantize(palette=palette, dither=Image.Dither.NONE) for image in images]
    frames[0].save(path, save_all=True, append_images=frames[1:], loop=0,
                   duration=durations, disposal=1, optimize=False)


def keyframe_sheet(variants: dict[str, list[Image.Image]]) -> Image.Image:
    font = ImageFont.load_default()
    labels = {"page_slide": "01 page slide", "paper_wipe": "02 paper wipe",
              "local_disclosure": "03 local disclosure"}
    sheet = Image.new("RGB", (3 * 416, 3 * 524), (37, 27, 36))
    draw = ImageDraw.Draw(sheet)
    for row, (kind, frames) in enumerate(variants.items()):
        for col, index in enumerate((0, FRAMES // 2, FRAMES - 1)):
            x, y = col * 416 + 8, row * 524 + 4
            draw.text((x, y), f"{labels[kind]} / frame {index + 1:02d}",
                      fill=(228, 194, 207), font=font)
            sheet.paste(frames[index], (x, y + 20))
    return sheet


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    runtime = ROOT / "platform/3ds/main.cpp"
    runtime_before = sha(runtime)
    sources: dict[str, dict[str, object]] = {}
    variants: dict[str, list[Image.Image]] = {}
    all_outputs: dict[str, dict[str, str]] = {}

    loaded = []
    for name, nav_index, anchor, destination_path in TARGETS:
        source_path = ROOT / f"ui_mockups/menu_stage1_runtime/nav_{nav_index}_dual_native.png"
        source = Image.open(source_path).convert("RGB")
        destination = Image.open(destination_path).convert("RGB")
        assert source.size == destination.size == (400, 496)
        loaded.append((name, anchor, source, destination))
        if name == "sigils":
            verify_blackletter_stems(destination)
        sources[name] = {
            "source": str(source_path.relative_to(ROOT)), "source_sha256": sha(source_path),
            "destination": str(destination_path.relative_to(ROOT)),
            "destination_sha256": sha(destination_path), "branch_anchor_y": anchor,
        }

    kinds = ("page_slide", "paper_wipe", "local_disclosure")
    for kind in kinds:
        sequence: list[Image.Image] = []
        durations: list[int] = []
        representative: list[Image.Image] | None = None
        for name, anchor, source, destination in loaded:
            frames = transition(kind, source, destination, anchor)
            if name == "sigils":
                representative = frames
            sequence.extend(frames)
            durations.extend([550] + [55] * (FRAMES - 2) + [700])
        assert representative is not None
        variants[kind] = representative
        native = OUT / f"{kinds.index(kind) + 1:02d}_{kind}_all_targets.gif"
        doubled = OUT / f"{kinds.index(kind) + 1:02d}_{kind}_sigils_2x.gif"
        save_gif(sequence, native, durations)
        save_gif(representative, doubled,
                 [650] + [55] * (FRAMES - 2) + [800], scale=2)
        all_outputs[kind] = {"all_targets_native": native.name,
                             "sigils_inspection_2x": doubled.name}

    # Font-linked alternative: pointed paper nibs use the actual horizontal stem
    # rhythm measured from the settled blackletter SIGILS heading.
    gothic_sequence: list[Image.Image] = []
    gothic_durations: list[int] = []
    gothic_sigils: list[Image.Image] | None = None
    for name, _anchor, source, destination in loaded:
        frames = blackletter_transition(source, destination)
        if name == "sigils":
            gothic_sigils = frames
        gothic_sequence.extend(frames)
        gothic_durations.extend([550] + [50] * (BLACKLETTER_FRAMES - 2) + [700])
    assert gothic_sigils is not None
    gothic_native = OUT / "04_blackletter_gate_all_targets.gif"
    gothic_doubled = OUT / "04_blackletter_gate_sigils_2x.gif"
    save_gif(gothic_sequence, gothic_native, gothic_durations)
    save_gif(gothic_sigils, gothic_doubled,
             [650] + [50] * (BLACKLETTER_FRAMES - 2) + [800], scale=2)
    all_outputs["blackletter_gate"] = {
        "all_targets_native": gothic_native.name,
        "sigils_inspection_2x": gothic_doubled.name,
    }
    gothic_sheet = Image.new("RGB", (4 * 408, 496), (37, 27, 36))
    for column, index in enumerate((0, 4, 7, BLACKLETTER_FRAMES - 1)):
        gothic_sheet.paste(gothic_sigils[index], (column * 408, 0))
    gothic_sheet.save(OUT / "04_blackletter_gate_keyframes_native.png")
    gothic_sheet.resize((gothic_sheet.width * 2, gothic_sheet.height * 2),
                        Image.Resampling.NEAREST).save(
                            OUT / "04_blackletter_gate_keyframes_2x.png")

    # Synchronized side-by-side comparison of the representative SIGILS route.
    comparison = []
    for index in range(FRAMES):
        frame = Image.new("RGB", (3 * 408, 496), (37, 27, 36))
        for col, kind in enumerate(kinds):
            frame.paste(variants[kind][index], (col * 408, 0))
        comparison.append(frame)
    save_gif(comparison, OUT / "comparison_sigils_native.gif",
             [650] + [55] * (FRAMES - 2) + [800])

    sheet = keyframe_sheet(variants)
    sheet.save(OUT / "keyframes_native.png")
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(
        OUT / "keyframes_2x.png")

    assert runtime_before == sha(runtime), "offline study modified runtime"
    report = {
        "scope": "offline GIF motion study only; no runtime edit/build/ARM/FTP/deploy",
        "variants": {
            "01_page_slide": "rigid integer vertical push; closest safe replacement for parked hinge",
            "02_paper_wipe": "fixed destination under a single bottom-up cut; no control drift",
            "03_local_disclosure": "fixed destination expands from selected GO TO branch",
            "04_blackletter_gate": "blank-paper nibs close and reopen with the measured stem rhythm of the actual gothic SIGILS heading",
        },
        "targets": sources,
        "motion_frames": {"variants_01_to_03": FRAMES,
                          "blackletter_gate": BLACKLETTER_FRAMES},
        "conceptual_input_lock_frames": {"variants_01_to_03": FRAMES,
                                           "blackletter_gate": BLACKLETTER_FRAMES},
        "blackletter_stem_centers_from_actual_sigils_heading": list(BLACKLETTER_STEMS),
        "rules": ["integer pixels only", "no opacity", "no blur", "no scaling",
                  "one shared GIF palette", "settled top/bottom surfaces pixel exact"],
        "outputs": all_outputs | {"comparison": "comparison_sigils_native.gif",
                                  "keyframes": "keyframes_2x.png",
                                  "blackletter_keyframes": "04_blackletter_gate_keyframes_2x.png"},
        "runtime_main_sha256_unchanged": runtime_before,
        "limits": ["existing host software-raster captures, not hardware LCD capture",
                   "intermediate motion is authored offline, not runtime proof",
                   "presentation hinge/gutters are outside both 3DS display surfaces"],
    }
    (OUT / "study.json").write_text(json.dumps(report, indent=2) + "\n")
    print(OUT / "comparison_sigils_native.gif")
    print(OUT / "04_blackletter_gate_sigils_2x.gif")
    print(OUT / "keyframes_2x.png")
    print("pass: 3 restrained variants + blackletter gate x 4 destinations; exact settled surfaces; runtime untouched")


if __name__ == "__main__":
    main()
