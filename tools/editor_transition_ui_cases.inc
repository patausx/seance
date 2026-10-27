// New focused groups only. Boundary imports do not run their inherited tests.
#include <filesystem>
#include <fstream>
#include <iomanip>
namespace transition_cases {
namespace et=seance::platform::ui3ds::editor_transition;
namespace cc=seance::platform::ui3ds::chain_columns;
namespace hid=osc_input_main;
int duals=0,peak=0,direct_peak=0,geometry_samples=0,release_calls=0;
std::string where;
struct Pair{std::vector<u32> top,bottom;int upper=0,lower=0;};
void gpu_frame(){editor_gpu::frame=true;menu_style::ambient_frame(host_time_ms);editor_style::frame();editor_gpu::frame=false;}
void capture(){
    if(g_eng)g_eng->capture_ui_snapshot(g_ui_snapshot,g_edit_chain,g_edit_slot);
    param_lens_capture();fm_context_capture(fm_focus_selected());fm_focus_capture();stranular_focus_capture();play_focus_capture();
    picker_capture();chains_editor_capture();chains_capture();editor_style::capture();gpu_frame();
}
struct Scene:Fixture{
    Scene(){
        nav_close();for(auto&c:engine->chains)for(auto*&u:c.units){delete u;u=nullptr;}
        g_view=View::Chains;g_overlay=Overlay::None;g_sel_chain=g_edit_chain=g_sel_slot=g_edit_slot=0;
        g_drag=Drag::None;g_last_px=-1;g_smooth_active=false;engine->transport_stop();workspace_reset_all();
        chains_editor_capture();chains_close_context_branch();capture();host_time_ms+=300;capture();
        u32 d=0,h=0,u=0;chains_editor_input(d,h,u);hid::up();release_calls=0;
    }
    ~Scene(){g_eng=engine.get();g_audio=&audio;nav_close();g_view=View::Chains;g_overlay=Overlay::None;g_seq_lock_armed=false;param_lens_cancel();chains_editor_capture();chains_close_context_branch();}
    Unit* add(int slot,UnitKind kind=UnitKind::Filter,int chain=0){auto*u=make_unit(kind);CHECK(u);delete engine->replace_unit(chain,slot,u);return u;}
    void settle(int slot=0){g_sel_slot=g_edit_slot=slot;capture();host_time_ms+=300;capture();}
    void open(int slot=0){settle(slot);chains_workspace_buttons(KEY_X);CHECK(g_overlay==Overlay::Editor&&et::state.active);capture();}
};
void geometry(){CHECK(!outside_rects&&!outside_lines);for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=240);}
Pair draw(bool detached=true){
    Pair out;const int dirty=mutations;auto*engine=g_eng;auto*audio=g_audio;const auto now=host_time_ms;
    const auto before=et::state;const auto uploads=editor_gpu::flushes,inits=editor_gpu::inits;
    const auto one=[&](bool top){module_inspector_surface(top);
        if(g_overlay==Overlay::Editor){if(top)top_draw();else editor_draw();chains_editor_draw(top);}
        else chains_draw_surface(top);
        geometry();};
    one(true);out.top=pixels;out.upper=objects;one(false);out.bottom=pixels;out.lower=objects;
    direct_peak=std::max(direct_peak,out.upper+out.lower);++duals;
    // Exact native main optimization: skip the covered editor only while the
    // compositor is wholly opaque. Its image must equal draw-then-cover above.
    int optimized=0;for(bool top:{true,false}){module_inspector_surface(top);
        if(g_overlay==Overlay::Editor){if(!chains_editor_covers()){if(top)top_draw();else editor_draw();}chains_editor_draw(top);}
        else chains_draw_surface(top);
        geometry();CHECK(pixels==(top?out.top:out.bottom));optimized+=objects;
    }peak=std::max(peak,optimized);
    if(detached){
        // Replay ONLY the new compositor onto exact ordinary-editor pixels. Its
        // DTO must survive missing engine/audio and a radically changed clock.
        for(bool top:{true,false}){
            module_inspector_surface(top);
            if(g_overlay==Overlay::Editor){if(top)top_draw();else editor_draw();}
            const auto under=pixels;
            chains_editor_draw(top);const auto expected=pixels;const int cost=objects;
            module_inspector_surface(top);pixels=under;g_eng=nullptr;g_audio=nullptr;host_time_ms+=999999;
            chains_editor_draw(top);geometry();CHECK(pixels==expected);(void)cost;
            g_eng=engine;g_audio=audio;host_time_ms=now;
        }
    }
    CHECK(et::state.progress==before.progress&&et::state.began==before.began&&et::state.closing==before.closing);
    CHECK(editor_gpu::flushes==uploads&&editor_gpu::inits==inits&&mutations==dirty);return out;
}
void touch(int x,int y,u32 down,u32 held){hid::touch={x,y};u32 up=held&KEY_TOUCH?0u:u32(KEY_TOUCH);const bool skip=chains_editor_input(down,held,up);if(!skip)hid::handle_touch(down,held);capture();}
void down(int x,int y){touch(x,y,KEY_TOUCH,KEY_TOUCH);}
void move(int x,int y){touch(x,y,0,KEY_TOUCH);}
void lift(){touch(hid::touch.px,hid::touch.py,0,0);}
void tap(int x,int y){down(x,y);lift();}
// Only unrelated project-menu bookkeeping is a boundary; the actual B branch,
// editor_back facade, lens/PLAY/COMP controllers and chains transition are real.
struct SessionBoundary{bool has_pending()const{return false;}void cancel_pending(){CHECK(false);}} g_project_session;
int g_patch_confirm_slot=-1,g_patch_delete_confirm_slot=-1;
void back(u32 held=KEY_B){u32 down=KEY_B,up=0;bool stage_skip_touch=chains_editor_input(down,held,up);
#include "main_editor_back.inc"
    (void)stage_skip_touch;capture();
}
void pure_motion(){
    for(int count=1;count<=6;++count)for(int selected=0;selected<count;++selected){
        int order[]={0,1,2,3,4,5};const auto layout=cc::layout(order,count,selected);const auto tile=layout.box[selected];
        const et::Bounds origin{tile.x,0,tile.x+tile.w-1,197};int last_left=origin.left,last_right=origin.right;
        for(int p=0;p<=200;++p){const float t=p/200.f;const auto a=et::bounds(origin,t,false),b=et::bounds(origin,t,true);
            CHECK(b.left==a.left+40&&b.right==a.right+40&&a.left>=-40&&a.right<=359&&b.left>=0&&b.right<=399);
            CHECK(a.left<=last_left&&a.right>=last_right&&a.bottom>=197&&a.bottom<=239&&b.top>=0&&b.top<=40);
            last_left=a.left;last_right=a.right;++geometry_samples;
            if(p==0)CHECK(a.left==origin.left&&a.right==origin.right&&a.bottom==197&&b.top==40);
            if(p==200)CHECK(a.left==-40&&a.right==359&&a.bottom==239&&b.left==0&&b.right==399&&b.top==0);
        }
    }
    for(int p=0;p<=200;++p){et::begin(1000);CHECK(!et::sample(999)&&et::state.progress==0);et::sample(1000+p);
        const float start=et::state.progress;et::close(1000+p);CHECK(et::state.closing&&et::state.from==start);
        const auto began=et::state.began;et::close(1000+p+50);CHECK(et::state.began==began);
        CHECK(et::sample(1000+p+160)&&et::state.progress==0);
    }
    et::begin(1000);et::sample(1100);const float forward=et::state.progress;et::sample(900);CHECK(et::state.progress==forward);
    et::close(1100);et::sample(1140);const float reversed=et::state.progress;et::sample(900);CHECK(et::state.progress==reversed);
    et::begin(0);et::sample(50);et::snap();CHECK(et::state.progress==1);et::sample(51);CHECK(et::state.progress==1);et::sample(0);CHECK(et::state.progress==1);
    et::begin(1000);et::sample(UINT64_MAX);CHECK(et::state.progress==1);et::close(2000);CHECK(et::sample(UINT64_MAX));et::reset();
}
void origins(){
    for(int count=1;count<=6;++count)for(int selected=0;selected<count;++selected){
        Scene s;for(int i=0;i<count;++i)s.add(i);s.settle(selected);const auto closed=draw();chains_workspace_buttons(KEY_X);capture();CHECK(et::state.active);
        const auto zero=draw();CHECK(zero.top==closed.top&&zero.bottom==closed.bottom);
        host_time_ms+=80;capture();CHECK(et::state.progress>.39f&&et::state.progress<.41f);draw();
        back();CHECK(et::state.closing);host_time_ms+=160;capture();CHECK(g_overlay==Overlay::None&&!et::state.active);draw();
    }
    for(int selected:{0,2,5}){Scene s;for(int slot:{0,2,5})s.add(slot);s.settle(selected);const auto old=draw();chains_workspace_buttons(KEY_X);capture();CHECK(et::state.active);
        CHECK(g_edit_slot==selected);const auto zero=draw();CHECK(old.top==zero.top&&old.bottom==zero.bottom);host_time_ms+=200;capture();draw();}
}
void input_edges(){
    Scene s;auto*u=static_cast<FilterUnit*>(s.add(0));s.settle();const auto before=draw();(void)before;
    const int order[]={0};const auto box=cc::layout(order,1,0).box[0];release_calls=0;
    down(box.x+20,23);CHECK(g_overlay==Overlay::None);lift();
    CHECK(g_overlay==Overlay::Editor&&et::state.active&&et::state.progress==0&&release_calls==0);
    const q15 base=u->params[FilterUnit::P_CUTOFF].base;
    down(100,90);CHECK(et::state.progress==1&&g_drag==Drag::FilterCutoff);move(114,90);lift();
    CHECK(u->params[FilterUnit::P_CUTOFF].base!=base&&release_calls>0);
    // The opener X held tail is not a new X down edge and cannot delete/confirm.
    back();host_time_ms+=160;capture();s.open();
    for(int i=0;i<5;++i){u32 d=0,h=KEY_X,up=0;CHECK(!chains_editor_input(d,h,up));CHECK(!d);capture();}
    CHECK(!chains_context_is_open()&&s.engine->chains[0].units[0]==u);
    // Fresh A snaps but remains available to real trigger dispatch in this frame.
    u32 d=KEY_A,h=KEY_A,up=0;CHECK(!chains_editor_input(d,h,up));CHECK(d==KEY_A&&et::state.progress==1);
    hid::trigger_frame(d,h);CHECK(s.engine->chains[0].units[0]==u);
}
void reverse_and_tails(){
    Scene s;auto*u=s.add(0);s.open();host_time_ms+=80;capture();const auto halfway=draw();
    back();CHECK(et::state.closing&&et::state.progress==.4f);const auto reverse=draw();CHECK(halfway.top==reverse.top&&halfway.bottom==reverse.bottom);
    u32 d=0,h=KEY_TOUCH|KEY_A|KEY_X,up=0;CHECK(chains_editor_input(d,h,up));CHECK(!d&&!h);host_time_ms+=64;capture();
    CHECK(g_overlay==Overlay::None&&!et::state.active);
    d=KEY_TOUCH|KEY_A|KEY_X;h=d;up=0;CHECK(chains_editor_input(d,h,up));CHECK(!d&&!h);CHECK(s.engine->chains[0].units[0]==u);
    d=0;h=0;up=KEY_TOUCH|KEY_A|KEY_X;CHECK(chains_editor_input(d,h,up));CHECK(!up);
    d=0;h=0;up=0;CHECK(!chains_editor_input(d,h,up));
    for(u32 global:{u32(KEY_START),u32(KEY_Y),u32(KEY_SELECT)})for(u32 shoulders:{0u,u32(KEY_L),u32(KEY_R)}){
        s.open();host_time_ms+=200;capture();back();d=global|KEY_X|KEY_TOUCH;h=d|shoulders;up=0;
        CHECK(chains_editor_input(d,h,up));CHECK(g_overlay==Overlay::None&&!et::state.active&&d==global&&h==(global|shoulders));
        d=0;h=0;up=KEY_TOUCH|KEY_X|shoulders;chains_editor_input(d,h,up);
    }
    // Values captured for reverse are current, not the opening tile's snapshot.
    s.open();host_time_ms+=200;capture();u->params[0].base=30000;u->params[0].snap();capture();back();
    host_time_ms+=160;capture();const auto returned=draw();chains_capture();const auto current=draw();
    CHECK(returned.top==current.top&&returned.bottom==current.bottom);
}
void priority(){
    fader_lancet_cases::sample_fixture();
    {Scene s;s.add(0);s.open();u32 down=KEY_L,held=KEY_L,up=0;CHECK(!chains_editor_input(down,held,up));CHECK(param_lens_input(down,held,up,false)&&param_lens_is_open());capture();
        back();CHECK(!param_lens_is_open()&&g_overlay==Overlay::Editor&&!et::state.closing);back();CHECK(et::state.closing);}
    {Scene s;auto*u=static_cast<PlayUnit*>(s.add(0,UnitKind::SamplePlayer));u->sample_slot=61;s.open();
        tap(276,220);CHECK(g_overlay==Overlay::Editor);back();CHECK(g_overlay==Overlay::Editor&&!et::state.closing);back();CHECK(et::state.closing);}
    {Scene s;s.add(0,UnitKind::Comp);s.open();host_time_ms+=200;capture();comp_focus_select(CompUnit::P_KNEE);capture();
        back();CHECK(g_overlay==Overlay::Editor&&!et::state.closing&&comp_focus_selected()==CompUnit::P_THRESHOLD);back();CHECK(et::state.closing);}
}
void stale(){
    for(int reason=0;reason<15;++reason){
        Scene s;auto*u=s.add(0);s.add(2);s.open();host_time_ms+=70;capture();Engine*engine=g_eng;auto*audio=g_audio;const auto kind=u->kind;
        if(reason==0)g_eng=nullptr;
        if(reason==1)g_audio=nullptr;
        if(reason==2)g_view=View::Sigils;
        if(reason==3)g_overlay=Overlay::SampleBrowser;
        if(reason==4)g_overlay=Overlay::Picker;
        if(reason==5)g_seq_lock_armed=true;
        if(reason==6)g_sel_slot=2;
        if(reason==7)g_edit_slot=2;
        if(reason==8)g_sel_chain=1;
        if(reason==9)g_edit_chain=1;
        if(reason==10)nav_open();
        if(reason==11)s.engine->chains[3].units[5]=new OscUnit;
        if(reason==12)u->kind=UnitKind::Noise;
        if(reason==13)std::swap(s.engine->chains[0].units[0],s.engine->chains[0].units[2]);
        std::unique_ptr<Engine> other;
        if(reason==14){other=std::make_unique<Engine>();g_eng=other.get();}
        chains_editor_capture();CHECK(!et::state.active);g_eng=engine;g_audio=audio;u->kind=kind;nav_close();
        g_view=View::Chains;g_overlay=Overlay::Editor;g_seq_lock_armed=false;g_sel_chain=g_edit_chain=g_sel_slot=g_edit_slot=0;
        CHECK(!chains_editor_back()&&!et::state.active);
    }
    {Scene s;s.add(0);s.add(1);s.settle();g_sel_slot=g_edit_slot=1;chains_workspace_buttons(KEY_X);
        CHECK(g_overlay==Overlay::Editor&&!et::state.active);}
    // A partially widened tile must use the last presented geometry, not snap.
    {Scene s;s.add(0);s.add(1);s.settle();g_sel_slot=g_edit_slot=1;capture();host_time_ms+=60;capture();
        const auto visible=draw();chains_workspace_buttons(KEY_X);capture();CHECK(et::state.active);
        const auto zero=draw();CHECK(visible.top==zero.top&&visible.bottom==zero.bottom);}
    // Direct/seq/browser entry has no chain-origin token and must not reverse.
    Scene s;s.add(0);s.settle();g_overlay=Overlay::Editor;capture();CHECK(!chains_editor_back());
    g_view=View::Sequencer;g_seq_lock_armed=true;back();CHECK(g_overlay==Overlay::None&&!g_seq_lock_armed&&!et::state.active);
}
void all_modules(){
    fader_lancet_cases::sample_fixture();
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink})for(const auto&item:fader_lancet_cases::catalogue){
        where=item.name;Scene s;palette::set(theme);auto*u=s.add(0,item.kind);
        if(item.kind==UnitKind::SamplePlayer)static_cast<PlayUnit*>(u)->sample_slot=61;
        if(item.kind==UnitKind::Grain)static_cast<GrainUnit*>(u)->sample_slot=61;
        s.open();host_time_ms+=80;capture();draw();
        host_time_ms+=40;capture();draw();host_time_ms+=40;capture();draw();host_time_ms+=40;capture();
        const auto full=draw();module_inspector_surface(true);top_draw();CHECK(pixels==full.top);
        module_inspector_surface(false);editor_draw();CHECK(pixels==full.bottom);back();host_time_ms+=80;capture();draw();
    }
}
void capture_media(const std::filesystem::path&dir){
    CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}){
        Scene s;palette::set(theme);s.add(0,UnitKind::SamplePlayer);auto*f=static_cast<FilterUnit*>(s.add(1));s.add(2,UnitKind::Ghost);
        f->params[FilterUnit::P_CUTOFF].base=5142;f->params[FilterUnit::P_RES].base=10485;f->params[FilterUnit::P_DRIVE].base=5898;
        for(auto&p:f->params)p.snap();
        s.settle(1);
        const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        const auto save=[&](const std::string&name){capture();const auto p=draw(false);width=400;pixels=p.top;dump_surface(dir.string(),prefix+name+"_top");width=320;pixels=p.bottom;dump_surface(dir.string(),prefix+name+"_bottom");};
        save("closed");chains_workspace_buttons(KEY_X);const auto began=host_time_ms;
        for(int ms:{0,40,80,120,160,200}){host_time_ms=began+ms;char name[32];std::snprintf(name,sizeof name,"open-%03d",ms);save(name);}
        back();const auto closed=host_time_ms;
        for(int ms:{40,80,120,160}){host_time_ms=closed+ms;char name[32];std::snprintf(name,sizeof name,"close-%03d",ms);save(name);}
    }
    std::puts("editor transition capture: 22 current C++ duals / 44 PPM; zero inherited groups; host texture emulation, not hardware");
}
int run(){int total=0,failed=0;const auto test=[&](const char*name,auto fn){++total;try{fn();std::printf("ok: transition %s\n",name);}catch(const std::exception&ex){++failed;std::fprintf(stderr,"FAIL transition %s [%s]: %s\n",name,where.c_str(),ex.what());}std::fflush(stdout);};
    test("pure native dual geometry / time / all reversal phases",pure_motion);
    test("1..6 duplicate and sparse origins / frozen endpoint",origins);
    test("actual main touch opener release / same-action snap / held X / A",input_edges);
    test("interrupted reverse / held-tail drain / global finish / current values",reverse_and_tails);
    test("actual main B lens PLAY COMP priority",priority);
    test("identity kind all-chain engine audio nav browser lock exclusion",stale);
    test("28 modules both themes / fullscreen endpoint / detached compositor",all_modules);
    CHECK(peak<8192);
    std::printf("editor transition: %d/%d groups; %d duals; %d geometry samples; raw host peak %d main-path / %d draw-then-cover objects; no GPU/FPS claim\n",total-failed,total,duals,geometry_samples,peak,direct_peak);
    return failed?1:0;
}
}
extern "C" void __real__ZN6seance8platform5ui3ds20editor_touch_releaseEv();
extern "C" void __wrap__ZN6seance8platform5ui3ds20editor_touch_releaseEv(){++transition_cases::release_calls;__real__ZN6seance8platform5ui3ds20editor_touch_releaseEv();}
int main(int argc,char**argv){theme_test::select();CHECK(menu_style::ambient_init());CHECK(editor_style::init());transition_cases::capture();int result=2;
    if(argc==2&&std::string(argv[1])=="--proof-only")result=transition_cases::run();
    if(argc==3&&std::string(argv[1])=="--capture-only"){transition_cases::capture_media(argv[2]);result=0;}
    editor_style::shutdown();menu_style::ambient_shutdown();CHECK(editor_gpu::textures.empty());return result;
}
