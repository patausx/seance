// Included inside the real-controller harness after Fixture. No duplicate UI logic.
void open_lens(Fixture& f, int c = 0, int s = 0) {
    param_lens_cancel();
    g_view = View::Chains; g_overlay = Overlay::Editor; g_seq_lock_armed = false;
    g_edit_chain = c; g_edit_slot = s;
    CHECK(param_lens_touch_down(250, 232));
    CHECK(param_lens_is_open());
    param_lens_touch_release(); param_lens_capture();
    CHECK(f.engine.get() == g_eng);
}
bool lens_frame(u32 down = 0, u32 held = 0, u32 up = 0, bool blocked = false) {
    return param_lens_input(down, held, up, blocked);
}
// Shared permanent-edit probe: real controller, independent expected value and
// complete unrelated-parameter checks. Leaves the quick panel closed after lift.
// Existing dump IDs containing "temp" remain packaging-compatible filenames;
// their current fixtures exercise held modulation, never temporary audition.
void lens_quick_begin() {
    param_lens_cancel();param_lens_touch_release();lens_frame();
    lens_frame(KEY_L,KEY_L);
    CHECK(param_lens_is_open()&&!g_eng->param_temp_active());
}
void lens_permanent_probe(Unit& u,int selected,bool relative=true) {
    CHECK(param_lens_is_open());
    const int clean=mutations;
    std::array<q15,PARAM_BANK_SIZE> bases{},depths{};
    std::array<ModSource,PARAM_BANK_SIZE> sources{};
    for(int p=0;p<u.num_params;++p){bases[p]=u.params[p].base;depths[p]=u.params[p].mod_depth;sources[p]=u.params[p].src;}
    // L inside a footer-latched panel is inert; release does not close it.
    lens_frame(KEY_L,KEY_L);CHECK(param_lens_is_open()&&!g_eng->param_temp_active()&&mutations==clean);
    lens_frame(0,0,KEY_L);CHECK(param_lens_is_open());
    lens_quick_begin();
    Param& p=u.params[selected];const int range=p.bipolar?65534:32767;
    const int minimum=p.bipolar?-32767:0;
    CHECK(param_lens_touch_down(250,129));
    if(relative)CHECK(p.base==bases[selected]&&mutations==clean);
    CHECK(param_lens_touch_drag(244,129));
    const q15 expected=q15(relative?std::max(minimum,int(bases[selected])-6*range/271):minimum+220*range/271);
    CHECK(p.base==expected&&!g_eng->param_temp_active());
    for(int i=0;i<u.num_params;++i){CHECK(u.params[i].base==(i==selected?expected:bases[i]));CHECK(u.params[i].src==sources[i]&&u.params[i].mod_depth==depths[i]&&!u.params[i].temp_override);}
    const int edited=mutations;CHECK(edited>=clean+(expected!=bases[selected]));
    lens_frame(0,KEY_TOUCH,KEY_L);CHECK(!param_lens_is_open());
    param_lens_touch_drag(295,129);CHECK(p.base==expected&&mutations==edited&&!g_eng->param_temp_active());
    // A held level is not a new L edge; stale stylus cannot reopen the panel.
    lens_frame(0,KEY_L|KEY_TOUCH);CHECK(!param_lens_is_open());
    param_lens_touch_release();lens_frame(0,0,KEY_L);
    CHECK(p.base==expected&&mutations==edited);
}
void test_param_lens_controls() {
    Fixture f; open_lens(f);
    auto& p = f.engine->chains[0].units[0]->params[0];
    // Navigation/selection is not a patch edit.
    const q15 original = p.base;
    CHECK(mutations == 0);
    param_lens_touch_down(290, 47); param_lens_touch_release();
    param_lens_touch_down(20, 47); param_lens_touch_release();
    CHECK(mutations == 0);
    param_lens_touch_down(295, 129); param_lens_touch_release();
    CHECK(p.base == 32767 && mutations == 1);
    param_lens_touch_down(295, 129); param_lens_touch_release();
    CHECK(mutations == 1); // stationary edits don't dirty twice
    param_lens_touch_down(280, 185); param_lens_touch_release();
    CHECK(p.src == ModSource::EnvFollow);
    param_lens_touch_down(24, 166); param_lens_touch_release();
    CHECK(p.mod_depth == -32767);
    for (int c = 0; c < 4; ++c) {
        param_lens_touch_down(85+c*32, 212); param_lens_touch_release();
        CHECK(f.engine->env_follow_source() == c);
    }
    param_lens_touch_down(245, 212); param_lens_touch_release();
    CHECK(f.engine->env_follow_response() == EnvResponse::Body);
    param_lens_touch_down(245, 212); param_lens_touch_release();
    CHECK(f.engine->env_follow_response() == EnvResponse::Slow);
    p.base = original;
    lens_permanent_probe(*f.engine->chains[0].units[0],0,false);
    // Both pre-existing and same-frame touches must lift after quick entry.
    for(bool simultaneous:{false,true}) {
        param_lens_cancel();param_lens_touch_release();lens_frame();
        const q15 base=p.base;const int dirty=mutations;
        lens_frame(KEY_L|(simultaneous?u32(KEY_TOUCH):0u),KEY_L|KEY_TOUCH);
        CHECK(param_lens_is_open());param_lens_touch_down(295,129);param_lens_touch_drag(24,129);
        CHECK(p.base==base&&mutations==dirty&&!f.engine->param_temp_active());
        param_lens_touch_release();param_lens_touch_down(24,129);param_lens_touch_release();CHECK(p.base==0);
        // Source/depth/settings remain persistent in held entry, not disabled.
        param_lens_touch_down(280,185);param_lens_touch_release();param_lens_touch_down(295,166);param_lens_touch_release();
        CHECK(p.src==ModSource::EnvFollow&&p.mod_depth==32767);
        lens_frame(0,0,KEY_L);CHECK(!param_lens_is_open()&&p.base==0&&p.mod_depth==32767);
    }
    // Muting/stopping affects audio, not permanent modulation editing.
    for(bool mute:{false,true}) {
        f.engine->chains[0].mute=mute;f.engine->transport_stop();lens_quick_begin();
        param_lens_touch_down(295,129);param_lens_touch_release();CHECK(p.base==32767&&!f.engine->param_temp_active());
        lens_frame(0,0,KEY_L);CHECK(!param_lens_is_open());
    }
    f.engine->chains[0].mute=false;
    // L never changes the owner of an ongoing footer-latched base drag.
    open_lens(f);param_lens_touch_down(160,129);
    lens_frame(KEY_L,KEY_L|KEY_TOUCH);param_lens_touch_drag(295,129);
    CHECK(p.base==32767&&param_lens_is_open()&&!f.engine->param_temp_active());
    lens_frame(0,KEY_TOUCH,KEY_L);param_lens_touch_drag(24,129);
    CHECK(p.base==0&&param_lens_is_open()&&!f.engine->param_temp_active());param_lens_touch_release();
    // Closed simultaneous chords never enter; quick chords pass directions on
    // without selecting a different parameter inside the lens.
    for(u32 direction:{u32(KEY_UP),u32(KEY_DOWN),u32(KEY_LEFT),u32(KEY_RIGHT)}) {
        param_lens_cancel();lens_frame();const int selected=fm_focus_selected();const int dirty=mutations;
        u32 down=KEY_L|direction;param_lens_input(down,KEY_L|direction,0,false);
        CHECK(!param_lens_is_open()&&(down&direction)&&mutations==dirty);
        lens_quick_begin();down=direction|KEY_X;
        CHECK(param_lens_input(down,KEY_L|direction|KEY_TOUCH,0,false));
        CHECK(!param_lens_is_open()&&down==direction&&fm_focus_selected()==selected&&mutations==dirty);
        param_lens_touch_drag(295,129);CHECK(p.base==0&&mutations==dirty);param_lens_touch_release();
    }
    // Quick access cannot bypass sequencer lock ownership.
    param_lens_cancel();g_seq_lock_armed=true;lens_frame(KEY_L,KEY_L);
    CHECK(!param_lens_is_open()&&!f.engine->param_temp_active());g_seq_lock_armed=false;lens_frame(0,0,KEY_L);
    // Global chords/cancellation own remaining stylus and require a fresh L.
    for(int reason=0;reason<4;++reason) {
        lens_quick_begin();const q15 base=p.base;const int dirty=mutations;
        if(reason==2)++g_edit_slot;
        u32 down=reason==0?KEY_SELECT|KEY_X|KEY_R|KEY_TOUCH:reason==1?KEY_B|KEY_X|KEY_TOUCH:0;
        CHECK(param_lens_input(down,KEY_L|KEY_TOUCH,0,reason==3));
        CHECK(!param_lens_is_open()&&!f.engine->param_temp_active());
        CHECK(down==(reason==0?u32(KEY_SELECT):0u));
        param_lens_touch_drag(24,129);CHECK(p.base==base&&mutations==dirty);
        g_edit_slot=0;lens_frame(0,KEY_L|KEY_TOUCH);CHECK(!param_lens_is_open());
        param_lens_touch_release();lens_frame(0,0,KEY_L);
    }
    // Replacement is an identity boundary even with the same module kind.
    lens_quick_begin();Unit* replacement=make_unit(f.engine->chains[0].units[0]->kind);
    const q15 replacement_base=replacement->params[0].base;
    delete f.engine->replace_unit(0,0,replacement);param_lens_capture();
    CHECK(!param_lens_is_open());param_lens_touch_drag(24,129);CHECK(replacement->params[0].base==replacement_base);
    lens_frame(0,KEY_L);CHECK(!param_lens_is_open());lens_frame(0,0,KEY_L);
}

