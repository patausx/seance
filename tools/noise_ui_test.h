// Actual Noise UI/controller checks, in the existing shared native harness.
struct NoiseFixture : ModuleInspectorFixture {
    NoiseFixture():ModuleInspectorFixture(UnitKind::Noise){noise_focus_release();configure();capture();}
    ~NoiseFixture(){noise_focus_release();param_lens_cancel();}
    NoiseUnit& noise(){return *static_cast<NoiseUnit*>(module);}
    void set(int p,q15 v){module->params[p].base=v;module->params[p].snap();}
    void configure(){
        const q15 defaults[]={10922,20316,16384,24576,0,32767,0,32767};
        for(int p=0;p<8;++p){module->params[p].src=ModSource::None;module->params[p].mod_depth=0;set(p,defaults[p]);}
        module->bypass=false;engine->chains[0].mute=false;noise_focus_select(NoiseUnit::P_DENSITY);
        noise().reset();noise().control_tick();
    }
    bool down(int x,int y){return noise_focus_touch_body(noise(),x,y);}
    void drag(int x,int y){noise_focus_drag(noise(),x,y);}
    void release(){noise_focus_release();g_drag=Drag::None;}
    std::array<q15,8> values(){std::array<q15,8> v{};for(int p=0;p<8;++p)v[p]=module->params[p].base;return v;}
    std::vector<u32> bottom(bool lens=false){
        module_inspector_surface(false);{ModuleInspectorNoEngine no;if(lens)param_lens_draw_bottom();else noise_focus_draw(noise());}
        module_inspector_geometry();return pixels;
    }
    void feed(int frames=512){std::array<q15,512> buffer{};while(frames>0){const int n=std::min(512,frames);noise().process(buffer.data(),n);frames-=n;}capture();}
};
void test_noise_controls_locks(){
    NoiseFixture f;auto& u=f.noise();const auto original=f.values();
    struct Point{int p,x,y,width;};const Point points[]={{1,80,92,138},{3,240,92,128},{2,80,180,138},{4,240,180,128},{6,80,215,138},{5,240,215,128}};
    for(const auto& p:points){
        f.configure();const int dirty=mutations;CHECK(f.down(p.x,p.y));CHECK(noise_focus_selected()==p.p&&f.values()==original);
        f.drag(p.x,p.y-20);CHECK(f.values()==original&&mutations==dirty);
        const int direction=original[p.p]>30000?-1:1;f.drag(p.x+direction*6,p.y);
        CHECK(u.params[p.p].base==original[p.p]+direction*6*32767/p.width);
        for(int other=0;other<8;++other)if(other!=p.p)CHECK(u.params[other].base==original[other]);
        f.release();
    }
    f.configure();for(int i=0;i<4;++i){const int x[]={69,128,179,260};f.down(x[i],45);CHECK(u.params[0].base==i*32767/3&&noise_focus_selected()==0);}
    const q15 decay=u.params[5].base;f.down(20,137);CHECK(u.params[7].base==16384&&noise_focus_selected()==7);f.down(87,137);CHECK(u.params[7].base==32767&&u.params[5].base==decay);
    for(auto xy:{std::pair<int,int>{200,122},{200,144},{14,103},{164,185},{164,220}}){const auto v=f.values();const int d=mutations;f.down(xy.first,xy.second);f.drag(xy.first+30,xy.second);CHECK(f.values()==v&&mutations==d);}
    f.configure();g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    CHECK(f.engine->set_step_lock(0,7,0,6,4000));f.down(80,215);f.drag(86,215);CHECK(seq_display_value(0,6,0)==4000+6*32767/138&&f.values()==original);
    const auto locked=f.hash();g_seq_lock_step=8;f.drag(120,215);CHECK(f.hash()==locked);g_seq_lock_step=7;f.release();
    f.down(14,195);CHECK(seq_display_value(0,6,0)==0);f.down(20,137);CHECK(seq_display_value(0,7,32767)==16384&&u.params[7].base==32767);f.down(14,118);CHECK(seq_display_value(0,7,32767)==32767);
    f.down(69,45);CHECK(seq_display_value(0,0,10922)==0&&u.params[0].base==10922);f.down(14,33);CHECK(seq_display_value(0,0,10922)==10922);
    f.down(258,10);CHECK(!u.bypass&&!param_lens_touch_down(250,232));
    g_seq_lock_chain=1;const auto wrong=f.hash();f.down(80,215);f.drag(110,215);f.down(20,137);f.down(69,45);CHECK(f.hash()==wrong&&f.values()==original);g_seq_lock_chain=0;
    f.engine->clear_track(0);delete f.engine->replace_unit(0,1,new FmUnit);
    for(int i=0;i<SEQ_LOCKS;++i)CHECK(f.engine->set_step_lock(0,7,1,i,1000));
    const auto full=f.hash();f.down(80,215);f.drag(90,215);f.down(20,137);CHECK(f.hash()==full&&f.values()==original&&g_seq_lock_flash>0);f.capture();f.bottom();CHECK(module_inspector_text("lock full"));
    g_seq_lock_armed=false;g_view=View::Chains;f.down(80,215);g_overlay=Overlay::None;f.drag(120,215);CHECK(f.values()==original);g_overlay=Overlay::Editor;
    f.down(80,215);Unit* previous=f.engine->replace_unit(0,0,new NoiseUnit);f.drag(120,215);CHECK(f.values()==original);delete f.engine->replace_unit(0,0,previous);
}
void test_noise_lens_frames(){
    NoiseFixture f;auto& u=f.noise();CHECK(module_inspector_supports(UnitKind::Noise));char text[24];
    for(auto point:{std::pair<int,const char*>{0,"0ms"},{1,"1ms"},{32767,"2.00s"}}){module_inspector_value_text(text,sizeof text,UnitKind::Noise,6,q15(point.first));CHECK(std::string(text)==point.second);}
    for(int p:{NoiseUnit::P_ATTACK,NoiseUnit::P_MODE}){
        f.configure();noise_focus_select(p);f.open();CHECK(noise_focus_selected()==p);
        const q15 value=u.params[p].base;param_lens_touch_down(150,129);CHECK(u.params[p].base==value);param_lens_touch_drag(156,129);param_lens_touch_release();CHECK(u.params[p].base==std::min(32767,value+6*32767/271));
        for(int source=0;source<6;++source){param_lens_touch_down(20+51*source,185);param_lens_touch_release();CHECK(int(u.params[p].src)==source);}
        param_lens_touch_down(70,185);param_lens_touch_release();param_lens_touch_down(150,166);param_lens_touch_drag(130,166);param_lens_touch_release();CHECK(u.params[p].mod_depth==-20*65534/271);
        lens_permanent_probe(u,p);CHECK(noise_focus_selected()==p);f.open();f.capture();module_inspector_top(true);CHECK(!module_inspector_text("temporary"));f.bottom(true);
        CHECK(module_inspector_text("base ")&&!module_inspector_text("temp"));param_lens_cancel();CHECK(noise_focus_selected()==p);
    }
    f.configure();f.feed();const auto bottom=f.bottom();CHECK(module_inspector_text("unused in pink"));
    const auto top=module_inspector_top(false,true);CHECK(module_inspector_text("pink ignores density"));const int top_count=objects;
    g_ui_snapshot={};f.set(6,32767);u.bypass=true;CHECK(f.bottom()==bottom&&module_inspector_top()==top);fold_test_geometry_pair(top_count);
    u.bypass=false;f.capture();u.params[0].effective=21844;f.capture();f.bottom();CHECK(module_inspector_text("event probability"));module_inspector_top();CHECK(module_inspector_text("crackle: event probability"));
    u.bypass=true;f.capture();f.bottom();CHECK(module_inspector_text("bypassed")&&module_inspector_text("--"));
    u.bypass=false;f.engine->chains[0].mute=true;f.capture();f.bottom();CHECK(module_inspector_text("muted")&&module_inspector_text("--"));f.engine->chains[0].mute=false;
    f.configure();f.set(7,0);f.capture();f.bottom();CHECK(module_inspector_text("auto"));noise_focus_select(7);f.capture();module_inspector_top();CHECK(module_inspector_text("legacy auto"));
    // Identity mismatch cannot borrow another generator's peak/envelope.
    f.engine->capture_ui_snapshot(g_ui_snapshot,0,0);g_ui_snapshot.effect_focus_slot=1;noise_focus_capture();f.bottom();CHECK(module_inspector_text("--"));
    // Real attack produces changing envelope scalars and thus meter pixels.
    f.configure();f.set(6,16384);f.set(7,16384);u.reset();u.control_tick();u.trigger();f.feed(3200);f.bottom();CHECK(module_inspector_text("20%"));
    CHECK(g_ui_snapshot.generator_phase_hi*256+g_ui_snapshot.generator_phase_lo==u.visual_envelope());const auto rising=f.bottom();f.feed(3200);CHECK(f.bottom()!=rising);
    // Each selected control is formatted and stays inside the native budget.
    for(int p=0;p<8;++p){noise_focus_select(p);f.capture();module_inspector_top();const int count=objects;f.bottom();fold_test_geometry_pair(count);}
    g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top());module_inspector_surface(false);noise_focus_draw(u);CHECK(texts.empty());
}
void dump_noise_runtime(const std::filesystem::path& directory){
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\ttop_objects\tbottom_objects\tshared_objects\n";
    NoiseFixture f;
    auto save=[&](const char* name,bool lens=false){f.capture();module_inspector_top(lens,true);const int top=objects;dump_surface(directory,std::string(name)+"_top");f.bottom(lens);fold_test_geometry_pair(top);dump_surface(directory,std::string(name)+"_bottom");manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\n';};
    f.feed();save("01_pink_drone");
    f.set(0,21844);f.set(1,3932);f.set(5,14000);f.set(6,16384);f.set(7,16384);f.noise().reset();f.noise().control_tick();f.noise().trigger();noise_focus_select(6);f.feed(3200);save("02_crackle_attack");
    f.feed(30000);noise_focus_select(7);save("03_hit_rest");
    noise_focus_select(6);f.open();param_lens_touch_down(70,185);param_lens_touch_release();param_lens_touch_down(150,166);param_lens_touch_drag(170,166);param_lens_touch_release();save("04_attack_mod",true);
}
