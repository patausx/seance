#!/usr/bin/env python3
"""Bounded browser-only smoke for the cabinet prototype, in a private Chromium profile.
Never touches the shared browser profile, app code, console or project storage.
"""
import base64
from io import BytesIO
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request
from PIL import Image, ImageChops

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'ui_mockups/start_menu_refresh_study/cabinet-v2'


class CDP:
    def __init__(self,url):
        u=urllib.parse.urlsplit(url)
        self.s=socket.create_connection((u.hostname,u.port),timeout=15)
        self.buffer=b''; self.id=0; self.errors=[]; self.mouse_down=False
        key=base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f'GET {u.path} HTTP/1.1\r\nHost: {u.netloc}\r\nUpgrade: websocket\r\n'
                       f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        while b'\r\n\r\n' not in self.buffer: self.buffer+=self.s.recv(4096)
        header,self.buffer=self.buffer.split(b'\r\n\r\n',1)
        assert header.split(b'\r\n',1)[0].split(b' ',2)[:2]==[b'HTTP/1.1',b'101'],header

    def read(self,n):
        while len(self.buffer)<n:
            block=self.s.recv(65536)
            if not block: raise RuntimeError('browser socket closed')
            self.buffer+=block
        result,self.buffer=self.buffer[:n],self.buffer[n:]
        return result

    def send(self,payload,opcode=1):
        mask=os.urandom(4); n=len(payload)
        size=bytes([128+n]) if n<126 else bytes([254])+struct.pack('!H',n) if n<65536 else bytes([255])+struct.pack('!Q',n)
        self.s.sendall(bytes([128+opcode])+size+mask+bytes(v^mask[i%4] for i,v in enumerate(payload)))

    def receive(self):
        a,b=self.read(2); opcode=a&15; n=b&127
        if n==126: n=struct.unpack('!H',self.read(2))[0]
        if n==127: n=struct.unpack('!Q',self.read(8))[0]
        mask=self.read(4) if b&128 else None
        payload=self.read(n)
        if mask: payload=bytes(v^mask[i%4] for i,v in enumerate(payload))
        if opcode==9: self.send(payload,10); return self.receive()
        assert opcode==1 and a&128,'unexpected frame fragmentation/opcode'
        return json.loads(payload)

    def call(self,method,params=None):
        self.id+=1; id=self.id
        self.send(json.dumps(dict(id=id,method=method,params=params or {})).encode())
        while True:
            message=self.receive()
            if message.get('method')=='Runtime.exceptionThrown': self.errors.append(message)
            if message.get('id')==id:
                assert 'error' not in message,message
                return message['result']

    def js(self,expression):
        result=self.call('Runtime.evaluate',dict(expression=expression,awaitPromise=True,returnByValue=True))
        assert 'exceptionDetails' not in result,result
        return result['result'].get('value')

    def pause(self,ms=180): self.js(f'new Promise(r=>setTimeout(r,{ms}))')
    def mouse(self,type,x,y):
        if type=='mousePressed': self.mouse_down=True
        if type=='mouseReleased': self.mouse_down=False
        params=dict(type=type,x=x,y=y,buttons=1 if self.mouse_down else 0)
        if type!='mouseMoved': params.update(button='left',clickCount=1)
        self.call('Input.dispatchMouseEvent',params)
    def key(self,key):
        self.call('Input.dispatchKeyEvent',dict(type='keyDown',key=key))
        self.call('Input.dispatchKeyEvent',dict(type='keyUp',key=key))


