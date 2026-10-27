#!/usr/bin/env python3
"""Focused real pixel primitives with ASan/UBSan; no renders, ARM or deployment."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix="seance-pixel-strokes-") as tmp:
        binary = Path(tmp) / "test"
        command = [
            "g++", "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-Itools/sequencer_test_stubs", "tools/pixel_strokes_test.cpp",
            "core/ui/draw.cpp", "core/ui/font.cpp", "-o", str(binary),
        ]
        subprocess.run(command, cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
