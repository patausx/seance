#!/usr/bin/env python3
"""Render/check the README GIF using the unchanged production wordmark renderer.

Requires Python 3, Pillow and a C++17 host compiler (CXX, default c++).
No uploads, local artwork, fonts or network access are needed.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile

from PIL import Image, ImageSequence
import generate_wordmark_morph as artwork

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/branding/readme-wordmark.gif"
BACKGROUND = (33, 40, 48)  # #212830, approved README background.
INK = (244, 244, 244)
SIZE = (800, 320)
STEP_MS = 100
LOOP_MS = 48000
HELPER = r'''
#include "platform/3ds/wordmark_morph.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
namespace wm = seance::platform::ui3ds::wordmark_morph;
namespace data = seance::platform::ui3ds::wordmark_morph_data;
static_assert(wm::kHoldMs == 4000 && wm::kMorphMs == 8000 && wm::kLoopMs == 48000);
static_assert(data::kX == 8 && data::kY == 48 && data::kWidth == 384 && data::kHeight == 128);
int main() {
    std::array<std::uint8_t, data::kWidth * data::kHeight> previous{}, first{};
    for (std::uint32_t ms = 0; ms <= wm::kLoopMs; ms += 100) {
        decltype(previous) mask{};
        bool bounds = true;
        wm::render(ms, [&](int x, int y, int w, int h, std::uint8_t a) {
            x -= data::kX; y -= data::kY;
            if (x < 0 || y < 0 || x+w > data::kWidth || y+h > data::kHeight) {
                bounds = false; return;
            }
            for (int row = y; row < y+h; ++row)
                std::fill_n(mask.begin() + row * data::kWidth + x, w, a);
        });
        if (!bounds) return 2;
        if (ms == 0) first = mask;
        if (ms == wm::kLoopMs) return mask == first ? 0 : 3;
        if (ms % 12000 == 0 || mask != previous) {
            if (std::fwrite(&ms, sizeof(ms), 1, stdout) != 1 ||
                std::fwrite(mask.data(), mask.size(), 1, stdout) != 1) return 4;
        }
        previous = mask;
    }
    return 5;
}
'''


def production_masks():
    with tempfile.TemporaryDirectory(prefix="seance-readme-logo-") as tmp:
        source, binary = Path(tmp) / "render.cpp", Path(tmp) / "render"
        source.write_text(HELPER, encoding="ascii")
        command = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run([*command, "-std=c++17", "-O2", "-I", str(ROOT),
                        str(source), "-o", str(binary)], check=True)
        raw = subprocess.check_output([str(binary)])
    record = 4 + 384 * 128
    if len(raw) % record:
        raise RuntimeError("incomplete production renderer output")
    samples = []
    for offset in range(0, len(raw), record):
        ms = struct.unpack_from("=I", raw, offset)[0]
        samples.append((ms, Image.frombytes("L", (384, 128), raw[offset+4:offset+record])))
    if not samples or samples[0][0] != 0:
        raise RuntimeError("missing first frame")
    endpoints = artwork.masks()
    for index, expected in enumerate(endpoints):
        mask = next(mask for ms, mask in samples if ms == index * 12000)
        native = Image.new("L", (400, 240))
        native.paste(mask, (8, 48))
        if native.tobytes() != expected.tobytes():
            raise RuntimeError("production endpoint differs from approved artwork")
    return samples


def rgb_digest(image):
    return hashlib.sha256(image.convert("RGB").tobytes()).hexdigest()


def append_run(runs, digest, duration):
    if runs and runs[-1][0] == digest:
        runs[-1] = (digest, runs[-1][1] + duration)
    else:
        runs.append((digest, duration))


def frames_and_timeline():
    samples = production_masks()
    palette = [b + ((i - b) * alpha + 127) // 255
               for alpha in range(256) for b, i in zip(BACKGROUND, INK)]
    frames, durations, runs = [], [], []
    for index, (ms, mask) in enumerate(samples):
        end = samples[index+1][0] if index+1 < len(samples) else LOOP_MS
        duration = end-ms
        if duration <= 0 or duration % STEP_MS:
            raise RuntimeError("invalid production sample timing")
        # Native crop y=36..196, then exact nearest-neighbor 2x expansion.
        frame = Image.new("P", (400, 160), 0)
        frame.putpalette(palette)
        frame.paste(mask, (8, 12))
        frame = frame.resize(SIZE, Image.Resampling.NEAREST)
        frames.append(frame)
        durations.append(duration)
        append_run(runs, rgb_digest(frame), duration)
    if sum(durations) != LOOP_MS:
        raise RuntimeError("incomplete animation cycle")
    return frames, durations, runs


def check_gif(path, expected):
    actual = []
    with Image.open(path) as gif:
        if gif.size != SIZE or gif.info.get("loop") != 0:
            raise RuntimeError("GIF size or infinite-loop flag differs")
        count = gif.n_frames
        for image in ImageSequence.Iterator(gif):
            rgb = image.convert("RGB")
            duration = image.info.get("duration", 0)
            if duration <= 0 or duration % STEP_MS:
                raise RuntimeError("invalid decoded GIF duration")
            if any(rgb.getpixel(p) != BACKGROUND for p in
                   ((0, 0), (799, 0), (0, 319), (799, 319), (400, 0))):
                raise RuntimeError("GIF background differs")
            append_run(actual, rgb_digest(rgb), duration)
    if actual != expected:
        raise RuntimeError("decoded GIF pixels/timing differ from production samples")
    if path.stat().st_size > 5_000_000:
        raise RuntimeError("README GIF exceeds the 5 MB size budget")
    print(f"PASS: {SIZE[0]}x{SIZE[1]}, {count} GIF frames, {LOOP_MS} ms loop, "
          f"{path.stat().st_size} bytes; exact production endpoints/pixels/timing; #212830 background")
    print(f"sha256 {hashlib.sha256(path.read_bytes()).hexdigest()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--install", action="store_true", help="write and verify the GIF")
    mode.add_argument("--check", action="store_true", help="verify the committed GIF without writing it")
    args = parser.parse_args()
    frames, durations, expected = frames_and_timeline()
    if args.install:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        frames[0].save(OUTPUT, save_all=True, append_images=frames[1:], duration=durations,
                       loop=0, optimize=True, disposal=1)
    check_gif(OUTPUT, expected)


if __name__ == "__main__":
    main()
