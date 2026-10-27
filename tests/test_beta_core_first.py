#!/usr/bin/env python3
"""Isolated beta core-first native sound/persistence proof (no ARM, FTP, user data).

Retains the expected pre-fix failure, compiles/runs only current scoped DSP and
SNC tests (max two compiler workers), checks byte-exact preservation of all
unapproved code, and publishes new receipts only under release/beta-20261004.
"""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'release/beta-20261004/core-first'
BASE=ROOT/'release/module-headings/baseline'
INPUTS=('core/engine/units.h','core/engine/units.cpp','core/engine/unit.h',
        'core/engine/engine.cpp','core/engine/engine.h','core/engine/patch_io.cpp','core/engine/patch_io.h',
        'core/engine/workspace_layout.cpp','core/synth/sampler.cpp','core/synth/wav_loader.cpp',
        'tests/beta_core_first_test.cpp','tests/delay_dsp_test.cpp','tests/delay_persistence_test.cpp','tests/chorus_dsp_test.cpp')
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def write(p,obj):p.write_text(json.dumps(obj,indent=2,sort_keys=True)+'\n')
def stamp():return datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
def static_contract():
    before=(BASE/'core/engine/units.cpp').read_text();after=(ROOT/'core/engine/units.cpp').read_text()
    original=(BASE/'core/engine/units.h').read_text();header=(ROOT/'core/engine/units.h').read_text()
    h_insert='    // Audio history exists only after a positive legacy process, not on allocation.\n    bool legacy_processed_ = false;\n'
    assert header.count(h_insert)==1;assert header.replace(h_insert,'')==original,'unit.h differs beyond private runtime bit'
    patches=[('    legacy_processed_ = false;\n',''),
        ('    state.ready = modern_ ? modern_ready_ : legacy_processed_;','    state.ready = modern_ ? modern_ready_ : ready();'),
        ('    legacy_processed_ = true;\n',''),
        ('void FoldUnit::process(q15* buf, int frames) {\n    if (!buf || frames <= 0) return;','void FoldUnit::process(q15* buf, int frames) {'),
        ('void BitUnit::process(q15* buf, int frames) {\n    if (!buf || frames <= 0) return;','void BitUnit::process(q15* buf, int frames) {'),
        ('void ChorusUnit::process(q15* buf, int frames) {\n    if (!buf_ || !buf || frames <= 0) return;','void ChorusUnit::process(q15* buf, int frames) {\n    if (!buf_ || frames <= 0) return;')]
    for new,old in patches:assert after.count(new)==1,('missing/duplicate approved edit',new);after=after.replace(new,old,1)
    assert after==before,'units.cpp differs beyond approved private visual bit and invalid-call guards'
    frozen=json.loads((BASE/'sources.json').read_text())
    assert all(sha(BASE/name)==digest for name,digest in frozen.items()),'frozen baseline was changed'
    assert sha(ROOT/'core/engine/unit.h')==frozen['core/engine/unit.h']
    for path in ('core/engine/engine.cpp','core/engine/engine.h','core/engine/patch_io.cpp','core/engine/patch_io.h',
                 'core/engine/workspace_layout.cpp','core/synth/sampler.cpp','core/synth/wav_loader.cpp'):
        assert sha(ROOT/path)==frozen[path],('unexpected production core change',path)
    return {'legacy_pcm_loop_old_sha256':sha(BASE/'core/engine/units.cpp'),
            'before_units_h_sha256':sha(BASE/'core/engine/units.h'),
            'current_units_cpp_sha256':sha(ROOT/'core/engine/units.cpp'),
            'current_units_h_sha256':sha(ROOT/'core/engine/units.h'),
            'byteexact_old_source_after_removing_only_six_approved_edits':True,
            'frozen_originals_verified':len(frozen),'unrelated_engine_storage_inputs_byteexact':True}

