#!/usr/bin/env python3
"""Offline retained catalogue resources using system FreeType and the original OTF.

Existing glyph ink and baseline offsets remain exact; no Pillow/font substitution.
SCRT28 and PAUL28 must equal pinned approved native mask hashes. Those digests
keep source exports self-contained: old local proof directories are not inputs.
"""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / 'assets/fonts/Gossip-MedPixel.otf'
META = ROOT / 'platform/3ds/picker_catalogue.inc'
APPROVED_MASKS = {
    'scrt': (34, 15, '0b624d218c9bac67d81f7d4c7172b7d8a2350cc27869f9fae47f9d4b1d5defb3'),
    'paul': (39, 23, 'ead090b2bbafb0ed98d52c0d8d800aedd69db63d21a884599f23760549344238'),
}
NATIVE = r'''
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <cstdio>
#include <vector>
int main(int argc,char**argv){
 FT_Library lib;FT_Face face;if(argc!=3||FT_Init_FreeType(&lib)||FT_New_Face(lib,argv[1],0,&face))return 1;
 for(int size:{28,40}){
  if(FT_Set_Pixel_Sizes(face,0,size))return 2;
  const int W=256,H=100,baseline=70;std::vector<unsigned char> pixels(W*H);FT_Pos pen=4*64;FT_UInt previous=0;
  for(const char*p=argv[2];*p;++p){const FT_UInt index=FT_Get_Char_Index(face,*p);
   FT_Vector kern{};if(previous&&FT_HAS_KERNING(face))FT_Get_Kerning(face,previous,index,FT_KERNING_DEFAULT,&kern);
   pen+=kern.x;if(FT_Load_Glyph(face,index,FT_LOAD_DEFAULT)||FT_Render_Glyph(face->glyph,FT_RENDER_MODE_NORMAL))return 3;
   const auto& b=face->glyph->bitmap;int left=int((pen+32)/64)+face->glyph->bitmap_left,top=baseline-face->glyph->bitmap_top;
   for(unsigned y=0;y<b.rows;++y)for(unsigned x=0;x<b.width;++x){int v=b.buffer[y*b.pitch+x];if(v>=128)pixels[(top+y)*W+left+x]=255;}
   pen+=face->glyph->advance.x;previous=index;
  }
  int x0=W,y0=H,x1=0,y1=0;for(int y=0;y<H;++y)for(int x=0;x<W;++x)if(pixels[y*W+x]){x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x+1);y1=std::max(y1,y+1);}
  std::printf("%d %d %d %d\n",size,x1-x0,y1-y0,y0-baseline);
  for(int y=y0;y<y1;++y)for(int byte=0;byte<(x1-x0+7)/8;++byte){unsigned v=0;for(int bit=0;bit<8;++bit)if(x0+byte*8+bit<x1&&pixels[y*W+x0+byte*8+bit])v|=128u>>bit;std::printf("%02x",v);}std::puts("");
 }
 FT_Done_Face(face);FT_Done_FreeType(lib);
}
'''


def glyphs(name='scrt'):
    with tempfile.TemporaryDirectory(prefix='seance-scrt-font-') as folder:
        cpp, binary = Path(folder)/'glyph.cpp', Path(folder)/'glyph'
        cpp.write_text(NATIVE)
        subprocess.run(['g++','-std=c++17','-O2','-I/usr/include/freetype2',str(cpp),'-lfreetype','-o',str(binary)],check=True)
        lines = subprocess.check_output([str(binary),str(FONT),name],text=True).splitlines()
    result = {}
    for i in range(0,len(lines),2):
        size,w,h,dy = map(int,lines[i].split())
        result[size] = (w,h,dy,bytes.fromhex(lines[i+1]))
    # Exact original mask identity without opening unexported historical receipts.
    if name in APPROVED_MASKS:
        w,h,digest = APPROVED_MASKS[name]
        assert result[28][:2] == (w,h) and hashlib.sha256(result[28][3]).hexdigest() == digest, \
            'FreeType differs from approved original Gossip28 ' + name
    return result


def append_bitmap(text, values):
    match = re.search(r'bitmap\[\]\s*=\s*\{([^}]*)\}',text)
    offset = len(re.findall(r'0x[0-9a-fA-F]{2}',match[1]))
    addition = ''.join('    '+','.join('0x%02x'%v for v in values[i:i+20])+',\n' for i in range(0,len(values),20))
    return text[:match.end(1)]+addition+text[match.end(1):],offset


def retained_bitmap(path):
    text=Path(path).read_text()
    assert hashlib.sha256(FONT.read_bytes()).hexdigest() in text
    raw=bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',text.split('bitmap[] = {',1)[1].split('};',1)[0]))
    return text,raw


