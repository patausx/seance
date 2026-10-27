#!/usr/bin/env python3
"""Compile/run only the real PLAY editor core regression; no renders or deployment."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='seance-play-editor-') as tmp:
    binary = pathlib.Path(tmp) / 'core-test'
    sources = ['tools/play_editor_core_test.cpp']
    sources += [str(p.relative_to(ROOT)) for p in sorted((ROOT / 'core/engine').glob('*.cpp'))]
    sources += ['core/synth/sampler.cpp',
                'core/synth/wav_loader.cpp', 'core/synth/wav_writer.cpp']
    subprocess.run(['g++', '-O2', '-std=gnu++17', '-Wall', '-Wextra', '-Icore',
                    *sources, '-o', str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], cwd=ROOT, check=True)
