// Focused REC UI checks; reuse the existing native C++ harness, not a UI model.
// Export and workspace-layout callbacks are explicit host shims in sequencer_test.cpp.
struct RecFixture : ModuleInspectorFixture {
    trackr::synth::Sample saved;
    RecFixture():ModuleInspectorFixture(UnitKind::Recorder) {
        std::swap(saved,trackr::synth::SampleBank::instance().slot(16));
        rec_export_enabled=true;rec_export_succeeds=false;rec_export_calls=rec_workspace_shifts=g_export_flash=0;
        module_focus_show_sample_result(trackr::synth::SampleEditResult::Ok);
        recorder_focus_release();configure();engine->launch_quantize=LaunchQuantize::Immediate;capture();
    }
    ~RecFixture(){recorder_focus_release();param_lens_cancel();engine.reset();std::swap(saved,trackr::synth::SampleBank::instance().slot(16));rec_export_enabled=false;g_eng=nullptr;}
    RecUnit& rec(){return *static_cast<RecUnit*>(module);}
    void configure() {
        const q15 values[]={0,8192,32767};
        for(int p=0;p<3;++p){module->params[p].base=values[p];module->params[p].src=ModSource::None;module->params[p].mod_depth=0;module->params[p].snap();}
        recorder_focus_select(RecUnit::P_LEN);module->bypass=false;
    }
    bool down(int x,int y){return recorder_focus_touch_body(rec(),x,y);}
    void drag(int x,int y){recorder_focus_drag(rec(),x,y);}
    void release(){recorder_focus_release();g_drag=Drag::None;}
    std::array<q15,3> values(){return {module->params[0].base,module->params[1].base,module->params[2].base};}
    std::vector<u32> bottom(bool lens=false) {
        module_inspector_surface(false);{ModuleInspectorNoEngine no;if(lens)param_lens_draw_bottom();else recorder_focus_draw(rec(),nullptr);}
        module_inspector_geometry();return pixels;
    }
    void feed() {
        CHECK(rec().is_recording());std::array<q15,512> input{};
        for(int block=0;block<64;++block) {
            for(int i=0;i<512;++i){const int n=block*512+i;input[i]=q15(std::lround(11000*std::sin(n*.023)*(.6+.4*std::cos(n*.0006))));}
            rec().capture_input(input.data(),512);
        }
        capture();
    }
};
void test_rec_controls() {
    RecFixture f;auto& u=f.rec();const auto original=f.values();const int dirty=mutations;
    CHECK(f.down(80,54));CHECK(recorder_focus_selected()==RecUnit::P_LEN&&f.values()==original);
    f.drag(80,10);CHECK(mutations==dirty);f.drag(86,54);CHECK(u.params[1].base==8192+6*32767/170);f.release();
    for(auto xy:{std::pair<int,int>{160,118},{210,76},{232,205},{24,147}}){const auto v=f.values();const int n=mutations;f.down(xy.first,xy.second);f.drag(xy.first+30,xy.second);CHECK(f.values()==v&&mutations==n);}
    f.down(230,32);CHECK(recorder_focus_selected()==2&&u.params[2].base==32767);f.down(230,50);CHECK(u.params[2].base==0);f.down(230,50);CHECK(u.params[2].base==32767);
    f.configure();g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    CHECK(f.engine->set_step_lock(0,7,0,1,4000));f.down(80,54);f.drag(86,54);CHECK(seq_display_value(0,1,0)==4000+6*32767/170&&f.values()==original);
    const auto locked=f.hash();g_seq_lock_step=8;f.drag(110,54);CHECK(f.hash()==locked);g_seq_lock_step=7;f.release();
    f.down(14,32);CHECK(seq_display_value(0,1,8192)==8192);f.down(230,50);CHECK(seq_display_value(0,2,32767)==0&&u.params[2].base==32767);f.down(230,32);CHECK(seq_display_value(0,2,32767)==32767);
    const int exports=rec_export_calls;for(auto xy:{std::pair<int,int>{24,170},{118,170},{224,170},{14,202},{118,202},{266,202},{236,76},{296,76},{258,10}})f.down(xy.first,xy.second);
    CHECK(!u.is_recording()&&u.recorded_frames()==0&&!u.bypass&&u.sample_slot==16&&rec_export_calls==exports);CHECK(!param_lens_touch_down(250,232));
    g_seq_lock_chain=1;const auto wrong=f.hash();f.down(80,54);f.drag(120,54);f.down(230,50);CHECK(f.hash()==wrong&&f.values()==original);g_seq_lock_chain=0;
    f.engine->clear_track(0);delete f.engine->replace_unit(0,1,new FmUnit);
    for(int i=0;i<SEQ_LOCKS;++i)CHECK(f.engine->set_step_lock(0,7,1,i,1000));
    const auto full=f.hash();f.down(80,54);f.drag(95,54);CHECK(f.hash()==full&&f.values()==original&&g_seq_lock_flash>0);f.capture();f.bottom();CHECK(module_inspector_text("lock full"));
    g_seq_lock_armed=false;g_view=View::Chains;f.down(80,54);g_overlay=Overlay::None;f.drag(110,54);CHECK(f.values()==original);g_overlay=Overlay::Editor;
    f.down(80,54);Unit* previous=f.engine->replace_unit(0,0,new RecUnit);f.drag(110,54);CHECK(f.values()==original);delete f.engine->replace_unit(0,0,previous);
}
void test_rec_mod() {
    RecFixture f;auto& u=f.rec();CHECK(module_inspector_supports(UnitKind::Recorder));
    for(int p:{RecUnit::P_LEN,RecUnit::P_MON}) {
        recorder_focus_select(p);f.open();CHECK(recorder_focus_selected()==p);
        const q15 base=u.params[p].base;param_lens_touch_down(150,129);CHECK(u.params[p].base==base);param_lens_touch_drag(144,129);param_lens_touch_release();CHECK(u.params[p].base==base-6*32767/271);
        for(int source=0;source<6;++source){param_lens_touch_down(20+51*source,185);param_lens_touch_release();CHECK(int(u.params[p].src)==source);}
        param_lens_touch_down(70,185);param_lens_touch_release();param_lens_touch_down(150,166);param_lens_touch_drag(170,166);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA&&u.params[p].mod_depth==20*65534/271);
        lens_permanent_probe(u,p);CHECK(recorder_focus_selected()==p);f.open();f.capture();f.bottom(true);
        CHECK(module_inspector_text("base ")&&!module_inspector_text("temp")&&!u.params[RecUnit::P_REC].temp_override);
        for(int i=0;i<4;++i){param_lens_touch_down(290,47);param_lens_touch_release();CHECK(recorder_focus_selected()==(i%2?p:(p==1?2:1)));}
        param_lens_cancel();CHECK(recorder_focus_selected()==p);
    }
    // True unchanged DSP uses modulated LEN on trigger, then latches it. MON
    // gates only pass-through; both paths still write the input into the take.
    f.configure();u.params[1].src=ModSource::SigilA;u.params[1].mod_depth=-4000;u.params[1].resolve(32767);
    const uint32_t expected=1024+uint32_t((uint64_t(u.params[1].effective)*(ENGINE_SR*20-1024))>>15);
    f.down(24,170);CHECK(u.is_recording()&&u.active_target_frames()==expected);const auto len=u.params[1].base;
    f.down(80,54);f.drag(120,54);CHECK(u.params[1].base==len);
    recorder_focus_select(1);f.open();param_lens_touch_down(150,129);param_lens_touch_drag(240,129);param_lens_touch_release();CHECK(u.params[1].base==len);
    param_lens_touch_down(150,166);param_lens_touch_drag(130,166);param_lens_touch_release();u.params[1].resolve(-32767);CHECK(u.active_target_frames()==expected);param_lens_cancel();
    u.params[2].base=0;u.params[2].mod_depth=32767;u.params[2].src=ModSource::SigilB;u.params[2].snap();u.params[2].resolve(32767);
    q15 audible[4]={1000,-1000,2000,-2000};u.process(audible,4);CHECK(audible[0]==1000);
    u.params[2].snap();u.params[2].resolve(-32767);q15 muted[4]={3000,-3000,4000,-4000};u.process(muted,4);CHECK(muted[0]==0&&muted[3]==0&&u.recorded_frames()==8);
    const auto& pcm=trackr::synth::SampleBank::instance().slot(u.sample_slot).data;CHECK(pcm[4]==3000&&pcm[7]==-4000);
    char text[24];module_inspector_value_text(text,sizeof text,UnitKind::Recorder,1,0);CHECK(std::string(text)=="32ms");module_inspector_value_text(text,sizeof text,UnitKind::Recorder,2,16384);CHECK(std::string(text)=="on");
}
void test_rec_commands_frames() {
    RecFixture f;auto& u=f.rec();f.capture();module_inspector_top();f.bottom();CHECK(module_inspector_text("empty take"));
    f.engine->transport_play();f.engine->launch_quantize=LaunchQuantize::Beat;f.down(24,170);CHECK(!u.is_recording()&&f.engine->recorder_record_pending(0,0));f.capture();f.bottom();CHECK(module_inspector_text("start queued"));
    const auto pending=f.values();f.down(80,54);f.drag(130,54);f.down(266,202);f.down(296,76);CHECK(f.values()==pending&&u.sample_slot==16&&f.engine->recorder_record_pending(0,0));
    f.down(118,170);CHECK(!f.engine->recorder_record_pending(0,0));f.engine->launch_quantize=LaunchQuantize::Immediate;f.down(24,170);f.feed();CHECK(u.recorded_frames()==32768);
    f.engine->launch_quantize=LaunchQuantize::Beat;f.down(118,170);CHECK(u.is_recording()&&f.engine->recorder_record_pending(0,0));f.capture();f.bottom();CHECK(module_inspector_text("stop queued"));
    const auto generation=u.take_generation();f.down(266,202);f.down(224,170);f.down(14,202);CHECK(u.take_generation()==generation&&rec_export_calls==0);
    CHECK(f.engine->request_recorder_record(0,0,false,LaunchQuantize::Immediate)==trackr::synth::SampleEditResult::Ok);f.engine->transport_stop();f.engine->launch_quantize=LaunchQuantize::Immediate;
    f.capture();const auto top=module_inspector_top(false,true);const int top_objects=objects;const auto bottom=f.bottom();fold_test_geometry_pair(top_objects);CHECK(module_inspector_text("take / unsaved"));
    f.down(24,170);CHECK(!u.is_recording()&&u.take_generation()==generation&&u.recorded_frames()==32768);
    // Mutate every source used by drawing after capture. Frozen output is exact.
    g_ui_snapshot={};u.params[1].base=32767;u.bypass=true;CHECK(f.bottom()==bottom&&module_inspector_top()==top);u.params[1].base=8192;u.bypass=false;
    rec_export_succeeds=false;f.down(224,170);CHECK(rec_export_calls==1&&u.take_needs_save());f.capture();f.bottom();CHECK(module_inspector_text("save/clear")||module_inspector_text("save failed"));
    module_focus_show_sample_result(trackr::synth::SampleEditResult::Ok);f.down(14,202);CHECK(rec_export_calls==2&&f.engine->chains[0].units[1]==nullptr);
    rec_export_succeeds=true;f.down(14,202);CHECK(rec_export_calls==3&&!u.take_needs_save()&&f.engine->chains[0].units[1]->kind==UnitKind::SamplePlayer&&rec_workspace_shifts==1);
    f.down(118,202);CHECK(rec_export_calls==3&&f.engine->chains[0].units[1]->kind==UnitKind::Grain&&rec_workspace_shifts==2);
    // Remove aliases before clearing: the separate COW engine contract is unchanged.
    delete f.engine->replace_unit(0,1,nullptr);delete f.engine->replace_unit(0,2,nullptr);
    f.down(266,202);CHECK(u.recorded_frames()==0&&!u.is_recording());f.capture();f.bottom();CHECK(module_inspector_text("empty take"));
    for(q15 v:{q15(0),q15(32767)})for(int p:{1,2}){u.params[p].base=v;u.params[p].snap();recorder_focus_select(p);f.capture();module_inspector_top();const int n=objects;f.bottom();fold_test_geometry_pair(n);}
    g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top());module_inspector_surface(false);recorder_focus_draw(u,nullptr);CHECK(texts.empty());
}
void dump_rec_runtime(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\ttop_objects\tbottom_objects\tshared_objects\n";
    RecFixture f;
    auto save=[&](const char* name,bool lens=false){f.capture();module_inspector_top(lens,true);const int top=objects;dump_surface(directory,std::string(name)+"_top");f.bottom(lens);fold_test_geometry_pair(top);dump_surface(directory,std::string(name)+"_bottom");manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\n';};
    save("01_empty");f.down(24,170);f.feed();save("02_recording");f.down(118,170);save("03_unsaved");
    recorder_focus_select(1);f.open();param_lens_touch_down(70,185);param_lens_touch_release();param_lens_touch_down(150,166);param_lens_touch_drag(170,166);param_lens_touch_release();save("04_mod",true);
}
