#!/usr/bin/env python3
"""Static browser proposal, not runtime or a hardware capture.

Three native 400x240 / 320x240 pairs, nearest 2x. Real local CC0 PCM;
favorites, project membership, owner and playback position are authored examples.
"""
from pathlib import Path
from functools import lru_cache
import array
import math
import sys
import wave
from PIL import Image
from render_universal_sigils_study import Surface, caption, OUTSIDE
from render_menu_style_study import overlaps

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ui_mockups/sample_browser_study'
SAMPLES = ROOT / 'sample_library/starter_cc0/wav'
INSTALLED = 'kick_deep.wav'
FAVORITES = {'kick_punch.wav', 'alien_hit.wav', 'snare_synth.wav',
             'fm_perc.wav', 'anvil.wav', 'cowbell.wav'}
STATES = (
    dict(source='library', location='starter cc0', selected='kick_punch.wav',
         rows=('kick_deep.wav', 'kick_punch.wav', 'hat_closed.wav',
               'hat_bright.wav', 'snare_synth.wav', 'snare_x0x.wav'),
         total=16, playing=True, position=.42,
         detail='library / starter cc0', title='01 / library / audition before use',
         notes=('six full-width rows / remembered folder',
                'tap to audition / separate use action')),
    dict(source='favorites', location='6 sounds', selected='alien_hit.wav',
         rows=('kick_punch.wav', 'alien_hit.wav', 'snare_synth.wav',
               'fm_perc.wav', 'anvil.wav', 'cowbell.wav'),
         total=6, playing=False, position=0,
         detail='library / starter cc0', title='02 / favorites / same controls',
         notes=('saved sounds / star at the row edge',
                'stopped preview keeps its waveform')),
    dict(source='project', location='4 sounds', selected='snare_synth.wav',
         rows=('kick_deep.wav', 'snare_synth.wav', 'hat_closed.wav', 'fm_perc.wav'),
         total=4, playing=False, position=0,
         detail='used by chain 3 / play', title='03 / project / loaded sounds only',
         notes=('no empty slots / no memory-bank pages',
                'selection is not the installed sample')),
)


@lru_cache(None)
def audio(name):
    with wave.open(str(SAMPLES / name), 'rb') as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 1
        rate, frames = w.getframerate(), w.getnframes()
        pcm = array.array('h', w.readframes(frames))
        if sys.byteorder != 'little':
            pcm.byteswap()
    assert len(pcm) == frames and frames >= 352
    return pcm, frames / rate, rate


def surface(width):
    return Surface(width, 'graphite')


def star(c, x, y, filled):
    points = []
    for i in range(10):
        angle = -math.pi / 2 + i * math.pi / 5
        radius = 5 if i % 2 == 0 else 2
        points.append((round(x + radius * math.cos(angle)),
                       round(y + radius * math.sin(angle))))
    if filled:
        c.d.polygon(points, fill=c.p['spot'])
    else:
        c.d.line(points + points[:1], fill=c.p['rail'])


def upper(state):
    c = surface(400)
    name = state['selected']
    pcm, duration, rate = audio(name)
    playing, pos = state['playing'], state['position']
    c.txt(24, 18, 'preview', 'ghost')
    status = f'listening / {duration * pos:.2f}s' if playing else 'stopped'
    c.txt(376, 18, status, 'spot' if playing else 'ghost', right=True)
    c.txt(24, 43, name, 'ink', scale=2)
    c.txt(24, 70, f'{duration:.2f}s / mono / {rate // 1000}khz', 'ghost')
    # Exhaustive extrema of actual PCM, fixed full scale, no auto gain.
    # A real engine playhead would need a new published cursor; this is scripted.
    x0, width, mid, amplitude = 24, 352, 130, 36
    cursor = x0 + round(pos * (width - 1))
    c.line((x0, mid, x0 + width - 1, mid), 'rail')
    for x in range(width):
        start, end = x * len(pcm) // width, (x + 1) * len(pcm) // width
        segment = pcm[start:end]
        lo, hi = min(segment), max(segment)
        y0 = mid - round(hi * amplitude / 32768)
        y1 = mid - round(lo * amplitude / 32768)
        assert 94 <= y0 <= y1 <= 166
        if lo != 0 or hi != 0:
            c.line((x0 + x, y0, x0 + x, y1),
                   'spot' if playing and x0 + x <= cursor else 'ink')
    if playing:
        c.line((cursor, 90, cursor, 170), 'spot')
        c.d.polygon(((cursor - 3, 87), (cursor + 3, 87), (cursor, 90)), fill=c.p['spot'])
    c.txt(24, 178, '0s', 'ghost')
    c.txt(376, 178, f'{duration:.2f}s', 'ghost', right=True)
    c.txt(24, 196, state['detail'], 'ghost')
    c.line((24, 213, 375, 213), 'rail')
    c.txt(24, 224, 'installed', 'ghost')
    c.txt(96, 224, INSTALLED, 'ink')
    return c.im


