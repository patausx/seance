#!/usr/bin/env python3
"""Fresh real-controller playhead03+edit-press proof/captures; no ARM/full suite/deployment."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import time
from PIL import Image, ImageDraw, ImageChops

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'ui_mockups/playhead_press_runtime'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def sources():
    paths=[p for d in ('core','platform') for p in (ROOT/d).rglob('*') if p.is_file() and p.suffix in ('.h','.hpp','.c','.cpp','.inc')]
    paths += [p for p in (ROOT/'tools').rglob('*') if p.is_file() and p.suffix in ('.h','.hpp')]
    paths += [ROOT/'tools/sequencer_test.cpp']
    return {str(p.relative_to(ROOT)):sha(p) for p in sorted(set(paths))}

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sanitize',action='store_true')
    ap.add_argument('--no-build',action='store_true')
    ap.add_argument('--no-captures',action='store_true')
    ap.add_argument('--no-neighbors',action='store_true')
    ap.add_argument('--out')
    ap.add_argument('--binary')
    args=ap.parse_args()
    parent=ROOT/args.out if args.out else BASE/('ui-sanitize' if args.sanitize else 'ui')
    out=parent/datetime.now(timezone.utc).strftime('run-%Y%m%dT%H%M%S.%fZ');out.mkdir(parents=True)
    binary=Path(args.binary or ('/tmp/seance_playhead_press_ui_sanitize' if args.sanitize else '/tmp/seance_playhead_press_ui_test'))
    stamp=binary.with_name(binary.name+'.sources.json')
    initial=sources();tools={str(Path(__file__).relative_to(ROOT)):sha(__file__)}
    protected=json.loads((BASE/'baseline.json').read_text());protected={**protected['protected'],**protected['studies']}
    inherited=json.loads((BASE/'inherited-protected.json').read_text());protected.update(inherited['artifacts'])
    originals={str((BASE/p).relative_to(ROOT)):sha(BASE/p) for p in ('baseline.json','inherited-protected.json','before-ui-host','before-ui-host.sources.json','before-ui_sequencer.cpp')}
    proof=dict(status='incomplete',sanitized=args.sanitize,runs=[],screens=[],animations=[],neighbors=[],sources=initial,tools=tools,originals=originals)
    def save():
        (out/'verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    def run(command,label,theme='pink',allow_failure=False):
        command=list(map(str,command));env=dict(os.environ,SEANCE_TEST_THEME=theme)
        if args.sanitize:env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        start=time.monotonic();p=subprocess.run(command,cwd=ROOT,env=env,text=True,capture_output=True,timeout=900)
        log=out/(label+'.log');log.write_text('$ '+shlex.join(command)+'\n'+p.stdout+p.stderr)
        proof['runs'].append(dict(command=command,theme=theme,seconds=round(time.monotonic()-start,3),returncode=p.returncode,log=str(log.relative_to(ROOT)),sha256=sha(log)))
        save();print(label,p.returncode,proof['runs'][-1]['seconds'],flush=True)
        if p.returncode and not allow_failure:print(p.stdout[-6000:]+p.stderr[-12000:]);p.check_returncode()
        return p
    save()
    if not args.no_build:
        dry=run(['make','--no-print-directory','-n','test-sequencer-menu'],'recipe')
        build=shlex.split(next(line for line in dry.stdout.splitlines() if line.startswith('g++ ')))
        build[build.index('-o')+1]=str(binary)
        if args.sanitize:
            build[build.index('-O2')]='-O1';build[1:1]=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
        run(build,'build');assert initial==sources(),'sources changed while building; fresh rebuild required'
        stamp.write_text(json.dumps(dict(binary_sha256=sha(binary),sources=initial),indent=2)+'\n')
    else:
        built=json.loads(stamp.read_text());assert built['sources']==initial and built['binary_sha256']==sha(binary),'stale binary; rebuild'
    shutil.copy2(binary,out/'ui-host');shutil.copy2(stamp,out/'compiled-sources.json')
    proof['binary']=dict(path=str((out/'ui-host').relative_to(ROOT)),sha256=sha(binary),source_stamp_sha256=sha(out/'compiled-sources.json'));save()
    for theme in ('pink','gray'):
        run([binary,'--playhead-press-only'],theme+'-feedback',theme)
        if not args.no_neighbors:
            for suite in ('step-focus','sequence-banks','cuts','slice'):
                after=run([binary,'--'+suite+'-only'],theme+'-'+suite,theme,True)
                before=None
                if suite=='slice' or after.returncode:
                    before=run([BASE/'before-ui-host','--'+suite+'-only'],theme+'-before-'+suite,theme,True)
                failures=lambda p:sorted(line for line in p.stderr.splitlines() if line.startswith('FAIL:'))
                entry=dict(theme=theme,suite=suite,after_exit=after.returncode,after_failures=failures(after))
                if before is not None:entry.update(before_exit=before.returncode,before_failures=failures(before))
                if suite=='slice':
                    assert before and after.returncode==before.returncode==1,(theme,suite,entry)
                    assert failures(after)==failures(before) and len(failures(after))==1 and '[line 259]' in failures(after)[0],entry
                    assert '8 tests, 1 failed' in after.stdout and '8 tests, 1 failed' in before.stdout
                    entry['result']='known preexisting fixture failure, exact same line259'
                else:
                    assert after.returncode==0,(theme,suite,entry)
                    entry['result']='pass'
                proof['neighbors'].append(entry);save()
        if not args.no_captures:
            run([binary,'--dump-playhead-press',out/theme],theme+'-capture',theme)
            before_dir=out/(theme+'-before-focus');after_dir=out/(theme+'-after-focus')
            run([BASE/'before-ui-host','--dump-step-focus',before_dir],theme+'-before-focus-capture',theme)
            run([binary,'--dump-step-focus',after_dir],theme+'-after-focus-capture',theme)
            before_rows=list(csv.DictReader((before_dir/'manifest.tsv').open(),delimiter='\t'))
            after_rows=list(csv.DictReader((after_dir/'manifest.tsv').open(),delimiter='\t'))
            assert len(before_rows)==len(after_rows)
            comparison=dict(theme=theme,pairs=0,pixels=0,changed_pixels=0,unchanged_screens=0,playhead_changed_screens=0)
            for before_row,after_row in zip(before_rows,after_rows):
                assert before_row['frame']==after_row['frame'] and before_row['scenario']==after_row['scenario']
                for screen in ('top','bottom'):
                    filename=before_row['frame']+'_'+screen+'.ppm'
                    a=Image.open(before_dir/filename).convert('RGB');b=Image.open(after_dir/filename).convert('RGB');assert a.size==b.size
                    if before_row['scenario']!='playhead' or screen=='top':
                        assert a.tobytes()==b.tobytes(),('idle native preservation',theme,filename)
                        comparison['unchanged_screens']+=1
                    else:
                        changed=0
                        for index,(x,y) in enumerate(zip(a.getdata(),b.getdata())):
                            if x==y:continue
                            px=index%320;py=index//320
                            assert any(row<=py<row+18 for row in (45,83,121,159)) and 8<=px<8+16*19 and (px-8)%19<18,('unexpected old/new change',theme,filename,px,py)
                            changed+=1
                        comparison['changed_pixels']+=changed
                        comparison['playhead_changed_screens']+=bool(changed)
                        comparison['unchanged_screens']+=not changed
                    comparison['pairs']+=1;comparison['pixels']+=a.width*a.height
            proof.setdefault('before_after',[]).append(comparison);save()
    rows=[];cards=[];strips=[];peak=0
    if not args.no_captures:
        for theme in ('pink','gray'):
            groups={}
            for row in csv.DictReader((out/theme/'manifest.tsv').open(),delimiter='\t'):
                peak=max(peak,int(row['shared_objects']));assert int(row['shared_objects'])<8192 and row['out_of_bounds']=='0'
                dual=Image.new('RGB',(400,488),(16,16,16))
                for which,offset,size in [('top',(0,0),(400,240)),('bottom',(40,248),(320,240))]:
                    path=out/theme/(row['frame']+'_'+which+'.ppm');image=Image.open(path).convert('RGB');assert image.size==size
                    dual.paste(image,offset);assert dual.crop((offset[0],offset[1],offset[0]+size[0],offset[1]+size[1])).tobytes()==image.tobytes()
                    proof['screens'].append(dict(path=str(path.relative_to(ROOT)),sha256=sha(path),pixels=size[0]*size[1]))
                groups.setdefault(row['scenario'],[]).append(dual);rows.append(dict(row,theme=theme))
            for scenario,frames in groups.items():
                if len(frames)>1:
                    times=[10]*len(frames);times[-1]=250
                    apng=out/f'{theme}_{scenario}.png';frames[0].save(apng,save_all=True,append_images=frames[1:],duration=times,loop=0,optimize=False,disposal=0,blend=0)
                    # APNG may coalesce identical adjacent frames. Decode on its
                    # timeline and require every original native timestamp exact.
                    decoded=Image.open(apng);actual=[]
                    for n in range(decoded.n_frames):
                        decoded.seek(n)
                        duration=int(decoded.info.get('duration',sum(times) if decoded.n_frames==1 else -1))
                        actual.append((decoded.convert('RGB').copy(),duration))
                    assert sum(ms for _,ms in actual)==sum(times)
                    cursor=0;remaining=actual[0][1]
                    for frame,duration in zip(frames,times):
                        assert frame.tobytes()==actual[cursor][0].tobytes(),(theme,scenario,cursor)
                        remaining-=duration
                        if remaining==0 and cursor+1<len(actual):cursor+=1;remaining=actual[cursor][1]
                        else:assert remaining>=0
                    proof['animations'].append(dict(path=str(apng.relative_to(ROOT)),sha256=sha(apng),format='APNG' if decoded.n_frames>1 else 'PNG (all source frames identical; constant hold)',source_frames=len(frames),decoded_frames=len(actual),milliseconds=sum(times),native_rgb_exact=True))
                    # GIF is a preview only if its 256-color budget is exceeded;
                    # native PPM+lossless APNG remain authoritative.
                    palettes=[f.quantize(colors=256,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE) for f in frames]
                    gif=out/f'{theme}_{scenario}.gif';palettes[0].save(gif,save_all=True,append_images=palettes[1:],duration=times,loop=0,optimize=False,disposal=2)
                    exact=all(a.tobytes()==b.convert('RGB').tobytes() for a,b in zip(frames,palettes))
                    gif_decoded=Image.open(gif);gif_frames=[]
                    for n in range(gif_decoded.n_frames):
                        gif_decoded.seek(n);gif_frames.append((gif_decoded.convert('RGB').copy(),int(gif_decoded.info['duration'])))
                    assert sum(t for _,t in gif_frames)==sum(times)
                    cursor=0;remaining=gif_frames[0][1]
                    for frame,duration in zip(palettes,times):
                        assert frame.convert('RGB').tobytes()==gif_frames[cursor][0].tobytes(),('gif roundtrip',theme,scenario,cursor)
                        remaining-=duration
                        if remaining==0 and cursor+1<len(gif_frames):cursor+=1;remaining=gif_frames[cursor][1]
                        else:assert remaining>=0
                    proof['animations'].append(dict(path=str(gif.relative_to(ROOT)),sha256=sha(gif),format='GIF',source_frames=len(frames),decoded_frames=len(gif_frames),milliseconds=sum(times),palette_rgb_exact=exact,quantized_roundtrip_exact=True))
                if scenario in ('play_1_free','play_1_selected','edit_1','play_4_selected','edit_7','real_retrigs','real_sequence','rapid_focus_press','flow_no_hit'):
                    cards.append((theme+' / '+scenario,frames[min(3,len(frames)-1)]))
                if scenario in ('play_1_selected','edit_1','rapid_focus_press','real_retrigs','real_sequence'):
                    indices=sorted(set(min(n,len(frames)-1) for n in (0,1,3,5,8,11,14)))
                    strips.append((theme+' / '+scenario,[frames[n].crop((40,248+35,360,248+190)) for n in indices]))
        sheet=Image.new('RGB',(4*410,((len(cards)+3)//4)*515),(16,16,16));pen=ImageDraw.Draw(sheet)
        for n,(label,image) in enumerate(cards):x=n%4*410;y=n//4*515;pen.text((x+5,y+4),label,fill='white');sheet.paste(image,(x+5,y+22))
        sheet.save(out/'contact_sheet.png')
        strip=Image.new('RGB',(7*324, len(strips)*180),(16,16,16));pen=ImageDraw.Draw(strip)
        for n,(label,frames) in enumerate(strips):
            pen.text((4,n*180+3),label,fill='white')
            for k,frame in enumerate(frames):strip.paste(frame,(k*324,n*180+20))
        strip.save(out/'motion_strip.png')
    assert initial==sources(),'compiled sources changed during tests/captures'
    assert tools=={p:sha(ROOT/p) for p in tools},'runner changed during proof'
    for p,digest in {**protected,**originals}.items():assert sha(ROOT/p)==digest,('protected modified',p)
    core_receipt=BASE/'core/run-20260912T202805.630493Z/verification.json';core=json.loads(core_receipt.read_text())
    for p,digest in {**core['current_core'],**core['harness']}.items():assert sha(ROOT/p)==digest,('core authority changed',p)
    proof.update(status='pass',frames=rows,peak_paired_objects=peak,protected_verified=len(protected),core_receipt_sha256=sha(core_receipt),
        limitations=['focused real C++ controller+software raster, not GPU/PICA/hardware FPS/listening proof',
        'synthetic telemetry pixel oracle is separate from real legacy/Q32 engine integration',
        'native glyph comparisons include all host pixels; no old slash exclusions in feedback oracle',
        'host trace budget, not hardware resource acceptance; allocations tested separately by scalar/core proofs',
        'only new focused suites + explicit neighbors, not generic/full-suite success',
        'no ARM/FTP/CIA/deployment/material deletion; old captures/studies/proofs protected'])
    proof['evidence']={str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file() and p.name!='verification.json'}
    save();shutil.copy2(out/'verification.json',parent/'verification.json')
    print('pass:',out.relative_to(ROOT),len(proof['screens']),'native screens',len(rows),'duals',len(proof['animations']),'animations; paired',peak,'/8192',flush=True)

if __name__=='__main__':
    main()
