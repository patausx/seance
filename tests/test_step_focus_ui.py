#!/usr/bin/env python3
"""Focused approved moving event/static flow selection proof; never ARM/FTP/CIA."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import time
from PIL import Image, ImageDraw, ImageChops

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', help='fresh output directory; never overwrites historical receipts')
    ap.add_argument('--before', help='optional same-contract capture baseline for exact optimized/sanitized twins')
    ap.add_argument('--binary', help='default: retained executable inside fresh output directory')
    ap.add_argument('--feedback', action='store_true', help='also assert the unchanged native playback/edit feedback law')
    ap.add_argument('--no-build', action='store_true')
    ap.add_argument('--sanitize', action='store_true')
    neighbors = ap.add_mutually_exclusive_group()
    neighbors.add_argument('--neighbors', action='store_true', help='also run banks/slice/cuts; the known baseline slice failure remains a hard failure')
    neighbors.add_argument('--no-neighbors', dest='neighbors', action='store_false', help=argparse.SUPPRESS)
    ap.set_defaults(neighbors=False)
    ap.add_argument('--no-captures', action='store_true')
    args = ap.parse_args()
    timestamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out = ROOT / (args.out or ('release/score-selection/'+('sanitized-' if args.sanitize else 'optimized-')+timestamp))
    out.mkdir(parents=True, exist_ok=False)
    binary = Path(args.binary) if args.binary else out/'ui-host'
    assert not args.no_build or args.binary, '--no-build requires an explicit retained binary'
    stamp = binary.with_name(binary.name+'.sources.json')
    def compiled_sources():
        paths=[p for d in ('core','platform') for p in (ROOT/d).rglob('*')
               if p.is_file() and p.suffix in ('.h','.hpp','.c','.cpp','.inc')]
        paths.extend(p for p in (ROOT/'tests').rglob('*') if p.is_file() and p.suffix in ('.h','.hpp','.inc'))
        paths += [ROOT/'tests/sequencer_test.cpp',ROOT/'Makefile',Path(__file__).resolve()]
        return {str(p.relative_to(ROOT)):sha(p) for p in sorted(paths)}
    source_hashes=compiled_sources()
    # Retain exact inputs even when compile/assertions fail; never repair or
    # overwrite an earlier attempt after the test contract changes.
    for name,digest in source_hashes.items():
        source=ROOT/name;target=out/'inputs'/name
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
        assert sha(target)==digest, (name,'input changed while retaining evidence')
    (out/'retained-inputs.json').write_text(json.dumps(source_hashes,indent=2)+'\n')
    attempt=1
    while (out/f'attempt-{attempt}.json').exists():attempt+=1
    attempt_path=out/f'attempt-{attempt}.json'
    runs = []
    def run(cmd, label, theme='pink', timeout=300):
        logs = out/'checks'; logs.mkdir(exist_ok=True)
        n=1
        while (logs/f'{label}-{n}.log').exists(): n+=1
        log=logs/f'{label}-{n}.log'
        env=dict(os.environ,SEANCE_TEST_THEME=theme)
        if args.sanitize:
            env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        before=time.monotonic()
        timed_out=False
        try:
            p=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,text=True,capture_output=True,timeout=timeout)
        except subprocess.TimeoutExpired as error:
            timed_out=True
            decode=lambda value: value.decode(errors='replace') if isinstance(value,bytes) else (value or '')
            p=subprocess.CompletedProcess(list(map(str,cmd)),124,decode(error.stdout),decode(error.stderr)+f'\ntimeout after {timeout}s\n')
        log.write_text(p.stdout+p.stderr)
        runs.append(dict(command=list(map(str,cmd)),theme=theme,seconds=round(time.monotonic()-before,3),returncode=p.returncode,timed_out=timed_out,log=str(log.relative_to(ROOT)),log_sha256=sha(log)))
        attempt_path.write_text(json.dumps(runs,indent=2)+'\n')
        (out/'runs.json').write_text(json.dumps(runs,indent=2)+'\n')
        print(label,p.returncode,runs[-1]['seconds'],flush=True)
        if p.returncode:
            print(p.stdout[-4000:]+p.stderr[-8000:]);p.check_returncode()
        return p
    if not args.no_build:
        # Ask make for its authoritative source/flag expansion without executing
        # the legacy generic suite or adding another runtime build recipe.
        dry=run(['make','--no-print-directory','-n','test-sequencer-menu'],'build-recipe')
        command=next(line for line in dry.stdout.splitlines() if line.startswith('g++ '))
        build=shlex.split(command)
        build[build.index('-o')+1]=str(binary)
        if args.sanitize:
            build[build.index('-O2')]='-O1'
            build[1:1]=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
        run(build,'build',timeout=600 if args.sanitize else 300)
        assert source_hashes==compiled_sources(), 'sources changed during compile; rebuild'
        stamp.write_text(json.dumps(dict(binary_sha256=sha(binary),sources=source_hashes),indent=2)+'\n')
    else:
        built=json.loads(stamp.read_text())
        assert built['sources']==source_hashes and built['binary_sha256']==sha(binary), 'stale binary; rebuild'
    for theme in ('pink','gray'):
        run([binary,'--step-focus-only'],theme+'-focus',theme)
        if args.feedback:
            run([binary,'--playhead-press-only'],theme+'-feedback',theme)
        if args.neighbors:
            for suite in ('sequence-banks','slice','cuts'):
                run([binary,'--'+suite+'-only'],theme+'-'+suite,theme)
        if not args.no_captures:
            run([binary,'--dump-step-focus',out/theme],theme+'-capture',theme)
            if args.feedback:
                run([binary,'--dump-playhead-press',out/(theme+'-feedback')],theme+'-feedback-capture',theme)
    capture_sets=[(theme,suite,theme if suite=='focus' else theme+'-feedback')
                  for suite in (('focus','feedback') if args.feedback else ('focus',)) for theme in ('pink','gray')]
    screens=[];animations=[];rows=[];cards=[];peak=0
    if not args.no_captures:
        for theme,suite,folder in capture_sets:
            theme_rows=list(csv.DictReader((out/folder/'manifest.tsv').open(),delimiter='\t'))
            groups={}
            for row in theme_rows:
                shared=int(row['shared_objects']);peak=max(peak,shared)
                assert shared<8192 and int(row['out_of_bounds'])==0
                top_path=out/folder/(row['frame']+'_top.ppm');bottom_path=out/folder/(row['frame']+'_bottom.ppm')
                top=Image.open(top_path).convert('RGB');bottom=Image.open(bottom_path).convert('RGB')
                assert top.size==(400,240) and bottom.size==(320,240)
                for path,im in ((top_path,top),(bottom_path,bottom)):
                    screens.append(dict(path=str(path.relative_to(ROOT)),sha256=sha(path),pixels=im.width*im.height))
                # Neither screen is a frozen compositor substrate: every source
                # image was rendered by the real C++ capture at its own timestamp.
                dual=Image.new('RGB',(400,488),(16,16,16));dual.paste(top,(0,0));dual.paste(bottom,(40,248))
                groups.setdefault(row['scenario'],[]).append(dual)
                row=dict(row,theme=theme,suite=suite);rows.append(row)
            for scenario,frames in groups.items():
                if len(frames)>1:
                    path=out/f'{theme}_{suite}_{scenario}.png'
                    duration={'note_journey':20,'playhead':16}.get(scenario,10)
                    durations=[duration]*len(frames);durations[-1]=300
                    frames[0].save(path,save_all=True,append_images=frames[1:],duration=durations,loop=0,optimize=False)
                    with Image.open(path) as decoded:
                        decoded_frames=decoded.n_frames
                        cursor=0;remaining=0;decoded_image=None;total=0
                        for frame,duration in zip(frames,durations):
                            if not remaining:
                                decoded.seek(cursor);cursor+=1
                                decoded_image=decoded.convert('RGB').copy()
                                remaining=round(decoded.info.get('duration',sum(durations) if decoded_frames==1 else -1));total+=remaining
                            assert frame.tobytes()==decoded_image.tobytes(), (theme,scenario,cursor)
                            remaining-=duration;assert remaining>=0
                        assert remaining==0 and cursor==decoded_frames and total==sum(durations)
                    animations.append(dict(path=str(path.relative_to(ROOT)),sha256=sha(path),source_frames=len(frames),decoded_frames=decoded_frames,duration_ms=sum(durations),format='APNG' if decoded_frames>1 else 'PNG (constant native frames)',native_rgb_exact=True))
                if suite=='focus' and scenario in ('layer_1','layer_4','flow','out_of_length','ring','cuts'):
                    cards.append((theme+' / '+scenario,frames[min(5,len(frames)-1)]))
        sheet=Image.new('RGB',(6*410,2*515),(16,16,16));draw=ImageDraw.Draw(sheet)
        for n,(label,im) in enumerate(cards):
            x=n%6*410;y=n//6*515;draw.text((x+5,y+5),label,fill=(244,244,244));sheet.paste(im,(x+5,y+22))
        sheet.save(out/'contact_sheet.png')
    preservation=[]
    before=ROOT/args.before if args.before else None
    if not args.no_captures and before is not None:
        assert before.is_dir(), 'explicit comparison baseline missing'
        for theme,suite,folder in capture_sets:
            def indexed(directory):
                groups={}
                for row in csv.DictReader((directory/'manifest.tsv').open(),delimiter='\t'):
                    groups.setdefault(row['scenario'],[]).append(row)
                return groups
            old=indexed(before/folder);new=indexed(out/folder)
            assert old.keys()==new.keys(), (theme,'scenario set changed')
            for scenario,old_rows in old.items():
                assert len(old_rows)==len(new[scenario]),scenario
                for a,b in zip(old_rows,new[scenario]):
                    assert a['ms']==b['ms']
                    entry=dict(theme=theme,suite=suite,scenario=scenario,ms=int(a['ms']))
                    for screen in ('top','bottom'):
                        pa=before/folder/(a['frame']+'_'+screen+'.ppm');pb=out/folder/(b['frame']+'_'+screen+'.ppm')
                        first=Image.open(pa).convert('RGB');second=Image.open(pb).convert('RGB')
                        diff=ImageChops.difference(first,second);r,g,blue=diff.split()
                        mask=ImageChops.lighter(ImageChops.lighter(r,g),blue)
                        changed=first.width*first.height-mask.histogram()[0]
                        # Explicit --before is a same-contract optimized/sanitized
                        # twin. Never silently waive a layer, rail or native pixel.
                        assert changed==0,(theme,scenario,screen,a['ms'],diff.getbbox())
                        entry[screen]=dict(before=str(pa.relative_to(ROOT)),before_sha256=sha(pa),after=str(pb.relative_to(ROOT)),after_sha256=sha(pb),pixels=first.width*first.height,changed_pixels=changed)
                    preservation.append(entry)
    runtime={str(p.relative_to(ROOT)):sha(p) for d in ('core','platform') for p in (ROOT/d).rglob('*') if p.is_file() and p.suffix in ('.h','.hpp','.c','.cpp','.inc')}
    tools={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'tests/sequencer_test.cpp',ROOT/'tests/step_focus_ui_test.h',ROOT/'tests/score_selection_oracle.h',ROOT/'tests/playhead_press_ui_test.h',ROOT/'tests/sequencer_test_stubs/3ds.h',Path(__file__),ROOT/'Makefile']}
    assert source_hashes==compiled_sources(), 'sources changed during test/capture; rebuild and rerun'
    receipt=dict(result='pass',sanitized=args.sanitize,contract='event: native inverse18x18 chamfered plate, original100ms travel+hit+press including crossed unselected flow lanes; flow/TRIG: static18x10 actual-value plate, clipped inverse native curve+radius1, resets event travel; final2x2 compact dice; zero waived pixels',feedback_suite=args.feedback,runs=runs,attempt_log=str(attempt_path.relative_to(ROOT)),binary=dict(path=str(binary),sha256=sha(binary),sources_sha256=sha(stamp)),runtime_hashes=runtime,tool_hashes=tools,
                 retained_inputs=dict(directory=str((out/'inputs').relative_to(ROOT)),count=len(source_hashes),manifest=str((out/'retained-inputs.json').relative_to(ROOT)),manifest_sha256=sha(out/'retained-inputs.json')),
                 screens=screens,animations=animations,frames=rows,peak_paired_objects=peak,preservation=preservation,
                 limits=['focused host controller + software raster, not PICA/GPU/hardware/listening proof',
                         'no full suite or generic sequencer suite; generic failures, if separately run, have their own logs',
                         'native captures update both screens independently; no frozen app compositor',
                         'no ARM/FTP/CIA; only new release evidence, no historical receipt rewriting'])
    (out/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(f'pass: {len(screens)} native screens / {len(rows)} duals / {len(animations)} exact animation containers; peak paired {peak}/8192')

if __name__=='__main__':
    main()
