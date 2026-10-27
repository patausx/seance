#!/usr/bin/env python3
"""Private GO TO text proposal, not implementation/deployment/device footage.

The current popup is rendered unchanged, then the same frame is drawn with only
its eight native label masks shaded by the real startup cabinet_ink::heading.
Existing 120ms/2px focus motion, underline, current dot, clock and upper are kept.
No accepted-open pulse is invented: GO TO destinations still commit immediately.
"""
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import os
import subprocess

from PIL import Image, ImageChops
from render_module_geometry_study import protected_hashes

ROOT = Path(__file__).resolve().parents[1]
STEP = 40
SAMPLES = (0, 1000, 2000, 6000, 8000, 14000)

HELPER = r'''
#include "start_menu_ink.h"
#include "menu_labels_data.h"
#include <array>
#include <cstring>
namespace goto_study {
bool enabled=false;
unsigned phase=0;
std::array<bool,320*240> mask{};
// The exact span coalescer below is extracted from today's startup renderer.
@GLYPH_ROWS@
void heading(int index,int x,int y,const char* text,bool selected,u32 ink,u32 spot) {
    namespace n=seance::platform::ui3ds;
    if(!enabled) {n::menu_style::heading(x,y,text,selected?spot:ink);return;}
    const n::menu_labels::Label* label=nullptr;
    for(const auto& l:n::menu_labels::labels)if(!std::strcmp(l.text,text))label=&l;
    if(!label)std::abort();
    const int stride=(label->width+7)/8;
    const auto* bits=n::menu_labels::bitmap+label->offset;
    const auto on=[&](int xx,int yy) {
        return xx>=0&&xx<label->width&&yy>=0&&yy<label->height&&
            (bits[yy*stride+xx/8]&(1u<<(7-xx%8)));
    };
    n::cabinet_ink::Clock clock;clock.ms=phase%n::cabinet_ink::kLoopMs;
    GlyphRows rows(x,y);
    for(int row=0;row<label->height;++row) {
        int run=-1;u32 previous=0;
        for(int col=0;col<=label->width;++col) {
            u32 color=0;
            if(on(col,row)) {
                if(x+col<0||x+col>=320||y+row<0||y+row>=240)std::abort();
                mask[(y+row)*320+x+col]=true;
                const bool edge=!(on(col-1,row)&&on(col+1,row)&&on(col,row-1)&&on(col,row+1));
                // Reuse its three authored row phases cyclically; never index
                // the startup's three pulse envelopes with an eight-item index.
                const auto shade=n::cabinet_ink::heading(clock,index%3,col,row,edge,selected?255:0);
                color=(n::theme_config::mix(ink,spot,shade.tint)&0x00ffffffu)|(u32(shade.alpha)<<24);
            }
            if(run>=0&&color!=previous){rows.add(run,col-run,previous);run=-1;}
            if(color&&run<0)run=col;
            previous=color;
        }
        rows.finish(row);
    }
    rows.flush();
}
}
'''

