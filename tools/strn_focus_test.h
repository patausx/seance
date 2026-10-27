// Included inside sequencer_test.cpp's anonymous namespace after dump_surface.
// Exercises the real runtime controller and drawing, never a duplicate UI model.
struct StrnFixture : Fixture {
    StranularUnit* strn = nullptr;
    bool more = false;
    StrnFixture() {
        delete engine->replace_unit(0, 0, new StranularUnit);
        strn = static_cast<StranularUnit*>(engine->chains[0].units[0]);
        g_view = View::Chains; g_overlay = Overlay::Editor;
        g_edit_chain = g_edit_slot = 0; g_drag = Drag::None;
        g_seq_lock_armed = false; g_seq_lock_chain = g_seq_lock_step = 0;
        stranular_focus_cancel(); capture();
    }
    void capture() { engine->capture_ui_snapshot(g_ui_snapshot, g_edit_chain, g_edit_slot); stranular_focus_capture(); }
    void down(int x, int y) { stranular_focus_release(); CHECK(stranular_focus_touch_body(*strn, x, y)); }
    void tap(int x, int y) { down(x, y); stranular_focus_release(); }
    void drag(int x, int y) { stranular_focus_drag(*strn, x, y); }
    void page(bool extra) { if (more != extra) { tap(280, 211); more = extra; } }
    void select(int p) {
        page(p == 7 || p == 8 || p == 10 || p == 11);
        static const int x[] = {40, 205, 40, 40, 145, 210, 210, 210, 40, 205, 40, 210};
        static const int y[] = {121, 121, 141, 175, 33, 175, 141, 141, 141, 33, 175, 175};
        // Freeze/hold are real toggles: selection for render restores the stored value.
        const q15 before = strn->params[p].base;
        tap(x[p], y[p]);
        if (p == 4 || p == 9) strn->params[p].base = before;
        CHECK(stranular_focus_selected() == p);
    }
};

