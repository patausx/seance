#!/usr/bin/env python3
"""Offline gallery only: isolated Chromium, exact native pixels, no app builds."""
from pathlib import Path
import base64
import hashlib
import http.server
import importlib.util
import io
import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import traceback
import urllib.request
from urllib.parse import urlparse
from datetime import datetime, timezone
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
STUDY = ROOT / 'ui_mockups/playhead_study'
TIMES = [0, 40, 1980, 3200, 3240, 4800, 6380]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class CDP:
    """Minimal localhost RFC6455 client; avoids optional websocket/node packages."""
    def __init__(self, url):
        u = urlparse(url)
        self.sock = socket.create_connection((u.hostname, u.port), timeout=25)
        self.buffer = b''
        self.serial = 0
        self.events = []
        key = base64.b64encode(os.urandom(16)).decode()
        request = (f'GET {u.path} HTTP/1.1\r\nHost: {u.hostname}:{u.port}\r\n'
                   f'Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n'
                   'Sec-WebSocket-Version: 13\r\n\r\n')
        self.sock.sendall(request.encode())
        while b'\r\n\r\n' not in self.buffer:
            self.buffer += self.sock.recv(4096)
        header, self.buffer = self.buffer.split(b'\r\n\r\n', 1)
        assert header.split(b'\r\n', 1)[0].split(b' ', 2)[1] == b'101', header
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
        assert accept in header

    def read(self, n):
        while len(self.buffer) < n:
            chunk = self.sock.recv(max(4096, n - len(self.buffer)))
            if not chunk:
                raise EOFError('chromium websocket closed')
            self.buffer += chunk
        result, self.buffer = self.buffer[:n], self.buffer[n:]
        return result

    def send(self, payload, opcode=1):
        mask = os.urandom(4)
        n = len(payload)
        header = bytes([0x80 | opcode])
        header += (bytes([0x80 | n]) if n < 126 else
                   bytes([0x80 | 126]) + struct.pack('!H', n) if n < 65536 else
                   bytes([0x80 | 127]) + struct.pack('!Q', n))
        self.sock.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    def message(self):
        chunks = []
        while True:
            a, b = self.read(2)
            n = b & 127
            if n == 126:
                n = struct.unpack('!H', self.read(2))[0]
            elif n == 127:
                n = struct.unpack('!Q', self.read(8))[0]
            mask = self.read(4) if b & 128 else None
            payload = self.read(n)
            if mask:
                payload = bytes(v ^ mask[i % 4] for i, v in enumerate(payload))
            opcode = a & 15
            if opcode == 8:
                raise EOFError('chromium closed websocket')
            if opcode == 9:
                self.send(payload, 10)
                continue
            if opcode not in (0, 1):
                continue
            chunks.append(payload)
            if a & 128:
                return json.loads(b''.join(chunks))

    def call(self, method, params=None):
        self.serial += 1
        number = self.serial
        self.send(json.dumps(dict(id=number, method=method, params=params or {})).encode())
        while True:
            reply = self.message()
            if reply.get('id') == number:
                if 'error' in reply:
                    raise RuntimeError(reply['error'])
                return reply.get('result', {})
            self.events.append(reply)

    def js(self, expression):
        result = self.call('Runtime.evaluate', dict(expression=expression, awaitPromise=True, returnByValue=True))
        if result.get('exceptionDetails'):
            raise RuntimeError(result['exceptionDetails'])
        return result['result'].get('value')


