// Current REV projection proof. Real C++ surfaces; no duplicate renderer/audio.
// Supersedes the historical center-only three-return pixel contract.
using RevValues=std::array<q15,5>;
constexpr RevValues REV_CTOR={18000,20000,9000,9000,0};
RevValues rev_values(const ReverbUnit& u){RevValues v{};for(int p=0;p<5;++p)v[p]=u.params[p].base;return v;}
void rev_visual_reset(RevFixture& f){
    param_lens_cancel();g_view=View::Chains;g_overlay=Overlay::Editor;g_edit_chain=g_edit_slot=0;
    g_seq_lock_armed=false;g_seq_lock_step=g_seq_lock_chain=g_seq_lock_flash=0;f.engine->clear_track(0);
    auto& u=*f.rev;u.bypass=false;f.engine->chains[0].mute=false;
    for(int p=0;p<5;++p){u.params[p].base=REV_CTOR[p];u.params[p].src=ModSource::None;u.params[p].mod_depth=0;u.params[p].snap();}
    u.reset();reverb_focus_release();reverb_focus_input(0,0,false);f.tap(50,40);
}
void rev_visual_step(ReverbUnit& u,int frames){
    std::array<q15,64> mono{};
    while(frames){const int n=std::min(frames,64);mono.fill(0);u.process(mono.data(),n);frames-=n;}
}
void rev_visual_feed(ReverbUnit& u,int frames){
    std::array<q15,64> mono{};unsigned random=91;
    for(int at=0;at<frames;){const int n=std::min(frames-at,64);
        for(int i=0;i<n;++i){random=random*1664525u+1013904223u;mono[i]=q15(int(random>>18)-8192);}
        u.process(mono.data(),n);at+=n;
    }
}
struct RevDetached {
    Engine* e=g_eng;platform::AudioOut* a=g_audio;
    RevDetached(){g_eng=nullptr;g_audio=nullptr;}~RevDetached(){g_eng=e;g_audio=a;}
};
void rev_visual_capture(RevFixture& f,bool lens=false){
    const auto calls=rev_process_calls;f.engine->capture_ui_snapshot(g_ui_snapshot,0,0);
    if(lens)param_lens_capture();else reverb_focus_capture();CHECK(rev_process_calls==calls);
}
void rev_visual_surface(ReverbUnit& u,bool top,bool lens=false){
    width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);
    const auto calls=rev_process_calls;RevDetached no;
    if(top){if(lens)CHECK(param_lens_draw_top());else CHECK(reverb_focus_draw_top());}
    else {if(lens)param_lens_draw_bottom();else reverb_focus_draw(u);}
    if(outside_rects||outside_lines||text_overlaps)std::fprintf(stderr,"rev surface %s: rect=%d line=%d overlap=%d\n",top?"top":"bottom",outside_rects,outside_lines,text_overlaps);
    CHECK(rev_process_calls==calls && objects>0 && objects<8192);
    CHECK(outside_rects==0 && outside_lines==0 && text_overlaps==0);
    for(const auto& t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);
    rev_menu_palette_and_paper();
    if(top)maximum_top_objects=std::max(maximum_top_objects,objects);else maximum_objects=std::max(maximum_objects,objects);
}
void rev_visual_budget(int top){CHECK(top+objects<=8192);maximum_shared_objects=std::max(maximum_shared_objects,top+objects);}
void test_rev_visual_model(){
    namespace r=rev_tail_visual;
    int count=0,previous=0;
    for(int raw=0;raw<=32767;++raw){const int value=r::strength(q15(raw));
        CHECK(value==int(std::floor(std::sqrt(raw*65025.L/32767.L))) && value>=previous);previous=value;
    }
    for(bool modern:{false,true})for(int size:{-32768,0,4000,18000,32767})
    for(int decay:{0,20000,32767})for(int damp:{0,32767})for(int energy:{0,1,8192,32767})
    for(int freeze:{0,8192,32767}) {
        r::Plot plot;r::build(q15(size),q15(decay),q15(damp),q15(energy),modern,false,false,freeze>0,q15(freeze),plot);
        CHECK(plot.count>0 && plot.count<r::MAX_STROKES);
        for(int i=0;i<plot.count;++i){const auto& s=plot.strokes[i];CHECK(s.y0<=95 && s.y1<=95 && s.ink<=r::Accent && (s.x0!=s.x1 || s.y0!=s.y1));if(!energy)CHECK(s.ink==r::Branch);}
        r::Plot other;r::build(q15(size),q15(decay),q15(damp),q15(energy),modern,false,false,freeze>0,q15(freeze),other);
        CHECK(!std::memcmp(&plot,&other,sizeof plot));++count;
    }
    for(bool requested:{false,true})for(bool effective:{false,true})for(int mix:{0,1,8192,32766,32767}){
        const auto expected=mix>0?(effective?(mix==32767?r::State::Held:r::State::Entering):r::State::Leaving):
            effective?r::State::Effective:requested?r::State::Requested:r::State::Running;
        CHECK(r::freeze_state(requested,effective,q15(mix))==expected);
    }
    r::Plot low,high;r::build(18000,0,9000,2000,true,false,true,true,32767,low);
    r::build(18000,32767,9000,2000,true,false,true,true,32767,high);CHECK(!std::memcmp(&low,&high,sizeof low));
    char text[24];reverb_focus_value_text(text,sizeof text,1,0,true);CHECK(std::string(text)=="0.20s");
    reverb_focus_value_text(text,sizeof text,1,32767,true);CHECK(std::string(text)=="8.00s");
    reverb_focus_value_text(text,sizeof text,1,32767,false);CHECK(std::string(text)=="91.5%");
    CHECK(rev_readout::milliseconds(rev_readout::legacy_length(32767,1))==75);
    std::printf("rev projection: %d bounded deterministic settings / 32768 peak values / all freeze states; no private DSP\n",count);
}
void test_rev_visual_passive_freeze(){
    RevFixture f;auto& u=*f.rev;rev_visual_reset(f);const auto bases=rev_values(u);const auto pattern=f.hash();const int dirty=mutations;int points=0;
    for(int y=58;y<=183;++y)for(int x:{0,7,8,14,75,76,155,156,165,166,191,192,233,302,313,314,319}){
        f.down(x,y);f.drag(319,198);CHECK(g_drag==Drag::None&&rev_values(u)==bases&&f.hash()==pattern&&mutations==dirty);++points;
    }
    for(int state=0;state<5;++state){rev_visual_reset(f);rev_visual_step(u,1);
        if(state==0)u.params[4].base=32767;
        if(state==1)u.params[4].effective=32767;
        if(state>=2){u.params[4].base=32767;u.params[4].snap();rev_visual_step(u,state==2?16:64);}
        if(state==4){u.params[4].base=0;u.params[4].snap();rev_visual_step(u,16);}
        for(auto z:{std::array<int,3>{0,50,40},{1,210,40},{3,50,198}}){const q15 before=u.params[z[0]].base;f.down(z[1],z[2]);f.drag(z[1]+24,z[2]);CHECK(u.params[z[0]].base==before);}
        const q15 mix=u.params[2].base;f.down(210,198);f.drag(234,198);CHECK(u.params[2].base==mix+3276);reverb_focus_release();
    }
    std::printf("rev passive: %d center points / five requested-effective-transition gesture states\n",points);
}
void test_rev_visual_frozen(){
    RevFixture f;auto& u=*f.rev;int pairs=0;
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite})for(bool modern:{false,true})
    for(int size:{0,18000,32767})for(int state=0;state<6;++state) {
        palette::set(theme);CHECK(u.set_modern(modern));rev_visual_reset(f);
        u.params[0].base=q15(size);u.params[0].snap();rev_visual_step(u,1);
        if(state==1)u.params[4].base=32767;
        if(state==2)u.params[4].effective=32767;
        if(state>=3){u.params[4].base=32767;u.params[4].snap();rev_visual_step(u,state==3?16:64);}
        if(state==5){u.params[4].base=0;u.params[4].snap();rev_visual_step(u,16);}
        rev_visual_capture(f);rev_visual_surface(u,true);const int top=objects;const auto upper=pixels;
        CHECK(has_text(modern?"dense reverb":"legacy reverb"));
        rev_visual_surface(u,false);const auto lower=pixels;rev_visual_budget(top);
        const char* expected[]={"quiet","requested","pending","freezing","held / empty","releasing"};CHECK(has_text(expected[state]));
        for(auto& p:u.params){p.base=123;p.snap();}u.reset();u.bypass=true;g_ui_snapshot={};g_edit_chain=3;g_edit_slot=5;g_view=View::Sigils;g_overlay=Overlay::None;g_seq_lock_armed=true;g_seq_lock_flash=100;
        ReverbUnit unrelated;rev_visual_surface(unrelated,true);CHECK(pixels==upper);rev_visual_surface(unrelated,false);CHECK(pixels==lower);rev_visual_budget(top);++pairs;
    }
    CHECK(u.set_modern(true));rev_visual_reset(f);rev_visual_step(u,1);rev_visual_capture(f);rev_visual_surface(u,false);const auto quiet=pixels;
    host_time_ms+=100000;rev_visual_capture(f);rev_visual_surface(u,false);CHECK(pixels==quiet);
    rev_visual_feed(u,8192);CHECK(u.visual_energy()>0);rev_visual_capture(f);rev_visual_surface(u,false);CHECK(pixels!=quiet);
    const auto active=pixels;host_time_ms+=100000;rev_visual_capture(f);rev_visual_surface(u,false);CHECK(pixels==active);
    u.bypass=true;rev_visual_capture(f);rev_visual_surface(u,false);CHECK(has_text("bypassed"));
    u.bypass=false;f.engine->chains[0].mute=true;rev_visual_capture(f);rev_visual_surface(u,false);CHECK(has_text("chain muted"));
    theme_test::select();std::printf("rev frozen: %d native duals / real-energy activity / no wall-clock animation / detached draw / zero DSP\n",pairs);
}
void dump_rev_visual(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tmodern\tenergy\tfreeze\ttop_objects\tbottom_objects\n";
    RevFixture f;auto& u=*f.rev;palette::set(palette::Theme::Graphite);
    const auto save=[&](const char* name){rev_visual_capture(f);rev_visual_surface(u,true);const int top=objects;dump_surface(directory,std::string(name)+"_top");
        rev_visual_surface(u,false);dump_surface(directory,std::string(name)+"_bottom");rev_visual_budget(top);
        manifest<<name<<'\t'<<u.modern()<<'\t'<<u.visual_energy()<<'\t'<<u.visual_freeze()<<'\t'<<top<<'\t'<<objects<<'\n';};
    rev_visual_reset(f);rev_visual_step(u,1);save("01_quiet");
    rev_visual_feed(u,16000);f.tap(210,40);save("02_dense_tail");
    u.params[0].base=0;u.params[1].base=4000;for(auto& p:u.params)p.snap();rev_visual_feed(u,8192);f.tap(50,40);save("03_small_space");
    u.params[0].base=u.params[1].base=32767;u.params[3].base=24000;for(auto& p:u.params)p.snap();rev_visual_feed(u,16000);f.tap(50,198);save("04_dark_long");
    f.tap(150,10);u.params[4].snap();rev_visual_step(u,800);save("05_held");
    f.tap(150,10);u.params[4].snap();rev_visual_step(u,16);save("06_releasing");
    rev_visual_reset(f);CHECK(u.set_modern(false));rev_visual_feed(u,16000);f.tap(210,40);save("07_legacy");
    CHECK(u.set_modern(true));rev_visual_reset(f);u.bypass=true;save("08_bypassed");
    theme_test::select();std::printf("rev captures: eight fresh actual native C++ duals; host raster, not hardware\n");
}
// Historical CLI now emits the current projection cases, not old circular edges.
void dump_rev_edges(const std::filesystem::path& directory){dump_rev_visual(directory);}