def retained_heading():
    """Canonical generator output, using checked original retained masks offline."""
    g=glyphs();text,raw=retained_bitmap(ROOT/'platform/3ds/module_heading_data.h')
    masks={kind:(name,int(w),int(h),raw[int(off):int(off)+(int(w)+7)//8*int(h)])
           for kind,name,off,w,h in re.findall(r'\{UnitKind::(\w+), "([^"]+)", (\d+), (\d+), (\d+)\}',text)}
    masks['Scratch']=('scrt',g[28][0],g[28][1],g[28][3])
    paul=glyphs('paul')[28]
    masks['Paul']=('paul',paul[0],paul[1],paul[3])
    data=[];rows=[]
    for kind,name in re.findall(r'^PICKER_ENTRY\((\w+), "([^"]+)"',META.read_text(),re.M):
        name='filter' if kind=='Filter' else name
        label,w,h,bitmap=masks[kind];assert label==name
        rows.append(f'    {{UnitKind::{kind}, "{name}", {len(data)}, {w}, {h}}},');data.extend(bitmap)
    prefix=text[:text.index('inline constexpr uint8_t bitmap[] = {')]
    prefix=re.sub(r'// Catalogue SHA256: [0-9a-f]+','// Catalogue SHA256: '+hashlib.sha256(META.read_bytes()).hexdigest(),prefix)
    tail=text[text.index('constexpr bool complete()'):]
    return prefix+'inline constexpr uint8_t bitmap[] = {\n'+''.join('    '+','.join(f'0x{v:02x}' for v in data[i:i+20])+',\n' for i in range(0,len(data),20))+'};\ninline constexpr Label labels[] = {\n'+'\n'.join(rows)+'\n};\n'+tail


def retained_picker():
    """Canonical sorted picker layout; every existing glyph is retained exact."""
    import json
    import textwrap
    g=glyphs();text,raw=retained_bitmap(ROOT/'platform/3ds/picker_catalogue_data.h')
    masks={name:(int(w),int(h),int(dy),raw[int(off):int(off)+(int(w)+7)//8*int(h)])
           for name,off,w,h,dy in re.findall(r'inline constexpr Label (\w+) = \{(\d+), (\d+), (\d+), (-?\d+)\}',text)}
    masks.update(Scratch_row=g[28],Scratch_title=g[40])
    paul=glyphs('paul')
    masks.update(Paul_row=paul[28],Paul_title=paul[40])
    records=[]
    for line in META.read_text().splitlines():
        if not line.startswith('PICKER_ENTRY('):continue
        match=re.fullmatch(r'PICKER_ENTRY\((\w+), ("[^"]*"), ("[^"]*"), (\w+), ("[^"]*")\)',line)
        kind,short,name,group,description=match.groups();records.append((kind,json.loads(short),json.loads(name),group,json.loads(description)))
    data=[];labels=[];rows={}
    def label(name):
        w,h,dy,bitmap=masks[name];labels.append(f'inline constexpr Label {name} = {{{len(data)}, {w}, {h}, {dy}}};');data.extend(bitmap)
    for kind,short,name,group,description in sorted(records,key=lambda e:(e[2],e[0])):
        label(kind+'_row');label(kind+'_title')
        desc=textwrap.wrap(description,40,break_long_words=True,break_on_hyphens=False);assert len(desc)<=2;desc+=['']*(2-len(desc))
        rows[kind]='    {UnitKind::%s, %s, %s, Group::%s, {%s, %s}, %s, %s},'%(kind,json.dumps(short),json.dumps(name),group,json.dumps(desc[0]),json.dumps(desc[1]),kind+'_row',kind+'_title')
    label('empty_title')
    prefix=text[:text.index('inline constexpr uint8_t bitmap[] = {')]
    prefix=re.sub(r'// Metadata SHA256: [0-9a-f]+','// Metadata SHA256: '+hashlib.sha256(META.read_bytes()).hexdigest(),prefix)
    structure=text[text.index('struct Entry {'):text.index('inline constexpr Entry entries[] = {')]
    tail=text[text.index('inline constexpr int count ='):]
    return prefix+'inline constexpr uint8_t bitmap[] = {\n'+''.join('    '+','.join('0x%02x'%v for v in data[i:i+20])+',\n' for i in range(0,len(data),20))+'};\n'+'\n'.join(labels)+'\n'+structure+'inline constexpr Entry entries[] = {\n'+'\n'.join(rows[e[0]] for e in records)+'\n};\n'+tail


def generate(check=False):
    for filename,generate_one in (('module_heading_data.h',retained_heading),('picker_catalogue_data.h',retained_picker)):
        path=ROOT/'platform/3ds'/filename;canonical=generate_one()
        if check:assert path.read_text()==canonical,'stale canonical '+filename
        else:path.write_text(canonical)
    print('scrt resources: original FreeType28 equals approved concept; exact28/40; existing glyph masks retained; canonical tables '+('verified' if check else 'generated'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    generate(parser.parse_args().check)