void test_strn_touch_precision() {
    StrnFixture f; auto& u = *f.strn;
    const auto before_pattern = f.hash();
    // The entire wave is read-only, including the old handle endpoints. Dragging
    // from telemetry onto another control must neither select nor write (even locks).
    for(int pitch_value:{-32767,0,32767}) for(bool extra:{false,true}) {
        f.page(extra);u.params[StranularUnit::P_PITCH].base=(q15)pitch_value;
        f.capture();
        for(bool armed:{false,true}) {
            g_seq_lock_armed=armed;
            std::array<q15,12> stored{};
            for(int p=0;p<12;++p)stored[p]=u.params[p].base;
            const int chosen=stranular_focus_selected(), unchanged=mutations;
            for(int y:{46,52,80,108,113}) for(int x:{8,12,40,155,156,163,164,205,308,311}) {
                f.down(x,y);f.drag(0,121);f.drag(319,190);
                CHECK(stranular_focus_selected()==chosen && mutations==unchanged && f.hash()==before_pattern);
                for(int p=0;p<12;++p)CHECK(u.params[p].base==stored[p]);
            }
        }
        g_seq_lock_armed=false;
        // Fixed rectangle boundaries: every x across both handles, the 8px gap,
        // and the pixels just above/below. Only the touched parameter can change.
        for(int y:{113,114,121,133,134}) for(int x=0;x<320;++x) {
            u.params[0].base=u.params[1].base=16000;
            std::array<q15,12> stored{};
            for(int p=0;p<12;++p)stored[p]=u.params[p].base;
            const int chosen=stranular_focus_selected(), unchanged=mutations;
            const int target=y>=114&&y<134?(x>=8&&x<156?0:x>=164&&x<312?1:-1):-1;
            f.down(x,y);f.drag(x<160?x+20:x-20,y);
            CHECK(stranular_focus_selected()==(target<0?chosen:target));
            for(int p=0;p<12;++p)CHECK(p==target?u.params[p].base!=stored[p]:u.params[p].base==stored[p]);
            CHECK(mutations==unchanged+(target>=0?1:0) && f.hash()==before_pattern);
        }
    }
    f.page(false);stranular_focus_release();
    const std::array<std::pair<int,int>,6> controls = {{{40,121},{205,121},{40,155},{210,155},{70,190},{210,190}}};
    const int params[] = {0,1,2,6,3,5};
    for (unsigned i=0; i<controls.size(); ++i) {
        const auto xy = controls[i]; const q15 before = u.params[params[i]].base;
        const int dirty = mutations;
        f.down(xy.first, xy.second); CHECK(stranular_focus_selected() == params[i]);
        f.drag(xy.first, xy.second); f.drag(xy.first, xy.second);
        CHECK(u.params[params[i]].base == before && mutations == dirty && f.hash() == before_pattern);
        stranular_focus_release();
    }
    auto& pitch = u.params[StranularUnit::P_PITCH];
    pitch.base = 123; const int dirty = mutations;
    f.tap(84,211); f.tap(30,211); // input modes only, no retune
    CHECK(pitch.base == 123 && mutations == dirty);
    pitch.base = 0;
    f.down(70,190); f.drag(76,190);
    CHECK(std::abs((int)pitch.base * 24 - 32767) <= 24);
    f.drag(70,190); CHECK(pitch.base == 0);
    f.drag(64,190); CHECK(std::abs((int)pitch.base * 24 + 32767) <= 24);
    f.drag(319,190); CHECK(pitch.base == 32767);
    pitch.base = -32000; f.down(145,190); f.drag(0,190); CHECK(pitch.base == -32767);
    f.tap(84,211); pitch.base = 123;
    f.down(70,190); f.drag(71,190); CHECK(pitch.base == 127);
    const int fine_dirty = mutations;
    f.drag(71,190); CHECK(mutations == fine_dirty);
    // External/other-controller edits cannot be overwritten by a held stylus.
    pitch.base=7000;f.drag(71,190);CHECK(pitch.base==7000 && mutations==fine_dirty);
    f.drag(72,190);CHECK(pitch.base==131); // still original123 +2*4, not external7000
    pitch.base=8000;const int external_dirty=mutations;
    f.drag(72,190);CHECK(pitch.base==8000 && mutations==external_dirty);
    f.drag(70,190); CHECK(pitch.base == 123);
    f.drag(69,190); CHECK(pitch.base == 119);
    f.down(70,190); f.drag(71,190); CHECK(pitch.base == 123); // repeated relative origin
    pitch.base = 32766; f.down(70,190); f.drag(71,190); CHECK(pitch.base == 32767);
    pitch.base = -32766; f.down(70,190); f.drag(69,190); CHECK(pitch.base == -32767);
    f.tap(140,211); CHECK(pitch.base == 0);
    const int zero_dirty = mutations; f.tap(140,211); CHECK(mutations == zero_dirty);
    CHECK(f.hash() == before_pattern);
}

