#!/usr/bin/env python3
"""Current GO TO handoff math/rectangle contract; no images or old title shader."""
from pathlib import Path
import argparse, os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sanitize',action='store_true');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='seance-nav-handoff-') as tmp:
        exe=Path(tmp)/'test'
        flags=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie'] if args.sanitize else []
        subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',*flags,'tools/nav_ink_motion_test.cpp','-o',str(exe)],cwd=ROOT,check=True)
        subprocess.run([str(exe)],cwd=ROOT,check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
if __name__=='__main__':main()
