#!/usr/bin/env python3
"""Fullscreen editor v3: keep every existing layout, change only surface styling.

Archived host C++ filter pixels are recolored, never resized/repositioned.
No inset card, patch-table heading, breadcrumb or new back button. Static
Pillow proposal, not runtime/device capture or a completed universal skin.
"""
from datetime import datetime, timezone
from functools import lru_cache
import json
from PIL import Image, ImageDraw, ImageChops, PngImagePlugin
import render_module_editor_sheet_study as v2

base = v2.base
ROOT, COLORS = base.ROOT, base.COLORS
OUT = ROOT/'ui_mockups/module_editor_link_study/editor-fullscreen-v3'
INPUTS = ['tools/render_module_editor_fullscreen_study.py', *v2.INPUTS]
PAPER_WEIGHT = 32
RAIL_BOXES = ((291,90,300,162),(15,206,141,217),(173,206,299,217))
TITLE_BOX, MODE_BOX = (14,10,104,38), (112,10,240,38)


@lru_cache(None)
def paper(theme, screen):
    # Same screen-space phase-zero smoke as main, quieted with its active paper.
    field = base.paper(theme).crop((0,0,400,240) if screen=='top' else (40,256,360,496))
    return Image.blend(Image.new('RGB',field.size,base.active_fill(theme)),field,PAPER_WEIGHT/100)


def skin(theme, mode, screen):
    source = v2.donor(theme,mode,screen)
    backdrop = paper(theme,screen)
    result = source.copy()
    old, new, bg = source.load(), result.load(), backdrop.load()
    allowed = Image.new('L',source.size)
    mask = allowed.load()
    old_shade = (45,43,48) if theme=='graphite' else (218,181,201)
    # Preserve all native foreground pixels and positions; not screenshot zoom.
    for y in range(240):
        old_bg = old[0,y]
        for x in range(source.width):
            if old[x,y] == old_bg:
                new[x,y] = bg[x,y]; mask[x,y] = 255
            elif screen=='top' and old[x,y] == old_shade:
                new[x,y] = base.mix(bg[x,y],COLORS[theme]['accent'],16,255); mask[x,y] = 255
    baseline = result.copy()
    if screen=='top':
        # The existing title gets main's accent hierarchy, without new type/layout.
        for box,old_color,new_color in (
            (TITLE_BOX,COLORS[theme]['ink'],COLORS[theme]['accent']),
            (MODE_BOX,COLORS[theme]['accent'],COLORS[theme]['muted'])):
            for y in range(box[1],box[3]):
                for x in range(box[0],box[2]):
                    if old[x,y] == old_color:
                        new[x,y] = new_color; mask[x,y] = 255
        # Both plots preserve their original paths, axes, clipping marks, sizes.
        assert result.crop((0,94,400,209)).tobytes() == baseline.crop((0,94,400,209)).tobytes()
    else:
        for box in RAIL_BOXES:
            result.paste(backdrop.crop(box),box[:2])
            ImageDraw.Draw(allowed).rectangle((box[0],box[1],box[2]-1,box[3]-1),fill=255)
        p = base.Surface(result,theme)
        v2.needle(p,base.PARAMS['res'],295,157,63,True)
        v2.needle(p,base.PARAMS['drive'],18,211,119)
        v2.needle(p,base.PARAMS['morph'],176,211,119)
        result = p.im
        # No v2 header/back action/footer/context changes survive into v3.
        for box in ((0,0,320,28),(0,28,320,90),(0,90,278,182),(0,182,320,204),(0,225,320,240)):
            assert result.crop(box).tobytes()==baseline.crop(box).tobytes(), (theme,mode,box)
    difference = ImageChops.difference(source,result)
    for channel in difference.split():
        assert ImageChops.multiply(channel,ImageChops.invert(allowed)).getbbox() is None
    assert result.size==source.size
    return result


def pages(theme='graphite',mode='low'):
    return tuple(skin(theme,mode,screen) for screen in ('top','bottom'))


