// Included by the existing real C++ UI harness; no second UI/controller model.
struct FmMenuFixture : Fixture {
    FmUnit& fm;
    FmMenuFixture() : fm(*static_cast<FmUnit*>(engine->chains[0].units[0])) {
        g_view=View::Chains;g_overlay=Overlay::Editor;g_edit_chain=g_edit_slot=0;
        param_lens_cancel();fm_focus_release();fm_focus_select(FmUnit::P_INDEX);
        g_drag=Drag::None;g_seq_lock_armed=false;fm.set_extended(true);
        down(250,35);down(140,162);fm_focus_select(FmUnit::P_INDEX);lift();
    }
    void capture() {
        engine->capture_ui_snapshot(g_ui_snapshot,g_edit_chain,g_edit_slot);
        param_lens_capture();fm_context_capture(fm_focus_selected());fm_focus_capture();
    }
    void down(int x,int y) {g_drag=Drag::None;CHECK(fm_focus_touch_body(fm,x,y));}
    void lift() {fm_focus_release();g_drag=Drag::None;}
    void tap(int x,int y) {down(x,y);lift();}
    void choose(int p) {
        static const int xs[]={50,154,50,154,154,258,50,50,258,154,258,258};
        static const int ys[]={155,155,195,195,155,195,195,155,155,195,155,195};
        fm_focus_select(p); // choose page, then prove the visible fixed zone selects it
        down(xs[p],ys[p]);CHECK(fm_focus_selected()==p);lift();
    }
    void audio() {
        std::array<int16_t,1024> pcm{};
        for(int i=0;i<16;++i) engine->render(pcm.data(),512);
    }
};
void fm_menu_render(FmMenuFixture& f,bool top) {
    width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();
    pixels.assign(width*HEIGHT,0xff000000u);
    if(top) {CHECK(fm_context_draw_top());} else if(param_lens_is_open()) param_lens_draw_bottom();
    else fm_focus_draw(f.fm);
    CHECK(objects>0&&objects<8192);
    if(text_overlaps) {for(const auto& t:texts)std::fprintf(stderr,"text %d,%d %s\n",t.x,t.y,t.text.c_str());}
    CHECK(outside_rects==0&&outside_lines==0&&text_overlaps==0);
    for(int y=0;y<240;++y) for(int x=0;x<2;++x) CHECK(pixels[y*width+x]==menu_style::paper_color(y));
}
bool fm_label(const std::string& text){return std::any_of(texts.begin(),texts.end(),[&](const TextRecord& t){return t.text==text;});}
bool fm_has(const std::string& needle) {
    return std::any_of(texts.begin(),texts.end(),[&](const TextRecord& t){return t.text.find(needle)!=std::string::npos;});
}
void test_fm_exact_reference() {
    const std::vector<std::array<int,12>> cases={
        {16384,8192,13926,13762,12000,22000,0,0,0,0,0,0},
        {22000,13107,26000,21000,28000,30000,18000,12000,-9000,15000,0,1},
        {32767,32767,32767,32767,32767,32767,32767,32767,32767,32767,1,1},
        {0,0,0,0,0,0,0,0,-32767,0,1,0}
    };
    for(const auto& values:cases) {
        FmUnit actual; q15 params[10]={};
        for(int p=0;p<10;++p) params[p]=actual.params[p].base=actual.params[p].effective=q15(values[p]);
        actual.drone=values[10]!=0;actual.alias_safe=values[11]!=0;actual.reset();actual.control_tick();actual.trigger();
        constexpr int N=2048;std::array<q15,N> pcm{},expected{},mod{},amp{},index{},feedback{};
        actual.process(pcm.data(),N);
        fm_focus_reset_reference(mod.data(),expected.data(),amp.data(),index.data(),feedback.data(),N,params,actual.drone,actual.alias_safe);
        for(int i=0;i<N;++i){CHECK(pcm[i]==expected[i]);CHECK(amp[i]>=0&&index[i]>=0&&feedback[i]>=0);}
    }
}
void test_fm_frozen_render() {
    FmMenuFixture f;
    for(int mode=0;mode<2;++mode){
        f.fm.drone=mode!=0;f.fm.alias_safe=mode==0;
        f.fm.params[FmUnit::P_ATTACK].base=19000;
        f.fm.params[FmUnit::P_DECAY].base=25000;
        f.fm.params[FmUnit::P_INDEX_FALL].base=21000;
        f.fm.params[FmUnit::P_FEEDBACK_FALL].base=14000;
        for(auto& p:f.fm.params)p.snap();
        f.fm.reset();f.fm.control_tick();f.fm.trigger();f.audio();
        fm_focus_select(mode?FmUnit::P_ATTACK:FmUnit::P_INDEX);f.capture();fm_menu_render(f,false);
        const auto frozen=pixels;const int frozen_objects=objects;
        for(auto& p:f.fm.params){p.base=1234;p.effective=-2345;}g_ui_snapshot={};
        Engine* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
        fm_menu_render(f,false);CHECK(pixels==frozen&&objects==frozen_objects);
        g_eng=engine;g_audio=audio;
    }
}
void test_fm_menu_selection_locks() {
    FmMenuFixture f;
    for(int p=0;p<12;++p) {f.choose(p);f.capture();fm_menu_render(f,true);}
    // Both pages remain navigable while armed, configurations/source remain inert.
    g_seq_lock_armed=true;g_view=View::Sequencer;
    f.tap(40,35);f.down(258,155);const int base=f.fm.params[8].base;
    fm_focus_drag(f.fm,268,155);CHECK(seq_display_value(0,8,0)==base+10*32767/336);f.lift();
    f.down(50,195);fm_focus_drag(f.fm,150,195);CHECK(seq_display_value(0,2,0)==32767);f.lift();
    auto before=pattern_hash(f.engine->pattern);int dirty=mutations;
    f.tap(150,10);f.tap(258,10);f.tap(8,45);
    CHECK(!f.fm.drone&&!f.fm.bypass&&before==pattern_hash(f.engine->pattern)&&dirty==mutations);
    f.engine->clear_step_locks(0,0);
    for(int p:{0,1,2,5}) CHECK(f.engine->set_step_lock(0,0,0,p,12000));
    before=pattern_hash(f.engine->pattern);f.tap(40,35);f.down(258,155);
    fm_focus_drag(f.fm,268,155);CHECK(g_seq_lock_flash>0&&before==pattern_hash(f.engine->pattern));f.lift();
    g_seq_lock_armed=false;g_view=View::Chains;
    // Stationary touch after external edits never overwrites them, even after movement.
    f.fm.params[8].base=123;f.down(258,155);fm_focus_drag(f.fm,259,155);CHECK(f.fm.params[8].base==123+32767/336);
    f.fm.params[8].base=8000;dirty=mutations;fm_focus_drag(f.fm,259,155);
    CHECK(f.fm.params[8].base==8000&&mutations==dirty);
    fm_focus_drag(f.fm,258,155);CHECK(f.fm.params[8].base==123);f.lift();
    fm_focus_drag(f.fm,280,155);CHECK(f.fm.params[8].base==123);
    f.fm.drone=true;
    for(int p:{4,6,7,9}) {const q15 v=f.fm.params[p].base;dirty=mutations;f.choose(p);
        CHECK(f.fm.params[p].base==v&&mutations==dirty);f.capture();fm_menu_render(f,true);CHECK(fm_has("dormant")||fm_has("held"));}
}
void test_fm_menu_context() {
    FmMenuFixture f;g_patch_slot=7; // browser cursor must never become project identity
    f.fm.drone=true;f.fm.params[2].base=13926;f.fm.params[2].snap();f.capture();
    fm_menu_render(f,true);CHECK(fm_has("patch01")&&!fm_has("patch08")&&fm_has("off")&&!fm_label("live"));
    const auto frozen=pixels;
    Engine* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
    fm_menu_render(f,true);CHECK(pixels==frozen);g_eng=engine;g_audio=audio;
    f.fm.params[2].base=32767;f.fm.params[2].snap();f.capture();fm_menu_render(f,true);CHECK(pixels!=frozen&&!fm_label("live"));
    for(int p=0;p<12;++p) for(auto src:{ModSource::None,ModSource::SigilA,ModSource::SigilD,ModSource::EnvFollow})
        for(int depth:{-32768,0,32767}) for(int raw:{-32767,0,32767}) {
            fm_focus_select(p);auto& v=f.fm.params[p];v.base=(q15)(v.bipolar?raw:std::max(0,raw));v.snap();
            v.src=src;v.mod_depth=(q15)depth;v.resolve(12345);f.capture();
            ParamInspector expected;f.engine->capture_param_inspector(0,0,p,expected);
            fm_menu_render(f,true);const int top=objects;
            char lo[20],hi[20],effective[20];
            fm_context_value_text(lo,sizeof lo,p,expected.range_min);
            fm_context_value_text(hi,sizeof hi,p,expected.range_max);
            fm_context_value_text(effective,sizeof effective,p,expected.effective);
            char shown[20];fm_context_value_text(shown,sizeof shown,p,v.base);
            CHECK(std::strcmp(shown,effective)==0?!fm_label("live"):fm_has(effective));
            CHECK(src==ModSource::None?!fm_has("potential"):fm_has(std::string(lo)+" .. "+hi));
            CHECK(src!=ModSource::None||fm_has("off"));
            fm_menu_render(f,false);maximum_shared_objects=std::max(maximum_shared_objects,top+objects);CHECK(top+objects<8192);
        }
    // Layered source ranges must fit even with signed fine and live step locks.
    for(int p:{2,8}) for(int layer=0;layer<5;++layer) {
        fm_focus_select(p);auto& v=f.fm.params[p];v.src=ModSource::SigilD;v.mod_depth=-32768;
        v.step_lock_override=layer==1;v.step_lock=30000;v.audition_override=layer==2;v.audition=-10000;
        v.performance_override=layer==3;v.performance=22000;v.temp_override=layer==4;v.temp=-30000;
        v.resolve(12345);g_seq_lock_armed=true;f.capture();fm_menu_render(f,true);
        v.step_lock_override=v.audition_override=v.performance_override=v.temp_override=false;
    }
    g_seq_lock_armed=false;
    // Six real kinds, selected slot six, preceding holes and bypasses.
    for(int s=0;s<6;++s) {delete f.engine->replace_unit(1,s,make_unit(s==5?UnitKind::Fm:UnitKind::Stranular));}
    g_edit_chain=1;g_edit_slot=5;fm_focus_select(8);f.capture();
    width=400;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(400*240,0);
    CHECK(fm_context_draw_top());CHECK(outside_rects==0&&outside_lines==0&&text_overlaps==0);
    g_overlay=Overlay::None;fm_context_capture(8);CHECK(!fm_context_draw_top());
    g_overlay=Overlay::Editor;g_edit_chain=g_edit_slot=0;
}
void test_fm_menu_lens_temp() {
    FmMenuFixture f;f.fm.drone=true;fm_focus_select(8);
    CHECK(param_lens_touch_down(260,232));CHECK(param_lens_is_open());param_lens_touch_release();f.capture();
    CHECK(fm_focus_selected()==8);fm_menu_render(f,false);
    u32 down=KEY_LEFT;param_lens_input(down,KEY_LEFT,0,false);f.capture();CHECK(fm_focus_selected()==7);
    down=KEY_RIGHT;param_lens_input(down,KEY_RIGHT,0,false);f.capture();CHECK(fm_focus_selected()==8);
    param_lens_touch_down(270,185);param_lens_touch_release(); // ENV
    param_lens_touch_down(50,166);param_lens_touch_release();f.capture();
    CHECK(f.fm.params[8].src==ModSource::EnvFollow&&f.fm.params[8].mod_depth<0);
    lens_permanent_probe(f.fm,8,false);CHECK(fm_focus_selected()==8);
    CHECK(param_lens_touch_down(260,232));param_lens_touch_release();f.audio();f.capture();
    fm_menu_render(f,true);CHECK(!fm_has("temp"));fm_menu_render(f,false);
    down=KEY_B|KEY_X;CHECK(param_lens_input(down,KEY_B|KEY_X,0,false));
    CHECK(!param_lens_is_open()&&down==0&&fm_focus_selected()==8);
    // Footer entry carries selected parameter back, not hard-coded pitch/index.
    CHECK(param_lens_touch_down(260,232));param_lens_touch_release();f.capture();CHECK(fm_focus_selected()==8);
    f.fm.bypass=true;lens_permanent_probe(f.fm,8,false);lens_quick_begin();f.capture();
    CHECK(!f.engine->param_temp_active());fm_menu_render(f,false);CHECK(!fm_has("temp")&&!fm_has("unavailable"));
    down=KEY_SELECT|KEY_X;CHECK(param_lens_input(down,KEY_SELECT|KEY_X,0,false));CHECK(down==KEY_SELECT&&!param_lens_is_open());
}
void dump_fm_menu(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    FmMenuFixture f;
    std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tparam\ttop_objects\tbottom_objects\n";
    auto render_audio=[&](){
        for(auto& p:f.fm.params)p.snap();
        f.fm.reset();f.fm.control_tick();f.fm.trigger();
        std::array<q15,1536> audio{};f.fm.process(audio.data(),int(audio.size()));
    };
    auto save=[&](const std::string& name){
        f.capture();fm_menu_render(f,true);const int top=objects;dump_surface(directory,name+"_top");
        fm_menu_render(f,false);CHECK(top+objects<8192);dump_surface(directory,name+"_bottom");
        manifest<<name<<'\t'<<fm_focus_selected()<<'\t'<<top<<'\t'<<objects<<'\n';
    };

    f.fm.drone=false;f.fm.alias_safe=false;f.fm.phase_reset=true;
    render_audio();fm_focus_select(FmUnit::P_INDEX);save("01_voice_strike");

    f.fm.params[FmUnit::P_ATTACK].base=18000;
    f.fm.params[FmUnit::P_DECAY].base=26000;
    f.fm.params[FmUnit::P_INDEX_FALL].base=23000;
    f.fm.params[FmUnit::P_FEEDBACK_FALL].base=15000;
    f.fm.alias_safe=true;render_audio();fm_focus_select(FmUnit::P_INDEX_FALL);save("02_motion_strike");

    f.fm.drone=true;f.fm.params[FmUnit::P_RATIO].base=13107;
    f.fm.params[FmUnit::P_RATIO_FINE].base=-7200;
    f.fm.params[FmUnit::P_INDEX].base=25000;
    f.fm.params[FmUnit::P_FEEDBACK].base=22000;
    f.fm.params[FmUnit::P_INDEX].src=ModSource::SigilB;
    f.fm.params[FmUnit::P_INDEX].mod_depth=-14000;
    f.fm.params[FmUnit::P_INDEX].effective=17000;fm_focus_select(FmUnit::P_INDEX);save("03_voice_drone_mod");

    fm_focus_select(FmUnit::P_ATTACK);g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=5;g_view=View::Sequencer;
    CHECK(f.engine->set_step_lock(0,5,0,FmUnit::P_ATTACK,24000));
    render_audio();save("04_motion_drone_lock");
    g_seq_lock_armed=false;g_view=View::Chains;
    f.fm.drone=false;f.fm.params[FmUnit::P_PITCH_ENV].base=16384;f.fm.params[FmUnit::P_PITCH_TIME].base=FmUnit::DEFAULT_PITCH_TIME;
    f.fm.carrier_wave=FmUnit::Wave::Triangle;f.fm.mod_wave=FmUnit::Wave::Pulse;
    render_audio();fm_focus_select(FmUnit::P_PITCH_ENV);save("05_pitch_envelope");
    f.tap(250,35);save("06_operators");
    f.fm.set_extended(false);f.tap(250,35);fm_focus_select(FmUnit::P_INDEX);f.tap(250,35);save("07_legacy");
}
