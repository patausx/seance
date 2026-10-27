// Focused production GO TO/controller/engine tests; no exported images.
#include "../platform/3ds/nav_backdrop.h"
#include "../platform/3ds/nav_ferrofluid.h"
#define main inherited_menu_stage1_main
#include "menu_stage1_test.cpp"
#undef main
#include "nav_living_ink_test.h"

namespace popup_test {
namespace bg=seance::platform::ui3ds::nav_backdrop;
namespace sf=seance::platform::ui3ds::sigil_surface;
namespace ferro=seance::platform::ui3ds::nav_ferrofluid;
NavChoice key(u32 k) { NavChoice c; nav_buttons(k,c); return c; }
NavChoice tap(int x,int y) { key(0); NavChoice c; nav_touch_down(x,y,c); return c; }
void open() { nav_open(); nav_gpu_frame(); }
int peak_objects=0;
Frame bottom() { nav_capture(); auto f=render(320,nav_draw); raw_text_audit(f);peak_objects=std::max(peak_objects,f.objects);return f; }
void at(int index) {
    g_overlay=index==6?Overlay::PatchBrowser:Overlay::None;
    const View views[]={View::Chains,View::Sigils,View::Patchbay,View::Sequencer,View::Performance,View::Bounce,View::Chains,View::Chains};
    g_view=views[index];
}
void inputs_and_layout() {
    Fixture f;
    for(int i=0;i<8;++i) {
        at(i);open(); CHECK(nav_clock_top_active());
        CHECK(tap(40+(i/4)*160,64+(i%4)*42)==CHOICES[i]);
        CHECK(!nav_is_open()&&mutations==0);
    }
    for(int i=0;i<7;++i) for(u32 arrow:{u32(KEY_DRIGHT),u32(KEY_CPAD_LEFT),u32(KEY_DUP),u32(KEY_CPAD_DOWN)}) {
        at(i);open();CHECK(key(arrow)==NavChoice::None);
        const int expected=arrow==KEY_DRIGHT||arrow==KEY_CPAD_LEFT?(i+4)%8:
            (i/4)*4+(i%4+4+(arrow==KEY_DUP?-1:1))%4;
        CHECK(key(KEY_A)==CHOICES[expected]);
    }
    at(3);open();CHECK(key(KEY_LEFT|KEY_RIGHT|KEY_UP|KEY_DOWN)==NavChoice::None);
    CHECK(key(KEY_A)==NavChoice::Sequencer);
    for(auto p:{std::pair<int,int>{0,60},{160,65},{40,92},{200,218},{319,208},{40,45}}) {
        at(0);open(); CHECK(tap(p.first,p.second)==NavChoice::None&&nav_is_open());
    }
    at(0);open();key(KEY_DOWN);CHECK(tap(40,64)==NavChoice::Chains); // new edge after key(0)
    open();key(KEY_DOWN);NavChoice c;nav_touch_down(40,64,c);CHECK(c==NavChoice::None&&nav_is_open());
    CHECK(key(KEY_Y)==NavChoice::None&&nav_is_open());
    CHECK(tap(280,226)==NavChoice::None&&nav_is_open());
    CHECK(!text_has(bottom(),"y theme"));
    key(KEY_START|KEY_R);CHECK(nav_is_open());key(KEY_B);CHECK(!nav_is_open());
    open();key(KEY_SELECT);CHECK(!nav_is_open());

    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite,palette::Theme::Grimoire,palette::Theme::BlackMass,palette::Theme::Custom}) {
        palette::set(theme);at(3);open();auto a=bottom();
        CHECK(!text_has(a,"here")&&!text_has(a,"go to"));
        CHECK(text_has(a,"120 bpm")&&text_has(a,"swing 50%"));
        CHECK(text_has(a,"a open")&&text_has(a,"b / select close"));
        // Every destination must use the shared gothic mask, never the ASCII fallback.
        for(const char* label:{"patch table","sigils","sigilbay","sequencer","perform","bounce","patches","exit"})
            CHECK(!text_has(a,label));
        const menu_labels::Label* exit_label=nullptr;
        for(const auto& label:menu_labels::labels) if(!std::strcmp(label.text,"exit")) exit_label=&label;
        CHECK(exit_label&&exit_label->width==30&&exit_label->height==20);
        CHECK(m::heading_width("exit")==exit_label->width);
        nav_ink_test::label_at(a,7,192,184,0,false); // same mask/position; authored shade at open phase0
        const int mark_x=32+m::heading_width("sequencer")+6, mark_y=58+3*42+10;
        CHECK(a.pixels[mark_y*320+mark_x]==ui::rgb(184,180,192));
        key(KEY_RIGHT);auto b=bottom();
        CHECK(b.pixels[mark_y*320+mark_x]==a.pixels[mark_y*320+mark_x]);
        CHECK(a.pixels!=b.pixels); // focus changed, current-page dot did not
        auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
        CHECK(render(320,nav_draw).pixels==b.pixels);g_eng=engine;g_audio=audio;
        key(KEY_A);auto closing=render(320,nav_draw_transition);
        CHECK(closing.pixels!=b.pixels); // accepted item depresses on the frozen closing frame
        for(int y=0;y<240;++y)for(int x=0;x<320;++x)
            if(!(x>=192&&x<225&&y>=184&&y<212)) CHECK(closing.pixels[y*320+x]==b.pixels[y*320+x]);
        CHECK(render(320,nav_draw_transition).pixels==closing.pixels);
        nav_transition_clear();
    }
    palette::set(palette::Theme::Graphite);
    std::puts("pass popup input/layout: eight destinations, two-column dpad/touch, gothic labels incl exit, current dot independent of focus, startup-only theme, frozen handoff, modal ownership");
}
void clocks() {
    Fixture f;open();
    CHECK(tap(60,18)==NavChoice::None);nav_touch_hold(80,18);CHECK(f.engine->bpm==120&&mutations==0);
    CHECK(text_has(bottom(),"130 bpm"));nav_touch_release();CHECK(f.engine->bpm==130&&mutations==1);
    tap(60,18);nav_touch_release();CHECK(mutations==1);
    tap(220,18);nav_touch_hold(236,18);CHECK(f.engine->swing_percent==50);
    nav_touch_release();CHECK(f.engine->swing_percent==54&&mutations==2);
    key(KEY_B);CHECK(nav_is_open());key(KEY_X);key(KEY_RIGHT);CHECK(f.engine->bpm==131);
    key(KEY_X);key(KEY_UP);CHECK(f.engine->swing_percent==59);
    const int count=mutations;
    tap(60,18);nav_touch_hold(100,42);nav_touch_hold(100,18);nav_touch_release();CHECK(mutations==count);
    tap(60,18);nav_touch_hold(100,18);key(KEY_RIGHT);nav_touch_release();CHECK(mutations==count&&f.engine->bpm==131);
    tap(60,18);nav_touch_hold(100,18);f.engine->set_swing(60);nav_touch_release();CHECK(f.engine->bpm==131&&mutations==count);
    // Source/depth editor and upper applied clock remain functional.
    tap(60,18);nav_touch_release();key(KEY_Y);nav_capture();
    CHECK(text_has(render(320,nav_draw),"sigil"));CHECK(text_has(render(400,nav_clock_draw_top),"applied"));
    key(KEY_X);key(KEY_RIGHT);CHECK(f.engine->clock_mod(ClockTarget::Tempo).src==sigil_mod_source(0));
    key(KEY_X);key(KEY_UP);CHECK(f.engine->clock_mod(ClockTarget::Tempo).depth==5);
    key(KEY_B);key(KEY_B);bottom();CHECK(nav_is_open());
    // Route badge opens detail instead of starting a value draft.
    tap(145,18);nav_capture();CHECK(text_has(render(320,nav_draw),"sigil"));key(KEY_B);key(KEY_B);
    tap(60,18);nav_touch_hold(100,18);key(KEY_SELECT);nav_touch_release();CHECK(!nav_is_open()&&f.engine->bpm==131);
    // A menu choice made while clock focus is active still has a frozen source.
    open();key(KEY_X);nav_capture();tap(40,190);render(320,nav_draw_transition);
    std::puts("pass popup clock: separate header bands, draft-on-lift/noop/cancellation/context, dpad, source/depth/applied inspector, badge and focused handoff");
}
void feedback() {
    Fixture f;
    // Native masks translate by 0/1/2 whole pixels, never scale or smear.
    const auto label_at=[](const Frame& frame,int index,int x,int y,unsigned phase,bool selected) {
        nav_ink_test::label_at(frame,index,x,y,phase,selected);
    }; // independent per-pixel shade/alpha oracle, not a flat spot/ink assertion
    palette::set(palette::Theme::Graphite);at(0);open();
    label_at(bottom(),0,34,58,0,true);
    key(KEY_DOWN);auto begin=bottom();label_at(begin,1,32,100,0,true);
    host_time_ms+=60;auto mid=bottom();label_at(mid,1,33,100,60,true);
    label_at(mid,0,33,58,60,false);
    key(KEY_UP);auto reversed=bottom();label_at(reversed,0,33,58,60,true);
    label_at(reversed,1,33,100,60,false);
    host_time_ms+=120;label_at(bottom(),0,34,58,180,true);
    key(KEY_DOWN);bottom();host_time_ms+=120;auto settled=bottom();
    label_at(settled,1,34,100,300,true);
    CHECK(key(KEY_Y|KEY_ZL)==NavChoice::None);CHECK(bottom().pixels==settled.pixels);
    CHECK(key(KEY_A)==NavChoice::Sigils&&!nav_is_open()&&mutations==0);
    auto pressed=render(320,nav_draw_transition);label_at(pressed,1,34,101,300,true);
    host_time_ms+=1000;CHECK(render(320,nav_draw_transition).pixels==pressed.pixels);
    open();auto neutral=bottom();CHECK(tap(40,106)==NavChoice::Sigils&&!nav_is_open());
    label_at(render(320,nav_draw_transition),1,32,101,0,true);
    open();CHECK(bottom().pixels==neutral.pixels); // no old press/shift tail on reopen

    // At a blank native pixel, both LCDs must follow the same eased paper veil.
    // Include light paper: foreground must not snap to dark ink over dark blur.
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
        palette::set(theme);open();
        const auto before=bottom();
        constexpr int x=5,y=218;
        const u32 blurred=before.pixels[y*320+x],paper=m::paper_color(y);
        std::vector<u32> top_cache(std::begin(sf::top_cache.pixels),std::end(sf::top_cache.pixels));
        std::vector<u32> bottom_cache(std::begin(sf::bottom_cache.pixels),std::end(sf::bottom_cache.pixels));
        const auto sample=[&](int alpha) {
            const auto bot=bottom();const auto top=render(400,nav_clock_draw_top);
            const auto expected=sf::Cache::blend(blurred,paper,unsigned(alpha),255);
            CHECK(bot.pixels[y*320+x]==expected&&top.pixels[y*400+x]==expected);
            peak_objects=std::max(peak_objects,bot.objects+top.objects);CHECK(bot.objects+top.objects<8192);
            return bot;
        };
        key(KEY_X);sample(0); // same blur at entry, no first-frame opaque fill
        host_time_ms+=90;auto half=sample(128);
        // Repeated draw, changing wall clock and missing engine cannot advance visuals.
        auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
        const auto captured_top=render(400,nav_clock_draw_top);
        const auto saved_time=host_time_ms;host_time_ms+=99999;
        CHECK(render(320,nav_draw).pixels==half.pixels);
        CHECK(render(400,nav_clock_draw_top).pixels==captured_top.pixels);
        host_time_ms=saved_time;g_eng=engine;g_audio=audio;
        // Switching BPM->swing or opening source/depth doesn't restart the fade.
        key(KEY_X);sample(128);host_time_ms+=45;sample(215);
        key(KEY_Y);sample(215);host_time_ms+=45;sample(255);
        const auto detail=render(320,nav_draw);
        host_time_ms+=10000;CHECK(bottom().pixels==detail.pixels);
        key(KEY_B);sample(255); // detail -> focused header keeps opaque paper
        key(KEY_B);sample(255); // return starts from the visible editor paper
        host_time_ms+=90;sample(127);
        key(KEY_X);sample(127); // reverse midway without a jump
        host_time_ms+=90;sample(191);
        const auto saved=host_time_ms;host_time_ms=1;sample(191); // rollback never flashes
        host_time_ms=saved+10000;sample(255);
        key(KEY_B);sample(255);host_time_ms+=180;sample(0);
        CHECK(std::equal(top_cache.begin(),top_cache.end(),std::begin(sf::top_cache.pixels)));
        CHECK(std::equal(bottom_cache.begin(),bottom_cache.end(),std::begin(sf::bottom_cache.pixels)));
        CHECK(!bg::requested); // no extra LCD readback/filter/upload
        // A destination chosen during the fade retains its exact backdrop phase.
        key(KEY_X);sample(0);host_time_ms+=90;sample(128);
        CHECK(tap(200,106)==NavChoice::Bounce);
        CHECK(render(320,nav_draw_transition).pixels[y*320+x]==sf::Cache::blend(blurred,paper,128,255));
        open();CHECK(bottom().pixels[y*320+x]==blurred);
        // Real touch follows the same entry path and the existing draft-on-lift.
        const int old=f.engine->bpm,dirty=mutations;
        tap(60,18);sample(0);nav_touch_hold(64,18);CHECK(f.engine->bpm==old);
        nav_touch_release();CHECK(f.engine->bpm==old+2&&mutations==dirty+1);
    }
    palette::set(palette::Theme::Graphite);
    std::puts("pass popup feedback: native 2px/120ms focus, 1px A/tap closing press; dual 180ms blur-to-paper/reverse, interrupted/large/rollback time, frozen draw/cache, immediate clock/destination actions");
}
void backdrop() {
    palette::set(palette::Theme::Graphite);
    std::vector<uint8_t> top(400*240*3),bot(320*240*3);
    const auto fill=[](auto& buffer,int width) {
        for(int x=0;x<width;++x)for(int y=0;y<240;++y) {
            auto* p=buffer.data()+((239-y)+x*240)*3;
            p[0]=uint8_t(y);p[1]=uint8_t(x%160);p[2]=uint8_t(y/2);
        }
    };
    fill(top,400);fill(bot,320);bg::request();bg::freeze(top.data(),bot.data());
    CHECK(bg::frozen&&!bg::requested&&sf::top_cache.pending&&sf::bottom_cache.pending);
    for(int y=0;y<120;++y)for(int x=1;x<159;++x)
        CHECK(sf::top_cache.pixels[y*200+x]==sf::bottom_cache.pixels[y*160+x]);
    CHECK((sf::top_cache.pixels[110*200+10]&255)>(sf::top_cache.pixels[10*200+10]&255));
    const auto old=sf::top_cache.pixels[50*200+40];std::fill(top.begin(),top.end(),255);
    bg::gpu_frame();CHECK(sf::top_cache.pixels[50*200+40]==old); // no re-read each frame
    // Uniform source has an exact, matching double blur/veil result on both LCDs.
    std::fill(bot.begin(),bot.end(),255);bg::request();bg::freeze(top.data(),bot.data());
    auto expected=sf::Cache::blend(0xffffffff,m::paper_color(100),24,100);
    expected=sf::Cache::blend(expected,0xff0a0506u,68,100);
    CHECK(sf::top_cache.pixels[50*200+40]==expected&&sf::bottom_cache.pixels[50*160+40]==expected);
    bg::request();bg::gpu_frame();CHECK(bg::frozen); // no-buffer fallback, no texture allocation
    std::puts("pass popup backdrop: rotated BGR/2x2 orientation, same blur+68% veil on both LCDs, frozen ownership, fallback; GPU fence/transfer assertions in runner");
}
struct DC : Unit {
    q15 value;
    explicit DC(q15 v):value(v){}
    void process(q15* out,int frames) override { std::fill(out,out+frames,value); }
};
void master_and_capture() {
    Fixture f;auto* dc=new DC(22000);f.engine->chains[0].units[0]=dc;
    f.engine->chains[0].pan=32767;f.engine->chains[0].volume=32767;
    q15 pcm[512*2];f.engine->render(pcm,512);f.refresh();
    int peak=0;for(int i=0;i<512;++i){CHECK(pcm[2*i]==0);peak=std::max(peak,std::abs(int(pcm[2*i+1])));}
    CHECK(peak>10000&&g_ui_snapshot.master_peak==peak&&g_ui_snapshot.master_serial==512);
    // Reactive capture consumes published scalars, not chain peak or scope.
    open();f.engine->render(pcm,512);f.refresh();host_time_ms=60;nav_capture();
    auto top=render(400,nav_clock_draw_top);peak_objects=std::max(peak_objects,top.objects);
    auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;host_time_ms=99999;
    CHECK(render(400,nav_clock_draw_top).pixels==top.pixels);g_eng=engine;g_audio=audio;
    host_time_ms=400;nav_capture(); // stale publisher decays instead of repeating an old attack
    dc->value=0;for(int i=0;i<80;++i)f.engine->render(pcm,512);f.refresh();CHECK(g_ui_snapshot.master_peak==0);
    open();host_time_ms=450;nav_capture();render(400,nav_clock_draw_top);
    CHECK(sizeof(EngineUiSnapshot)<=3840);
    // Limiter saturation still publishes the actual stereo output, not pre-sum peak.
    dc->value=32767;for(int c=1;c<4;++c) {f.engine->chains[c].units[0]=new DC(32767);f.engine->chains[c].volume=32767;}
    f.engine->render(pcm,512);f.refresh();peak=0;for(q15 p:pcm)peak=std::max(peak,std::abs(int(p)));
    CHECK(peak<=30000&&g_ui_snapshot.master_peak<=30000&&g_ui_snapshot.master_peak>0);
    std::puts("pass popup master: real post-pan/post-limiter stereo peak, silence decay, audio progress, frozen draw/no engine access, bounded snapshot");
}
}
int main(int argc,char** argv) {
    try {
        test_nav_theme();
        popup_test::inputs_and_layout();popup_test::clocks();popup_test::feedback();popup_test::backdrop();popup_test::master_and_capture();
        std::printf("pass popup: 5 focused groups; peak host pair/component %d/8192 (flat software blur fallback, not hardware FPS)\n",popup_test::peak_objects);
        nav_ink_test::run(argc>1?argv[1]:nullptr);
        return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL popup: %s\n",e.what());return 1;}
}