DRIVER = r'''
#define main unused_inherited_menu_main
#include "menu_stage1_test.cpp"
#undef main
#include "private_nav.cpp"

namespace study_driver {
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
    std::filesystem::create_directories(out);
    std::ofstream receipt(out/"checks.tsv");
    receipt<<"theme\tframes\tcompared_pixels\tchanged_ink_pixels\tpeak_pair_host_objects\tpeak_pair_native_command_estimate\n";
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
        palette::set(theme);Fixture fixture;fixture.four();
        chains_capture();host_time_ms=1000;chains_capture();
        const auto source_top=render(400,chains_context_draw_top);
        const auto source_bottom=render(320,chains_view_draw);
        const auto tb=bgr(source_top),bb=bgr(source_bottom);
        nav_open();nav_backdrop::freeze(tb.data(),bb.data());
        const std::string name=theme==palette::Theme::Graphite?"graphite":"pink";
        save(source_top,out/name/"source_top.ppm");save(source_bottom,out/name/"source_bottom.ppm");
        const auto cache_top=std::vector<u32>(std::begin(sigil_surface::top_cache.pixels),std::end(sigil_surface::top_cache.pixels));
        const auto cache_bottom=std::vector<u32>(std::begin(sigil_surface::bottom_cache.pixels),std::end(sigil_surface::bottom_cache.pixels));
        // Host BasicCache::draw emits many CPU spans; hardware emits one image
        // quad per LCD. Keep the raw count AND a separately labelled estimate.
        const int cache_spans=render(320,[]{nav_backdrop::draw(false);}).objects+
            render(400,[]{nav_backdrop::draw(true);}).objects;
        uint64_t changed=0,compared=0;int peak=0,command_peak=0;
        for(unsigned t=0;t<16000;t+=40) {
            host_time_ms=1000+t;
            if(t==2000||t==4000||t==6000)key(KEY_DOWN);
            if(t==8000)key(KEY_RIGHT);
            if(t==10000||t==12000||t==14000)key(KEY_UP);
            if(t==15600)key(KEY_LEFT);
            nav_capture();goto_study::phase=t;goto_study::enabled=false;
            const auto baseline=render(320,nav_draw),upper=render(400,nav_clock_draw_top);
            const int dirty=mutations;auto* e=g_eng;auto* a=g_audio;
            g_eng=nullptr;g_audio=nullptr;
            goto_study::enabled=true;goto_study::mask.fill(false);
            const auto proposal=render(320,nav_draw);
            CHECK(render(320,nav_draw).pixels==proposal.pixels);
            CHECK(render(400,nav_clock_draw_top).pixels==upper.pixels);
            g_eng=e;g_audio=a;CHECK(mutations==dirty);
            int differences=0;
            for(size_t p=0;p<baseline.pixels.size();++p) {
                ++compared;
                if(baseline.pixels[p]!=proposal.pixels[p]){CHECK(goto_study::mask[p]);++differences;}
            }
            CHECK(differences>0);changed+=differences;
            peak=std::max(peak,proposal.objects+upper.objects);
            command_peak=std::max(command_peak,proposal.objects+upper.objects-cache_spans+2);
            CHECK(command_peak<8192);
            CHECK(std::equal(cache_top.begin(),cache_top.end(),std::begin(sigil_surface::top_cache.pixels)));
            CHECK(std::equal(cache_bottom.begin(),cache_bottom.end(),std::begin(sigil_surface::bottom_cache.pixels)));
            char filename[40];std::snprintf(filename,sizeof filename,"frame-%04u.ppm",t/40);
            save(proposal,out/name/filename);
            if(t==0||t==1000||t==2000||t==6000||t==8000||t==14000) {
                save(baseline,out/name/("before-"+std::to_string(t)+".ppm"));
                save(upper,out/name/("upper-"+std::to_string(t)+".ppm"));
            }
            if(t==0){goto_study::phase=16000;CHECK(render(320,nav_draw).pixels==proposal.pixels);goto_study::phase=t;}
        }
        CHECK(fixture.engine->bpm==120&&fixture.engine->swing_percent==50&&mutations==0);
        receipt<<name<<'\t'<<400<<'\t'<<compared<<'\t'<<changed<<'\t'<<peak<<'\t'<<command_peak<<'\n';
        std::printf("%s: 400 paired comparisons; %llu native pixels compared, no changes outside ink; upper/cache/read-only draw exact; peak %d raw host objects / %d native-command estimate, not fps\n",name.c_str(),(unsigned long long)compared,peak,command_peak);
        nav_close();nav_transition_clear();
    }
}
}
int main(int argc,char** argv) {
    try{CHECK(argc==2);study_driver::run(argv[1]);return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL study: %s\n",e.what());return 1;}
}
'''


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def once(source, old, new):
    assert source.count(old) == 1, old[:120]
    return source.replace(old, new)


