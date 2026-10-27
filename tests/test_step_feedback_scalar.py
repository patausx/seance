#!/usr/bin/env python3
"""Run pure feedback state/envelope proof, optimized and ASan/UBSan/leaks. No ARM."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['tests/test_step_feedback_scalar.py', 'tests/step_feedback_scalar_test.cpp',
           'platform/3ds/step_feedback.h', 'platform/3ds/step_focus_motion.h',
           'platform/3ds/sequencer_layout.h', 'platform/3ds/ui_palette.h']
def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    out = ROOT/'ui_mockups/playhead_press_runtime/scalar'/datetime.now(timezone.utc).strftime('run-%Y%m%dT%H%M%S.%fZ')
    out.mkdir(parents=True)
    sources = {p: sha(ROOT/p) for p in SOURCES}
    result = dict(status='incomplete', sources=sources, runs=[], evidence={},
                  limitations=['host scalar proof, not GPU or hardware timing',
                               'color byte oracle permits IEEE float half-integer rounding ambiguity only; native UI oracle separate'])
    for mode in ('optimized', 'sanitized'):
        binary = out/mode
        cmd = ['g++', '-std=gnu++17', '-Wall', '-Wextra', '-O2' if mode=='optimized' else '-O1']
        if mode=='sanitized':
            cmd += ['-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined', '-no-pie']
        cmd += ['tests/step_feedback_scalar_test.cpp', '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc', '-o', str(binary)]
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        for label, command in [('build', cmd), ('check', [str(binary)])]:
            start=time.monotonic()
            p=subprocess.run(command,cwd=ROOT,env=env,text=True,capture_output=True)
            log=out/f'{mode}-{label}.log';log.write_text(p.stdout+p.stderr)
            result['runs'].append(dict(command=command,returncode=p.returncode,seconds=round(time.monotonic()-start,3),log=str(log.relative_to(ROOT))))
            result['evidence'][str(log.relative_to(ROOT))]=sha(log)
            if binary.exists():result['evidence'][str(binary.relative_to(ROOT))]=sha(binary)
            (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
            print(mode,label,p.returncode,p.stdout,p.stderr,flush=True)
            p.check_returncode()
    assert sources=={p:sha(ROOT/p) for p in SOURCES}, 'proof sources changed'
    result['status']='pass'
    (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(out/'verification.json')
if __name__=='__main__':main()
