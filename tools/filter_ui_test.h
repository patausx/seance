// Included after module_inspector_test.h: actual Filter controllers and frozen C++ drawing.
// The response oracle is LINEARIZED recurrence only, never a fixed-point PCM claim.
using FilterMode = trackr::dsp::FilterType;
std::array<q15,4> filter_test_bases(const Unit& u) {
    std::array<q15,4> result{};for(int p=0;p<4;++p)result[p]=u.params[p].base;return result;
}
void filter_test_release() {filter_focus_release();g_drag=Drag::None;}
int filter_test_span(int p) {return p==0?232:p==1?70:126;}
void filter_test_delta(ModuleInspectorFixture& f,int p,int delta) {
    const auto xy=f.point(p);f.drag(xy.first+(p==1?0:delta),xy.second-(p==1?delta:0));
}
void test_filter_controls() {
    ModuleInspectorFixture f(UnitKind::Filter);auto& u=*static_cast<FilterUnit*>(f.module);
    CHECK(u.num_params==4&&module_inspector_supports(UnitKind::Filter));
    struct Box {int x0,y0,x1,y1,p;};
    const Box boxes[]={{10,52,271,167,0},{276,52,319,167,1},{10,174,154,216,2},{166,174,309,216,3}};
    const Drag drags[]={Drag::FilterCutoff,Drag::FilterResonance,Drag::FilterDrive,Drag::FilterMorph};
    for(int raw:{0,17000,32767})for(const auto& b:boxes)for(int corner=0;corner<4;++corner) {
        u.params[b.p].base=q15(raw);const auto before=filter_test_bases(u);const auto locks=f.hash();const int dirty=mutations;
        const int x=corner&1?b.x1:b.x0,y=corner&2?b.y1:b.y0;
        CHECK(f.down(x,y));CHECK(filter_focus_selected()==b.p&&g_drag==drags[b.p]);
        CHECK(filter_test_bases(u)==before&&f.hash()==locks&&mutations==dirty);
        f.drag(x,y);CHECK(filter_test_bases(u)==before&&mutations==dirty);filter_test_release();
    }
    for(int p=0;p<4;++p) {
        for(int n=0;n<4;++n)u.params[n].base=13000;
        const auto xy=f.point(p);CHECK(f.down(xy.first,xy.second));filter_test_delta(f,p,6);
        CHECK(u.params[p].base==13000+6*32767/filter_test_span(p));
        for(int n=0;n<4;++n)if(n!=p)CHECK(u.params[n].base==13000);
        filter_test_delta(f,p,1000);CHECK(u.params[p].base==32767);
        filter_test_delta(f,p,-1000);CHECK(u.params[p].base==0);filter_test_release();
    }
    // The former value-dependent overlap must now always belong to CUT, never RES.
    for(int raw:{0,24000,32767}) {
        u.params[0].base=q15(raw);u.params[1].base=4096;
        CHECK(f.down(237,112));CHECK(filter_focus_selected()==0&&g_drag==Drag::FilterCutoff);filter_test_release();
    }
    const auto before=filter_test_bases(u);const int dirty=mutations;
    for(auto xy:{std::pair<int,int>{273,112},{160,198},{150,170},{80,221}}) {
        f.down(xy.first,xy.second);f.drag(xy.first+30,xy.second-20);
        CHECK(filter_test_bases(u)==before&&mutations==dirty);filter_test_release();
    }
    // Context belongs to the gesture, not merely to the current global Drag enum.
    for(int reason=0;reason<6;++reason) {
        const auto xy=f.point(0);f.down(xy.first,xy.second);const auto saved=filter_test_bases(u);const int count=mutations;
        if(reason==0)g_edit_slot=1;
        if(reason==1)g_edit_chain=1;
        if(reason==2)g_overlay=Overlay::None;
        if(reason==3)g_view=View::Sigils;
        if(reason==4)g_seq_lock_armed=true;
        if(reason==5)filter_test_release();
        filter_test_delta(f,0,30);CHECK(filter_test_bases(u)==saved&&mutations==count);
        g_edit_chain=g_edit_slot=0;g_overlay=Overlay::Editor;g_view=View::Chains;g_seq_lock_armed=false;filter_test_release();
    }
    for(int mode=0;mode<5;++mode) {
        const auto previous=u.type;int count=mutations;CHECK(f.down(30+60*mode,38));
        CHECK(u.type==FilterMode(mode)&&mutations==count+(previous!=u.type));
        count=mutations;CHECK(f.down(30+60*mode,38));CHECK(mutations==count);filter_test_release();
    }
    const bool bypass=u.bypass;f.down(260,10);CHECK(u.bypass!=bypass);filter_test_release();
    filter_focus_select(3);filter_focus_select(-1);filter_focus_select(4);CHECK(filter_focus_selected()==3);
}
void test_filter_locks() {
    ModuleInspectorFixture f(UnitKind::Filter);auto& u=*static_cast<FilterUnit*>(f.module);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    const std::pair<int,int> labels[]={{20,58},{290,58},{25,180},{180,180}};
    const auto bases=filter_test_bases(u);
    for(int p=0;p<4;++p) {
        CHECK(f.engine->set_step_lock(0,7,0,p,4000));const auto xy=f.point(p);const auto locked=f.hash();const int dirty=mutations;
        CHECK(f.down(xy.first,xy.second));CHECK(filter_focus_selected()==p&&f.hash()==locked&&mutations==dirty);
        f.drag(xy.first,xy.second);CHECK(f.hash()==locked&&mutations==dirty);
        filter_test_delta(f,p,6);CHECK(seq_display_value(0,p,u.params[p].base)==4000+6*32767/filter_test_span(p));
        CHECK(filter_test_bases(u)==bases);const auto moved=f.hash();g_seq_lock_step=8;
        filter_test_delta(f,p,20);CHECK(f.hash()==moved);g_seq_lock_step=7;filter_test_release();
        f.drag(xy.first+30,xy.second-30);CHECK(f.hash()==moved);
        CHECK(f.down(labels[p].first,labels[p].second));CHECK(seq_display_value(0,p,u.params[p].base)==u.params[p].base);
        CHECK(filter_focus_selected()==p&&filter_test_bases(u)==bases);filter_test_release();
    }
    for(int reason=0;reason<3;++reason) {
        CHECK(f.engine->set_step_lock(0,7,0,0,4000));const auto xy=f.point(0);f.down(xy.first,xy.second);const auto saved=f.hash();
        if(reason==0)g_seq_lock_chain=1;
        if(reason==1)g_seq_lock_armed=false;
        if(reason==2)g_edit_chain=1;
        filter_test_delta(f,0,30);CHECK(f.hash()==saved&&filter_test_bases(u)==bases);
        g_seq_lock_chain=g_edit_chain=0;g_seq_lock_armed=true;filter_test_release();
    }
    // Mode/OFF and bypass are not parameter locks and must never escape into base edits.
    const auto previous=u.type;const bool bypass=u.bypass;const auto saved=f.hash();const int dirty=mutations;
    for(int mode=0;mode<5;++mode){f.down(30+60*mode,38);CHECK(u.type==previous);filter_test_release();}
    f.down(260,10);CHECK(u.bypass==bypass&&mutations==dirty&&f.hash()==saved);filter_test_release();
    f.engine->clear_track(0);delete f.engine->replace_unit(0,1,new FmUnit);
    for(int n=0;n<SEQ_LOCKS;++n)CHECK(f.engine->set_step_lock(0,7,1,n,q15(5000+n)));
    const auto full=f.hash();
    for(int p=0;p<4;++p) {
        const auto xy=f.point(p);f.down(xy.first,xy.second);filter_test_delta(f,p,6);
        CHECK(f.hash()==full&&filter_test_bases(u)==bases&&g_seq_lock_flash>0);filter_test_release();
    }
    CHECK(!param_lens_touch_down(250,232));g_view=View::Chains;f.capture();module_inspector_top();CHECK(module_inspector_text("full"));
}
void test_filter_lens_temp() {
    ModuleInspectorFixture f(UnitKind::Filter);auto& u=*f.module;
    for(int selected=0;selected<4;++selected) {
        f.pick(selected);filter_test_release();f.open();auto& p=u.params[selected];p.base=13000;p.mod_depth=1000;p.snap();
        int dirty=mutations;CHECK(param_lens_touch_down(250,129));CHECK(p.base==13000&&mutations==dirty);
        CHECK(param_lens_touch_drag(256,129));CHECK(p.base==13000+6*32767/271);param_lens_touch_release();
        dirty=mutations;CHECK(param_lens_touch_down(100,166));CHECK(p.mod_depth==1000&&mutations==dirty);
        param_lens_touch_drag(106,166);CHECK(p.mod_depth==1000+6*65534/271);param_lens_touch_release();
        CHECK(param_lens_touch_down(70,185));param_lens_touch_release();CHECK(p.src==ModSource::SigilA);
        // Immediate arrow navigation and B return use retained selection without waiting for capture.
        CHECK(param_lens_touch_down(290,47));param_lens_touch_release();CHECK(filter_focus_selected()==(selected+1)%4);
        u32 down=KEY_LEFT;CHECK(param_lens_input(down,KEY_LEFT,0,false)&&down==0&&filter_focus_selected()==selected);
        lens_permanent_probe(u,selected);CHECK(filter_focus_selected()==selected);f.open();
        down=KEY_B|KEY_X;CHECK(param_lens_input(down,0,0,false)&&down==0&&!param_lens_is_open());
        CHECK(filter_focus_selected()==selected&&g_overlay==Overlay::Editor);f.open();CHECK(filter_focus_selected()==selected);param_lens_cancel();filter_test_release();
    }
    f.pick(0);
    for(bool muted:{false,true}) {
        f.engine->chains[0].mute=muted;f.engine->transport_stop();f.open();lens_permanent_probe(u,0);CHECK(filter_focus_selected()==0);
    }
    f.engine->chains[0].mute=false;const auto saved=filter_test_bases(u);const int dirty=mutations;
    for(int reason=0;reason<6;++reason) {
        lens_quick_begin();
        if(reason==0)g_edit_slot=1;
        if(reason==1)g_edit_chain=1;
        if(reason==2)g_view=View::Sequencer;
        if(reason==3)g_overlay=Overlay::None;
        u32 down=reason==4?KEY_SELECT|KEY_X:0;
        CHECK(param_lens_input(down,KEY_L|KEY_TOUCH,0,reason==5));
        param_lens_touch_drag(295,129);
        CHECK(!param_lens_is_open()&&!f.engine->param_temp_active()&&filter_test_bases(u)==saved&&mutations==dirty);
        g_edit_chain=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;param_lens_touch_release();
    }
}
void test_filter_response_model() {
    namespace model=filter_response_model;constexpr double pi=3.14159265358979323846;
    for(int raw:{-32768,0,1,128,8192,16384,27850,27851,27852,32767}) {
        const double expected=ENGINE_SR/pi*std::asin(std::clamp(raw,1,27851)/65536.0);
        CHECK(std::abs(model::nominal_hz(q15(raw))-expected)<0.001);
        if(raw>=0) {
            char out[32];module_inspector_value_text(out,sizeof out,UnitKind::Filter,0,q15(raw));char* suffix=nullptr;
            const double value=std::strtod(out,&suffix);CHECK(suffix&&std::string(suffix)=="hz"&&std::abs(value-expected)<=0.51);
        }
    }
    CHECK(model::nominal_hz(0)>0&&model::nominal_hz(27851)==model::nominal_hz(32767));
    for(int cutoff:{0,1,8192,27851,32767})for(int res:{0,16000,32767})for(int morph:{0,16384,32767})
        for(int type=0;type<5;++type)for(float hz:{0.1f,20.f,200.f,1000.f,8000.f,15999.f}) {
            const double db=model::magnitude_db(q15(cutoff),q15(res),q15(morph),FilterMode(type),hz);CHECK(std::isfinite(db));
            if(type==4)CHECK(std::abs(db)<0.0001);
            else if(morph==16384)CHECK(std::abs(db-20*std::log10(0.5))<0.0001);
            if(cutoff==27851)CHECK(db==model::magnitude_db(32767,q15(res),q15(morph),FilterMode(type),hz));
        }
    // Independent time-domain linear impulse + DFT oracle. Uses the exact existing
    // Chamberlin update ORDER but no production response helper/transfer polynomial.
    // Saturation, drive soft clipping and fixed-point rounding deliberately excluded.
    for(int cutoff:{2048,12000,27851})for(int res:{0,16000,32767})for(double hz:{31.25,250.,1500.,6000.,14000.}) {
        const double f=cutoff/32768.0,d=std::max(1024,32767-res)/32768.0;
        double lp=0,bp=0;std::array<std::complex<double>,4> h{};
        const auto rotation=std::polar(1.0,-2*pi*hz/ENGINE_SR);std::complex<double> phase=1;
        for(int n=0;n<65536;++n) {
            const double input=n==0?1:0,hp=input-lp-d*bp;bp+=f*hp;lp+=f*bp;
            const double output[]={lp,hp,bp,input-bp};
            for(int type=0;type<4;++type)h[type]+=phase*output[type];
            phase*=rotation;
        }
        CHECK(std::abs(lp)+std::abs(bp)<1e-12);
        for(int type=0;type<4;++type)for(int raw:{0,16384,32767}) {
            const double morph=raw/32768.0,expected=20*std::log10(std::abs(morph+(1-2*morph)*h[type]));
            const double actual=model::magnitude_db(q15(cutoff),q15(res),q15(raw),FilterMode(type),float(hz));
            if(std::abs(actual-expected)>=0.025)std::fprintf(stderr,"filter response cut=%d res=%d morph=%d type=%d hz=%.2f expected=%.6f actual=%.6f\n",cutoff,res,raw,type,hz,expected,actual);
            CHECK(std::abs(actual-expected)<0.025);
        }
    }
}
void filter_test_geometry_pair(int top) {
    module_inspector_geometry();CHECK(top+objects<=8192);maximum_objects=std::max(maximum_objects,objects);
    maximum_shared_objects=std::max(maximum_shared_objects,top+objects);
}
void test_filter_frames() {
    ModuleInspectorFixture f(UnitKind::Filter);auto& u=*static_cast<FilterUnit*>(f.module);
    for(int type=0;type<5;++type)for(int morph:{0,16384,32767})for(int cutoff:{0,27851,32767})for(int res:{0,32767}) {
        u.type=FilterMode(type);u.params[0].base=q15(cutoff);u.params[1].base=q15(res);
        u.params[2].base=q15(res);u.params[3].base=q15(morph);
        for(int p=0;p<4;++p)u.params[p].snap();
        u.reset();filter_focus_select((type+res)%4);f.capture();
        const auto top=module_inspector_top();const int top_count=objects;CHECK(module_inspector_top(false,true)==top);
        module_inspector_surface(false);{ModuleInspectorNoEngine detached;f.lower();}filter_test_geometry_pair(top_count);
        CHECK(module_inspector_text("cut")&&module_inspector_text("res")&&module_inspector_text("morph"));
        const auto lower=pixels;const int bottom_count=objects;
        // No engine/audio/global snapshot/live unit access in either draw. Even a
        // different valid FilterUnit argument cannot change the already captured frame.
        FilterUnit decoy;decoy.type=FilterMode((type+1)%5);decoy.bypass=true;
        for(int p=0;p<4;++p)u.params[p].base=1234;
        g_ui_snapshot={};
        CHECK(module_inspector_top()==top);module_inspector_surface(false);
        {ModuleInspectorNoEngine detached;filter_focus_draw(decoy);}CHECK(pixels==lower&&objects==bottom_count);
    }
    // Distinct stored/control/target/effective layers remain truthful in the upper.
    u.type=FilterMode::LPF;u.params[0].base=13000;u.params[0].effective=8000;u.params[0].src=ModSource::SigilA;
    u.params[0].mod_depth=10000;f.engine->sigils[0].value=16384;filter_focus_select(0);f.capture();
    ParamInspector detail;f.engine->capture_param_inspector(0,0,0,detail);
    CHECK(detail.control!=detail.target&&detail.target!=detail.effective&&detail.control!=detail.effective);
    // Filter's displayed live CUT is the actual internal smoothed scalar, not
    // Param::effective. Reuse the shared geometry checks with that explicit source.
    auto check_detail=[&]() {ParamInspector shown=detail;shown.effective=q15(u.visual_cutoff());module_inspector_detail(shown);};
    module_inspector_top();check_detail();CHECK(module_inspector_text("nominal"));
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,0,4000));f.capture();
    module_inspector_top();CHECK(module_inspector_text("lock"));f.engine->capture_param_inspector(0,0,0,detail);check_detail();
    g_seq_lock_armed=false;f.open();
    for(bool held_mod:{false,true}) {
        if(held_mod){lens_quick_begin();param_lens_touch_down(250,129);param_lens_touch_drag(244,129);}
        f.capture();const auto top=module_inspector_top(true);const int top_count=objects;CHECK(module_inspector_top(true,true)==top);
        module_inspector_surface(false);{ModuleInspectorNoEngine detached;param_lens_draw_bottom();}filter_test_geometry_pair(top_count);
        CHECK(module_inspector_text("base ")&&!module_inspector_text("temp"));
    }
    lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();
    u.bypass=true;f.capture();module_inspector_top();CHECK(module_inspector_text("bypassed"));
    u.bypass=false;f.engine->chains[0].mute=true;f.capture();module_inspector_top();CHECK(module_inspector_text("chain muted"));
    g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top()&&objects==0);
    module_inspector_surface(false);f.lower();CHECK(texts.empty());
}
void dump_filter_runtime(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tparam\ttype\tcutoff_raw\tresonance_raw\tdrive_raw\tmorph_raw\ttop_objects\tbottom_objects\tshared_objects\tbottom_clipped\tbottom_overlaps\n";
    ModuleInspectorFixture f(UnitKind::Filter);auto& u=*static_cast<FilterUnit*>(f.module);
    auto configure=[&](FilterMode type,int cutoff=12000,int res=4096,int drive=0,int morph=0) {
        u.type=type;const q15 values[]={q15(cutoff),q15(res),q15(drive),q15(morph)};
        for(int p=0;p<4;++p){u.params[p].base=values[p];u.params[p].src=ModSource::None;u.params[p].mod_depth=0;u.params[p].snap();}
        u.reset();filter_focus_select(0);
    };
    auto save=[&](const std::string& name,bool lens=false) {
        f.capture();module_inspector_top(lens);const int top=objects;dump_surface(directory,name+"_top");
        module_inspector_surface(false);{ModuleInspectorNoEngine detached;if(lens)param_lens_draw_bottom();else f.lower();}
        filter_test_geometry_pair(top);dump_surface(directory,name+"_bottom");
        manifest<<name<<'\t'<<filter_focus_selected()<<'\t'<<int(u.type);
        for(int p=0;p<4;++p)manifest<<'\t'<<u.params[p].base;
        manifest<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_rects+outside_lines<<'\t'<<text_overlaps<<'\n';
    };
    const char* types[]={"01_lp","02_hp","03_bp","04_notch","05_off"};
    for(int type=0;type<5;++type){configure(FilterMode(type));save(types[type]);}
    configure(FilterMode::LPF,12000,32767,32767);filter_focus_select(1);save("06_resonance");
    configure(FilterMode::LPF,12000,4096,0,16384);filter_focus_select(3);save("07_morph_mid");
    configure(FilterMode::LPF,12000,4096,0,32767);filter_focus_select(3);save("08_morph_end");
    configure(FilterMode::LPF,0);save("09_cut_min");configure(FilterMode::LPF,32767);save("10_cut_max");
    configure(FilterMode::LPF);g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,0,4000));save("11_lock");g_seq_lock_armed=false;
    u.params[0].src=ModSource::SigilB;u.params[0].mod_depth=10000;f.open();save("12_mod",true);
    lens_quick_begin();param_lens_touch_down(180,129);param_lens_touch_drag(204,129);save("13_temp",true);
    lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();filter_test_release();save("14_return");
}
