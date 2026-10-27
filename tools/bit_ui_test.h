// Focused Bit coverage in the existing native C++ UI harness.
std::array<q15,7> bit_bases(const Unit& u) {
    std::array<q15,7> out;for(int p=0;p<7;++p)out[p]=u.params[p].base;return out;
}
void bit_config(Unit& u) {
    const q15 values[]={8000,24500,32767,0,0,0,0};
    for(int p=0;p<7;++p){u.params[p].base=values[p];u.params[p].src=ModSource::None;u.params[p].mod_depth=0;u.params[p].snap();}
}
void test_bit_controls() {
    ModuleInspectorFixture f(UnitKind::Bitcrush);auto& u=*f.module;
    struct Box{int x,y,w,h;};
    const Box boxes[]={{8,27,96,46},{112,27,96,46},{216,27,96,46},{8,172,72,48},{84,172,72,48},{160,172,72,48},{236,172,76,48}};
    for(int p=0;p<7;++p) {
        const auto b=boxes[p];const auto xy=f.point(p);const int span=p<3?82:p==6?62:60,range=p==5?65534:32767;
        for(int edge:{p==5?-32767:0,32767})for(int corner=0;corner<4;++corner) {
            u.params[p].base=edge;const auto before=bit_bases(u);
            CHECK(f.down(b.x+(corner&1?b.w-1:0),b.y+(corner&2?b.h-1:0)));
            CHECK(bit_focus_selected()==p&&bit_bases(u)==before);bit_focus_release();g_drag=Drag::None;
        }
        u.params[p].base=10000;auto before=bit_bases(u);CHECK(f.down(xy.first,xy.second));
        f.drag(xy.first,xy.second-30);CHECK(bit_bases(u)==before);
        f.drag(xy.first+6,xy.second);before[p]=10000+6*range/span;CHECK(bit_bases(u)==before);
        f.drag(xy.first+1000,xy.second);CHECK(u.params[p].base==32767);
        f.drag(xy.first-1000,xy.second);CHECK(u.params[p].base==(p==5?-32767:0));bit_focus_release();
    }
    const auto before=bit_bases(u);const int dirty=mutations;
    for(auto xy:{std::pair<int,int>{155,118},{108,50},{80,190},{158,180}}) {
        f.down(xy.first,xy.second);f.drag(xy.first+30,xy.second);CHECK(bit_bases(u)==before&&mutations==dirty);
    }
    f.down(260,10);CHECK(u.bypass);f.down(260,10);CHECK(!u.bypass);
    f.down(55,65);g_edit_slot=1;f.drag(80,65);CHECK(bit_bases(u)==before);g_edit_slot=0;
    bit_focus_select(6);bit_focus_select(-1);bit_focus_select(7);CHECK(bit_focus_selected()==6);
}
void test_bit_locks() {
    ModuleInspectorFixture f(UnitKind::Bitcrush);auto& u=*f.module;const auto bases=bit_bases(u);
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    const int labels[]={14,118,222,14,90,166,242};
    for(int p=0;p<7;++p) {
        const auto xy=f.point(p);const int span=p<3?82:p==6?62:60,range=p==5?65534:32767;
        CHECK(f.engine->set_step_lock(0,7,0,p,4000));f.down(xy.first,xy.second);f.drag(xy.first+6,xy.second);
        CHECK(seq_display_value(0,p,bases[p])==4000+6*range/span&&bit_bases(u)==bases);
        const auto locked=f.hash();g_seq_lock_step=8;f.drag(xy.first+12,xy.second);CHECK(f.hash()==locked);g_seq_lock_step=7;
        f.down(labels[p],p<3?32:177);CHECK(seq_display_value(0,p,bases[p])==bases[p]);
    }
    f.down(260,10);CHECK(!u.bypass);CHECK(!param_lens_touch_down(250,232));
    for(int i=0;i<SEQ_LOCKS;++i)CHECK(f.engine->set_step_lock(0,7,1,i,1234));
    const auto full=f.hash();f.down(272,213);f.drag(280,213);CHECK(f.hash()==full&&g_seq_lock_flash>0&&bit_bases(u)==bases);
}
void test_bit_lens() {
    ModuleInspectorFixture f(UnitKind::Bitcrush);auto& u=*f.module;
    for(int p=0;p<7;++p) {
        const int range=p==5?65534:32767;u.params[p].base=13000;u.params[p].mod_depth=1000;u.params[p].snap();
        f.pick(p);f.open();CHECK(bit_focus_selected()==p);
        param_lens_touch_down(250,129);CHECK(u.params[p].base==13000);
        param_lens_touch_drag(256,129);param_lens_touch_release();CHECK(u.params[p].base==13000+6*range/271);
        param_lens_touch_down(100,166);CHECK(u.params[p].mod_depth==1000);
        param_lens_touch_drag(106,166);param_lens_touch_release();CHECK(u.params[p].mod_depth==1000+6*65534/271);
        param_lens_touch_down(70,185);param_lens_touch_release();CHECK(u.params[p].src==ModSource::SigilA);
        param_lens_touch_down(290,47);param_lens_touch_release();CHECK(bit_focus_selected()==(p+1)%7);
        u32 down=KEY_LEFT;param_lens_input(down,KEY_LEFT,0,false);CHECK(bit_focus_selected()==p);
        lens_permanent_probe(u,p);CHECK(bit_focus_selected()==p);f.open();
        f.capture();module_inspector_top(true);const int top=objects;
        module_inspector_surface(false);{ModuleInspectorNoEngine no;param_lens_draw_bottom();}fold_test_geometry_pair(top);
        CHECK(!module_inspector_text("temp")&&!module_inspector_text("audition"));
        down=KEY_B;param_lens_input(down,0,0,false);CHECK(!param_lens_is_open()&&bit_focus_selected()==p);
    }
}
void test_bit_reference() {
    namespace b=bit_reference;
    for(int edge:{0,32767})for(int drift:{-32767,0,32767})for(bool bypass:{false,true}) {
        q15 params[]={q15(edge),q15(edge),q15(edge),q15(edge),q15(edge),q15(drift),q15(edge)};
        b::Plot a,c;b::plot(params,bypass,a);b::plot(params,bypass,c);CHECK(std::equal(a.source,a.source+288,c.source)&&std::equal(a.shaped,a.shaped+288,c.shaped));
        BitUnit actual;for(int p=0;p<7;++p)actual.params[p].effective=params[p];
        for(int i=0;i<288;++i) {
            const int phase=i%96;const int t=phase<24?phase:phase<72?48-phase:phase-96;
            q15 sample=q15(t*24575/24);CHECK(sample==b::source_sample(i));const q15 source=sample;
            if(!bypass)actual.process(&sample,1);
            CHECK(a.source[i]==119-int(source)*32/32768&&a.shaped[i]==119-int(sample)*32/32768);
        }
    }
    char value[32];
    for(int raw:{0,1,8192,16384,32767}) {
        module_inspector_value_text(value,sizeof value,UnitKind::Bitcrush,0,raw);CHECK(std::atoi(value)==3+(raw*13>>15));
        module_inspector_value_text(value,sizeof value,UnitKind::Bitcrush,1,raw);const int hold=1+(32767-raw)*31/32767;
        CHECK(std::atoi(value)==(32000+hold/2)/hold);
    }
    module_inspector_value_text(value,sizeof value,UnitKind::Bitcrush,5,-32767);CHECK(std::string(value)=="-125.0hz");
    module_inspector_value_text(value,sizeof value,UnitKind::Bitcrush,3,32767);CHECK(std::string(value)=="-3/+3smp");
}
void test_bit_frames() {
    ModuleInspectorFixture f(UnitKind::Bitcrush);auto& u=*f.module;
    for(int p=0;p<7;++p)for(int edge:{0,32767})for(int drift:{-32767,0,32767}) {
        bit_config(u);u.params[p].base=edge;u.params[5].base=drift;
        for(int n=0;n<7;++n)u.params[n].snap();
        bit_focus_select(p);f.capture();
        const auto top=module_inspector_top();const int count=objects;CHECK(module_inspector_top(false,true)==top);
        module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}fold_test_geometry_pair(count);
        const auto lower=pixels;const int lower_count=objects;const q15 saved=u.params[0].base;
        u.params[0].base=12345;g_ui_snapshot={};module_inspector_surface(false);{ModuleInspectorNoEngine no;f.lower();}
        CHECK(pixels==lower&&objects==lower_count);CHECK(module_inspector_top()==top);u.params[0].base=saved;
    }
    for(int state=0;state<2;++state){u.bypass=state==0;f.engine->chains[0].mute=state==1;f.capture();module_inspector_top();CHECK(module_inspector_text(state==0?"bypassed":"chain muted"));}
    g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top());module_inspector_surface(false);f.lower();CHECK(texts.empty());
}
void dump_bit_runtime(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\ttop_objects\tbottom_objects\tshared_objects\tbottom_clipped\tbottom_overlaps\n";
    ModuleInspectorFixture f(UnitKind::Bitcrush);auto& u=*f.module;bit_config(u);bit_focus_select(0);
    auto save=[&](const std::string& name,bool lens=false) {
        f.capture();module_inspector_top(lens,true);const int top=objects;dump_surface(directory,name+"_top");
        module_inspector_surface(false);{ModuleInspectorNoEngine no;if(lens)param_lens_draw_bottom();else f.lower();}
        fold_test_geometry_pair(top);dump_surface(directory,name+"_bottom");
        manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_lines+outside_rects<<'\t'<<text_overlaps<<'\n';
    };
    save("01_bit");
    const char* names[]={"02_3bits","03_slow_clock","04_dry","05_jitter","06_dropout","07_drift","08_comp"};
    for(int p=0;p<7;++p){bit_config(u);u.params[p].base=p<3?0:p==5?-32767:32767;u.params[p].snap();bit_focus_select(p);save(names[p]);}
    bit_config(u);u.params[0].src=ModSource::SigilB;u.params[0].mod_depth=12000;u.params[0].effective=17000;bit_focus_select(0);save("09_live_mod");
    g_seq_lock_armed=true;g_seq_lock_step=7;CHECK(f.engine->set_step_lock(0,7,0,0,0));save("10_lock");g_seq_lock_armed=false;
    bit_focus_select(5);f.open();save("11_mod",true);lens_quick_begin();param_lens_touch_down(180,129);param_lens_touch_drag(204,129);save("12_temp",true);
    lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();bit_focus_release();
    u.bypass=true;save("13_bypass");u.bypass=false;f.engine->chains[0].mute=true;save("14_muted");
}
