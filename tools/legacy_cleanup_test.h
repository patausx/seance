// Included only by the real-editor integration harness, after its existing fixtures.
namespace {
void cleanup_geometry() {
    CHECK(objects>0 && objects<=8192 && outside_rects==0 && outside_lines==0 && text_overlaps==0);
    for(const auto& t:texts)CHECK(t.x>=0 && t.y>=0 && t.x+t.w<=width && t.y+t.h<=240);
}
std::vector<u32> cleanup_draw(bool top,bool editor=true) {
    module_inspector_surface(top);
    // editor_draw still needs the UI-owned topology/kind lookup. Audio access is forbidden.
    auto* audio=g_audio;g_audio=nullptr;
    if(top)top_draw();else if(editor)editor_draw();else sequencer_view_draw();
    g_audio=audio;cleanup_geometry();return pixels;
}
void cleanup_pair_budget(int top) {
    CHECK(top+objects<=8192);maximum_shared_objects=std::max(maximum_shared_objects,top+objects);
}
void cleanup_capture(Fixture& f) {
    f.engine->capture_ui_snapshot(g_ui_snapshot,g_edit_chain,g_edit_slot);
    param_lens_capture();fm_context_capture(fm_focus_selected());stranular_focus_capture();play_focus_capture();
}
void test_cleanup_composed_footers() {
    int pairs=0;
    for(UnitKind kind:{UnitKind::Motor,UnitKind::MicIn}) {
        ModuleInspectorFixture f(kind);
        for(int state=0;state<4;++state) {
            f.module->bypass=state==1;f.engine->chains[0].mute=state==2;g_mic_ok=state!=3;
            cleanup_capture(f);const int dirty=mutations;
            const auto top=cleanup_draw(true);const int top_count=objects;
            module_inspector_surface(false);
            if(kind==UnitKind::Motor)motor_focus_draw(*static_cast<MotorUnit*>(f.module));
            else mic_focus_draw(*static_cast<MicUnit*>(f.module));
            const auto direct=pixels;const int direct_objects=objects;
            CHECK(cleanup_draw(false)==direct && objects==direct_objects);cleanup_pair_budget(top_count);
            CHECK(pixels[239*320]==m::paper_color(239));
            // Frozen body/footer still match without recapture, even as base state changes.
            const q15 old=f.module->params[0].base;f.module->params[0].base=1234;
            CHECK(cleanup_draw(false)==direct && objects==direct_objects);
            CHECK(cleanup_draw(true)==top && mutations==dirty);f.module->params[0].base=old;
            ++pairs;
        }
        g_mic_ok=false;
    }
    std::printf("cleanup: %d composed MOTOR/MIC pairs equal dedicated draw pixels AND object counts\n",pairs);
}
void test_cleanup_footer_entry() {
    for(UnitKind kind:{UnitKind::Motor,UnitKind::MicIn}) {
        ModuleInspectorFixture f(kind);cleanup_capture(f);
        input::down(250,232);input::up();CHECK(param_lens_is_open());cleanup_capture(f);
        cleanup_draw(true);const int top=objects;cleanup_draw(false);cleanup_pair_budget(top);
        input::frame(KEY_B,KEY_B);CHECK(!param_lens_is_open() && g_overlay==Overlay::Editor);input::frame(0,0,KEY_B);
        input::frame(KEY_L,KEY_L);CHECK(param_lens_is_open());cleanup_capture(f);
        cleanup_draw(true);const int held_top=objects;cleanup_draw(false);cleanup_pair_budget(held_top);
        input::frame(0,0,KEY_L);CHECK(!param_lens_is_open());
    }
}
void test_cleanup_strn_modulation() {
    int pairs=0;
    for(bool held:{false,true}) {
        StrnFixture f;
        for(int selected=0;selected<f.strn->num_params;++selected) {
            param_lens_cancel();input::frame(0,0);f.select(selected);
            auto& p=f.strn->params[selected];
            for(int raw:{p.bipolar?-32767:0,16384,32767}) {
                param_lens_cancel();p.base=q15(raw);p.src=selected%2?ModSource::EnvFollow:ModSource::SigilA;
                p.mod_depth=-12000;p.snap();cleanup_capture(f);const int dirty=mutations;
                if(held)input::frame(KEY_L,KEY_L);else {input::down(250,232);input::up();}
                CHECK(param_lens_is_open());cleanup_capture(f);
                const auto top=cleanup_draw(true);const int top_count=objects;
                char expected[24];std::snprintf(expected,sizeof expected,p.bipolar?"base %+d%%":"base %d%%",raw*100/32767);
                CHECK(has_text(expected));CHECK(pixels[400*235]==m::paper_color(235));
                const auto lower=cleanup_draw(false);cleanup_pair_budget(top_count);CHECK(has_text(expected));
                CHECK(!module_inspector_text("%x"));
                const q15 base=p.base;p.base=1234;g_ui_snapshot={};
                // Both real top dispatch and composed lens draw use captured scalars only.
                {ModuleInspectorNoEngine frozen;CHECK(cleanup_draw(true)==top);CHECK(cleanup_draw(false)==lower);}
                p.base=base;CHECK(mutations==dirty);
                if(held)input::frame(0,0,KEY_L);else input::frame(KEY_B,KEY_B);
                CHECK(!param_lens_is_open() && g_overlay==Overlay::Editor);input::frame(0,0);
                ++pairs;
            }
        }
    }
    std::printf("cleanup: %d STRN selected/endpoint/held-or-latched frozen duals; no ratio suffix\n",pairs);
}
void cleanup_no_old_top_palette() {
    // The global default now shares semantic role colors with the clean upper;
    // rejecting old accessor VALUES would incorrectly reject the new ghost ink.
    // Keep a strict positive palette check rather than relaxing raster coverage.
    for(u32 color:pixels)CHECK(color==m::paper_color(0)||color==m::ink()||color==m::spot()||color==m::ghost()||color==m::branch());
}
void test_cleanup_sequencer_top() {
    Fixture f;g_overlay=Overlay::None;f.dense();
    CHECK(f.engine->set_step_lock(0,0,0,FmUnit::P_INDEX,15000));
    CHECK(f.engine->set_step_lock(0,0,0,FmUnit::P_PITCH_ENV,-10000));
    int pairs=0;
    for(int mode=0;mode<8;++mode)for(bool help:{false,true}) {
        f.mode(mode);f.frame(help?u32(KEY_L):0u,help?u32(KEY_L):0u);
        sequencer_capture_inspector();const int dirty=mutations;const auto hash=f.hash();
        const auto top=cleanup_draw(true,false);const int count=objects;cleanup_no_old_top_palette();
        if(mode!=7 && help)CHECK(has_text("l / hold shortcuts"));
        if(mode==6 && !help) {CHECK(has_text("step locks"));CHECK(module_inspector_text("raw "));CHECK(pixels[67*400+385]==m::paper_color(67));}
        const auto lower=cleanup_draw(false,false);cleanup_pair_budget(count);
        {ModuleInspectorNoEngine frozen;CHECK(cleanup_draw(true,false)==top);CHECK(cleanup_draw(false,false)==lower);}
        CHECK(f.hash()==hash && mutations==dirty);++pairs;
    }
    f.mode(6);f.frame(0,0);f.tap(KEY_L|KEY_Y); // walk written parameters including extended second page
    for(int i=0;i<4;++i){f.tap(KEY_L|KEY_Y);sequencer_capture_inspector();cleanup_draw(true,false);cleanup_no_old_top_palette();}
    // Empty chain still has a clean upper context without invented targets.
    for(int s=0;s<UNITS_PER_CHAIN;++s)delete f.engine->replace_unit(0,s,nullptr);
    f.refresh();sequencer_capture_inspector();cleanup_draw(true,false);CHECK(has_text("no module parameters"));
    std::printf("cleanup: %d mode/help frozen sequencer pairs + written-lock pages and no-target\n",pairs);
}
void cleanup_dump_pair(const std::filesystem::path& dir,std::ofstream& manifest,const char* name,bool editor=true) {
    cleanup_draw(true,editor);const int top=objects;dump_surface(dir,std::string(name)+"_top");
    cleanup_draw(false,editor);cleanup_pair_budget(top);dump_surface(dir,std::string(name)+"_bottom");
    manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\t'<<outside_rects+outside_lines<<'\t'<<text_overlaps<<'\n';
}
void dump_cleanup(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);std::ofstream manifest(dir/"manifest.tsv");
    manifest<<"frame\ttop\tbottom\tshared\tclipped\toverlaps\n";
    {MotorFixture f;cleanup_capture(f);cleanup_dump_pair(dir,manifest,"01_motor_composed");}
    {MicFixture f;f.feed();cleanup_capture(f);cleanup_dump_pair(dir,manifest,"02_mic_composed");}
    {StrnFixture f;f.select(StranularUnit::P_PITCH);auto& p=f.strn->params[StranularUnit::P_PITCH];p.base=-12000;p.src=ModSource::SigilA;p.mod_depth=16000;p.snap();input::frame(KEY_L,KEY_L);cleanup_capture(f);cleanup_dump_pair(dir,manifest,"03_strn_held_mod");input::frame(0,0,KEY_L);}
    {Fixture f;g_overlay=Overlay::None;f.dense();f.mode(6);sequencer_capture_inspector();cleanup_dump_pair(dir,manifest,"04_sequence_locks",false);
     f.frame(KEY_L,KEY_L);sequencer_capture_inspector();cleanup_dump_pair(dir,manifest,"05_sequence_help",false);}
}
int run_cleanup(int argc,char** argv) {
    int failed=0,total=0;
    auto run=[&](const char* name,const std::function<void()>& test){++total;try{test();std::printf("ok: %s\n",name);}catch(const std::exception& e){++failed;std::fprintf(stderr,"FAIL: %s: %s\n",name,e.what());}};
    if(argc==3 && std::string(argv[1])=="--dump-legacy-cleanup")run("five actual composed dual captures",[&]{dump_cleanup(argv[2]);});
    else {
        run("composed editor / complete footer ownership",test_cleanup_composed_footers);
        run("composed footer / latch and held-L entry",test_cleanup_footer_entry);
        run("STRN upper/lower selected modulation",test_cleanup_strn_modulation);
        run("sequencer upper / palette, selection, help and frozen frames",test_cleanup_sequencer_top);
        run("existing inline lock browser / single removal / frozen DTO",test_lock_browser_and_snapshot);
        run("existing level target / upper worst density",test_level_targets_and_inspector_density);
        run("existing STRN modulation gesture law",test_strn_lens_routing);
    }
    std::printf("legacy cleanup: %d tests, %d failed; shared %d / 8192 objects\n",total,failed,maximum_shared_objects);return failed?1:0;
}
} // namespace
