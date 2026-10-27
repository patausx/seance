// Preserved old eight-parameter AHD cases, updated only for its new native-needle
// layout. New modes/all banks/legacy AD remain covered in vca_extended_ui_cases.inc.
// Focused real flexible VCA controller/capture/drawing integration.
// Included after module_inspector_test.h; no Python-authored UI in these fixtures.
struct VcaFixture : ModuleInspectorFixture {
    using V=VcaUnit;
    VcaFixture():ModuleInspectorFixture(UnitKind::Vca) {u().set_flexible(true);seed();}
    V& u() {return *static_cast<V*>(module);}
    void seed(bool duck=false) {
        vca_focus_release();g_drag=Drag::None;param_lens_cancel();
        u().params[V::P_GAIN].base=26214;u().params[V::P_FLOOR].base=6553;
        u().params[V::P_ATTACK].base=vca_envelope::time_value(240*32);
        u().params[V::P_HOLD].base=vca_envelope::time_value(180*32,true);
        u().params[V::P_DECAY].base=vca_envelope::time_value(780*32);
        u().params[V::P_A_CURVE].base=8192;u().params[V::P_D_CURVE].base=-18022;
        u().params[V::P_DUCK].base=duck?32767:0;
        for(int p=0;p<V::P_COUNT;++p) {u().params[p].src=ModSource::None;u().params[p].mod_depth=0;u().params[p].snap();}
        u().reset();vca_focus_select(V::P_DECAY);capture();
    }
    void advance(int frames) {
        q15 block[128];while(frames>0) {int n=std::min(frames,128);std::fill(std::begin(block),std::end(block),10000);u().process(block,n);frames-=n;}
    }
    std::pair<int,int> point(int p) const {
        static const std::pair<int,int> points[]={{230,113},{70,67},{70,113},{230,67},{70,205},{70,159},{230,159},{280,205}};
        return points[p];
    }
    uint64_t state_hash() const {
        uint64_t hash=ModuleInspectorFixture::hash();
        for(int p=0;p<V::P_COUNT;++p)for(int v:{int(module->params[p].base),int(module->params[p].src),int(module->params[p].mod_depth)})hash=(hash^uint32_t(v))*1099511628211ull;
        return (hash^module->bypass)*1099511628211ull;
    }
    void choose(int p) {auto xy=point(p);CHECK(down(xy.first,xy.second));vca_focus_release();g_drag=Drag::None;CHECK(vca_focus_selected()==p);}
};

void test_vca_flexible_controls() {
    using V=VcaUnit;
    for(int p=0;p<V::P_DUCK;++p)for(int edge:{-32767,0,32767}) {
        VcaFixture f;auto& u=f.u();if(edge<0 && !u.params[p].bipolar)continue;
        u.params[p].base=edge;const auto xy=f.point(p);const auto before=f.state_hash();const int dirty=mutations;
        CHECK(f.down(xy.first,xy.second));CHECK(vca_focus_selected()==p);
        f.drag(xy.first,xy.second);CHECK(f.state_hash()==before&&mutations==dirty);
        f.drag(xy.first+5,xy.second-5);CHECK(u.params[p].base>=(u.params[p].bipolar?-32767:0));
        vca_focus_release();const auto after=f.state_hash();f.drag(0,0);CHECK(f.state_hash()==after);
    }
    VcaFixture f;const auto original=f.state_hash();
    // Empty header/body/footer must not reach invisible old three-parameter rows.
    for(auto xy:{std::pair<int,int>{185,10},{158,35},{158,215},{140,225},{158,155}}) {
        f.down(xy.first,xy.second);f.drag(xy.first+4,xy.second+4);CHECK(f.state_hash()==original);
    }
    CHECK(f.down(280,205));CHECK(f.u().params[V::P_DUCK].base==32767);
    CHECK(f.down(180,205));CHECK(f.u().params[V::P_DUCK].base==0);
    f.seed();const q15 values[]={f.u().params[0].base,f.u().params[1].base,f.u().params[2].base};
    CHECK(f.down(258,10));CHECK(f.u().bypass);CHECK(f.u().params[0].base==values[0]&&f.u().params[1].base==values[1]&&f.u().params[2].base==values[2]);
    // Owner changes, lens entry, slot replacement and lock-step changes cancel.
    for(int reason=0;reason<4;++reason) {
        f.seed();auto xy=f.point(V::P_FLOOR);f.down(xy.first,xy.second);const auto before=f.state_hash();
        if(reason==0)g_edit_slot=1;
        if(reason==1)g_overlay=Overlay::None;
        if(reason==2)f.open();
        if(reason==3) {g_seq_lock_armed=true;g_seq_lock_step=7;}
        f.drag(xy.first,xy.second-10);CHECK(f.state_hash()==before);
        g_edit_slot=0;g_overlay=Overlay::Editor;g_seq_lock_armed=false;param_lens_cancel();
    }
}