def main():
    log=OUT/'browser-smoke.log'
    with tempfile.TemporaryDirectory(prefix='seance-cabinet-browser-') as tmp, log.open('w') as stderr:
        process=subprocess.Popen(['/usr/bin/chromium','--headless','--no-sandbox','--disable-gpu',
            '--disable-dev-shm-usage','--disable-extensions','--no-first-run','--no-default-browser-check',
            '--remote-debugging-address=127.0.0.1','--remote-debugging-port=0',
            '--user-data-dir='+tmp,'about:blank'],stdout=stderr,stderr=stderr)
        try:
            active=Path(tmp)/'DevToolsActivePort'
            for _ in range(100):
                if active.exists(): break
                assert process.poll() is None,'isolated browser exited'
                time.sleep(.05)
            port=int(active.read_text().splitlines()[0])
            with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list') as response:
                page=next(p for p in json.load(response) if p.get('type')=='page' and p.get('url')=='about:blank')
            c=CDP(page['webSocketDebuggerUrl']); c.call('Runtime.enable'); c.call('Page.enable')
            c.call('Emulation.setDeviceMetricsOverride',dict(width=1000,height=1320,deviceScaleFactor=1,mobile=False))
            navigation=c.call('Page.navigate',dict(url=(OUT/'index.html').as_uri()))
            assert 'errorText' not in navigation,navigation
            for _ in range(100):
                if c.js('window.cabinetReady===true'): break
                c.pause(50)
            assert c.js('window.cabinetReady===true'), (c.errors,c.js("({url:location.href,title:document.title,ready:document.readyState,hasData:!!window.CABINET,text:document.body?.innerText?.slice(0,500)})"))
            def state(): return c.js('cabinetState()')
            def center(id): return c.js(f"(()=>{{const b=document.querySelector('[data-id=\"{id}\"]').getBoundingClientRect();return [b.x+b.width/2,b.y+b.height/2]}})()")
            ids=c.js("[...document.querySelectorAll('#targets button')].map(b=>b.dataset.id)")
            assert len(ids)==7
            for id in ids:
                x,y=center(id); before=state()['commits']
                c.mouse('mouseMoved',x,y); c.mouse('mousePressed',x,y)
                assert state()['held'] and state()['commits']==before,'press must not activate'
                c.pause(160); c.mouse('mouseReleased',x,y)
                assert state()['commits']==before+1 and c.js("document.querySelector('#status').dataset.action")==id
            # Leaving a captured row irrevocably cancels, even after moving back.
            c.js("window.pointerTrace=[];for(const type of ['pointerdown','pointermove','pointerup','click'])document.addEventListener(type,e=>window.pointerTrace.push([type,e.clientX,e.clientY,e.buttons,e.detail,e.target.dataset.id,cabinetState()]),true)")
            x,y=center('new'); before=state()['commits']
            c.mouse('mouseMoved',x,y); c.mouse('mousePressed',x,y)
            c.mouse('mouseMoved',10,10); c.pause(40)
            c.mouse('mouseMoved',x,y); c.pause(40); c.mouse('mouseReleased',x,y)
            assert state()['commits']==before,('drag out-and-back must not activate',c.js('window.pointerTrace'))
            c.js("document.querySelector('[data-id=recent-0]').focus()")
            c.key('ArrowLeft'); assert state()['selected']=='new'
            c.key('ArrowDown'); assert state()['selected']=='library'
            c.key('ArrowRight'); assert state()['selected']=='recent-1'
            before=state()['commits']; c.key('a'); assert state()['commits']==before+1
            c.key('Escape'); assert state()['commits']==before+1
            c.call('Emulation.setEmulatedMedia',dict(features=[dict(name='prefers-reduced-motion',value='reduce')]))
            assert state()['reduced']
            c.call('Emulation.setEmulatedMedia',dict(features=[]))
            native=[]
            for theme in ('pink','graphite'):
                c.js(f"document.querySelector('[data-theme={theme}]').click();document.querySelector('[data-id=recent-0]').focus()")
                c.pause(650)
                encoded=c.js("document.querySelector('#screen').toDataURL('image/png').split(',')[1]")
                im=Image.open(BytesIO(base64.b64decode(encoded))).convert('RGB')
                expected=Image.open(OUT/f'cabinet-v2-{theme}-dual-2x.png').convert('RGB').resize((400,496),Image.Resampling.NEAREST)
                assert ImageChops.difference(im,expected).getbbox() is None,theme+' browser/native pixel equality'
                native.append(theme)
            image=c.call('Page.captureScreenshot',dict(format='png'))
            (OUT/'browser-proof.png').write_bytes(base64.b64decode(image['data']))
            assert not c.errors,c.errors
            report=dict(status='pass',pointer_targets=len(ids),release_only=True,drag_out_return_cancels=True,
                keyboard_navigation=True,keyboard_activation=True,escape_no_commit=True,reduced_motion=True,
                exact_browser_native_themes=native,javascript_exceptions=0,shared_profile_touched=False,
                kind='browser prototype only; not 3ds controller or hardware validation')
            (OUT/'browser-smoke.json').write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps(report)); c.s.close()
        finally:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired: process.kill();process.wait()


if __name__=='__main__': main()