def save(image, name):
    path=OUT/name; assert not path.exists()
    big=image.resize((image.width*2,image.height*2),Image.Resampling.NEAREST)
    meta=PngImagePlugin.PngInfo(); meta.add_text('provenance',__doc__)
    big.save(path,pnginfo=meta)
    with Image.open(path) as decoded: assert decoded.convert('RGB').tobytes()==big.tobytes()
    return dict(path=str(path.relative_to(ROOT)),size=list(big.size),bytes=path.stat().st_size,sha256=base.sha(path))


def main():
    assert not OUT.exists(), 'preserve earlier renders'
    sources={p:base.sha(ROOT/p) for p in INPUTS}
    protected=dict(base.protected(),Makefile=base.sha(ROOT/'Makefile'))
    ready={p:base.sha(ROOT/p) for p in base.READY}
    previous=v2.inventory(OUT.parent); archive=v2.inventory(base.ARCHIVE)
    cases=0
    for theme in COLORS:
        for mode in v2.MODES:
            top,bottom=pages(theme,mode)
            assert top.size==(400,240) and bottom.size==(320,240)
            cases+=1
    OUT.mkdir(parents=True)
    outputs=[save(base.pair(pages(theme)),'editor-fullscreen-'+theme+'-2x.png') for theme in COLORS]
    current=tuple(v2.donor('graphite','low',screen) for screen in ('top','bottom'))
    outputs.append(save(v2.board('fullscreen / same editor','same coordinates and working area / only the visual skin changes',[
        ('current / archived native layout',base.pair(current)),
        ('proposal / quiet main-screen skin',base.pair(pages()))]),'editor-fullscreen-before-after-2x.png'))
    assert sources=={p:base.sha(ROOT/p) for p in INPUTS}
    assert protected==dict(base.protected(),Makefile=base.sha(ROOT/'Makefile'))
    assert all(base.sha(ROOT/p)==digest for group in (ready,previous,archive) for p,digest in group.items())
    report=dict(created_at=datetime.now(timezone.utc).isoformat(),origin=__doc__,
        status='design only / awaiting feedback / no app or deployment approval',
        source_sha256=sources,protected_sha256=protected,ready_sha256=ready,
        previous_sha256=previous,archive_sha256=archive,outputs=outputs,
        validation=dict(native_dual_cases=cases,all_unapproved_pixel_changes_excluded=True,
            full_size_top=[400,240],full_size_bottom=[320,240],
            original_upper_plot=[32,110,348,75],original_lower_plot=[32,94,228,63],
            no_geometry_reflow_or_text_reraster=True,fonts_and_all_label_positions_preserved=True,
            lower_header_modes_plot_labels_footer_exact_after_material_recolor=True),
        appearance=dict(paper_main_smoke_percent=PAPER_WEIGHT,active_card_paper_percent=100-PAPER_WEIGHT,
            phase='frozen0 in common400x496 screen-space field',
            title='existing bitmap ink -> accent',mode='existing bitmap accent -> muted',
            rails='same axes/value positions, main-style needles instead of diamonds'),
        removed_from_v2=['inset312px frame and halo','shrunk upper graph','patch table heading and breadcrumb',
            'new header back button/target','new footer patch identity','longer bypass label'],
        unchanged=['all original400/320 screen geometry and text/glyph sizes, including ch01/01',
            'all original filter hit targets, labels, A/B/X/L semantics and parameter laws are retained by proposal',
            'raw5142/res10485/drive5898/morph0; ordinary five modes in both legacy themes'],
        limitations=['Pillow recolor of archived C++ host donor, not new runtime/hardware capture',
            'no touch handlers, common runtime skin helpers, audio/project/sample loading, build or deployment',
            'quiet smoke is proposed material blending, not existing editor rendering or measured GPU performance',
            'five ordinary filter modes only; lock/lens/mute/bypass/custom themes and other modules not proven',
            'other editors may need style hookups; no promise that a single recolor automatically handles every surface',
            'static PNG only, no new motion; prior studies and ready3dsx unchanged'])
    (OUT/'proposal.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    for output in outputs: print(output['path'],output['size'],output['sha256'])
    print('pass:',cases,'native dual cases; prior files:',len(previous),'; protected:',len(protected))


if __name__=='__main__': main()