void test_vca_flexible_locks_lens() {
    using V=VcaUnit;
    for(int p=0;p<V::P_DUCK;++p) {
        VcaFixture f;const auto xy=f.point(p);const q15 base=f.u().params[p].base;
        g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_step=7;g_seq_lock_chain=0;
        CHECK(f.engine->set_step_lock(0,7,0,p,4000));f.down(xy.first,xy.second);
        f.drag(xy.first+4,xy.second-4);CHECK(f.u().params[p].base==base);
        const int range=(p==V::P_A_CURVE||p==V::P_D_CURVE)?65534:32767;
        CHECK(seq_display_value(0,p,base)==4000+4*range/296);
        const auto before=f.state_hash();g_seq_lock_step=8;f.drag(xy.first+12,xy.second-12);CHECK(f.state_hash()==before);
        g_seq_lock_step=7;
        auto name=xy;name.first=xy.first<160?18:176;name.second-=23;
        f.down(name.first,name.second);CHECK(seq_display_value(0,p,base)==base);
        CHECK(!param_lens_touch_down(250,232));
    }
    VcaFixture full;g_view=View::Sequencer;g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;
    delete full.engine->replace_unit(0,1,new FmUnit);
    for(int n=0;n<SEQ_LOCKS;++n)CHECK(full.engine->set_step_lock(0,7,1,n,5000));
    const auto before=full.state_hash();full.down(70,205);full.drag(90,205);
    CHECK(full.state_hash()==before&&g_seq_lock_flash>0);full.capture();module_inspector_top();CHECK(module_inspector_text("lock full"));
    full.down(280,205);CHECK(full.state_hash()==before);full.down(258,10);CHECK(full.state_hash()==before);
    g_seq_lock_armed=false;g_view=View::Chains;
    for(int p=0;p<V::AHD_COUNT;++p) {
        VcaFixture f;f.choose(p);f.open();
        CHECK(param_lens_touch_down(70,185));param_lens_touch_release();CHECK(f.u().params[p].src==ModSource::SigilA);
        CHECK(param_lens_touch_down(290,47));param_lens_touch_release();const int next=(p+1)%V::AHD_COUNT;
        CHECK(vca_focus_selected()==next);u32 down=KEY_B;CHECK(param_lens_input(down,0,0,false));f.open();CHECK(vca_focus_selected()==next);
        lens_permanent_probe(f.u(),next,false);CHECK(vca_focus_selected()==next);f.open();f.capture();
        module_inspector_top(true);CHECK(!module_inspector_text("temporary"));
        down=KEY_B;param_lens_input(down,0,0,false);CHECK(vca_focus_selected()==next);
    }
}

