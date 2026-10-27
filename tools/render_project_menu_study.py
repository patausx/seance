#!/usr/bin/env python3
"""One eight-state static menu/empty-chain sheet; NOT runtime or telemetry.

Native 400x240 + 320x240, Gossip28/6x8, existing branding masks, nearest 2x.
Only writes one concept PNG; does not build, deploy, or modify app assets.
"""
from pathlib import Path
from PIL import Image, ImageDraw
from render_theme_picker_study import Page, GRAY
from render_menu_style_study import heading_mask
from render_universal_sigils_study import caption

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/project_menu_study'
BRANDING = ROOT / 'assets/branding'
CHAINS = (('play', 'filt', 'del'), ('bird', 'rev'), (),
          ('play', 'ring', 'fold', 'chor', 'del', 'comp'))
ROUTES = {(0, 1): 'b:cut', (1, 0): 'c:pit', (3, 1): 'a:freq', (3, 3): 'd:mix'}
ITEMS = (('patch table', 'build'), ('sigils', 'draw'),
         ('sigilbay', 'modulation'), ('sequencer', 'score'),
         ('perform', 'morph'), ('bounce', 'mix to loop'),
         ('patches', 'save / load'), ('exit', 'hbmenu'))
OUTSIDE = (17, 17, 20)


def page(width=400):
    return Page(width, GRAY)


def center(c, y, text, color=None, x=None):
    cx = c.im.width // 2 if x is None else x
    c.txt(cx - len(text) * 3, y, text, color)


