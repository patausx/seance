#!/usr/bin/env python3
"""Fresh production drawing path vs approved ink study and pinned pre-change popup.
No production/private renderer injection in the current executable; no deployment.
"""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT/'release/go-to-living-ink'
APPROVED = ROOT/'ui_mockups/go_to_living_ink_study/20261005T192141_138724Z'
EVENTS = {2000: 1, 4000: 2, 6000: 3, 8000: 7, 10000: 6, 12000: 5, 14000: 4, 15600: 0}
DRIVER = r'''
#define main unused_inherited_menu_main
#include "menu_stage1_test.cpp"
#undef main
#include "nav_backdrop.h"
namespace ink_capture {
void save(const Frame& f,const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary);out<<"P6\n"<<f.width<<" 240\n255\n";
    for(u32 c:f.pixels){out.put(char(c));out.put(char(c>>8));out.put(char(c>>16));}
    CHECK(bool(out));
}
std::vector<uint8_t> bgr(const Frame& f) {
    std::vector<uint8_t> b(f.width*240*3);
    for(int y=0;y<240;++y)for(int x=0;x<f.width;++x) {
        const u32 c=f.pixels[y*f.width+x];auto* p=&b[(x*240+239-y)*3];
        p[0]=uint8_t(c>>16);p[1]=uint8_t(c>>8);p[2]=uint8_t(c);
    }
    return b;
}
void key(u32 k){NavChoice c;nav_buttons(k,c);CHECK(c==NavChoice::None&&nav_is_open());}
void run(const std::filesystem::path& out) {
    CHECK(!std::filesystem::exists(out));std::filesystem::create_directories(out);
    std::ofstream state(out/"state.tsv"),counts(out/"commands.tsv"),masks(out/"masks.tsv");
    for(const char* name:{"patch table","sigils","sigilbay","sequencer","perform","bounce","patches","exit"}) {
        const menu_labels::Label* l=nullptr;
        for(const auto& label:menu_labels::labels)if(!std::strcmp(name,label.text))l=&label;
        CHECK(l&&l->width<=128&&l->height<=254);
        for(int y=0;y<l->height;++y)for(int x=0;x<l->width;++x)
            if(menu_labels::bitmap[l->offset+y*((l->width+7)/8)+x/8]&(0x80>>(x%8)))masks<<name<<'\t'<<x<<'\t'<<y<<'\n';
    }
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
        palette::set(theme);Fixture f;f.four();chains_capture();host_time_ms=1000;chains_capture();
        const auto tb=bgr(render(400,chains_context_draw_top)),bb=bgr(render(320,chains_view_draw));
        nav_open();nav_backdrop::freeze(tb.data(),bb.data());
        const std::string name=theme==palette::Theme::Graphite?"graphite":"pink";
        const auto ct=std::vector<u32>(std::begin(sigil_surface::top_cache.pixels),std::end(sigil_surface::top_cache.pixels));
        const auto cb=std::vector<u32>(std::begin(sigil_surface::bottom_cache.pixels),std::end(sigil_surface::bottom_cache.pixels));
        const int spans=render(320,[]{nav_backdrop::draw(false);}).objects+render(400,[]{nav_backdrop::draw(true);}).objects;
        int raw_peak=0,estimate=0;
        for(unsigned t=0;t<16000;t+=40) {
            host_time_ms=1000+t;
            if(t==2000||t==4000||t==6000)key(KEY_DOWN);
            if(t==8000)key(KEY_RIGHT);
            if(t==10000||t==12000||t==14000)key(KEY_UP);
            if(t==15600)key(KEY_LEFT);
            nav_capture();
            const auto bottom=render(320,nav_draw),top=render(400,nav_clock_draw_top);
            auto* e=g_eng;auto* a=g_audio;g_eng=nullptr;g_audio=nullptr;
            const auto time=host_time_ms;host_time_ms+=999999;
            CHECK(render(320,nav_draw).pixels==bottom.pixels);
            CHECK(render(400,nav_clock_draw_top).pixels==top.pixels);
            g_eng=e;g_audio=a;host_time_ms=time;
            CHECK(std::equal(ct.begin(),ct.end(),std::begin(sigil_surface::top_cache.pixels)));
            CHECK(std::equal(cb.begin(),cb.end(),std::begin(sigil_surface::bottom_cache.pixels)));
            CHECK(f.engine->bpm==120&&f.engine->swing_percent==50&&mutations==0);
            raw_peak=std::max(raw_peak,top.objects+bottom.objects);
            estimate=std::max(estimate,top.objects+bottom.objects-spans+2);CHECK(estimate<8192);
            char stem[30];std::snprintf(stem,sizeof stem,"frame-%04u",t/40);
            save(bottom,out/name/(std::string(stem)+"-bottom.ppm"));
            save(top,out/name/(std::string(stem)+"-top.ppm"));
            state<<name<<'\t'<<t<<'\t'<<f.engine->bpm<<'\t'<<f.engine->swing_percent<<'\t'<<mutations<<'\t'<<int(nav_is_open())<<'\n';
        }
        counts<<name<<'\t'<<raw_peak<<'\t'<<estimate<<'\n';nav_close();nav_transition_clear();
    }
    std::puts("pass: 800 live production phase frames, frozen detached redraw, original cache, unchanged clock/dirty; bounded command estimate (not device timing)");
}
}
int main(int argc,char** argv) {
    try{CHECK(argc==2);ink_capture::run(argv[1]);return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL capture: %s\n",e.what());return 1;}
}
'''


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def production():
    paths = [p for d in ('core', 'platform', 'assets') for p in (ROOT/d).rglob('*') if p.is_file()]
    paths += [ROOT/'Makefile', ROOT/'VERSION', *[p for p in ROOT.glob('seance.*') if p.is_file()]]
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted(set(paths))}


