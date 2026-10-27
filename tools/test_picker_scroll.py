#!/usr/bin/env python3
"""Focused real picker-scroll math: pixel tracking, timing, damping, bounds."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CODE = r'''
#include "platform/3ds/picker_scroll.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using seance::platform::ui3ds::picker_scroll::State;
int main() {
    int cases = 0;
    for (float limit : {0.f, 81.f, 594.f}) for (int sign : {-1, 1}) {
        State s; s.set(limit * .5f, limit, 1000);
        const float start = s.position;
        s.drag(start + sign * 7, limit, 1016);
        assert(s.position == std::clamp(start + sign * 7, 0.f, limit));
        s.drag(start + sign * 19, limit, 1032);
        s.release(1032);
        float previous = s.position;
        for (int ms = 1040; ms <= 1600; ms += 8) {
            s.sample(limit, ms);
            assert(s.position >= 0 && s.position <= limit);
            assert(sign > 0 ? s.position >= previous : s.position <= previous);
            previous = s.position;
        }
        assert(!s.moving()); ++cases;
    }
    // Integrate the same coast at different frame rates; no frame-count physics.
    for (int interval : {8, 16, 33, 50}) {
        State s; s.set(100, 594, 0); s.drag(130, 594, 30); s.release(30);
        const float initial = s.velocity;
        const float expected = 130 + initial * initial / (.0045f * 2);
        for (int now = 30 + interval; now < 600; now += interval) s.sample(594, now);
        assert(std::fabs(s.position - expected) < .1f); assert(!s.moving()); ++cases;
    }
    for (int mode = 0; mode < 5; ++mode) {
        State s; s.set(100, 594, 1000); s.drag(130, 594, 1030);
        if (mode == 0) s.stop(1030); // cancellation
        if (mode == 1) s.release(1130); // paused stylus before lift
        if (mode == 2) { s.release(1030); s.sample(594, 1400); } // suspended UI
        if (mode == 3) { s.release(1030); s.sample(594, 999); } // clock reversal
        if (mode == 4) s.set(130, 594, 1030); // scrollbar/filter takeover
        s.sample(594, 1500); assert(s.position == 130 && !s.moving()); ++cases;
    }
    State edge; edge.set(590, 594, 0); edge.drag(900, 594, 16); edge.release(16);
    edge.sample(594, 32); assert(edge.position == 594 && !edge.moving());
    edge.set(-100, 594, 100); assert(edge.position == 0);
    edge.set(5000, 594, 100); assert(edge.position == 594);
    std::printf("picker scroll: %d timing/tracking cases + hard bounds; pass\n", cases);
}
'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    inputs = [Path(__file__).resolve(), ROOT / 'platform/3ds/picker_scroll.h']
    hashes = {str(p.relative_to(ROOT)): sha(p) for p in inputs}
    with tempfile.TemporaryDirectory(prefix='seance-picker-scroll-') as tmp:
        source = Path(tmp) / 'test.cpp'
        source.write_text(CODE)
        binary = Path(tmp) / 'test'
        command = ['g++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                   '-I' + str(ROOT), str(source), '-o', str(binary)]
        subprocess.run(command, cwd=ROOT, check=True)
        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True,
                                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
        print(result.stdout, end='')
        assert all(sha(ROOT / p) == h for p, h in hashes.items())
        out = ROOT / 'release/picker-scroll/math-proof.json'
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(dict(inputs=hashes, summary=result.stdout.strip(),
                                       sanitizer='ASAN/UBSAN/leaks', hardware=False), indent=2) + '\n')


if __name__ == '__main__':
    main()