void test_strn_all_controls() {
    StrnFixture f; auto& u = *f.strn;
    const int old_dirty = mutations;
    f.tap(280,211); f.more = true; f.tap(280,211); f.more = false;
    CHECK(mutations == old_dirty);
    const int params[] = {0,1,2,6,5,8,7,10,11};
    const int xx[] = {60,205,60,205,205,60,205,60,205};
    const int yy[] = {121,121,155,155,190,155,155,190,190};
    for (int i=0; i<9; ++i) {
        f.page(i >= 5); const int p = params[i]; u.params[p].base = 12000;
        const int dirty = mutations;
        f.down(xx[i], yy[i]); f.drag(xx[i],yy[i]);
        CHECK(stranular_focus_selected() == p && u.params[p].base == 12000 && mutations == dirty);
        const int move=p==11?60:20;
        f.drag(xx[i]+(p==0?-move:move),yy[i]); CHECK(u.params[p].base > 12000);
        const int changed = mutations; f.drag(xx[i]+(p==0?-move:move),yy[i]); CHECK(mutations == changed);
        f.drag(xx[i],yy[i]);
        if(p==11) CHECK(std::abs((int)StranularUnit::cluster_count(u.params[p].base)-(int)StranularUnit::cluster_count(12000))<=1);
        else CHECK(u.params[p].base == 12000);
        // Repeated gestures, rather than synthetic out-of-screen coordinates, reach endpoints.
        for(int repeat=0;repeat<12;++repeat) { f.down(xx[i],yy[i]); f.drag(p==0?0:319,yy[i]); }
        CHECK(u.params[p].base == 32767);
        for(int repeat=0;repeat<12;++repeat) { f.down(xx[i],yy[i]); f.drag(p==0?319:0,yy[i]); }
        CHECK(u.params[p].base == (u.params[p].bipolar ? -32767 : 0));
    }
    u.params[StranularUnit::P_CLUSTER].base = 0;
    CHECK(StranularUnit::cluster_count(u.params[11].base) == 1);
    f.down(205,190); f.drag(319,190);
    for(int i=0;i<4;++i) { f.down(205,190); f.drag(319,190); }
    CHECK(StranularUnit::cluster_count(u.params[11].base) == 4);
    u.params[10].base = 0; u.sync_division = 0;
    for(int d=0;d<4;++d) { f.tap(120,175); CHECK(u.params[10].base > 16384 && u.sync_division == d); }
    f.tap(120,175); CHECK(u.params[10].base == 0 && u.sync_division == 0);
    for(int mode=0;mode<3;++mode) {
        f.tap(mode==0?20:mode==1?60:100,211); CHECK(u.window_mode == mode);
        const int dirty=mutations; f.tap(mode==0?20:mode==1?60:100,211); CHECK(mutations == dirty);
    }
    f.tap(200,211); CHECK(u.long_mode); f.tap(160,211); CHECK(!u.long_mode);
    f.page(false);
    for(int p:{StranularUnit::P_FREEZE,StranularUnit::P_HOLD}) {
        u.params[p].base=0; const int x=p==4?145:205;
        f.tap(x,33); CHECK(u.params[p].base == 32767 && stranular_focus_selected() == p);
        f.tap(x,33); CHECK(u.params[p].base == 0);
    }
    CHECK(!u.bypass); f.tap(240,10); CHECK(u.bypass); f.tap(240,10); CHECK(!u.bypass);
    const int dirty=mutations;
    stranular_focus_touch_body(u,290,10); // owned but no destructive touch target
    CHECK(mutations==dirty && f.engine->chains[0].units[0]==&u);
}

