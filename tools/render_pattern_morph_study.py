#!/usr/bin/env python3
"""Authored pattern/morph proposals, NOT runtime captures or a working morph.

Existing paper/native fonts; the performance fader body is an unchanged crop
of a prior actual C++ fixture. All bank/rhythm states are illustrative. No DSP,
probability scheduler, edit-bank controller or runtime source is implemented.
Use --quiet for the current score: remove surrounding labels, retain ALL step ink.
--playback retains the proposed held bank+length+direction overlay. Earlier
--hotkeys and default boxed/compact layouts are historical.
"""
from pathlib import Path
import hashlib
import json
import sys
from PIL import Image, ImageDraw, ImageFilter
from render_menu_style_study import Page, paper, GHOST, BRANCH
from render_module_geometry_study import INK, SPOT, HI, FONT, pair
from render_fold_wave_study import numeric, rail
from render_upper_menu_study import number, GLYPHS, reserve

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/pattern_morph_study'
MIXER = ROOT / 'ui_mockups/performance_feedback_runtime/mixer_01_capture_half_peak_hold_dual.png'
PROTECTED = ('core/engine/engine.h', 'core/engine/engine.cpp', 'core/engine/patch_io.cpp',
             'platform/3ds/ui_sequencer.cpp', 'platform/3ds/ui_performance.cpp',
             'platform/3ds/main.cpp', '.local-state/project.toml')
# Stored endpoint examples only, not an intermediate stochastic result.
A = ((0, 4, 8, 12), (4, 12), (2, 6, 10, 14), (0, 3, 6, 8, 11, 14))
B = ((0, 3, 4, 8, 11, 14), (4, 10, 12), (1, 2, 6, 9, 10, 14), (0, 4, 7, 8, 12, 15))
NAMES = ('fm', 'noise', 'play', 'osc')


def identity():
    return {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in PROTECTED}


def rect(c, box, fill=None, outline=None):
    assert 0 <= box[0] <= box[2] < c.im.width and 0 <= box[1] <= box[3] < 240
    if fill is not None or outline is not None:
        c.d.rectangle(box, fill=fill, outline=outline)


def note_hero(c):
    # Exact existing note-display masks, with the octave subordinate to the note.
    number(c, 14, 138, 'c', scale=3)
    number(c, 64, 138, '4', scale=1)
    c.text(14, 151, 'midi 60', GHOST)


def seq_top(extended=False):
    c = Page(400)
    step = 21 if extended else 5
    c.text(14, 10, 'patch01 / unsaved', GHOST)
    c.text(386, 10, '120 bpm / play', GHOST, right=True)
    c.heading(14, 32, 'note', SPOT)
    c.text(386, 41, 'edit 02 / play 01', SPOT, right=True)
    c.text(14, 56, 'chain 1 / fm', INK)
    note_hero(c)
    c.text(14, 175, 'level 67% / repeat x1', INK)
    c.text(14, 192, 'always / timing +0', GHOST)
    c.line((176, 71, 176, 210), BRANCH)
    c.text(194, 76, 'selected step', GHOST)
    c.text(194, 94, f'{step:02d} / ' + ('64' if extended else '16'), SPOT)
    c.text(194, 115, 'lock 1 / 4', INK)
    c.text(194, 132, 'fm / pitch / c4', GHOST)
    c.text(194, 157, 'pattern 02 / stored', INK)
    c.text(194, 175, '4 chains / shared sound', GHOST)
    c.text(194, 192, 'editing does not launch', GHOST)
    c.line((14, 219, 386, 219), BRANCH)
    c.text(14, 230, 'concept / bank editing', GHOST)
    c.text(386, 230, 'r modes unchanged', GHOST, right=True)
    return c


