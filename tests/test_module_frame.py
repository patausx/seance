#!/usr/bin/env python3
"""Fixed module chrome contract; optional one scoped current native-host capture.

No historical receipt is rewritten. Chrome envelopes are not clipping regions:
specialized body controls and their padded targets retain their local layout.
"""
import argparse
import csv
import json
from pathlib import Path
from functools import lru_cache
import re
import subprocess
import sys
import tempfile
import check_module_frame_contract as contract

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'release/module-frame'
CASES = ROOT / 'tests/module_frame_ui_cases.inc'


def fast_check():
    import test_fader_lancet_ui as common
    contract.check()
    source = (ROOT / 'platform/3ds/menu_style.cpp').read_text().split(
        'namespace seance::platform::ui3ds::editor_style {', 1)[1]
    funcs = common.functions(source)
    actual = '\n'.join(funcs[n][2] for n in ('caption', 'header_text', 'footer'))
    # Compile the exact production helpers, not a Python reimplementation.
    driver = r'''
#include "platform/3ds/editor_style.h"
#include "platform/3ds/module_heading.h"
#include <cassert>
#include <string>
#include <vector>
namespace f=seance::platform::ui3ds::module_frame_layout;
static_assert(f::frame(320).w==292&&f::frame(400).w==372);
static_assert(f::frame(320).h==228&&f::frame(400).h==228);
static_assert(f::header(320).y==8&&f::header(320).h==26);
static_assert(f::footer(320).y==224&&f::footer(320).h==12);
static_assert(f::kHeaderTextY==14&&f::kHeaderMarkY==26);
static_assert(f::kFooterTextY==228&&f::kTextHeight==8);
static_assert(seance::platform::ui3ds::module_heading::kX==14);
static_assert(seance::platform::ui3ds::module_heading::kY==8);
struct Text {int x,y,scale;std::string text;};
std::vector<Text> texts;
namespace seance::ui {
void text(int x,int y,const char* s,uint32_t,int scale){texts.push_back({x,y,scale,s});}
}
namespace seance::platform::ui3ds::menu_style {uint32_t ghost(){return 0xff808080u;}}
namespace seance::platform::ui3ds::editor_style {
''' + actual + r'''
}
int main(){namespace m=seance::platform::ui3ds::editor_style;
 for(int width=0;width<=500;++width)for(int len:{0,1,2,3,10,48,62,63,200}){
  const std::string s(len,'a');texts.clear();m::caption(14,14,width,s.c_str(),0);
  assert(texts.size()<=1);
  if(texts.empty()){assert(width<6||len==0);continue;}
  const auto&t=texts[0];assert(t.x==14&&t.y==14&&t.scale==1);
  assert(int(t.text.size())*6<=width&&t.text.size()<=62);
  if(int(t.text.size())<len){assert(!t.text.empty()&&t.text.back()=='.');}
  else assert(t.text==s);
 }
 for(int width:{320,400})for(int a:{0,1,20,48,62,100})for(int b:{0,1,8,30,62,100}){
  std::string left(a,'a'),right(b,'b');texts.clear();m::footer(width,left.c_str(),right.c_str());
  for(const auto&t:texts){assert(t.y==228&&t.scale==1);assert(t.x>=14&&t.x+int(t.text.size())*6<=width-14);}
  if(texts.size()==2)assert(texts[0].x+int(texts[0].text.size())*6+8<=texts[1].x);
 }
 texts.clear();m::footer(320,nullptr,nullptr);m::caption(14,14,200,nullptr,0);assert(texts.empty());
 m::header_text(80,140,"12345678901",0);assert(texts.size()==1&&texts[0].y==14&&texts[0].text=="1234567...");
}
'''
    with tempfile.TemporaryDirectory(prefix='seance-frame-') as folder:
        cpp, binary = Path(folder) / 'test.cpp', Path(folder) / 'test'
        cpp.write_text(driver)
        subprocess.run(['g++', '-std=c++17', '-O1', '-g1', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-fno-pie', '-no-pie', '-I'+str(ROOT),
                        str(cpp), '-o', str(binary)], check=True, cwd=ROOT)
        subprocess.run([str(binary)], check=True, cwd=ROOT)
    print('module frame: fixed geometry and exact production caption helpers passed (ASAN/UBSAN)')