void test_strn_locks_context() {
    StrnFixture f; auto& u=*f.strn; auto& pitch=u.params[3];
    f.tap(84,211); pitch.base=123;
    g_seq_lock_armed=true; g_seq_lock_step=7;
    CHECK(f.engine->set_step_lock(0,7,0,3,1000));
    const auto before=f.hash(); const int dirty=mutations;
    f.down(70,190); f.drag(70,190); CHECK(f.hash()==before && mutations==dirty);
    f.drag(71,190); CHECK(pitch.base==123 && seq_display_value(0,3,0)==1004);
    const int edited=mutations; f.drag(71,190); CHECK(mutations==edited);
    CHECK(f.engine->set_step_lock(0,7,0,3,7000));
    const auto external=f.hash();f.drag(71,190);
    CHECK(f.hash()==external && mutations==edited && seq_display_value(0,3,0)==7000);
    f.drag(72,190);CHECK(seq_display_value(0,3,0)==1008 && pitch.base==123);
    f.drag(70,190); CHECK(seq_display_value(0,3,0)==1000);
    f.tap(140,211); CHECK(seq_display_value(0,3,1)==0 && pitch.base==123);
    f.engine->clear_track(0);
    for(int p:{0,1,2,5}) CHECK(f.engine->set_step_lock(0,7,0,p,1234));
    const auto full=f.hash(); const int full_dirty=mutations;
    f.down(70,190); f.drag(71,190); f.tap(140,211);
    CHECK(f.hash()==full && mutations==full_dirty && pitch.base==123);
    const bool bypass=u.bypass, long_mode=u.long_mode; const int window=u.window_mode, division=u.sync_division;
    const q15 sync=u.params[10].base;
    f.tap(240,10); f.page(true);
    for(int x:{20,60,100,160,200}) f.tap(x,211);
    f.tap(120,175);
    CHECK(u.bypass==bypass && u.long_mode==long_mode && u.window_mode==window && u.sync_division==division);
    CHECK(u.params[10].base==sync && mutations==full_dirty && f.hash()==full);
    f.page(false); f.tap(30,211); f.tap(84,211); CHECK(mutations==full_dirty);
    // Continuous sync base still writes an ordinary lock; subdivision config does not.
    f.engine->clear_track(0); f.page(true); u.params[10].base=1000;
    f.down(60,190); f.drag(80,190);
    CHECK(u.params[10].base==1000 && seq_display_value(0,10,1000)>1000);
    const auto sync_lock=f.hash(); const int sync_dirty=mutations;
    f.tap(120,175); CHECK(f.hash()==sync_lock && mutations==sync_dirty && u.sync_division==division);
    f.page(false);
    // Cancel or context change must discard a previous relative origin.
    g_seq_lock_armed=false;
    f.down(70,190); const q15 original=pitch.base;
    stranular_focus_cancel(); f.drag(100,190); CHECK(pitch.base==original);
    f.capture(); f.down(70,190); g_edit_slot=1; f.capture(); f.drag(100,190);
    CHECK(pitch.base==original); g_edit_slot=0; f.capture();
    f.down(70,190); stranular_focus_release(); f.drag(100,190); CHECK(pitch.base==original);
    f.down(70,190); g_overlay=Overlay::None; stranular_focus_capture(); f.drag(100,190); CHECK(pitch.base==original);
    CHECK(!stranular_focus_draw_top() && !stranular_focus_draw_bottom());
    g_overlay=Overlay::Editor;
    g_view=View::Sequencer; stranular_focus_capture();
    CHECK(!stranular_focus_draw_top() && !stranular_focus_draw_bottom());
}

void test_strn_old_hold_freeze() {
    StrnFixture f; auto& u=*f.strn;
    u.params[StranularUnit::P_MIX].base=2000;
    f.tap(205,33); CHECK(u.params[9].base==32767); // hold only prevents spawning
    for(auto& p:u.params) p.snap();
    std::array<q15,512> audio; audio.fill(9000); u.process(audio.data(),512);
    CHECK(std::all_of(audio.begin(),audio.end(),[](q15 v){return v==9000;})); // unchanged no-grain dry fallback
    CHECK(!u.visual_frozen() && u.visual_grains()==0);
    f.tap(145,33); for(auto& p:u.params) p.snap(); audio.fill(9000); u.process(audio.data(),512);
    CHECK(u.visual_frozen() && u.visual_mix()==32767 && u.params[5].base==2000);
    const int dirty=mutations; f.capture(); CHECK(mutations==dirty && u.params[5].base==2000);
    f.tap(145,33); for(auto& p:u.params) p.snap(); audio.fill(9000); u.process(audio.data(),512);
    CHECK(!u.visual_frozen() && u.visual_mix()==2000 && u.params[9].base==32767);
}

