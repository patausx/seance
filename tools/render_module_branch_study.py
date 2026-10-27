#!/usr/bin/env python3
"""Static proposal: a catalogue grows from a node's action branch on patch table.

Not runtime/device output or a working pan/animation implementation. Other patch
nodes are omitted during branch focus; the root, action path and all 22 choices
fit the native 320x240 viewport. Labels remain native 6x8, row pitch is 14px.
Heading masks are the existing Gossip-derived assets; see assets/fonts/OFL.txt.
"""
from pathlib import Path
import re
from PIL import Image
from render_module_field_study import (
    Canvas, BG, INK, MUTED, QUIET, SPOT, heading, node, text,
)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/module_branch_study'


def catalogue():
    picker = (ROOT / 'platform/3ds/ui_picker.cpp').read_text()
    style = (ROOT / 'platform/3ds/menu_style.cpp').read_text()
    kinds = re.findall(r'UnitKind::(\w+)', picker.split('PICKER_UNITS[] = {', 1)[1].split('};', 1)[0])
    names = dict(re.findall(r'case UnitKind::(\w+):\s*return "([^"]+)";',
                           style.split('const char* module_name', 1)[1].split('const char* module_description', 1)[0]))
    descriptions = dict(re.findall(r'case UnitKind::(\w+):\s*return "([^"]+)";',
                                  style.split('const char* module_description', 1)[1]))
    assert len(kinds) == len(set(kinds)) == 22
    return [(kind, names[kind], descriptions[kind]) for kind in kinds]


def chevron(c, x, y, color=SPOT):
    for dy in range(-3, 4):
        c.d.point((x - abs(dy), y + dy), fill=color)


def render():
    c = Canvas(320, bg=BG)
    heading(c, 'patch table')  # Deliberately no independent "add module" page.
    text(c, 300, 24, 'chain 1', right=True)

    # Control branch leaves the body's lower edge, separately from audio sockets.
    # Draw the stem first so it visually meets, rather than covers, the body.
    c.d.line((32, 85, 32, 201), fill=MUTED)
    node(c, 'filt', 20, 60, 'flat', selected=True)
    actions = ('edit', 'bypass', 'copy', 'paste', 'delete', 'add')
    for i, action in enumerate(actions):
        y = 107 + 18 * i
        color = SPOT if action == 'add' else QUIET
        c.d.line((32, y + 4, 48, y + 4), fill=color)
        if action == 'add':
            chevron(c, 48, y + 4)
        text(c, 57, y, action, color)

    # One uninterrupted add continuation and a shared top branch join both
    # columns. These are leaves of the same menu, never parallel DSP routes.
    c.d.line([(80, 201), (110, 201), (110, 49), (224, 49)], fill=SPOT)
    columns = (130, 224)
    entries = catalogue()
    selected_kind = 'Chorus'
    chosen = next(entry for entry in entries if entry[0] == selected_kind)
    boxes = []
    for col, rail_x in enumerate(columns):
        c.d.line((rail_x, 49, rail_x, 203), fill=INK)
        for row, (kind, name, _) in enumerate(entries[col * 11:(col + 1) * 11]):
            y = 59 + row * 14
            color = SPOT if kind == selected_kind else INK
            c.d.line((rail_x, y + 4, rail_x + 15, y + 4), fill=color)
            if kind == selected_kind:
                chevron(c, rail_x + 15, y + 4)
            text(c, rail_x + 24, y, name, color)
            # Proposed compact stylus/D-pad targets, not validated interaction.
            box = (rail_x - 3, y - 3, rail_x + 76, y + 11)
            assert 0 <= box[0] < box[2] <= 320 and 49 <= box[1] < box[3] <= 214
            boxes.append(box)
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            assert min(a[2], b[2]) <= max(a[0], b[0]) or min(a[3], b[3]) <= max(a[1], b[1])

    # Only the focused leaf needs its explanation, not 22 repeated descriptions.
    text(c, 20, 215, chosen[1], SPOT)
    text(c, 20 + 6 * (len(chosen[1]) + 2), 215, '/ ' + chosen[2])
    text(c, 300, 215, '22 / 22', right=True)
    text(c, 20, 230, 'drag branch / a add')
    text(c, 300, 230, 'b back', right=True)
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    image = render()
    assert image.size == (320, 240)
    image.save(OUT / '01_expanded_native.png')
    image.resize((640, 480), Image.Resampling.NEAREST).save(OUT / '01_expanded_2x.png')
    print(OUT / '01_expanded_2x.png')


if __name__ == '__main__':
    main()
