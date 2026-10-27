#!/usr/bin/env python3
"""Scoped cabinet proof + optional fresh C++ capture, no deployment or broad suites."""
from pathlib import Path
from datetime import datetime, timezone
import argparse, hashlib, json, os, subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/start_menu_cabinet_runtime/implemented'
SOURCES=['tests/start_menu_cabinet_test.cpp','platform/3ds/ui_start_menu.cpp',
         'platform/3ds/ui_theme.cpp','platform/3ds/menu_style.cpp','core/ui/draw.cpp','core/ui/font.cpp']
FLAGS=['-std=gnu++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
       '-fno-omit-frame-pointer','-fno-pie','-no-pie','-Itests/sequencer_test_stubs',
       '-Wl,--wrap=_ZN6seance2ui4textEiiPKcji']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',action='store_true')
    parser.add_argument('--output',type=Path,default=OUT,help='new receipt/log directory; override to preserve installed-release proofs')
    args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if out!=OUT.resolve():
        assert not (out/'proof.json').exists(),'preserve prior receipt; choose a new output directory'
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    log=out/f'check-{stamp}.log';binary=Path('/tmp/seance_start_menu_cabinet_ui_san')
    deps=subprocess.check_output(['g++','-std=gnu++17','-Itests/sequencer_test_stubs','-MM',*SOURCES],cwd=ROOT,text=True).replace('\\\n',' ').split()
    paths=set(SOURCES)
    for name in deps:
        p=ROOT/name
        if p.is_file():paths.add(str(p.resolve().relative_to(ROOT)))
    paths.update(['tools/generate_cabinet_font.py','assets/fonts/Gossip-MedPixel.otf','tests/test_start_menu_cabinet.py',
                  'tests/generate_wordmark_reference.py','tools/pixel_wordmark.py','assets/branding/gothic_wordmark_native_mask.png'])
    before={n:sha(ROOT/n) for n in sorted(paths)}
    command=['g++',*FLAGS,*SOURCES,'-o',str(binary)]
    with log.open('w') as stream:
        def run(cmd,env=None):
            print('$ '+' '.join(cmd),file=stream,flush=True)
            result=subprocess.run(cmd,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            stream.write(result.stdout);stream.flush();print(result.stdout,end='')
            result.check_returncode()
        run(['python3','tools/generate_cabinet_font.py','--check'])
        run(['python3','tests/generate_wordmark_reference.py','--check']);run(command)
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
        for theme in ['graphite','pink']:
            run([str(binary)],dict(env,SEANCE_TEST_THEME=theme))
        capture={}
        if args.capture:
            for theme in ['graphite','pink']:
                dest=out/'capture'/theme
                assert not dest.exists(),'preserve prior media'
                run([str(binary),'--capture',str(dest)],dict(env,SEANCE_TEST_THEME=theme))
                for p in dest.glob('*.ppm'):capture[str(p)]=sha(p)
    assert before=={n:sha(ROOT/n) for n in before}
    receipt=dict(at=datetime.now(timezone.utc).isoformat(),command=command,sources=before,
                 binary=str(binary),binary_sha256=sha(binary),log=str(log),
                 capture=capture,scope='7 current startup groups in each of two themes; no inherited legacy suite',
                 limits='software coverage, not GPU/FPS/audio or hardware; capture has 20px host paper fallback')
    (out/'proof.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('proof:',out/'proof.json')
if __name__=='__main__':main()