void strn_menu_palette_and_paper() {
    auto paper_row=[](int y) { return theme_test::paper(y); };
    const std::array<u32,4> inks={theme_test::ink(),theme_test::spot(),theme_test::ghost(),theme_test::branch()};
    CHECK(pixels.size()==(size_t)width*HEIGHT);
    for(int y=0;y<HEIGHT;++y)for(int x=0;x<width;++x) {
        const u32 px=pixels[y*width+x];
        CHECK(px==paper_row(y)||std::find(inks.begin(),inks.end(),px)!=inks.end());
    }
    for(int y=0;y<HEIGHT;++y)for(int x=0;x<2;++x)CHECK(pixels[y*width+x]==paper_row(y));
}
void strn_render(bool top) {
    width=top?400:320; objects=outside_rects=outside_lines=text_overlaps=0;
    texts.clear(); pixels.assign(width*HEIGHT,0xff000000u);
    struct Restore {
        Engine* engine=g_eng; platform::AudioOut* audio=g_audio;
        Restore(){g_eng=nullptr;g_audio=nullptr;}
        ~Restore(){g_eng=engine;g_audio=audio;strn_hero_audit=false;}
    } restore;
    strn_hero_audit=top;
    CHECK(top?stranular_focus_draw_top():stranular_focus_draw_bottom());
    CHECK(objects>0 && objects<C2D_DEFAULT_MAX_OBJECTS*2);
    if(outside_rects || outside_lines || text_overlaps)
        std::fprintf(stderr,"strn %s diagnostics: rectangles=%d lines=%d text overlaps=%d\n",top?"top":"bottom",outside_rects,outside_lines,text_overlaps);
    CHECK(outside_rects==0 && outside_lines==0 && text_overlaps==0);
    strn_menu_palette_and_paper();
    for(const auto& t:texts) {
        CHECK(t.x>=0 && t.y>=0 && t.x+t.w<=width && t.y+t.h<=HEIGHT);
        if(top && t.y>=120 && t.y<140 && t.x<239) CHECK(t.x+t.w<239);
    }
    if(top) maximum_top_objects=std::max(maximum_top_objects,objects);
    else maximum_objects=std::max(maximum_objects,objects);
}
void strn_dual() {
    strn_render(true); const int top=objects; strn_render(false);
    maximum_shared_objects=std::max(maximum_shared_objects,top+objects);
    CHECK(top+objects<C2D_DEFAULT_MAX_OBJECTS*2);
}
void test_strn_menu_style_geometry() {
    StrnFixture f; auto& u=*f.strn;
    // Styling may not move, merge or create controller targets. Reassert every
    // original boundary plus the inert waveform and header delete text.
    struct Zone { int x,y,w,h,param; } zones[]={
        {8,114,148,20,StranularUnit::P_POS},{164,114,148,20,StranularUnit::P_SIZE},
        {8,136,147,29,StranularUnit::P_DENS},{166,136,147,29,StranularUnit::P_SPRAY},
        {8,170,147,29,StranularUnit::P_PITCH},{166,170,147,29,StranularUnit::P_MIX}
    };
    for(const auto& z:zones)for(int edge=0;edge<4;++edge) {
        const int x=edge==0?z.x:edge==1?z.x+z.w-1:z.x+z.w/2;
        const int y=edge==2?z.y:edge==3?z.y+z.h-1:z.y+z.h/2;
        f.down(x,y);CHECK(stranular_focus_selected()==z.param);stranular_focus_release();
    }
    const auto before=f.hash();std::array<q15,12> values{};
    for(int p=0;p<12;++p)values[p]=u.params[p].base;
    for(auto xy:std::array<std::pair<int,int>,8>{{{8,46},{155,80},{163,113},{311,52},{156,121},{160,121},{163,121},{290,10}}}) {
        f.down(xy.first,xy.second);f.drag(319,xy.second);stranular_focus_release();
    }
    CHECK(f.hash()==before);for(int p=0;p<12;++p)CHECK(u.params[p].base==values[p]);
    f.capture();strn_render(false);strn_render(true);
}

