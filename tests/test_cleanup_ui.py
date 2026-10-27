#!/usr/bin/env python3
"""Current minimal-L + ghost regression after cleanup; no historical preservation guards/media.

One fresh sanitized actual-main/editor executable. The existing point-in-time
release proofs stay immutable; this runner records its own source/binary hashes.
"""
from pathlib import Path
from datetime import datetime, timezone
import argparse, hashlib, json, os, subprocess, tempfile
from test_fader_lancet_ui import ROOT, WRAP, make_variable, sources
from test_mod_minimal_ui import extract_main

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--group',choices=['all','minimal','ghost'],default='all')
    parser.add_argument('--output',type=Path,default=ROOT/'.local-state/checks/cleanup-ui')
    args=parser.parse_args();stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out=args.output.resolve()/stamp;out.mkdir(parents=True,exist_ok=False)
    baseline=sources()
    for name in ['tests/test_cleanup_ui.py','tests/mod_minimal_ui_cases.inc','tests/ghost_refresh_ui_cases.inc']:
        baseline[name]=sha(ROOT/name)
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    report={'source_sha256':baseline,'media':False,'deployment':False,'checks':[]}
    def run(cmd,name):
        with (out/(name+'.log')).open('x') as log:
            log.write('$ '+' '.join(map(str,cmd))+'\n');log.flush()
            r=subprocess.run(cmd,cwd=ROOT,env=env,text=True,stdout=log,stderr=subprocess.STDOUT,timeout=300)
        print((out/(name+'.log')).read_text()[-14000:],flush=True)
        report['checks'].append({'name':name,'exit':r.returncode})
        (out/'proof.json').write_text(json.dumps(report,indent=2)+'\n');r.check_returncode()
    with tempfile.TemporaryDirectory(prefix='seance-cleanup-ui-') as temp:
        directory=Path(temp);driver=directory/'driver.cpp'
        original=(ROOT/'tests/osc_input_dispatch_test.cpp').read_text();entry='int main(int argc,char** argv) {'
        assert original.count(entry)==1
        text=original.replace(entry,'int archived_driver_main(int argc,char** argv) {',1)
        for name in ['mod_minimal_ui_cases.inc','ghost_refresh_ui_cases.inc']:
            part=(ROOT/'tests'/name).read_text();assert part.count('\nint main(')==1
            text+='\n'+part[:part.index('\nint main(')]
        text+='\nint main(int argc,char** argv){theme_test::select();if(argc!=2)return 2;if(std::string(argv[1])=="minimal")return mod_minimal_cases::run();if(std::string(argv[1])=="ghost")return ghost_refresh_cases::run();return 2;}\n'
        driver.write_text(text);report['extracted_main']=extract_main(directory)
        units=make_variable('OSC_INPUT_SOURCES');units[units.index('tests/osc_input_dispatch_test.cpp')]=str(driver)
        binary=directory/'test'
        command=[os.environ.get('CXX','g++'),'-O1','-g1',*make_variable('SEQUENCER_TEST_FLAGS'),'-DSEANCE_REAL_EDITOR','-Itests','-I'+str(directory),'-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie',*WRAP,*units,'-o',str(binary)]
        report['build_command']=command;run(command,'build');report['binary_sha256']=sha(binary)
        for group in (['minimal','ghost'] if args.group=='all' else [args.group]):run([str(binary),group],group)
        assert all((ROOT/p).is_file() and sha(ROOT/p)==digest for p,digest in baseline.items()),'sources changed during check'
    report['status']='passed';(out/'proof.json').write_text(json.dumps(report,indent=2)+'\n');print(out/'proof.json')
if __name__=='__main__':main()