def run(command,log,timeout=300):
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    with log.open('x') as stream:
        stream.write('$ '+shlex.join(map(str,command))+'\n');stream.flush()
        proc=subprocess.run(command,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
    return proc.returncode

def preproof():
    old=OUT/'before';binary=old/'probe';assert binary.is_file()
    logs=['compile.log','readiness-failed.log','readiness-exit.txt','invalid-calls.log','invalid-exit.txt','pcm.log','pcm-exit.txt','probe-source.cpp']
    hashes={n:sha(old/n) for n in logs}
    assert (old/'readiness-exit.txt').read_text().strip()=='1'
    assert 'FAIL tests/beta_core_first_test.cpp:23: !u.visual_state().ready' in (old/'readiness-failed.log').read_text()
    assert (old/'invalid-exit.txt').read_text().strip()=='0'
    for kind in ('Fold','Bit','Chorus'):
        assert f'{kind} nullptr/positive: signal:11' in (old/'invalid-calls.log').read_text(),kind
    assert (old/'pcm-exit.txt').read_text().strip()=='0'
    output=(old/'pcm.log').read_text();expected={'fold':'b7177876041f37e1','bit':'dcf06432aee9618e','chorus':'df210a2f9bc41ae4'}
    for n,h in expected.items():assert f'{n} valid PCM 7168 fnv64={h}' in output
    assert sha(BASE/'core/engine/units.cpp')=='049580896fd7e651ed331b9f06b6e219141dd5287ad407d6237789d2dfda4e17'
    proof={'readiness_expected_failure':True,'readiness_exit':1,'invalid_inputs_expected_SIGSEGV11':['Fold','Bit','Chorus'],
           'baseline_input_pcm_fnv64':expected,'probe_binary_sha256':sha(binary),'saved_source_sha256':hashes['probe-source.cpp'],
           'logs_and_inputs_sha256':hashes,'baseline_units_cpp_sha256':sha(BASE/'core/engine/units.cpp')}
    path=OUT/'before-reproduction.json';write(path,proof);return proof

def build_variant(folder,sanitize):
    paths={name:sha(ROOT/name) for name in INPUTS};flags=['-O1','-g1'] if sanitize else ['-O2']
    flags+=['-std=gnu++17','-Wall','-Wextra','-ffunction-sections','-fdata-sections','-Icore']
    if sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    compiler=os.environ.get('CXX','g++');link=['-Wl,--gc-sections']
    cases={'probe':['tests/beta_core_first_test.cpp','core/engine/units.cpp'],
           'delay-dsp':['tests/delay_dsp_test.cpp','core/engine/units.cpp'],
           'chorus-dsp':['tests/chorus_dsp_test.cpp','core/engine/units.cpp'],
           'delay-persistence':['tests/delay_persistence_test.cpp','core/engine/engine.cpp','core/engine/units.cpp',
                                'core/engine/patch_io.cpp','core/engine/workspace_layout.cpp',
                                'core/synth/sampler.cpp','core/synth/wav_loader.cpp']}
    commands={name:[compiler,*flags,*units,*link,'-o',str(folder/name)] for name,units in cases.items()}
    proof={'source_pins_sha256':paths,'compiler':compiler,'build_commands':commands,'workers':2,'checks':{},'status':'building',
           'host_only':True,'arm':False,'deployment':False,'test_file_io':'delay-persistence uses own pid-named synthetic /tmp SNC and removes it'}
    receipt=folder/'receipt.json';write(receipt,proof)
    def compile_one(item):
        name,command=item;log=folder/(name+'-build.log');status=run(command,log)
        print(folder.name,'compile',name,status,flush=True);return name,status,log
    with ThreadPoolExecutor(max_workers=2) as executor:
        for name,status,log in executor.map(compile_one,commands.items()):
            proof['checks'][name]={'compile_exit':status,'compile_log':str(log),'compile_log_sha256':sha(log)}
            write(receipt,proof)
    if any(item['compile_exit'] for item in proof['checks'].values()):proof['status']='compile failed';write(receipt,proof);raise RuntimeError(str(receipt))
    for name in cases:
        binary=folder/name;proof['checks'][name]['binary_sha256']=sha(binary)
        opts=['--readiness','--invalid','--pcm'] if name=='probe' else [None]
        for option in opts:
            key=option.lstrip('-') if option else 'focused'
            log=folder/(name+'-'+key+'.log');command=[str(binary),*([option] if option else [])]
            status=run(command,log);proof['checks'][name][key]={'exit':status,'log':str(log),'sha256':sha(log)}
            print(folder.name,'run',name,key,status,flush=True)
            write(receipt,proof)
            if status:proof['status']='run failed';write(receipt,proof);raise RuntimeError(str(receipt))
    assert paths=={name:sha(ROOT/name) for name in INPUTS},'sources changed during scoped builds'
    assert static_contract()['byteexact_old_source_after_removing_only_six_approved_edits']
    proof['status']='passed';write(receipt,proof);return proof

def main():
    OUT.mkdir(parents=True,exist_ok=True);before=preproof();guards=static_contract()
    folder=OUT/('after-'+stamp());folder.mkdir()
    (folder/'runner-source.py').write_bytes(Path(__file__).read_bytes());(folder/'probe-source.cpp').write_bytes((ROOT/'tests/beta_core_first_test.cpp').read_bytes())
    result={'status':'pending','before':before,'static_preservation':guards,'runner_sha256':sha(__file__),
            'test_input_sha256':sha(ROOT/'tests/beta_core_first_test.cpp'),'builds':{},'host_only':True,'hardware':False,'deployment':False,
            'scope':'Delay/Chorus/Fold/Bit engine only; no UI suite or ARM; synthetic PCM and SNC only'}
    path=folder/'final-receipt.json';write(path,result)
    try:
        for mode,sanitize in (('optimized',False),('sanitized',True)):
            stage=folder/mode;stage.mkdir();result['builds'][mode]=build_variant(stage,sanitize);write(path,result)
        old=before['baseline_input_pcm_fnv64']
        for mode in result['builds']:
            checks=result['builds'][mode]['checks'];pcm=Path(checks['probe']['pcm']['log']).read_text()
            for name,fnv in old.items():assert f'{name} valid PCM 7168 fnv64={fnv}' in pcm,(mode,name)
            invalid=Path(checks['probe']['invalid']['log']).read_text()
            for name in ('Fold','Bit','Chorus'):assert f'{name} nullptr/positive: exit:0' in invalid,(mode,name)
            assert 'ok frozen independent legacy PCM 72000 samples fnv64=310f7ae0e4225258' in Path(checks['delay-dsp']['focused']['log']).read_text()
            assert 'delay dsp: 5/5 passed' in Path(checks['delay-dsp']['focused']['log']).read_text()
            assert 'chorus dsp: 6/6 passed' in Path(checks['chorus-dsp']['focused']['log']).read_text()
            assert 'delay persistence: 4/4 passed' in Path(checks['delay-persistence']['focused']['log']).read_text()
        result['valid_pcm_unchanged_fnv64']=old;result['legacy_pcm_unchanged_72000_fnv64']='310f7ae0e4225258';result['status']='passed'
        write(path,result);write(OUT/'latest.json',{'receipt':str(path),'sha256':sha(path)})
        print(path)
    except Exception as error:
        import traceback
        result['status']='failed';result['failure']=str(error);result['traceback']=traceback.format_exc();write(path,result);raise

if __name__=='__main__':main()
