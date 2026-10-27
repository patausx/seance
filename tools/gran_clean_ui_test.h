// GRAN v2: actual main touch dispatch, frozen native framebuffers, no exports.
namespace {
struct GranCleanFixture:GranFixture {
    GranCleanFixture(){gran->clean=true;gran->reset();snap_all();capture();}
    void snap_all(){for(auto& p:gran->params)p.snap();gran->control_tick();}
    std::array<q15,GrainUnit::P_COUNT> all() const {std::array<q15,GrainUnit::P_COUNT> a{};for(int p=0;p<GrainUnit::P_COUNT;++p)a[p]=gran->params[p].base;return a;}
    static std::pair<int,int> point(int p){return p==GrainUnit::P_SCAN?std::pair<int,int>{264,224}:GranFixture::point(p);}
};
void test_gran_clean_controls(){
    using C=GrainUnit;
    for(bool lock:{false,true})for(int p=0;p<C::P_COUNT;++p){
        GranCleanFixture f;auto& u=*f.gran;f.pick(p);u.params[p].base=12000;f.snap_all();
        if(lock){g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,p,9000));}
        const auto before=f.all();const auto xy=f.point(p);const int start=lock?9000:12000,range=(p==C::P_PITCH||p==C::P_SCAN)?65534:32767;
        input::down(xy.first,xy.second);CHECK(g_drag==Drag::GrainPosition&&grain_focus_selected()==p&&f.all()==before);
        input::move(xy.first,xy.second);CHECK(f.all()==before);
        input::move(xy.first+6,xy.second);CHECK(seq_display_value(0,p,u.params[p].base)==start+6*range/84);
        if(lock)CHECK(f.all()==before);
        input::up();const auto stopped=f.all();input::move(xy.first+20,xy.second);CHECK(f.all()==stopped);
        const int dirty=mutations;input::down(180,9);input::up();CHECK(mutations==dirty);f.capture();f.lower();CHECK(module_inspector_text("fine /32"));
        const int fine_start=seq_display_value(0,p,u.params[p].base);
        input::down(xy.first,xy.second);input::move(xy.first+1,xy.second);input::up();
        CHECK(seq_display_value(0,p,u.params[p].base)==fine_start+range/(84*32));
        if(lock){CHECK(f.all()==before);input::down(xy.first-42,xy.second-25);input::up();CHECK(seq_display_value(0,p,before[p])==before[p]);}
    }
    GranCleanFixture f;auto& u=*f.gran;u.params[C::P_SCAN].base=15000;
    input::down(166,40);input::up();CHECK(u.params[C::P_SCAN].base==0&&grain_focus_selected()==C::P_SCAN);
    const int dirty=mutations;input::down(166,40);input::up();CHECK(mutations==dirty);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    CHECK(f.engine->set_step_lock(0,7,0,C::P_SCAN,15000));input::down(166,40);input::up();CHECK(seq_display_value(0,C::P_SCAN,1234)==0);
    f.engine->remove_step_lock(0,7,0,C::P_SCAN);delete f.engine->replace_unit(0,1,new FmUnit);
    for(int p=0;p<SEQ_LOCKS;++p)CHECK(f.engine->set_step_lock(0,7,1,p,1000));
    const auto hash=f.hash();const auto bases=f.all();f.pick(C::P_SCAN);input::down(264,224);input::move(275,224);input::up();
    input::down(160,113);input::move(178,113);input::up();CHECK(f.hash()==hash&&f.all()==bases&&g_seq_lock_flash>0);
}
void test_gran_clean_wave(){
    using C=GrainUnit;GranCleanFixture f;auto& u=*f.gran;u.params[C::P_POS].base=14000;f.snap_all();
    for(int zoom:{2,4,8,16,1}){
        f.capture();f.lower();CHECK(module_inspector_text(("x"+std::to_string(zoom)).c_str()));
        const auto before=f.all();const int dirty=mutations;input::down(160,113);CHECK(f.all()==before&&mutations==dirty);
        input::move(168,113);f.capture();input::move(168,113);input::up();
        auto expected=before;expected[C::P_POS]+=int64_t(8)*32768*((64000+zoom-1)/zoom)/(292ll*64000);
        CHECK(f.all()==expected);const int edited=mutations;input::down(214,65);input::up();CHECK(mutations==edited);
    }
    // Fine and the zoom span are transient sensitivity, not patch parameters.
    input::down(180,9);input::up();const auto before=f.all();input::down(160,113);input::move(192,113);input::up();
    auto expected=before;expected[C::P_POS]+=int64_t(32)*32768*32000/(292ll*64000*32);CHECK(f.all()==expected);
    // Actual scan moves while touching: the viewport/sensitivity stays latched.
    u.params[C::P_SCAN].base=8192;f.snap_all();f.process(1200);const auto start=f.all();
    input::down(160,113);f.process(2000);f.capture();input::move(192,113);input::up();
    expected=start;expected[C::P_POS]+=int64_t(32)*32768*32000/(292ll*64000*32);CHECK(f.all()==expected);
    // No stale contact may write a replaced source, overlay or edit bank.
    auto& bank=trackr::synth::SampleBank::instance();auto& s=bank.slot(60);
    for(int reason=0;reason<4;++reason){
        f.capture();const auto saved=f.all();input::down(160,113);
        if(reason==0)s.logical_frames=1000;
        if(reason==1){g_overlay=Overlay::None;f.capture();g_overlay=Overlay::Editor;}
        if(reason==2){g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;}
        if(reason==3){trackr::synth::Sample held=std::move(s);bank.swap_sample_unchecked(60,held,trackr::synth::SampleOrigin::SdWav,"same-memory-replacement.wav",false);}
        input::move(190,113);input::up();CHECK(f.all()==saved);
        s.logical_frames=UINT32_MAX;g_seq_lock_armed=false;g_view=View::Chains;
    }
    // Waveform edits write POS only to the armed step, never base; a bank change cancels.
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;f.capture();
    CHECK(f.engine->set_step_lock(0,7,0,C::P_POS,12000));const auto saved=f.all();
    input::down(160,113);input::move(168,113);input::up();CHECK(f.all()==saved&&seq_display_value(0,C::P_POS,0)>12000);
    const int locked=seq_display_value(0,C::P_POS,0);input::down(160,113);f.engine->active_pattern=1;input::move(190,113);input::up();f.engine->active_pattern=0;
    CHECK(seq_display_value(0,C::P_POS,0)==locked&&f.all()==saved);
}
void test_gran_clean_mode_lens(){
    using C=GrainUnit;GranCleanFixture f;auto& u=*f.gran;u.clean=false;f.capture();const auto old=f.all();
    f.pick(C::P_SCAN);input::down(264,224);input::move(280,224);input::up();CHECK(!u.clean&&f.all()==old);
    input::frame(0,0);input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open()&&grain_focus_selected()==C::P_POS&&!u.clean&&f.all()==old);
    input::frame(0,0,KEY_L);input::frame(0,0);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    input::down(130,40);input::up();CHECK(!u.clean&&f.all()==old);
    g_seq_lock_armed=false;g_view=View::Chains;input::down(130,40);input::up();CHECK(u.clean&&f.all()==old);
    f.pick(C::P_SCAN);input::frame(0,0);input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());
    input::down(220,129);input::move(213,129);input::up();CHECK(u.params[C::P_SCAN].base==-7*65534/271);
    input::down(75,185);input::up();CHECK(u.params[C::P_SCAN].src==ModSource::SigilA);
    input::down(150,160);input::move(130,160);input::up();CHECK(u.params[C::P_SCAN].mod_depth<0);
    f.capture();module_inspector_top(true,true);const int n=objects;CHECK(module_inspector_text("trigger rewinds"));
    module_inspector_surface(false);{ModuleInspectorNoEngine frozen;param_lens_draw_bottom();}gran_geometry_pair(n);
    input::frame(0,0,KEY_L);CHECK(!param_lens_is_open()&&!f.engine->param_temp_active());input::frame(0,0);
    // Hold-L owns a pre-existing waveform contact through release.
    f.capture();const auto values=f.all();input::touch={160,113};input::frame(KEY_TOUCH,KEY_TOUCH);input::frame(KEY_L,KEY_L|KEY_TOUCH);
    input::touch={290,129};input::frame(0,KEY_L|KEY_TOUCH);input::frame(0,KEY_TOUCH,KEY_L);input::frame(0,0,KEY_TOUCH);CHECK(f.all()==values);
    char out[32];for(bool clean:{false,true}){u.clean=clean;const auto variant=seq_lock::variant(u);CHECK(bool(variant&8)==clean);
        seq_lock::format_value(UnitKind::Grain,variant,C::P_SCAN,0,out,sizeof out);CHECK(std::string(out)==(clean?"hold":"v2 only"));}
    seq_lock::format_value(UnitKind::Grain,8,C::P_SCAN,-8192,out,sizeof out);CHECK(std::abs(std::strtod(out,nullptr)+1.0)<.001);
}
void test_gran_clean_frozen(){
    using C=GrainUnit;GranCleanFixture f;auto& u=*f.gran;auto& s=trackr::synth::SampleBank::instance().slot(60);
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){
        palette::set(theme);f.config();u.params[C::P_SCAN].base=-8192;u.params[C::P_POS].base=23000;f.snap_all();f.process(1600);f.pick(C::P_SCAN);f.capture();
        const auto top=module_inspector_top(false,true);const int top_count=objects;f.lower();gran_geometry_pair(top_count);const auto bottom=pixels;
        CHECK(module_inspector_text("v2")&&module_inspector_text("scan"));
        // The solid nominal marker is derived from the audio-clock scan, not GUI time.
        const uint32_t pos=uint64_t(u.scan_position())*64000>>15,span=32000,start=pos>span/2?std::min(pos-span/2,64000-span):0;
        const int x=14+int64_t(pos-start)*292/span;CHECK(pixels[76*320+x]==m::spot());
        const auto saved=f.all();u.params[C::P_POS].base=0;g_ui_snapshot={};g_ui_frame+=1000;s.data[0]=123;
        {ModuleInspectorNoEngine frozen;f.lower();CHECK(pixels==bottom);CHECK(module_inspector_top()==top);}
        for(int p=0;p<C::P_COUNT;++p)u.params[p].base=saved[p];
    }
    u.bypass=true;f.capture();f.lower();CHECK(module_inspector_text("paused"));u.bypass=false;
    f.engine->chains[0].mute=true;f.capture();f.lower();CHECK(module_inspector_text("muted / scan runs"));f.engine->chains[0].mute=false;
    s.data.clear();f.capture();f.lower();CHECK(module_inspector_text("no source"));const auto before=f.all();input::down(160,113);input::move(180,113);input::up();CHECK(f.all()==before);
    theme_test::select();
}
int run_gran_clean_ui(){
    int failed=0,total=0;const auto run=[&](const char* name,auto test){++total;try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::printf("FAIL: %s: %s\n",name,e.what());}};
    run("gran legacy controls / updated toolbar",test_gran_controls);
    run("gran legacy locks / lens / ownership",test_gran_locks_lens);
    run("gran legacy actual wave / latched heads",test_gran_wave);
    run("gran legacy frozen pairs / physical mappings",test_gran_frames);
    run("gran v2 main / 12 base+lock controls / fine / hold / full bank",test_gran_clean_controls);
    run("gran v2 waveform / zoom / latched sensitivity / source and bank ownership",test_gran_clean_wave);
    run("gran v2 opt-in / scan lens / held-L / stored score identity",test_gran_clean_mode_lens);
    run("gran v2 actual scan marker / frozen two-theme duals / empty and bypass",test_gran_clean_frozen);
    std::printf("gran v2 ui: %d/%d passed, shared peak %d/8192 (not GPU timing)\n",total-failed,total,maximum_shared_objects);return failed?1:0;
}
} // namespace
