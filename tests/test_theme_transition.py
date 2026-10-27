#!/usr/bin/env python3
"""Current palette role/transition test, no removed startup cache/GPU fixtures."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    with tempfile.TemporaryDirectory(prefix='seance-palette-') as tmp:
        exe=Path(tmp)/'test'
        subprocess.run(['g++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','tests/palette_roles_test.cpp','-o',str(exe)],cwd=ROOT,check=True)
        subprocess.run([str(exe)],cwd=ROOT,check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
if __name__=='__main__':main()
