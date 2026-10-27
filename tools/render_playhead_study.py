#!/usr/bin/env python3
"""Three OFFLINE playback concepts on native captures, not a running sequencer.
Synthetic event schedule; no audio/engine changes, no manual edit-press animation.
"""
from pathlib import Path
import base64
import hashlib
import io
import json
import math
from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/playhead_study'
ROWS = (45, 83, 121, 159)
TICK, LENGTH = 20, 6400
PALETTES = {
    'gray': dict(spot=(181,154,232), cell=(48,48,48), ink=(244,244,244),
                 ghost=(184,184,184), play=(255,255,255)),
    'pink': dict(spot=(91,49,124), cell=(242,216,226), ink=(31,20,31),
                 ghost=(174,130,151), play=(112,139,91)),
}
FILES = {'trig':'0000_layer_0_bottom.ppm', 'note':'0014_layer_1_bottom.ppm'}
# Stored on/off events, not inferred from the engine's current-step index.
# The fourth native lane is continuous flow: no invented discrete hit there.
PATTERN = ({0,3,6,8,11,14}, {2,6,10,14}, {0,2,4,6,8,10,12,14,15}, set())
VARIANTS = [
    dict(id='01', name='living line', description='brief expansion with a fading previous trail'),
    dict(id='02', name='bracket', description='an open bracket forms below the current step'),
    dict(id='03', name='cell strike', description='only a triggered cell contracts; rests show the marker alone'),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def protected():
    paths = [p for directory in ('core','platform') for p in (ROOT/directory).rglob('*')
             if p.is_file() and p.suffix in ('.h','.hpp','.c','.cpp','.inc')]
    paths += [p for p in (ROOT/'release').rglob('*') if p.is_file()]
    paths += [p for p in ROOT.iterdir() if p.suffix in ('.3dsx','.elf','.smdh','.cia')]
    return {str(p.relative_to(ROOT)):sha(p) for p in sorted(paths)}


def png_url(im):
    out = io.BytesIO()
    im.save(out, format='PNG')
    return 'data:image/png;base64,' + base64.b64encode(out.getvalue()).decode()


def rounded(value):
    return int(math.floor(value + .5))


def mix(a, b, amount):
    return tuple(rounded(x+(y-x)*amount) for x,y in zip(a,b))


def add(ops, x, y, w, h, color):
    assert all(isinstance(v, int) for v in (x,y,w,h))
    assert w > 0 and h > 0 and 0 <= x < x+w <= 320 and 41 <= y < y+h <= 194
    ops.append([x,y,w,h,*color])


def badge(ops, x, y, size, color):
    add(ops,x+2,y,size-4,1,color)
    add(ops,x+1,y+1,size-2,1,color)
    add(ops,x,y+2,size,size-4,color)
    add(ops,x+1,y+size-2,size-2,1,color)
    add(ops,x+2,y+size-1,size-4,1,color)


def apply(base, ops):
    im = base.copy()
    draw = ImageDraw.Draw(im)
    for x,y,w,h,r,g,b in ops:
        draw.rectangle((x,y,x+w-1,y+h-1),fill=(r,g,b))
    return im


def substrate(theme, mode):
    source = ROOT/'ui_mockups/step_focus_runtime/ui'/theme/FILES[mode]
    original = Image.open(source).convert('RGB')
    assert original.size == (320,240)
    base = original.copy()
    draw = ImageDraw.Draw(base)
    removed = []
    # Sparse example rhythm. Erase only native cells explicitly cleared in this
    # offline fixture, then reproduce draw_event_cell's native 3x3 empty dot.
    # Selected first event and the complete continuous flow row are preserved.
    for row in range(3):
        for col in range(16):
            if col in PATTERN[row]:
                continue
            x,y = 8+19*col, ROWS[row]
            for yy in range(y,y+31):
                draw.line((x,yy,x+17,yy),fill=original.getpixel((0,yy)))
            draw.rectangle((x+7,y+7,x+9,y+9),fill=PALETTES[theme]['ghost'])
            removed.append([row,col])
    assert base.crop((0,0,320,41)).tobytes() == original.crop((0,0,320,41)).tobytes()
    assert base.crop((0,194,320,240)).tobytes() == original.crop((0,194,320,240)).tobytes()
    assert base.crop((0,159,320,190)).tobytes() == original.crop((0,159,320,190)).tobytes()
    assert base.crop((8,45,26,76)).tobytes() == original.crop((8,45,26,76)).tobytes()
    for row in range(3):
        for col in PATTERN[row]:
            box=(8+19*col,ROWS[row],26+19*col,ROWS[row]+31)
            assert base.crop(box).tobytes() == original.crop(box).tobytes()
    base.save(OUT/f'{theme}_{mode}_base.png')
    return base, dict(path=str(source.relative_to(ROOT)),sha256=sha(source),cleared_cells=removed)


def column(row, n, mixed):
    if not mixed or row == 0:
        return n % 16
    if row == 1:
        return 15 - n % 16
    if row == 2:
        phase = n % 30
        return phase if phase <= 15 else 30-phase
    return n % 16


def timeline():
    result=[]
    for now in range(0,LENGTH,TICK):
        mixed=now>=3200
        local=now-3200 if mixed else now
        duration=100 if mixed else 200
        columns=[];ages=[];previous=[];hits=[];periods=[]
        for row in range(4):
            period=duration*(2 if mixed and row==3 else 1)
            n,age=divmod(local,period)
            col=column(row,n,mixed)
            old=column(row,n-1,mixed) if n else None
            columns.append(col);ages.append(age);previous.append(old)
            hits.append(col in PATTERN[row]);periods.append(period)
        result.append(dict(ms=now,columns=columns,ages=ages,previous=previous,hits=hits,periods=periods,
                           section='mixed directions · 150 bpm' if mixed else 'forward · 75 bpm'))
    return result


def envelope(age, duration):
    # Quick attack, longer return, bounded before the following event.
    attack=min(30,duration*.3)
    if age >= duration:
        return 0.0
    if age < attack:
        return age/attack
    t=(age-attack)/(duration-attack)
    return (1-t)**2


def glyph_runs(base, theme, row, col):
    x,y=8+19*col,ROWS[row]
    glyph=PALETTES[theme]['cell' if (row,col)==(0,0) else 'ink']
    ops=[]
    for yy in range(y,y+18):
        xx=x
        while xx<x+18:
            if base.getpixel((xx,yy)) != glyph:
                xx+=1;continue
            start=xx
            while xx<x+18 and base.getpixel((xx,yy))==glyph:
                xx+=1
            add(ops,start,yy,xx-start,1,glyph)
    assert ops, (theme,row,col)
    return ops


def commands(base, theme, kind, state, native_glyphs):
    ops=[];p=PALETTES[theme]
    for row,(col,age,old,hit,period) in enumerate(zip(state['columns'],state['ages'],state['previous'],state['hits'],state['periods'])):
        x,y=8+19*col,ROWS[row]
        if kind==0:
            pulse=envelope(age,min(130,period*.85))
            width=12+2*rounded(3*pulse)
            if old is not None and old!=col and age<80:
                ox=8+19*old
                # Explicit integer colors; no browser/GIF alpha differences.
                alpha=.35*(1-age/80)**2
                for yy in range(y+31,y+33):
                    add(ops,ox+3,yy,12,1,mix(base.getpixel((0,yy)),p['play'],alpha))
            add(ops,x+(18-width)//2,y+31,width,2,p['play'])
        elif kind==1:
            pulse=envelope(age,min(150,period*.85))
            width=12+2*rounded(3*pulse)
            depth=3+rounded(pulse)
            left=x+(18-width)//2
            # Ends stay below all native octave/duration pixels; no next-row hit.
            add(ops,left,y+34,width,1,p['play'])
            add(ops,left,y+35-depth,1,depth-1,p['play'])
            add(ops,left+width-1,y+35-depth,1,depth-1,p['play'])
        else:
            if hit:
                power=envelope(age,min(140,period*.85))
                inset=1 if power>=.16 else 0
                if power>0:
                    for yy in range(y,y+18):
                        add(ops,x,yy,18,1,base.getpixel((0,yy)))
                    # Only the face contracts; no glyph scaling or text jitter.
                    face=p['spot'] if (row,col)==(0,0) else p['cell']
                    badge(ops,x+inset,y+inset,18-2*inset,mix(face,p['play'],power*.28))
                    ops.extend(native_glyphs[(row,col)])
            add(ops,x+3,y+31,12,2,p['play'])
    return ops


def save_gif(frames, path, scale=1):
    colors=sorted({c for im in frames for _,c in im.getcolors(100000)})
    assert len(colors)<=256, (path,len(colors))
    encoded=[]
    for im in frames:
        # Adaptive per-frame palettes are exact when each frame has <256 colors.
        # Pillow's supplied-palette lookup coarsens close RGB entries internally.
        q=im.quantize(colors=256,dither=Image.Dither.NONE)
        assert q.convert('RGB').tobytes()==im.tobytes(), 'lossy GIF palette'
        if scale!=1:q=q.resize((q.width*scale,q.height*scale),Image.Resampling.NEAREST)
        encoded.append(q)
    encoded[0].save(path,save_all=True,append_images=encoded[1:],duration=TICK,loop=0,optimize=False,disposal=1)
    with Image.open(path) as gif:
        elapsed=0;decoded=0
        for n in range(gif.n_frames):
            gif.seek(n)
            duration=gif.info['duration']
            assert duration>0 and duration%TICK==0
            got=gif.convert('RGB')
            if scale!=1:got=got.resize((320,240),Image.Resampling.NEAREST)
            for i in range(elapsed//TICK,(elapsed+duration)//TICK):
                assert got.tobytes()==frames[i].tobytes(), (path,n,i)
                decoded+=1
            elapsed+=duration
        assert elapsed==LENGTH and decoded==LENGTH//TICK
    return dict(sha256=sha(path),bytes=path.stat().st_size,colors=len(colors),duration_ms=elapsed,decoded_source_frames=decoded)


def main():
    before=protected();OUT.mkdir(parents=True,exist_ok=True)
    schedule=timeline()
    data=dict(tick_ms=TICK,length_ms=LENGTH,frame_count=len(schedule),variants=VARIANTS,timeline=schedule,themes={})
    sources={};artifacts={};frame_checks=0;glyph_pixels=0;peak_ops=0
    font=ImageFont.truetype('/usr/share/fonts/TTF/IBMPlexMono-Regular.ttf',12)
    sheets={}
    # Test schedule independent of visuals, including wrap, reverse and ping-pong.
    assert schedule[0]['columns']==[0,0,0,0]
    assert schedule[150]['columns']==[15,15,15,15]
    assert schedule[160]['columns']==[0,15,0,0]
    assert schedule[240]['columns']==[0,15,14,8]
    assert all(not s['hits'][3] for s in schedule)
    assert any(any(s['hits']) and not all(s['hits'][:3]) for s in schedule)
    for theme in PALETTES:
        data['themes'][theme]={'modes':{}}
        for mode in FILES:
            base,origin=substrate(theme,mode);sources[f'{theme}/{mode}']=origin
            glyphs={(row,col):glyph_runs(base,theme,row,col) for row in range(3) for col in PATTERN[row]}
            all_frames=[];all_commands=[]
            for kind in range(3):
                frames=[];frame_ops=[]
                for state in schedule:
                    ops=commands(base,theme,kind,state,glyphs)
                    peak_ops=max(peak_ops,len(ops))
                    im=apply(base,ops)
                    assert im.crop((0,0,320,41)).tobytes()==base.crop((0,0,320,41)).tobytes()
                    assert im.crop((0,194,320,240)).tobytes()==base.crop((0,194,320,240)).tobytes()
                    for row,y in enumerate(ROWS):
                        if kind<2 or row==3 or not state['hits'][row]:
                            assert im.crop((0,y,320,y+31)).tobytes()==base.crop((0,y,320,y+31)).tobytes()
                    for runs in glyphs.values():
                        for x,y,w,h,*color in runs:
                            for xx in range(x,x+w):
                                assert im.getpixel((xx,y))==tuple(color)
                                glyph_pixels+=1
                    # Repaint at identical time is deterministic, and concepts
                    # never move the independently selected step's native glyphs.
                    assert apply(base,ops).tobytes()==im.tobytes()
                    frames.append(im);frame_ops.append(ops);frame_checks+=1
                prefix=f'{theme}_{kind+1:02d}' if mode=='trig' else f'{theme}_note_{kind+1:02d}'
                path=OUT/f'{prefix}.gif'
                artifacts[path.name]=save_gif(frames,path,scale=2)
                frames[2].resize((640,480),Image.Resampling.NEAREST).save(OUT/f'{prefix}_still.png')
                all_frames.append(frames);all_commands.append(frame_ops)
                print(f'{prefix}: {len(frames)} frames, {artifacts[path.name]["bytes"]} bytes',flush=True)
            data['themes'][theme]['modes'][mode]={'base':png_url(base),'frames':all_commands}
            comparison=[]
            for i,state in enumerate(schedule):
                im=Image.new('RGB',(1000,306),(20,20,23));d=ImageDraw.Draw(im)
                d.fontmode='1'  # Crisp labels keep the entire GIF palette lossless.
                for kind in range(3):
                    x=12+328*kind
                    d.text((x,10),f'{kind+1:02d} / {VARIANTS[kind]["name"]}',font=font,fill=(205,192,224))
                    im.paste(all_frames[kind][i],(x,34))
                d.text((12,284),f'{state["section"]}  /  {mode}  /  offline preview',font=font,fill=(163,158,173))
                comparison.append(im)
            prefix=f'{theme}_comparison' if mode=='trig' else f'{theme}_note_comparison'
            artifacts[prefix+'.gif']=save_gif(comparison,OUT/f'{prefix}.gif')
            comparison[2].save(OUT/f'{prefix}.png')
            sheets[(theme,mode)]=comparison[2]
    # Static atlas for review: complete displays, no app pixels enlarged unevenly.
    contact=Image.new('RGB',(1000,1224),(20,20,23))
    for i,key in enumerate((('gray','trig'),('pink','trig'),('gray','note'),('pink','note'))):
        contact.paste(sheets[key],(0,i*306))
    contact.save(OUT/'contact_sheet.png')
    (OUT/'study_data.js').write_text('window.playheadStudyData = '+json.dumps(data,ensure_ascii=False,separators=(',',':'))+';\n')
    assert protected()==before, 'protected runtime/release changed during offline study'
    report=dict(status='offline concepts only; no runtime, audio, hardware or performance proof',
                generator_sha256=sha(Path(__file__)),protected_unchanged=before,sources=sources,
                artifacts=artifacts,frames_checked=frame_checks,native_glyph_pixels_checked=glyph_pixels,
                max_offline_rect_commands=peak_ops,frame_count_per_variant=len(schedule),duration_ms=LENGTH,
                tick_ms=TICK,study_data_sha256=sha(OUT/'study_data.js'),
                limitations=['synthetic timing and event hits, not engine events',
                             'wave and editor are frozen native capture pixels',
                             'first three rows made sparse offline; fourth flow row preserved',
                             'selected step fixed at row 1 / step 1; no edit-press animation',
                             'pixel command list is preview transport, NOT proposed runtime architecture'])
    (OUT/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('frames_checked','native_glyph_pixels_checked','max_offline_rect_commands')},indent=2))


if __name__=='__main__':
    main()
