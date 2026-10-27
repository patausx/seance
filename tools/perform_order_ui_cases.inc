namespace order_cases {
int frozen_duals=0,order_max_top=0,order_max_bottom=0,order_max_shared=0;
std::vector<TextRecord> bottom_texts;
void begin_surface(int w,int h){CHECK(h==HEIGHT);width=w;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(w*HEIGHT,0xff000000u);}
void refresh(Fixture& f){f.refresh();performance_view_tick();}
void enter(Fixture& f){host_time_ms=1000;g_view=View::Performance;g_overlay=Overlay::None;g_drag=Drag::None;performance_view_reset();refresh(f);}
void advance(Fixture& f,u64 milliseconds){host_time_ms+=milliseconds;refresh(f);}
void tap(Fixture& f,int x,int y){osc_input_main::down(x,y);osc_input_main::up();refresh(f);}
void add(Fixture& f,int bank,int count){CHECK(f.engine->insert_pattern_chain_step(f.engine->pattern_chain_length,bank,count));refresh(f);}
std::vector<u32> render_frozen(Fixture& f,const char* path=nullptr,bool occlusion=false,bool publish=true){
    if(publish)refresh(f);
    const auto snapshot=g_ui_snapshot;
    const auto active=f.engine->active_pattern,position=f.engine->pattern_chain_position;
    const int changes=mutations;
    begin_surface(400,240);performance_view_draw_top();CHECK(!outside_rects&&!outside_lines&&!text_overlaps);CHECK(objects<C2D_DEFAULT_MAX_OBJECTS);const auto top=pixels;const auto top_texts=texts;const int top_objects=objects;
    begin_surface(320,240);performance_view_draw();CHECK(!outside_rects&&!outside_lines);if(!occlusion)CHECK(!text_overlaps);CHECK(objects<C2D_DEFAULT_MAX_OBJECTS);const auto bottom=pixels;bottom_texts=texts;
    order_max_top=std::max(order_max_top,top_objects);order_max_bottom=std::max(order_max_bottom,objects);order_max_shared=std::max(order_max_shared,top_objects+objects);
    CHECK(top_objects+objects<C2D_DEFAULT_MAX_OBJECTS);++frozen_duals;
    // Snapshot-driven: a repeated draw, even after changing live engine fields,
    // is pixel-identical on both displays and cannot commit anything.
    f.engine->active_pattern=(active+3)%NUM_PATTERNS;
    f.engine->pattern_chain_position=(position+2)%PATTERN_CHAIN_STEPS;
    const u64 captured_ms=host_time_ms;host_time_ms+=777; // draw must not extrapolate motion from live clock
    begin_surface(400,240);performance_view_draw_top();CHECK(pixels==top);
    begin_surface(320,240);performance_view_draw();CHECK(pixels==bottom);
    host_time_ms=captured_ms;
    CHECK(mutations==changes);CHECK(std::memcmp(&snapshot,&g_ui_snapshot,sizeof snapshot)==0);
    f.engine->active_pattern=active;f.engine->pattern_chain_position=position;
    if(path){std::filesystem::create_directories(path);width=400;pixels=top;texts=top_texts;dump_surface(path,"top");width=320;pixels=bottom;texts=bottom_texts;dump_surface(path,"bottom");}
    return bottom;
}
void tabs_and_ownership(){
    Fixture f;enter(f);add(f,0,2);add(f,1,3);CHECK(f.engine->resume_pattern_chain(0));refresh(f);
    tap(f,47,10);CHECK(order::active());CHECK(f.engine->pattern_chain_playing);
    tap(f,263,10);CHECK(transition::active());CHECK(!order::active());CHECK(f.engine->pattern_chain_playing);
    // Ordinary bank candidate selection and opening tabs never take over.
    transition::touch_down(80,39);transition::touch_drag(200,39);transition::touch_release(true);refresh(f);
    CHECK(f.engine->pattern_chain_playing);
    tap(f,160,133);CHECK(!f.engine->pattern_transition().active);CHECK(f.engine->pattern_chain_playing);
    tap(f,30,10);CHECK(order::active());CHECK(f.engine->pattern_chain_playing);
    tap(f,200,223);CHECK(f.engine->pattern_chain_paused);CHECK(!f.engine->pattern_chain_playing);
    const int pattern=f.engine->active_pattern;
    tap(f,264,10);tap(f,30,10);CHECK(f.engine->pattern_chain_paused);CHECK(f.engine->active_pattern==pattern);
    render_frozen(f);
}
void editing_and_cancel(){
    Fixture f;enter(f);add(f,0,2);add(f,0,4);add(f,2,6);tap(f,42,10);
    tap(f,18,223);CHECK(f.engine->pattern_chain_length==4);CHECK(mutations==1);
    // Current selection is now card 4. Choose another bank through card 1.
    tap(f,35,86);tap(f,16+3*74+20,90);CHECK(f.engine->pattern_chain[0].pattern==3);CHECK(mutations==2);
    osc_input_main::down(85,82);osc_input_main::move(121,82);CHECK(f.engine->pattern_chain[0].bars==2);osc_input_main::up();refresh(f);
    CHECK(f.engine->pattern_chain[0].bars==5);CHECK(mutations==3);
    osc_input_main::down(85,82);osc_input_main::move(121,82);performance_view_touch_release(false);osc_input_main::up();refresh(f);CHECK(f.engine->pattern_chain[0].bars==5);
    // Exactly one reorder on a true lift, and duplicate banks remain distinct.
    osc_input_main::down(143,60);osc_input_main::move(300,140);CHECK(f.engine->pattern_chain[0].pattern==3);osc_input_main::up();refresh(f);
    CHECK(f.engine->pattern_chain[3].pattern==3);CHECK(mutations==4);advance(f,200); // let accepted visual landing finish before a distinct edit
    // Modal bank chooser cannot accidentally activate footer commands.
    tap(f,35,86);tap(f,260,10);CHECK(order::active()&&!transition::active());CHECK(!f.engine->pattern_chain_playing);tap(f,230,223);
    const int count=f.engine->pattern_chain_length;
    osc_input_main::down(15,223);performance_view_buttons(KEY_START,KEY_START,0);osc_input_main::up();refresh(f);CHECK(f.engine->pattern_chain_length==count);
    performance_view_buttons(0,0,KEY_START);
    tap(f,20,223);CHECK(f.engine->pattern_chain_length==count+1);
    tap(f,305,33);render_frozen(f);
}
void current_hold_next_takeover(){
    Fixture f;enter(f);add(f,0,2);add(f,1,4);add(f,1,6);tap(f,40,10);tap(f,200,223);
    CHECK(f.engine->pattern_chain_playing);tap(f,125,110);CHECK(f.engine->pattern_chain_held);
    // Editing current banks/bars saves only its next visit.
    const int current=f.engine->pattern_chain_current_pattern;
    tap(f,30,86);tap(f,16+3*74+20,90);CHECK(f.engine->pattern_chain_current_pattern==current);
    tap(f,280,223);CHECK(f.engine->pattern_chain_next_pending);
    render_frozen(f);
    // A cannot mute/trigger while order owns the local page; L cancels edits.
    const int changes=mutations;performance_view_buttons(KEY_A,KEY_A,0);CHECK(mutations==changes);
    tap(f,260,10);CHECK(transition::active());CHECK(f.engine->pattern_chain_playing);
    tap(f,160,103);CHECK(!f.engine->pattern_chain_playing);CHECK(f.engine->pattern_chain_paused);
    render_frozen(f);
}
void removal_and_capacity(){
    Fixture f;enter(f);for(int i=0;i<16;++i)add(f,i%8,i%16+1);tap(f,40,10);
    tap(f,20,223);CHECK(f.engine->pattern_chain_length==16);CHECK(mutations==0);
    CHECK(f.engine->resume_pattern_chain(0));refresh(f);
    tap(f,30,86);tap(f,80,223);CHECK(f.engine->pattern_chain_length==16);CHECK(mutations==0);
    tap(f,230,223);tap(f,185,86);tap(f,80,223);CHECK(f.engine->pattern_chain_length==15);CHECK(mutations==1);
    // Page stays clamped after edits, bank 8 / 16 bars glyphs render safely.
    osc_input_main::down(143,60);osc_input_main::move(305,33);g_ui_frame+=24;osc_input_main::move(305,33);
    osc_input_main::move(300,140);osc_input_main::up();refresh(f);
    CHECK(f.engine->pattern_chain_position==11);CHECK(f.engine->pattern_chain[11].pattern==0);CHECK(mutations==2);
    tap(f,305,33);tap(f,305,33);tap(f,305,33);render_frozen(f);
}
void latched_next_and_run_intent(){
    Fixture f;enter(f);add(f,0,1);add(f,1,4);add(f,1,6);tap(f,40,10);
    CHECK(f.engine->resume_pattern_chain(0));CHECK(f.engine->next_pattern_chain());
    CHECK(f.engine->set_pattern_chain_step(1,6,16));refresh(f);
    CHECK(g_ui_snapshot.pattern_chain_next_position==1&&g_ui_snapshot.pattern_chain_next_pattern==1&&g_ui_snapshot.pattern_chain_next_bars==4);
    begin_surface(320,240);performance_view_draw();const auto edited=pixels;
    bool annotation=false,duration=false;
    for(const auto& t:texts){annotation|=t.text=="next7"&&t.call_x==174;duration|=t.text=="4>16 bars"&&t.call_x==232;}
    CHECK(annotation&&duration);
    const auto snapshot=g_ui_snapshot;
    g_ui_snapshot.pattern_chain_steps[1].pattern=1;g_ui_snapshot.pattern_chain_steps[1].bars=4;
    begin_surface(320,240);performance_view_draw();
    // Only the saved-visit annotation may change: the next glyph and ruler
    // remain exact to the accepted bank2/4, never the newly saved bank7/16.
    for(int y=74;y<108;++y)for(int x=174;x<219;++x)CHECK(pixels[y*320+x]==edited[y*320+x]);
    for(int y=94;y<97;++y)for(int x=232;x<294;++x)CHECK(pixels[y*320+x]==edited[y*320+x]);
    g_ui_snapshot=snapshot;
    CHECK(f.engine->move_pattern_chain_step(1,2));refresh(f);
    CHECK(g_ui_snapshot.pattern_chain_next_position==2);render_frozen(f);
    f.engine->stop_pattern_chain();CHECK(f.engine->set_pattern_chain_length(1));
    f.engine->set_bpm(300);f.engine->pattern_chain_loop=false;f.engine->transport_play();
    CHECK(f.engine->resume_pattern_chain(0));refresh(f);
    std::array<q15,512> audio{};
    // render() takes stereo frames, not interleaved sample count.
    for(int i=0;i<200&&g_ui_snapshot.pattern_chain_start_pending;++i){f.engine->render(audio.data(),audio.size()/2);refresh(f);}
    CHECK(f.engine->pattern_chain_playing&&!g_ui_snapshot.pattern_chain_start_pending);
    osc_input_main::down(200,223); // pause intent, held across natural order end
    for(int i=0;i<200&&f.engine->pattern_chain_playing;++i){f.engine->render(audio.data(),audio.size()/2);refresh(f);}
    CHECK(!f.engine->pattern_chain_playing);osc_input_main::up();refresh(f);
    CHECK(!f.engine->pattern_chain_playing&&!g_ui_snapshot.pattern_chain_start_pending&&f.engine->transport_playing);
    osc_input_main::down(200,223); // start intent cannot turn into a later pause
    CHECK(f.engine->resume_pattern_chain(0));refresh(f);osc_input_main::up();refresh(f);
    CHECK(f.engine->pattern_chain_playing);render_frozen(f);
}
void fx_across_auto_bank(){
    for(int pad=0;pad<3;++pad){
        Fixture f;enter(f);add(f,0,1);add(f,1,1);f.engine->set_bpm(300);
        CHECK(f.engine->resume_pattern_chain(0));f.engine->transport_play();refresh(f);
        std::array<q15,512> audio{};
        for(int i=0;i<200&&g_ui_snapshot.pattern_chain_start_pending;++i){f.engine->render(audio.data(),audio.size()/2);refresh(f);}
        CHECK(!g_ui_snapshot.pattern_chain_start_pending);tap(f,264,10);
        osc_input_main::down(56+104*pad,185);const auto fx=f.engine->transition_fx();
        CHECK(fx.kind==TransitionFxKind(pad+1));const int bank=f.engine->active_pattern;
        for(int i=0;i<200&&f.engine->active_pattern==bank;++i){f.engine->render(audio.data(),audio.size()/2);refresh(f);}
        CHECK(f.engine->active_pattern!=bank&&f.engine->pattern_chain_playing);
        CHECK(f.engine->transition_fx().kind==fx.kind&&f.engine->transition_fx().epoch==fx.epoch);
        osc_input_main::move(75+104*pad,185);CHECK(f.engine->transition_fx().kind==fx.kind);
        osc_input_main::up();refresh(f);CHECK(f.engine->transition_fx().kind==TransitionFxKind::None);
        render_frozen(f);
    }
}
#include "order_visual_ui_cases.inc"
void motion_math(){
    using order_motion::Point;
    const auto near=[](float a,float b){CHECK(std::isfinite(a)&&std::abs(a-b)<.0005f);};
    for(unsigned duration:{150u,180u}){
        Point direct,irregular;direct.snap(8,129);irregular.snap(8,129);
        direct.aim(164,51,1000,duration);irregular.aim(164,51,1000,duration);
        float previous_x=8,previous_y=129;
        for(unsigned elapsed:{0u,1u,7u,31u,60u,89u,149u,150u,179u,180u,999u}){
            irregular.sample(1000+elapsed);const float t=std::min(1.f,float(elapsed)/duration),u=1.f-t,eased=1.f-u*u*u;
            near(irregular.x,8+156*eased);near(irregular.y,129-78*eased);
            CHECK(irregular.x>=previous_x&&irregular.y<=previous_y);previous_x=irregular.x;previous_y=irregular.y;
            direct.sample(1000+elapsed);near(irregular.x,direct.x);near(irregular.y,direct.y);
        }
        CHECK(!irregular.active&&irregular.ready);near(irregular.x,164);near(irregular.y,51);
        Point once;once.snap(8,129);once.aim(164,51,1000,duration);once.sample(1089);
        Point cadence;cadence.snap(8,129);cadence.aim(164,51,1000,duration);
        for(u64 now=1000;now<=1089;++now)cadence.aim(164,51,now,duration);
        near(once.x,cadence.x);near(once.y,cadence.y);CHECK(cadence.since==1000); // no aim restart
    }
    Point retarget;retarget.snap(8,51);retarget.aim(164,129,1000,150);retarget.sample(1060);
    const float x=retarget.x,y=retarget.y;retarget.aim(8,129,1060,180);near(retarget.x,x);near(retarget.y,y);
    retarget.sample(1120);const float ease=1.f-std::pow(2.f/3.f,3.f);near(retarget.x,x+(8-x)*ease);near(retarget.y,y+(129-y)*ease);
    retarget.sample(1240);CHECK(!retarget.active);near(retarget.x,8);near(retarget.y,129);
    Point zero;zero.aim(164,51,800,150);CHECK(zero.ready&&!zero.active);near(zero.x,164);
    zero.aim(8,129,900,0);CHECK(!zero.active);near(zero.x,8);near(zero.y,129);
    zero.aim(164,51,1000,150);zero.sample(999);CHECK(!zero.active);near(zero.x,164);near(zero.y,51); // backward clock snaps safely
    zero.aim(8,129,~u64(0)-80,150);zero.sample(~u64(0)-1);CHECK(zero.active&&zero.x>=8&&zero.x<=164);
    zero.sample(10);CHECK(!zero.active);near(zero.x,8);near(zero.y,129); // wrapped clock has no underflow/NaN
}
void new_page_stale_dto(){
    for(int count:{4,8,12}){Fixture f;enter(f);for(int i=0;i<count;++i)add(f,i%8,i%16+1);tap(f,40,10);
        f.engine->active_pattern=6;f.refresh();const auto old=g_ui_snapshot;
        osc_input_main::down(20,223);osc_input_main::up();CHECK(f.engine->pattern_chain_length==count+1&&mutations==1);
        CHECK(std::memcmp(&old,&g_ui_snapshot,sizeof old)==0);
        ++g_ui_frame;performance_view_tick(); // actual main order: UI tick, THEN publish audio DTO
        CHECK(std::memcmp(&old,&g_ui_snapshot,sizeof old)==0);
        f.refresh();performance_view_tick();const auto image=render_frozen(f);
        char number[4];std::snprintf(number,sizeof number,"%02d",count+1);CHECK(label(number)==std::make_pair(18,59));
        solid(image,8,51,148,1,p::ink());numeric_mask(image,37,74,6,p::bg(),p::ink());
        tap(f,200,223);CHECK(f.engine->pattern_chain_position==count&&f.engine->pattern_chain_current_pattern==6);
    }
}
void captures(const std::string& base){
    for(int theme=0;theme<tc::kCount;++theme){use_theme(theme);const std::string root=base+"/"+theme_names[theme];
        Fixture f;enter(f);tap(f,40,10);render_frozen(f,(root+"/empty").c_str());
        add(f,0,4);add(f,1,5);add(f,2,8);add(f,3,2);
        std::filesystem::create_directories(root);std::ofstream timing(root+"/timings.tsv");CHECK(timing.good());timing<<"state\tvirtual_ms\tui_frame\tintentional_card_occlusion\n";
        const auto shot=[&](const char* name,bool moving=false){timing<<name<<'\t'<<host_time_ms<<'\t'<<g_ui_frame<<'\t'<<moving<<'\n';return render_frozen(f,(root+"/"+name).c_str(),moving);};
        int frame=0;const auto motion=[&](){char name[40];std::snprintf(name,sizeof name,"motion-%03d",frame++);shot(name,true);};
        shot("idle");tap(f,174,139);shot("selected");
        tap(f,185,164);shot("chooser");osc_input_main::down(16+2*74+30,127+20);shot("chooser-pressed");
        performance_view_touch_release(false);osc_input_main::up();tap(f,230,223);
        const auto before=chain_state(f);osc_input_main::down(180,139);shot("drag-before");motion();
        for(int i=1;i<=12;++i){host_time_ms+=20;osc_input_main::move(180,139-68*i/12);refresh(f);motion();}
        advance(f,60);shot("drag-middle",true);motion();
        advance(f,140);shot("drag-preview",true);motion();CHECK(chain_state(f)==before);
        osc_input_main::up();refresh(f);shot("drop-start",true);motion();CHECK(mutations==1);
        for(int i=1;i<=10;++i){advance(f,20);motion();if(i==3)shot("drop-middle",true);}
        shot("drop-settled");
        const auto placed=chain_state(f);osc_input_main::down(180,61);osc_input_main::move(180,139);advance(f,80);shot("cancel-before",true);motion();
        performance_view_touch_release(false);osc_input_main::up();refresh(f);
        for(int i=1;i<=10;++i){advance(f,20);motion();}shot("cancel-return");CHECK(chain_state(f)==placed);
        f.engine->transport_play();CHECK(f.engine->resume_pattern_chain(0));refresh(f);shot("armed");
        std::array<q15,512> audio{};for(int i=0;i<600&&g_ui_snapshot.pattern_chain_start_pending;++i){f.engine->render(audio.data(),audio.size()/2);refresh(f);}
        CHECK(!g_ui_snapshot.pattern_chain_start_pending);shot("running");
        tap(f,125,110);shot("held");tap(f,200,223);shot("paused");
        tap(f,264,10);shot("banks");tap(f,160,10);shot("perform");
    }
}
}
int main(int argc,char** argv){
    try{theme_test::select();
        char name[]="order-ui",perf[]="--performance-only",banks[]="--transition-only";
        char* legacy[]={name,perf};setenv("SEANCE_PERFORMANCE_MIXER_ONLY","1",1);
        CHECK(existing_sequencer_test_main(2,legacy)==0);unsetenv("SEANCE_PERFORMANCE_MIXER_ONLY");
        legacy[1]=banks;CHECK(existing_sequencer_test_main(2,legacy)==0);
        order_cases::tabs_and_ownership();std::puts("PASS tabs / ownership / frozen draw");
        order_cases::editing_and_cancel();std::puts("PASS edits / lift / cancel / reorder");
        order_cases::current_hold_next_takeover();std::puts("PASS hold / next / takeover");
        order_cases::removal_and_capacity();std::puts("PASS protect / capacity / pages");
        order_cases::latched_next_and_run_intent();std::puts("PASS queued display / remap / down-intent across end");
        order_cases::fx_across_auto_bank();std::puts("PASS all three held FX across auto bank advance");
        order_cases::pixel_roles_and_chooser();std::puts("PASS all five theme roles / exact gothic digit pixels / chooser press+cancel+ownership");
        order_cases::header_drag_and_live_pixels();std::puts("PASS 15 header anchors / strict six-pixel threshold / grab offset / true-lift once");
        order_cases::reflow_and_settle();std::puts("PASS all five themes / controlled-ms neighbor reflow / frozen midpoint / settle");
        order_cases::cancel_regrab_and_interference();std::puts("PASS seven invalid/cancel/interference routes / return / rapid regrab");
        order_cases::pages_identity_and_external_edits();std::puts("PASS page dwell / current+queued card identity / external edit cancellation");
        order_cases::raw_reorder_publication();std::puts("PASS raw post-lift pre-DTO bridge / current+queued remap / external DTO boundary");
        order_cases::motion_math();std::puts("PASS Point elapsed-ms cubic / cadence / retarget / zero / backwards+wrapped clocks");
        order_cases::new_page_stale_dto();std::puts("PASS fifth/ninth/thirteenth add selection across main tick-before-DTO publication");
        if(argc==2)order_cases::captures(argv[1]);
        std::printf("order visual UI: 14 focused groups; %d frozen duals; max top %d bottom %d shared %d / 8192; five themes\n",order_cases::frozen_duals,order_cases::order_max_top,order_cases::order_max_bottom,order_cases::order_max_shared);
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL order UI: %s\n",e.what());return 1;}
    return 0;
}
