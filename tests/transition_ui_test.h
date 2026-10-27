// Actual routed performance/controller tests in the existing native raster harness.
struct TransitionFixture : PerformanceFixture {
    void open_page(){tap_at(260,10);CHECK(transition::active());performance_view_tick();}
    void rail(int from,int to){performance_view_touch_down(from,134);performance_view_touch_drag(to,134);release();performance_view_tick();}
    void swipe(int side,int from,int to,bool commit=true){const int x=side?276:36;performance_view_touch_down(x,from);performance_view_touch_hold(x,to);performance_view_touch_drag(x,to);release(commit);performance_view_tick();}
};
void test_transition_swipes() {
    TransitionFixture f;const int dirty=mutations;const auto old=f.hash();
    std::array<uint64_t,8> banks{};for(int i=0;i<8;++i)banks[i]=pattern_hash(f.engine->sequence_pattern(i));
    f.open_page();f.tap_at(276,80);f.swipe(1,100,83); // tap + 17px remain bank2
    CHECK(!f.engine->pattern_transition().active&&f.engine->active_pattern==0);
    performance_view_touch_down(276,100);
    for(int y:{82,81,83,90,93})performance_view_touch_drag(276,y); // step up, jitter does not reverse
    f.release();f.rail(20,21);CHECK(f.engine->pattern_transition().destination==2);
    f.rail(21,20);CHECK(f.engine->pattern_transition().position==0);
    // Release confirms; queue/clock/bank storage are not launched by selection.
    f.swipe(1,100,240);f.swipe(1,100,-200);f.rail(20,21);
    CHECK(f.engine->pattern_transition().destination==3); // saturation, not wrapping
    CHECK(f.engine->active_pattern==0&&f.engine->pending_pattern==255);
    f.rail(21,20);f.swipe(1,100,124);performance_view_touch_down(276,100);performance_view_touch_drag(276,82);
    performance_view_touch_drag(276,94);f.release();f.rail(20,21);
    CHECK(f.engine->pattern_transition().destination==2); // from clamped4, reverse hysteresis to3
    // Navigation and horizontal escape discard staged selection, including real lift later.
    f.rail(21,20);f.swipe(1,100,124,false);f.rail(20,21);CHECK(f.engine->pattern_transition().destination==2);
    f.rail(21,20);performance_view_touch_down(276,100);performance_view_touch_drag(276,76);performance_view_touch_drag(180,76);f.release();
    f.rail(20,21);CHECK(f.engine->pattern_transition().destination==2);
    f.rail(21,20);performance_view_touch_down(276,100);performance_view_touch_drag(276,124);performance_view_leave();f.release();f.open_page();
    f.rail(20,21);CHECK(f.engine->pattern_transition().destination==2);
    // Both currently contribute: neither endpoint can be replaced.
    f.rail(21,161);const auto state=f.engine->pattern_transition();
    f.swipe(0,100,76);f.swipe(1,100,124);
    CHECK(f.engine->pattern_transition().source==state.source&&f.engine->pattern_transition().destination==state.destination);
    CHECK(f.engine->pattern_transition().position==state.position);
    CHECK(f.hash()==old&&mutations==dirty);
    for(int i=0;i<8;++i)CHECK(banks[i]==pattern_hash(f.engine->sequence_pattern(i)));
    // Before the first rail gesture, either endpoint can be selected with no launch.
    f.engine->transport_reset();performance_view_tick();f.swipe(0,100,76);f.swipe(1,100,52);
    CHECK(!f.engine->pattern_transition().active);f.rail(20,21);
    CHECK(f.engine->pattern_transition().source==1&&f.engine->pattern_transition().destination==3);
}
void test_transition_modes_lifecycle() {
    TransitionFixture f;f.engine->chains[0].volume=9000;f.engine->chains[1].volume=15000;
    f.save_scene(0);f.engine->chains[0].units[0]->params[0].base=21000;f.save_scene(1);f.tap_at(60,211);
    f.open_page();f.swipe(1,100,76);f.tap_at(208,43);f.rail(20,160);
    const auto first=f.engine->pattern_transition();CHECK(first.active&&first.destination==2&&first.position==16384);
    for(int c=0;c<4;++c)CHECK(f.engine->scene_morph[c]==first.position);
    CHECK(f.engine->chains[0].volume==9000&&f.engine->chains[1].volume==15000);
    f.tap_at(152,43);f.rail(160,200); // sound only holds rhythm
    CHECK(f.engine->pattern_transition().position==first.position&&f.engine->scene_morph[0]>first.position);
    const int sound=f.engine->scene_morph[0];f.tap_at(208,43); // misaligned both refuses, no snap
    CHECK(f.engine->scene_morph[0]==sound&&f.engine->pattern_transition().position==first.position);
    performance_surface(false);CHECK(has_text("match rhythm and a/b positions"));
    f.tap_at(100,43);f.rail(160,180);CHECK(f.engine->scene_morph[0]==sound&&f.engine->pattern_transition().position>first.position);
    // Held controls and page exits cannot continue a rail or trigger/mute chains.
    const auto held=f.engine->pattern_transition();const int chain=g_sel_chain;
    for(u32 keys:{KEY_A,KEY_X,KEY_Y,KEY_DUP,KEY_DDOWN,KEY_DLEFT,KEY_DRIGHT})performance_view_buttons(keys,keys,0);
    CHECK(g_sel_chain==chain&&!f.engine->chains[chain].mute&&f.engine->pattern_transition().position==held.position);
    for(u32 keys:{KEY_L,KEY_R,KEY_SELECT,KEY_B,KEY_START}) {
        performance_view_touch_down(100,134);performance_view_buttons(keys,keys,0);performance_view_touch_drag(300,134);f.release();
        if(keys==KEY_L)CHECK(f.engine->pattern_transition().position!=held.position);
        else CHECK(f.engine->pattern_transition().position==held.position);
        performance_view_buttons(0,0,keys);CHECK(f.engine->pattern_transition().position==held.position);
    }
    performance_view_touch_down(100,134);g_overlay=Overlay::PatchBrowser;performance_view_touch_hold(300,134);g_overlay=Overlay::None;f.release();
    CHECK(f.engine->pattern_transition().position==held.position);
    performance_view_leave();performance_view_reset();CHECK(f.engine->pattern_transition().active);f.open_page();
    f.rail(20,320);CHECK(f.engine->pattern_transition().position==32767);f.tap_at(280,229);
    CHECK(!f.engine->pattern_transition().active&&f.engine->active_pattern==2&&f.engine->scene_morph[0]==sound);
    CHECK(f.engine->chains[0].volume==9000&&f.engine->chains[1].volume==15000);
    f.rail(20,60);CHECK(f.engine->pattern_transition().active);f.engine->transport_reset();performance_view_tick();
    performance_surface(false);CHECK(has_text("0%")&&!f.engine->pattern_transition().active);
    // Missing/mismatched scenes refuse both entirely, not a hidden rhythm fallback.
    f.engine->scenes[0][1].valid=false;f.tap_at(208,43);performance_surface(false);CHECK(has_text("capture matching a/b in mixer"));
}
void test_transition_polish() {
    TransitionFixture f;
    auto header=[]() {
        for(const auto& expected:std::array<std::pair<const char*,int>,3>{{{"order",10},{"perform",130},{"banks",274}}}) {
            bool found=false;
            for(const auto& text:texts)found|=text.text==expected.first&&text.call_x==expected.second&&text.call_y==8;
            CHECK(found);
        }
    };
    f.open_page();performance_surface(false);header();
    CHECK(has_text("a/b: capture in mixer")&&has_text("launch below")&&has_text("perform"));
    f.tap_at(260,10);CHECK(transition::active()); // current tab remains selected
    f.tap_at(160,10);CHECK(!transition::active());performance_surface(false);header();
    f.save_scene(0);f.save_scene(1);
    f.engine->scene_morph[1]=1000;
    const int dirty=mutations;
    f.open_page();performance_surface(false);CHECK(has_text("a/b: align in mixer"));
    f.tap_at(152,43);performance_surface(false);
    CHECK(has_text("align chain a/b in mixer")&&!has_text("capture matching a/b in mixer"));
    CHECK(f.engine->scene_morph[0]==32767&&f.engine->scene_morph[1]==1000&&mutations==dirty);
    f.tap_at(160,10);f.tap_at(60,211); // explicitly align all four scene positions in mixer
    f.open_page();performance_surface(false);CHECK(has_text("a/b scenes ready"));
    f.tap_at(152,43);performance_surface(false);CHECK(has_text("a/b scenes / rhythm held"));
    f.tap_at(100,43);
    const int before=mutations;
    for(const auto& example:std::array<std::pair<int,int>,5>{{{0,0},{1,1},{16384,50},{32766,99},{32767,100}}}) {
        const int position=example.first,percent=example.second;
        CHECK(f.engine->set_pattern_transition(0,1,(q15)position));
        f.tap_at(160,10);CHECK(!transition::active());
        for(auto& morph:f.engine->scene_morph)morph=(q15)position;
        f.refresh();performance_view_tick();performance_surface(true);
        CHECK(has_text("bank 1>2 / "+std::to_string(percent)+"%"));
        performance_surface(false);header();CHECK(has_text("morph "+std::to_string(percent)+"%"));
        f.open_page();f.refresh();performance_view_tick();
        const auto top=performance_surface(true);
        CHECK(has_text("bank 1>2 / "+std::to_string(percent)+"% / blend"));
        const auto bottom=performance_surface(false);header();
        CHECK(has_text(std::to_string(percent)+"%"));
        const bool edge=position==0||position==32767;
        CHECK(has_text(edge?"at bank edge":"rail to edge"));
        // The existing keep target is dim away from an actual eligible edge.
        for(int y=0;y<8;++y)for(int x=0;x<6;++x)
            if(trackr::ui::glyph_row('k',y)&(1u<<(5-x)))
                CHECK(bottom[(229+y)*320+258+x]==(edge?m::spot():m::ghost()));
        auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
        CHECK(performance_surface(true)==top&&performance_surface(false)==bottom);g_eng=engine;g_audio=audio;
        if(!edge) {
            f.tap_at(280,229);performance_surface(false);CHECK(has_text("move rhythm rail to either end"));
            CHECK(f.engine->pattern_transition().active&&f.engine->pattern_transition().position==position);
        }
    }
    CHECK(mutations==before);
    f.tap_at(280,229);CHECK(!f.engine->pattern_transition().active&&f.engine->active_pattern==1);
    // Same-bank transitions are already a valid endpoint even at an interior value.
    CHECK(f.engine->set_pattern_transition(1,1,16384));f.tap_at(160,10);f.open_page();
    performance_surface(false);CHECK(has_text("at bank edge"));
    f.tap_at(280,229);CHECK(!f.engine->pattern_transition().active&&f.engine->active_pattern==1);
}
void test_transition_frames() {
    test_transition_polish();
    TransitionFixture f;f.open_page();const int dirty=mutations;
    for(int bank=0;bank<8;++bank)for(int c=0;c<4;++c) {
        f.engine->sequence_pattern(bank).length[c]=(uint8_t)(1+(bank*7+c*13)%64);
        f.engine->sequence_pattern(bank).direction[c]=(SequenceDirection)((bank+c)%3);
    }
    for(int side=0;side<2;++side)for(int dy:{0,-18,-42,-66,30,66,120}) {
        performance_view_touch_down(side?276:36,90);performance_view_touch_drag(side?276:36,90+dy);
        f.refresh();performance_view_tick();const auto top=performance_surface(true);const int topcount=objects;
        const auto bottom=performance_surface(false);CHECK(topcount+objects<8192);
        maximum_shared_objects=std::max(maximum_shared_objects,topcount+objects);
        auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
        CHECK(performance_surface(true)==top&&performance_surface(false)==bottom);g_eng=engine;g_audio=audio;
        f.release(false);
    }
    CHECK(mutations==dirty);
    // Existing bank8 can be shown without clamping/launching/truncating it.
    f.engine->request_pattern(7,false);performance_view_tick();performance_surface(false);f.rail(20,300);
    CHECK(f.engine->pattern_transition().source==7);f.refresh();performance_view_tick();performance_surface(true);performance_surface(false);
}
void dump_transition(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\ttop_objects\tbottom_objects\tshared_objects\n";
    TransitionFixture f;f.save_scene(0);f.engine->chains[0].units[0]->params[0].base=21000;f.save_scene(1);f.tap_at(60,211);
    for(int c=0;c<4;++c){f.engine->sequence_pattern(2).length[c]=(uint8_t)(64-c*16);f.engine->sequence_pattern(2).direction[c]=(SequenceDirection)(c%3);}
    f.open_page();auto save=[&](const char* name){f.refresh();performance_view_tick();performance_surface(true);const int top=objects;dump_surface(directory,std::string(name)+"_top");performance_surface(false);CHECK(top+objects<8192);dump_surface(directory,std::string(name)+"_bottom");manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\n';};
    performance_view_touch_down(276,90);performance_view_touch_drag(276,66);save("01_staged_bank");f.release();
    f.tap_at(208,43);f.rail(20,160);save("02_both_half");
    f.rail(160,300);CHECK(f.engine->pattern_transition().position==32767);save("03_target_endpoint");
    f.tap_at(280,229);CHECK(!f.engine->pattern_transition().active&&f.engine->active_pattern==2);
    f.tap_at(160,10);save("04_mixer_bank_kept");
}