void draw_lens(bool top) {
    width = top ? 400 : 320;
    pixels.assign(width * HEIGHT, 0xff000000u);
    objects = outside_rects = outside_lines = text_overlaps = 0; texts.clear();
    // Capture before draw; prove render never accesses engine, audio or mutable unit data.
    // Match main's pre-GPU order: PLAY has its own upper capture, outside the
    // shared inspector capture. Omitting it leaves a stale/blank first module.
    play_focus_capture();param_lens_capture(); Engine* engine = g_eng; auto* audio = g_audio;
    const int kind=int(engine->chains[g_edit_chain].units[g_edit_slot]->kind);
    g_eng = nullptr; g_audio = nullptr;
    if (top) {
        const bool drawn=param_lens_draw_top();
        if(!drawn){g_eng=engine;g_audio=audio;std::fprintf(stderr,"missing lens top for kind %d\n",kind);}
        CHECK(drawn);
    } else param_lens_draw_bottom();
    g_eng = engine; g_audio = audio;
    CHECK(objects > 0 && objects < C2D_DEFAULT_MAX_OBJECTS * 2);
    CHECK(outside_rects == 0 && outside_lines == 0 && text_overlaps == 0);
    for (const auto& t : texts) CHECK(t.x >= 0 && t.x+t.w <= width && t.y >= 0 && t.y+t.h <= HEIGHT);
}
void test_param_lens_draw() {
    Fixture f;
    for (int kind = 1; kind < (int)UnitKind::Count; ++kind) {
        if ((UnitKind)kind == UnitKind::Recorder) continue;
        // Engine public factory follows real module names/parameter banks.
        Unit* u = make_unit((UnitKind)kind);
        CHECK(u); delete f.engine->replace_unit(0, 0, u);
        open_lens(f);
        for (int p = 0; p < u->num_params; ++p) {
            u->params[p].src = p % 2 ? ModSource::EnvFollow : ModSource::SigilB;
            u->params[p].base = p % 2 ? 32767 : 0;
            u->params[p].mod_depth = p % 2 ? -32767 : 32767;
            draw_lens(true); int top_objects = objects; draw_lens(false);
            maximum_shared_objects = std::max(maximum_shared_objects, top_objects+objects);
            CHECK(top_objects+objects < C2D_DEFAULT_MAX_OBJECTS * 2);
            param_lens_touch_down(290, 47); param_lens_touch_release();
        }
        lens_quick_begin(); draw_lens(true); draw_lens(false);
        CHECK(std::none_of(texts.begin(), texts.end(), [](const TextRecord& t) {
            return t.text.find("temp")!=std::string::npos || t.text.find("audition")!=std::string::npos;
        }));
        lens_frame(0,0,KEY_L);CHECK(!param_lens_is_open());
        param_lens_cancel();
    }
}