void test_strn_draw() {
    StrnFixture f; auto& u=*f.strn;
    for(bool longer:{false,true}) for(int p=0;p<u.num_params;++p) for(int edge:{-1,0,1}) {
        f.select(p); u.long_mode=longer;
        u.params[p].base=edge<0?(u.params[p].bipolar?-32767:0):edge>0?32767:12345;
        u.params[p].src=edge<0?ModSource::EnvFollow:ModSource::SigilD;
        u.params[p].mod_depth=edge<0?-32767:32767; u.params[p].snap();
        f.capture(); strn_dual();
    }
    // Fill the real ring and all eight grain voices, then exercise the four-digit
    // history-age hero, selected long size, drawn trace and reverse read heads.
    for(auto& p:u.params) {p.src=ModSource::None;p.base=0;}
    u.params[1].base=u.params[2].base=u.params[5].base=u.params[11].base=32767;
    u.long_mode=true;for(auto& p:u.params)p.snap();
    std::array<q15,512> wave{};
    for(int b=0;b<150;++b) {
        for(int i=0;i<512;++i)wave[i]=(q15)(std::sin((b*512+i)*0.023)*24000);
        u.process(wave.data(),512);
    }
    CHECK(u.scan_span_frames()>=64000 && u.active_grain_count()==8 && u.visual_grains()==255);
    CHECK(std::any_of(u.visual_wave(),u.visual_wave()+StranularUnit::VIZ_POINTS,[](q15 v){return v!=0;}));
    // A stored pitch edit cannot stretch a fictitious source-range bracket.
    // With no audio processing, all telemetry and both fixed handles stay exact.
    f.select(0);u.params[3].base=-32767;f.capture();strn_render(false);const auto scan=pixels;
    u.params[3].base=32767;f.capture();strn_render(false);
    for(int y=44;y<134;++y)for(int x=0;x<320;++x)CHECK(pixels[y*320+x]==scan[y*320+x]);
    auto& sigil=f.engine->sigils[1];sigil.stroke_to(0,-32767,63,32767);sigil.stroke_to(63,32767,127,-32767);
    u.params[0].base=32767;u.params[1].base=0;u.params[3].src=ModSource::SigilB;u.params[3].mod_depth=12000;
    for(int p:{0,1,3}) for(int sign:{-1,1}) {
        f.select(p);u.params[p].base=u.params[p].bipolar?sign*32767:32767;
        sigil.depth=sign*32767;sigil.phase=sign<0?0:0xffffffffu;
        f.capture();strn_dual();
    }
    f.select(3);sigil.mode=SigilMode::Loop;f.capture();strn_render(true);
    const auto drawn=pixels;const int drawn_objects=objects;
    sigil.mode=SigilMode::RandomStep;f.capture();strn_render(true);
    CHECK(pixels!=drawn && objects<drawn_objects); // random uses output marker, never drawn table
    sigil.mode=SigilMode::Loop;u.params[7].base=32767;u.params[1].base=32767;
    for(auto& p:u.params)p.snap();
    for(int b=0;b<70;++b){wave.fill(12000);u.process(wave.data(),512);}
    CHECK(u.active_grain_count()==8);
    CHECK(std::any_of(u.visual_increments(),u.visual_increments()+StranularUnit::MAX_GRAINS,[](int32_t v){return v<0;}));
    f.select(3);f.capture();strn_dual();
    // Captured draw is immutable even when mutable unit/base/snapshot data changes.
    f.page(false); f.select(3); f.capture(); strn_render(true); const auto top=pixels;
    strn_render(false); const auto bottom=pixels;
    u.params[3].base=-111; u.params[0].base=0; u.long_mode=!u.long_mode; g_ui_snapshot={};
    strn_render(true); CHECK(pixels==top); strn_render(false); CHECK(pixels==bottom);
    // Full six-slot path uses maximum four-letter labels, incl bypass strikes.
    for(int slot=1;slot<UNITS_PER_CHAIN;++slot) {
        delete f.engine->replace_unit(0,slot,new StranularUnit);
        f.engine->chains[0].units[slot]->bypass=slot%2;
    }
    f.capture();strn_dual();
    for(int slot=1;slot<UNITS_PER_CHAIN;++slot) {
        g_edit_slot=slot;f.capture();strn_dual();
    }
    // Source removed between captures: old frozen DTO remains drawable, next capture invalidates it.
    Unit* removed=f.engine->replace_unit(0,g_edit_slot,nullptr);
    strn_dual();delete removed;stranular_focus_capture();
    CHECK(!stranular_focus_draw_top() && !stranular_focus_draw_bottom());
    stranular_focus_cancel(); CHECK(!stranular_focus_draw_top() && !stranular_focus_draw_bottom());
}

