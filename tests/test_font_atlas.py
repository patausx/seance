#!/usr/bin/env python3
"""One real ARM-conditional font-atlas host regression; no renders/full suites."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="seance-font-atlas-", dir="/tmp") as tmp:
        binary = Path(tmp) / "font_atlas_test"
        command = [
            "g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-D__3DS__", "-Itests/paper_motion_test_stubs",
            "tests/font_atlas_test.cpp", "core/ui/draw.cpp", "core/ui/font.cpp",
            "-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc", "-o", str(binary),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