def center_title(c, y, text, color=None):
    c.title((c.im.width - heading_mask(text).width) // 2, y, text, color)


def marker(c, x, y, color=None):
    c.line((x - 3, y - 3, x, y, x - 3, y + 3), color or c.accent)


def footer(c, left, right):
    c.txt(20, 228, left, c.muted)
    c.txt(c.im.width - 20, 228, right, c.muted, right=True)


def cover():
    c = page()
    with Image.open(BRANDING / 'gothic_wordmark_header_mask.png') as im:
        c.im.paste(c.ink, (114, 30), im.convert('L'))
    # Integer-enlarged existing seal, not a new source of modulation.
    for layer, color in (('rim', c.rule), ('core', c.accent)):
        with Image.open(BRANDING / f'sigil_icon_48_{layer}_mask.png') as im:
            mask = im.convert('L').resize((96, 96), Image.Resampling.NEAREST)
            c.im.paste(color, (152, 102), mask)
    c.line((58, 150, 132, 150))
    c.line((268, 150, 342, 150))
    for x in (58, 342):
        c.d.ellipse((x - 2, 148, x + 2, 152), fill=c.rule)
    center(c, 220, 'sampler / synth / sigils', c.muted)
    return c.im


def home():
    c = page(320)
    for y, text in ((20, 'new patch'), (60, 'library'), (100, 'settings')):
        c.title(38, y, text, c.accent if y == 20 else c.ink)
    marker(c, 24, 31)
    marker(c, 113, 73, c.ink)
    footer(c, 'a new', 'dpad move')
    return c.im


def library():
    c = page(320)
    c.title(38, 20, 'library')
    c.txt(38, 50, 'all projects / load', c.muted)
    c.txt(210, 50, '<', c.rule)
    c.txt(236, 50, '01/17', c.muted)
    c.txt(286, 50, '>', c.ink)
    c.line((64, 71, 64, 182))
    rows = ('patch02', 'night birds', 'slow room', 'cut paper', '--', '--')
    for i, text in enumerate(rows):
        y = 68 + i * 22
        color = c.accent if i == 0 else c.ink if i < 4 else c.muted
        c.txt(38, y, f'{i + 1:03}', color)
        c.line((64, y + 4, 73, y + 4), c.accent if i == 0 else c.rule)
        if i == 0:
            marker(c, 76, y + 4)
        c.txt(82, y, text, color)
        c.txt(292, y, 'load >' if i < 4 else 'empty', color, right=True)
    c.txt(38, 208, '< back', c.muted)
    footer(c, 'a load', 'l/r page  b home')
    return c.im


def navigation(selected='patch table'):
    c = page(320)
    c.title(38, 20, 'go to')
    c.line((174, 16, 174, 42))
    for y, label, value in ((12, 'tempo', '128'), (38, 'swing', '50%')):
        c.line((174, y + 4, 184, y + 4))
        c.txt(190, y, label, c.muted)
        c.txt(244, y, value)
    c.txt(268, 12, 'bpm', c.muted)
    c.line((52, 62, 52, 209))
    for i, (label, hint) in enumerate(ITEMS):
        y = 64 + i * 20
        chosen = label == selected
        color = c.accent if chosen else c.ink
        c.line((52, y + 4, 68, y + 4), c.accent if chosen else c.rule)
        if chosen:
            marker(c, 68, y + 4)
        c.txt(77, y, label, color)
        c.txt(292, y, hint, c.muted, right=True)
    footer(c, 'a open / x clock', 'y theme: gray')
    return c.im


def sequence_strip(c, row, y):
    # Illustrative state, not a fake audio trace. Fixed short view of 16 steps.
    if row == 2:
        c.txt(70, y, 'seq off', c.muted)
        return
    bank, step, length, playing = (('a', 5, 16, True), ('c', 0, 12, False),
                                   ('a', 0, 16, False), ('b', 21, 32, True))[row]
    c.txt(70, y, f'bank {bank}', c.muted)
    page_start = ((step - 1) // 16) * 16 if step else 0
    for i in range(min(16, length - page_start)):
        x = 120 + i * 6
        active = playing and page_start + i + 1 == step
        c.d.rectangle((x, y + 2, x + 2, y + 5), fill=c.accent if active else c.rule)
        if active:
            c.line((x + 1, y, x + 1, y + 7), c.accent)
    c.txt(238, y, f'{step:02}/{length:02}' if playing else 'stopped',
          c.accent if playing else c.muted)


def project_map(layer='map', empty=False, saved=False):
    c = page()
    c.txt(40, 15, 'saved preview' if saved else 'current patch', c.muted)
    c.txt(360, 15, 'library' if saved else 'new' if empty else 'unsaved', c.muted, right=True)
    c.title(40, 35, 'untitled' if empty else 'patch02' if saved else 'patch02 *')
    context = 'project map' if layer == 'map' else 'sigil routes' if layer == 'sigils' else 'sequence'
    c.txt(360, 47, context, c.muted, right=True)
    c.line((40, 68, 360, 68))
    selected = 0 if empty else 3
    for row, modules in enumerate(((),) * 4 if empty else CHAINS):
        y = 83 + row * 34
        chosen = row == selected and not saved
        color = c.accent if chosen else c.ink
        c.txt(40, y, str(row + 1), color)
        if chosen:
            marker(c, 31, y + 4)
        c.line((53, y + 4, 64, y + 4), color if chosen else c.rule)
        if not modules:
            c.line((70, y + 4, 108, y + 4), c.accent if chosen else c.rule)
            c.d.ellipse((107, y + 2, 111, y + 6), outline=c.accent if chosen else c.rule)
            c.line((126, y + 4, 147, y + 4))
            if not empty:
                c.txt(162, y, 'empty', c.muted)
        for col, name in enumerate(modules):
            x = 70 + col * 48
            assigned = (row, col) in ROUTES and layer == 'sigils'
            label_color = c.accent if assigned else c.muted if layer == 'sigils' else color
            c.txt(x, y, name, label_color)
            edge = x + len(name) * 6
            if col < len(modules) - 1:
                c.line((edge + 3, y + 4, x + 43, y + 4), c.rule)
            else:
                c.line((edge + 3, y + 4, edge + 13, y + 4), c.rule)
                marker(c, edge + 13, y + 4, c.rule)
            if assigned:
                c.line((x + 3, y + 9, x + 3, y + 12), c.accent)
                c.txt(x, y + 15, ROUTES[row, col], c.accent)
        if layer == 'sequence' and not empty:
            sequence_strip(c, row, y + 15)
    c.line((40, 214, 360, 214))
    if empty:
        center(c, 226, 'patch table > add your first module', c.muted)
    elif layer == 'sigils':
        c.txt(40, 226, 'source : destination', c.muted)
        c.txt(360, 226, '4 routes', c.muted, right=True)
    elif layer == 'sequence':
        c.txt(40, 226, 'bank / play position', c.muted)
        c.txt(360, 226, '2 running', c.muted, right=True)
    else:
        c.txt(40, 226, '4 chains', c.muted)
        c.txt(360, 226, '11 modules', c.muted, right=True)
    return c.im


def empty_upper(mixed=False):
    c = page()
    c.title(40, 14, 'patch table')
    c.txt(360, 26, 'chain 3' if mixed else 'chain 1', c.accent, right=True)
    center_title(c, 66, 'empty chain')
    # Deliberately incomplete connection, not an oscilloscope or a touch target.
    c.line((96, 135, 170, 135))
    c.line((230, 135, 304, 135))
    c.d.arc((174, 109, 226, 161), 25, 160, fill=c.rule)
    c.d.arc((174, 109, 226, 161), 205, 340, fill=c.rule)
    c.line((189, 124, 198, 133), c.accent)
    c.line((202, 137, 211, 146), c.accent)
    for x in (170, 230):
        c.d.ellipse((x - 2, 133, x + 2, 137), outline=c.muted)
    center(c, 177, 'add the first module below', c.muted)
    if mixed:
        c.line((40, 203, 360, 203))
        for x, text in ((40, '1 / 3 modules'), (154, '2 / 2 modules'), (268, '4 / 6 modules')):
            c.txt(x, 220, text, c.muted)
    else:
        center(c, 221, 'no modules in this project yet', c.muted)
    return c.im


def empty_lower(mixed=False):
    c = page(320)
    label = 'add module'
    mask = heading_mask(label)
    width = 24 + mask.width
    x = (320 - width) // 2
    cy = 150
    c.line((x, cy, x + 12, cy), c.accent)
    c.line((x + 6, cy - 6, x + 6, cy + 6), c.accent)
    c.title(x + 24, cy - mask.height // 2, label)
    center(c, 178, 'tap or press r', c.muted)
    active = 3 if mixed else 1
    for i, x in enumerate((18, 54, 90, 126), 1):
        c.txt(x, 219, str(i), c.accent if i == active else c.muted)
        if i == active:
            c.line((x - 5, 233, x + 10, 233), c.accent)
    c.txt(304, 219, 'patch02 *' if mixed else 'untitled', c.muted, right=True)
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    states = (
        ('01 / home / cover', cover(), home(),
         'existing mark + a quiet seal', 'new patch selected'),
        ('02 / library / saved preview', project_map(saved=True), library(),
         'see the patch before loading', 'preview would need a saved-data cache'),
        ('03 / go to / project map', project_map(), navigation(),
         'one stable view of all four chains', 'no meters, no duplicate controls'),
        ('04 / go to / sigil context', project_map('sigils'), navigation('sigils'),
         'same map, destination labels appear', 'optional context layer'),
        ('05 / go to / sequence context', project_map('sequence'), navigation('sequencer'),
         'same map, banks and step positions', 'optional context layer'),
        ('06 / go to / empty project', project_map(empty=True), navigation(),
         'four beginnings, not 24 empty slots', 'the next action stays in the menu'),
        ('07 / patch table / new project', empty_upper(), empty_lower(),
         'one centered action, lower on screen', 'add center: x160 / y150'),
        ('08 / patch table / empty chain', empty_upper(True), empty_lower(True),
         'empty chain is not an empty project', 'other chain contents remain visible'),
    )
    sheet = Image.new('RGB', (1744, 1248), OUTSIDE)
    caption(sheet, 20, 14, 'seance / menus & empty states / v1')
    caption(sheet, 20, 33, 'one visual proposal / graphite / native screens / example project data')
    for i, (label, upper, lower, note, detail) in enumerate(states):
        x = 20 + (i % 4) * 432
        y = 62 + (i // 4) * 588
        caption(sheet, x, y, label)
        sheet.paste(upper, (x, y + 20))
        sheet.paste(lower, (x + 40, y + 276))
        caption(sheet, x, y + 532, note)
        caption(sheet, x, y + 548, detail)
    caption(sheet, 20, 1230, 'static concepts, not console captures / startup + project menu + empty workspace / no runtime changes')
    path = OUT / 'menus-and-empty-v1-2x.png'
    sheet.resize((3488, 2496), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))


if __name__ == '__main__':
    main()
