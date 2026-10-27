#!/usr/bin/env python3
"""Render a frameless native-resolution Seance startup-menu concept.

The installed wordmark silhouette stays intact over a subtle paper gradient.
Library discloses three recent projects; only that local list has fine branches.
Closed, expanded and selected states are offline studies, not runtime changes.
"""
from pathlib import Path
import math
import re
import json
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "ui_mockups" / "start_menu_recent_disclosure"
OUT.mkdir(parents=True, exist_ok=True)

PAPER = (226, 190, 206)
HI = (242, 216, 226)
FAINT = (207, 165, 184)
GHOST = (126, 100, 119)
LINE = (174, 145, 165)
PAPER_TOP = (228, 194, 207)
PAPER_BOTTOM = (211, 187, 206)
RECENTS = (("black altar", "12 min"), ("ash choir", "18 min"),
           ("glass veil", "yesterday"))
SPOT = (91, 49, 124)
INK = (31, 20, 31)
PRESENTATION = (11, 8, 12)
FRAMES = 24
TEXT_BOUNDS = []


def load_font():
    src = (ROOT / "core/ui/font.cpp").read_text().split(
        "font_6x8[96][8] = {", 1
    )[1].split("};", 1)[0]
    glyphs = []
    for body in re.findall(r"\{([^{}]*)\}", src):
        body = re.sub(r"//.*", "", body)
        vals = []
        for macro, zero in re.findall(r"R\(([^)]*)\)|(\b0\b)", body):
            if zero:
                vals.append(0)
            else:
                value = 0
                for bit in (int(x.strip()) for x in macro.split(",")):
                    value = (value << 1) | bit
                vals.append(value)
        if len(vals) == 8:
            glyphs.append(vals)
    if len(glyphs) != 96:
        raise RuntimeError(f"font parse: {len(glyphs)} glyphs")
    return glyphs


FONT = load_font()
WORDMARK = Image.open(
    ROOT / "assets/branding/gothic_wordmark_native_mask.png"
).convert("L").point(lambda p: 255 if p >= 128 else 0)
GOTHIC_PATH = ROOT / "assets/fonts/Gossip-MedPixel.otf"
SMALL_GOTHIC_PATH = ROOT / "research/gossip/otf/Gossip-LowPixel.otf"


def gothic(im, x, y, text, color=INK, size=28, anchor="la", small=False):
    """Original Pixel outlines at their integral 28 ppem grid, never squeezed."""
    assert size == 28, "fractional Gothic grid destroys the small counters"
    font = ImageFont.truetype(str(SMALL_GOTHIC_PATH if small else GOTHIC_PATH), size)
    box = font.getbbox(text)
    mask = Image.new("L", (max(1, box[2] - box[0] + 4),
                           max(1, box[3] - box[1] + 4)), 0)
    md = ImageDraw.Draw(mask)
    md.text((2 - box[0], 2 - box[1]), text, font=font, fill=255)
    mask = mask.point(lambda p: 255 if p >= 128 else 0)
    crop = mask.getbbox()
    if not crop:
        return
    mask = mask.crop(crop)
    if anchor[0] == "r":
        x -= mask.width
    elif anchor[0] == "m":
        x -= mask.width // 2
    im.paste(Image.new("RGB", mask.size, color), (x, y), mask)
    TEXT_BOUNDS.append((text, (x, y, x + mask.width, y + mask.height)))


def tw(text, scale=1):
    return len(text) * 6 * scale


def tx(d, x, y, text, color=INK, scale=1, anchor="la"):
    width = tw(text, scale)
    if anchor[0] == "r":
        x -= width
    elif anchor[0] == "m":
        x -= width // 2
    TEXT_BOUNDS.append((text, (x, y, x + width, y + 8 * scale)))
    for ch in text:
        idx = ord(ch) - 32
        if idx < 0 or idx >= 96:
            idx = 0
        for row, bits in enumerate(FONT[idx]):
            for col in range(6):
                if bits & (1 << (5 - col)):
                    d.rectangle((x + col * scale, y + row * scale,
                                 x + (col + 1) * scale - 1,
                                 y + (row + 1) * scale - 1), fill=color)
        x += 6 * scale


def cyclic_distance(a, b, period):
    delta = abs(a - b) % period
    return min(delta, period - delta)


def paper_color(y):
    t = max(0, min(239, y)) / 239
    t = t * t * (3 - 2 * t)
    return tuple(round(a + (b - a) * t) for a, b in zip(PAPER_TOP, PAPER_BOTTOM))


