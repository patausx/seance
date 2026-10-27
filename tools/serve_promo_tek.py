#!/usr/bin/env python3
"""Serve ONLY this promo's viewing page, final video, poster and composed audio.

Local/LAN preview; no upload, directory browsing or arbitrary filesystem routes.
Single HTTP byte ranges support video seeking without publishing the workspace.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
from urllib.parse import urlsplit

ROOT=Path(__file__).resolve().parents[1]/'promo/tek-01'
FILES={
    '/':('index.html','text/html; charset=utf-8'),
    '/index.html':('index.html','text/html; charset=utf-8'),
    '/seance-tek-192.mp4':('seance-tek-192.mp4','video/mp4'),
    '/poster.jpg':('seance-tek-192-poster.jpg','image/jpeg'),
    '/audio.wav':('audio/seance-tek-192.wav','audio/wav'),
}

class Handler(BaseHTTPRequestHandler):
    def do_HEAD(self):self.respond(False)
    def do_GET(self):self.respond(True)
    def respond(self,body):
        route=FILES.get(urlsplit(self.path).path)
        if not route:return self.send_error(404)
        path=ROOT/route[0]
        if not path.is_file():return self.send_error(404)
        size=path.stat().st_size;start=0;end=size-1;status=200
        requested=self.headers.get('Range')
        if requested:
            match=re.fullmatch(r'bytes=(\d*)-(\d*)',requested)
            if not match or not any(match.groups()):return self.send_error(416)
            a,b=match.groups()
            if a:start=int(a);end=min(size-1,int(b)) if b else size-1
            else:start=max(0,size-int(b))
            if start>=size or start>end:return self.send_error(416)
            status=206
        self.send_response(status)
        self.send_header('Content-Type',route[1]);self.send_header('Accept-Ranges','bytes')
        self.send_header('Content-Length',str(end-start+1))
        self.send_header('Cache-Control','no-cache');self.send_header('X-Content-Type-Options','nosniff')
        if status==206:self.send_header('Content-Range',f'bytes {start}-{end}/{size}')
        self.end_headers()
        if body:
            try:
                with path.open('rb') as f:
                    f.seek(start);remaining=end-start+1
                    while remaining:
                        block=f.read(min(256*1024,remaining))
                        if not block:break
                        self.wfile.write(block);remaining-=len(block)
            except (BrokenPipeError,ConnectionResetError):pass

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bind',default='0.0.0.0');p.add_argument('--port',type=int,default=8791)
    a=p.parse_args();print(f'seance promo preview on {a.bind}:{a.port}',flush=True)
    ThreadingHTTPServer((a.bind,a.port),Handler).serve_forever()