def current_cases():
    import verify_module_identity_current as masks
    text = masks.current_cases()
    old = '''for(const auto&item:f::catalogue){state(item.kind,p,"default");if(changed(item.kind))for(const char*v:{"bypass","long6","holes","live","lens","lock"})state(item.kind,p,v);
   else if(workflow(item.kind))for(const char*v:{"lens","lock"})state(item.kind,p,v);}'''
    new = '''for(const auto&item:f::catalogue)for(const char*v:{"default","bypass","lock"})state(item.kind,p,v);
  for(auto k:{UnitKind::Chorus,UnitKind::Delay,UnitKind::Ghost,UnitKind::Reverb,UnitKind::Stranular,UnitKind::Ring,UnitKind::Fm})
   for(const char*v:{"long6","holes"})state(k,p,v);'''
    assert text.count(old) == 1
    text = text.replace(old, new).replace('28 modules x 2 themes', 'all registered modules x 2 themes')
    # Current swarm has a header setup disclosure, not the historical more hit.
    old_swarm = '{Scene s(UnitKind::Stranular);tap(283,214);take(s,prefix+"strn_more","more");}'
    assert text.count(old_swarm) == 1
    text = text.replace(old_swarm, '{Scene s(UnitKind::Stranular);tap(188,14);take(s,prefix+"strn_setup","setup");}')
    assert text.count('\nint run(') == 1 and text.count('variants(p);extra_identity_states(p);') == 1
    return text.replace('\nint run(', '\n'+CASES.read_text()+'\nint run(').replace(
        'variants(p);extra_identity_states(p);', 'variants(p);extra_identity_states(p);frame_header_actions(p);')


@lru_cache(None)
def mod_title_colors():
    """Exact shared MOD header veil over the unchanged title, not a waiver.

    The native host fixture uses menu paper's row-gradient fallback for this
    veil. Query the real palette in C++, then use the host compositor's integer
    alpha law. Foreground AND clear mask bits remain checked at the fixed origin.
    """
    lens = (ROOT/'platform/3ds/ui_param_lens.cpp').read_text()
    match = re.search(r'm::paper_rect\(0,\s*0,\s*320,\s*44,\s*(\d+)\)', lens)
    assert match, 'shared MOD header veil recipe changed; review the oracle'
    alpha = int(match[1])
    assert 0 < alpha < 255
    cpp = r'''
#include "platform/3ds/ui_palette.h"
#include <cstdio>
#include <initializer_list>
namespace p=seance::platform::ui3ds::palette;
int main(){for(auto t:{p::Theme::Graphite,p::Theme::Pink}){
 p::set(t);for(int y=0;y<44;++y){const auto c=p::paper_color(y),ink=p::spot();
  std::printf("%d %d",t==p::Theme::Pink,y);
  for(int s=0;s<24;s+=8)std::printf(" %u",unsigned(((c>>s&255)*ALPHA+(ink>>s&255)*(255-ALPHA)+127)/255));
  std::puts("");}}
}
'''.replace('ALPHA', str(alpha))
    with tempfile.TemporaryDirectory(prefix='seance-frame-mod-colors-') as folder:
        source, binary = Path(folder)/'colors.cpp', Path(folder)/'colors'
        source.write_text(cpp)
        subprocess.run(['g++','-std=c++17','-O2','-I'+str(ROOT),str(source),'-o',str(binary)], check=True)
        result = subprocess.check_output([str(binary)],text=True)
    colors = {'graphite':{},'pink':{}}
    for line in result.splitlines():
        theme,y,*rgb = map(int,line.split())
        colors['pink' if theme else 'graphite'][y] = tuple(rgb)
    assert all(len(rows)==44 for rows in colors.values())
    return colors, alpha


