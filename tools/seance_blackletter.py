#!/usr/bin/env python3
"""Canonical Seance cell-blackletter display face.

The face is intentionally small and project-owned: glyphs are authored as
9x12 binary cells, rendered without antialiasing, and remain independent from
host fonts. It is for logos, splash titles, and rare hero values—not body UI.
"""

GLYPHS = {
    "s": (
        ".######..", "###..###.", "##....##.", "###......",
        ".####....", "..#####..", ".....####", ".#.....##",
        "##.....##", "###...###", ".######..", "...##....",
    ),
    "e": (
        "..#####..", ".##...##.", "##.....##", "##..#####",
        "#########", "##.......", "##.......", "##.....##",
        ".##...##.", "..#####..", "....##...", ".........",
    ),
    "a": (
        "...###...", "..#####..", ".##...##.", "##.....##",
        "##.....##", "#########", "##.....##", "##.....##",
        "##.....##", "###...###", ".##...##.", ".........",
    ),
    "n": (
        "##....###", "###...###", "####..###", "#####.###",
        "###.#####", "###..####", "###...###", "###...###",
        "###...###", "###...###", ".##...##.", ".........",
    ),
    "c": (
        "..######.", ".###..###", "###....##", "##.......",
        "##.......", "##.......", "##.......", "##.......",
        "###....##", ".###..###", "..######.", "....##...",
    ),
    "g": (
        "..######.", ".###..###", "###....##", "##.......",
        "##..#####", "##.....##", "##.....##", "###....##",
        ".########", ".....###.", ".##..###..", "..####...",
    ),
    "3": (
        ".######..", "###..###.", ".....###.", "....###..",
        "..#####..", ".....###.", "......###", "##....###",
        "###..###.", ".######..", "...##....", ".........",
    ),
    "#": (
        "..##.##..", "..##.##..", "#########", "..##.##..",
        ".##.##...", "#########", ".##.##...", ".##.##...",
        ".........", ".........", ".........", ".........",
    ),
}

CELL_COLUMNS = 9
CELL_ROWS = 12


def text_width(text, cell=1, spacing=1):
    """Return exact pixel advance for a string at a given cell size."""
    if not text:
        return 0
    widths = []
    for char in text:
        rows = GLYPHS.get(char.lower())
        widths.append((len(rows[0]) if rows else 5) * cell)
    return sum(widths) + (len(widths) - 1) * spacing * cell


def draw_text(draw, x, y, text, cell=4, color=(31, 20, 31), spacing=1,
              colors=None):
    """Draw cell-blackletter and return the first pixel after its advance.

    `colors`, when supplied, is a per-character palette used for restrained
    spot-ink treatments without duplicating glyph geometry.
    """
    cursor = x
    for index, char in enumerate(text):
        rows = GLYPHS.get(char.lower())
        glyph_color = colors[index] if colors and index < len(colors) else color
        if rows is None:
            cursor += 5 * cell
        else:
            for row, line in enumerate(rows):
                for column, bit in enumerate(line):
                    if bit == "#":
                        draw.rectangle((cursor + column * cell,
                                        y + row * cell,
                                        cursor + (column + 1) * cell - 1,
                                        y + (row + 1) * cell - 1),
                                       fill=glyph_color)
            cursor += len(rows[0]) * cell
        if index + 1 < len(text):
            cursor += spacing * cell
    return cursor
