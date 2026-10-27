// Temporary master pad ownership in the actual routed UI; no synthetic DSP.
void test_transition_fx_ui() {
    TransitionFixture f;f.open_page();f.rail(20,160);
    const auto rhythm=f.engine->pattern_transition();const int dirty=mutations;
    const auto pattern=f.hash();const auto frame=f.engine->transport_frame;
    const int chain=g_sel_chain;
    auto state=[&](){return f.engine->transition_fx();};
    auto draw_frozen=[&]() {
        f.refresh();performance_view_tick();const auto top=performance_surface(true);const int topcount=objects;
        const auto bottom=performance_surface(false);CHECK(topcount+objects<8192);
        maximum_shared_objects=std::max(maximum_shared_objects,topcount+objects);
        auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
        CHECK(performance_surface(true)==top&&performance_surface(false)==bottom);
        g_eng=engine;g_audio=audio;
    };
    // Relative pickup: arbitrary touch-down x is neutral; no absolute-pad jump.
    for(int start:{16,56,95}) {
        performance_view_touch_down(start,185);CHECK(state().kind==TransitionFxKind::Filter&&state().amount==0);
        performance_view_touch_hold(start,185);CHECK(state().amount==0);
        performance_view_touch_drag(start-40,185);CHECK(state().amount==-32767);draw_frozen();
        performance_view_touch_drag(start+40,185);CHECK(state().amount==32767);draw_frozen();
        performance_view_touch_drag(start,185);CHECK(state().amount==0);f.release();CHECK(state().kind==TransitionFxKind::None);
    }
    performance_view_touch_down(160,185);CHECK(state().kind==TransitionFxKind::Echo&&state().amount==16384);
    performance_view_touch_drag(200,185);CHECK(state().amount==32767);draw_frozen();
    // Crossing another pad never transfers single-touch ownership.
    performance_view_touch_drag(280,185);CHECK(state().kind==TransitionFxKind::Echo);
    performance_view_touch_drag(120,185);CHECK(state().amount==0);
    performance_view_touch_drag(80,185);CHECK(state().amount==0);f.release(false);CHECK(state().kind==TransitionFxKind::None);
    performance_view_touch_down(200,185);CHECK(state().amount==0);performance_view_touch_drag(240,185);CHECK(state().amount==16384);f.release();
    performance_view_touch_down(264,185);CHECK(state().kind==TransitionFxKind::Roll&&state().roll_division==2);
    draw_frozen(); // cold-start wait is real requested state, not fabricated audio
    for(int x:{281,264}) {performance_view_touch_drag(x,185);CHECK(state().roll_division==2);}
    for(int x:{282,281,283,280}) {performance_view_touch_drag(x,185);CHECK(state().roll_division==3);}
    performance_view_touch_drag(270,185);CHECK(state().roll_division==2);
    performance_view_touch_drag(-100,185);CHECK(state().roll_division==0);
    performance_view_touch_drag(-82,185);CHECK(state().roll_division==1);draw_frozen();
    performance_view_touch_drag(300,147);CHECK(state().kind==TransitionFxKind::None);
    performance_view_touch_drag(264,185);CHECK(state().kind==TransitionFxKind::None);f.release();
    CHECK(f.engine->pattern_transition().position==rhythm.position&&f.engine->pattern_transition().active);
    CHECK(f.engine->transport_frame==frame&&mutations==dirty&&f.hash()==pattern&&g_sel_chain==chain);
    for(int c=0;c<4;++c)CHECK(f.engine->chains[c].volume==32767&&!f.engine->chains[c].mute);
    // Held modifiers, modal entry, mixer/navigation and lifecycle invalidate a capture.
    for(u32 key:{KEY_L,KEY_R,KEY_B,KEY_SELECT,KEY_START}) {
        // Main polls a neutral HID frame after L key-up; direct fixture calls
        // must model that frame before accepting a fresh, unrelated gesture.
        performance_view_buttons(0,0,0);
        performance_view_touch_down(160,185);CHECK(state().kind==TransitionFxKind::Echo);
        performance_view_buttons(key,key,0);CHECK(state().kind==TransitionFxKind::None);
        performance_view_touch_drag(200,185);CHECK(state().kind==TransitionFxKind::None);
        performance_view_touch_down(160,185);CHECK(state().kind==TransitionFxKind::None);
        f.release();performance_view_buttons(0,0,key);
    }
    performance_view_touch_down(56,185);g_overlay=Overlay::PatchBrowser;performance_view_tick();
    CHECK(state().kind==TransitionFxKind::None);g_overlay=Overlay::None;
    performance_view_touch_drag(200,185);CHECK(state().kind==TransitionFxKind::None);f.release();
    performance_view_touch_down(160,185);performance_view_leave();CHECK(state().kind==TransitionFxKind::None);
    f.open_page();CHECK(f.engine->pattern_transition().position==rhythm.position);
    performance_view_touch_down(160,185);f.engine->transport_stop();
    performance_view_touch_drag(200,185);CHECK(state().kind==TransitionFxKind::None);f.release();
    performance_view_touch_down(160,185);f.engine->transport_reset();performance_view_tick();
    CHECK(state().kind==TransitionFxKind::None);performance_view_touch_drag(200,185);CHECK(state().kind==TransitionFxKind::None);f.release();
    performance_view_touch_down(160,185);auto fresh=std::make_unique<Engine>();f.engine->swap_patch_state(*fresh);
    performance_view_touch_drag(200,185);CHECK(state().kind==TransitionFxKind::None);f.release();
    f.rail(20,160);const auto kept=f.engine->pattern_transition();f.tap_at(40,229);
    CHECK(state().kind==TransitionFxKind::None&&f.engine->pattern_transition().position==kept.position);draw_frozen();
    performance_view_touch_down(56,185);CHECK(state().kind==TransitionFxKind::Filter);
    f.tap_at(160,10);CHECK(!transition::active()&&state().kind==TransitionFxKind::None);
    CHECK(f.engine->pattern_transition().position==kept.position);
    CHECK(mutations==dirty);
}

void dump_transition_fx(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\ttop_objects\tbottom_objects\tshared_objects\n";
    TransitionFixture f;delete f.engine->replace_unit(0,0,new MixerConstant);
    f.save_scene(0);f.engine->chains[1].units[0]->params[0].base=21000;f.save_scene(1);f.tap_at(60,211);
    f.open_page();f.swipe(1,100,76);f.tap_at(208,43);f.rail(20,160);
    auto audio=[&](int frames){q15 b[512];while(frames){int n=std::min(frames,256);f.engine->render(b,n);frames-=n;}};
    auto save=[&](const char* name) {
        f.refresh();performance_view_tick();performance_surface(true);const int top=objects;
        dump_surface(directory,std::string(name)+"_top");performance_surface(false);CHECK(top+objects<8192);
        dump_surface(directory,std::string(name)+"_bottom");manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\n';
    };
    audio(16000);
    performance_view_touch_down(56,185);performance_view_touch_drag(28,185);audio(512);save("01_filter_held");f.release();audio(512);
    performance_view_touch_down(160,185);performance_view_touch_drag(168,185);audio(18000);save("02_echo_held");f.release();audio(512);
    performance_view_touch_down(264,185);audio(512);CHECK(f.engine->transition_fx().ready);save("03_roll_held");f.release();audio(512);
    CHECK(f.engine->transition_fx().kind==TransitionFxKind::None);save("04_fx_released");
}