def exact_mod_title(image, mask, theme, origin=(14,8)):
    from PIL import Image
    x,y=origin;w,h,bits,_=mask
    colors,alpha=mod_title_colors()
    with Image.open(image) as im:
        assert im.mode=='RGB' and im.size==(320,240)
        assert x>=0 and y>=0 and x+w<=320 and y+h<=44
        actual=list(im.crop((x,y,x+w,y+h)).getdata())
    wrong=[(i%w,i//w,bool(bits[i]),rgb) for i,rgb in enumerate(actual)
           if (rgb==colors[theme][y+i//w])!=bool(bits[i])]
    assert not wrong, ('wrong exact veiled title pixels',str(image),origin,'count',len(wrong),'first',wrong[:8])
    return {'image':str(image),'origin':list(origin),'size':[w,h],
            'ink_pixels':sum(bits),'checked_pixels':w*h,'shared_mod_veil_alpha':alpha,
            'foreground_rgb_by_row':[list(colors[theme][yy]) for yy in range(y,y+h)]}


def validate_mod_title_oracle(mask):
    """Reject a missing ink pixel, painted clear pixel and shifted title."""
    from PIL import Image
    colors,_=mod_title_colors();w,h,bits,_=mask
    with tempfile.TemporaryDirectory(prefix='seance-frame-mod-mutations-') as folder:
        path=Path(folder)/'mask.png'
        for theme in colors:
            im=Image.new('RGB',(320,240),(0,0,0))
            for i,bit in enumerate(bits):
                if bit:im.putpixel((14+i%w,8+i//w),colors[theme][8+i//w])
            im.save(path);exact_mod_title(path,mask,theme)
            for bit in (True,False):
                i=next(i for i,value in enumerate(bits) if bool(value)==bit)
                damaged=im.copy();damaged.putpixel((14+i%w,8+i//w),(0,0,0) if bit else colors[theme][8+i//w]);damaged.save(path)
                try:exact_mod_title(path,mask,theme)
                except AssertionError:pass
                else:raise AssertionError('MOD title mutation accepted')
            shifted=Image.new('RGB',(320,240),(0,0,0));shifted.paste(im,(1,0));shifted.save(path)
            try:exact_mod_title(path,mask,theme)
            except AssertionError:pass
            else:raise AssertionError('shifted MOD title accepted')


def verify(receipt):
    import test_module_headings_ui as host
    import verify_module_identity_current as masks
    from PIL import Image, ImageDraw
    receipt = Path(receipt)
    proof = json.loads(receipt.read_text())
    assert proof['status'] == 'passed' and proof['run']['exit'] == 0
    assert proof['case_sha256'] == host.sha_text(current_cases())
    assert host.sha(proof['binary']) == proof['binary_sha256']
    for name, sha in proof['source_sha256'].items():
        assert host.sha(ROOT/name) == sha, ('source drift', name)
    for name, sha in proof['dependencies'].items():
        assert host.sha(name) == sha, ('dependency drift', name)
    folder = Path(proof['capture'])
    for name, sha in proof['media'].items():
        assert host.sha(folder/name) == sha, ('media drift', name)
    rows = proof['surfaces']
    assert all(r['outside_rects']=='0' and r['outside_lines']=='0' and r['text_overlaps']=='0' for r in rows), [r for r in rows if any(r[k]!='0' for k in ('outside_rects','outside_lines','text_overlaps'))]
    defaults = [r for r in rows if r['variant']=='default']
    sources = contract.load_sources()
    kinds = contract.check_sources(sources)['kinds']
    expected = {(str(kind), surface, theme) for kind in kinds.values()
                for surface in ('top','bottom') for theme in masks.SPOT_RGB}
    observed = [(r['kind'],r['surface'],r['id'].split('_',1)[0]) for r in defaults]
    assert len(observed)==len(expected) and set(observed)==expected, 'incomplete or duplicate default module surfaces'
    labels = {str(kinds[kind]): mask[:4] for kind,mask in contract.load_masks(sources,kinds).items()}
    validate_mod_title_oracle(next(iter(labels.values())))
    checked = []
    text_checks = []
    for r in rows:
        stem = r['id']+'_'+r['surface']
        theme = r['id'].split('_',1)[0]
        veiled = r['surface']=='bottom' and (r['variant']=='lens' or r['variant'].endswith('-mod'))
        checked.append(exact_mod_title(folder/(stem+'.ppm'), labels[r['kind']], theme) if veiled else
                       masks.exact_title(folder/(stem+'.ppm'), labels[r['kind']], masks.SPOT_RGB[theme], origin=(14,8)))
        width = 400 if r['surface']=='top' else 320
        records = list(csv.DictReader((folder/(stem+'_text.tsv')).open(), delimiter='\t'))
        for t in records:
            x,y,w,h = (int(t[k]) for k in ('x','y','w','h'))
            if y < 26: # recorded small header ink, not bitmap module/control words
                assert 14<=y and y+h<=22, ('metadata row',stem,t)
                assert 14<=x and x+w<=width-14, ('header inset',stem,t)
                text_checks.append((stem,'header',t))
            elif y >= 228:
                assert y+h<=236 and 14<=x and x+w<=width-14, ('footer frame',stem,t)
                text_checks.append((stem,'footer',t))
    log = Path(proof['run']['log']).read_text()
    assert log.count('module frame: visible header centres + qnt bounds/lock passed')==2
    for theme in masks.SPOT_RGB:
        selected = [r for r in defaults if r['surface']=='top' and r['id'].startswith(theme+'_')]
        board = Image.new('RGB',(1632,((len(selected)+3)//4)*180),(18,18,21)); draw = ImageDraw.Draw(board)
        for i,r in enumerate(selected):
            x,y=i%4*408,i//4*180;draw.text((x+6,y+2),r['module'],fill='white')
            for surface,dx,dy in (('top',0,18),('bottom',40,98)):
                with Image.open(folder/(r['id']+'_'+surface+'.ppm')) as im:
                    board.paste(im.crop((0,0,im.width,56)),(x+dx,y+dy))
                    board.paste(im.crop((0,224,im.width,240)),(x+dx,y+dy+58))
        board.save(receipt.parent/(theme+'-frame.png'))
    result = dict(status='passed', receipt=str(receipt), receipt_sha256=host.sha(receipt),
                  tool_sha256=host.sha(__file__), cases_sha256=host.sha(CASES),
                  module_count=len(kinds), default_surfaces=len(defaults),
                  native_duals=len(rows)//2, exact_title_surfaces=len(checked),
                  exact_shared_mod_veil_surfaces=sum('shared_mod_veil_alpha' in c for c in checked),
                  title_pixel_checks=checked,
                  header_footer_text_checks=len(text_checks), text_overlaps=0, outside=0,
                  frame={'side':14,'top':8,'bottom':4,'bottom_width':292,'top_width':372,'height':228,
                         'header_height':26,'header_text_y':14,'footer_height':12,'footer_text_y':228},
                  limits=['chrome envelopes, not body clipping/scaling or new input dead zones',
                          'optimized software host only; no native UI sanitizer, device, ARM, FTP, FPS or audio acceptance',
                          'only registered MOD fixture overlays are included; no broad interaction suite'])
    host.json_write(receipt.parent/'frame-verification.json', result)
    print(json.dumps(result,indent=2))


def capture():
    import test_module_headings_ui as host
    OUT.mkdir(parents=True,exist_ok=True)
    cases = OUT/('cases-'+host.stamp()+'.inc')
    with cases.open('x') as f:
        f.write(current_cases())
    host.OUT=OUT;host.CASES=cases;host.supplement=lambda:{}
    receipt,_=host.build(ROOT,'frame-optimized',capture=True)
    verify(receipt)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture',action='store_true')
    parser.add_argument('--verify',type=Path)
    args=parser.parse_args()
    assert not(args.capture and args.verify)
    if args.verify:
        verify(args.verify)
    else:
        fast_check()
        if args.capture:
            capture()
