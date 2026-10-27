#!/usr/bin/env python3
"""Compose one 15s promo from the real host capture and existing generated gate.

No API calls, no test-suite runs, no installed-asset edits. Pillow + FFmpeg only.
The native UI is never redrawn: either a labelled detail crop or an integer 2x
capture. Screen framing/typography are editorial graphics, not a fake device.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import wave

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
W, H, FPS = 1280, 720, 30
INK = (31, 20, 31)
SPOT = (91, 49, 124)
QUIET = (126, 100, 119)
PALE = (238, 211, 230)
DARK = (5, 4, 9)
NEAREST = Image.Resampling.NEAREST


def bitmap_font():
    src = (ROOT / 'core/ui/font.cpp').read_text()
    body = src.split('font_6x8[96][8] = {', 1)[1].split('\n};', 1)[0]
    body = re.sub(r'//[^\n]*', '', body)
    body = re.sub(r'R\(([01]),([01]),([01]),([01]),([01]),([01])\)',
                  lambda m: str(int(''.join(m.groups()), 2)), body)
    rows = [tuple(map(int, re.findall(r'\d+', r)))
            for r in re.findall(r'\{([^{}]+)\}', body)]
    assert len(rows) == 96 and all(len(r) == 8 for r in rows)
    return rows


GLYPHS = bitmap_font()


def small_text(text, scale=2, color=INK):
    mask = Image.new('L', (max(1, len(text) * 6), 8))
    p = mask.load()
    for i, c in enumerate(text):
        glyph = GLYPHS[ord(c) - 32] if 32 <= ord(c) <= 127 else GLYPHS[0]
        for y, row in enumerate(glyph):
            for x in range(6):
                if row & (1 << (5 - x)):
                    p[i * 6 + x, y] = 255
    mask = mask.resize((mask.width * scale, mask.height * scale), NEAREST)
    result = Image.new('RGBA', mask.size, color)
    result.putalpha(mask)
    return result


def wordmark(scale, color):
    mask = Image.open(ROOT / 'assets/branding/gothic_wordmark_pixel_master.png').convert('L')
    mask = mask.resize((mask.width * scale, mask.height * scale), NEAREST)
    result = Image.new('RGBA', mask.size, color)
    result.putalpha(mask)
    return result


def hero(text):
    font = ImageFont.truetype(str(ROOT / 'assets/fonts/Gossip-MedPixel.otf'), 28)
    box = font.getbbox(text)
    mask = Image.new('L', (box[2] - box[0], box[3] - box[1]))
    ImageDraw.Draw(mask).text((-box[0], -box[1]), text, font=font, fill=255)
    # Typography uses an integer magnification of the actual native heading face.
    mask = mask.point(lambda x: 255 if x >= 128 else 0)
    mask = mask.resize((mask.width * 5, mask.height * 5), NEAREST)
    result = Image.new('RGBA', mask.size, INK)
    result.putalpha(mask)
    return result


def paste(im, layer, xy, opacity=1.0):
    if opacity < 1:
        layer = layer.copy()
        layer.putalpha(layer.getchannel('A').point(lambda a: round(a * max(0, opacity))))
    im.paste(layer, xy, layer)


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def background():
    # Same Pink paper endpoints as ui_palette.h; this is the editorial surround.
    im = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = smooth(y / (H - 1))
        c = tuple(round(a * (1 - t) + b * t) for a, b in
                  zip((228, 194, 207), (211, 187, 206)))
        d.line((0, y, W - 1, y), fill=c)
    return im


def decode(path, start=0, length=None):
    cmd = ['ffmpeg', '-v', 'error', '-ss', str(start), '-i', str(path)]
    if length is not None:
        cmd += ['-t', str(length)]
    cmd += ['-an', '-vf', 'fps=30', '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1']
    return subprocess.Popen(cmd, stdout=subprocess.PIPE)


def read_frame(process, size):
    required = size[0] * size[1] * 3
    data = bytearray()
    while len(data) < required:
        block = process.stdout.read(required - len(data))
        if not block:
            raise RuntimeError('capture ended early')
        data.extend(block)
    return Image.frombytes('RGB', size, bytes(data))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--bottom', type=Path, required=True)
    ap.add_argument('--audio', type=Path, required=True)
    ap.add_argument('--gate', type=Path, default=ROOT / 'promo/gate-test-01/seance-gate-test-01.mp4')
    ap.add_argument('--output', type=Path, default=ROOT / 'promo/draft-01/seance-promo-draft-01.mp4')
    ap.add_argument('--sigils-at', type=float, default=9.0,
                    help='actual sigils scene time; >=11 keeps sequencer until gate')
    args = ap.parse_args()
    for path in (args.bottom, args.audio, args.gate):
        if not path.is_file():
            ap.error(f'missing input: {path}')
    with wave.open(str(args.audio), 'rb') as audio:
        if audio.getnchannels() != 2 or audio.getnframes() < 15 * audio.getframerate():
            ap.error('requires at least 15 seconds of stereo engine WAV, not the 11s UI capture audio')
    if args.output.exists():
        ap.error(f'preserving existing output; use another --output: {args.output}')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    bg = background()
    wm = wordmark(3, INK)
    end_wm = wordmark(4, PALE)
    footer = small_text('sampler + synth / nintendo 3ds', 2, QUIET)
    end_footer = small_text('sampler + synth for nintendo 3ds', 2, PALE)
    scenes = [
        (0, 'cuts', '01', 'source / slices', 3),
        (3, 'scrub', '02', 'position / speed', 7),
        (7, 'sequence', '03', 'four chains', min(11, args.sigils_at)),
    ]
    if args.sigils_at < 11:
        scenes.append((args.sigils_at, 'sigils', '04', 'a / b / c / d', 11))
    graphics = {s[1]: (hero(s[1]), small_text(s[2] + ' / ' + s[1], 3, SPOT),
                       small_text(s[3], 2, QUIET)) for s in scenes}
    raw = decode(args.bottom, length=11)
    gate = decode(args.gate, start=1, length=4)
    log_path = args.output.with_suffix('.render.log')
    peak_log = args.output.with_suffix('.audio-check.log')
    # Engine waveform only. The generated gate's audio is deliberately not used.
    audio_filter = ('atrim=duration=15,asetpts=PTS-STARTPTS,'
                    'loudnorm=I=-16:TP=-1.5:LRA=9,aresample=48000,'
                    'afade=t=in:st=0:d=0.025,afade=t=out:st=13.65:d=1.35')
    cmd = ['ffmpeg', '-v', 'warning', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
           '-s', f'{W}x{H}', '-r', str(FPS), '-i', 'pipe:0', '-i', str(args.audio),
           '-map', '0:v:0', '-map', '1:a:0', '-af', audio_filter,
           '-c:v', 'libx264', '-preset', 'medium', '-crf', '17', '-pix_fmt', 'yuv420p',
           '-c:a', 'aac', '-b:a', '192k', '-t', '15', '-movflags', '+faststart',
           '-metadata', 'title=seance / promo draft 01', str(args.output)]
    with log_path.open('wb') as log:
        enc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stderr=log)
        try:
            for n in range(450):
                t = n / FPS
                if n < 330:
                    ui = read_frame(raw, (320, 240))
                    scene = next(s for s in reversed(scenes) if t >= s[0])
                    label, index, desc = graphics[scene[1]]
                    im = bg.copy()
                    paste(im, wm, (64, 48))
                    paste(im, index, (W - 64 - index.width, 74))
                    paste(im, footer, (64, 676))
                    if t < 3:
                        # Exact source waveform / seam controls, not invented graphics.
                        detail = ui.crop((12, 102, 308, 192)).resize((1184, 360), NEAREST)
                        im.paste(detail, (48, 208))
                        paste(im, label, (64, 575))
                        paste(im, desc, (W - 64 - desc.width, 607))
                    else:
                        # A full, unaltered native bottom screen at integer 2x.
                        im.paste(ui.resize((640, 480), NEAREST), (552, 132))
                        d = ImageDraw.Draw(im)
                        d.line((528, 132, 528, 611), fill=(174, 145, 165), width=2)
                        entrance = smooth((t - scene[0]) / .18)
                        paste(im, label, (64, 284 + round(10 * (1 - entrance))), entrance)
                        paste(im, desc, (68, 407), entrance)
                        for i, s in enumerate(scenes):
                            d.rectangle((68 + i * 70, 572, 117 + i * 70, 577),
                                        fill=SPOT if s[1] == scene[1] else (174, 145, 165))
                else:
                    generated = read_frame(gate, (W, H))
                    im = Image.new('RGB', (W, H), DARK)
                    im.paste(generated, (0, -48))
                    a = smooth((t - 11.25) / .45)
                    paste(im, end_wm, ((W - end_wm.width) // 2, 553), a)
                    paste(im, end_footer, ((W - end_footer.width) // 2, 678), a)
                if t > 14.65:
                    im = Image.blend(im, Image.new('RGB', (W, H), DARK), smooth((t - 14.65) / .35))
                enc.stdin.write(im.tobytes())
            enc.stdin.close()
            if enc.wait() != 0:
                raise RuntimeError(f'ffmpeg failed; see {log_path}')
        finally:
            for proc in (raw, gate):
                if proc.stdout:
                    proc.stdout.close()
                if proc.poll() is None:
                    proc.terminate()
                proc.wait()
            if enc.poll() is None:
                enc.terminate()
                enc.wait()
    probe = subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries',
        'format=duration,size:stream=codec_name,codec_type,width,height,r_frame_rate,sample_rate,channels',
        '-of', 'json', str(args.output)], text=True)
    metadata = {'output': str(args.output.relative_to(ROOT)) if args.output.is_relative_to(ROOT) else str(args.output),
                'sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
                'capture': str(args.bottom), 'audio': str(args.audio), 'gate': str(args.gate),
                'new_generation_cost_usd': 0, 'timing': scenes, 'gate_timing': [11, 15],
                'ui_boundary': 'real current UI/Engine, offline host raster; not console capture',
                'audio_boundary': 'real Engine only; gentle loudness/edge fades; generated gate audio excluded',
                'probe': json.loads(probe)}
    args.output.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')
    with peak_log.open('wb') as log:
        subprocess.run(['ffmpeg', '-hide_banner', '-i', str(args.output), '-vn',
                        '-af', 'volumedetect', '-f', 'null', '-'], stderr=log, check=True)
    print(args.output)
    print(probe)


if __name__ == '__main__':
    main()
