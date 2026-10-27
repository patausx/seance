#!/usr/bin/env python3
"""Real REC WAV/SNC storage proof with current exact main save/load, ASAN/UBSAN."""
from datetime import datetime,timezone
from pathlib import Path
import hashlib,json,os,subprocess,tempfile
import patch_menu_runtime as extract
import test_editor_fullscreen_ui as host
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'release/rec-dictaphone'
def inputs():
    paths=[p for d in ('core','platform/3ds') for p in sorted((ROOT/d).rglob('*')) if p.is_file() and p.suffix in ('.cpp','.h','.inc')]
    paths+=list((ROOT/'tests').rglob('*.h'))
    paths+=[ROOT/p for p in ('tests/test_rec_dictaphone_io.py','tests/rec_dictaphone_io_cases.inc','tests/audit_safety_test.cpp',
        'tests/menu_stage1_test.cpp','tests/patch_menu_runtime.py','tests/test_editor_fullscreen_ui.py')]
    return {str(p.relative_to(ROOT)):host.sha(p) for p in sorted(set(paths))}
def main():
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ');work=Path(tempfile.mkdtemp(prefix='seance-rec-disk-'));before=inputs()
    extract.GEN=work;extract.extract();source=(ROOT/'platform/3ds/main.cpp').read_text()
    (work/'audit_project_main.inc').write_text('\n'.join(extract.function(source,name) for name in ('project_recordings_ready','save_current_patch','transactional_patch_load')))
    text=(ROOT/'tests/audit_safety_test.cpp').read_text();assert text.count('int main(){try{')==1
    text=text.replace('int main(){try{','int inherited_audit_main(){try{')+'\n'+(ROOT/'tests/rec_dictaphone_io_cases.inc').read_text()
    text=text.replace('#include "../','#include "'+str(ROOT)+'/');driver=work/'driver.cpp';driver.write_text(text)
    sources=[driver]+[ROOT/('platform/3ds/'+n+'.cpp') for n in ('ui_nav','ui_picker','ui_chains','ui_workspace','ui_transition','menu_style','project_names','ui_chains_top','ui_theme')]
    sources += [ROOT/'core/ui/draw.cpp',ROOT/'core/ui/font.cpp',*sorted((ROOT/'core/engine').glob('*.cpp'))]
    sources += [ROOT/('core/'+n) for n in ('synth/sampler.cpp','synth/wav_loader.cpp','synth/wav_writer.cpp')]
    exe=work/'rec-disk-sanitized';flags=['-std=gnu++17','-O0','-g1','-Wall','-Wextra','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Wl,--wrap=rename','-Wl,--wrap=_ZN6seance2ui4textEiiPKcji']
    cmd=[os.environ.get('CXX','g++'),*flags,'-I'+str(ROOT/'tests/menu_stage1_test_stubs'),'-I'+str(ROOT/'tests/sequencer_test_stubs'),'-I'+str(ROOT/'tests'),'-I'+str(ROOT/'core'),'-I'+str(work),*map(str,sources),'-o',str(exe)]
    status=host.run(cmd,OUT/('io-build-'+stamp+'.log'))
    if status:raise SystemExit(status)
    log=OUT/('io-check-'+stamp+'.log')
    with log.open('x') as f:
        run=subprocess.run([str(exe)],cwd=work,stdout=f,stderr=subprocess.STDOUT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
    print(log.read_text(),flush=True)
    result=dict(status='passed' if run.returncode==0 else 'failed',inputs=before,inputs_still_exact=before==inputs(),binary=str(exe),binary_sha256=host.sha(exe),command=cmd,log=str(log.relative_to(ROOT)),sanitizers='ASAN/UBSAN/leaks',device=False,deployed=False)
    (OUT/('io-proof-'+stamp+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    if run.returncode:raise SystemExit(run.returncode)
    assert result['inputs_still_exact'],'inputs changed during build/check; repeat after freeze'
    (OUT/'io-proof.json').write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
