#!/usr/bin/env python3
"""Focused universal8/PBAY-migration ASan/UBSan suite; retained target name, no UI/deploy."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="seance-patchbay-") as directory:
    exe = Path(directory) / "patchbay_core_test"
    sources = [ROOT / "tools/patchbay_core_test.cpp", *sorted((ROOT / "core/engine").glob("*.cpp")),
               *[ROOT / f"core/{p}" for p in ("synth/sampler.cpp",
                   "synth/wav_loader.cpp", "synth/wav_writer.cpp")]]
    subprocess.run([os.environ.get("CXX", "g++"), "-std=gnu++17", "-O1", "-g", "-Wall", "-Wextra",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                    "-I" + str(ROOT / "core"), *map(str, sources),
                    *["-Wl,--wrap=" + name for name in ("malloc", "calloc", "realloc", "_Znwm", "_Znam",
                        "_ZnwmRKSt9nothrow_t", "_ZnamRKSt9nothrow_t")],
                    "-o", str(exe)], check=True, cwd=ROOT)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
    subprocess.run([str(exe)], check=True, cwd=directory, env=env)
