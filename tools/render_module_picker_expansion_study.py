#!/usr/bin/env python3
"""Active-window expansion, v6 / authored Pillow motion study, NOT runtime.

The lower selected strip opens into the catalogue. Its original frame moves;
old controls dissolve without glyph scaling. The SAME frozen, screen-anchored
paper field fills the opened surface. Upper chain becomes a frozen soft context.
Only existing Gossip28 and native6x8 fonts. No application changes or input test.
"""
from datetime import datetime, timezone
from functools import lru_cache
import json

from PIL import Image, ImageDraw, ImageChops, PngImagePlugin, ImageStat
from render_bounce_visual_study import Canvas, COLORS, ROOT, OUTSIDE, sha, protected, dual
from render_module_picker_patch_study import (
    Surface, paper, patch_pages, blurred_patch, fit_gothic, clipped_native,
    INPUTS as PREVIOUS_INPUTS, READY,
)
from render_module_quick_picker_study import BUILTINS, CUSTOM, Entry, filters, subset, inside

OUT = ROOT/'ui_mockups/module_picker_study/active-expansion-v6'
INPUTS = ['tools/render_module_picker_expansion_study.py', *PREVIOUS_INPUTS]
ROW_Y, ROW_H, VISIBLE = 46, 27, 6


def smooth(t):
    t = max(0., min(1., t))
    return t*t*(3-2*t)


def catalogue(theme='graphite', group='all', entries=BUILTINS,
              selected_id='builtin.Filter', replace=False,
              module_start=None, filter_start=None):
    """Independent transparent foreground; no panel fill or extra page title."""
    p = Surface(Image.new('RGBA', (320, 240)), theme)
    p.txt(22, 16, 'ch01 / '+('replace' if replace else 'add'))
    p.txt(298, 16, 'selected slot' if replace else 'at chain end', 'muted', 'right')
    choices = filters(entries)
    assert group in choices
    view = subset(entries, group)
    ids = [e.id for e in view]
    assert len(ids) == len(set(ids))
    cursor = ids.index(selected_id) if selected_id in ids else 0
    if module_start is None:
        module_start = cursor-3
    module_start = max(0, min(module_start, max(0, len(view)-VISIBLE)))
    if filter_start is None:
        filter_start = choices.index(group)-3
    filter_start = max(0, min(filter_start, max(0, len(choices)-VISIBLE)))
    filter_rows, module_rows = [], []
    for row, name in enumerate(choices[filter_start:filter_start+VISIBLE]):
        y = ROW_Y+row*ROW_H
        label = clipped_native(name, 66)
        chosen = name == group
        p.txt(24, y+9, label, 'accent' if chosen else 'muted')
        if chosen:
            p.line(24, y+20, 24+len(label)*6-2, y+20, 'accent')
        box = (14, y, 102, y+ROW_H)
        assert inside(p.labels[-1][0], box)
        p.hit('filter:'+name, box)
        filter_rows.append((name, box))
    if len(choices) > VISIBLE:
        p.line(100, 51, 100, 202)
        fy = 53+round(filter_start*146/max(1, len(choices)-VISIBLE))
        p.line(99, fy, 101, fy, 'muted')
    if not view:
        p.txt(128, 82, 'no modules')
        p.txt(128, 100, 'choose a filter')
    for row, entry in enumerate(view[module_start:module_start+VISIBLE]):
        y = ROW_Y+row*ROW_H
        chosen = module_start+row == cursor
        if chosen:
            p.diamond(118, y+13, 'accent', True)
        label = p.word(128, y+20, fit_gothic(entry.name, 156), 28,
                       'accent' if chosen else 'ink')
        box = (116, y, 286, y+ROW_H)
        assert inside(label, box), (entry, label, box)
        p.hit(entry.id, box)
        module_rows.append((entry.id, box))
    if len(view) > VISIBLE:
        p.line(301, 53, 301, 202)
        cy = 55+round(module_start*145/(len(view)-VISIBLE))
        p.d.rectangle((300, cy-2, 302, cy+2), fill=(*p.c['accent'], 255))
        p.d.rectangle((299, cy-1, 303, cy+1), fill=(*p.c['accent'], 255))
        p.hit('module scrollbar', (288, 46, 320, 208))
    p.txt(18, 228, 'tap / a '+('replace' if replace else 'add') if view else 'choose filter')
    p.txt(188, 228, str(len(view))+' total', 'muted', 'right')
    p.txt(304, 228, 'b close', 'muted', 'right')
    p.hit('close', (246, 216, 312, 240))
    return p, dict(labels=p.labels, targets=p.hits, filter_rows=filter_rows,
                  module_rows=module_rows, count=len(view), group=group,
                  selected_id=ids[cursor] if ids else None,
                  module_start=module_start, filter_start=filter_start,
                  action='replace selected slot' if replace else 'append at chain end')


