#!/usr/bin/env python3
"""Prepare embedded menu PCM from the preserved user WAVs (requires ffmpeg only)."""
import array
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/menu_audio'
RATE = 32000
OVERLAP = 4 * RATE


def decode(path):
    raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(path),
        '-ar', str(RATE), '-ac', '2', '-f', 'f32le', '-'])
    values = array.array('f', raw)
    if sys.byteorder != 'little':
        values.byteswap()
    if not values or len(values) % 2 or not all(map(math.isfinite, values)):
        raise ValueError('invalid source PCM')
    return values


def pack(name, values):
    # Remove constant DC; never normalize a quiet recording upwards. Reserve
    # 1 dB even if the equal-power seam sums correlated material above full scale.
    dc = [sum(values[c::2]) / (len(values) // 2) for c in range(2)]
    peak = max(abs(v - dc[i % 2]) for i, v in enumerate(values))
    scale = min(1.0, 10 ** (-1 / 20) / max(peak, 1e-12))
    pcm = array.array('h', (max(-32768, min(32767,
        round((v - dc[i % 2]) * scale * 32767))) for i, v in enumerate(values)))
    if name == 'menu_entry':
        # Endpoint tapers are applied AFTER DC correction; first/last stay zero.
        frames = len(pcm) // 2
        attack, release = RATE * 12 // 1000, RATE * 180 // 1000
        for i in range(frames):
            gain = min(1.0, i / attack, (frames - 1 - i) / release)
            if gain < 1:
                gain = gain * gain * (3 - 2 * gain)
                for c in range(2):
                    pcm[2*i+c] = round(pcm[2*i+c] * gain)
    seam_delta = [int(pcm[c]) - int(pcm[-2+c]) for c in range(2)]
    rms = math.sqrt(sum(int(v) * int(v) for v in pcm) / len(pcm)) / 32768
    peak_pcm = max(abs(int(v)) for v in pcm)
    if sys.byteorder != 'little':
        pcm.byteswap()
    data = pcm.tobytes()
    path = ASSETS / (name + '.bin')
    path.write_bytes(data)
    return dict(file=path.name, frames=len(pcm)//2, bytes=len(data),
        sha256=hashlib.sha256(data).hexdigest(), peak=peak_pcm,
        rms_dbfs=round(20 * math.log10(max(rms, 1e-12)), 3),
        seam_delta=seam_delta, dc_removed=dc, scale=scale)


def main():
    drone_path = ASSETS / 'source/1.wav'
    entry_path = ASSETS / 'source/2.wav'
    drone = decode(drone_path)
    frames = len(drone) // 2
    if frames <= 2 * OVERLAP:
        raise ValueError('drone too short for the four-second overlap')
    # Begin at source 4s, play to 26s, then overlap 26..30s with 0..4s.
    # The next iteration continues naturally at source 4s. Neither splice is
    # forced to silence, and the output does not repeat a one-shot fade-in.
    loop = drone[OVERLAP*2:(frames-OVERLAP)*2]
    for i in range(OVERLAP):
        phase = i / (OVERLAP - 1) * math.pi / 2
        a, b = math.cos(phase), math.sin(phase)
        for c in range(2):
            loop.append(drone[2*(frames-OVERLAP+i)+c] * a + drone[2*i+c] * b)
    outputs = [pack('menu_drone', loop), pack('menu_entry', decode(entry_path))]
    manifest = dict(sample_rate=RATE, channels=2, encoding='s16le', overlap_frames=OVERLAP,
        source_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in (drone_path, entry_path)}, outputs=outputs)
    (ASSETS / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
