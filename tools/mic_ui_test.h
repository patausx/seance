// Narrow real MIC controller/capture/inspector checks inside the existing host harness.
struct MicFixture : ModuleInspectorFixture {
    MicFixture():ModuleInspectorFixture(UnitKind::MicIn) {g_mic_ok=true;mic_focus_release();configure();}
    ~MicFixture(){g_mic_ok=false;MicUnit::reset_input();mic_focus_release();}
    MicUnit& mic(){return *static_cast<MicUnit*>(module);}
    void configure(q15 gain=16384,q15 gate=32767,q15 cut=0) {
        const q15 v[]={gain,gate,cut};for(int p=0;p<3;++p){module->params[p].base=v[p];module->params[p].mod_depth=0;module->params[p].src=ModSource::None;module->params[p].snap();}
        module->bypass=false;engine->chains[0].mute=false;mic_focus_select(0);
    }
    bool down(int x,int y){return mic_focus_touch_body(mic(),x,y);}
    void drag(int x,int y){mic_focus_drag(mic(),x,y);}
    void release(){mic_focus_release();g_drag=Drag::None;}
    void lower(){mic_focus_draw(mic());}
    void feed() {
        std::array<q15,1200> raw{};const double pi=3.141592653589793;
        for(int i=0;i<1200;++i)raw[i]=q15(std::lround(11200*std::sin(2*pi*i/131.)*(.65+.35*std::sin(pi*i/1200.))));
        MicUnit::reset_input();MicUnit::push(raw.data(),int(raw.size()));
        std::array<q15,512> output{};mic().process(output.data(),int(output.size()));++g_ui_frame;capture();
    }
    std::array<q15,3> values(){return {module->params[0].base,module->params[1].base,module->params[2].base};}
    std::vector<u32> bottom(){module_inspector_surface(false);{ModuleInspectorNoEngine no;lower();}module_inspector_geometry();return pixels;}
    std::vector<u32> wave(){const auto b=bottom();std::vector<u32> result;for(int y=89;y<=137;++y)for(int x=14;x<=306;++x)result.push_back(b[y*320+x]);return result;}
};
void test_mic_controls_locks() {
    MicFixture f;auto& u=f.mic();const auto original=f.values();const int dirty=mutations;
    CHECK(f.down(80,53));CHECK(mic_focus_selected()==0&&f.values()==original);
    f.drag(80,15);CHECK(f.values()==original&&mutations==dirty);
    f.drag(86,53);CHECK(u.params[0].base==16384+6*32767/138);f.release();
    for(auto xy:{std::pair<int,int>{160,113},{210,174},{270,207}}){const auto v=f.values();const int d=mutations;f.down(xy.first,xy.second);f.drag(xy.first+50,xy.second);CHECK(f.values()==v&&mutations==d);}
    f.down(230,32);CHECK(mic_focus_selected()==1&&u.params[1].base==32767);
    f.down(230,50);CHECK(u.params[1].base==0);f.down(230,50);CHECK(u.params[1].base==32767);
    for(auto choice:{std::pair<int,int>{132,16384},{171,32767},{92,0}}){f.down(choice.first,206);CHECK(mic_focus_selected()==2&&u.params[2].base==choice.second);}
    f.configure();g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    CHECK(f.engine->set_step_lock(0,7,0,0,8000));f.down(80,53);f.drag(86,53);
    CHECK(seq_display_value(0,0,0)==8000+6*32767/138&&f.values()==original);
    const auto locked=f.hash();g_seq_lock_step=8;f.drag(100,53);CHECK(f.hash()==locked);g_seq_lock_step=7;f.release();
    f.down(14,32);CHECK(seq_display_value(0,0,16384)==16384);
    f.down(230,50);CHECK(seq_display_value(0,1,32767)==0&&u.params[1].base==32767);f.down(230,32);CHECK(seq_display_value(0,1,32767)==32767);
    f.down(132,206);CHECK(seq_display_value(0,2,0)==16384&&u.params[2].base==0);f.down(14,206);CHECK(seq_display_value(0,2,0)==0);
    f.down(260,10);CHECK(!u.bypass);CHECK(!param_lens_touch_down(250,232));
    g_seq_lock_chain=1;const auto wrong=f.hash();f.down(80,53);f.drag(120,53);f.down(230,50);f.down(171,206);CHECK(f.hash()==wrong&&f.values()==original);g_seq_lock_chain=0;
    f.engine->clear_track(0);delete f.engine->replace_unit(0,1,new FmUnit);
    for(int i=0;i<SEQ_LOCKS;++i)CHECK(f.engine->set_step_lock(0,7,1,i,1000));
    const auto full=f.hash();f.down(80,53);f.drag(95,53);f.down(230,50);f.down(171,206);CHECK(f.hash()==full&&g_seq_lock_flash>0&&f.values()==original);f.release();
    g_seq_lock_armed=false;g_view=View::Chains;f.down(80,53);const auto before=f.values();g_overlay=Overlay::None;f.drag(120,53);CHECK(f.values()==before);g_overlay=Overlay::Editor;
    f.down(80,53);Unit* previous=f.engine->replace_unit(0,0,new MicUnit);f.drag(120,53);CHECK(f.values()==before);delete f.engine->replace_unit(0,0,previous);
}
void test_mic_lens_inspector() {
    MicFixture f;auto& u=f.mic();char text[24];
    for(auto item:{std::pair<int,const char*>{0,"silent"},{8192,"+0.0db"},{16384,"+6.0db"},{32767,"+12.0db"}}){module_inspector_value_text(text,sizeof text,UnitKind::MicIn,0,q15(item.first));CHECK(std::string(text)==item.second);}
    for(int p=0;p<3;++p) {
        mic_focus_select(p);f.open();CHECK(mic_focus_selected()==p);
        const q15 origin=u.params[p].base;param_lens_touch_down(150,129);CHECK(u.params[p].base==origin);param_lens_touch_drag(144,129);param_lens_touch_release();CHECK(u.params[p].base==std::max(0,origin-6*32767/271));
        param_lens_touch_down(70,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);
        lens_permanent_probe(u,p);CHECK(mic_focus_selected()==p);f.open();f.capture();module_inspector_top(true);CHECK(!module_inspector_text("temporary"));
        module_inspector_surface(false);{ModuleInspectorNoEngine no;param_lens_draw_bottom();}module_inspector_geometry();
        CHECK(module_inspector_text("base ")&&!module_inspector_text("temp"));
        param_lens_cancel();CHECK(mic_focus_selected()==p);
    }
    for(int p=0;p<3;++p)for(q15 v:{q15(0),q15(32767)}){f.configure();u.params[p].base=v;u.params[p].snap();mic_focus_select(p);f.capture();module_inspector_top();module_inspector_detail([&]{ParamInspector d;f.engine->capture_param_inspector(0,0,p,d);return d;}());}
}
void test_mic_live_frames() {
    MicFixture f;f.feed();auto& u=f.mic();const auto normal=f.bottom(),wave=f.wave();const auto top=module_inspector_top();const int top_count=objects;
    const uint32_t processed=u.visual_process_serial(),input=MicUnit::input_serial();
    // Draw only the frozen copy after both source and parameter mutations.
    f.configure(32767,0,32767);MicUnit::reset_input();g_mic_ok=false;CHECK(f.bottom()==normal&&module_inspector_top()==top);CHECK(u.visual_process_serial()==processed);
    g_mic_ok=true;f.configure();f.feed();CHECK(f.wave()==wave);CHECK(MicUnit::input_serial()!=input);
    f.configure(32767);f.feed();CHECK(u.visual_clip());CHECK(f.wave()==wave);f.bottom();CHECK(module_inspector_text("0.0 dbfs"));
    f.configure(16384,0);f.feed();CHECK(f.wave()==wave);f.bottom();CHECK(module_inspector_text("silent"));CHECK(!u.visual_clip());
    f.configure(16384,32767,32767);f.feed();CHECK(f.wave()==wave);
    f.configure();f.feed();u.bypass=true;f.capture();CHECK(f.wave()==wave);f.bottom();CHECK(module_inspector_text("silent"));
    u.bypass=false;f.engine->chains[0].mute=true;f.capture();CHECK(f.wave()==wave);f.engine->chains[0].mute=false;
    // Raw extrema are driven by actual signed PCM including negative full scale.
    MicUnit::reset_input();std::array<q15,1048> dc{};dc.fill(-32768);MicUnit::push(dc.data(),1048);f.capture();f.bottom();
    CHECK(pixels[137*320+100]==m::ink());CHECK(module_inspector_text("0.0 dbfs"));
    MicUnit::reset_input();dc.fill(32767);MicUnit::push(dc.data(),1048);f.capture();f.bottom();CHECK(pixels[89*320+100]==m::ink());
    MicUnit::reset_input();dc.fill(0);MicUnit::push(dc.data(),1048);f.capture();f.bottom();CHECK(module_inspector_text("silent"));
    g_ui_frame+=6;f.capture();f.bottom();CHECK(module_inspector_text("no new input")&&module_inspector_text("--"));
    g_mic_ok=false;f.capture();f.bottom();CHECK(module_inspector_text("mic unavailable"));
    g_mic_ok=true;MicUnit::reset_input();f.capture();f.bottom();CHECK(module_inspector_text("waiting"));
    q15 one=-32768;MicUnit::push(&one,1);f.capture();f.bottom();CHECK(pixels[137*320+306]==m::ink());
    fold_test_geometry_pair(top_count);
    g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top());module_inspector_surface(false);f.lower();CHECK(texts.empty());
}
void dump_mic_runtime(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\ttop_objects\tbottom_objects\tshared_objects\n";MicFixture f;
    auto save=[&](const char* name){f.feed();module_inspector_top(false,true);const int top=objects;dump_surface(directory,std::string(name)+"_top");
        f.bottom();fold_test_geometry_pair(top);dump_surface(directory,std::string(name)+"_bottom");manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\n';};
    f.configure();save("01_open");f.configure(32767);save("02_clip");f.configure(16384,0);save("03_muted");
    f.configure(16384,32767,32767);mic_focus_select(2);save("04_low_cut");
}
