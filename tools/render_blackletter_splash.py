#!/usr/bin/env python3
"""Render the canonical Seance splash at native Nintendo 3DS resolutions."""
from pathlib import Path
from math import cos, pi
import re
from PIL import Image, ImageDraw
from seance_blackletter import draw_text, text_width

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "ui_mockups" / "generated"
OUT.mkdir(parents=True, exist_ok=True)

# Exact runtime palette from platform/3ds/ui_theme.cpp.
PAPER = (226, 190, 206)
HI = (242, 216, 226)
FAINT = (207, 165, 184)
GHOST = (174, 130, 151)
SPOT = (91, 49, 124)
INK = (31, 20, 31)

TW, TH = 400, 240
BW, BH = 320, 240
TITLE_WORD = "seance"
TITLE_CELL = 6
TITLE_X = (TW - text_width(TITLE_WORD, TITLE_CELL, 0)) // 2
TITLE_W = text_width(TITLE_WORD, TITLE_CELL, 0)
TITLE_H = 12 * TITLE_CELL
TITLE_Y = (TH - TITLE_H) // 2
ANIMATION_FRAMES = 48
ANIMATION_FRAME_MS = 50


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
        raise RuntimeError(f"font parse: {len(glyphs)}")
    return glyphs


FONT = load_font()


def tx(d, x, y, text, color=INK, scale=1, anchor="la"):
    width = len(text) * 6 * scale
    if anchor == "ra":
        x -= width
    elif anchor == "ca":
        x -= width // 2
    for char in text:
        index = ord(char) - 32
        if not 0 <= index < 96:
            index = 0
        for row, bits in enumerate(FONT[index]):
            for column in range(6):
                if bits & (1 << (5 - column)):
                    d.rectangle((x + column * scale, y + row * scale,
                                 x + (column + 1) * scale - 1,
                                 y + (row + 1) * scale - 1), fill=color)
        x += 6 * scale


def grid(d, width, height, step=5):
    for x in range(0, width, step):
        d.line((x, 0, x, height - 1), fill=FAINT)
    for y in range(0, height, step):
        d.line((0, y, width - 1, y), fill=FAINT)
    for x in range(0, width, step * 4):
        d.line((x, 0, x, height - 1), fill=GHOST)
    for y in range(0, height, step * 4):
        d.line((0, y, width - 1, y), fill=GHOST)


def corners(d, width, height):
    for x, y, sx, sy in ((4, 4, 1, 1), (width - 5, 4, -1, 1),
                         (4, height - 5, 1, -1),
                         (width - 5, height - 5, -1, -1)):
        d.line((x, y, x + sx * 9, y), fill=INK)
        d.line((x, y, x, y + sy * 9), fill=INK)


def blend(a, b, amount):
    amount = max(0.0, min(1.0, amount))
    return tuple(round(x + (y - x) * amount) for x, y in zip(a, b))


def title_flow_weight(x, progress):
    """Raised-cosine UV ink band; zero at both ends for a seamless loop."""
    center = -46 + progress * (TITLE_W + 92)
    distance = abs(x - center)
    radius = 52
    if distance >= radius:
        return 0.0
    return 0.5 + 0.5 * cos(pi * distance / radius)


def draw_flowing_title(image, progress):
    """Repaint title pixels with a soft horizontal UV band clipped to glyphs."""
    mask = Image.new("L", (TW, TH), 0)
    md = ImageDraw.Draw(mask)
    draw_text(md, TITLE_X, TITLE_Y, TITLE_WORD, cell=TITLE_CELL,
              color=255, spacing=0)
    colors = Image.new("RGB", (TW, TH), INK)
    cd = ImageDraw.Draw(colors)
    for px in range(TITLE_X, TITLE_X + TITLE_W):
        weight = title_flow_weight(px - TITLE_X, progress)
        cd.line((px, TITLE_Y, px, TITLE_Y + TITLE_H - 1),
                fill=blend(INK, SPOT, weight))
    image.paste(colors, (0, 0), mask)


def top_screen(flow_progress=None):
    image = Image.new("RGB", (TW, TH), PAPER)
    d = ImageDraw.Draw(image)

    # The splash is only the fixed Seance mark on uninterrupted dusty paper.
    # Static mark keeps its UV "e". Animation replaces it with one continuous
    # ink band that overlaps neighboring glyphs rather than switching them.
    if flow_progress is None:
        colors = (INK, SPOT, INK, INK, INK, INK)
        draw_text(d, TITLE_X, TITLE_Y, TITLE_WORD, cell=TITLE_CELL,
                  color=INK, spacing=0, colors=colors)
    else:
        draw_text(d, TITLE_X, TITLE_Y, TITLE_WORD, cell=TITLE_CELL,
                  color=INK, spacing=0)
        draw_flowing_title(image, flow_progress)
    return image


def bottom_screen():
    # Intentionally empty: no instructions, emblem, grid, or release metadata.
    return Image.new("RGB", (BW, BH), PAPER)


def dual_frame(top, bottom):
    frame = Image.new("RGB", (400, 496), (13, 12, 15))
    frame.paste(top, (0, 0))
    frame.paste(bottom, (40, 256))
    return frame


def animation_progress(frame):
    """Short rests bracket a left-to-right pass so the GIF loops calmly."""
    rest = 6
    travel = ANIMATION_FRAMES - rest * 2
    if frame < rest:
        return 0.0
    if frame >= rest + travel:
        return 1.0
    t = (frame - rest) / (travel - 1)
    # Smoothstep removes speed discontinuities at the start and end.
    return t * t * (3.0 - 2.0 * t)


def main():
    top = top_screen()
    bottom = bottom_screen()
    top.save(OUT / "seance_blackletter_splash_top.png")
    bottom.save(OUT / "seance_blackletter_splash_bottom.png")

    dual = dual_frame(top, bottom)
    dual.save(OUT / "seance_blackletter_splash_dual.png")
    dual.resize((800, 992), Image.Resampling.NEAREST).save(
        OUT / "seance_blackletter_splash_dual_2x.png"
    )

    animated_top = []
    animated_dual = []
    for frame in range(ANIMATION_FRAMES):
        progress = animation_progress(frame)
        moving_top = top_screen(progress)
        animated_top.append(moving_top)
        animated_dual.append(dual_frame(moving_top, bottom))
    animated_top[0].save(
        OUT / "seance_blackletter_splash_top_flow.gif", save_all=True,
        append_images=animated_top[1:], duration=ANIMATION_FRAME_MS,
        loop=0, disposal=2, optimize=False,
    )
    animated_dual[0].save(
        OUT / "seance_blackletter_splash_dual_flow.gif", save_all=True,
        append_images=animated_dual[1:], duration=ANIMATION_FRAME_MS,
        loop=0, disposal=2, optimize=False,
    )
    print(OUT / "seance_blackletter_splash_dual_flow.gif")


if __name__ == "__main__":
    main()