def bounds(count=3, selected=1, progress=0.):
    """Inclusive frame edges, always expanding. Edge slots retain their margin."""
    _, _, layout, add = patch_pages('graphite', count, selected)
    if count and selected is not None:
        x, w = layout[selected]
        start = (x+2, 0, x+w-3, 197)
    else:
        # Empty/Add-focused has NO active unit frame: use a tiny +add origin.
        assert add
        x = add[0]+add[1]//2
        start = (x-1, 22, x+1, 48)
    end = (min(8, start[0]), 0, max(311, start[2]), 216)
    t = 1-(1-max(0., min(1., progress)))**3
    return tuple(round(a+(b-a)*t) for a, b in zip(start, end))


@lru_cache(None)
def origin(theme, count, selected):
    top, bottom, layout, _ = patch_pages(theme, count, selected)
    frozen_top, frozen_bottom = blurred_patch(theme, count, selected)
    field = paper(theme).crop((40, 256, 360, 496))
    # Controls only, not the old border or opaque fill. Fade at original pixels.
    old_mask = Image.new('L', (320, 240))
    if count and selected is not None:
        x, w = layout[selected]
        box = (x+3, 0, x+w-3, 197)
        active_color = bottom.im.getpixel((x+4, 1))
        difference = ImageChops.difference(bottom.im.crop(box),
                                          Image.new('RGB', (box[2]-box[0], 197), active_color))
        mask = difference.convert('L').point(lambda v: 255 if v else 0)
        old_mask.paste(mask, box[:2])
    else:
        active_color = field.getpixel((160, 120))
    return top.im, bottom.im, frozen_top, frozen_bottom, field, old_mask, active_color


def scene(theme='graphite', progress=1., count=3, selected=1, foreground=None):
    if not count:
        selected = None
    top, bottom, frozen_top, frozen_bottom, field, old_mask, active_color = origin(theme, count, selected)
    t = max(0., min(1., progress))
    if t == 0:
        return Surface(top, theme), Surface(bottom, theme)
    if foreground is None:
        foreground, _ = catalogue(theme)
    # Frozen source cache is sampled once per illustrative context, not per focus.
    top_out = Image.blend(top, frozen_top, smooth(t/.72))
    result = Image.blend(bottom, frozen_bottom, smooth(t/.8))
    left, upper, right, lower = bounds(count, selected, t)
    box = (left, upper, right+1, lower+1)
    has_active = count > 0 and selected is not None
    # Reuse absolute UVs: never stretch, pan, reseed or scale the smoke texture.
    if has_active:
        surface = Image.blend(Image.new('RGB', (320, 240), active_color), field, smooth(t/.85))
    else:
        surface = field
    result.paste(surface.crop(box), box[:2])
    # Footer belongs to the catalogue, not to ghosted bank-selection controls.
    footer = Image.blend(result.crop((0, 217, 320, 240)), field.crop((0, 217, 320, 240)), smooth(t/.8))
    result.paste(footer, (0, 217))
    old_opacity = 1-smooth(t/.42)
    if has_active and old_opacity > 0:
        mask = old_mask.point(lambda a: round(a*old_opacity))
        result = Image.composite(bottom, result, mask)
    # The old lower U-frame itself grows; no nested original frame, popup slab,
    # branch line, new title bar or new halo style. Top context is not enlarged.
    edge = Image.new('RGBA', (320, 240))
    d = ImageDraw.Draw(edge)
    strength = (1-.4*smooth(t))*(1 if has_active else smooth(t/.45))
    for radius, alpha in reversed(list(enumerate((17,13,10,7,5,3,2,1), 1))):
        d.rectangle((left-radius, upper-radius, right+radius, lower+radius),
                    outline=(*COLORS[theme]['accent'], round(alpha*strength)))
    color = (*COLORS[theme]['accent'], round(255*strength))
    d.line((left, upper, left, lower, right, lower, right, upper), fill=color)
    result = Image.alpha_composite(result.convert('RGBA'), edge)
    # Text appears at its final native size/position, clipped by the growing
    # frame. No staggered row delays, letter stretching or interpolation.
    ink = foreground.im.copy()
    ImageDraw.Draw(ink).rectangle((0, 0, 319, 39), fill=(0,0,0,0))
    alpha = ink.getchannel('A').point(lambda a: round(a*smooth((t-.38)/.42)))
    clip = Image.new('L', (320, 240))
    ImageDraw.Draw(clip).rectangle((left+5, upper, right-5, 239), fill=255)
    ink.putalpha(ImageChops.multiply(alpha, clip))
    # State add/replace BEFORE the names emerge. The two small native labels
    # follow their respective moving edges, without resizing either glyph run.
    header = Image.new('RGBA', (320, 240))
    header.paste(foreground.im.crop((0, 0, 160, 40)), (left-8, 0))
    header.paste(foreground.im.crop((160, 0, 320, 40)), (160+right-311, 0))
    header.putalpha(ImageChops.multiply(
        header.getchannel('A').point(lambda a: round(a*smooth((t-.20)/.20))), clip))
    result = Image.alpha_composite(Image.alpha_composite(result, header), ink).convert('RGB')
    return Surface(top_out, theme), Surface(result, theme)


