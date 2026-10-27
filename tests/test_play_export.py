#!/usr/bin/env python3
"""Focused real PLAY WAV export service; stubs only 3DS locking/UI globals."""
import pathlib
import subprocess
import tempfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='seance-play-export-') as directory:
    output = pathlib.Path(directory) / 'export-test'
    sources = ['tests/play_export_test.cpp', 'platform/3ds/ui_play_export.cpp']
    sources += [str(p.relative_to(ROOT)) for p in sorted((ROOT / 'core/engine').glob('*.cpp'))]
    sources += ['core/synth/sampler.cpp', 'core/synth/wav_loader.cpp', 'core/synth/wav_writer.cpp']
    subprocess.run(['g++', '-O2', '-std=gnu++17', '-Wall', '-Wextra', '-fno-builtin-memcpy',
                    '-Itests/play_export_test_stubs', '-Icore', *sources,
                    '-Wl,--wrap=fread,--wrap=fwrite,--wrap=rename,--wrap=memcpy', '-o', str(output)], cwd=ROOT, check=True)
    subprocess.run([str(output)], cwd=ROOT, check=True)
