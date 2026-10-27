// New DLY pair cases. Uses only native fixture/render utilities; no older suites.
// Driver lives exactly four levels below root in release/beta-20261004/delay-pair.
#include "../../../../platform/3ds/delay_iris.h"
namespace delay_pair_cases {
namespace h=module_headings_cases;
namespace f=fader_lancet_cases;
using D=DelayUnit;
// Distinct derived fixture type makes our paired take an exact match; inherited
// catalogue take(Scene&) remains available only through explicit h::take.
struct Scene : h::Scene { using h::Scene::Scene; };
int gesture_cases=0,detached_pairs=0;
std::ofstream gestures;
void case_begin(const std::string& name) {
    gestures<<gesture_cases<<'\t'<<name<<'\n';gestures.flush();
    std::printf("delay pair check: %s\n",name.c_str());std::fflush(stdout);
}
int iris_math_checks=0;
void iris_math(){
    // Independent trigonometric reference for the 65-word bounded quarter ROM.
    namespace iris=seance::platform::ui3ds::delay_iris;
    CHECK(iris::SIN[0]==0&&iris::SIN[64]==16384);
    for(int k=0;k<=64;++k){
        const int expected=int(std::lround(std::sin(double(k)*3.14159265358979323846/128)*16384));
        CHECK(std::abs(int(iris::SIN[k])-expected)<=1);++iris_math_checks;
    }
    for(int angle=0;angle<65536;angle+=193){
        const int wanted=int(std::lround(std::sin(double(angle)*6.283185307179586/65536)*16384));
        CHECK(std::abs(iris::sine(angle)-wanted)<=3);++iris_math_checks;
    }
    for(int t:{0,8192,16384,32767})for(int fb:{0,16384,32767})for(int mix:{0,261,262,8192,32767}) {
        iris::Frame frame{};iris::build(frame,t,12000,fb,mix);
        int expected=0;unsigned gain=unsigned(mix);
        for(int n=0;n<8&&gain>=262;++n){++expected;gain=gain*unsigned(fb)/32767;}
        CHECK(frame.count==expected && frame.count<=8);
        CHECK(frame.base.x>=34&&frame.base.x<=366&&frame.base.y>=58&&frame.base.y<=204);
        for(int n=0;n<frame.count;++n){
            CHECK(frame.emphasis[n]>=38&&frame.emphasis[n]<=255);
            for(int j=0;j<=iris::SEGMENTS;++j){const auto&p=frame.points[n][j];
                CHECK(p.x>=34&&p.x<=366&&p.y>=58&&p.y<=204);++iris_math_checks;}
        }
        ++iris_math_checks;
    }
}
std::array<q15,D::P_COUNT> bases(const D& u) {
    std::array<q15,D::P_COUNT> out{};for(int p=0;p<u.num_params;++p)out[p]=u.params[p].base;return out;
}
D& unit(Scene& s){return *static_cast<D*>(s.unit);}
void legacy(Scene& s) {
    editor_cancel_input();auto* old=new D(false);delete s.engine->replace_unit(0,s.slot,old);s.unit=old;
    old->control_tick();s.capture();delay_focus_select(D::P_TIME);s.capture();
}
void cancel(){editor_cancel_input();param_lens_touch_release();small_press::cancel();}
void down(int x,int y){editor_touch_down(x,y);}
void drag(int x,int y){editor_touch_drag(x,y);}
void release(){editor_touch_release();small_press::cancel();}
void lens_pair(Scene& s,const std::string& id){
    // Real editor_draw paints frozen DLY underlay then the TRANSLUCENT MOD panel.
    // Reconstruct that same order without engine/audio reads and require zero
    // text overlaps after the approved DLY-only inactive-body suppression.
    s.capture();editor_style::capture();editor_gpu::begin();
    const auto time=s.engine->transport_frame;const int dirty=mutations;
    module_inspector_surface(true);{f::AudioHidden hidden;top_draw();}
    const auto top=pixels;const int upper=objects;CHECK(upper>0&&upper<8192&&!outside_rects&&!outside_lines&&!text_overlaps);
    module_inspector_surface(false);{f::AudioHidden hidden;editor_draw();}
    const auto bottom=pixels;const int lower=objects;
    CHECK(lower>0&&lower<8192&&upper+lower<=8192&&!outside_rects&&!outside_lines&&!text_overlaps);
    const auto again=[&](){
        module_inspector_surface(true);{ModuleInspectorNoEngine detached;top_draw();}
        CHECK(pixels==top&&objects==upper&&!outside_rects&&!outside_lines&&!text_overlaps);
        module_inspector_surface(false);{ModuleInspectorNoEngine detached;
            delay_focus_draw(unit(s));recorder_focus_draw_return();param_lens_draw_bottom();}
        CHECK(pixels==bottom&&objects==lower&&!outside_rects&&!outside_lines&&!text_overlaps);
    };
    again();{f::PoisonAfterCapture poisoned(s);again();}
    CHECK(mutations==dirty&&s.engine->transport_frame==time);
    ++detached_pairs;std::printf("delay pair translucent lens frozen underlay: %s\n",id.c_str());
}
void take(Scene& s,const std::string& id,const std::string& variant) {
    // Reference is the composed top_draw/editor_draw. Utility pair then detaches
    // BOTH engine/audio and poisons copied globals/params before pure re-draws.
    h::take(s,id,variant); // keep diagnostic raw surfaces even if a later guard fails
    if(param_lens_is_open()){lens_pair(s,id);return;}
    s.capture();editor_style::capture();editor_gpu::begin();f::case_name=id;
    f::pair(s,true);++detached_pairs;
}
void lock(Scene& s,int step=7) {
    cancel();g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=step;s.capture();
}
void process(D& u,int count) {
    u.control_tick();q15 silence[128]={};
    while(count>0){const int n=std::min(128,count);std::fill(std::begin(silence),std::end(silence),0);u.process(silence,n);count-=n;}
}
struct Box {int p,x,y,w,h;bool sync=false;};
std::vector<Box> controls(bool modern,bool synced=false) {
    std::vector<Box> result={{synced?D::P_DIV:D::P_TIME,8,28,192,97,synced},
        {D::P_FEEDBACK,208,28,105,97},{D::P_MIX,8,132,70,69},
        {D::P_WOW,160,132,70,69},{D::P_SAT,236,132,70,69}};
    if(modern)result.push_back({D::P_TONE,84,132,70,69});
    if(synced)result.push_back({D::P_FEEL,142,28,58,24,true});
    return result;
}
void continuous(bool modern,bool synced) {
    Scene s(UnitKind::Delay);if(!modern)legacy(s);auto& u=unit(s);
    if(modern)s.set(D::P_SYNC,synced?32767:0);
    case_begin(std::string("continuous corners / ")+(modern?(synced?"sync":"modern"):"legacy"));
    for(const auto& box:controls(modern,synced))for(int value:{0,17000,32767})for(int corner=0;corner<4;++corner) {
        const int x=box.x+((corner&1)?box.w-1:0),y=box.y+((corner&2)?box.h-1:0);
        const int p=synced && box.p==D::P_DIV && x>=142 && y<52?D::P_FEEL:box.p;
        s.set(p,q15(value));const auto before=bases(u);const int dirty=mutations;
        down(x,y);CHECK(delay_focus_selected()==p&&bases(u)==before&&mutations==dirty);
        drag(x,y);CHECK(bases(u)==before&&mutations==dirty);cancel();++gesture_cases;
    }
    for(const auto& box:controls(modern,synced)) {
        const int x=box.p==D::P_DIV?50:box.x+box.w/2,y=box.y+box.h/2;
        s.set(box.p,13000);const auto before=bases(u);const int dirty=mutations;
        down(x,y);CHECK(bases(u)==before&&mutations==dirty);drag(x+6,y+16);
        const int expected=13000+6*32767/(box.p==D::P_FEEL?48:144);
        CHECK(u.params[box.p].base==expected&&mutations==dirty+1);
        for(int p=0;p<u.num_params;++p)if(p!=box.p)CHECK(u.params[p].base==before[p]);
        drag(x+6,y+16);CHECK(mutations==dirty+1);release();++gesture_cases;
        // Same stationary coordinate must not put an externally changed value back.
        s.set(box.p,13000);down(x,y);u.params[box.p].base=22222;
        const int external_dirty=mutations;drag(x,y);CHECK(u.params[box.p].base==22222&&mutations==external_dirty);
        cancel();++gesture_cases;
    }
}
void cancelled_owners() {
    case_begin("drag ownership / selection bank step slot unit view overlay lock cancel");
    for(int why=0;why<10;++why) {
        Scene s(UnitKind::Delay);auto& u=unit(s);s.set(D::P_TIME,13000);
        if(why==2)lock(s);
        down(60,80);const auto before=bases(u);const int dirty=mutations;const auto pattern=s.hash();
        Unit* replacement=nullptr;
        switch(why) {
            case 0:delay_focus_select(D::P_FEEDBACK);break;
            case 1:s.engine->active_pattern=1;break;
            case 2:g_seq_lock_step=8;break;
            case 3:g_edit_slot=0;break;
            case 4:g_edit_chain=1;break;
            case 5:replacement=new D;CHECK(s.engine->replace_unit(0,s.slot,replacement)==s.unit);break;
            case 6:g_overlay=Overlay::None;break;
            case 7:g_view=View::Sequencer;break;
            case 8:g_seq_lock_armed=true;break;
            case 9:cancel();break;
        }
        // Body dispatch tests the new owner; direct drag also sees the previous
        // object still alive, so a recycled matching kind cannot accept ownership.
        if(why==3||why==4||why==5)editor_touch_drag(90,80);else delay_focus_drag(u,90,80);
        CHECK(bases(u)==before&&mutations==dirty&&s.hash()==pattern);
        if(replacement){CHECK(bases(*static_cast<D*>(replacement))!=before);delete s.engine->replace_unit(0,s.slot,s.unit);}
        g_edit_chain=0;g_edit_slot=s.slot;cancel();++gesture_cases;
    }
}
struct Tap {int p,x,y,w,h;int before,next;};
void taps(bool modern) {
    case_begin(modern?"modern taps owned lift / movement / stale edit":"legacy hold owned lift");
    Scene s(UnitKind::Delay);if(!modern)legacy(s);auto& u=unit(s);
    std::vector<Tap> boxes={{D::P_HOLD,184,0,56,25,0,32767}};
    if(modern) {
        boxes.push_back({D::P_SYNC,8,204,46,24,32767,0});boxes.push_back({D::P_SYNC,54,204,54,24,0,32767});
        boxes.push_back({D::P_MODE,148,204,52,24,32767,0});boxes.push_back({D::P_MODE,200,204,50,24,0,32767});
    }
    for(const auto& box:boxes) {
        for(int corner=0;corner<4;++corner) {
            s.set(box.p,box.before);const int x=box.x+((corner&1)?box.w-1:0),y=box.y+((corner&2)?box.h-1:0);
            const auto before=bases(u);const int dirty=mutations;
            down(x,y);drag(x,y);CHECK(bases(u)==before&&mutations==dirty);cancel();++gesture_cases;
        }
        for(int scenario=0;scenario<8;++scenario) {
            s.set(box.p,box.before);const int x=box.x+box.w/2,y=box.y+box.h/2;
            const int dirty=mutations;const auto before=bases(u);down(x,y);
            CHECK(bases(u)==before&&mutations==dirty);
            if(scenario==1)drag(x+8,y); // boundary, still owned and within the box
            if(scenario==2)drag(x+9,y);
            if(scenario==3)drag(box.x-1,y);
            if(scenario==4)delay_focus_select(box.p==D::P_TIME?D::P_MIX:D::P_TIME);
            if(scenario==5)s.engine->active_pattern=1;
            if(scenario==6)u.params[box.p].base=12000;
            if(scenario==7)cancel();
            release();
            CHECK(u.params[box.p].base==(scenario<2?box.next:scenario==6?12000:box.before));
            CHECK(mutations==dirty+(scenario<2));
            s.engine->active_pattern=0;++gesture_cases;
        }
    }
}
void lock_regression(const std::string& prefix) {
    case_begin("lock edit preserves stored bases / step cancel / clear lock / full lock");
    Scene s(UnitKind::Delay);auto& u=unit(s);lock(s);
    for(const auto& box:controls(true,false)) {
        CHECK(s.engine->set_step_lock(0,7,s.slot,box.p,4000));s.capture();
        const auto before=bases(u);const int x=box.x+box.w/2,y=box.y+box.h/2;
        down(x,y);CHECK(seq_display_value(s.slot,box.p,u.params[box.p].base)==4000);
        drag(x+6,y);CHECK(seq_display_value(s.slot,box.p,u.params[box.p].base)==4000+6*32767/144&&bases(u)==before);
        const auto edited=s.hash();g_seq_lock_step=8;drag(x+20,y);CHECK(s.hash()==edited&&bases(u)==before);
        g_seq_lock_step=7;cancel();down(20,215);release();
        CHECK(seq_display_value(s.slot,box.p,u.params[box.p].base)==u.params[box.p].base&&bases(u)==before);++gesture_cases;
    }
    for(const auto& tap:std::vector<Tap>{{D::P_SYNC,108,204,40,24,0,32767},{D::P_MODE,200,204,50,24,0,32767},{D::P_HOLD,184,0,56,25,0,32767}}) {
        CHECK(s.engine->set_step_lock(0,7,s.slot,tap.p,0));s.capture();const auto before=bases(u);
        down(tap.x+10,tap.y+10);CHECK(seq_display_value(s.slot,tap.p,u.params[tap.p].base)==0);
        release();CHECK(seq_display_value(s.slot,tap.p,u.params[tap.p].base)==32767&&bases(u)==before);
        down(20,215);release();CHECK(bases(u)==before);++gesture_cases;
    }
    const bool bypass=u.bypass;down(260,10);release();CHECK(u.bypass==bypass);
    const auto before=bases(u);const auto old=s.hash();down(280,215);g_ui_frame+=60;drag(280,215);release();
    CHECK(!u.visual_state().clearing&&bases(u)==before&&s.hash()==old);++gesture_cases;
    for(int p=1;p<=SEQ_LOCKS;++p)CHECK(s.engine->set_step_lock(0,7,s.slot,p,q15(5000+p)));
    const auto full=s.hash();down(50,80);drag(56,80);
    CHECK(s.hash()==full&&bases(u)==before&&g_seq_lock_flash>0);cancel();
    take(s,prefix+"delay_lock_full","lock-full");++gesture_cases;
}
void clear_regression(const std::string& prefix) {
    case_begin("clear 44/45 frame boundary / release leave bank selection context cancellation");
    for(int why=0;why<7;++why) {
        Scene s(UnitKind::Delay);auto& u=unit(s);const auto before=bases(u);const int dirty=mutations;
        g_ui_frame=100;down(280,215);g_ui_frame=144;drag(280,215);CHECK(!u.visual_state().clearing);
        if(why==0)release();
        if(why==1)drag(257,215);
        if(why==2)s.engine->active_pattern=1;
        if(why==3)delay_focus_select(D::P_FEEDBACK);
        if(why==4)g_overlay=Overlay::None;
        if(why==5)g_seq_lock_armed=true;
        if(why==6)cancel();
        g_ui_frame=145;delay_focus_drag(u,280,215);
        CHECK(!u.visual_state().clearing&&bases(u)==before&&mutations==dirty);cancel();++gesture_cases;
    }
    Scene s(UnitKind::Delay);auto& u=unit(s);const auto before=bases(u);const int dirty=mutations;
    g_ui_frame=100;down(280,215);CHECK(!u.visual_state().clearing);
    g_ui_frame=130;drag(280,215);take(s,prefix+"delay_clear_armed","clear-armed");
    g_ui_frame=144;drag(280,215);CHECK(!u.visual_state().clearing);
    g_ui_frame=145;drag(280,215);CHECK(u.visual_state().clearing&&bases(u)==before&&mutations==dirty);
    u.bypass=true;take(s,prefix+"delay_clear_pending","clear-pending");
    s.render(128);CHECK(u.visual_state().clearing);u.bypass=false;s.render(128);
    CHECK(u.visual_state().clearing);take(s,prefix+"delay_clear_fade","clear-fade");
    s.render(2048);CHECK(!u.visual_state().clearing);g_ui_frame+=45;drag(280,215);release();
    CHECK(!u.visual_state().clearing&&bases(u)==before&&mutations==dirty);++gesture_cases;
}
void legacy_disabled() {
    case_begin("legacy fixed tone / mode and clear inert; bypass independent");
    Scene s(UnitKind::Delay);legacy(s);auto& u=unit(s);const auto before=bases(u);const int dirty=mutations;
    for(auto xy:std::vector<std::pair<int,int>>{{84,132},{154,200},{120,165},{20,215},{80,215},{120,215},{175,215},{225,215},{280,215}}) {
        down(xy.first,xy.second);g_ui_frame+=60;drag(xy.first+6,xy.second);release();
        CHECK(bases(u)==before&&mutations==dirty&&!u.visual_state().clearing);++gesture_cases;
    }
    const bool byp=u.bypass;down(260,10);CHECK(u.bypass!=byp);release();CHECK(mutations==dirty+1);++gesture_cases;
}
void lens_regression(const std::string& prefix) {
    case_begin("real mod lens route / relative base depth / held permanent edit / lift");
    for(bool modern:{true,false}) {
        Scene s(UnitKind::Delay);if(!modern)legacy(s);auto& u=unit(s);
        for(int p=0;p<u.num_params;++p) {
            cancel();param_lens_cancel();s.set(p,13000);delay_focus_select(p);s.capture();
            CHECK(param_lens_touch_down(250,232)&&param_lens_is_open());param_lens_touch_release();
            const auto before=bases(u);const int dirty=mutations;
            CHECK(param_lens_touch_down(180,95));CHECK(bases(u)==before&&mutations==dirty);
            CHECK(param_lens_touch_drag(186,95));param_lens_touch_release();
            CHECK(u.params[p].base==13000+6*32767/271&&mutations==dirty+1);
            CHECK(delay_focus_selected()==p&&!s.engine->param_temp_active());
            param_lens_touch_down(50,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);
            u.params[p].mod_depth=1000;param_lens_touch_down(100,145);param_lens_touch_drag(106,145);param_lens_touch_release();
            CHECK(u.params[p].mod_depth==1000+6*65534/271);
            u32 back=KEY_B;CHECK(param_lens_input(back,0,0,false)&&!param_lens_is_open()&&delay_focus_selected()==p);++gesture_cases;
        }
        if(modern) {
            delay_focus_select(D::P_FEEDBACK);s.capture();u32 key=KEY_L;
            CHECK(param_lens_input(key,KEY_L,0,false)&&param_lens_is_open());param_lens_touch_release();
            const auto stored=u.params[D::P_FEEDBACK].base;param_lens_touch_down(180,95);param_lens_touch_drag(186,95);param_lens_touch_release();
            CHECK(u.params[D::P_FEEDBACK].base==stored+6*32767/271&&!s.engine->param_temp_active());
            take(s,prefix+"delay_held_mod","held-mod");
            key=0;CHECK(param_lens_input(key,0,KEY_L,false)&&!param_lens_is_open());
            CHECK(u.params[D::P_FEEDBACK].base==stored+6*32767/271);++gesture_cases;
        }
    }
}
void states(const std::string& prefix) {
    for(const char* v:{"bypass","long6","holes","live","lens","lock"}) {
        Scene s(UnitKind::Delay);auto& u=unit(s);const std::string name=v;
        if(name=="bypass")u.bypass=true;
        if(name=="long6"||name=="holes")s.path(name=="holes");
        if(name=="live")s.signal(20000,12000,true); // real routed Engine render, never hand-assign target/effective
        if(name=="lens"){s.capture();u32 down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());param_lens_touch_release();}
        if(name=="lock"){lock(s,4);CHECK(s.engine->set_step_lock(0,4,s.slot,D::P_TIME,14000));}
        take(s,prefix+"delay_"+name,name);
    }
    for(bool modern:{true,false}) {
        const std::string tag=modern?"delay_":"delay_legacy_";
        {Scene s(UnitKind::Delay);if(!modern)legacy(s);unit(s).reset();CHECK(!unit(s).visual_state().ready);take(s,prefix+tag+"fresh","fresh");}
        const int count=modern?D::P_COUNT:6;
        for(int p=0;p<count;++p)for(int edge:{0,16384,32767}) {
            Scene s(UnitKind::Delay);if(!modern)legacy(s);s.set(p,edge);delay_focus_select(p);s.render(512);
            take(s,prefix+tag+"p"+std::to_string(p)+"_"+std::to_string(edge),"parameter-edge");
        }
    }
    for(int feel=0;feel<3;++feel) {
        Scene s(UnitKind::Delay);s.set(D::P_SYNC,32767);s.set(D::P_DIV,delay_time::division_value(4));s.set(D::P_FEEL,delay_time::feel_value(feel));
        delay_focus_select(feel?D::P_FEEL:D::P_DIV);s.render(2048);
        take(s,prefix+"delay_sync_"+(feel==0?"straight":feel==1?"dotted":"triplet"),"sync-feel");
    }
    {Scene s(UnitKind::Delay);s.engine->set_bpm(20);s.set(D::P_SYNC,32767);s.set(D::P_DIV,32767);s.set(D::P_FEEL,delay_time::feel_value(1));s.render(2048);
     CHECK(unit(s).visual_state().limited&&unit(s).visual_state().target_frames==128000);take(s,prefix+"delay_sync_limited","sync-limited");}
    for(int raw:{0,32767}) {Scene s(UnitKind::Delay);s.set(D::P_TIME,raw);s.render(2048);take(s,prefix+(raw?"delay_free_max":"delay_free_min"),"free-edge");}
    for(int feedback:{0,32767})for(int mix:{0,32767}) {
        Scene s(UnitKind::Delay);s.set(D::P_TIME,0);s.set(D::P_FEEDBACK,feedback);s.set(D::P_MIX,mix);s.render(2048);
        take(s,prefix+"delay_dense_"+std::to_string(feedback)+"_"+std::to_string(mix),"feedback-mix-edge");
    }
    {Scene s(UnitKind::Delay);s.set(D::P_TIME,0);s.set(D::P_FEEDBACK,32767);s.set(D::P_MIX,32767);s.render(2048);take(s,prefix+"delay_dense","dense");}
    {Scene s(UnitKind::Delay);auto& u=unit(s);s.set(D::P_MODE,32767);process(u,2048);s.set(D::P_TIME,32767);process(u,128);
     CHECK(u.visual_state().crossfading);take(s,prefix+"delay_clean_handoff","clean-handoff");
     s.set(D::P_HOLD,32767);process(u,128);CHECK(u.visual_state().hold_amount>0&&u.visual_state().hold_amount<32767);take(s,prefix+"delay_hold_arrival","hold-arrival");
     process(u,1024);CHECK(u.visual_state().hold_amount==32767);take(s,prefix+"delay_hold_full","hold-full");
     u.reset();CHECK(!u.visual_state().ready);take(s,prefix+"delay_reset","reset");}
    {Scene s(UnitKind::Delay);s.engine->chains[0].mute=true;take(s,prefix+"delay_mute","mute");}
}
void iris_idle(const std::string& prefix){
    case_begin("no autonomous iris rotation or numeric motion without engine input");
    Scene s(UnitKind::Delay);s.signal(19000,9000,true);
    take(s,prefix+"delay_idle_first","idle-first");
    const auto previous=pixels;const int dirty=mutations;const auto clock=s.engine->transport_frame;
    g_ui_frame+=137;host_time_ms+=5000;
    take(s,prefix+"delay_idle_later","idle-later");
    CHECK(pixels==previous&&s.engine->transport_frame==clock&&mutations==dirty);
    ++gesture_cases;
}
int clamp_expected(int v){return std::min(32767,std::max(0,v));}
void discrete_routes(const std::string& prefix){
    case_begin("actual routed sync/mode/hold/div/feel; crossed gesture cancels, lock draft separate");
    Scene s(UnitKind::Delay);auto& u=unit(s);auto& sg=s.engine->sigils[0];sg.depth=32767;
    auto drive=[&](int value){for(int j=0;j<SIGIL_POINTS;++j)sg.set_point(j,q15(value));
        s.engine->reset_sigil(0);s.render(128);s.capture();};
    auto& sync=u.params[D::P_SYNC];sync.base=16384;sync.src=ModSource::SigilA;
    sync.mod_depth=7000;sync.mod_enabled=true;sync.snap();
    drive(-32767);CHECK(sync.target<16384);const auto first=bases(u);
    take(s,prefix+"delay_mod_sync_free","routed-sync-free");
    CHECK(pixels[223*320+24]==palette::spot());
    down(60,80);CHECK(delay_focus_selected()==D::P_TIME);drive(32767);
    CHECK(sync.target>16384&&delay_focus_selected()==D::P_TIME);
    drag(66,80);CHECK(bases(u)==first);release();
    take(s,prefix+"delay_mod_sync_switch","routed-sync-switch");
    CHECK(pixels[223*320+72]==palette::spot());++gesture_cases;
    const auto before=bases(u);down(60,80);CHECK(delay_focus_selected()==D::P_DIV);
    drive(-32767);drag(66,80);release();CHECK(bases(u)==before);
    take(s,prefix+"delay_mod_sync_return","routed-sync-return");++gesture_cases;
    auto& mode=u.params[D::P_MODE];mode.base=16384;mode.src=ModSource::SigilA;
    mode.mod_depth=7000;mode.mod_enabled=true;mode.snap();
    drive(-32767);CHECK(mode.target<16384);take(s,prefix+"delay_mod_tape","routed-mode-tape");
    CHECK(pixels[223*320+164]==palette::spot());
    drive(32767);CHECK(mode.target>16384);take(s,prefix+"delay_mod_clean","routed-mode-clean");
    CHECK(pixels[223*320+216]==palette::spot());++gesture_cases;
    auto& hold=u.params[D::P_HOLD];hold.base=16384;hold.src=ModSource::SigilA;
    hold.mod_depth=7000;hold.mod_enabled=true;hold.snap();
    drive(-32767);CHECK(hold.target<16384);take(s,prefix+"delay_mod_hold_off","routed-hold-off");
    drive(32767);CHECK(hold.target>16384);take(s,prefix+"delay_mod_hold_on","routed-hold-on");
    ++gesture_cases;
    // Only the mode/div/feel targets gate modern sync; no effective-slew alias.
    sync.base=32767;sync.mod_depth=0;sync.snap();
    auto& div=u.params[D::P_DIV];div.base=16384;div.src=ModSource::SigilA;div.mod_depth=9000;div.snap();
    auto& feel=u.params[D::P_FEEL];feel.base=16384;feel.src=ModSource::SigilA;feel.mod_depth=8500;feel.snap();
    for(int signal:{-32767,0,32767}) {
        drive(signal);CHECK(sync.target>16384);
        const int wanted_div=clamp_expected(16384+((int64_t(sg.value)*9000)>>15));
        const int wanted_feel=clamp_expected(16384+((int64_t(sg.value)*8500)>>15));
        CHECK(div.target==wanted_div&&feel.target==wanted_feel);
        take(s,prefix+"delay_mod_divfeel_"+std::to_string(signal+32767),"routed-division-feel");++gesture_cases;
    }
    lock(s,7);CHECK(s.engine->set_step_lock(0,7,s.slot,D::P_SYNC,0));
    drive(32767);CHECK(sync.target>16384&&seq_display_value(s.slot,D::P_SYNC,sync.base)==0);
    take(s,prefix+"delay_mod_lock_draft","routed-lock-draft");
    CHECK(bases(u)[D::P_SYNC]==32767);++gesture_cases;
}
int motion_frames=0, route_cases=0, physical_oracles=0;
std::ofstream motion;
// Independent integer/physical expectations. Never ask production's display
// formatter to supply both the expected and the observed screen values.
int pct(q15 v){return (std::max(0,int(v))*100+16383)/32767;}
int tone_hz_expected(q15 v){return int(200.0*std::pow(60.0,double(std::max(0,int(v)))/32767.0)+0.5);}
int ms_expected(q15 v,bool modern){
    const int frames=modern?int(32.0*std::pow(4000.0,double(std::max(0,int(v)))/32767.0)+0.5):
        32+((int64_t(std::max(0,int(v)))*31967)>>15);
    return (frames+16)/32;
}
void motion_trace(const std::string& prefix){
    case_begin("48 live frames / one actual routed sigil / six musical controls");
    Scene s(UnitKind::Delay);auto& u=unit(s);
    // Persistent bases remain fixed; modulation is resolved by actual Engine::render.
    // Different depths keep all six numbers distinguishable and unclipped.
    const int params[]={D::P_TIME,D::P_FEEDBACK,D::P_MIX,D::P_TONE,D::P_WOW,D::P_SAT};
    const int stored[]={delay_time::free_value(8000),19000,16000,11000,9000,10000};
    const int depth[]={4400,9000,7600,7100,7400,6900};
    for(int i=0;i<6;++i){auto& q=u.params[params[i]];q.base=stored[i];q.src=ModSource::SigilA;
        q.mod_depth=depth[i];q.mod_enabled=true;q.snap();}
    u.control_tick();auto& sigil=s.engine->sigils[0];sigil.depth=32767;
    for(int p=0;p<SIGIL_POINTS;++p)sigil.set_point(p,q15(std::lround(std::sin(double(p)*6.283185307179586/SIGIL_POINTS)*30000)));
    auto config=s.engine->sigil_config[0];config.enabled=true;config.legacy=true;
    s.engine->set_sigil_config(0,config);s.engine->set_sigil_sync(0,false);s.engine->set_sigil_rate(0,6000);s.engine->reset_sigil(0);
    delay_focus_select(D::P_TIME);
    const int original_dirty=mutations;
    for(int frame=0;frame<48;++frame){
        // 1024 genuine engine audio frames: sigil phase and the delayed audio
        // history change without fabricated signal, effective, or visual_state.
        s.render(1024);const int source=s.engine->sigils[0].value;
        const auto b=bases(u);s.capture();
        std::array<ParamInspector,6> inspector{};
        for(int i=0;i<6;++i){
            const Param& q=u.params[params[i]];
            CHECK(q.base==stored[i]&&q.src==ModSource::SigilA&&q.mod_depth==depth[i]&&q.mod_enabled);
            const int wanted=clamp_expected(stored[i]+((int64_t(source)*depth[i])>>15));
            CHECK(q.target==wanted&&std::abs(int(q.effective)-wanted)<1000);
            s.engine->capture_param_inspector(0,s.slot,params[i],inspector[i]);
            CHECK(inspector[i].valid&&inspector[i].base==stored[i]&&inspector[i].target==wanted && inspector[i].source==ModSource::SigilA);
            ++physical_oracles;
        }
        const std::string id=prefix+"delay_motion_"+(frame<10?"0":"")+std::to_string(frame);
        take(s,id,"engine-sigil");
        // Live solid markers on the six actual lower rails, sampled from the
        // composed native raster. Stored/base hollow anchors stay unchanged.
        static const int rail[][3]={{14,190,118},{218,306,118},{16,70,192},
                                    {92,146,192},{168,222,192},{244,298,192}};
        for(int i=0;i<6;++i){
            const int x=rail[i][0]+std::max(0,int(u.params[params[i]].effective))*(rail[i][1]-rail[i][0])/32767;
            CHECK(x>=rail[i][0]&&x<=rail[i][1]&&pixels[rail[i][2]*320+x]==palette::spot());
        }
        const int time=ms_expected(u.params[D::P_TIME].effective,true);
        const int tone=tone_hz_expected(u.params[D::P_TONE].effective);
        motion<<prefix.substr(0,prefix.size()-1)<<'\t'<<frame<<'\t'<<id<<'\t'<<source<<'\t'<<u.visual_state().ready;
        for(int i=0;i<6;++i){const auto& q=u.params[params[i]];
            motion<<'\t'<<params[i]<<'\t'<<q.base<<'\t'<<q.target<<'\t'<<q.effective;
            if(i==0)motion<<'\t'<<time;
            else if(i==3)motion<<'\t'<<tone;
            else motion<<'\t'<<pct(q.effective);
        }
        motion<<'\n';motion.flush();++motion_frames;
        CHECK(bases(u)==b&&mutations==original_dirty);
    }
}
void route_edges(const std::string& prefix){
    case_begin("a..h / env routes, disabled cable, negative clamp and target-v-effective lag");
    static const ModSource sources[]={ModSource::SigilA,ModSource::SigilB,ModSource::SigilC,ModSource::SigilD,
        ModSource::SigilE,ModSource::SigilF,ModSource::SigilG,ModSource::SigilH,ModSource::EnvFollow};
    for(int i=0;i<9;++i){
        Scene s(UnitKind::Delay);auto& u=unit(s);auto& q=u.params[D::P_FEEDBACK];
        q.base=12000;q.src=sources[i];q.mod_depth=i==7?-23000:10000;q.mod_enabled=true;q.snap();
        if(i!=8){const int sg=mod_source_sigil(sources[i]);CHECK(sg>=0&&sg<8);auto& sigil=s.engine->sigils[sg];
            sigil.depth=32767;for(int j=0;j<SIGIL_POINTS;++j)sigil.set_point(j,22000);
            s.engine->reset_sigil(sg);
        }
        s.render(2048);ParamInspector inspector{};s.engine->capture_param_inspector(0,s.slot,D::P_FEEDBACK,inspector);
        CHECK(inspector.valid&&inspector.source==sources[i]&&inspector.base==12000);
        if(i!=8)CHECK(q.target==clamp_expected(12000+((int64_t(s.engine->sigils[mod_source_sigil(sources[i])].value)*q.mod_depth)>>15)));
        take(s,prefix+"delay_route_"+std::to_string(i),i==8?"env-route":"sigil-route");
        const std::string badge=i==8?"env":std::string(1,char('a'+i));
        CHECK(std::any_of(texts.begin(),texts.end(),[&](const TextRecord& r){return r.text==badge&&r.call_y==35;}));
        ++route_cases;
    }
    {Scene s(UnitKind::Delay);auto& u=unit(s);auto& q=u.params[D::P_FEEDBACK];
     q.base=12000;q.src=ModSource::SigilA;q.mod_depth=22000;q.mod_enabled=false;q.snap();
     auto& sig=s.engine->sigils[0];sig.depth=32767;for(int j=0;j<SIGIL_POINTS;++j)sig.set_point(j,32767);
     s.engine->reset_sigil(0);s.render(1024);CHECK(q.target==12000&&q.base==12000&&q.src==ModSource::SigilA);
     take(s,prefix+"delay_route_muted","muted-route");++route_cases;}
    {Scene s(UnitKind::Delay);auto& u=unit(s);auto& q=u.params[D::P_MIX];
     q.base=1000;q.src=ModSource::SigilA;q.mod_depth=-30000;q.mod_enabled=true;q.snap();
     auto& sig=s.engine->sigils[0];sig.depth=32767;for(int j=0;j<SIGIL_POINTS;++j)sig.set_point(j,32767);
     s.engine->reset_sigil(0);s.render(1024);CHECK(q.target==0&&q.base==1000);
     take(s,prefix+"delay_negative_clamp","negative-clamp");++route_cases;}
    {Scene s(UnitKind::Delay);auto& u=unit(s);auto& q=u.params[D::P_TIME];q.base=delay_time::free_value(8000);
     q.src=ModSource::SigilA;q.mod_depth=9000;q.mod_enabled=true;q.snap();
     auto& sig=s.engine->sigils[0];sig.depth=32767;for(int j=0;j<SIGIL_POINTS;++j)sig.set_point(j,-32767);
     s.engine->reset_sigil(0);s.render(1024);for(int j=0;j<SIGIL_POINTS;++j)sig.set_point(j,32767);
     s.render(32);CHECK(q.target>q.effective+1000 && q.base==delay_time::free_value(8000));
     take(s,prefix+"delay_tape_lag","tape-lag");++route_cases;
     s.set(D::P_MODE,32767);s.render(32);CHECK(q.target>q.effective);
     take(s,prefix+"delay_clean_lag","clean-lag");++route_cases;}
}
int main(const std::filesystem::path& dir) {
    CHECK(!std::filesystem::exists(dir));std::filesystem::create_directories(dir);
    h::output=dir;h::capture_files=true;h::manifest.open(dir/"surfaces.tsv");
    h::manifest<<"id\tkind\tmodule\tvariant\tsurface\tobjects\toutside_rects\toutside_lines\ttext_overlaps\traster_fnv64\n";
    gestures.open(dir/"gestures.tsv");gestures<<"case\tgroup\n";
    motion.open(dir/"motion.tsv");motion<<"theme\tframe\tid\tsigil_value\tactual_read_ready";
    for(const char* p:{"time","feedback","mix","tone","wow","sat"})
        motion<<'\t'<<p<<"_param\t"<<p<<"_base\t"<<p<<"_target\t"<<p<<"_effective\t"<<p<<"_expected_number";
    motion<<'\n';
    f::sample_fixture();CHECK(editor_style::init());iris_math();
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
        palette::set(theme);const std::string prefix=theme==palette::Theme::Graphite?"graphite_":"pink_";
        for(const auto& item:f::catalogue) {
            if(item.kind==UnitKind::Delay){Scene s(item.kind);take(s,prefix+"delay_default","default");}
            else h::state(item.kind,prefix,"default");
        }
        states(prefix);iris_idle(prefix);
        motion_trace(prefix);route_edges(prefix);discrete_routes(prefix);
        continuous(true,false);continuous(true,true);continuous(false,false);
        cancelled_owners();taps(true);taps(false);legacy_disabled();lock_regression(prefix);clear_regression(prefix);lens_regression(prefix);
    }
    editor_style::shutdown();CHECK(editor_gpu::textures.empty());h::manifest.close();gestures.close();motion.close();
    std::ofstream result(dir/"behavior.json");result<<"{\"status\":\"passed\",\"gesture_cases\":"<<gesture_cases
        <<",\"detached_delay_pairs\":"<<detached_pairs<<",\"composed_duals\":"<<h::duals<<",\"peak_objects\":"<<h::peak
        <<",\"motion_frames\":"<<motion_frames<<",\"route_cases\":"<<route_cases<<",\"physical_oracles\":"<<physical_oracles
        <<",\"iris_math_checks\":"<<iris_math_checks
        <<",\"old_suites_run\":0,\"lens_semantics\":\"held permanent modulation; not temporary audition\"}\n";
    std::printf("delay pair complete: %d composed duals, %d gesture cases, %d detached/poisoned pairs, peak %d/8192 objects\n",h::duals,gesture_cases,detached_pairs,h::peak);
    return 0;
}
}
int main(int argc,char**argv) {try {CHECK(argc==3);return delay_pair_cases::main(argv[2]);}
    catch(const std::exception& error){std::fprintf(stderr,"delay pair failed: %s\n",error.what());return 1;}}