def main():
    out = STUDY / 'checks' / datetime.now(timezone.utc).strftime('browser-%Y%m%dT%H%M%S.%fZ')
    out.mkdir(parents=True)
    paths = [ROOT / 'tools/render_playhead_study.py', Path(__file__), STUDY / 'index.html', STUDY / 'study_data.js']
    paths += sorted(STUDY.glob('*_base.png'))
    identity = {str(p.relative_to(ROOT)): sha(p) for p in paths}
    receipt = dict(status='incomplete', sources=identity, pixel_cases=[], controls=[], layouts=[], limitations=[
        'offline browser evidence, not runtime integration, audio or hardware performance',
        'Python oracle regenerates rectangle commands; no screenshot similarity threshold'])
    proc = None
    cdp = None
    server = None
    profile = None
    logs = []
    requests = []

    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(STUDY), **kwargs)

        def do_GET(self):
            requests.append(self.path)
            if self.path == '/favicon.ico':
                self.send_response(204)
                self.end_headers()
            else:
                super().do_GET()

        def log_message(self, fmt, *args):
            logs.append(fmt % args)

    try:
        sys.dont_write_bytecode = True
        spec = importlib.util.spec_from_file_location('playhead_generator', ROOT / 'tools/render_playhead_study.py')
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        protected = generator.protected()
        schedule = generator.timeline()
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        port = server.server_address[1]
        profile = Path(tempfile.mkdtemp(prefix='chromium-profile-', dir=out))
        command = [shutil.which('chromium') or '/usr/bin/chromium', '--headless', '--no-sandbox',
                   '--disable-dev-shm-usage', '--disable-background-networking', '--disable-component-update',
                   '--disable-default-apps', '--disable-sync', '--no-first-run', '--no-default-browser-check',
                   '--disable-extensions', '--metrics-recording-only', '--mute-audio', '--force-color-profile=srgb',
                   '--remote-debugging-port=0', '--remote-debugging-address=127.0.0.1',
                   '--user-data-dir=' + str(profile), 'about:blank']
        receipt['command'] = command
        with (out / 'chromium.log').open('w') as log:
            proc = subprocess.Popen(command, stdout=log, stderr=log, start_new_session=True)
        deadline = time.monotonic() + 25
        while not (profile / 'DevToolsActivePort').exists():
            if proc.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError('chromium failed to open debugger')
            time.sleep(.05)
        debug_port = int((profile / 'DevToolsActivePort').read_text().splitlines()[0])
        with urllib.request.urlopen(f'http://127.0.0.1:{debug_port}/json/list') as response:
            targets = json.load(response)
        cdp = CDP(next(t['webSocketDebuggerUrl'] for t in targets if t['type'] == 'page'))
        cdp.call('Runtime.enable')
        cdp.call('Page.enable')
        cdp.call('Network.enable')
        receipt['browser'] = cdp.call('Browser.getVersion')

        def viewport(width, height=1000):
            cdp.call('Emulation.setDeviceMetricsOverride', dict(width=width, height=height, deviceScaleFactor=1, mobile=False))

        def load(reduced=False):
            cdp.call('Emulation.setEmulatedMedia', dict(features=[dict(name='prefers-reduced-motion', value='reduce' if reduced else 'no-preference')]))
            cdp.call('Page.navigate', dict(url=f'http://127.0.0.1:{port}/index.html'))
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if cdp.js('Boolean(window.playheadStudyReady)'):
                    return cdp.js('window.playheadStudyReady.then(() => window.playheadStudy.state)')
                time.sleep(.05)
            raise TimeoutError('gallery readiness')

        def check(name, condition, details=None):
            receipt['controls'].append(dict(name=name, pass_=bool(condition), details=details))
            assert condition, (name, details)

        viewport(1280)
        initial = load()
        check('normal motion starts playing', initial['playing'], initial)
        actual = cdp.js("""(async () => {
          const a = window.playheadStudy; a.seek(0); document.getElementById('play').click();
          await new Promise(r=>setTimeout(r,140)); a.render(); const moving=a.state;
          document.getElementById('play').click(); const paused=a.state;
          await new Promise(r=>setTimeout(r,100)); a.render();
          return {moving,paused,after:a.state};
        })()""")
        check('actual animation frame play and pause', actual['moving']['frame'] > 0 and not actual['paused']['playing'] and actual['paused'] == actual['after'], actual)
        for speed in (1, .5, .25):
            result = cdp.js("""(() => {
              let now=0; Object.defineProperty(performance,'now',{configurable:true,value:()=>now});
              const a=window.playheadStudy; a.seek(0);
              const speed=document.getElementById('speed'); speed.value='%s'; speed.dispatchEvent(new Event('change'));
              document.getElementById('restart').click(); const start=a.state;
              now=400; a.render(); const end=a.state;
              a.seek(0); delete performance.now;
              return {start,end};
            })()""" % str(speed))
            check(f'exact speed {speed} and restart', result['start']['frame'] == 0 and result['start']['playing'] and result['end']['frame'] == int(400 * speed / 20) and result['end']['speed'] == speed, result)
        check('seek floors/clamps/pauses', cdp.js("(() => { const a=window.playheadStudy; return [-20,49,9000].map(t=>{a.seek(t);return [a.state.frame,a.state.playing]}); })()") == [[0, False], [2, False], [319, False]])
        cdp.js("window.playheadStudy.seek(1980); document.getElementById('timeline').focus()")
        cdp.call('Input.dispatchKeyEvent', dict(type='keyDown', key='ArrowRight', code='ArrowRight', windowsVirtualKeyCode=39, nativeVirtualKeyCode=39))
        cdp.call('Input.dispatchKeyEvent', dict(type='keyUp', key='ArrowRight', code='ArrowRight', windowsVirtualKeyCode=39, nativeVirtualKeyCode=39))
        slider = cdp.js("({state:window.playheadStudy.state,value:document.getElementById('timeline').value})")
        check('native keyboard slider step20', slider['value'] == '2000' and slider['state']['frame'] == 100 and not slider['state']['playing'], slider)

        for theme in ('gray', 'pink'):
            for mode in ('trig', 'note'):
                choice = cdp.js("""(() => {
                  document.querySelector('[data-theme="%s"]').click();
                  document.querySelector('[data-mode="%s"]').click();
                  return {state:window.playheadStudy.state,
                    pressed:[...document.querySelectorAll('[aria-pressed="true"][data-theme],[aria-pressed="true"][data-mode]')].map(e=>e.dataset.theme||e.dataset.mode),
                    links:[...document.querySelectorAll('.concept-foot a,.downloads a')].map(a=>a.getAttribute('href'))};
                })()""" % (theme, mode))
                part = '_note' if mode == 'note' else ''
                expected_links = [f'{theme}{part}_{i:02}.gif' for i in (1, 2, 3)] + [f'{t}{part}_comparison.gif' for t in ('gray', 'pink')]
                check(f'{theme}/{mode} controls and downloads', choice['state']['theme'] == theme and choice['state']['mode'] == mode and choice['pressed'] == [theme, mode] and choice['links'] == expected_links and all((STUDY / p).is_file() for p in expected_links), choice)
                base = Image.open(STUDY / f'{theme}_{mode}_base.png').convert('RGB')
                glyphs = {(row, col): generator.glyph_runs(base, theme, row, col) for row in range(3) for col in generator.PATTERN[row]}
                for ms in TIMES:
                    result = cdp.js("""(() => {
                      window.playheadStudy.seek(%d); window.playheadStudy.render();
                      return {state:window.playheadStudy.state, images:[...document.querySelectorAll('canvas')].map(c=>c.toDataURL('image/png')),
                        readout:document.getElementById('steps').textContent};
                    })()""" % ms)
                    assert result['state']['frame'] == ms // 20
                    for kind, url in enumerate(result['images']):
                        png = base64.b64decode(url.split(',', 1)[1])
                        got = Image.open(io.BytesIO(png)).convert('RGBA')
                        want = generator.apply(base, generator.commands(base, theme, kind, schedule[ms // 20], glyphs)).convert('RGBA')
                        actual_bytes, expected_bytes = got.tobytes(), want.tobytes()
                        differing = sum(actual_bytes[i:i+4] != expected_bytes[i:i+4] for i in range(0, len(actual_bytes), 4))
                        name = f'{theme}_{mode}_{kind+1:02}_{ms:04}.png'
                        (out / name).write_bytes(png)
                        case = dict(theme=theme, mode=mode, variant=kind+1, ms=ms, pixels=320*240, differing_pixels=differing,
                                    browser_rgba_sha256=hashlib.sha256(actual_bytes).hexdigest(), python_rgba_sha256=hashlib.sha256(expected_bytes).hexdigest(), screenshot=name)
                        receipt['pixel_cases'].append(case)
                        assert differing == 0, case
                    expected_readout = 'tracks 1–4 · step ' + ' / '.join(f'{c+1:02}' for c in schedule[ms // 20]['columns'])
                    assert result['readout'] == expected_readout

        for width in (1280, 1150, 1149, 390, 320):
            viewport(width)
            cdp.js('window.playheadStudy.seek(40)')
            layout = cdp.js("""(() => ({width:innerWidth,scroll:document.documentElement.scrollWidth,
              cards:[...document.querySelectorAll('.concept')].map(e=>{const r=e.getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height}}),
              canvases:[...document.querySelectorAll('canvas')].map(e=>{const r=e.getBoundingClientRect();return {w:r.width,h:r.height,native:[e.width,e.height],rendering:getComputedStyle(e).imageRendering}})
            }))()""")
            assert layout['width'] == width and layout['scroll'] <= width, layout
            ys = [r['y'] for r in layout['cards']]
            assert (ys[0] == ys[1] == ys[2]) if width >= 1150 else (ys[0] < ys[1] < ys[2]), layout
            assert all(c['native'] == [320, 240] and c['rendering'] in ('pixelated', 'crisp-edges') for c in layout['canvases'])
            if width >= 1150:
                assert all(c['w'] == 320 and c['h'] == 240 for c in layout['canvases'])
            receipt['layouts'].append(layout)
            if width in (1280, 390):
                image = cdp.call('Page.captureScreenshot', dict(format='png', captureBeyondViewport=True))
                (out / f'gallery-{width}.png').write_bytes(base64.b64decode(image['data']))

        viewport(1280)
        reduced = load(True)
        check('reduced motion starts paused', not reduced['playing'] and reduced['frame'] == 0, reduced)
        reduced_play = cdp.js("(() => { document.getElementById('play').click();return window.playheadStudy.state; })()")
        check('reduced motion explicit play works', reduced_play['playing'], reduced_play)
        cdp.call('Emulation.setEmulatedMedia', dict(features=[dict(name='prefers-reduced-motion', value='no-preference')]))
        time.sleep(.04)
        cdp.call('Emulation.setEmulatedMedia', dict(features=[dict(name='prefers-reduced-motion', value='reduce')]))
        time.sleep(.04)
        check('reduced motion change pauses', not cdp.js('window.playheadStudy.state')['playing'])
        accessibility = cdp.js("""({lang:document.documentElement.lang,
          labels:[...document.querySelectorAll('button,input,select,canvas')].map(e=>({tag:e.tagName,id:e.id,name:e.getAttribute('aria-label')||e.textContent.trim()||(e.labels&&[...e.labels].map(l=>l.textContent).join(' '))})),
          external:[...document.querySelectorAll('[src],[href]')].map(e=>e.src||e.href).filter(u=>!u.startsWith(location.origin)&&!u.startsWith('data:'))})""")
        check('accessible labels and no external assets', accessibility['lang'] == 'en' and all(x['name'] for x in accessibility['labels']) and not accessibility['external'], accessibility)
        errors = [e for e in cdp.events if e.get('method') == 'Runtime.exceptionThrown' or (e.get('method') == 'Runtime.consoleAPICalled' and e['params'].get('type') == 'error')]
        network = [e['params']['request']['url'] for e in cdp.events if e.get('method') == 'Network.requestWillBeSent']
        check('no browser exceptions or console errors', not errors, errors)
        check('no page external network', all(u.startswith(f'http://127.0.0.1:{port}/') or u.startswith('data:') for u in network), network)
        assert identity == {str(p.relative_to(ROOT)): sha(p) for p in paths}, 'evidence inputs changed during verification'
        assert protected == generator.protected(), 'protected runtime/release inputs changed'
        receipt.update(status='pass', protected_files_unchanged=len(protected), exact_canvas_count=len(receipt['pixel_cases']),
                       exact_rgba_pixels=sum(c['pixels'] for c in receipt['pixel_cases']), differing_pixels=sum(c['differing_pixels'] for c in receipt['pixel_cases']),
                       requests=requests)
    except Exception as exc:
        receipt.update(status='fail', failure=str(exc), traceback=traceback.format_exc())
        raise
    finally:
        if cdp:
            cdp.sock.close()
        if proc:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait(timeout=5)
            receipt['own_chromium_stopped'] = proc.poll() is not None
        if server:
            server.shutdown()
            server.server_close()
            receipt['own_server_stopped'] = True
        if profile:
            shutil.rmtree(profile, ignore_errors=True)
        (out / 'http.log').write_text('\n'.join(logs) + '\n')
        (out / 'verification.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n')
        print(out / 'verification.json', flush=True)
        print(json.dumps({key: receipt[key] for key in ('status', 'exact_canvas_count', 'exact_rgba_pixels', 'differing_pixels') if key in receipt}), flush=True)


if __name__ == '__main__':
    main()