void test_vca_flexible_units_frames() {
    using V=VcaUnit;
    {
        VcaFixture f;auto& gate=*static_cast<V*>(f.engine->chains[1].units[1]);
        // Existing continuous OSC -> VCA chain: floor/duck must retain Flow.
        for(int mode=0;mode<4;++mode) {
            gate.params[V::P_FLOOR].base=mode==1?12000:mode==3?32767:0;
            gate.params[V::P_DUCK].base=mode==2?32767:0;
            f.capture();CHECK(g_ui_snapshot.sequence_lifecycle[1]==uint8_t(mode==0?SequenceLifecycle::Gate:SequenceLifecycle::Flow));
        }
        gate.set_flexible(false);gate.params[V::P_ATTACK].base=gate.params[V::P_DECAY].base=0;
        f.capture();CHECK(g_ui_snapshot.sequence_lifecycle[1]==uint8_t(SequenceLifecycle::Flow));
    }
    for(int p=0;p<V::P_COUNT;++p)for(int raw:{-32768,0,1,16384,32767}) {
        char text[32];module_inspector_value_text(text,sizeof text,UnitKind::Vca,p,raw);
        CHECK(std::string(text)!="--");
        if(p==V::P_ATTACK||p==V::P_HOLD||p==V::P_DECAY)CHECK(std::strtol(text,nullptr,10)==long(vca_envelope::time_frames(raw,p==V::P_HOLD)/32));
        if(p==V::P_A_CURVE||p==V::P_D_CURVE)CHECK(std::strtol(text,nullptr,10)==std::max(-32767,raw)*100/32767);
        if(p==V::P_DUCK)CHECK(std::string(text)==(raw>=16384?"duck":"open"));
        char tiny[3]={'q','q','z'};module_inspector_value_text(tiny,2,UnitKind::Vca,p,raw);CHECK(tiny[1]==0&&tiny[2]=='z');
    }
    for(bool duck:{false,true})for(int edge=0;edge<4;++edge) {
        VcaFixture f;f.seed(duck);
        for(int p=0;p<V::P_DUCK;++p) {
            if(edge==1)f.u().params[p].base=0;
            if(edge==2)f.u().params[p].base=32767;
            if(edge==3)f.u().params[p].base=(p==V::P_A_CURVE||p==V::P_D_CURVE)?-32767:(p%2?0:32767);
            f.u().params[p].snap();
        }
        f.u().trigger();f.advance(113);
        for(int p=0;p<V::AHD_COUNT;++p) {
            vca_focus_select(p);f.capture();const auto state=f.state_hash();const int dirty=mutations;
            const auto top=module_inspector_top();const int top_count=objects;
            module_inspector_surface(false);{ModuleInspectorNoEngine detached;f.lower();}module_inspector_geometry();
            if(edge==1)CHECK(module_inspector_text("1ms")); // each nonzero free A/D minimum is 1ms
            const auto lower=pixels;const int bottom_count=objects;CHECK(top_count+bottom_count<=8192);
            maximum_shared_objects=std::max(maximum_shared_objects,top_count+bottom_count);
            maximum_objects=std::max(maximum_objects,bottom_count);
            f.u().params[p].effective=1000;f.advance(29);g_ui_snapshot={};
            CHECK(module_inspector_top()==top);module_inspector_surface(false);{ModuleInspectorNoEngine detached;f.lower();}
            CHECK(pixels==lower&&objects==bottom_count&&f.state_hash()==state&&mutations==dirty);
        }
    }
    VcaFixture f;f.u().trigger();f.advance(650*32);f.capture();module_inspector_top();
    CHECK(module_inspector_text("opening "+std::to_string((f.u().visual_envelope()*100u+16383)/32767)+"%"));
    CHECK(module_inspector_text("multiplier "+std::to_string((f.u().visual_gain()*100u+16383)/32767)+"%"));
    f.u().bypass=true;f.capture();module_inspector_top();CHECK(module_inspector_text("bypassed")&&module_inspector_text("opening --"));
    f.u().bypass=false;f.engine->chains[0].mute=true;f.capture();module_inspector_top();CHECK(module_inspector_text("chain muted"));
    g_overlay=Overlay::None;f.capture();module_inspector_surface(true);CHECK(!module_inspector_draw_top()&&objects==0);
    module_inspector_surface(false);f.lower();CHECK(texts.empty());
}

void dump_vca_runtime(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);VcaFixture f;
    std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\ttop_objects\tbottom_objects\topening_q15\tgain_q15\tstage\n";
    auto save=[&](const std::string& name,bool lens=false) {
        f.capture();module_inspector_top(lens);const int top=objects;dump_surface(directory,name+"_top");
        module_inspector_surface(false);if(lens)param_lens_draw_bottom();else f.lower();module_inspector_geometry();
        CHECK(top+objects<=8192);dump_surface(directory,name+"_bottom");
        manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<f.u().visual_envelope()<<'\t'<<f.u().visual_gain()<<'\t'<<int(f.u().visual_stage())<<'\n';
    };
    for(int mode=0;mode<2;++mode) {
        f.seed(mode);f.u().trigger();f.advance(650*32);vca_focus_select(mode?VcaUnit::P_FLOOR:VcaUnit::P_DECAY);save(mode?"03_duck":"01_open");
    }
    f.seed();f.u().trigger();f.advance(330*32);vca_focus_select(VcaUnit::P_HOLD);save("02_hold");
    vca_focus_select(VcaUnit::P_D_CURVE);save("04_curve");
    for(int p=0;p<VcaUnit::AHD_COUNT;++p) {vca_focus_select(p);save("param_"+std::to_string(p));}
    f.open();f.u().params[VcaUnit::P_DUCK].src=ModSource::SigilB;f.u().params[VcaUnit::P_DUCK].mod_depth=-10000;save("mod",true);
    lens_quick_begin();param_lens_touch_down(295,129);save("temp",true);lens_frame(0,0,KEY_L);param_lens_touch_release();param_lens_cancel();
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;vca_focus_select(VcaUnit::P_D_CURVE);
    CHECK(f.engine->set_step_lock(0,7,0,VcaUnit::P_D_CURVE,-28000));save("lock");g_seq_lock_armed=false;
    f.u().set_flexible(false);vca_focus_select(VcaUnit::P_DECAY);save("legacy");f.u().set_flexible(true);
    for(int mode=0;mode<2;++mode) {
        f.seed(mode);vca_focus_select(VcaUnit::P_DECAY);f.u().trigger();
        for(int n=0;n<20;++n) {f.advance(3200);char name[32];std::snprintf(name,sizeof name,"motion_%02d",mode*20+n);save(name);}
    }
}
