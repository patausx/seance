// BIRD v2: focused real-main input, latched model/frozen dual screens; no exports unless requested.
namespace {
struct BirdFixture : ModuleInspectorFixture {
    using B=BirdUnit;
    BirdFixture():ModuleInspectorFixture(UnitKind::Bird) {
        B defaults;
        for(int p=0;p<B::P_COUNT;++p){module->params[p]=defaults.params[p];module->params[p].snap();}
        bird_focus_release();bird_focus_select(0);capture();
    }
    B& unit(){return *static_cast<B*>(module);}
    std::array<q15,B::P_COUNT> bases() const {
        std::array<q15,B::P_COUNT> out;for(int p=0;p<B::P_COUNT;++p)out[p]=module->params[p].base;return out;
    }
    static bool bipolar(int p){return p==B::P_SWEEP || p==B::P_CONTOUR;}
    static int index(int p){constexpr int order[]={0,1,2,3,6,7,4,5};for(int i=0;i<8;++i)if(order[i]==p)return i;return -1;}
    static std::pair<int,int> point(int p){const int i=index(p);return {(i&1)?235:85,115+(i/2)*28};}
    void feed(int n){q15 b[128];while(n>0){const int block=std::min(n,128);unit().process(b,block);n-=block;}}
};
void test_bird_controls() {
    using B=BirdUnit;
    for(bool lock:{false,true})for(int p=0;p<B::P_COUNT;++p) {
        BirdFixture f;auto& u=f.unit();auto& v=u.params[p];v.base=BirdFixture::bipolar(p)?-7000:10000;v.snap();
        if(lock){g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,p,12000));}
        const auto original=f.bases();const q15 start=lock?12000:v.base;const int range=BirdFixture::bipolar(p)?65534:32767;
        const auto point=BirdFixture::point(p);const int x=point.first,y=point.second,dirty=mutations;
        input::down(x,y);CHECK(g_drag==Drag::BirdParam && bird_focus_selected()==p && f.bases()==original);
        input::move(x,y);CHECK(f.bases()==original && mutations==dirty);
        input::move(x+7,y);f.capture();const q15 expected=q15(start+7*range/128);
        if(lock)CHECK(f.bases()==original && seq_display_value(0,p,v.base)==expected);
        else {auto values=original;values[p]=expected;CHECK(f.bases()==values);}
        input::up();CHECK(g_drag==Drag::None);
        if(lock){const int i=BirdFixture::index(p);input::down((i&1)?166:16,y-15);input::up();CHECK(f.bases()==original && seq_display_value(0,p,v.base)==v.base);}
        // Saturate using more than the full 128px relative range from inside each field.
        input::down((BirdFixture::index(p)&1)?160:10,y);input::move(319,y);input::up();CHECK(seq_display_value(0,p,v.base)==32767);
        input::down((BirdFixture::index(p)&1)?300:150,y);input::move(0,y);input::up();CHECK(seq_display_value(0,p,v.base)==(BirdFixture::bipolar(p)?-32767:0));
    }
    BirdFixture f;auto& u=f.unit();
    input::down(207,100);input::up();CHECK(u.params[B::P_SWEEP].base==0);
    u.params[B::P_CONTOUR].base=9000;input::down(65,156);input::up();CHECK(u.params[B::P_CONTOUR].base==0);
    const auto before=f.bases();input::down(140,64);input::move(140,64);CHECK(f.bases()==before);
    input::move(155,64);input::up();CHECK(u.params[B::P_CONTOUR].base==15*65534/284 && u.params[B::P_SWEEP].base==before[B::P_SWEEP]);
    input::down(276,10);input::up();CHECK(u.bypass);input::down(276,10);input::up();CHECK(!u.bypass);
    // Context invalidation: base and contour gestures, lock step and bank identity.
    for(int why=0;why<4;++why) {
        input::down(85,115);const auto saved=f.bases();
        if(why==0)g_edit_slot=1;
        if(why==1)g_overlay=Overlay::None;
        if(why==2){g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=2;}
        if(why==3)g_edit_chain=1;
        f.capture();g_edit_chain=g_edit_slot=0;g_overlay=Overlay::Editor;g_seq_lock_armed=false;g_view=View::Chains;
        input::move(100,115);input::up();CHECK(f.bases()==saved);
    }
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=9;
    for(int p=0;p<SEQ_LOCKS;++p)CHECK(f.engine->set_step_lock(0,9,1,p,3000));
    const auto saved=f.bases();input::down(140,64);input::move(170,64);input::up();CHECK(f.bases()==saved && g_seq_lock_flash>0);
    input::down(276,10);input::up();CHECK(!u.bypass);
    const int dirty=mutations;g_seq_lock_chain=1;input::down(65,156);input::up();CHECK(mutations==dirty);
    g_seq_lock_chain=0;g_seq_lock_step=8;input::down(140,64);f.engine->active_pattern=1;
    input::move(160,64);input::up();CHECK(f.bases()==saved && mutations==dirty);f.engine->active_pattern=0;
}
void test_bird_mod_and_readouts() {
    using B=BirdUnit;
    BirdFixture f;auto& u=f.unit();
    for(int p:{B::P_PITCH,B::P_CONTOUR,B::P_SYLLABLE}) {
        bird_focus_select(p);input::frame(0,0);input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());f.capture();
        const q15 before=u.params[p].base;input::down(220,129);input::move(227,129);input::up();CHECK(u.params[p].base!=before);
        input::down(200,155);input::move(207,155);input::up();CHECK(u.params[p].mod_depth!=0);
        input::down(200,189);input::up();CHECK(u.params[p].src!=ModSource::None);
        f.capture();const auto top=cleanup_draw(true);const int count=objects;const auto lower=cleanup_draw(false);cleanup_pair_budget(count);
        {ModuleInspectorNoEngine frozen;CHECK(cleanup_draw(true)==top && cleanup_draw(false)==lower);}
        input::frame(0,0,KEY_L);CHECK(!param_lens_is_open() && !f.engine->param_temp_active());
    }
    input::frame(0,0);input::down(250,232);input::up();CHECK(param_lens_is_open());
    input::frame(KEY_B,KEY_B);CHECK(!param_lens_is_open());input::frame(0,0);
    const char* endpoints[B::P_COUNT][2]={{"500hz","6000hz"},{"-24.00st","+24.00st"},{"40ms","1600ms"},{"0.0hz","40.0hz"},{"0%","100%"},{"0%","100%"},{"-100%","+100%"},{"0%","100%"}};
    char text[24];for(int p=0;p<B::P_COUNT;++p)for(int end=0;end<2;++end) {
        module_inspector_value_text(text,sizeof text,UnitKind::Bird,p,end?32767:BirdFixture::bipolar(p)?-32767:0);CHECK(std::string(text)==endpoints[p][end]);
    }
    module_inspector_value_text(text,sizeof text,UnitKind::Bird,B::P_COUNT,0);CHECK(std::string(text)=="--");
    // Exact main A -> ordinary immediate engine strike, not a visual animation trigger.
    for(int c=1;c<NUM_CHAINS;++c)for(int s=0;s<UNITS_PER_CHAIN;++s)delete f.engine->replace_unit(c,s,nullptr);
    f.engine->launch_quantize=LaunchQuantize::Immediate;const auto before=f.bases();
    q15 output[2048]={};f.engine->render(output,1024);CHECK(!u.visual().active);
    input::trigger_frame(KEY_A,KEY_A);f.engine->render(output,1024);
    CHECK(u.visual().active && std::any_of(std::begin(output),std::end(output),[](q15 v){return std::abs(int(v))>100;}));
    CHECK(f.bases()==before);
}
void test_bird_model_frames() {
    using B=BirdUnit;
    BirdFixture f;auto& u=f.unit();
    for(int p=0;p<B::P_COUNT;++p) {
        u.params[p].base=BirdFixture::bipolar(p)?-18000:22000;
        u.params[p].src=ModSource(int(ModSource::SigilA)+p%4);u.params[p].mod_depth=p%2?-6000:7000;u.params[p].snap();u.params[p].resolve(9000);
    }
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}) {
        palette::set(theme);
        for(int p:{B::P_SWEEP,B::P_CONTOUR,B::P_SYLLABLE}) {
            bird_focus_select(p);f.capture();const auto top=cleanup_draw(true);const int count=objects;CHECK(has_text("next call") && has_text("shape / not pcm"));
            const auto lower=cleanup_draw(false);cleanup_pair_budget(count);
            // All eight native rails stay continuous through opaque modulation
            // bands. Only their intentional stored handle/zero tick covers them.
            for(int rp=0;rp<B::P_COUNT;++rp) {
                const int ri=BirdFixture::index(rp),ry=115+ri/2*28,rx=(ri&1)?174:24;
                const bool rb=BirdFixture::bipolar(rp);
                const int handle=rx+(int(u.params[rp].base)+(rb?32767:0))*128/(rb?65534:32767);
                for(int xx=rx;xx<=rx+128;++xx) {
                    if(std::abs(xx-handle)<=3)continue;
                    CHECK(pixels[ry*320+xx]==(rb&&xx==rx+64?m::ghost():m::branch()));
                }
            }
            const int i=BirdFixture::index(p),y=115+i/2*28,rx=(i&1)?174:24;const bool bi=BirdFixture::bipolar(p);
            const int bx=rx+(int(u.params[p].base)+(bi?32767:0))*128/(bi?65534:32767);
            const int ex=rx+(int(u.params[p].effective)+(bi?32767:0))*128/(bi?65534:32767);
            CHECK(pixels[(y-3)*320+bx]==m::spot() && pixels[(y+5)*320+ex]==m::spot());
            const auto saved=f.bases();const int dirty=mutations;
            {ModuleInspectorNoEngine frozen;
                module_inspector_surface(false);bird_focus_draw(u);cleanup_geometry();CHECK(pixels==lower);
                module_inspector_surface(true);CHECK(bird_focus_draw_top());cleanup_geometry();CHECK(pixels==top);
            }
            CHECK(f.bases()==saved && mutations==dirty);
        }
    }
    // The actual audio clock moves the cursor; next-call edits don't rewrite its curve.
    bird_focus_select(B::P_CONTOUR);u.trigger();f.feed(1000);f.capture();const auto active=cleanup_draw(true);CHECK(has_text("call / latched"));
    const auto visual=u.visual();u.params[B::P_CONTOUR].base=u.params[B::P_CONTOUR].effective=32767;f.capture();const auto edited=cleanup_draw(true);
    for(int y=125;y<=210;++y)for(int x=16;x<384;++x)CHECK(active[y*400+x]==edited[y*400+x]);
    CHECK(u.visual().age==visual.age && u.visual().latched[B::P_CONTOUR]==visual.latched[B::P_CONTOUR]);
    f.feed(1000);f.capture();const auto advanced=cleanup_draw(true);CHECK(advanced!=edited && u.visual().age==visual.age+1000);
    u.bypass=true;f.capture();cleanup_draw(true);CHECK(has_text("paused / bypass"));u.bypass=false;
    f.feed(int(visual.duration));f.capture();cleanup_draw(true);CHECK(has_text("next call") && !u.visual().active);
    g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    CHECK(f.engine->set_step_lock(0,7,0,B::P_CONTOUR,-32767));f.capture();cleanup_draw(false);CHECK(has_text("stored lock"));
    theme_test::select();
}
// Kept as an explicit opt-in export command; never called by the focused checks.
void dump_bird(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);std::ofstream manifest(dir/"manifest.tsv");manifest<<"frame\ttop\tbottom\tshared\tclipped\toverlaps\n";
    BirdFixture f;f.capture();cleanup_dump_pair(dir,manifest,"01_bird_whistle");
    auto& u=f.unit();u.params[BirdUnit::P_CONTOUR].base=-24000;u.params[BirdUnit::P_SYLLABLE].base=22000;u.params[BirdUnit::P_TRILL].base=15000;
    u.params[BirdUnit::P_TIME].base=23000;for(auto& v:u.params)v.snap();u.trigger();f.feed(3000);bird_focus_select(BirdUnit::P_CONTOUR);f.capture();
    cleanup_dump_pair(dir,manifest,"02_bird_articulated");
    u.params[BirdUnit::P_CONTOUR].base=32767;u.params[BirdUnit::P_SYLLABLE].base=0;
    for(auto& v:u.params)v.snap();
    u.trigger();f.feed(3000);f.capture();
    cleanup_dump_pair(dir,manifest,"03_bird_hook");
}
int run_bird_ui(int argc,char** argv) {
    int failed=0,total=0;
    const auto run=[&](const char* name,auto test){++total;try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("bird all eight base/lock controls / contour gesture / ownership",test_bird_controls);
    run("bird modulation / physical values / exact main A to real voice",test_bird_mod_and_readouts);
    run("bird frozen duals / palettes / markers / real latched model and cursor",test_bird_model_frames);
    if(argc==3 && std::string(argv[1])=="--dump-bird")run("bird requested dual captures",[&]{dump_bird(argv[2]);});
    std::printf("bird ui: %d/%d passed, host shared peak %d/8192 (not hardware fps)\n",total-failed,total,maximum_shared_objects);return failed?1:0;
}
} // namespace
