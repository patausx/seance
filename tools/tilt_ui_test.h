// Actual C++ UI harness; shares Fold/Bit geometry, lens and frozen-draw checks.
std::array<q15,6> tilt_bases(const Unit& u) {std::array<q15,6> a;for(int p=0;p<6;++p)a[p]=u.params[p].base;return a;}
void tilt_config(Unit& u) {
    const q15 v[]={27852,16384,0,32767,21846,tilt_eq::TRIM_UNITY};
    for(int p=0;p<6;++p){u.params[p].base=v[p];u.params[p].src=ModSource::None;u.params[p].mod_depth=0;u.params[p].snap();}
}
int tilt_span(int p){return p==0?118:p==1?102:62;}
void test_tilt_controls() {
    ModuleInspectorFixture f(UnitKind::Tilt);auto& u=*static_cast<TiltUnit*>(f.module);tilt_config(u);
    struct Box{int x,y,w,h;};const Box boxes[]={{8,27,134,46},{196,27,116,46},{84,177,72,44},{236,177,72,44},{8,177,72,44},{160,177,72,44}};
    for(int p=0;p<6;++p){const auto b=boxes[p];const auto xy=f.point(p);const int span=tilt_span(p),range=p==0?65534:32767;
        for(int edge:{p==0?-32767:0,32767})for(int corner=0;corner<4;++corner){u.params[p].base=edge;const auto before=tilt_bases(u);
            CHECK(f.down(b.x+(corner&1?b.w-1:0),b.y+(corner&2?b.h-1:0)));CHECK(tilt_focus_selected()==p&&tilt_bases(u)==before);tilt_focus_release();g_drag=Drag::None;}
        u.params[p].base=10000;auto before=tilt_bases(u);CHECK(f.down(xy.first,xy.second));f.drag(xy.first,xy.second-30);CHECK(tilt_bases(u)==before);
        f.drag(xy.first+6,xy.second);before[p]=10000+6*range/span;CHECK(tilt_bases(u)==before);
        f.drag(xy.first+1000,xy.second);CHECK(u.params[p].base==32767);f.drag(xy.first-1000,xy.second);CHECK(u.params[p].base==(p==0?-32767:0));tilt_focus_release();
    }
    tilt_config(u);auto before=tilt_bases(u);const int dirty=mutations;
    for(auto xy:{std::pair<int,int>{155,118},{144,50},{80,190},{158,180},{190,45}}){f.down(xy.first,xy.second);f.drag(xy.first+30,xy.second);CHECK(tilt_bases(u)==before&&mutations==dirty);}
    for(auto xy:{std::pair<int,int>{148,27},{187,72},{168,50}}){u.params[0].base=27852;before=tilt_bases(u);f.down(xy.first,xy.second);before[0]=0;CHECK(tilt_bases(u)==before);f.drag(305,50);CHECK(tilt_bases(u)==before);}
    const int zero_dirty=mutations;f.down(168,50);CHECK(mutations==zero_dirty);
    f.down(260,10);CHECK(u.bypass);f.down(260,10);CHECK(!u.bypass);
    f.down(73,65);g_edit_slot=1;f.drag(80,65);CHECK(tilt_bases(u)==before);g_edit_slot=0;
    tilt_focus_select(5);tilt_focus_select(-1);tilt_focus_select(6);CHECK(tilt_focus_selected()==5);
    u.set_modern(false);f.capture();CHECK(tilt_focus_selected()==0);before=tilt_bases(u);
    f.down(197,214);f.drag(227,214);CHECK(!u.modern()&&tilt_bases(u)==before);f.pick(4);f.open();param_lens_touch_down(290,47);param_lens_touch_release();CHECK(tilt_focus_selected()==0);
    param_lens_cancel();
}
void test_tilt_locks() {
    ModuleInspectorFixture f(UnitKind::Tilt);auto& u=*f.module;tilt_config(u);const auto bases=tilt_bases(u);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    const int labels[]={14,202,90,242,14,166};
    for(int p=0;p<6;++p){const auto xy=f.point(p);const int range=p==0?65534:32767;
        CHECK(f.engine->set_step_lock(0,7,0,p,4000));f.down(xy.first,xy.second);f.drag(xy.first+6,xy.second);
        CHECK(seq_display_value(0,p,bases[p])==4000+6*range/tilt_span(p)&&tilt_bases(u)==bases);
        const auto locked=f.hash();g_seq_lock_step=8;f.drag(xy.first+12,xy.second);CHECK(f.hash()==locked);g_seq_lock_step=7;
        f.down(labels[p],p<2?32:182);CHECK(seq_display_value(0,p,bases[p])==bases[p]);
    }
    // Zero writes a real zero lock even with a zero base: modulation must not turn it into a no-op.
    f.down(168,50);CHECK(seq_display_value(0,0,bases[0])==0&&tilt_bases(u)==bases);f.down(14,32);
    u.params[0].base=0;f.down(168,50);CHECK(f.engine->pattern.steps[0][7].lock_count==1);u.params[0].base=bases[0];f.down(14,32);
    f.down(260,10);CHECK(!u.bypass);CHECK(!param_lens_touch_down(250,232));
    for(int i=0;i<SEQ_LOCKS;++i)CHECK(f.engine->set_step_lock(0,7,1,i,1234));
    const auto full=f.hash();f.down(168,50);CHECK(f.hash()==full&&g_seq_lock_flash>0&&tilt_bases(u)==bases);
    f.down(197,214);f.drag(203,214);CHECK(f.hash()==full&&tilt_bases(u)==bases);
    CHECK(f.engine->remove_step_lock(0,7,1,0));CHECK(f.engine->set_step_lock(0,7,0,0,12000));
    f.down(168,50);CHECK(seq_display_value(0,0,bases[0])==0&&tilt_bases(u)==bases);
    const auto locked=f.hash();g_seq_lock_chain=1;f.down(168,50);f.down(73,65);f.drag(90,65);CHECK(f.hash()==locked&&tilt_bases(u)==bases);
}
void test_tilt_lens() {
    ModuleInspectorFixture f(UnitKind::Tilt);auto& u=*f.module;
    for(int p=0;p<6;++p){const int range=p==0?65534:32767;u.params[p].base=13000;u.params[p].mod_depth=1000;u.params[p].snap();
        f.pick(p);f.open();CHECK(tilt_focus_selected()==p);param_lens_touch_down(250,129);CHECK(u.params[p].base==13000);
        param_lens_touch_drag(256,129);param_lens_touch_release();CHECK(u.params[p].base==13000+6*range/271);
        param_lens_touch_down(100,166);CHECK(u.params[p].mod_depth==1000);param_lens_touch_drag(106,166);param_lens_touch_release();CHECK(u.params[p].mod_depth==1000+6*65534/271);
        param_lens_touch_down(280,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::EnvFollow);
        param_lens_touch_down(70,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);
        param_lens_touch_down(290,47);param_lens_touch_release();CHECK(tilt_focus_selected()==(p+1)%6);
        u32 down=KEY_LEFT;param_lens_input(down,KEY_LEFT,0,false);CHECK(tilt_focus_selected()==p);
        lens_permanent_probe(u,p);CHECK(tilt_focus_selected()==p);f.open();
        f.capture();module_inspector_top(true);const int top=objects;module_inspector_surface(false);{ModuleInspectorNoEngine no;param_lens_draw_bottom();}fold_test_geometry_pair(top);
        CHECK(!module_inspector_text("temp")&&!module_inspector_text("audition"));
        down=KEY_B;param_lens_input(down,0,0,false);CHECK(!param_lens_is_open()&&tilt_focus_selected()==p);
    }
    lens_quick_begin();const auto saved=tilt_bases(u);g_edit_slot=1;f.capture();CHECK(!param_lens_is_open()&&!f.engine->param_temp_active()&&tilt_bases(u)==saved);g_edit_slot=0;
}
void test_tilt_model() {
    namespace t=tilt_response_model;
    // Independent time-domain linear recurrence, DFT of impulse. DSP integer LUT
    // and gain selection themselves have the separate frozen/samplewise oracle.
    int comparisons=0;
    for(bool modern:{false,true})for(int range:{0,1,21846,32767})for(int tilt:{-32767,0,27852})for(int mix:{0,16384,32767}){
        q15 v[]={q15(tilt),16384,10923,q15(mix),q15(range),q15(19660)};
        const bool legacy=!modern && range==0;const double a=tilt_eq::coeff(v[1],legacy)/32768.0;
        uint32_t l,h;tilt_eq::shelf_gains(v[0],v[4],modern,l,h);
        const double gl=legacy?1-tilt/32768.0:l/65536.0,gh=legacy?1+tilt/32768.0:h/65536.0;
        const double drive=legacy?(32767.0+v[2]*3.0)/32768.0:1+v[2]*6.0/65536.0;
        const double trim=modern?tilt_eq::trim_gain_q16(v[5])/65536.0:1,wet=mix/32768.0;
        for(int hz:{20,100,1000,8000,16000}){double low=0,re=0,im=0;const double w=2*3.14159265358979323846*hz/32000;
            for(int n=0;n<8192;++n){const double dry=n==0?1:0;low+=(dry-low)*a;
                const double y=dry*(1-wet)+(low*gl+(dry-low)*gh)*drive*trim*wet;re+=y*std::cos(w*n);im-=y*std::sin(w*n);}
            const double expected=10*std::log10(std::max(1e-24,re*re+im*im));CHECK(std::abs(t::magnitude_db(v,modern,hz)-expected)<0.001);++comparisons;
        }
        t::Plot plot;t::plot(v,modern,false,plot);for(auto y:plot.y)CHECK(y>=93&&y<=151);
        t::plot(v,modern,true,plot);for(auto y:plot.y)CHECK(y==122);CHECK(!plot.over&&!plot.under);
    }
    q15 neutral[]={0,16384,0,32767,0,tilt_eq::TRIM_UNITY};for(int hz:{20,1000,16000})CHECK(std::abs(t::magnitude_db(neutral,true,hz))<0.00001);
    neutral[5]=0;t::Plot clipped;t::plot(neutral,true,false,clipped);CHECK(clipped.under&&!clipped.over);
    neutral[2]=32767;neutral[5]=32767;t::plot(neutral,true,false,clipped);CHECK(clipped.over);
    char text[32];module_inspector_value_text(text,sizeof text,UnitKind::Tilt,4,32767);CHECK(std::string(text)=="11.625db");
    module_inspector_value_text(text,sizeof text,UnitKind::Tilt,5,tilt_eq::TRIM_UNITY);CHECK(std::string(text)=="+0.0db");
    module_inspector_value_text(text,sizeof text,UnitKind::Tilt,4,0,true,false,false,true);CHECK(std::string(text)=="legacy");
    std::printf("tilt model: %d independent recurrence DFT comparisons\n",comparisons);
}
void test_tilt_frames() {
    ModuleInspectorFixture f(UnitKind::Tilt);auto& u=*static_cast<TiltUnit*>(f.module);
    int frames=0;
    for(bool modern:{false,true})for(int p=0;p<(modern?6:5);++p)for(int edge:{0,32767})for(int tilt:{-32767,0,32767}){
        u.set_modern(modern);tilt_config(u);u.params[p].base=edge;u.params[0].base=tilt;for(int n=0;n<6;++n)u.params[n].snap();tilt_focus_select(p);f.capture();
        const auto top=module_inspector_top();const int count=objects;CHECK(module_inspector_top(false,true)==top);module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}fold_test_geometry_pair(count);
        const auto lower=pixels;const int lower_count=objects;const auto saved=u.params[0].base;u.params[0].base=12345;g_ui_snapshot={};module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}
        CHECK(pixels==lower&&objects==lower_count);CHECK(module_inspector_top()==top);u.params[0].base=saved;++frames;
    }
    // The old RANGE switch can itself be modulated: stored Hz and effective Hz
    // must use their respective captured laws, not silently relabel the base.
    u.set_modern(false);tilt_config(u);u.params[4].base=0;u.params[4].effective=8192;tilt_focus_select(1);f.capture();
    module_inspector_top();CHECK(module_inspector_text("stored base 1079hz"));
    bool live_hz=false;for(const auto& t:texts)if(t.call_x==254&&t.call_y==60&&t.text=="1000hz")live_hz=true;CHECK(live_hz);
    for(int state=0;state<2;++state){u.bypass=state==0;f.engine->chains[0].mute=state==1;f.capture();module_inspector_top();CHECK(module_inspector_text(state==0?"bypassed":"chain muted"));}
    g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top());module_inspector_surface(false);f.lower();CHECK(texts.empty());
    std::printf("tilt frozen extrema: %d pairs\n",frames);
}
void dump_tilt_runtime(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\ttop_objects\tbottom_objects\tshared_objects\tbottom_clipped\tbottom_overlaps\n";
    ModuleInspectorFixture f(UnitKind::Tilt);auto& u=*static_cast<TiltUnit*>(f.module);
    auto reset=[&]{u.set_modern(true);tilt_config(u);tilt_focus_select(0);};reset();
    // Match the approved osc / tilt / vca breadcrumb; actual chain, not painted labels.
    Unit* moved=f.engine->replace_unit(0,0,new OscUnit);delete f.engine->replace_unit(0,1,moved);delete f.engine->replace_unit(0,2,new VcaUnit);g_edit_slot=1;
    auto save=[&](const std::string& name,bool lens=false){f.capture();module_inspector_top(lens,true);const int top=objects;dump_surface(directory,name+"_top");
        module_inspector_surface(false);{ModuleInspectorNoEngine no;if(lens)param_lens_draw_bottom();else f.lower();}fold_test_geometry_pair(top);dump_surface(directory,name+"_bottom");
        manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_lines+outside_rects<<'\t'<<text_overlaps<<'\n';};
    save("01_air");u.params[0].base=-27852;u.params[0].snap();save("02_body");f.down(168,50);u.params[0].snap();save("03_zero");
    reset();u.params[2].base=10923;u.params[5].base=19660;u.params[2].snap();u.params[5].snap();tilt_focus_select(5);save("04_trim");
    const char* names[]={"05_pivot","06_range","07_drive","08_dry","09_trim_low","10_trim_high"};const int param[]={1,4,2,3,5,5},val[]={32767,32767,32767,0,0,32767};
    for(int i=0;i<6;++i){reset();u.params[param[i]].base=val[i];u.params[param[i]].snap();tilt_focus_select(param[i]);save(names[i]);}
    reset();u.params[0].src=ModSource::SigilB;u.params[0].mod_depth=-18000;u.params[0].effective=-6000;save("11_live_mod");
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,1,0,-18000));save("12_lock");f.down(168,50);save("13_zero_lock");g_seq_lock_armed=false;
    reset();tilt_focus_select(5);f.open();save("14_mod",true);lens_quick_begin();param_lens_touch_down(180,129);param_lens_touch_drag(158,129);save("15_temp",true);
    lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();tilt_focus_release();
    reset();u.bypass=true;save("16_bypass");u.bypass=false;f.engine->chains[0].mute=true;save("17_muted");f.engine->chains[0].mute=false;
    reset();u.set_modern(false);u.params[4].base=0;u.params[4].snap();tilt_focus_select(1);save("18_legacy_zero");u.params[4].base=21846;u.params[4].snap();tilt_focus_select(4);save("19_legacy_stepped");
}