def run(cmd, log, cwd=ROOT):
    with log.open('x') as f:
        f.write('$ '+' '.join(map(str, cmd))+'\n');f.flush()
        p = subprocess.run(list(map(str, cmd)), cwd=cwd, stdout=f, stderr=subprocess.STDOUT,
                           env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'), timeout=240)
    if p.returncode:
        raise RuntimeError(str(log)+'\n'+log.read_text()[-5000:])


def package_and_check(out):
    masks = {}
    for line in (out/'current/masks.tsv').read_text().splitlines():
        name, x, y = line.split('\t');masks.setdefault(name, []).append((int(x), int(y)))
    assert len(masks) == 8
    assert (out/'current/masks.tsv').read_bytes() == (out/'baseline/masks.tsv').read_bytes()
    assert (out/'current/state.tsv').read_bytes() == (out/'baseline/state.tsv').read_bytes()
    compared = changed = exact = tops = 0
    for theme in ('graphite', 'pink'):
        focus, previous, started = 0, 0, 0
        animation = []
        for i in range(400):
            t = i*40
            if t in EVENTS:
                previous, focus, started = focus, EVENTS[t], t
            age = min(120, t-started)
            weight = (255*age*age*(360-2*age)+120**3//2)//120**3
            allowed = Image.new('L', (320, 240))
            for index, points in enumerate(masks.values()):
                shift = (255 if index == 0 else 0) if t < 2000 else (weight if index == focus else 255-weight if index == previous else 0)
                dx = (2*shift+127)//255
                for x, y in points:
                    allowed.putpixel((32+160*(index//4)+dx+x, 58+42*(index%4)+y), 255)
            imgs = {}
            for variant in ('current', 'baseline'):
                for screen in ('top', 'bottom'):
                    p = out/variant/theme/f'frame-{i:04d}-{screen}.ppm'
                    with Image.open(p) as im:
                        image = im.convert('RGB')
                    image.save(p.with_suffix('.png'));p.unlink()
                    imgs[variant, screen] = image
            assert imgs['current', 'top'].tobytes() == imgs['baseline', 'top'].tobytes(), (theme, t, 'upper changed')
            tops += 1
            diff = ImageChops.difference(imgs['current', 'bottom'], imgs['baseline', 'bottom'])
            occupancy = diff.point(lambda v: 255 if v else 0).convert('L').point(lambda v: 255 if v else 0)
            assert not ImageChops.subtract(occupancy, allowed).getbbox(), (theme, t, 'outside mask')
            changed += sum(1 for v in occupancy.getdata() if v)
            compared += 320*240
            with Image.open(APPROVED/'capture'/theme/f'frame-{i:04d}.png') as ref:
                assert ref.convert('RGB').tobytes() == imgs['current', 'bottom'].tobytes(), (theme, t, 'approved mismatch')
            exact += 1
            animation.append(imgs['current', 'bottom'].resize((640, 480), Image.Resampling.NEAREST))
            if t == 1000:
                dual = Image.new('RGB', (400, 496), (18, 18, 21));dual.paste(imgs['current', 'top'], (0, 0));dual.paste(imgs['current', 'bottom'], (40, 256))
                dual.resize((800, 992), Image.Resampling.NEAREST).save(out/f'{theme}-runtime-dual-2x.png')
            if t in (0, 1000, 2000, 6000, 8000, 14000):
                with Image.open(APPROVED/'capture'/theme/f'upper-{t}.png') as ref:
                    assert ref.convert('RGB').tobytes() == imgs['current', 'top'].tobytes()
        path = out/f'{theme}-runtime-2x.png'
        animation[0].save(path, save_all=True, append_images=animation[1:], duration=40, loop=0, disposal=0, blend=0)
        with Image.open(path) as apng:
            assert apng.n_frames == 400
            for i, original in enumerate(animation):
                apng.seek(i);assert apng.convert('RGB').tobytes() == original.tobytes()
    return dict(approved_bottoms_exact=exact, unchanged_upper_frames=tops,
                compared_bottom_pixels=compared, changed_inside_ink=changed, apng_roundtrips=800,
                current_commands=(out/'current/commands.tsv').read_text())


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--sanitize', action='store_true');args=parser.parse_args()
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out=RELEASE/('capture-'+('sanitized-' if args.sanitize else 'optimized-')+stamp);out.mkdir(parents=True, exist_ok=False)
    (out/'objects').mkdir();(out/'driver.cpp').write_text(DRIVER)
    before=production()
    pre=json.loads((RELEASE/'before.json').read_text());baseline=ROOT/pre['source_backup'];assert sha(baseline)==pre['source_backup_sha256']
    approved=json.loads((APPROVED/'proposal.json').read_text());assert sha(APPROVED/'proposal.json')==pre['approved_preview_sha256']
    for name, digest in approved['files'].items():assert sha(APPROVED/name)==digest, name
    # Pin every potential source before the compiler starts, then retain only
    # actual per-object -MMD dependencies; unrelated agent test edits are excluded.
    candidates={str(p.resolve()):sha(p) for d in ('core','platform','tools') for p in (ROOT/d).rglob('*') if p.is_file() and p.suffix in ('.h','.hpp','.cpp','.inc','.c')}
    candidates[str(baseline.resolve())]=sha(baseline);candidates[str((out/'driver.cpp').resolve())]=sha(out/'driver.cpp')
    sources=[out/'driver.cpp']+[ROOT/('platform/3ds/'+p+'.cpp') for p in ('ui_picker','ui_chains','ui_workspace','menu_style','project_names','ui_chains_top','ui_theme','ui_transition')]
    sources += [ROOT/'core/ui/draw.cpp',ROOT/'core/ui/font.cpp',*sorted((ROOT/'core/engine').glob('*.cpp'))]
    sources += [ROOT/('core/'+p) for p in ('synth/sampler.cpp','synth/wav_loader.cpp','synth/wav_writer.cpp')]
    cxx=os.environ.get('CXX','g++')
    flags=['-std=gnu++17','-O1','-g1','-Wall','-Wextra','-ffunction-sections','-fdata-sections']
    if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    flags += ['-I'+str(ROOT/p) for p in ('tests/menu_stage1_test_stubs','tests/sequencer_test_stubs','core','tools','platform/3ds')]
    all_sources=sources+[ROOT/'platform/3ds/ui_nav.cpp',baseline]
    def compile_one(pair):
        i,source=pair;obj=out/'objects'/f'{i:02d}.o'
        run([cxx,*flags,'-MMD','-MF',obj.with_suffix('.d'),'-c',source,'-o',obj],obj.with_suffix('.log'))
        return obj
    with ThreadPoolExecutor(max_workers=3) as pool:objects=list(pool.map(compile_one,enumerate(all_sources)))
    for variant,index in (('current',-2),('baseline',-1)):
        exe=out/(variant+'.exe')
        run([cxx,*flags,'-Wl,--gc-sections','-Wl,--wrap=_ZN6seance2ui4textEiiPKcji',*objects[:-2],objects[index],'-o',exe],out/(variant+'-link.log'))
        run([exe,out/variant],out/(variant+'-capture.log'))
    dependencies={}
    for depfile in (out/'objects').glob('*.d'):
        paths=depfile.read_text().replace('\\\n',' ').split(':',1)[1].split()
        for path in paths:
            p=Path(path).resolve();digest=sha(p);assert candidates[str(p)]==digest,str(p)
            dependencies[str(p.relative_to(ROOT))]=digest
    checks=package_and_check(out)
    assert production()==before
    receipt=dict(status='passed',mode='sanitized' if args.sanitize else 'optimized',
                 production_inputs=before,dependencies=dependencies,checks=checks,
                 approved_receipt_sha256=sha(APPROVED/'proposal.json'),runner_sha256=sha(__file__),
                 limits='native host proof; not hardware FPS, touch/listening or deployment; software cache spans separated from two GPU image quads',
                 files={str(p.relative_to(out)):sha(p) for p in out.rglob('*') if p.is_file()})
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(out);print(json.dumps(checks,indent=2))


if __name__=='__main__':main()