def paper(width):
    im = Image.new('RGB', (width, 240))
    d = ImageDraw.Draw(im)
    for y in range(240):
        d.line((0, y, width - 1, y), fill=paper_color(y))
    return im


def draw_top(frame=None):
    """Unaltered logo mask; the new paper gradient is concept-only."""
    im = paper(400)
    # Permanent carbon wordmark, exactly matching the installed 400x240 mask.
    ink = Image.new("RGB", im.size, INK)
    im.paste(ink, (0, 0), WORDMARK)
    if frame is None:
        return im
    phase = frame / FRAMES

    # A wrapped ultraviolet scan passes only through the lettering. This keeps
    # the logo stable while giving it a cheap, readable, perfectly cyclic pulse.
    center = phase * 400
    band = Image.new("L", im.size, 0)
    bp = band.load()
    wm = WORDMARK.load()
    for y in range(52, 181):
        wave = 10 * math.sin(y * 0.105 + phase * math.tau)
        for x in range(400):
            if wm[x, y] and cyclic_distance(x + wave, center, 400) < 25:
                bp[x, y] = 255
    violet = Image.new("RGB", im.size, SPOT)
    im.paste(violet, (0, 0), band)

    # Four tiny unconnected marks orbit in open space; no rings or framing.
    d = ImageDraw.Draw(im)
    for i in range(4):
        angle = phase * math.tau + i * math.pi / 2
        x = round(200 + math.cos(angle) * 180)
        y = round(120 + math.sin(angle) * 92)
        color = SPOT if i == 0 else GHOST
        d.point((x, y), fill=color)
        if i == 0:
            d.point((x - 1, y), fill=color)
            d.point((x + 1, y), fill=color)
            d.point((x, y - 1), fill=color)
            d.point((x, y + 1), fill=color)
    return im


def draw_bottom(expanded=False, selected=-1):
    """Accordion study: tap Library toggles, a recent row opens that project.

    selected=-1 keeps focus on Library, 0..2 represents a focused recent project.
    All projects remains a separate browser entry, not a fourth recent project.
    """
    assert -1 <= selected < len(RECENTS)
    assert expanded or selected == -1
    im = paper(320)
    d = ImageDraw.Draw(im)
    gothic(im, 38, 20, 'new patch')
    focus = SPOT if selected == -1 else INK
    gothic(im, 38, 60, 'library', focus)
    if expanded:
        d.line((108, 72, 111, 75, 114, 72), fill=focus)
        tx(d, 292, 74, '3 recent', GHOST, anchor='ra')
        # Fine local stem, no main-menu trunk or repeated square junctions.
        d.line((52, 94, 52, 156, 55, 159, 66, 159), fill=LINE)
        for i, (name, age) in enumerate(RECENTS):
            y = 107 + i * 24
            if i < 2:
                d.line((52, y + 4, 66, y + 4), fill=LINE)
            if selected == i:
                # One highlighted route, with a small bevel at the turn.
                d.line((52, 94, 52, y + 1, 55, y + 4, 66, y + 4), fill=SPOT)
                for py in range(y + 2, y + 7):
                    d.line((64, py, 68, py), fill=paper_color(py))
                d.rectangle((64, y + 2, 68, y + 6), outline=SPOT)
            tx(d, 77, y, name, SPOT if selected == i else INK)
            tx(d, 292, y, age, SPOT if selected == i else GHOST, anchor='ra')
        tx(d, 77, 178, 'all projects >', GHOST)
    else:
        d.line((109, 71, 112, 74, 109, 77), fill=focus)
    gothic(im, 38, 201 if expanded else 110, 'settings')
    tx(d, 20, 228, 'a open' if selected >= 0 else
       ('a collapse' if expanded else 'a expand'), GHOST)
    tx(d, 300, 228, 'up/down select', GHOST, anchor='ra')
    return im


