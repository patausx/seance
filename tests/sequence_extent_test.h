// Included in the existing actual controller/engine/raster harness.
// Replaces the retired touch-settings tests with the approved held-B workflow.
void extent_hold_key(Fixture& f,u32 key) {
    f.frame(key,KEY_B|key);f.frame(0,KEY_B,key);
}
void extent_test_open(Fixture& f,int chain=-1) {
    f.frame();
    if(chain>=0)while(sequencer_selected_chain()!=chain)f.tap(KEY_DOWN);
    f.frame(KEY_B,KEY_B);
    for(int i=0;i<15;++i)f.frame(0,KEY_B);
    draw();CHECK(has_text("hold b") && has_text("up/down bank"));
}
void extent_test_close(Fixture& f) {
    f.frame(0,0,KEY_B);f.frame();f.frame();
    draw();CHECK(!has_text("hold b"));
}
void extent_bank(Fixture& f,int bank) {
    const int old=sequencer_edit_bank();extent_test_open(f);
    for(int n=0;n<(bank-old+4)%4;++n)extent_hold_key(f,KEY_DOWN);
    extent_test_close(f);CHECK(sequencer_edit_bank()==bank);
}
void test_sequence_extent_controls() {
    Fixture f;f.frame();
    f.engine->pattern.steps[0][47].flags=SeqStep::Trig;
    f.engine->pattern.steps[0][47].set_retrig_count(3);
    f.engine->pattern.microtiming[0][47]=-7;
    CHECK(f.engine->set_step_lock(0,47,0,FmUnit::P_INDEX,14000));
    const auto stored=f.engine->pattern.steps[0][47];f.refresh();
    extent_test_open(f);
    for(int i=0;i<20;++i)extent_hold_key(f,KEY_LEFT);
    CHECK(f.engine->pattern.length[0]==16); // staged, never a hidden live write
    extent_test_close(f);CHECK(f.engine->pattern.length[0]==1);
    const int at_min=mutations;extent_test_open(f);extent_hold_key(f,KEY_LEFT);extent_test_close(f);
    CHECK(mutations==at_min);
    extent_test_open(f);for(int i=0;i<70;++i)extent_hold_key(f,KEY_RIGHT);extent_test_close(f);
    CHECK(f.engine->pattern.length[0]==64);
    CHECK(std::memcmp(&stored,&f.engine->pattern.steps[0][47],sizeof stored)==0);
    CHECK(f.engine->pattern.microtiming[0][47]==-7);
    CHECK(f.engine->pattern.length[1]==16 && f.engine->pattern.length[3]==16);
    // Bank selection alone is UI state, not project dirtiness or launch.
    const auto active=f.hash();const int clean=mutations;
    f.engine->request_pattern(7); // stopped launch establishes a real nonvisible bank
    f.refresh();CHECK(f.engine->active_pattern==7);
    extent_bank(f,2);CHECK(mutations==clean && f.engine->active_pattern==7);
    // Selected bank aliases stored data, not Engine::pattern or stale bank0.
    extent_test_open(f,2);extent_hold_key(f,KEY_RIGHT);extent_hold_key(f,KEY_A);extent_test_close(f);
    CHECK(f.engine->sequence_pattern(2).length[2]==17);
    CHECK(f.engine->sequence_pattern(2).direction[2]==SequenceDirection::Reverse);
    CHECK(f.engine->pattern.length[2]==16 && pattern_hash(f.engine->sequence_pattern(0))==active);
    f.mode(0);f.touch(2,15);CHECK(f.engine->sequence_pattern(2).steps[2][15].flags&SeqStep::Trig);
    CHECK(!f.engine->pattern.steps[2][15].flags);
    // Every page remains independent from length; the final inset target includes x311.
    for(int page=1;page<=3;++page) { f.tap(KEY_L|KEY_RIGHT);CHECK(sequencer_current_page()==page); }
    f.frame();sequencer_view_touch_down(311,layout::ROWS[2]+30);f.release();
    CHECK(sequencer_selected_step()==63 && (f.engine->sequence_pattern(2).steps[2][63].flags&SeqStep::Trig));
    const auto inactive=f.engine->sequence_pattern(2).steps[2][63];
    extent_test_open(f);for(int i=0;i<20;++i)extent_hold_key(f,KEY_LEFT);extent_test_close(f);
    CHECK(f.engine->sequence_pattern(2).length[2]==1 && sequencer_current_page()==3);
    CHECK(!std::memcmp(&inactive,&f.engine->sequence_pattern(2).steps[2][63],sizeof inactive));
    // All seven non-slice edit paths use the selected bank, including full-lock refusal.
    f.mode(1);f.touch(0,0);f.tap(KEY_A);
    auto& bank=f.engine->sequence_pattern(2);
    CHECK(bank.steps[0][48].lock_count==1 && f.engine->pattern.steps[0][48].lock_count==0);
    f.mode(2);f.tap(KEY_A);CHECK(bank.steps[0][48].lock_count==2);
    f.mode(3);f.tap(KEY_A);CHECK(bank.steps[0][48].retrig_count()==2);
    f.mode(4);f.tap(KEY_A);CHECK(bank.steps[0][48].condition()==SeqStep::Condition::Half);
    f.mode(5);f.tap(KEY_A);CHECK(bank.microtiming[0][48]==1);
    f.mode(6);f.tap(KEY_L|KEY_UP);CHECK(f.engine->pattern.steps[0][48].lock_count==0);
    f.mode(0);f.tap(KEY_Y);f.tap(KEY_RIGHT);f.tap(KEY_Y);
    CHECK(!std::memcmp(&bank.steps[0][48],&bank.steps[0][49],sizeof(SeqStep)));
    CHECK(bank.microtiming[0][49]==1);f.tap(KEY_X);CHECK(bank.steps[0][49].lock_count==0);
    f.refresh();sequencer_capture_inspector();
    CHECK(sequencer_snapshot().sequence_step[0]==255 && g_ui_snapshot.active_pattern==7);
    CHECK(sequencer_snapshot().sequence_length[2]==1 && g_ui_snapshot.sequence_length[2]==16);
    CHECK(sequencer_inspector().step==49);
    check_dual_budget();
}
void test_sequence_extent_direction() {
    for(int direction=0;direction<3;++direction) {
        Fixture f;f.frame();extent_test_open(f);
        extent_hold_key(f,KEY_RIGHT);extent_hold_key(f,KEY_RIGHT);
        for(int i=0;i<direction;++i)extent_hold_key(f,KEY_A);
        CHECK(f.engine->pattern.length[0]==16 && f.engine->pattern.direction[0]==SequenceDirection::Forward);
        extent_test_close(f);CHECK(f.engine->pattern.length[0]==18);
        CHECK((int)f.engine->pattern.direction[0]==direction);
        f.tap(KEY_L|KEY_RIGHT);f.touch(0,1);f.tap(KEY_START);
        std::array<q15,1024> audio{};
        uint64_t visited=0,previous_tick=0;
        for(int block=0;block<600 && f.engine->sequence_tick<36;++block) {
            f.engine->render(audio.data(),512);f.refresh();
            const uint64_t tick=f.engine->sequence_tick;if(!tick||tick==previous_tick)continue;
            const int t=(int)(tick-1),phase=t%34;
            const int expected=direction==0?t%18:direction==1?17-t%18:phase<18?phase:34-phase;
            CHECK(g_ui_snapshot.sequence_step[0]==expected);
            visited|=uint64_t(1)<<expected;previous_tick=tick;
        }
        CHECK(f.engine->sequence_tick>=36 && visited==((uint64_t(1)<<18)-1));
        CHECK(sequencer_current_page()==1 && sequencer_selected_step()==17);
        extent_test_open(f);for(int i=0;i<17;++i)extent_hold_key(f,KEY_LEFT);extent_test_close(f);
        for(int i=0;i<9;++i)f.engine->render(audio.data(),512);
        f.refresh();CHECK(g_ui_snapshot.sequence_step[0]==0 && f.trig(0,17));
        f.tap(KEY_START);
    }
}
void test_sequence_extent_ownership() {
    Fixture f;f.frame();f.touch(0,0);f.tap(KEY_Y);const auto original=f.hash();
    // 14 frame tap keeps legacy clear; 15 frame hold must preserve clipboard.
    f.frame(KEY_B,KEY_B);for(int i=0;i<13;++i)f.frame(0,KEY_B);
    draw();CHECK(!has_text("hold b"));f.frame(0,0,KEY_B);f.frame();f.frame();
    f.tap(KEY_RIGHT);f.tap(KEY_Y);CHECK(!f.trig(0,1)); // copied empty, not pasted
    f.tap(KEY_B);f.frame();f.frame();f.tap(KEY_LEFT);f.tap(KEY_Y);
    extent_test_open(f);extent_hold_key(f,KEY_DOWN);extent_test_close(f);
    f.tap(KEY_Y);CHECK(f.engine->sequence_pattern(1).steps[0][0].flags&SeqStep::Trig);
    CHECK(f.hash()==original); // hold did not clear source clipboard or change playing pattern
    // Pending touch cancellation; key/touch/A release cannot leak into the score.
    f.down(0,3);f.frame(KEY_B|KEY_A,KEY_B|KEY_A);
    for(int i=0;i<15;++i)f.frame(0,KEY_B|KEY_A);
    const auto untouched=pattern_hash(f.engine->sequence_pattern(1));
    sequencer_view_touch_down(319,layout::ROWS[3]);sequencer_view_touch_hold(100,218);f.release();
    f.frame(KEY_X|KEY_Y|KEY_L|KEY_R,KEY_B|KEY_X|KEY_Y|KEY_L|KEY_R);
    f.frame(KEY_A|KEY_RIGHT,KEY_A|KEY_RIGHT|KEY_TOUCH,KEY_B);
    sequencer_view_touch_down(319,layout::ROWS[3]);f.release();
    for(int i=0;i<3;++i)f.frame(KEY_A,KEY_A|KEY_TOUCH);
    f.frame();f.frame();
    CHECK(pattern_hash(f.engine->sequence_pattern(1))==untouched && sequencer_selected_layer()==0);
    // Navigation cancels staged edits. Held inputs stay quarantined through release.
    extent_test_open(f);extent_hold_key(f,KEY_RIGHT);extent_hold_key(f,KEY_A);
    f.frame(KEY_SELECT,KEY_SELECT|KEY_B);f.frame(KEY_A,KEY_B|KEY_A);
    f.frame(0,0,KEY_B|KEY_A);f.frame();
    CHECK(pattern_hash(f.engine->sequence_pattern(1))==untouched);
    // Repeat, wrap-safe threshold and unchanged direction round trip.
    g_ui_frame=UINT32_MAX-7;extent_test_open(f);
    f.frame(KEY_RIGHT,KEY_B|KEY_RIGHT);for(int i=0;i<29;++i)f.frame(0,KEY_B|KEY_RIGHT);
    for(int i=0;i<3;++i)extent_hold_key(f,KEY_A);
    extent_test_close(f);CHECK(f.engine->sequence_pattern(1).length[0]==20);
    CHECK(f.engine->sequence_pattern(1).direction[0]==SequenceDirection::Forward);
    const auto applied=pattern_hash(f.engine->sequence_pattern(1));
    const u32 keys[]={KEY_X,KEY_UP,KEY_RIGHT,KEY_DOWN,KEY_Y,KEY_LEFT,KEY_A,KEY_L};
    for(int mode=0;mode<8;++mode) {
        f.chord(keys[mode]);CHECK(sequencer_selected_layer()==mode);
        CHECK(pattern_hash(f.engine->sequence_pattern(1))==applied);
    }
    f.mode(6);f.tap(KEY_L|KEY_B);draw();CHECK(!has_text("hold b"));
    f.r_hold();f.frame(KEY_B,KEY_R|KEY_B);f.frame(0,0,KEY_R|KEY_B);draw();CHECK(!has_text("hold b"));
    sequencer_open_slices(2);f.frame();f.tap(KEY_B);f.frame();draw();CHECK(!has_text("hold b"));
    // Dense all-mode geometry: actual native ink centered in inset squares.
    f.engine->request_pattern(0,false);sequencer_forget_pins();sequencer_view_enter();f.frame();f.dense();
    for(int mode=0;mode<8;++mode) {
        f.mode(mode);check_dual_budget();CHECK(text_overlaps==0);
        for(const auto& text:texts)for(int row:layout::ROWS) {
            if(text.y<row || text.y>=row+layout::CELL_H)continue;
            const int cell=(text.x-layout::X0)/layout::PITCH;
            CHECK(cell>=0 && cell<16);
            const int x=layout::X0+cell*layout::PITCH;
            CHECK(text.x>=x && text.x+text.w<=x+layout::CELL_W);
            // Diagonal WHEN fractions intentionally have two offset numerals.
            if(mode!=4)CHECK(std::abs(2*text.x+text.w-(2*x+layout::CELL_W))<=1);
            if((mode==1||mode==2||mode==6||mode==7) && text.y<row+layout::BADGE_H)
                CHECK(std::abs(2*text.y+text.h-(2*row+layout::BADGE_H))<=1);
        }
        for(int c=0;c<4;++c)for(int edge : {0,15}) { f.touch(c,edge);check_dual_budget(); }
        for(int row:layout::ROWS)for(int y=row-2;y<row+layout::CELL_H+2;++y)
            for(int x=0;x<320;++x)if(x<8||x>=312)CHECK(pixels[y*320+x]==m::paper_color(y));
    }
    // Side margins never toggle/select/paint an edge cell.
    f.mode(0);f.touch(0,7);const auto inset_hash=f.hash();
    for(int x:{0,7,312,319}) {
        f.frame();sequencer_view_touch_down(x,layout::ROWS[1]+8);f.release();
        CHECK(f.hash()==inset_hash && sequencer_selected_chain()==0 && sequencer_selected_step()==7);
    }
    // End brackets are absent on EVERY page and for an in-page length of nine.
    f.mode(1);
    for(int page=0;page<4;++page) {
        for(int c=0;c<4;++c)f.engine->set_sequence_length(c,(page+1)*16);
        while(sequencer_current_page()!=page)f.tap(KEY_L|KEY_RIGHT);
        f.refresh();draw();
        for(int row:layout::ROWS)for(int y=row;y<row+layout::CELL_H;++y)
            CHECK(pixels[y*320+311]==m::paper_color(y));
    }
    f.tap(KEY_L|KEY_RIGHT);f.engine->set_sequence_length(0,9);f.refresh();draw();
    for(int y=layout::ROWS[0];y<layout::ROWS[0]+layout::CELL_H;++y)
        CHECK(pixels[y*320+layout::X0+8*layout::PITCH+layout::CELL_W]==m::paper_color(y));
    CHECK(pixels[(layout::ROWS[0]+21)*320+layout::X0+9*layout::PITCH+6]==m::branch());
}
// Independent fixture: legacy bank/copy gestures above must not prevent the
// actual scope and frozen renderer regression from being exercised on their own.
void test_sequence_scope_frozen() {
    Fixture f;f.frame();f.dense();
    for(int mode=0;mode<8;++mode) {f.mode(mode);check_dual_budget();CHECK(text_overlaps==0);}
    f.mode(1);
    // Real master-left output, not UI time: chronological ring points project
    // to native trace pixels, including ring wrap, polarity and full scale.
    auto scope_matches=[&](const std::vector<u32>& image) {
        int peak=0;
        for(const auto sample:g_ui_snapshot.scope)peak=std::max(peak,std::abs(int(sample)));
        const u32 ink=seq_wave::ink(m::ink(),m::spot(),seq_wave::accent_amount(unsigned(peak)));
        for(int i=0;i<Engine::SCOPE_LEN;++i) {
            const int x=i*319/(Engine::SCOPE_LEN-1);
            const int sample=g_ui_snapshot.scope[(g_ui_snapshot.scope_pos+i)%Engine::SCOPE_LEN];
            const int y=22-sample*17/32767;
            CHECK(y>=5 && y<=39);
            CHECK(image[y*320+x]==ink);
        }
        for(int y=0;y<43;++y)if(y<5||y>39)for(int x=0;x<320;++x)
            CHECK(image[y*320+x]==m::paper_color(y));
    };
    f.engine->scope_pos=173;
    for(int i=0;i<Engine::SCOPE_LEN;++i)f.engine->scope[i]=(q15)(i%3==0?-32768:i%3==1?32767:0);
    f.refresh();scope_matches(draw());check_dual_budget();
    f.engine->cancel_sequence_audition();
    for(int c=0;c<4;++c)f.engine->chains[c].mute=c!=3;
    auto& osc=*static_cast<OscUnit*>(f.engine->chains[3].units[0]);
    osc.params[OscUnit::P_PITCH].base=0;osc.params[OscUnit::P_GAIN].base=28000;
    std::array<q15,4096> rendered{};
    for(int block=0;block<4;++block)f.engine->render(rendered.data()+block*1024,512);
    f.refresh();
    for(int i=0;i<Engine::SCOPE_LEN;++i)
        CHECK(g_ui_snapshot.scope[(g_ui_snapshot.scope_pos+i)%Engine::SCOPE_LEN]
              ==rendered[(2048-Engine::SCOPE_LEN*8+i*8+7)*2]);
    const auto live0=draw();scope_matches(live0);
    const auto static_hash=f.hash();const auto tick=f.engine->sequence_tick;
    g_ui_frame+=641;CHECK(draw()==live0); // clock alone cannot fabricate motion
    CHECK(f.hash()==static_hash && f.engine->sequence_tick==tick);
    f.engine->render(rendered.data(),512); // not published yet: retain old trace
    CHECK(draw()==live0);
    f.refresh();const auto live1=draw();scope_matches(live1);
    CHECK(!std::equal(live0.begin(),live0.begin()+40*320,live1.begin()));
    const auto published=draw();g_ui_frame+=7;Engine* saved_engine=g_eng;g_eng=nullptr;
    std::fill(std::begin(g_ui_snapshot.scope),std::end(g_ui_snapshot.scope),0);
    width=320;pixels.assign(320*240,0xff000000);objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();
    sequencer_view_draw();CHECK(pixels==published);g_eng=saved_engine;
    // Real all-chain silence drains the ~50ms ring to an honest horizontal line.
    for(int c=0;c<4;++c)f.engine->chains[c].mute=true;
    for(int block=0;block<4;++block)f.engine->render(rendered.data(),512);
    f.refresh();const auto silence=draw();scope_matches(silence);
    for(int y=0;y<40;++y)for(int x=0;x<320;++x) {
        if(y==22)CHECK(silence[y*320+x]==m::ink());
        else CHECK(silence[y*320+x]==m::paper_color(y));
    }
    extent_test_open(f);
    const auto frozen=draw();
    f.engine->pattern.steps[0][0]={};f.engine->chains[0].units[0]->params[FmUnit::P_PITCH].base=321;
    f.engine->request_pattern(3,false);
    f.engine->chains[3].mute=false;f.engine->render(rendered.data(),512);
    f.refresh();g_ui_frame+=100;CHECK(draw()==frozen);
    Engine* engine=g_eng;g_eng=nullptr;
    width=320;pixels.assign(320*240,0xff000000);objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();
    sequencer_view_draw();CHECK(pixels==frozen);g_eng=engine;
    extent_test_close(f);CHECK(f.engine->active_pattern==3 && sequencer_edit_bank()==0);
}
void dump_sequence_extent(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");
    manifest<<"frame\tobjects\ttop_objects\tshared_objects\tclipped_primitives\ttop_clipped_primitives\ttext_overlaps\ttop_text_overlaps\n";
    Fixture f;f.frame();f.dense();
    // Seed a separate edit bank, retain real active data and no false edited-bank head.
    f.engine->sequence_pattern(2)=f.engine->pattern;
    delete f.engine->replace_unit(1,0,new NoiseUnit);f.refresh();
    f.mode(1);f.touch(0,4);
    // Actual low OSC output makes the live trace legible; no authored waveform.
    f.engine->chains[3].units[0]->params[OscUnit::P_PITCH].base=0;
    f.engine->chains[3].units[0]->params[OscUnit::P_GAIN].base=28000;
    std::array<q15,1024> audio{};
    for(int block=0;block<4;++block)f.engine->render(audio.data(),512);
    f.refresh();check_dual_budget();dump_frame(directory,"01_live_scope",manifest);
    extent_test_open(f);extent_hold_key(f,KEY_DOWN);extent_hold_key(f,KEY_DOWN);
    for(int i=0;i<32;++i)extent_hold_key(f,KEY_LEFT);
    extent_hold_key(f,KEY_A);extent_hold_key(f,KEY_A);
    check_dual_budget();dump_frame(directory,"02_bank_length_direction",manifest);
    extent_test_close(f);f.tap(KEY_L|KEY_RIGHT);f.touch(0,15);
    f.tap(KEY_START);for(int block=0;block<4;++block)f.engine->render(audio.data(),512);f.refresh();
    check_dual_budget();dump_frame(directory,"03_independent_bank_page",manifest);
    // Actual muted output: an honest flat trace, 16-step ends without brackets.
    extent_bank(f,0);f.tap(KEY_START);f.tap(KEY_L|KEY_LEFT);f.touch(0,4);
    for(int c=0;c<4;++c) { f.engine->set_sequence_length(c,16);f.engine->chains[c].mute=true; }
    for(int block=0;block<4;++block)f.engine->render(audio.data(),512);
    f.refresh();check_dual_budget();dump_frame(directory,"04_inset_silence",manifest);
}
