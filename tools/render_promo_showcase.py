#!/usr/bin/env python3
"""One 36s/200bpm edit from current native UI/Engine captures and native splash.

No API calls, generated footage, fake UI or device frames. Editorial surround,
labels and beat cuts are added; native screens use nearest integer enlargement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import wave

from PIL import Image, ImageDraw
from render_promo_draft import (ROOT, W, H, FPS, INK, SPOT, QUIET, NEAREST,
                                background, small_text, wordmark, hero, paste,
                                smooth, decode, read_frame)

PAPER = (226, 190, 206)
BRANCH = (174, 145, 165)
MODULES = [
    ('osc', 'oscillator'), ('noise', 'noise source'), ('fm', 'fm synthesis'),
    ('motor', 'motor synthesis'), ('play', 'sample player'), ('gran', 'granular sampler'),
    ('mic', 'microphone input'), ('rec', 'recorder'),
    ('filter', 'filter'), ('vca', 'amplitude + envelope'), ('delay', 'delay'),
    ('fold', 'wavefolder'), ('bit', 'bitcrusher'), ('chorus', 'chorus'),
    ('strn', 'stranular'), ('rev', 'reverb'),
    ('ghost', 'ghost'), ('ring', 'ring modulation'), ('tilt', 'tilt eq'),
    ('strain', 'strain'), ('shift', 'frequency shifter'), ('bird', 'bird'),
]
# Video frame ranges: one beat = 9 frames; all catalogue cards occupy one beat.
SCENES = [
    (0, 36, 'intro', 'seance', '22 modules / 4 chains / 200 bpm'),
    (36, 144, 'sequence', 'sequence', 'four chains / parameter locks'),
    (144, 216, 'catalogue', '', 'voices + sampling'),
    (216, 360, 'perform', 'perform', 'faders / mute / scene morph'),
    (360, 432, 'catalogue', '', 'shape + space'),
    (432, 540, 'sigils', 'sigils', 'draw the modulation'),
    (540, 594, 'catalogue', '', 'motion + texture'),
    (594, 720, 'play', 'scratch', 'cuts / rate / source scratch'),
    (720, 828, 'banks', 'sequence', 'banks / locks / variation'),
    (828, 936, 'finale', 'perform', 'four chains / a + b scenes'),
    (936, 1080, 'splash', 'seance', 'sampler + synth for nintendo 3ds'),
]


def catalogue_index(n):
    if 144 <= n < 216:
        return (n - 144) // 9
    if 360 <= n < 432:
        return 8 + (n - 360) // 9
    if 540 <= n < 594:
        return 16 + (n - 540) // 9
    return None


def splash_frames(path):
    data = subprocess.check_output([
        'ffmpeg', '-v', 'error', '-i', str(path), '-an', '-f', 'rawvideo',
        '-pix_fmt', 'rgb24', 'pipe:1'])
    frame_bytes = 400 * 240 * 3
    if len(data) != 80 * frame_bytes:
        raise ValueError('requires the exact 80-frame / 20fps native splash')
    return [Image.frombytes('RGB', (400, 240), data[i:i + frame_bytes])
            for i in range(0, len(data), frame_bytes)]


def small_hero(text, scale):
    im = hero(text)  # shared helper uses native 28ppem binary glyphs at integer 5x
    return im.resize((im.width // 5 * scale, im.height // 5 * scale), NEAREST)


def compose(n, bottom, top, splash, assets):
    start, end, kind, title, desc = next(s for s in SCENES if s[0] <= n < s[1])
    t = n / FPS
    if kind in ('intro', 'splash'):
        # Opening is an excerpt at original speed; closing is the full native
        # four-second pass at original speed, then a .8s endpoint hold.
        sf = 24 + n * 20 // FPS if kind == 'intro' else min(79, (n - 936) * 20 // FPS)
        im = Image.new('RGB', (W, H), PAPER)
        im.paste(splash[sf].resize((1200, 720), NEAREST), (40, 0))
        label = assets['intro_label'] if kind == 'intro' else assets['end_label']
        paste(im, label, ((W - label.width) // 2, 572))
        if kind == 'splash':
            paste(im, assets['intro_label'], ((W - assets['intro_label'].width) // 2, 619))
            paste(im, assets['end_note'], ((W - assets['end_note'].width) // 2, 678))
        return im

    ci = catalogue_index(n)
    if ci is not None:
        title, desc = MODULES[ci]
    # Native bottom 3x during the first mixer phrase and closing performance.
    # Every native pixel remains an integer square; no interpolated UI zooms.
    wide = kind == 'finale' or (kind == 'perform' and n < 288)
    if wide:
        im = Image.new('RGB', (W, H), PAPER)
        im.paste(bottom.resize((960, 720), NEAREST), (320, 0))
        paste(im, assets['wm'], (40, 40))
        paste(im, assets['compact'][title], (40, 203))
        for j, line in enumerate(('four chains', 'a / b scenes', '200 bpm')):
            paste(im, assets['line'][line], (42, 335 + j * 34))
        paste(im, assets['line']['engine audio'], (42, 636))
        ImageDraw.Draw(im).line((305, 32, 305, 688), fill=BRANCH, width=2)
        # Small editorial beat count, never laid over a native control.
        d = ImageDraw.Draw(im)
        for beat in range(4):
            x = 42 + beat * 52
            d.rectangle((x, 568, x + 30, 573), fill=SPOT if (n // 9) % 4 == beat else BRANCH)
        return im

    im = assets['bg'].copy()
    paste(im, assets['wm'], (48, 36))
    if ci is not None:
        tag = assets['module_tags'][ci]
    else:
        tag = assets['tags'][kind]
    paste(im, tag, (W - 56 - tag.width, 56))
    # Two-beat waveform close-up, then full UI before the real rate drag starts.
    if kind == 'play' and n < 612:
        detail = bottom.crop((12, 102, 308, 192)).resize((1184, 360), NEAREST)
        im.paste(detail, (48, 156))
        paste(im, assets['titles']['cuts'], (48, 533))
        label = assets['line']['cuts / rate / source scratch']
        paste(im, label, (W - 56 - label.width, 583))
    else:
        im.paste(bottom.resize((640, 480), NEAREST), (584, 132))
        paste(im, assets['titles'][title], (48, 164))
        paste(im, assets['line'][desc], (50, 316))
        # Entire real top screen, not an invented waveform/chain illustration.
        im.paste(top, (48, 376))
        paste(im, assets['line']['top screen'], (48, 350))
        ImageDraw.Draw(im).line((552, 132, 552, 611), fill=BRANCH, width=2)
    d = ImageDraw.Draw(im)
    d.line((48, 648, 1232, 648), fill=BRANCH, width=2)
    d.line((48, 648, 48 + round(1184 * t / 36), 648), fill=SPOT, width=3)
    paste(im, assets['footer'], (48, 671))
    paste(im, assets['capture_note'], (W - 48 - assets['capture_note'].width, 671))
    return im


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--capture', type=Path, default=ROOT / 'promo/expanded-01/capture')
    ap.add_argument('--splash', type=Path, default=ROOT / 'promo/expanded-01/splash-native.mkv')
    ap.add_argument('--output', type=Path, default=ROOT / 'promo/expanded-01/seance-promo-expanded-01.mp4')
    ap.add_argument('--master-gain-db', type=float, default=None,
                    help='fixed gain instead of loudnorm; preserves performed level changes (-60..12 dB)')
    args = ap.parse_args()
    if args.master_gain_db is not None and not -60 <= args.master_gain_db <= 12:
        ap.error('--master-gain-db must be finite and between -60 and 12')
    files = [args.capture / 'bottom.mkv', args.capture / 'top.mkv',
             args.capture / 'engine.wav', args.splash]
    if not all(p.is_file() for p in files):
        ap.error('missing capture or native splash input')
    if any(args.output.with_suffix(s).exists() for s in ('.mp4', '.json', '.render.log')):
        ap.error('preserving existing output; choose a new --output basename')
    with wave.open(str(files[2]), 'rb') as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) != (2, 2, 32000, 1152000):
            ap.error('requires exact36s/32kHz/PCM16/stereo Engine soundtrack')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    descs = {s[4] for s in SCENES} | {m[1] for m in MODULES}
    descs |= {'top screen', 'four chains', 'a / b scenes', '200 bpm', 'engine audio'}
    titles = {s[3] for s in SCENES if s[3]} | {m[0] for m in MODULES} | {'cuts'}
    assets = {
        'bg': background(), 'wm': wordmark(2, INK),
        'titles': {s: hero(s) for s in titles},
        'compact': {s: small_hero(s, 3) for s in titles},
        'line': {s: small_text(s, 2, QUIET) for s in descs},
        'tags': {s[2]: small_text('200 bpm / ' + s[2], 3, SPOT) for s in SCENES},
        'module_tags': [small_text(f'{i + 1:02} / 22  module catalogue', 3, SPOT) for i in range(22)],
        'footer': small_text('sampler + synth / nintendo 3ds', 2, QUIET),
        'capture_note': small_text('host capture / engine audio', 2, QUIET),
        'intro_label': small_text('22 modules / 4 chains / 200 bpm', 3, SPOT),
        'end_label': small_text('sampler + synth for nintendo 3ds', 3, INK),
        'end_note': small_text('native interface + engine sound / host capture', 2, QUIET),
    }
    splash = splash_frames(args.splash)
    raw = decode(files[0], length=31.2)
    top = decode(files[1], length=31.2)
    # This is Engine audio only, never a hardware recording or external track.
    # A fixed master retains the actual fader/mute dynamics. Legacy renders keep
    # their original default loudnorm policy unless --master-gain-db is supplied.
    master = ('loudnorm=I=-14:TP=-2:LRA=8' if args.master_gain_db is None
              else f'volume={args.master_gain_db:.9f}dB')
    af = (f'atrim=duration=36,asetpts=PTS-STARTPTS,{master},aresample=48000,'
          'afade=t=in:st=0:d=0.008,afade=t=out:st=34.8:d=1.2')
    cmd = ['ffmpeg', '-nostdin', '-n', '-v', 'warning', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
           '-s', f'{W}x{H}', '-r', str(FPS), '-i', 'pipe:0', '-i', str(files[2]),
           '-map', '0:v:0', '-map', '1:a:0', '-af', af,
           '-c:v', 'libx264', '-preset', 'medium', '-crf', '16', '-pix_fmt', 'yuv420p',
           '-c:a', 'aac', '-b:a', '256k', '-t', '36', '-movflags', '+faststart',
           '-metadata', 'title=seance / 22 modules / 200 bpm', str(args.output)]
    with args.output.with_suffix('.render.log').open('xb') as log:
        enc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=log)
        try:
            for n in range(1080):
                b = read_frame(raw, (320, 240)) if n < 936 else None
                u = read_frame(top, (400, 240)) if n < 936 else None
                enc.stdin.write(compose(n, b, u, splash, assets).tobytes())
            enc.stdin.close()
            if enc.wait():
                raise RuntimeError('ffmpeg encode failed; see render log')
        finally:
            for proc in (raw, top):
                proc.stdout.close()
                if proc.poll() is None:
                    proc.terminate()
                proc.wait()
            if enc.poll() is None:
                enc.terminate()
                enc.wait()
    probe = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-count_frames', '-show_entries',
        'format=duration,size:stream=codec_name,codec_type,width,height,r_frame_rate,nb_read_frames,sample_rate,channels',
        '-of', 'json', str(args.output)], text=True))
    video = next(s for s in probe['streams'] if s['codec_type'] == 'video')
    assert video['nb_read_frames'] == '1080' and float(probe['format']['duration']) == 36
    metadata = {
        'output': str(args.output), 'sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
        'bpm': 200, 'duration': 36, 'fps': FPS, 'probe': probe,
        'inputs': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        'scenes_frames': SCENES, 'catalogue_modules': MODULES,
        'native_splash': 'actual splash_raster.h Pink texture; 20fps unchanged. intro excerpt1.2..2.4s; end full4s + .8s hold',
        'boundary': 'real host UI/Engine, not console footage; catalogue is separate prepared engine, not solo audio demos. mic no host input; rec idle',
        'audio': 'continuous actual performance Engine, CC0 samples + synth, 200bpm; mastering + edge fades only',
        'master_gain_db': args.master_gain_db,
        'audio_filter': af,
        'generation_cost_usd': 0,
    }
    args.output.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(args.output)
    print(json.dumps(probe))


if __name__ == '__main__':
    main()
