#pragma once
// Public API only. Shared authored shade math; independent bitmap admission,
// edge detection, color/alpha arithmetic and per-pixel raster (no span coalescer).
#include "../platform/3ds/start_menu_ink.h"
#include <array>
#include <limits>

namespace nav_ink_test {
namespace bg=seance::platform::ui3ds::nav_backdrop;
namespace sf=seance::platform::ui3ds::sigil_surface;
constexpr const char* names[]={"patch table","sigils","sigilbay","sequencer","perform","bounce","patches","exit"};
using Shifts=std::array<int,8>;
uint64_t pixel_checks=0, outside_checks=0, pcm_samples=0, frames=0;
int groups=0, peak_host_pair=0, peak_native_estimate=0;
std::ofstream trace;
const char* group="legacy";

uint64_t fingerprint(const std::vector<u32>& image) {
    uint64_t h=1469598103934665603ull;
    for(auto p:image)for(unsigned s=0;s<32;s+=8){h^=(p>>s)&255;h*=1099511628211ull;}
    return h;
}
// Intentionally no 8192 software-span assertion here. BasicCache's host path
// emits many CPU spans, while each native cache is exactly one image quad.
Frame host(int w,const std::function<void()>& draw) {
    width=w;pixels.assign(w*HEIGHT,0xff000000);texts.clear();raw_texts.clear();
    text_owners.assign(w*HEIGHT,0);text_collisions.assign(w*HEIGHT,false);next_text_id=0;
    objects=outside_rects=outside_lines=0;draw();
    CHECK(std::count(text_collisions.begin(),text_collisions.end(),true)==0);
    CHECK(objects>0&&outside_rects==0&&outside_lines==0);
    ++frames;
    Frame f{w,objects,0,pixels,texts,raw_texts};
    if(trace)trace<<group<<'\t'<<frames<<'\t'<<w<<'\t'<<objects<<'\t'<<fingerprint(f.pixels)<<'\n';
    return f;
}
Frame capture() {nav_capture();return host(320,nav_draw);}
NavChoice key(u32 k) {NavChoice choice;nav_buttons(k,choice);return choice;}
void open(u64 time=10000) {
    host_time_ms=time;g_view=View::Chains;g_overlay=Overlay::None;
    nav_open();nav_gpu_frame();CHECK(nav_is_open());
}
u32 mix(u32 a,u32 b,unsigned amount) {
    u32 out=0xff000000u;
    for(unsigned s=0;s<24;s+=8)out|=((((a>>s)&255)*(255-amount)+((b>>s)&255)*amount+127)/255)<<s;
    return out;
}
struct Colors {u32 ink,spot;};
Colors colors(int paper=0) {
    const u32 light=ui::rgb(244,244,244);u32 accent=m::spot();
    const unsigned luma=((accent&255)*54+((accent>>8)&255)*183+((accent>>16)&255)*19)/256;
    if(luma<150)accent=mix(accent,light,(170-luma)*255/(244-luma));
    return {mix(light,m::ink(),paper),mix(accent,m::spot(),paper)};
}
const menu_labels::Label& label(int i) {
    for(const auto& l:menu_labels::labels)if(!std::strcmp(l.text,names[i]))return l;
    throw std::runtime_error("missing native GO TO mask");
}
bool bit(const menu_labels::Label& l,int x,int y) {
    return x>=0&&x<l.width&&y>=0&&y<l.height&&
        (menu_labels::bitmap[l.offset+y*((l.width+7)/8)+x/8]&(0x80>>(x%8)));
}
u32 expected_pixel(int i,int x,int y,unsigned phase,bool selected,u32 under,int paper=0) {
    const auto& l=label(i);
    const bool edge=!bit(l,x-1,y)||!bit(l,x+1,y)||!bit(l,x,y-1)||!bit(l,x,y+1);
    cabinet_ink::Clock clock;clock.ms=phase%16000;
    const auto shade=cabinet_ink::heading(clock,i%3,x,y,edge,selected?255:0);
    const auto c=colors(paper);
    return mix(under,mix(c.ink,c.spot,shade.tint),shade.alpha);
}
Frame background(int paper=0) {
    return host(320,[&]{bg::draw(false);if(paper)m::paper_rect(0,0,320,240,paper);});
}
void label_at(const Frame& f,int i,int x,int y,unsigned phase,bool selected,int paper=0) {
    const auto under=background(paper);const auto& l=label(i);
    for(int yy=0;yy<l.height;++yy)for(int xx=0;xx<l.width;++xx)if(bit(l,xx,yy)) {
        ++pixel_checks;const auto at=(y+yy)*320+x+xx;
        const auto expected=(selected&&yy==25)?colors(paper).spot:
            expected_pixel(i,xx,yy,phase,selected,under.pixels[at],paper);
        if(f.pixels[at]!=expected)throw std::runtime_error(std::string("label pixel ")+names[i]+" phase="+std::to_string(phase)+" x="+std::to_string(xx)+" y="+std::to_string(yy));
    }
}
Shifts settled(int selected=0) {Shifts s{};if(selected>=0)s[selected]=2;return s;}
void all_labels(const Frame& f,unsigned phase,int selected=0,Shifts dx=settled(),int pressed=-1,int paper=0) {
    const auto under=background(paper);
    for(int i=0;i<8;++i) {
        const auto& l=label(i);const int bx=32+(i/4)*160,by=58+(i%4)*42;
        // Native patch table has26 rows; its final row is intentionally covered
        // by the unchanged full-width underline, drawn after the shaded mask.
        CHECK(l.height<=26);
        for(int y=by;y<=by+l.height;++y)for(int x=bx;x<bx+l.width+3;++x) {
            const int xx=x-bx-dx[i],yy=y-by-(i==pressed?1:0);
            const auto at=y*320+x;
            u32 wanted=bit(l,xx,yy)?expected_pixel(i,xx,yy,phase,i==selected,under.pixels[at],paper):under.pixels[at];
            if(i==selected&&yy==25&&xx>=0&&xx<l.width)wanted=colors(paper).spot;
            ++pixel_checks;
            if(f.pixels[at]!=wanted)throw std::runtime_error(std::string("mask/alpha ")+names[i]+" phase="+std::to_string(phase)+" x="+std::to_string(x)+" y="+std::to_string(y)+" expected="+std::to_string(wanted)+" actual="+std::to_string(f.pixels[at]));
        }
    }
}
std::vector<unsigned char> masks(Shifts dx=settled()) {
    std::vector<unsigned char> result(320*240);
    for(int i=0;i<8;++i){const auto& l=label(i);
        for(int y=0;y<l.height;++y)for(int x=0;x<l.width;++x)if(bit(l,x,y))
            result[(58+(i%4)*42+y)*320+32+(i/4)*160+dx[i]+x]=1;}
    return result;
}
void outside_equal(const Frame& a,const Frame& b,const std::vector<unsigned char>& allowed) {
    for(size_t p=0;p<a.pixels.size();++p)if(!allowed[p]){++outside_checks;CHECK(a.pixels[p]==b.pixels[p]);}
}
struct CacheCopy {
    std::vector<u32> top{std::begin(sf::top_cache.pixels),std::end(sf::top_cache.pixels)};
    std::vector<u32> bottom{std::begin(sf::bottom_cache.pixels),std::end(sf::bottom_cache.pixels)};
    bool tp=sf::top_cache.pending,bp=sf::bottom_cache.pending,tv=sf::top_cache.valid,bv=sf::bottom_cache.valid;
    void assert_same() const {
        CHECK(std::equal(top.begin(),top.end(),std::begin(sf::top_cache.pixels)));
        CHECK(std::equal(bottom.begin(),bottom.end(),std::begin(sf::bottom_cache.pixels)));
        CHECK(tp==sf::top_cache.pending&&bp==sf::bottom_cache.pending&&tv==sf::top_cache.valid&&bv==sf::bottom_cache.valid);
        CHECK(!bg::requested&&bg::frozen);
    }
};
void done(const char* description){++groups;std::printf("pass living-ink %s\n",description);}
void loop() {
    group="loop";Fixture f;open();const auto first=capture();all_labels(first,0);
    const auto admitted=masks();CacheCopy cache;auto previous=first;int changed=0;
    for(unsigned t=40;t<=16000;t+=40) {
        host_time_ms=10000+t;auto current=capture();all_labels(current,t);
        changed+=current.pixels!=previous.pixels;outside_equal(first,current,admitted);
        const auto upper=host(400,nav_clock_draw_top);const u64 time=host_time_ms;
        auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;host_time_ms+=987654;
        CHECK(host(320,nav_draw).pixels==current.pixels);
        CHECK(host(400,nav_clock_draw_top).pixels==upper.pixels);
        host_time_ms=time;g_eng=engine;g_audio=audio;cache.assert_same();previous=std::move(current);
    }
    CHECK(changed>390&&previous.pixels==first.pixels);
    CHECK(mutations==0&&f.engine->bpm==120&&f.engine->swing_percent==50);
    done("16s/40ms exact period; all eight masks, alpha holes and non-label pixels; frozen no-engine draws/cache");
}
void timing() {
    group="timing";Fixture f;open(999999);auto zero=capture();all_labels(zero,0);
    CHECK(capture().pixels==zero.pixels);
    unsigned phase=0;
    for(unsigned dt:{4u,4u,992u,1000u,1001u,1u,250000u,7u}) {
        host_time_ms+=dt;if(dt<=1000)phase=(phase+dt)%16000;all_labels(capture(),phase);
    }
    host_time_ms=1;all_labels(capture(),phase);host_time_ms=5;phase+=4;all_labels(capture(),phase);
    host_time_ms=std::numeric_limits<u64>::max()-10;all_labels(capture(),phase);
    host_time_ms+=4;phase+=4;all_labels(capture(),phase);
    host_time_ms=0;all_labels(capture(),phase);host_time_ms=4;phase+=4;all_labels(capture(),phase);
    open();for(int i=0;i<8;++i){host_time_ms+=1000;nav_capture();}host_time_ms+=731;
    const auto grouped=capture();all_labels(grouped,8731);
    open();for(int i=0;i<873;++i){host_time_ms+=10;nav_capture();}++host_time_ms;
    const auto fine=capture();all_labels(fine,8731);CHECK(fine.pixels==grouped.pixels);
    done("zero/4ms/grouped ticks; 1000ms accepted vs1001ms rebase; rollback/uint64 max/huge gaps without catch-up");
}
void focus_and_close() {
    group="focus";Fixture f;open();Shifts weights{};weights[0]=255;Shifts from=weights;
    int selected=0;unsigned start=0,phase=0;
    const u32 arrows[]={KEY_DOWN,KEY_DOWN,KEY_RIGHT,KEY_UP,KEY_LEFT,KEY_UP,KEY_DOWN,KEY_RIGHT};
    for(unsigned t=0;t<=400;t+=4) {
        if(t%36==0&&t/36<8) {
            CHECK(key(arrows[t/36])==NavChoice::None);from=weights;start=t;
            if(arrows[t/36]==KEY_DOWN)selected=(selected/4)*4+(selected%4+1)%4;
            else if(arrows[t/36]==KEY_UP)selected=(selected/4)*4+(selected%4+3)%4;
            else selected=(selected+4)%8;
        }
        const unsigned elapsed=std::min(120u,t-start);
        const int n=int(elapsed),weight=(255*n*n*(360-2*n)+864000)/1728000;
        Shifts dx{};
        for(int i=0;i<8;++i){weights[i]=(from[i]*(255-weight)+(i==selected?255:0)*weight+127)/255;dx[i]=(2*weights[i]+127)/255;}
        host_time_ms=10000+t;phase=t;all_labels(capture(),phase,selected,dx);
    }
    for(bool touch:{false,true}) {
        open();host_time_ms+=733;auto last=capture();all_labels(last,733);
        host_time_ms+=700;NavChoice result;
        if(touch){key(0);nav_touch_down(200,106,result);selected=5;}else{result=key(KEY_A);selected=0;}
        CHECK(result==(touch?NavChoice::Bounce:NavChoice::Chains)&&!nav_is_open());
        const auto closing=host(320,nav_draw_transition);all_labels(closing,733,selected,settled(),selected);
        CHECK(closing.pixels!=last.pixels);
        f.engine->set_bpm(144);f.engine->set_swing(57);host_time_ms+=5000;nav_capture();
        CHECK(host(320,nav_draw_transition).pixels==closing.pixels);
        auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;host_time_ms=1;
        CHECK(host(320,nav_draw_transition).pixels==closing.pixels);g_eng=engine;g_audio=audio;
        open();all_labels(capture(),0);CHECK(mutations==0);nav_transition_clear();
    }
    for(u32 cancel:{u32(KEY_B),u32(KEY_SELECT)}) {
        open();host_time_ms+=631;capture();CHECK(key(cancel)==NavChoice::None&&!nav_is_open());
        host_time_ms+=900;nav_capture();open(host_time_ms);all_labels(capture(),0);
    }
    nav_close();host_time_ms+=40;nav_capture();open(host_time_ms);all_labels(capture(),0);
    done("4ms interrupted focus stencil; immediate A/tap1px frozen phase, capture-after-close/clock mutation/no-engine; B/SELECT/reopen reset");
}
void pause_and_theme() {
    group="pause";Fixture f;open();host_time_ms+=317;all_labels(capture(),317);
    host_time_ms+=90;key(KEY_X);auto enter=capture();
    // Focus enters at existing 2px geometry, now idle shade; this interval is skipped.
    all_labels(enter,317,-1,settled());
    host_time_ms+=180;all_labels(capture(),317,-1,settled(-1),-1,255);
    key(KEY_X);host_time_ms+=700;all_labels(capture(),317,-1,settled(-1),-1,255);
    key(KEY_Y);host_time_ms+=800;auto detail=capture();CHECK(text_has(detail,"sigil"));
    host_time_ms+=200000;CHECK(capture().pixels==detail.pixels);
    key(KEY_B);host_time_ms+=63;all_labels(capture(),317,-1,settled(-1),-1,255);
    key(KEY_B);host_time_ms+=127;all_labels(capture(),317,0,settled(-1),-1,255);
    host_time_ms+=180;all_labels(capture(),497); // resume only subsequent visible interval
    host_time_ms+=4;all_labels(capture(),501);
    // Recolor while continuously visible: neither palette changes nor draw read
    // wall time. Cache belongs to the original screen and is not re-frozen.
    CacheCopy cache;const auto original=palette::settings();unsigned phase=501;
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite,palette::Theme::Grimoire,palette::Theme::BlackMass,palette::Theme::Custom}) {
        palette::set(theme);all_labels(host(320,nav_draw),phase);all_labels(capture(),phase);
        host_time_ms+=4;phase+=4;all_labels(capture(),phase);cache.assert_same();
    }
    palette::set(original);cache.assert_same();CHECK(mutations==0&&f.engine->bpm==120&&f.engine->swing_percent==50);
    done("clock/detail entry+exit intervals skipped; paused huge time; saved-phase resume; five theme recolors without restart");
}
std::vector<uint8_t> bgr(const Frame& f) {
    std::vector<uint8_t> out(f.width*240*3);
    for(int y=0;y<240;++y)for(int x=0;x<f.width;++x){auto c=f.pixels[y*f.width+x];auto* p=&out[(x*240+239-y)*3];p[0]=c>>16;p[1]=c>>8;p[2]=c;}
    return out;
}
void real_backdrop() {
    group="real-backdrop";Fixture f;f.four();chains_capture();host_time_ms=1000;chains_capture();
    auto top=bgr(host(400,chains_context_draw_top)),bottom=bgr(host(320,chains_view_draw));
    nav_open();bg::freeze(top.data(),bottom.data());CacheCopy cache;
    const int cache_spans=host(320,[]{bg::draw(false);}).objects+host(400,[]{bg::draw(true);}).objects;
    CHECK(cache_spans>2);
    for(unsigned t=0;t<=16000;t+=40) {
        host_time_ms=1000+t;const auto lower=capture(),upper=host(400,nav_clock_draw_top);all_labels(lower,t);
        const int raw=lower.objects+upper.objects,estimate=raw-cache_spans+2;
        peak_host_pair=std::max(peak_host_pair,raw);peak_native_estimate=std::max(peak_native_estimate,estimate);
        CHECK(estimate>0&&estimate<8192);cache.assert_same();
    }
    CHECK(mutations==0);done("real main dual backdrop; raw host cache spans reported separately from two-quad native estimate (not fps)");
}
std::vector<char> persistent(Engine& engine,const char* name) {
    CHECK(patchio::save(engine,name));std::ifstream in(name,std::ios::binary);CHECK(bool(in));
    return std::vector<char>(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
}
void pcm_and_state() {
    group="pcm";Fixture f;f.four();auto twin=std::make_unique<Engine>();
    const auto before=persistent(*f.engine,"ink-before.snc");CHECK(patchio::load(*twin,"ink-before.snc"));
    open();const int dirty=mutations;std::array<q15,1024> a{},b{};bool nonzero=false;
    for(int n=0;n<96;++n) {
        f.engine->render(a.data(),512);twin->render(b.data(),512);CHECK(a==b);
        for(auto v:a)nonzero|=v!=0;
        pcm_samples+=a.size();f.refresh();host_time_ms+=17;
        if(n%13==0)key(KEY_DOWN);
        capture();host(400,nav_clock_draw_top);
        if(n%19==0){key(KEY_X);capture();host_time_ms+=400;capture();key(KEY_B);capture();}
    }
    CHECK(nonzero&&mutations==dirty&&persistent(*f.engine,"ink-after.snc")==before);
    done("96 nonzero stereo512 PCM twins with/without UI; complete persisted patch bytes/dirty unchanged");
}
void run(const char* trace_path) {
    if(trace_path){trace.open(trace_path);CHECK(bool(trace));trace<<"group\tframe\twidth\thost_objects\tfnv1a64\n";}
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
        palette::set(theme);loop();timing();focus_and_close();pause_and_theme();real_backdrop();pcm_and_state();
    }
    std::printf("living-ink summary: groups=%d frames=%llu oracle_pixels=%llu unchanged_outside_pixels=%llu pcm_twin_samples=%llu peak_raw_host_pair=%d peak_native_two_quad_estimate=%d/8192\n",groups,(unsigned long long)frames,(unsigned long long)pixel_checks,(unsigned long long)outside_checks,(unsigned long long)pcm_samples,peak_host_pair,peak_native_estimate);
    if(trace)trace.close();
    palette::set(palette::Theme::Graphite);
}
} // namespace nav_ink_test