void test_strn_lens_routing() {
    StrnFixture f; auto& u=*f.strn;
    f.select(StranularUnit::P_PITCH);
    CHECK(param_lens_touch_down(250,232)); param_lens_touch_release(); CHECK(param_lens_is_open());
    param_lens_touch_down(280,185); param_lens_touch_release();
    CHECK(u.params[3].src==ModSource::EnvFollow && u.params[0].src==ModSource::None);
    param_lens_touch_down(24,166); param_lens_touch_release(); CHECK(u.params[3].mod_depth==-32767);
    lens_permanent_probe(u,3,false);CHECK(stranular_focus_selected()==3);
    CHECK(param_lens_touch_down(250,232));param_lens_touch_release();draw_lens(true);draw_lens(false);
    lens_frame(KEY_B,KEY_B);CHECK(!param_lens_is_open());
    f.capture(); strn_dual();
    g_seq_lock_armed=true; CHECK(!param_lens_touch_down(250,232));
}

void dump_strn(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tparam\ttop_objects\tbottom_objects\tshared_objects\n";
    StrnFixture f; auto& u=*f.strn;
    // Feed deterministic actual PCM into the existing STRN engine so waveform/readheads
    // in these captures are real telemetry, not synthetic drawing substitutions.
    std::array<q15,512> audio{};
    for(auto& p:u.params) p.snap();
    for(int block=0;block<128;++block) {
        for(int i=0;i<512;++i) audio[i]=(q15)(std::sin((block*512+i)*0.019)*10000+std::sin((block*512+i)*0.073)*4000);
        u.process(audio.data(),512);
    }
    auto save=[&](const std::string& name) {
        f.capture(); strn_render(true); const int top=objects; dump_surface(directory,name+"_top");
        strn_render(false); const int bottom=objects; dump_surface(directory,name+"_bottom");
        CHECK(top+bottom<C2D_DEFAULT_MAX_OBJECTS*2);
        manifest<<name<<'\t'<<stranular_focus_selected()<<'\t'<<top<<'\t'<<bottom<<'\t'<<top+bottom<<'\n';
    };
    for(int p=0;p<u.num_params;++p) { f.select(p); save("param_"+std::to_string(p)); }
    f.select(3); u.params[3].src=ModSource::SigilB; u.params[3].mod_depth=4000;
    u.params[3].base=9721; u.params[3].snap();
    f.engine->sigils[1].stroke_to(0,-26000,63,26000);
    f.engine->sigils[1].stroke_to(63,26000,127,-26000);
    f.engine->sigils[1].phase=0x68000000;
    // Real engine control ticks evaluate the drawn table and normal modulation/slew.
    std::array<q15,1024> output{};
    for(int block=0;block<4;++block)f.engine->render(output.data(),512);
    CHECK(u.params[3].base==9721 && u.params[3].effective!=u.params[3].base);
    CHECK(f.engine->sigils[1].value!=0);save("pitch_mod");
    f.tap(84,211); save("pitch_fine");
    f.page(true); u.long_mode=true; save("more_long");
    f.page(false); f.tap(145,33); for(auto& p:u.params) p.snap();
    audio.fill(0); u.process(audio.data(),512); save("frozen");
    f.tap(205,33); for(auto& p:u.params) p.snap(); u.process(audio.data(),512); save("frozen_hold");
    std::ofstream notice(directory/"README.txt");
    notice<<"actual c++ STRN menu-style focus controller; portable engine telemetry; software Citro2D raster.\n"
          <<"exact post-splash smoothstep paper and carbon/violet/ghost/branch inks only.\n"
          <<"not hardware screenshots or performance measurements. line coverage approximates GPU.\n"
          <<"reproduce: make test-strn; /tmp/seance_strn_test --dump-strn ui_mockups/strn_runtime\n"
          <<"PPM native top400x240/bottom320x240. paired PNGs use nearest-neighbor 2x only.\n";
    std::printf("strn software frames: %s\n",directory.string().c_str());
}
