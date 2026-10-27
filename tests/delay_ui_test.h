// Included after the shared fixture in sequencer_test.cpp. Actual UI, no copied controller.
using Delay = DelayUnit;
q15 delay_test_time(int milliseconds) {
    // Choose the first raw value giving the requested whole-frame legacy delay.
    return q15(((int64_t(milliseconds)*32-32)*32768+31966)/31967);
}
std::array<q15,6> delay_test_bases(const Unit& u) {
    std::array<q15,6> result{};for(int p=0;p<6;++p)result[p]=u.params[p].base;return result;
}
void delay_test_release() {delay_focus_release();g_drag=Drag::None;}
void delay_test_audio(DelayUnit& u) {
    // Real silence processing only to populate the existing last-read diagnostics.
    // No DSP oracle, PCM capture, or synthetic audio implementation in this UI suite.
    u.control_tick();q15 silence[128]={};
    for(int block=0;block<16;++block){std::fill(std::begin(silence),std::end(silence),0);u.process(silence,128);}
}
void test_delay_controls() {
    ModuleInspectorFixture f(UnitKind::Delay);auto& u=*f.module;
    CHECK(u.num_params==6&&module_inspector_supports(UnitKind::Delay));
    struct Box {int x,y,w,h,p;};
    const Box boxes[]={{8,26,144,44,0},{168,26,145,44,1},{8,180,70,28,2},
                       {160,180,70,28,3},{236,180,70,28,4},{184,0,56,25,5}};
    for(int value:{0,17000,32767})for(const auto& b:boxes)for(int corner=0;corner<4;++corner) {
        u.params[b.p].base=q15(value);const auto before=delay_test_bases(u);const int dirty=mutations;
        const int x=b.x+(corner&1?b.w-1:0),y=b.y+(corner&2?b.h-1:0);
        CHECK(f.down(x,y));CHECK(delay_focus_selected()==b.p&&delay_test_bases(u)==before);
        f.drag(x,y);CHECK(delay_test_bases(u)==before&&mutations==dirty);delay_test_release();
    }
    for(int p=0;p<6;++p) {
        u.params[p].base=13000;const auto xy=f.point(p);f.down(xy.first,xy.second);
        f.drag(xy.first+6,xy.second+20);
        CHECK(u.params[p].base==(p==5?13000:13000+6*32767/144));
        if(p==5){f.drag(xy.first+9,xy.second);CHECK(u.params[p].base==32767);f.drag(xy.first-9,xy.second);CHECK(u.params[p].base==0);}
        delay_test_release();
    }
    const auto before=delay_test_bases(u);const int dirty=mutations;
    // Tone is fixed, the echo diagram/static labels have no hidden controls.
    for(auto xy:{std::pair<int,int>{84,180},{153,207},{20,100},{150,120},{280,160},{155,48},{20,218}}) {
        CHECK(f.down(xy.first,xy.second));f.drag(xy.first+24,xy.second-18);
        CHECK(delay_test_bases(u)==before&&mutations==dirty);delay_test_release();
    }
    for(int reason=0;reason<3;++reason) {
        const auto xy=f.point(0);f.down(xy.first,xy.second);const auto saved=delay_test_bases(u);
        if(reason==0)g_edit_slot=1;else if(reason==1)g_overlay=Overlay::None;else delay_test_release();
        f.drag(xy.first+30,xy.second);CHECK(delay_test_bases(u)==saved);
        g_edit_slot=0;g_overlay=Overlay::Editor;delay_test_release();
    }
    const bool bypass=u.bypass;g_seq_lock_armed=true;f.down(260,10);CHECK(u.bypass==bypass);
    g_seq_lock_armed=false;f.down(260,10);CHECK(u.bypass!=bypass);delay_test_release();
    delay_focus_select(4);delay_focus_select(-1);delay_focus_select(6);CHECK(delay_focus_selected()==4);
}
void test_delay_locks() {
    ModuleInspectorFixture f(UnitKind::Delay);auto& u=*f.module;
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    CHECK(f.engine->set_step_lock(0,7,0,1,4000));const auto before=delay_test_bases(u);
    CHECK(f.down(200,30));CHECK(seq_display_value(0,1,u.params[1].base)==4000); // labels never delete
    f.drag(206,30);CHECK(seq_display_value(0,1,u.params[1].base)==4000+6*32767/144);
    CHECK(delay_test_bases(u)==before);const auto locked=f.hash();g_seq_lock_step=8;
    f.drag(220,30);CHECK(f.hash()==locked);g_seq_lock_step=7;delay_test_release();
    f.drag(230,30);CHECK(f.hash()==locked);
    CHECK(f.down(20,215));CHECK(seq_display_value(0,1,u.params[1].base)==u.params[1].base);
    CHECK(delay_test_bases(u)==before);delay_test_release();
    f.engine->clear_track(0);delete f.engine->replace_unit(0,1,new FmUnit);
    for(int n=0;n<SEQ_LOCKS;++n)CHECK(f.engine->set_step_lock(0,7,1,n,q15(5000+n)));
    const auto full=f.hash();f.down(200,30);f.drag(210,30);
    CHECK(f.hash()==full&&delay_test_bases(u)==before&&g_seq_lock_flash>0);
    CHECK(!param_lens_touch_down(250,232));delay_test_release();
    g_view=View::Chains;f.capture();module_inspector_top();CHECK(module_inspector_text("full"));
}
void test_delay_lens_temp() {
    ModuleInspectorFixture f(UnitKind::Delay);auto& u=*f.module;
    for(int selected=0;selected<6;++selected) {
        f.pick(selected);f.open();auto& p=u.params[selected];p.base=13000;p.mod_depth=1000;p.snap();
        int dirty=mutations;CHECK(param_lens_touch_down(250,129));CHECK(p.base==13000&&mutations==dirty);
        CHECK(param_lens_touch_drag(256,129));CHECK(p.base==13000+6*32767/271);param_lens_touch_release();
        dirty=mutations;CHECK(param_lens_touch_down(100,166));CHECK(p.mod_depth==1000&&mutations==dirty);
        param_lens_touch_drag(106,166);CHECK(p.mod_depth==1000+6*65534/271);param_lens_touch_release();
        lens_permanent_probe(u,selected);CHECK(delay_focus_selected()==selected);f.open();
        u32 close=KEY_B|KEY_X;CHECK(param_lens_input(close,0,0,false)&&close==0&&!param_lens_is_open());
        CHECK(delay_focus_selected()==selected&&g_overlay==Overlay::Editor);f.open();
        CHECK(delay_focus_selected()==selected);param_lens_cancel();
    }
    f.pick(1);f.open();const auto before=delay_test_bases(u);const int dirty=mutations;
    lens_quick_begin();g_edit_slot=1;u32 down=KEY_X;
    CHECK(param_lens_input(down,KEY_L,0,false)&&down==0&&!param_lens_is_open());
    CHECK(!f.engine->param_temp_active()&&delay_test_bases(u)==before&&mutations==dirty);g_edit_slot=0;
}
void test_delay_units_frames() {
    namespace model=delay_echo_model;
    CHECK(model::X0==24&&model::X1==304&&model::SPAN_FRAMES==51200&&model::PACKET_FRAMES==2304);
    CHECK(model::time_frames(0)==32&&model::time_frames(32767)==31998&&model::time_frames(-1)==32);
    for(int samples:{0,5760,13440,51200})CHECK(std::abs(model::onset_q4(samples)-samples*4480.0/51200.0)<=0.5);
    // A few independent scalar packet oracle cells; one common normalization, not per echo.
    const auto packet=[](double t) {
        if(t<0||t>72)return 0.0;
        const double x=t/72;return std::sin(6*3.14159265358979323846*x)*std::exp(-3*x)*std::min(1.0,24*x);
    };
    double norm=0;for(int i=0;i<=1152;++i)norm=std::max(norm,std::abs(packet(i/16.0)));
    for(auto cell:{std::pair<int,int>{0,0},{0,4},{7,3},{15,14}}) {
        double low=1,high=-1;
        for(int j=0;j<9;++j){const double v=packet((cell.second+j/8.0-.5-cell.first/16.0)*1600/280)/norm;low=std::min(low,v);high=std::max(high,v);}
        CHECK(std::abs(model::PACKET[cell.first][cell.second][0]-std::round(low*32767))<=1);
        CHECK(std::abs(model::PACKET[cell.first][cell.second][1]-std::round(high*32767))<=1);
    }
    for(int raw:{0,8192,32767}) {
        char out[32];module_inspector_value_text(out,sizeof out,UnitKind::Delay,0,q15(raw));char* end=nullptr;
        const double value=std::strtod(out,&end),expected=(32+((int64_t(raw)*31967)>>15))/32.0;
        CHECK(end&&std::string(end)=="ms"&&value==std::floor(expected+0.5));
    }
    ModuleInspectorFixture f(UnitKind::Delay);auto& u=*static_cast<DelayUnit*>(f.module);
    u.params[0].base=13000;u.params[0].effective=8000;u.params[0].src=ModSource::SigilA;
    u.params[0].mod_depth=10000;f.engine->sigils[0].value=16384;delay_focus_select(0);delay_test_audio(u);f.capture();
    ParamInspector detail;f.engine->capture_param_inspector(0,0,0,detail);
    CHECK(detail.control!=detail.target&&detail.target!=detail.effective&&detail.control!=detail.effective);
    module_inspector_top();module_inspector_detail(detail);CHECK(module_inspector_text("effective"));
    char read[32];std::snprintf(read,sizeof read,"applied ~%ums",unsigned((u.visual_delay_frames()+16)/32));
    CHECK(module_inspector_text(read));u.bypass=true;f.capture();module_inspector_top();CHECK(module_inspector_text("applied --"));
    u.bypass=false;u.params[0].src=ModSource::None;u.params[0].mod_depth=0;
    for(int time:{0,int(delay_test_time(420)),32767})for(int mix:{0,32767}) {
        for(int p=0;p<6;++p){u.params[p].base=0;u.params[p].snap();}
        u.params[0].base=q15(time);u.params[1].base=32767;u.params[2].base=q15(mix);
        for(int p=0;p<6;++p){u.params[p].snap();}delay_focus_select(mix?1:0);f.capture();
        const auto top=module_inspector_top();const int top_count=objects;
        CHECK(module_inspector_top(false,true)==top);
        module_inspector_surface(false);{ModuleInspectorNoEngine detached;f.lower();}module_inspector_geometry();
        const auto lower=pixels;const int bottom_count=objects;CHECK(top_count+bottom_count<=8192);
        maximum_objects=std::max(maximum_objects,bottom_count);
        maximum_shared_objects=std::max(maximum_shared_objects,top_count+bottom_count);
        CHECK(module_inspector_text("same scale / 1.6s")&&module_inspector_text("model"));
        // Captures are immutable: no engine, audio, live params or global snapshot during either draw.
        const auto bases=delay_test_bases(u);for(int p=0;p<6;++p)u.params[p].base=1234;g_ui_snapshot={};
        CHECK(module_inspector_top()==top);module_inspector_surface(false);
        {ModuleInspectorNoEngine detached;f.lower();}CHECK(pixels==lower&&objects==bottom_count);
        for(int p=0;p<6;++p)u.params[p].base=bases[p];
    }
    // The response uses the edited base, not an animated/live effective control value.
    u.params[0].base=delay_test_time(420);u.params[0].effective=1000;f.capture();
    module_inspector_surface(false);f.lower();const auto frozen_model=pixels;
    u.params[0].effective=30000;++g_ui_frame;f.capture();module_inspector_surface(false);f.lower();CHECK(pixels==frozen_model);
    g_overlay=Overlay::None;f.capture();module_inspector_surface(false);f.lower();CHECK(texts.empty());
}
void test_delay_modern() {
    ModuleInspectorFixture f(UnitKind::Delay,true);auto& u=*static_cast<DelayUnit*>(f.module);
    CHECK(u.modern()&&u.num_params==11&&delay_focus_modern());
    for(int p=0;p<11;++p){u.params[p].base=0;u.params[p].snap();}
    f.capture();
    for(auto pc:{std::pair<int,int>{6,110},{7,50},{10,180}}) {
        const int p=pc.first,x=pc.second,y=p==6?194:215;
        f.down(x,y);CHECK(delay_focus_selected()==p&&u.params[p].base==0);
        f.drag(x+10,y);CHECK(u.params[p].base==(p==6?10*32767/144:32767));delay_test_release();
    }
    f.down(50,48);CHECK(delay_focus_selected()==Delay::P_DIV);
    f.drag(68,48);CHECK(u.params[Delay::P_DIV].base==18*32767/144);delay_test_release();
    f.down(125,31);CHECK(delay_focus_selected()==Delay::P_FEEL);
    f.drag(145,31);CHECK(u.params[Delay::P_FEEL].base==20*32767/48);delay_test_release();
    // Mode/version stays with selected mod; every new parameter keeps relative origin.
    for(int p=6;p<11;++p) {
        delay_focus_select(p);f.open();u.params[p].base=14000;u.params[p].snap();
        const int dirty=mutations;param_lens_touch_down(180,129);CHECK(u.params[p].base==14000);
        param_lens_touch_drag(186,129);CHECK(u.params[p].base==14000+6*32767/271);param_lens_touch_release();
        CHECK(mutations==dirty+1);lens_permanent_probe(u,p);
        CHECK(delay_focus_selected()==p);f.capture();
    }
    // Clear: no mutation on touchdown or early release, exit cancels, deliberate hold requests once.
    g_seq_lock_armed=false;g_ui_frame=100;f.down(280,215);f.drag(280,215);CHECK(!u.visual_state().clearing);
    g_ui_frame=144;f.drag(280,215);CHECK(!u.visual_state().clearing);delay_test_release();
    f.down(280,215);g_ui_frame+=45;f.drag(250,215);CHECK(!u.visual_state().clearing);
    f.down(280,215);const int dirty=mutations;g_ui_frame+=45;f.drag(280,215);CHECK(u.visual_state().clearing&&mutations==dirty);
    delay_test_audio(u);CHECK(!u.visual_state().clearing);
    // All appended controls can be p-locked; clear audio never works from a lock edit.
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    for(auto pp:{std::pair<int,int>{6,110},{7,122},{10,180}}) {
        const int p=pp.first,y=p==6?194:215;const auto base=u.params[p].base;
        CHECK(f.engine->set_step_lock(0,7,0,p,7000));f.down(pp.second,y);f.drag(pp.second+10,y);
        CHECK(seq_display_value(0,p,base)==(p==6?7000+10*32767/144:32767)&&u.params[p].base==base);
        f.down(20,215);CHECK(seq_display_value(0,p,base)==base);delay_test_release();
    }
    f.down(280,215);g_ui_frame+=60;f.drag(280,215);CHECK(!u.visual_state().clearing);
    g_seq_lock_armed=false;g_view=View::Chains;
    // Four seconds is visibly on the larger fixed scale, not silently cropped.
    u.params[0].base=32767;u.params[7].base=0;u.params[2].base=16000;
    for(int p=0;p<11;++p){u.params[p].snap();}delay_test_audio(u);delay_focus_select(0);f.capture();
    const auto top=module_inspector_top();const int top_count=objects;
    CHECK(module_inspector_text("4000ms"));module_inspector_surface(false);f.lower();module_inspector_geometry();
    CHECK(module_inspector_text("scale / 6.4s")&&module_inspector_text("4000ms"));
    const auto lower=pixels;const int bottom_count=objects;CHECK(top_count+bottom_count<=8192);
    maximum_shared_objects=std::max(maximum_shared_objects,top_count+bottom_count);
    {ModuleInspectorNoEngine noengine;module_inspector_surface(false);f.lower();CHECK(pixels==lower);}
    u.params[0].base=0;CHECK(module_inspector_top()==top);
    for(int feel=0;feel<3;++feel) {
        u.params[7].base=32767;u.params[8].base=delay_time::division_value(7);u.params[9].base=delay_time::feel_value(feel);
        for(int p=0;p<11;++p){u.params[p].snap();}u.set_clock_bpm_q16(20*65536);delay_test_audio(u);f.capture();
        module_inspector_top();CHECK(module_inspector_text("limited to 4s"));
        module_inspector_surface(false);f.lower();module_inspector_geometry();CHECK(module_inspector_text("4000ms!"));
    }
}
void dump_delay_runtime(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tparam\ttime_raw\tfeedback_raw\tmix_raw\ttop_objects\tbottom_objects\tshared_objects\tbottom_clipped\tbottom_overlaps\n";
    ModuleInspectorFixture f(UnitKind::Delay,true);auto& u=*static_cast<DelayUnit*>(f.module);
    auto configure=[&](int ms,int feedback,int mix) {
        const q15 values[]={delay_time::free_value(ms*32),q15(feedback*32767/100),q15(mix*32767/100),0,0,0,
                            32767,0,delay_time::division_value(3),0,32767};
        for(int p=0;p<u.num_params;++p){u.params[p].base=values[p];u.params[p].snap();}delay_test_audio(u);
    };
    auto save=[&](const std::string& name,bool lens=false) {
        f.capture();module_inspector_top(lens);const int top=objects;dump_surface(directory,name+"_top");
        module_inspector_surface(false);{ModuleInspectorNoEngine detached;if(lens)param_lens_draw_bottom();else f.lower();}
        module_inspector_geometry();CHECK(top+objects<=8192);dump_surface(directory,name+"_bottom");
        manifest<<name<<'\t'<<delay_focus_selected()<<'\t'<<u.params[0].base<<'\t'<<u.params[1].base<<'\t'<<u.params[2].base
                <<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_rects+outside_lines<<'\t'<<text_overlaps<<'\n';
    };
    configure(180,58,50);delay_focus_select(0);save("01_short");
    configure(420,58,50);save("02_long");
    configure(420,84,50);delay_focus_select(1);save("03_feedback");
    u.params[5].base=32767;u.params[5].snap();delay_test_audio(u);delay_focus_select(5);save("04_hold");
    configure(1,100,100);delay_focus_select(0);save("05_dense_min");
    u.params[0].base=32767;u.params[0].snap();delay_test_audio(u);save("06_max");
    configure(420,58,50);delay_focus_select(0);g_seq_lock_armed=true;g_seq_lock_step=7;
    CHECK(f.engine->set_step_lock(0,7,0,0,delay_time::free_value(180*32)));save("07_lock");g_seq_lock_armed=false;
    f.open();save("08_mod",true);lens_quick_begin();param_lens_touch_down(160,129);param_lens_touch_drag(190,129);
    save("09_temp",true);lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();delay_test_release();
    configure(420,58,50);u.params[7].base=32767;u.params[8].base=delay_time::division_value(4);
    for(int feel=0;feel<3;++feel) {
        u.params[9].base=delay_time::feel_value(feel);for(int p=0;p<11;++p){u.params[p].snap();}
        delay_focus_select(feel?9:8);delay_test_audio(u);
        save(feel==0?"10_sync_straight":feel==1?"11_sync_dotted":"12_sync_triplet");
    }
    u.set_clock_bpm_q16(20*65536);u.params[8].base=delay_time::division_value(7);u.params[8].snap();delay_test_audio(u);
    save("13_sync_limited");u.set_clock_bpm_q16(120*65536);
    configure(180,58,50);u.params[0].base=delay_time::free_value(420*32);u.params[0].snap();u.control_tick();
    q15 silence[320]={};u.process(silence,320);delay_focus_select(10);save("14_clean_transition");
    delay_test_audio(u);f.down(280,215);g_ui_frame+=30;f.drag(280,215);save("15_clear_armed");
    g_ui_frame+=15;f.drag(280,215);save("16_clear_pending");u.process(silence,160);save("17_clearing");
    delay_test_audio(u);CHECK(u.set_modern(false));configure(420,58,50);
    u.params[0].base=delay_test_time(420);u.params[0].snap();delay_test_audio(u);delay_focus_select(0);save("18_legacy");
}