def stages(theme):
    c = Canvas(1056, 370, OUTSIDE)
    c.text(24, 14, 'active window / opens from within', COLORS['graphite']['ink'], scale=2)
    c.text(24, 43, 'same edges / same smoke / no separate popup', COLORS['graphite']['muted'])
    for i, (label, t) in enumerate((('01 / active patch column', 0.),
                                    ('02 / column widens', .30),
                                    ('03 / catalogue inside', 1.))):
        c.text(24+i*344, 76, label, COLORS['graphite']['accent'])
        _, bottom = scene(theme, t)
        c.im.paste(bottom.im, (24+i*344, 96))
    c.text(24, 350, 'authored motion proposal / native lcd x2 / not implemented', COLORS['graphite']['muted'])
    return c.im


def item(path, image):
    return dict(path=str(path.relative_to(ROOT)), size=list(image.size),
                bytes=path.stat().st_size, sha256=sha(path))


def save_png(image, name):
    path = OUT/name
    assert not path.exists()
    big = image.resize((image.width*2, image.height*2), Image.Resampling.NEAREST)
    meta = PngImagePlugin.PngInfo()
    meta.add_text('provenance', __doc__)
    meta.add_text('limits', 'scripted study, not C++/GPU/input/device capture; illustrative patch; no engine/PCM/project loaded')
    big.save(path, pnginfo=meta)
    with Image.open(path) as decoded:
        assert decoded.convert('RGB').tobytes() == big.tobytes()
        assert decoded.resize(image.size, Image.Resampling.NEAREST).tobytes() == image.tobytes()
    return item(path, big)


def save_motion():
    # Fixed paper phase. Scripted open/cancel only, no simulated module commit.
    steps = [0.]+[i/10 for i in range(1, 11)]+[i/8 for i in range(7, -1, -1)]
    durations = [720]+[20]*9+[1520]+[20]*7+[720]
    frames = [dual(*scene(progress=t)) for t in steps]
    assert frames[0].tobytes() == frames[-1].tobytes()
    # One global palette + no dithering avoids per-frame palette pumping.
    atlas = Image.new('RGB', (400, 496*len(frames)))
    for i, im in enumerate(frames):
        atlas.paste(im, (0, i*496))
    palette = atlas.quantize(colors=248, method=Image.Quantize.MEDIANCUT,
                             dither=Image.Dither.NONE)
    colors = palette.getpalette()[:248*3]
    for color in (*COLORS['graphite'].values(), OUTSIDE, (36,36,36), (46,44,50), (0,0,0)):
        colors.extend(color)
    palette.putpalette(colors)
    indexed = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in frames]
    # GIF writers coalesce equal adjacent indexed frames. Do this explicitly
    # so validation checks the actual timeline, not the pre-quantization count.
    encoded, encoded_durations, ranges = [], [], []
    for i, (im, duration) in enumerate(zip(indexed, durations)):
        big = im.resize((800, 992), Image.Resampling.NEAREST)
        if encoded and encoded[-1].tobytes() == big.tobytes():
            encoded_durations[-1] += duration
            ranges[-1].append(i)
        else:
            encoded.append(big)
            encoded_durations.append(duration)
            ranges.append([i])
    path = OUT/'active-window-open-close-2x.gif'
    assert not path.exists()
    encoded[0].save(path, save_all=True, append_images=encoded[1:], duration=encoded_durations,
                    loop=0, optimize=False, disposal=1,
                    comment=b'authored design study; proposed 200ms open / 160ms close; not device timings; frozen paper phase')
    with Image.open(path) as decoded:
        assert decoded.n_frames == len(encoded)
        for i, frame in enumerate(encoded):
            decoded.seek(i)
            assert decoded.convert('RGB').tobytes() == frame.convert('RGB').tobytes()
            assert decoded.info['duration'] == encoded_durations[i]
    errors = [ImageStat.Stat(ImageChops.difference(a, b.convert('RGB'))) for a, b in zip(frames, indexed)]
    return dict(item(path, encoded[0]), frames=len(encoded), rgb_source_frames=len(frames),
                duration_ms=sum(durations), encoded_duration_ms=sum(encoded_durations),
                decoded_quantized_frames_exact=True, global_palette=True, dither=False,
                maximum_rgb_error=max(max(b for a,b in e.extrema) for e in errors),
                worst_mean_channel_error=max(max(e.mean) for e in errors),
                limitation='GIF palette is lossy against RGB originals; PNGs are lossless',
                progress=steps, frame_durations_ms=durations,
                encoded_durations_ms=encoded_durations, coalesced_source_indices=ranges)