def seq_bottom(extended=False):
    c = Page()
    c.text(10, 11, 'edit', GHOST)
    for i in range(4):
        x = 52 + 48*i
        rect(c, (x, 3, x+39, 27), fill=SPOT if i == 1 else None,
             outline=None if i == 1 else BRANCH)
        c.text(x+15, 11, str(i+1), HI if i == 1 else INK)
        if i == 0:  # Separate playing identity, not edit selection.
            c.d.polygon(((x+4, 11), (x+4, 17), (x+8, 14)), fill=INK)
        c.hit(f'edit bank {i+1}; does not launch', (x, 1, x+44, 30))
    c.text(270, 11, 'bank >', GHOST)
    c.hit('bank actions: copy from / length; proposal', (250, 1, 318, 30))
    c.text(10, 37, 'note', SPOT)
    c.text(112, 37, 'steps', GHOST)
    # Pages remain a distinct control, never relabel them as bank numbers.
    c.text(185, 37, '<', INK)
    c.text(212, 37, '17-32' if extended else '01-16', INK)
    c.text(293, 37, '>', INK)
    c.hit('previous 16-step page', (174, 31, 204, 51))
    c.hit('next 16-step page', (280, 31, 314, 51))
    c.line((10, 50, 310, 50), BRANCH)
    for ch in range(4):
        y = 57 + ch*34
        c.text(10, y, f'{ch+1} / {NAMES[ch]}', SPOT if ch == 0 else INK)
        length = 64 if extended else 16
        c.text(309, y, f'len {length} >', GHOST, right=True)
        c.hit(f'chain {ch+1} length / direction', (242, y-3, 318, y+10))
        for i in range(16):
            x = 12 + 17*i + 7*(i//4)
            cy = y+13
            selected = ch == 0 and i == 4
            on = i in B[ch]
            rect(c, (x, cy, x+13, cy+15),
                 fill=SPOT if selected else HI if on else None,
                 outline=SPOT if selected else BRANCH if i % 4 == 0 else None)
            if on:
                c.text(x+4, cy+4, 'c' if ch in (0, 3) else '+' if ch == 1 else str(i%4+1),
                       HI if selected else INK)
            else:
                rect(c, (x+6, cy+7, x+7, cy+8), fill=BRANCH)
            if selected:
                c.line((x, cy+17, x+13, cy+17), SPOT)
            c.hit(f'chain {ch+1} step {i+1+(16 if extended else 0)}', (x-1, cy-1, x+16, cy+18))
        # A displayed end post only when this page contains the real length.
        if not extended:
            c.line((307, y+13, 309, y+13, 309, y+28, 307, y+28), GHOST)
    c.line((10, 191, 310, 191), BRANCH)
    c.text(10, 198, 'note / 1:' + ('21' if extended else '05'), SPOT)
    c.text(309, 198, 'c4', INK, right=True)
    rail(c, 16, 304, 219, .47, SPOT)
    c.hit('selected note relative adjustment', (8, 209, 312, 225))
    c.text(10, 231, 'dpad step / l value', GHOST)
    c.text(310, 231, 'r mode', GHOST, right=True)
    return c


def mini_pattern(c, x, y, active, color):
    for i in range(16):
        xx = x+i*4+(i//4)*2
        if i in active:
            rect(c, (xx, y, xx+2, y+4), fill=color)
        else:
            c.d.point((xx+1, y+2), fill=BRANCH)


def performance_top():
    c = Page(400)
    c.text(14, 10, 'perform / four chains', GHOST)
    c.text(386, 10, '120 bpm / play', GHOST, right=True)
    c.heading(14, 32, 'rhythm', SPOT)
    c.text(386, 43, 'bank 1 > bank 3', SPOT, right=True)
    numeric(c, '50')
    c.text(14, 151, '% toward bank 3', GHOST)
    c.text(14, 175, '1 < morph > 3', INK)
    c.text(14, 193, 'levels stay yours', GHOST)
    c.line((178, 74, 178, 211), BRANCH)
    c.text(216, 78, 'bank 1', INK)
    c.text(310, 78, 'bank 3', SPOT)
    for ch in range(4):
        y = 108+24*ch
        c.text(192, y-1, str(ch+1), GHOST)
        mini_pattern(c, 216, y, A[ch], INK)
        mini_pattern(c, 310, y, B[ch], SPOT)
    c.text(194, 203, 'stored / 16 steps / forward', GHOST)
    c.line((14, 219, 386, 219), BRANCH)
    c.text(14, 230, 'concept / not a running morph', GHOST)
    return c


def performance_bottom():
    c = Page()
    # Preserve the existing native mixer rather than redesigning its faders.
    original = Image.open(MIXER).convert('RGB').crop((40, 256, 360, 496))
    c.im.paste(original.crop((0, 24, 320, 194)), (0, 24))
    c.text(10, 9, 'perform', INK)
    c.text(150, 9, 'sound', GHOST)
    c.text(238, 9, 'rhythm', SPOT)
    c.line((238, 21, 273, 21), SPOT)
    c.hit('show sound morph; no automatic sound change', (132, 0, 216, 24))
    c.hit('show rhythm morph; no automatic sound change', (220, 0, 312, 24))
    for ch in range(4):
        x = 8+ch*78
        c.hit(f'existing chain {ch+1} trigger', (x, 24, x+70, 53))
        c.hit(f'existing chain {ch+1} relative fader', (x, 74, x+70, 171))
        c.hit(f'existing chain {ch+1} mute', (x, 175, x+70, 193))
    c.line((8, 196, 312, 196), BRANCH)
    rect(c, (8, 201, 52, 224), outline=INK)
    c.text(15, 209, '1 >', INK)
    rect(c, (268, 201, 311, 224), outline=SPOT)
    c.text(275, 209, '< 3', SPOT)
    c.text(146, 199, '50%', SPOT)
    rail(c, 64, 256, 214, .5, SPOT)
    c.hit('choose left pattern endpoint', (8, 199, 54, 226))
    c.hit('rhythm morph relative drag; proposed', (58, 207, 262, 226))
    c.hit('choose right pattern endpoint', (266, 199, 312, 226))
    c.text(10, 231, 'bank 1 > 3', GHOST)
    c.text(310, 231, 'tap ends: choose bank', GHOST, right=True)
    assert c.im.crop((0, 24, 320, 194)).tobytes() == original.crop((0, 24, 320, 194)).tobytes()
    return c


def hotkey_score():
    """Revised authored score: original 19px pitch / 29px touch height, no bank bar."""
    c = Page()
    rows = (21, 67, 113, 159)
    for ch, y in enumerate(rows):
        c.text(8, y-15, f'{ch+1} / {NAMES[ch]}', SPOT if ch == 0 else INK)
        c.text(92, y-15, 'strike' if ch != 2 else 'slice', GHOST)
        c.text(156, y-15, 'len16 >', GHOST)
        if ch == 0:
            c.text(310, y-15, '01-16', GHOST, right=True)
        for i in range(16):
            x = 8 + 19*i
            on = i in B[ch]
            selected = ch == 0 and i == 4
            if selected:
                rect(c, (x, y, x+16, y+28), fill=SPOT)
            elif on:
                # Event badge only, not a new bank/action button.
                rect(c, (x, y, x+16, y+16), fill=HI)
            if on:
                color = HI if selected else INK
                c.text(x+5, y+5, 'c' if ch in (0, 3) else '+' if ch == 1 else str(i%4+1), color)
                if ch in (0, 3):
                    c.text(x+5, y+21, '4', color)
            else:
                rect(c, (x+7, y+7, x+9, y+9), fill=BRANCH)
            c.hit(f'chain {ch+1} step {i+1}', (x, y, x+19, y+29))
        for group in range(4):
            x = 8+76*group
            c.line((x, y+30, x+69, y+30), BRANCH)
    c.text(8, 200, 'note / 1:05', SPOT)
    c.text(310, 200, 'c4', INK, right=True)
    rail(c, 16, 304, 219, .47, SPOT)
    c.hit('existing selected-note adjustment area', (8, 212, 312, 226))
    c.text(8, 232, 'dpad step / l value', GHOST)
    c.text(310, 232, 'hold b: bank', GHOST, right=True)
    for y in rows:
        assert all(not (box[1] <= y+30 < box[3]) for box, _ in c.labels), 'group rule crosses text'
    return c


def hotkey_bank_overlay(background):
    """Pillow blur is a visual target only, not a proposed per-frame 3DS filter."""
    c = Page()
    c.im = Image.blend(background.im.filter(ImageFilter.GaussianBlur(2.2)), paper(320), .48)
    c.d = ImageDraw.Draw(c.im)
    c.heading(16, 23, 'bank', INK)
    c.text(304, 31, 'hold b', SPOT, right=True)
    c.text(130, 57, 'up / down', GHOST)
    # No panels, cards, borders or touch targets. These are keyboard choices.
    for bank, center in enumerate((48, 122, 196, 270), 1):
        mask, top = GLYPHS[(str(bank), False)]
        mask = mask.resize((mask.width*2, mask.height*2), Image.Resampling.NEAREST)
        x, y = center-mask.width//2, 136+top*2
        reserve(c, (x, y, x+mask.width, y+mask.height), f'bank {bank}')
        c.im.paste(SPOT if bank == 3 else GHOST, (x, y), mask)
        if bank == 3:
            c.line((center-25, 153, center+25, 153), SPOT)
            c.text(center-18, 162, 'choose', SPOT)
        elif bank == 1:
            c.text(center-12, 162, 'play', GHOST)
    c.text(16, 190, 'chain 1 / fm', INK)
    c.text(144, 190, '<', GHOST)
    number(c, 172, 208, '32', scale=1)
    c.text(243, 190, '>', GHOST)
    c.text(266, 190, 'steps', GHOST)
    c.text(16, 230, 'left / right: length', GHOST)
    c.text(304, 230, 'release: keep', GHOST, right=True)
    assert not c.hits, 'keyboard-only overlay must not grow touch buttons'
    return c


def hotkey_main():
    before = identity()
    layout = (ROOT / 'platform/3ds/sequencer_layout.h').read_text()
    assert 'X0 = 8, PITCH = 19, CELL_W = 17, CELL_H = 29' in layout
    assert 'ROWS[4] = {21, 67, 113, 159}' in layout
    out = ROOT / 'ui_mockups/pattern_morph_hotkeys'
    out.mkdir(parents=True, exist_ok=True)
    score = hotkey_score()
    top = seq_top()
    overlay = hotkey_bank_overlay(score)
    frames = []
    for name, bottom in (('01_full_size_score', score), ('02_hold_bank_length', overlay)):
        dual = pair(top.im, bottom.im)
        dual.save(out / f'{name}_dual.png')
        dual.resize((800, 992), Image.Resampling.NEAREST).save(out / f'{name}_dual_2x.png')
        bottom.im.resize((640, 480), Image.Resampling.NEAREST).save(out / f'{name}_bottom_2x.png')
        frames.append({'name': name, 'labels': len(top.labels)+len(bottom.labels),
                       'touch_targets': bottom.hits})
    assert before == identity()
    assert all(box[2]-box[0] == 19 and box[3]-box[1] == 29 for _, box in score.hits[:64])
    (out / 'verification.json').write_text(json.dumps({
        'origin': '2 authored Pillow duals; not runtime or hardware captures',
        'revision': 'hotkeys replace rejected bank boxes and compressed score',
        'geometry': '64 step targets: native 19px pitch, 29px height, rows21/67/113/159',
        'overlay': '4 current native gothic masks, keyboard-only, blurred/faded score background',
        'proposed_modifier': 'hold b on bare score only; tap/hold arbitration still needs implementation',
        'controls': 'up/down edit-bank candidate; left/right selected-chain length candidate, 1..64 by1',
        'fixture': 'underlying edit2/play1/length16; pending bank3/chain1/length32; release keeps, no launch',
        'preserved': 'r modes, l page/value, ordinary dpad step/chain and global navigation',
        'checks': 'label bounds/nonoverlap, target bounds/nonoverlap, native geometry, no overlay targets',
        'limits': 'Gaussian blur cost/feasibility on 3DS not measured; no runtime/controller/audio/persistence proof',
        'preserved_source_sha256': before, 'frames': frames,
    }, indent=2)+'\n')
    print('2 authored hotkey duals: labels/targets/native step geometry pass; zero overlay touch targets')
    print('no runtime changes or build; blur is a visual proposal, not a hardware effect')
    print(out)


def centered_ink(c, region, text, color=INK):
    """Native 6x8 ink, never stretched. Center the actual pixels, not the advance."""
    mask = Image.new('L', (len(text)*6, 8))
    for i, ch in enumerate(text):
        assert 32 <= ord(ch) < 128
        for y, bits in enumerate(FONT[ord(ch)-32]):
            for x in range(6):
                if bits & (1 << (5-x)):
                    mask.putpixel((i*6+x, y), 255)
    assert mask.getbbox(), text
    mask = mask.crop(mask.getbbox())
    x0, y0, x1, y1 = region
    assert mask.width <= x1-x0 and mask.height <= y1-y0, (text, region)
    x = x0+(x1-x0-mask.width)//2
    y = y0+(y1-y0-mask.height)//2
    box = (x, y, x+mask.width, y+mask.height)
    reserve(c, box, text)
    assert abs((2*x+mask.width)-(x0+x1)) <= 1
    assert abs((2*y+mask.height)-(y0+y1)) <= 1
    c.im.paste(color, (x, y), mask)
    return box


def large_cut_badge(c, x, y, color):
    """19x19 version of actual cut_badge's five spans; same 2px corner cut."""
    for inset, dy, height in ((2, 0, 1), (1, 1, 1), (0, 2, 15), (1, 17, 1), (2, 18, 1)):
        rect(c, (x+inset, y+dy, x+18-inset, y+dy+height-1), fill=color)


def centered_score(dense=False):
    """Proposed 20px pitch/19px square badges. Not new runtime geometry."""
    c = Page()
    rows = (21, 67, 113, 159)
    for ch, y in enumerate(rows):
        c.text(8, y-15, f'{ch+1} / {NAMES[ch]}', SPOT if ch == 0 else INK)
        c.text(92, y-15, 'strike' if ch != 2 else 'loop', GHOST)
        c.text(156, y-15, 'len16 >', GHOST)
        if ch == 0:
            c.text(312, y-15, '01-16', GHOST, right=True)
        for i in range(16):
            x = 20*i
            on = dense or i in B[ch]
            selected = ch == 0 and i == 4
            if on:
                large_cut_badge(c, x, y, SPOT if selected else HI)
                face, detail = (HI, SPOT) if selected else (INK, INK)
                if ch in (0, 3):
                    note = 'c' if selected else ('c', 'c#', 'd', 'f#', 'a', 'g#', 'b', 'e')[i%8]
                    octave = '4' if selected else (('-1', '3', '4', '5') if dense else ('4', '4', '4', '5'))[i%4]
                    centered_ink(c, (x, y+5, x+19, y+14), note, face)
                    centered_ink(c, (x, y+23, x+19, y+31), octave, detail)
                elif ch == 1:
                    # Noise has no pitch source. Do not invent a note or slice index.
                    centered_ink(c, (x, y+5, x+19, y+14), '--', face)
                else:
                    # NOTE mode on PLAY: sign/reverse plus semitone offset, NOT slice numbers.
                    sign, semi = (('+', '0'), ('+', '12'), ('r-', '24'), ('-', '7'))[i%4]
                    centered_ink(c, (x, y+5, x+19, y+14), sign, face)
                    centered_ink(c, (x, y+23, x+19, y+31), semi, detail)
                if ch == 2 and i == 6:
                    # PLAY loop pitch lock: same free badge-cap semantic as runtime.
                    mark = (x+12, y+2, x+15, y+3)
                    reserve(c, mark, 'stored pitch lock')
                    rect(c, (mark[0], mark[1], mark[2]-1, mark[3]-1), fill=face)
                if selected:
                    c.line((x+5, y-2, x+13, y-2), SPOT)
            else:
                rect(c, (x+8, y+8, x+10, y+10), fill=BRANCH)
            c.hit(f'chain {ch+1} step {i+1}', (x, y, x+20, y+31))
    # No group rules, separators or passive timelines between the rows.
    c.text(8, 200, 'note / 1:05', SPOT)
    c.text(312, 200, 'c4', INK, right=True)
    rail(c, 16, 304, 219, .47, SPOT)
    c.hit('existing selected-note adjustment area', (8, 212, 312, 226))
    c.text(8, 232, 'dpad step / l value', GHOST)
    c.text(312, 232, 'hold b: setup', GHOST, right=True)
    pristine = paper(320)
    for y in rows:
        for i in range(16):
            assert c.im.getpixel((20*i, y)) == pristine.getpixel((20*i, y))
    return c


def playback_overlay(background, direction=2):
    """One proposed held-button replacement for old length/direction window."""
    c = Page()
    c.im = Image.blend(background.im.filter(ImageFilter.GaussianBlur(2.2)), paper(320), .48)
    c.d = ImageDraw.Draw(c.im)
    c.heading(16, 14, 'bank', INK)
    c.text(304, 22, 'hold b', SPOT, right=True)
    c.text(118, 46, 'up/down bank', GHOST)
    for bank, center in enumerate((48, 122, 196, 270), 1):
        mask, top = GLYPHS[(str(bank), False)]
        mask = mask.resize((mask.width*2, mask.height*2), Image.Resampling.NEAREST)
        x, y = center-mask.width//2, 124+top*2
        reserve(c, (x, y, x+mask.width, y+mask.height), f'bank {bank}')
        c.im.paste(SPOT if bank == 3 else GHOST, (x, y), mask)
        if bank == 3:
            c.line((center-25, 141, center+25, 141), SPOT)
            c.text(center-18, 148, 'choose', SPOT)
        elif bank == 1:
            c.text(center-12, 148, 'play', GHOST)
    c.text(16, 176, 'chain 1 / fm', INK)
    c.text(144, 176, '<', GHOST)
    number(c, 172, 190, '32', scale=1)
    c.text(243, 176, '>', GHOST)
    c.text(266, 176, 'steps', GHOST)
    c.text(16, 211, 'a:', GHOST)
    for i, (x, word) in enumerate(((52, 'forward'), (136, 'reverse'), (220, 'ping-pong'))):
        c.text(x, 211, word, SPOT if direction == i else GHOST)
        if direction == i:
            c.line((x, 223, x+len(word)*6-1, 223), SPOT)
    c.text(16, 232, 'left/right: length', GHOST)
    c.text(304, 232, 'release: keep', GHOST, right=True)
    assert not c.hits
    return c


def playback_main():
    before = identity()
    out = ROOT / 'ui_mockups/pattern_morph_playback'
    out.mkdir(parents=True, exist_ok=True)
    score, dense = centered_score(), centered_score(True)
    # Verify all three direction labels in the same proposed footprint.
    overlays = [playback_overlay(score, d) for d in range(3)]
    # Only font fit/centering checks; this is not an all-mode controller simulation.
    cases = tuple(str(i) for i in range(101)) + ('c#', 'f#', '-1', 'r-', 'r+', '--', '??', '!f', 'f', '%', '+', '-', '=')
    # The displayed square keeps the actual chamfer family and mirror symmetry.
    silhouette = Page()
    large_cut_badge(silhouette, 0, 21, HI)
    for y in range(19):
        row = [silhouette.im.getpixel((x, 21+y)) == HI for x in range(19)]
        assert row == row[::-1]
        assert sum(row) == (15 if y in (0, 18) else 17 if y in (1, 17) else 19)
    for text in cases:
        scratch = Page()
        large_cut_badge(scratch, 0, 21, HI)
        centered_ink(scratch, (0, 26, 19, 35), text)
    top = seq_top()
    frames = []
    for name, bottom in (('01_centered_score', score), ('02_dense_notes', dense),
                         ('03_bank_length_direction', overlays[2])):
        dual = pair(top.im, bottom.im)
        dual.save(out / f'{name}_dual.png')
        dual.resize((800, 992), Image.Resampling.NEAREST).save(out / f'{name}_dual_2x.png')
        bottom.im.resize((640, 480), Image.Resampling.NEAREST).save(out / f'{name}_bottom_2x.png')
        frames.append({'name': name, 'labels': len(top.labels)+len(bottom.labels), 'touch_targets': bottom.hits})
    assert before == identity()
    assert all(box[2]-box[0] == 20 and box[3]-box[1] == 31 for _, box in score.hits[:64])
    (out / 'verification.json').write_text(json.dumps({
        'origin': '3 authored Pillow duals; NOT runtime or hardware captures',
        'revision': 'no gray group rules; larger cut-square badges; old extent window replaced in proposal',
        'geometry': '19x19 badges (was17x17), same2px corner cut; pitch20/touch20x31, rows21/67/113/159',
        'text': 'unscaled native6x8, actual ink bounds centered to <=0.5px; octave outside badge, never inverted on paper',
        'font_fit_cases': len(cases), 'direction_layout_cases': len(overlays),
        'controls': 'hold b; up/down bank; left/right length; a cycles forward/reverse/ping-pong; release keeps',
        'fixture': 'committed edit2/play1/length16/forward; candidate bank3/chain1/length32/ping-pong',
        'preserved': 'r edit modes and l page/value; no separate lower bank buttons; zero overlay touch targets',
        'checks': 'label/target bounds and nonoverlap, badge corners, centered ink, reserved lock cap, runtime hashes',
        'limits': 'font-fit checks do not prove all-mode runtime layout; input ownership/cancel/blur cost pending, no audio/morph implementation',
        'preserved_source_sha256': before, 'frames': frames,
    }, indent=2)+'\n')
    print(f'3 authored duals; {len(cases)} native-font fit/centering cases; 3 direction layouts pass')
    print('no runtime/controller/audio edits, no build/deployment; blur visual only')
    print(out)


def quiet_main():
    """Surgical authored label removal. Every step pixel and hit target is retained."""
    before = identity()
    out = ROOT / 'ui_mockups/pattern_morph_quiet'
    out.mkdir(parents=True, exist_ok=True)
    blank, top = paper(320), seq_top()
    bands = [(0, y-2, 320, y+31) for y in (21, 67, 113, 159)]
    frames = []
    for name, dense in (('01_clean_score', False), ('02_dense_notes', True)):
        original = centered_score(dense)
        c = Page()
        c.im = original.im.copy()
        c.d = ImageDraw.Draw(c.im)
        c.hits = list(original.hits)
        removed = []
        for box, label in original.labels:
            inside_step = any(box[0] >= band[0] and box[1] >= band[1] and
                              box[2] <= band[2] and box[3] <= band[3] for band in bands)
            if inside_step:
                c.labels.append((box, label))
            else:
                c.im.paste(blank.crop(box), box[:2])
                removed.append(label)
        for band in bands:
            assert c.im.crop(band).tobytes() == original.im.crop(band).tobytes()
        assert c.hits == original.hits
        assert c.im.crop((0, 212, 320, 226)).tobytes() == original.im.crop((0, 212, 320, 226)).tobytes()
        dual = pair(top.im, c.im)
        dual.save(out / f'{name}_dual.png')
        dual.resize((800, 992), Image.Resampling.NEAREST).save(out / f'{name}_dual_2x.png')
        c.im.resize((640, 480), Image.Resampling.NEAREST).save(out / f'{name}_bottom_2x.png')
        frames.append({'name': name, 'removed_labels': removed,
                       'retained_step_labels': len(c.labels), 'identical_step_bands': bands,
                       'identical_touch_targets': len(c.hits), 'value_rail_identical': True})
    assert before == identity()
    (out / 'verification.json').write_text(json.dumps({
        'origin': '2 authored Pillow duals, NOT runtime or hardware captures',
        'scope': 'remove ONLY surrounding score labels; all step content remains byte-identical',
        'retained': 'native note/sharp/octave/semitone/placeholder/lock ink, cut-square size/positions, hit targets, value rail, authored upper',
        'overlay': 'bank/length/direction held overlay unchanged from playback proposal; not rerendered',
        'limits': 'no controller, sound, storage or runtime edits; no all-mode or hardware claim',
        'preserved_source_sha256': before, 'frames': frames,
    }, indent=2)+'\n')
    print('2 authored label-only revisions; all8 full-width step bands and130 targets identical')
    print('upper and value rail retained; runtime hashes unchanged; no build/deployment')
    print(out)


def main():
    if sys.argv[1:] == ['--quiet']:
        quiet_main()
        return
    if sys.argv[1:] == ['--playback']:
        playback_main()
        return
    if sys.argv[1:] == ['--hotkeys']:
        hotkey_main()
        return
    before = identity()
    OUT.mkdir(parents=True, exist_ok=True)
    jobs = (('01_sequencer_16', seq_top(), seq_bottom()),
            ('02_sequencer_64_page_two', seq_top(True), seq_bottom(True)),
            ('03_performance_rhythm', performance_top(), performance_bottom()))
    rows = []
    for name, top, bottom in jobs:
        dual = pair(top.im, bottom.im)
        dual.save(OUT / f'{name}_dual.png')
        dual.resize((800, 992), Image.Resampling.NEAREST).save(OUT / f'{name}_dual_2x.png')
        rows.append({'name': name, 'text_labels': len(top.labels)+len(bottom.labels),
                     'proposed_disjoint_touch_zones': bottom.hits})
    assert before == identity(), 'runtime files changed during preview generation'
    (OUT / 'verification.json').write_text(json.dumps({
        'origin': 'authored Python/Pillow concepts, not C++ runtime or hardware captures',
        'scope': 'two layouts, three fixture states; no morph/scheduler/controller implementation',
        'native_sizes': {'top': [400, 240], 'bottom': [320, 240]},
        'checks': 'label bounds/overlap, declared touch-zone bounds/overlap, native mixer crop exact',
        'limits': 'no runtime object budget, audio, probability law, controller or persistence proof',
        'morph_example': '16 steps, forward; differing lengths/directions require a separate contract',
        'preserved_mixer_source': str(MIXER.relative_to(ROOT)),
        'preserved_source_sha256': before, 'frames': rows,
    }, indent=2)+'\n')
    print('3 authored duals / 2 layouts; label and proposed touch-zone assertions passed')
    print('existing native mixer crop exact; runtime files unchanged; no DSP/build/deployment')
    print(OUT)


if __name__ == '__main__':
    main()