def save_outputs():
    top = draw_top()
    pixels = list(top.get_flattened_data())
    assert bytes(255 if p == INK else 0 for p in pixels) == WORDMARK.tobytes()
    palette = {paper_color(y) for y in range(240)} | {INK, SPOT, GHOST, LINE}
    assert set(pixels) <= palette
    top.save(OUT / 'top_native.png')
    states = [('closed', False, -1), ('expanded', True, -1)]
    states += [(f'selected_{i + 1}', True, i) for i in range(3)]
    bottoms = {}
    reports = {}
    for key, expanded, selected in states:
        TEXT_BOUNDS.clear()
        bottom = draw_bottom(expanded, selected)
        for text, (x0, y0, x1, y1) in TEXT_BOUNDS:
            assert 0 <= x0 < x1 <= 320 and 0 <= y0 < y1 <= 240, text
        for i, (a, r) in enumerate(TEXT_BOUNDS):
            for b, s in TEXT_BOUNDS[i + 1:]:
                assert not (max(r[0], s[0]) < min(r[2], s[2]) and
                            max(r[1], s[1]) < min(r[3], s[3])), (a, b)
        labels = [t for t, _ in TEXT_BOUNDS]
        assert sum(name in labels for name, _ in RECENTS) == (3 if expanded else 0)
        assert set(bottom.get_flattened_data()) <= palette
        # Independent items have no left-side trunk; the background is untouched.
        assert bottom.crop((0, 0, 32, 220)).tobytes() == paper(320).crop((0, 0, 32, 220)).tobytes()
        bottom.save(OUT / f'{key}_bottom_native.png')
        bottom.resize((640, 480), Image.Resampling.NEAREST).save(OUT / f'{key}_bottom_2x.png')
        dual = Image.new('RGB', (400, 528), PRESENTATION)
        dual.paste(top, (0, 0))
        dual.paste(bottom, (40, 288))
        dual.save(OUT / f'{key}_dual_native.png')
        enlarged = dual.resize((800, 1056), Image.Resampling.NEAREST)
        enlarged.save(OUT / f'{key}_dual_2x.png')
        for dy in range(2):
            for dx in range(2):
                assert all(enlarged.getpixel((x * 2 + dx, y * 2 + dy)) ==
                           dual.getpixel((x, y)) for y in range(528) for x in range(400))
        bottoms[key] = bottom
        reports[key] = list(TEXT_BOUNDS)
    comparison = Image.new('RGB', (656, 256), PRESENTATION)
    d = ImageDraw.Draw(comparison)
    tx(d, 0, 2, '01 / closed', PAPER_TOP)
    tx(d, 336, 2, '02 / tap library', PAPER_TOP)
    comparison.paste(bottoms['closed'], (0, 16))
    comparison.paste(bottoms['expanded'], (336, 16))
    comparison.resize((1312, 512), Image.Resampling.NEAREST).save(OUT / 'disclosure_comparison_2x.png')
    frames = [bottoms[k].resize((640, 480), Image.Resampling.NEAREST)
              for k in ('closed', 'expanded', 'selected_1', 'expanded', 'closed')]
    frames[0].save(OUT / 'disclosure_storyboard.gif', save_all=True,
                   append_images=frames[1:], duration=[1200, 1400, 1400, 800, 400],
                   loop=0, disposal=2)
    (OUT / 'README.txt').write_text(
        'offline startup-menu visual study, not a hardware capture.\n'
        'tap library / a: show exactly three recent projects; tap again: collapse.\n'
        'select a project then a: open; all projects: full browser (proposed).\n'
        'settings moves below the disclosed list, never behind it.\n'
        'fine 1px branches only under library; one hollow square on selection.\n'
        'soft rose to cool lilac paper gradient, no glow or vignette.\n'
        'original logo silhouette and source fonts unchanged.\n'
        'med pixel headings at integer 28 ppem; standard 6x8 project names.\n'
        'font copyright deborah khodanovich 2025; sil ofl 1.1.\n'
        'license: assets/fonts/OFL.txt; no modified font files.\n'
        'project names, count and times are illustrative.\n'
        '400x240 / 320x240 native screens; uniform nearest 2x.\n'
        'gif is a timed storyboard, not actual input or hardware animation.\n'
        'no runtime, splash assets, binary, or ftp modifications.\n')
    (OUT / 'report.json').write_text(json.dumps({
        'kind': 'offline visual study', 'native': [[400, 240], [320, 240]],
        'checks': ['text bounds', 'text separation', 'gradient palette',
                   'wordmark mask exact', 'integer scaling', 'three recent projects',
                   'no main trunk'],
        'gradient': [PAPER_TOP, PAPER_BOTTOM],
        'states': reports,
    }, indent=2) + '\n')
    print(OUT / 'expanded_dual_2x.png')
    print(OUT / 'disclosure_comparison_2x.png')


if __name__ == "__main__":
    save_outputs()