def validate():
    count = 0
    layouts = {}
    for theme in COLORS:
        for entry in BUILTINS:
            for group in ('all', entry.group):
                foreground, layout = catalogue(theme, group, selected_id=entry.id)
                assert entry.id in dict(layout['module_rows'])
                count += 1
        for e in (e for e in CUSTOM if e.user):
            catalogue(theme, 'user', CUSTOM, e.id)
            count += 1
        for group in filters(CUSTOM):
            catalogue(theme, group, CUSTOM)
            count += 1
        for replace in (False, True):
            foreground, layout = catalogue(theme, replace=replace)
            layouts[theme+('/replace' if replace else '/all')] = layout
            top, bottom = scene(theme, foreground=foreground)
            assert top.im.tobytes() == blurred_patch(theme)[0].tobytes()
            # Final smoke itself remains the exact world-positioned field in a
            # region without foreground; it is not scaled with the window.
            assert bottom.im.crop((150,30,270,38)).tobytes() == paper(theme).crop((190,286,310,294)).tobytes()
            count += 1
        catalogue(theme, 'user')
        count += 1
    for n in (0,1,6,7,28,31,150,300):
        entries = [Entry('probe.'+str(i), 'module '+str(i), 'module '+str(i),
                         'group '+str(i%12), i%2 == 0) for i in range(n)]
        for at in sorted({0,n//2,max(0,n-1)}):
            e = entries[at] if entries else None
            for group in ('all', e.group if e else 'user'):
                _, layout = catalogue(entries=entries, selected_id=e.id if e else None, group=group)
                if e:
                    assert e.id in dict(layout['module_rows'])
                assert len(layout['module_rows']) <= VISIBLE and len(layout['filter_rows']) <= VISIBLE
                count += 1
    geometry = 0
    for n in range(7):
        for selected in ([None] if not n else [*range(n), *([None] if n<6 else [])]):
            previous = bounds(n, selected, 0)
            for i in range(1, 101):
                current = bounds(n, selected, i/100)
                assert inside((current[0], current[1], current[2]+1, current[3]+1), (0,0,320,240))
                assert current[0] <= previous[0] and current[1] <= previous[1]
                assert current[2] >= previous[2] and current[3] >= previous[3]
                previous = current
                geometry += 1
    for theme in COLORS:
        closed = patch_pages(theme)
        for t in (0., .1, .3, .5, .7, 1.):
            top, bottom = scene(theme, t)
            assert (top.im.size, bottom.im.size) == ((400,240),(320,240))
            if t == 0:
                assert top.im.tobytes() == closed[0].im.tobytes()
                assert bottom.im.tobytes() == closed[1].im.tobytes()
            count += 1
    return dict(native_layout_and_frame_cases=count, expansion_geometry_samples=geometry,
                labels_targets_bounded_nonoverlapping=True, module_target=[170,27], filter_target=[88,27],
                closed_endpoint_exact_to_v5_fixture=True, foreground_never_scaled=True,
                final_unpainted_smoke_probe_exact=True, upper_final_exact_to_v5_frozen=True), layouts


def main():
    assert not OUT.exists(), 'preserve earlier media; use a fresh revision'
    source = {s:sha(ROOT/s) for s in INPUTS}
    before = protected(); before['Makefile'] = sha(ROOT/'Makefile')
    ready = {s:sha(ROOT/s) for s in READY}
    previous = {str(p.relative_to(ROOT)):sha(p) for p in OUT.parent.rglob('*') if p.is_file()}
    validation, layouts = validate()
    OUT.mkdir(parents=True)
    outputs = []
    for theme in COLORS:
        outputs.append(save_png(stages(theme), 'active-window-stages-'+theme+'-2x.png'))
        outputs.append(save_png(dual(*scene(theme)), 'active-window-'+theme+'-dual-2x.png'))
    outputs.append(save_motion())
    assert source == {s:sha(ROOT/s) for s in INPUTS}
    after = protected(); after['Makefile'] = sha(ROOT/'Makefile')
    assert before == after
    assert ready == {s:sha(ROOT/s) for s in READY}
    assert all(sha(ROOT/s) == h for s,h in previous.items())
    report = dict(created_at=datetime.now(timezone.utc).isoformat(), origin=__doc__,
        status='unapproved design-only variant; no runtime/build/deploy authorized',
        source_sha256=source, protected_sha256=before, ready_sha256=ready, previous_sha256=previous,
        outputs=outputs, validation=validation, layouts=layouts,
        visual=dict(origin='lower active strip, not whole-screen zoom or a second modal',
            closed='exact authored v5 play/filt/ghost fixture, active filt; not actual project data',
            expanded='same U-frame expands; controls fade, final-size catalogue is clipped in',
            upper='original chain softens into frozen context; no enlargement/inspector/preview',
            smoke='original phase=0 400x496 field, absolute screen coordinates, no reseeding/zoom or new recipe',
            add='visual origin is NOT target: add still appends at chain end; explicit header/action labels',
            replace='selected slot, explicit replace label; no new entry shortcut proposed',
            exceptions='empty chain or Add-focused: no active frame exists; expand from +add origin'),
        behavior_proposal=dict(
            opening_ms=200, closing_ms=160, animation='interruptible visual affordance, not an input timer',
            opener='consume whole originating down/hold/lift and held/repeat buttons; release cannot select a row',
            fresh_touch='new down during opening snaps to final layout BEFORE hit-test; only a later valid physical lift may commit that same visible target, never down or queued events',
            fresh_button='new A during opening only snaps fully open and is consumed; next fresh A may commit; navigation may snap then move; no held/repeat A commit',
            cancellation='B cancels immediately; reverse from current shown geometry, never wait for opening to finish',
            selection='filter then one valid tap/lift or A; no preview, hold, extra confirmation or auto-editor',
            ownership='gesture capture, drag/out-of-target/context loss cancel; no click-through',
            persistence='remember filter/scroll/stable-id focus in session; no saved project fields',
            data='no creation, sample I/O, thumbnails or DSP on focus; user ABI/loader not part of study'),
        limits=[
            'Pillow authored fixture and Python float paper/blur approximation, NOT C++/GPU/device capture',
            'timings are scripted design suggestions, not measured responsiveness/FPS/LCD readability',
            'no actual input state machine, transition interrupt or error/commit animation implemented/tested',
            'GIF uses a fixed lossy palette; all decoded quantized frames verified, PNGs lossless NN2x',
            'no animated smoke claim: field intentionally freezes at open, same phase through cancel',
            'current selected tile is opaque active_paper; fading that fill to expose smoke inside is NEW proposed behavior',
            'current append selects Add and snaps before cache; pre-open selected origin/cancel restoration is NEW proposed presentation',
            'current picker resets cursor/scroll and has no filters or expansion; existing 200ms column motion is not picker timing proof',
            'aliases/categories inherited proposals; canonical UnitKind/shortnames unchanged',
            'long/duplicate/Unicode user metadata policy unresolved; user entries fictional',
            'no application/core/shared-font/asset changes, ARM/C++ capture/FTP/CIA/audio export/userdata access',
        ])
    (OUT/'proposal.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    for output in outputs:
        print(output['path'], output['size'], output['bytes'], output['sha256'])
    print('pass:', validation, ';', len(before), 'production/assets/Makefile and', len(previous), 'previous files unchanged')


if __name__ == '__main__':
    main()
