#!/usr/bin/env python3
"""Focused startup per-key-tail runtime proof; no ARM build, FTP or device claims."""
from pathlib import Path
from datetime import datetime, timezone
import argparse, hashlib, json, os, resource, shutil, subprocess
ROOT=Path(__file__).resolve().parents[1]
BASE=['-std=gnu++17','-g','-Wall','-Wextra','-Werror','-Wno-error=format-truncation','-Wno-unused-function','-Wno-unused-variable',
      '-Itests/sequencer_test_stubs','-Iplatform/3ds','-I.',
      '-Wl,--wrap=_ZN6seance2ui4textEiiPKcji']
LINK=['platform/3ds/ui_theme.cpp','platform/3ds/menu_style.cpp','core/ui/draw.cpp','core/ui/font.cpp']
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def no_core():resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def main_extract(source):
    start=source.index('    // Own ',source.index('project_ready = startup_choose(top, bot);'))
    end=source.index('    if (!project_ready) {',start)
    capture=source[start:end]
    start=source.index('        u32 down = hidKeysDown();',end)
    end=source.index('        nav_ink_input(down);',start)
    reads=source[start:end]
    return capture,reads

def extracted_test(capture,reads):
    return '''#include "platform/3ds/startup_input_tail.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using u32=std::uint32_t;
using seance::platform::ui3ds::StartupInputTail;
static u32 raw_down,raw_held,raw_up,observed_down,observed_held,observed_up;
namespace platform::debug_overlay {
void input(u32 down,u32 held,u32 up){observed_down=down;observed_held=held;observed_up=up;}
}
static int scans,apts,blanks,frames;
void hidScanInput(){++scans;}
u32 hidKeysDown(){return raw_down;}
u32 hidKeysHeld(){return raw_held;}
u32 hidKeysUp(){return raw_up;}
bool aptMainLoop(){return ++apts<32;}
void gspWaitForVBlank(){++blanks;}
void require(bool condition,const char* why){if(!condition){std::fprintf(stderr,"failed: %s\\n",why);std::exit(1);}}
int main(){
 for(int bit=0;bit<32;++bit){
  u32 captured=u32(1)<<bit,other=bit?1u:256u;
  raw_down=raw_up=0;raw_held=captured;scans=apts=blanks=0;
  bool project_ready=true;
'''+capture+'''
  require(project_ready&&scans==1&&apts==0&&blanks==0,"actual main capture never waits for stuck held input");
  auto frame=[&](){
   ++frames;
'''+reads+'''
   require(observed_down==raw_down&&observed_held==raw_held&&observed_up==raw_up,"diagnostic observes raw HID before tail filtering");
   return std::array<u32,3>{down,held,up};
  };
  for(int i=0;i<100;++i)require(frame()==std::array<u32,3>{0,0,0},"actual main suppresses held captured tail");
  raw_down=other;raw_held=captured|other;
  require(frame()==std::array<u32,3>{other,other,0},"actual main fresh other key works with captured key stuck");
  raw_down=0;raw_held=other;raw_up=captured;
  require(frame()==std::array<u32,3>{0,other,0},"actual main consumes captured release edge");
  raw_down=captured;raw_held=captured;raw_up=other;
  require(frame()==std::array<u32,3>{captured,captured,other},"actual main permits subsequent fresh press and unrelated up");
 }
 std::printf("ok actual extracted main capture + raw HID reads/filter: 32 bits / %d frames; no waiting/leaks\\n",frames);
}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--baseline-ui',type=Path,required=True)
    p.add_argument('--baseline-main',type=Path,required=True)
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    assert not (out/'receipt.json').exists(),'preserve prior receipt'
    before=out/'before';before.mkdir(exist_ok=True)
    for name,source in [('ui_start_menu.cpp',args.baseline_ui),('main.cpp',args.baseline_main)]:
        dest=before/name
        if not dest.exists():shutil.copyfile(source,dest)
        assert sha(dest)==sha(source)
    current_ui=ROOT/'platform/3ds/ui_start_menu.cpp';current_main=ROOT/'platform/3ds/main.cpp'
    prior_proof=ROOT/'release/device-recovery/menu-input-check-20261024T1740Z/proof.json'
    assert sha(before/'ui_start_menu.cpp')==json.loads(prior_proof.read_text())['sources']['platform/3ds/ui_start_menu.cpp']
    current_capture,current_reads=main_extract(current_main.read_text())
    old_capture,old_reads=main_extract((before/'main.cpp').read_text())
    # Placement/source guards supplement (do not replace) compiled runtime checks.
    assert 'while' not in current_capture and 'startup_input_tail.capture(0, hidKeysHeld());' in current_capture
    assert current_reads.count('startup_input_tail.filter(down, held, up);')==1
    assert current_reads.index('platform::debug_overlay::input(')<current_reads.index('startup_input_tail.filter(')
    text=current_main.read_text();assert text.index(current_capture)<text.index('g_has_dsp = audio.init(*g_eng);')
    assert text.index(current_reads)<text.index('        nav_ink_input(down);',text.index(current_reads))
    assert current_ui.read_text().split('void start_menu_capture(')[1]==(before/'ui_start_menu.cpp').read_text().split('void start_menu_capture(')[1]
    generated=out/'main-extracted.cpp';generated.write_text(extracted_test(current_capture,current_reads))
    old_generated=out/'main-before-extracted.cpp';old_generated.write_text(extracted_test(old_capture,old_reads))
    sources=['tests/startup_input_tail_test.cpp','tests/test_startup_input_tail.py',*LINK,
             'platform/3ds/ui_start_menu.cpp','platform/3ds/main.cpp','platform/3ds/startup_input_tail.h']
    deps=subprocess.check_output(['g++',*BASE[:1],'-Itests/sequencer_test_stubs','-Iplatform/3ds','-MM',
                                  'tests/startup_input_tail_test.cpp',*LINK],cwd=ROOT,text=True).replace('\\\n',' ').split()
    for name in deps:
        path=ROOT/name
        if path.is_file() and str(path.relative_to(ROOT)) not in sources:sources.append(str(path.relative_to(ROOT)))
    inventory={name:sha(ROOT/name) for name in sorted(sources)}
    results=[];log=out/'checks.log'
    with log.open('w') as stream:
        def run(cmd,expected=0,env=None):
            print('$ '+' '.join(map(str,cmd)),file=stream,flush=True)
            result=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                  text=True,preexec_fn=no_core)
            stream.write(result.stdout);stream.write(f'\nexit={result.returncode}\n');stream.flush();print(result.stdout,end='')
            if expected=='nonzero':assert result.returncode!=0,'old controller must reproduce failure'
            else:assert result.returncode==expected,f'command failed ({result.returncode}): {cmd}'
            results.append(dict(command=list(map(str,cmd)),exit=result.returncode,expected=expected))
            return result
        old_binary=out/'before-controller'
        run(['g++',*BASE,'-O2',f'-DSTART_MENU_CONTROLLER="{before / "ui_start_menu.cpp"}"',
             'tests/startup_input_tail_test.cpp',*LINK,'-o',old_binary])
        for theme in ['graphite','pink']:
            result=run([old_binary,'--repro'],expected='nonzero',env=dict(os.environ,SEANCE_TEST_THEME=theme))
            assert 'fresh dpad works while captured R never releases' in result.stdout
        old_app=out/'before-app-tail'
        run(['g++','-std=gnu++17','-O2','-I.',old_generated,'-o',old_app])
        result=run([old_app],expected='nonzero');assert 'actual main capture never waits' in result.stdout
        for mode,flags in [('optimized',['-O2']),('sanitized',['-O1','-fsanitize=address,undefined',
                               '-fno-omit-frame-pointer','-fno-pie','-no-pie'])]:
            binary=out/f'{mode}-controller'
            run(['g++',*BASE,*flags,'tests/startup_input_tail_test.cpp',*LINK,'-o',binary])
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
            for theme in ['graphite','pink']:run([binary],env=dict(env,SEANCE_TEST_THEME=theme))
            app=out/f'{mode}-app-tail'
            run(['g++','-std=gnu++17','-I.',*flags,generated,'-o',app]);run([app],env=env)
        # Existing scoped regression is run as-is, with new output and fresh media.
        regression=out/'cabinet-regression'
        run(['python3','tests/test_start_menu_cabinet.py','--capture','--output',regression])
        old_cabinet=out/'before-cabinet'
        run(['g++',*BASE,'-O2','tests/start_menu_cabinet_test.cpp',before/'ui_start_menu.cpp',*LINK,'-o',old_cabinet])
        media={}
        for theme in ['graphite','pink']:
            dest=out/'before-capture'/theme
            run([old_cabinet,'--capture',dest],env=dict(os.environ,SEANCE_TEST_THEME=theme))
            for image in sorted(dest.glob('*.ppm')):
                current=regression/'capture'/theme/image.name
                assert sha(image)==sha(current),f'render changed: {theme}/{image.name}'
                media[f'{theme}/{image.name}']=sha(image)
        run(['bash','-lc','make test-module-frame && python3 tests/test_nav_instant.py'])
    assert inventory=={name:sha(ROOT/name) for name in inventory},'source changed during proof'
    receipt=dict(at=datetime.now(timezone.utc).isoformat(),status='passed',sources=inventory,
                 baseline={name:sha(before/name) for name in ['ui_start_menu.cpp','main.cpp']},commands=results,
                 prior_1740_proof_sha256=sha(prior_proof),
                 main_scope='compiled actual extracted startup capture and raw-read/filter block + lexical dispatch-order guards; not full main executable',
                 media_before_after_byte_exact=media,prior_baseline_proof='release/device-recovery/menu-input-check-20261024T1740Z/proof.json (no capture media existed)',
                 limits='native software only; no device held-bit diagnosis, GPU/FPS/audio, ARM build, FTP or installation')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    proof=dict(status='passed',inputs=inventory,
               evidence_sha256={str(path.relative_to(out)):sha(path) for path in sorted(out.rglob('*')) if path.is_file() and path!=out/'proof.json'},
               scope=receipt['main_scope'],limits=receipt['limits'])
    (out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('receipt:',out/'receipt.json');print('pinned proof:',out/'proof.json')
if __name__=='__main__':main()
