#!/usr/bin/env python3
"""Independent font oracle for the actual private C++ module heading raster.

Runs only a tiny host renderer: no application, hardware, audio or deployment.
"""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / 'assets/fonts/Gossip-MedPixel.otf'
CPP = r'''
#include "platform/3ds/module_heading.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static unsigned char raster[80*40];
static int calls=0;
namespace seance::ui {
void rect(float x,float y,float w,float h,uint32_t color) {
    if (int(x)!=x || int(y)!=y || int(w)!=w || int(h)!=h ||
        x<0 || y<0 || w<=0 || h<=0 || x+w>80 || y+h>40 || color!=255) std::abort();
    ++calls;
    for(int row=int(y); row<int(y+h); ++row) for(int col=int(x); col<int(x+w); ++col) {
        if(raster[row*80+col]) std::abort();
        raster[row*80+col]=255;
    }
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    using namespace seance;
    using namespace platform::ui3ds;
    for(int k=0; k<=int(UnitKind::Count); ++k) {
        std::memset(raster,0,sizeof raster);calls=0;
        module_heading::draw(3,2,UnitKind(k),255);
        const auto* label=module_heading::label(UnitKind(k));
        if(!label) { if(calls || module_heading::width(UnitKind(k)))return 3; continue; }
        if(module_heading::width(UnitKind(k))!=label->width || calls>200)return 4;
        char path[1024];std::snprintf(path,sizeof path,"%s/%d.pgm",argv[1],k);
        FILE* f=std::fopen(path,"wb");if(!f)return 5;
        std::fprintf(f,"P5\n80 40\n255\n");std::fwrite(raster,1,sizeof raster,f);std::fclose(f);
        std::printf("%d %s %d %d %d\n",k,label->name,label->width,label->height,calls);
    }
}
'''


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out = ROOT / 'release/module-headings' / ('masks-' + stamp)
    out.mkdir(parents=True)
    inputs = [Path(__file__), FONT, ROOT/'platform/3ds/module_heading.h',
              ROOT/'platform/3ds/module_heading_data.h', ROOT/'platform/3ds/menu_labels_data.h',
              ROOT/'tools/generate_module_heading.py', ROOT/'platform/3ds/picker_catalogue.inc',
              ROOT/'core/engine/unit.h', ROOT/'core/ui/draw.h']
    before = {str(p.relative_to(ROOT)): sha(p) for p in inputs}
    driver = out/'driver.cpp';driver.write_text(CPP)
    binary = out/'raster'
    command = ['g++','-O1','-g1','-std=c++17','-fsanitize=address,undefined',
               '-fno-omit-frame-pointer','-fno-pie','-no-pie','-I'+str(ROOT),str(driver),'-o',str(binary)]
    build = subprocess.run(command, capture_output=True, text=True)
    (out/'build.log').write_text(build.stdout+build.stderr)
    assert build.returncode == 0, build.stderr
    env = dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    run = subprocess.run([str(binary),str(out)],env=env,capture_output=True,text=True)
    (out/'run.log').write_text(run.stdout+run.stderr)
    assert run.returncode == 0 and not run.stderr, run.stdout+run.stderr
    font = ImageFont.truetype(str(FONT),28)
    shared = (ROOT/'platform/3ds/menu_labels_data.h').read_text()
    shared_bytes = bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',shared))
    old_labels = {name:(int(o),int(w),int(h)) for name,o,w,h in
                  re.findall(r'\{"([^"]+)", (\d+), (\d+), (\d+)\}',shared)}
    labels=[];common=0
    for row in run.stdout.splitlines():
        k,name,w,h,calls = row.split();w,h,calls=map(int,(w,h,calls))
        # Independent placement/cropping, not the production data decoder.
        im=Image.new('L',(256,96))
        ImageDraw.Draw(im).text((16,64),name,font=font,fill=255,anchor='ls')
        im=im.point(lambda v:255 if v>=128 else 0)
        glyph=im.crop(im.getbbox())
        assert glyph.size==(w,h),(name,glyph.size,(w,h))
        expected=Image.new('L',(80,40));expected.paste(glyph,(3,2))
        with Image.open(out/(k+'.pgm')) as actual:
            assert actual.tobytes()==expected.tobytes(),('cpp raster',name)
        assert h+2<=27 and w<=57,(name,w,h)
        if name in old_labels:
            off,ow,oh=old_labels[name];assert (ow,oh)==(w,h),name
            stride=(w+7)//8;raw=Image.new('L',(w,h))
            for y in range(h):
                for x in range(w):
                    if shared_bytes[off+y*stride+x//8] & (128>>(x%8)):raw.putpixel((x,y),255)
            assert raw.tobytes()==glyph.tobytes(),('existing shared mask changed',name)
            common+=1
        labels.append(dict(kind=int(k),name=name,width=w,height=h,rectangles=calls))
    assert len(labels)==28 and len({r['kind'] for r in labels})==28
    assert common==25
    assert {str(p.relative_to(ROOT)):sha(p) for p in inputs}==before
    proof=dict(status='pass',created_utc=datetime.now(timezone.utc).isoformat(),labels=labels,
               inputs=before,commands=[command,[str(binary),str(out)]],
               bitmap_labels=28,existing_shared_rasters_exact=common,
               maximum_rectangles=max(r['rectangles'] for r in labels),
               raster_pixels_compared=28*80*40,invalid_kinds_inert=['empty','count'],
               binary_sha256=sha(binary),driver_sha256=sha(driver),
               evidence={p.name:sha(p) for p in out.iterdir() if p.suffix in ('.log','.pgm')},
               limitations=['tiny native host raster only, not composed UI or hardware acceptance'])
    (out/'verification.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('pass: 28 C++ title rasters, 25 unchanged shared masks; max rectangles',proof['maximum_rectangles'])
    print(out/'verification.json')


if __name__=='__main__':
    main()
