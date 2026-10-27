// Included after module_inspector_test.h in the real C++ UI harness.
void test_module_inspector_chorus_controls() {
    ModuleInspectorFixture f(UnitKind::Chorus);auto& u=*static_cast<ChorusUnit*>(f.module);
    struct Box {int x,y,w,h,p;};
    const Box boxes[]={{10,28,231,31,0},{8,170,79,47,1},{278,96,42,73,2},{246,170,70,47,2}};
    for(int value:{0,32767})for(const auto& b:boxes)for(int corner=0;corner<4;++corner) {
        u.params[b.p].base=value;
        CHECK(f.down(b.x+(corner&1?b.w-1:0),b.y+(corner&2?b.h-1:0)));
        CHECK(chorus_focus_selected()==b.p&&u.params[b.p].base==value);
        chorus_focus_release();g_drag=Drag::None;
    }
    // Model crests/paths are passive, and do not expose the old hidden sliders.
    const auto unchanged=f.hash();
    for(auto xy:{std::pair<int,int>{92,73},{121,107},{151,97},{102,151},{200,120},{260,83}}) {
        f.down(xy.first,xy.second);f.drag(xy.first+30,xy.second+30);CHECK(f.hash()==unchanged);
    }
    // p-lock edits start from the stored lock, not from base; label tap removes.
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;
    CHECK(f.engine->set_step_lock(0,7,0,1,4000));const auto base=u.params[1].base;
    CHECK(f.down(68,192));f.drag(68,197);
    CHECK(seq_display_value(0,1,base)==4000+5*32767/34&&u.params[1].base==base);
    const auto lock=f.hash();g_seq_lock_step=8;f.drag(68,205);CHECK(f.hash()==lock);
    g_seq_lock_step=7;CHECK(f.down(20,178));CHECK(seq_display_value(0,1,base)==base);
    const bool bypass=u.bypass;f.down(256,10);CHECK(u.bypass==bypass);
    g_seq_lock_armed=false;g_view=View::Chains;f.down(256,10);CHECK(u.bypass!=bypass);
    // Context/owner change must never continue a gesture into another slot.
    f.down(169,43);const auto before=f.hash();g_edit_slot=1;f.drag(200,43);CHECK(f.hash()==before);
    g_edit_slot=0;chorus_focus_release();
}

void test_module_inspector_chorus_frame() {
    ModuleInspectorFixture f(UnitKind::Chorus);auto& u=*static_cast<ChorusUnit*>(f.module);
    module_inspector_surface(false);f.lower();module_inspector_geometry();CHECK(module_inspector_text("await audio"));
    q15 block[128]={};
    for(int n=0;n<40;++n)u.process(block,128);
    for(int mix:{0,17000,32767}) {
        u.params[2].base=u.params[2].effective=mix;u.process(block,128);f.capture();
        module_inspector_top();const int top=objects;
        module_inspector_surface(false);f.lower();module_inspector_geometry();
        const auto frozen=pixels;const int count=objects;const auto hash=f.hash();
        CHECK(module_inspector_text("delay model"));
        CHECK(module_inspector_text(mix==0?"dry only":mix==32767?"wet copies":"moving copies"));
        for(int voice=0;voice<2;++voice) {
            const unsigned cent=(uint64_t(u.visual_delay_q16(voice))*100000+(uint64_t(ENGINE_SR)<<15))/(uint64_t(ENGINE_SR)<<16);
            char expected[24];std::snprintf(expected,sizeof expected,"%u.%02ums",cent/100,cent%100);
            CHECK(module_inspector_text(expected));
        }
        // Lower renderer may only use its frame. Change live state WITHOUT capture.
        const q15 saved=u.params[1].base;u.params[1].base=0;u.process(block,128);g_ui_snapshot={};
        module_inspector_surface(false);{ModuleInspectorNoEngine detached;f.lower();}
        CHECK(pixels==frozen&&objects==count);u.params[1].base=saved;
        CHECK(f.hash()==hash);CHECK(top+count<=8192);
        maximum_shared_objects=std::max(maximum_shared_objects,top+count);
        maximum_objects=std::max(maximum_objects,count);
    }
    for(int state=0;state<3;++state) {
        u.bypass=state==0;f.engine->chains[0].mute=state==1;if(state==2)u.reset();
        f.capture();module_inspector_surface(false);f.lower();module_inspector_geometry();
        CHECK(module_inspector_text(state==0?"bypassed":state==1?"chain muted":"await audio"));
    }
    g_overlay=Overlay::None;f.capture();module_inspector_surface(false);f.lower();
    CHECK(texts.empty());
}

void dump_chorus_motion(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    ModuleInspectorFixture f(UnitKind::Chorus);auto& u=*static_cast<ChorusUnit*>(f.module);
    u.params[0].base=3105;u.params[1].base=17000;u.params[2].base=16711;
    for(int p=0;p<3;++p)u.params[p].snap();
    q15 silence[128]={};
    std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\ttop_objects\tbottom_objects\n";
    for(int n=0;n<40;++n) {
        int remaining=1600;
        while(remaining){const int frames=std::min(remaining,128);std::fill(std::begin(silence),std::end(silence),0);u.process(silence,frames);remaining-=frames;}
        f.capture();char name[32];std::snprintf(name,sizeof name,"chorus_%02d",n);
        module_inspector_top();const int top=objects;dump_surface(directory,std::string(name)+"_top");
        module_inspector_surface(false);f.lower();module_inspector_geometry();
        CHECK(top+objects<=8192);dump_surface(directory,std::string(name)+"_bottom");
        manifest<<name<<'\t'<<top<<'\t'<<objects<<'\n';
    }
}
