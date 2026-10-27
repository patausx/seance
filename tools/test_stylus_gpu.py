#!/usr/bin/env python3
"""Real stylus GPU adapter against a deferred-batch API/color simulation. No media."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
main = (ROOT/'platform/3ds/main.cpp').read_text()
# Both exit branches must fence/unbind BEFORE freeing the optional texture.
for block in re.findall(r'if \(C3D_FrameBegin\(0\)\) \{(.*?)\n\s*}', main, re.S):
    if 'stylus_feedback::shutdown();' in block:
        assert block.index('C2D_Prepare();') < block.index('C3D_TexBind(0, nullptr);') < block.index('C3D_FrameEnd(0);') < block.index('stylus_feedback::shutdown();')
assert main.count('stylus_feedback::shutdown();') == 2
adapter = (ROOT/'platform/3ds/stylus_feedback.cpp').read_text()
assert 'gfxGetFramebuffer' not in adapter and 'GX_' not in adapter
assert 'osGetTime' not in adapter and 'hidTouchRead' not in adapter
with tempfile.TemporaryDirectory(prefix='seance-stylus-gpu-') as tmp:
    binary = Path(tmp)/'test'
    subprocess.run(['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
        '-Itools/stylus_test_stubs', '-I.', 'tools/stylus_gpu_test.cpp',
        'platform/3ds/stylus_feedback.cpp', '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True, env={**os.environ,
        'ASAN_OPTIONS':'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'})