def execute(command, log):
    with log.open('x') as stream:
        stream.write('$ ' + ' '.join(map(str, command)) + '\n'); stream.flush()
        result = subprocess.run(list(map(str, command)), cwd=ROOT, stdout=stream,
                                stderr=subprocess.STDOUT, timeout=240)
    if result.returncode:
        raise RuntimeError(log.read_text()[-6000:])


def media(out, theme):
    folder = out/'capture'/theme
    frames = []
    # Preserve the native RGB evidence as lossless PNG, not bulky temporary PPM.
    for ppm in sorted(folder.glob('*.ppm')):
        with Image.open(ppm) as image:
            image.save(ppm.with_suffix('.png'))
        ppm.unlink()
    for i in range(400):
        frames.append(Image.open(folder/f'frame-{i:04d}.png').convert('RGB'))
    big = [im.resize((640, 480), Image.Resampling.NEAREST) for im in frames]
    apng = out/f'{theme}-living-ink-2x.png'
    big[0].save(apng, save_all=True, append_images=big[1:], duration=STEP, loop=0,
                disposal=0, blend=0)
    # Browser-friendly GIF has one global palette and no random/dither pixels.
    atlas = Image.new('RGB', (320*8, 240*5))
    for i, image in enumerate(frames[::10]):
        atlas.paste(image, ((i%8)*320, (i//8)*240))
    palette = atlas.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    indexed = [im.quantize(palette=palette, dither=Image.Dither.NONE) for im in big]
    gif = out/f'{theme}-living-ink-2x.gif'
    indexed[0].save(gif, save_all=True, append_images=indexed[1:], duration=STEP,
                    loop=0, optimize=False, disposal=1,
                    comment=b'private native GO TO study; actual startup shade; not shipping/device footage')
    worst = 0
    for path, expected in [(apng, big), (gif, indexed)]:
        with Image.open(path) as decoded:
            assert decoded.n_frames == 400
            for i, ref in enumerate(expected):
                decoded.seek(i)
                assert decoded.convert('RGB').tobytes() == ref.convert('RGB').tobytes()
                if path == gif:
                    worst = max(worst, max(v[1] for v in ImageChops.difference(decoded.convert('RGB'), big[i]).getextrema()))
    for t in SAMPLES:
        before = Image.open(folder/f'before-{t}.png').convert('RGB')
        upper = Image.open(folder/f'upper-{t}.png').convert('RGB')
        dual = Image.new('RGB', (400, 496), (18, 18, 21))
        dual.paste(upper, (0, 0)); dual.paste(frames[t//STEP], (40, 256))
        dual.resize((800, 992), Image.Resampling.NEAREST).save(out/f'{theme}-dual-{t}-2x.png')
        if t == 1000:
            board = Image.new('RGB', (648, 240), (18, 18, 21))
            board.paste(before, (0, 0)); board.paste(frames[t//STEP], (328, 0))
            board.resize((1296, 480), Image.Resampling.NEAREST).save(out/f'{theme}-before-after-2x.png')
    assert frames[0].tobytes() != frames[25].tobytes()
    return {'theme': theme, 'frames': 400, 'duration_ms': 16000,
            'lossless_apng_roundtrip': True, 'gif_roundtrip': True,
            'gif_max_channel_error': worst}


def main():
    out = ROOT/'ui_mockups/go_to_living_ink_study'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S_%fZ')
    out.mkdir(parents=True, exist_ok=False)
    before = protected_hashes()
    (out/'before.json').write_text(json.dumps(before, indent=2)+'\n')
    nav = (ROOT/'platform/3ds/ui_nav.cpp').read_text()
    startup = (ROOT/'platform/3ds/ui_start_menu.cpp').read_text()
    rows = startup[startup.index('struct GlyphRows {'):startup.index('constexpr bool bounded_ink_glyphs()')]
    # Preview helper is private and is not an exported production abstraction.
    helper = HELPER.replace('@GLYPH_ROWS@', rows)
    helper = helper.replace('ui::rect(', 'seance::ui::rect(')
    nav = once(nav, 'namespace seance::platform::ui3ds {', helper+'\nnamespace seance::platform::ui3ds {')
    nav = once(nav, '        m::heading(x + dx, y + dy, ITEMS[i].label, selected ? spot : ink);',
               '        goto_study::heading(i, x + dx, y + dy, ITEMS[i].label, selected, ink, spot);')
    nav = nav.replace('#include "../../', '#include "'+str(ROOT)+'/')
    (out/'private_nav.cpp').write_text(nav)
    (out/'driver.cpp').write_text(DRIVER)
    sources = [out/'driver.cpp']
    sources += [ROOT/('platform/3ds/'+p+'.cpp') for p in (
        'ui_picker', 'ui_chains', 'ui_workspace', 'menu_style', 'project_names',
        'ui_chains_top', 'ui_theme', 'ui_transition')]
    sources += [ROOT/'core/ui/draw.cpp', ROOT/'core/ui/font.cpp', *sorted((ROOT/'core/engine').glob('*.cpp'))]
    sources += [ROOT/('core/'+p) for p in ('synth/sampler.cpp', 'synth/wav_loader.cpp', 'synth/wav_writer.cpp')]
    command = [os.environ.get('CXX', 'g++'), '-std=gnu++17', '-O1', '-g1', '-Wall', '-Wextra',
               '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
               '-Wl,--wrap=_ZN6seance2ui4textEiiPKcji', '-MMD',
               *['-I'+str(ROOT/p) for p in ('tools/menu_stage1_test_stubs', 'tools/sequencer_test_stubs',
                                            'core', 'tools', 'platform/3ds')],
               *sources, '-o', out/'study']
    execute(command, out/'build.log')
    execute([out/'study', out/'capture'], out/'capture.log')
    results = [media(out, theme) for theme in ('graphite', 'pink')]
    assert protected_hashes() == before, 'production/assets/root artifacts changed'
    receipt = {
        'kind': 'private visual proposal; NOT production, ARM, deployment or device acceptance',
        'script_sha256': sha(__file__), 'protected_files_unchanged': len(before),
        'shade_source': {'path': 'platform/3ds/start_menu_ink.h', 'sha256': sha(ROOT/'platform/3ds/start_menu_ink.h')},
        'results': results, 'native_checks': (out/'capture/checks.tsv').read_text(),
        'scope': 'same frozen native popup, only existing eight destination glyph pixels change; exact startup shade with row phases repeated modulo 3; no pulse, no new timing/geometry',
        'preserved': '120ms/2px focus shift, underline/branches/current dot/header/footer/touch/clock/upper/blur; main startup and production untouched',
        'limits': 'host raster approximates GPU lines/cache; raw host pair includes software cache spans, native command estimate substitutes the actual two image quads (not hardware profiling); no full regressions, hardware fps, audio/listening or input acceptance; private phase is authored for this preview only',
        'files': {str(p.relative_to(out)): sha(p) for p in out.rglob('*') if p.is_file()},
    }
    (out/'proposal.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n')
    (out/'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8"><title>go to / living ink — study</title><style>body{background:#121215;color:#eee;font:15px/1.6 system-ui;margin:24px}main{max-width:1320px;margin:auto}.row{display:flex;gap:16px;flex-wrap:wrap}figure{margin:0}img{max-width:100%;image-rendering:pixelated}h1{font-size:20px;font-weight:500}p{max-width:850px;color:#bbb}</style><main><h1>go to / living ink</h1><p>study: the same shade and density within letters as the startup menu. existing focus movement, underlines and geometry are preserved. 16 seconds; not implemented or deployed. upper screen and bpm / swing unchanged.</p><div class="row"><figure><figcaption>graphite</figcaption><img src="graphite-living-ink-2x.png"></figure><figure><figcaption>pink</figcaption><img src="pink-living-ink-2x.png"></figure></div><p>current text on the left, proposal on the right:</p><img src="graphite-before-after-2x.png"><p>full view, upper screen unchanged:</p><img src="graphite-dual-1000-2x.png"></main></html>''')
    print(out)
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