def lower(state):
    c = surface(320)
    c.heading(16, 8, 'samples')
    c.txt(142, 19, 'ch 2 / play', 'ghost')
    c.txt(304, 19, 'close', 'ghost', right=True)
    source = state['source']
    c.txt(16, 41, source, 'spot')
    c.chevron(16 + len(source) * 6 + 9, 44, 'spot')
    c.txt(304, 41, state['location'], 'ghost', right=True)
    hits = [('close', (262, 0, 320, 32)),
            ('sources', (8, 32, 100, 56)), ('parent', (106, 32, 310, 56))]
    assert state['selected'] in state['rows']
    for i, name in enumerate(state['rows']):
        top, y = 56 + i * 24, 64 + i * 24
        selected = name == state['selected']
        role = 'spot' if selected else 'ink'
        if selected:
            c.d.rectangle((15, top + 4, 16, top + 19), fill=c.p['spot'])
        if selected and state['playing']:
            c.d.polygon(((26, y), (26, y + 7), (31, y + 3)), fill=c.p['spot'])
        c.txt(40, y, name, role)
        c.txt(276, y, f'{audio(name)[1]:.2f}s', 'ghost', right=True)
        star(c, 297, y + 3, name in FAVORITES)
        hits += [(f'row {i}', (8, top, 282, top + 24)),
                 (f'star {i}', (282, top, 309, top + 24))]
    if state['total'] > 6:
        c.line((315, 59, 315, 196), 'rail')
        c.d.rectangle((314, 59, 315, 59 + round(137 * 6 / state['total'])), fill=c.p['ghost'])
        hits.append(('scroll', (310, 56, 320, 200)))
    c.line((16, 201, 303, 201), 'rail')
    c.txt(16, 210, 'y stop' if state['playing'] else 'y listen', 'ink')
    c.txt(304, 210, 'a use >', 'spot', right=True)
    c.line((262, 221, 303, 221), 'spot')
    c.txt(16, 231, 'auto preview on', 'ghost')
    c.txt(304, 231, 'b back', 'ghost', right=True)
    hits += [('listen', (8, 202, 116, 226)), ('use', (236, 202, 312, 226)),
             ('auto', (8, 226, 140, 240)), ('back', (242, 226, 312, 240))]
    for i, (label, box) in enumerate(hits):
        assert 0 <= box[0] < box[2] <= 320 and 0 <= box[1] < box[3] <= 240, label
        assert all(not overlaps(box, prev) for _, prev in hits[:i]), label
    return c.im


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = Image.new('RGB', (1280, 618), OUTSIDE)
    caption(sheet, 16, 14, 'seance / sample browser / listen first, use when ready')
    for i, state in enumerate(STATES):
        x = 16 + i * 424
        caption(sheet, x, 43, state['title'])
        sheet.paste(upper(state), (x, 64))
        sheet.paste(lower(state), (x + 40, 320))
        for y, note in zip((575, 590), state['notes']):
            caption(sheet, x, y, note)
    caption(sheet, 16, 607, 'static concept / native fonts + real local pcm / scripted states / not implemented')
    path = OUT / 'sample-browser-v1-2x.png'
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(path)
    print(path.relative_to(ROOT))
    print('three dual-screen states; text bounds/nonoverlap, proposed targets and pcm bounds passed')


if __name__ == '__main__':
    main()
