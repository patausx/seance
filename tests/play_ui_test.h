// Real PLAY controller/capture/draw; no concept renderer or exported gallery.
struct PlayUiFixture : Fixture {
    static constexpr int SAMPLE=61;
    PlayUnit* play=nullptr;trackr::synth::Sample saved;trackr::synth::SampleProvenance provenance;
    PlayUiFixture(){auto&bank=trackr::synth::SampleBank::instance();saved=std::move(bank.slot(SAMPLE));provenance=bank.provenance(SAMPLE);
        trackr::synth::Sample pcm;pcm.channels=2;pcm.data.resize(256002);
        for(int i=0;i<128001;++i){q15 v=q15(12000*std::exp(-std::fmod(double(i)/32000,.25)*19)*std::sin(i*.04));pcm.data[i*2]=v;pcm.data[i*2+1]=q15(-v/2);}
        // One-frame transient that old sparse probing could miss.
        pcm.data[33333*2]=32767;
        CHECK(bank.replace_sample(SAMPLE,std::move(pcm),trackr::synth::SampleOrigin::SdWav,"test/break_01.wav")==trackr::synth::SampleEditResult::Ok);
        play=new PlayUnit;play->sample_slot=SAMPLE;delete engine->replace_unit(0,0,play);
        for(int c=1;c<NUM_CHAINS;++c)engine->chains[c].mute=true;
        g_edit_chain=g_edit_slot=0;g_view=View::Chains;g_overlay=Overlay::Editor;param_lens_cancel();g_drag=Drag::None;capture();}
    ~PlayUiFixture(){g_seq_lock_armed=false;g_overlay=Overlay::None;play_focus_capture();auto&bank=trackr::synth::SampleBank::instance();bank.swap_sample_unchecked(SAMPLE,saved,provenance.origin,provenance.path,provenance.missing);}
    void capture(){engine->capture_ui_snapshot(g_ui_snapshot,0,0);play_focus_capture();param_lens_capture();}
    void warm(){for(int i=0;i<18;++i)capture();}
    void touch(int x,int y){CHECK(play_focus_touch_body(*play,x,y));}
    void drag(int x,int y){play_focus_drag(*play,x,y);}
    void up(){play_focus_touch_release(*play);g_drag=Drag::None;}
    void tap(int x,int y){touch(x,y);up();}
    void snap(){for(int p=0;p<play->num_params;++p)play->params[p].snap();play->control_tick();}
    void trim(){tap(g_seq_lock_armed?65:25,g_seq_lock_armed?20:72);}
    void cuts(){tap(90,72);}
    void menu_row(int row){
        // Return the flat list to its top without activating the touched rows.
        for(int i=0;i<3;++i){touch(180,96);drag(180,192);up();}
        while(row>4){int rows=std::min(4,row-4);touch(180,192);drag(180,192-24*rows);up();row-=rows;}
        tap(40,98+24*row);
    }
    std::string snap_text(){for(const auto&t:texts)if(t.text.rfind("snap:",0)==0)return t.text;return {}; }
    void edges(int a,int len){play->params[PlayUnit::P_START].base=a;play->params[PlayUnit::P_LEN].base=len;snap();capture();}
    std::array<q15,PlayUnit::P_COUNT> bases(){std::array<q15,PlayUnit::P_COUNT>a{};for(int p=0;p<PlayUnit::P_COUNT;++p)a[p]=play->params[p].base;return a;}
    void geometry(){
        if(outside_rects||outside_lines||text_overlaps)std::fprintf(stderr,"play geometry: rect %d line %d text %d\n",outside_rects,outside_lines,text_overlaps);
        CHECK(objects>0&&objects<=8192&&outside_rects==0&&outside_lines==0&&text_overlaps==0);
        for(const auto&t:texts)CHECK(t.x>=0&&t.y>=0&&t.x+t.w<=width&&t.y+t.h<=HEIGHT);
        for(int y=0;y<HEIGHT;++y)for(int x=0;x<width;++x){u32 p=pixels[y*width+x];CHECK(p==menu_style::paper_color(y)||p==menu_style::ink()||p==menu_style::spot()||p==menu_style::ghost()||p==menu_style::branch()||p==col_cell()||p==menu_style::paper_color(191));}
    }
    void lower(){module_inspector_surface(false);ModuleInspectorNoEngine frozen;play_focus_draw(*play);geometry();}
    int pair(){capture();module_inspector_surface(true);{ModuleInspectorNoEngine frozen;top_draw();}geometry();int top=objects;lower();CHECK(top+objects<=8192);maximum_top_objects=std::max(maximum_top_objects,top);maximum_objects=std::max(maximum_objects,objects);maximum_shared_objects=std::max(maximum_shared_objects,top+objects);return top+objects;}
    void more(){tap(280,20);}
};
void test_play_ui_controls(){
    PlayUiFixture f;auto&u=*f.play;f.edges(8192,16384);const auto bases=f.bases();const uint32_t first=u.projected_window(bases[1],bases[2],0).start;
    f.touch(30,176);f.drag(30,176);f.up();CHECK(!u.edit_state().enabled&&f.bases()==bases);
    f.tap(165,214);CHECK(u.edit_state().enabled&&u.edit_state().start==first+1&&f.bases()==bases);
    f.tap(110,214);CHECK(u.edit_state().start==first);
    const auto original=u.edit_state();f.touch(26,176);f.drag(50,176);f.up();CHECK(u.edit_state().start>original.start&&u.edit_state().end==original.end);
    f.tap(220,72);CHECK(play_edit_equal(u.edit_state(),original));f.tap(280,72);CHECK(u.edit_state().start>original.start);
    f.trim();f.more();f.menu_row(0);
    f.touch(180,176);f.drag(195,176);f.up();CHECK(u.edit_state().fade_in>0);
    const auto faded=u.edit_state();const int clean=mutations;
    // Interior drag and descriptive tool text are inert, not hidden move/snap controls.
    f.touch(170,128);f.drag(180,128);f.up();CHECK(play_edit_equal(u.edit_state(),faded)&&mutations==clean);
    f.warm();f.lower();const auto snap_line=f.snap_text();CHECK(!snap_line.empty());
    f.touch(160,94);f.drag(180,94);f.up();f.capture();f.lower();CHECK(f.snap_text()==snap_line&&play_edit_equal(u.edit_state(),faded));
    const int edge_x=12+int(uint64_t(faded.start)*296/128001);
    f.touch(edge_x,128);f.drag(edge_x+5,128);f.up();CHECK(u.edit_state().start>faded.start&&u.edit_state().end==faded.end);
    f.tap(220,72);CHECK(play_edit_equal(u.edit_state(),faded));
    // Only the explicit menu tool enables whole-window movement.
    f.more();f.menu_row(11);f.touch(170,128);f.drag(180,128);f.up();CHECK(u.edit_state().start>faded.start&&u.edit_state().end-u.edit_state().start==faded.end-faded.start);
    f.tap(220,72);CHECK(play_edit_equal(u.edit_state(),faded));
    // Slider tap is not fit; pan changes only viewport and keeps redo/snap intact.
    f.tap(300,94);f.warm();f.lower();const auto viewport=pixels;const auto edit=u.edit_state();const int pan_clean=mutations;
    f.tap(280,196);f.warm();f.lower();CHECK(pixels==viewport);
    f.touch(170,196);f.drag(205,196);f.up();f.warm();f.lower();CHECK(pixels!=viewport&&f.snap_text()==snap_line);
    CHECK(play_edit_equal(u.edit_state(),edit)&&f.bases()==bases&&mutations==pan_clean);
    f.tap(280,72);CHECK(u.edit_state().start>faded.start);f.tap(220,72);CHECK(play_edit_equal(u.edit_state(),faded));
    f.tap(220,46);f.tap(40,214);f.touch(20,176);f.drag(30,176);f.up();CHECK(u.edit_state().loop_start>u.edit_state().start);
    f.touch(243,176);f.drag(250,176);f.up();CHECK(u.edit_state().crossfade>0);
    // Source repeat renders fades every lap; musical/seam sustain is separate.
    {PlayUnit preview;preview.sample_slot=61;auto e=u.edit_state();e.start=e.loop_start=0;e.end=e.loop_end=512;e.fade_in=e.fade_out=64;e.crossfade=32;
        CHECK(preview.set_edit_state(e,trackr::synth::SampleBank::instance().generation(61)));
        CHECK(preview.preview_frames(0,512,true,trackr::synth::SampleBank::instance().generation(61)));q15 loop[1536];preview.process(loop,1536);
        CHECK(loop[511]==0&&loop[512]==0&&loop[1023]==0&&loop[1024]==0);
        CHECK(preview.preview_seek(490,true));CHECK(preview.visual_source_frame()==490);CHECK(!preview.preview_seek(512));}
    // Shared cuts: explicit auto preview, stable IDs, wave taps never drag seams.
    f.cuts();for(int i=0;i<3;++i)f.tap(220,94);f.tap(90,94);auto&sample=trackr::synth::SampleBank::instance().slot(61);
    f.tap(110,98);CHECK(sample.chops[0]==UINT32_MAX);f.tap(250,203);CHECK(sample.chops[3]!=UINT32_MAX&&sample.chops[4]==UINT32_MAX);
    const uint32_t seam=sample.chops[1];f.touch(100,128);f.drag(120,128);f.up();CHECK(sample.chops[1]==seam);
    f.touch(86,176);f.drag(92,176);f.up();CHECK(sample.chops[1]>seam&&sample.chops[2]==uint32_t(uint64_t(2)*128001/4));
    f.tap(170,214);CHECK(sample.slice_reverse_mask&(1u<<1));
    f.tap(265,214);CHECK(sample.chops[1]==UINT32_MAX&&sample.chops[2]!=UINT32_MAX);
    f.tap(220,128); // select original cut 03, then leave cuts without losing selection
    const uint32_t copy_frames=sample.chops[3]-sample.chops[2];
    char temp[]="/tmp/seance-play-selection-XXXXXX";CHECK(mkdtemp(temp));const char*old_dir=RECORDING_DIR;RECORDING_DIR=temp;
    f.more();f.menu_row(4);RECORDING_DIR=old_dir;
    auto&bank=trackr::synth::SampleBank::instance();int copied=-1;
    for(int s=0;s<trackr::synth::SAMPLE_BANK_SIZE;++s)if(std::strstr(bank.provenance(s).path,temp)){copied=s;CHECK(bank.slot(s).playable_frames()==copy_frames);}
    CHECK(copied>=0&&u.sample_slot==61);bank.clear_slot(copied);std::filesystem::remove_all(temp);
    f.more();auto snapshot=f.hash();f.tap(257,232);CHECK(g_view==View::Sequencer&&g_overlay==Overlay::None&&f.hash()==snapshot);
    sequencer_view_buttons(0,0,0);sequencer_view_touch_down(130,200);CHECK(g_view==View::Chains&&g_overlay==Overlay::Editor);CHECK(play_focus_back(u)&&g_view==View::Sequencer);
}
void test_play_ui_locks(){
    PlayUiFixture f;auto&u=*f.play;f.edges(10000,12000);const auto bases=f.bases();
    g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=7;g_view=View::Sequencer;f.trim();
    for(int i=0;i<3;++i)CHECK(f.engine->set_step_lock(0,7,0,i==0?PlayUnit::P_SPEED:i==1?PlayUnit::P_GAIN:PlayUnit::P_LOOP,8000));
    const auto before=f.hash();f.touch(120,90);f.drag(150,90);f.up();CHECK(f.hash()==before&&f.bases()==bases&&g_seq_lock_flash>0);
    f.engine->clear_step_locks(0,7);f.touch(120,90);f.drag(150,90);f.up();CHECK(f.engine->pattern.steps[0][7].lock_count==2&&f.bases()==bases);
    CHECK(seq_display_value(0,PlayUnit::P_START,0)+seq_display_value(0,PlayUnit::P_LEN,0)==22000);
    const auto locked=f.hash();f.touch(258,10);f.touch(100,231);f.touch(289,34);CHECK(!u.bypass&&!u.pitch_quantize&&f.hash()==locked);
    g_seq_lock_armed=false;g_view=View::Chains;f.capture();u.set_modern(false);f.capture();f.tap(165,214);CHECK(!u.modern()&&!u.edit_state().enabled);
    f.tap(220,46);f.tap(30,98);CHECK(u.modern());
    f.trim();f.touch(25,176);const auto stored=f.bases();auto&bank=trackr::synth::SampleBank::instance();
    trackr::synth::Sample replacement;replacement.data.assign(32000,1000);CHECK(bank.replace_sample(61,std::move(replacement),trackr::synth::SampleOrigin::Runtime)==trackr::synth::SampleEditResult::Ok);
    f.drag(200,176);CHECK(f.bases()==stored&&!u.edit_state().enabled);f.up();
    // A same-length replacement cannot be opted in via stale nudge/undo state.
    f.capture();f.tap(165,214);CHECK(u.edit_state().enabled);const auto exact=u.edit_state();
    trackr::synth::Sample same_length;same_length.data.assign(32000,2000);CHECK(bank.replace_sample(61,std::move(same_length),trackr::synth::SampleOrigin::Runtime)==trackr::synth::SampleEditResult::Ok);
    f.capture();f.tap(165,214);f.tap(220,72);CHECK(!u.edit_source_matches()&&play_edit_equal(u.edit_state(),exact));
}
void test_play_ui_preview_lens(){
    PlayUiFixture f;auto&u=*f.play;f.edges(8192,8192);const auto saved=f.bases();const auto pattern=f.hash();const int dirty=mutations;
    // Independent source preview neither starts nor stops the actual PLAY voice.
    f.engine->transport_playing=true;f.tap(30,232);CHECK(!u.playback_active());std::array<q15,2048> out{};f.engine->render(out.data(),1024);
    CHECK(std::any_of(out.begin(),out.end(),[](q15 v){return v!=0;}));CHECK(f.bases()==saved&&f.hash()==pattern&&mutations==dirty);
    f.tap(115,232);f.engine->render(out.data(),1024);CHECK(u.params[PlayUnit::P_LOOP].base==saved[3]);
    f.tap(30,232);f.engine->render(out.data(),1024);f.capture();f.lower();CHECK(has_text("listen")&&!u.playback_active());
    f.engine->transport_playing=false;f.more();f.tap(160,232); // explicit chain route, same flat surface
    f.tap(30,232);CHECK(u.playback_active());f.tap(30,232);f.engine->render(out.data(),128);CHECK(!u.playback_active());
    f.tap(30,232);CHECK(u.playback_active());f.tap(160,232);f.engine->render(out.data(),128);CHECK(!u.playback_active()); // route switch stops old chain audition
    // Direct speed/gain remain relative and selectable, including while the menu is open.
    for(auto control:{std::pair<int,int>{45,PlayUnit::P_SPEED},{150,PlayUnit::P_GAIN}}){
        const auto before=f.bases();const int clean=mutations;
        f.touch(control.first,46);CHECK(f.bases()==before);f.drag(control.first,46);f.up();CHECK(f.bases()==before&&mutations==clean&&play_focus_selected()==control.second);
        f.touch(control.first,46);f.drag(control.first-9,46);f.up();CHECK(u.params[control.second].base<before[control.second]);
        unsigned down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());f.capture();
        CHECK(play_focus_selected()==control.second);param_lens_input(down,0,KEY_L,false);param_lens_cancel();
        f.tap(220,72);CHECK(f.bases()==before);
    }
    CHECK(!param_lens_touch_down(260,232));
    // Source is one direct header tap from both waveform and menu; browser itself has its own harness.
    for(int surface=0;surface<2;++surface){if(surface)f.more();else f.trim();const auto before=f.bases();const int scans=gran_browser_scans;
        gran_browser_enabled=true;f.touch(160,20);CHECK(g_overlay==Overlay::SampleBrowser&&gran_browser_scans==scans+1&&u.sample_slot==61&&f.bases()==before);
        f.up();gran_browser_enabled=false;g_overlay=Overlay::Editor;f.capture();}
    f.cuts();f.tap(145,94);f.engine->render(out.data(),1024);
    unsigned down=KEY_A;CHECK(play_editor_button(down));auto&sample=trackr::synth::SampleBank::instance().slot(61);CHECK(sample.chops[1]>0&&sample.chops[1]<128001);
    CHECK(f.hash()==pattern);
}
void test_play_finish_ui(){
    PlayUiFixture f;auto&u=*f.play;auto&bank=trackr::synth::SampleBank::instance();auto&s=bank.slot(61);
    s.chops[0]=0;s.chops[3]=32000;s.chops[7]=64000;f.capture();f.cuts();f.tap(110,128);
    const auto base=f.bases();f.touch(50,46);f.drag(50,46);f.up();CHECK(s.slice_speed_q8[3]==256&&f.bases()==base);
    f.touch(50,46);f.drag(114,46);f.up();CHECK(s.slice_speed_q8[3]==512&&s.slice_speed_q8[7]==256&&f.bases()==base);
    f.tap(220,72);CHECK(s.slice_speed_q8[3]==256);f.tap(280,72);CHECK(s.slice_speed_q8[3]==512);
    f.trim();f.edges(4096,16384);f.tap(165,214);const auto original=u.edit_state();const auto original_bases=f.bases();
    const auto source_pcm=s.data;const auto pattern=f.hash();
    auto*alias=new PlayUnit;alias->sample_slot=61;delete f.engine->replace_unit(1,0,alias);
    char temp[]="/tmp/seance-play-cut-XXXXXX";CHECK(mkdtemp(temp));const char*old_dir=RECORDING_DIR;RECORDING_DIR=temp;
    f.touch(180,232);f.drag(200,232);f.up();CHECK(u.sample_slot==61&&std::filesystem::is_empty(temp));
    f.touch(180,232);play_focus_release();f.up();CHECK(u.sample_slot==61&&std::filesystem::is_empty(temp));
    f.tap(180,232);const int cropped=u.sample_slot;CHECK(cropped!=61&&alias->sample_slot==61&&s.data==source_pcm);
    CHECK(bank.slot(cropped).playable_frames()==original.end-original.start&&u.edit_state().start==0&&f.hash()==pattern);
    CHECK(bank.slot(cropped).chops[0]==0&&bank.slot(cropped).chops[3]==32000-original.start&&bank.slot(cropped).slice_speed_q8[3]==512);
    f.tap(220,72);CHECK(u.sample_slot==61&&f.bases()==original_bases&&play_edit_equal(u.edit_state(),original));
    f.tap(280,72);CHECK(u.sample_slot==cropped&&u.edit_source_matches());
    // Local edits undo before the source switch; redo retains the saved short WAV.
    const auto cropped_edit=u.edit_state();f.tap(165,214);f.tap(220,72);CHECK(u.sample_slot==cropped&&play_edit_equal(u.edit_state(),cropped_edit));
    f.tap(220,72);CHECK(u.sample_slot==61);f.tap(280,72);CHECK(u.sample_slot==cropped);
    f.tap(220,72); // back on original before testing private scratch
    bank.slot(cropped).slice_speed_q8[3]=256;f.tap(280,72);CHECK(u.sample_slot==61); // stale destination cannot be adopted by redo
    f.tap(150,72);const auto before=f.bases();const int clean=mutations;std::array<q15,4096> out{};
    f.touch(100,128);f.engine->render(out.data(),2048);CHECK(std::all_of(out.begin(),out.end(),[](q15 v){return v==0;}));
    f.drag(155,128);f.engine->render(out.data(),2048);
    CHECK(std::any_of(out.begin(),out.end(),[](q15 v){return v!=0;}));
    CHECK(f.bases()==before&&mutations==clean&&!u.playback_active()&&f.hash()==pattern);
    f.up();f.engine->render(out.data(),128);f.capture();f.lower();CHECK(has_text("listen"));
    f.touch(50,176);f.drag(80,176);f.up();CHECK(u.params[PlayUnit::P_POSITION].base>before[PlayUnit::P_POSITION]);
    CHECK(play_focus_selected()==PlayUnit::P_POSITION);unsigned down=KEY_L;
    CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());f.capture();
    CHECK(param_lens_touch_down(53,185));CHECK(u.params[PlayUnit::P_POSITION].src==ModSource::SigilA); // off / a-h / env: a is the second 31px cell
    CHECK(param_lens_touch_down(130,155));CHECK(param_lens_touch_drag(170,155));CHECK(u.params[PlayUnit::P_POSITION].mod_depth>0);
    param_lens_input(down,0,KEY_L,false);param_lens_cancel();f.capture();
    f.tap(45,94);CHECK(u.params[PlayUnit::P_SCRUB].base>16383&&u.params[PlayUnit::P_LOOP].base==before[3]);
    f.tap(220,72);CHECK(u.params[PlayUnit::P_SCRUB].base==0);
    f.warm();f.pair();RECORDING_DIR=old_dir;bank.clear_slot(cropped);std::filesystem::remove_all(temp);
}
void test_play_hold_ui(){
    PlayUiFixture f;auto&u=*f.play;f.tap(150,72);f.warm();f.pair();
    CHECK(has_text("scratch")&&u.params[PlayUnit::P_HOLD].base==0);
    const auto original=f.bases();const auto pattern=f.hash();
    f.tap(153,94);CHECK(u.params[PlayUnit::P_HOLD].base==32767&&play_focus_selected()==PlayUnit::P_HOLD);
    CHECK(u.params[PlayUnit::P_SCRUB].base==0&&!u.playback_active());
    auto held=f.bases();held[PlayUnit::P_HOLD]=0;CHECK(held==original);
    f.tap(220,72);CHECK(f.bases()==original);f.tap(280,72);CHECK(u.params[PlayUnit::P_HOLD].base==32767);
    unsigned down=KEY_L;CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());
    CHECK(play_focus_selected()==PlayUnit::P_HOLD);param_lens_input(down,0,KEY_L,false);param_lens_cancel();f.capture();
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){palette::set(theme);f.pair();CHECK(has_text("hold 50ms"));}
    palette::set(palette::Theme::Pink);
    std::array<q15,4096> out{};const auto before=f.bases();const int clean=mutations;
    const auto audible=[&]{return std::any_of(out.begin(),out.end(),[](q15 v){return v!=0;});};
    const auto render=[&]{f.engine->render(out.data(),2048);};
    f.touch(100,128);render();CHECK(audible());render();CHECK(audible());
    const auto stationary=out;f.drag(190,128);render();CHECK(audible()&&out!=stationary);
    CHECK(f.bases()==before&&f.hash()==pattern&&mutations==clean&&!u.playback_active());
    f.up();render();render();CHECK(!audible());f.capture();f.lower();CHECK(has_text("listen"));
    // Listen also sustains without pointer motion; live style change keeps ownership.
    f.tap(30,232);render();render();CHECK(audible());
    f.tap(153,94);render();render();CHECK(!audible()&&u.params[PlayUnit::P_HOLD].base==0);
    f.tap(153,94);render();render();CHECK(audible());f.tap(30,232);render();render();CHECK(!audible());
    // Lost gesture ownership and explicit cancellation release a sustaining preview.
    f.touch(100,128);render();CHECK(audible());g_drag=Drag::None;f.drag(100,128);render();render();CHECK(!audible());
    f.touch(100,128);render();CHECK(audible());play_focus_release();render();render();CHECK(!audible());
    // Shared lock surface treats style as discrete, never a percentage/offset.
    using namespace seance::platform::ui3ds::seq_lock;
    char text[32];format_value(UnitKind::SamplePlayer,1,PlayUnit::P_HOLD,0,text,sizeof text);CHECK(std::string(text)=="scratch");
    format_value(UnitKind::SamplePlayer,1,PlayUnit::P_HOLD,32767,text,sizeof text);CHECK(std::string(text)=="hold");
    CHECK(!supports_offset(UnitKind::SamplePlayer,1,PlayUnit::P_HOLD));
    CHECK(nudge_value(UnitKind::SamplePlayer,1,PlayUnit::P_HOLD,0,1,false)==32767);
    CHECK(nudge_value(UnitKind::SamplePlayer,1,PlayUnit::P_HOLD,32767,-1,false)==0);
    CHECK(f.engine->set_step_lock(0,7,0,PlayUnit::P_HOLD,32767));
    CHECK(f.engine->pattern.steps[0][7].locks[0].param==PlayUnit::P_HOLD);
    // Legacy mode cannot author hidden style by tapping its disabled control.
    u.set_modern(false);f.capture();f.tap(150,72);const auto legacy=f.bases();f.tap(153,94);CHECK(f.bases()==legacy&&!u.modern());
}
void test_play_ui_frames(){
    PlayUiFixture f;auto&u=*f.play;
    // One nominal speed law everywhere, including legacy, qnt and the retained
    // negative-near-zero floor. Rendering must use captured mode, not a live unit.
    char expected[24],text[24];int readouts=0;
    for(bool modern:{false,true})for(bool quantize:{false,true}){
        for(int raw:{-32768,-32767,-8192,-128,-127,-1,0,1,127,128,8192,12000,32767}){
            const auto w=play_window(2,nullptr,0,q15(raw),0,32767,0,quantize,modern);
            std::snprintf(expected,sizeof expected,"%+.2fx",double(w.increment)/65536.0);
            play_readout::value_text(text,sizeof text,PlayUnit::P_SPEED,q15(raw),modern,quantize);CHECK(std::string(text)==expected);
            module_inspector_value_text(text,sizeof text,UnitKind::SamplePlayer,PlayUnit::P_SPEED,q15(raw),true,false,true,false,modern,quantize);CHECK(std::string(text)==expected);
            seq_lock::format_value(UnitKind::SamplePlayer,uint8_t(modern|(quantize<<1)),PlayUnit::P_SPEED,q15(raw),text,sizeof text);CHECK(std::string(text)==expected);++readouts;
        }
        u.set_modern(modern);u.pitch_quantize=quantize;u.params[PlayUnit::P_SPEED].base=12000;f.capture();f.lower();
        play_readout::value_text(expected,sizeof expected,PlayUnit::P_SPEED,12000,modern,quantize);CHECK(has_text(expected));
        u.pitch_quantize=!quantize;f.lower();CHECK(has_text(expected));u.pitch_quantize=quantize;
        play_focus_select(PlayUnit::P_SPEED);unsigned down=KEY_L;
        CHECK(param_lens_input(down,KEY_L,0,false)&&param_lens_is_open());f.capture();
        module_inspector_surface(false);{ModuleInspectorNoEngine frozen;param_lens_draw_bottom();}f.geometry();CHECK(has_text(std::string("base ")+expected));
        down=0;param_lens_input(down,0,KEY_L,false);CHECK(!param_lens_is_open());
        CHECK(f.engine->set_step_lock(0,0,0,PlayUnit::P_SPEED,-12000));
        g_seq_lock_armed=true;g_seq_lock_chain=0;g_seq_lock_step=0;g_view=View::Sequencer;f.capture();f.lower();
        seq_lock::format_value(UnitKind::SamplePlayer,seq_lock::variant(u),PlayUnit::P_SPEED,-12000,expected,sizeof expected);CHECK(has_text(expected));
        play_focus_value_text(text,sizeof text,PlayUnit::P_SPEED,-12000);CHECK(std::string(text)==expected);
        g_seq_lock_armed=false;g_view=View::Chains;f.engine->clear_step_locks(0,0);
    }
    play_readout::value_text(text,sizeof text,PlayUnit::P_SPEED,-1,false,false);CHECK(std::string(text)=="+0.02x");
    play_readout::value_text(text,sizeof text,PlayUnit::P_SPEED,-1,true,true);CHECK(std::string(text)=="-0.02x");
    play_readout::value_text(text,sizeof text,PlayUnit::P_MODE,32767,false,true);CHECK(std::string(text)=="--");
    play_readout::value_text(text,sizeof text,-1,0,true,false);CHECK(std::string(text)=="--");
    char tiny[2]={'?','!'};play_readout::value_text(tiny,0,PlayUnit::P_SPEED,8192,true,true);CHECK(tiny[0]=='?'&&tiny[1]=='!');
    play_readout::value_text(tiny,1,PlayUnit::P_SPEED,8192,true,true);CHECK(tiny[0]==0&&tiny[1]=='!');
    play_readout::value_text(nullptr,24,PlayUnit::P_SPEED,8192,true,true);
    u.set_modern(true);u.pitch_quantize=false;u.params[PlayUnit::P_SPEED].base=8192;
    std::printf("play readouts: %d scalar cases / native + held mod + lock / frozen qnt\n",readouts);
    f.warm();int pairs=0;
    for(auto theme:{palette::Theme::Pink,palette::Theme::Graphite}){palette::set(theme);
        for(int page=0;page<7;++page){f.trim();
            if(page==1)f.cuts();else if(page==2)f.tap(160,72);else if(page==3)f.more();
            else if(page==4)f.tap(220,46);else if(page==5){f.cuts();f.tap(90,94);}else if(page==6){f.tap(220,46);f.tap(30,214);}
            f.warm();CHECK(f.pair()<=8192);const auto bottom=pixels;int count=objects;const auto saved=f.bases();
            for(int p=0;p<PlayUnit::P_COUNT;++p)u.params[p].base=12345;
            auto&pcm=trackr::synth::SampleBank::instance().slot(61);std::vector<q15>held;held.swap(pcm.data);
            f.lower();CHECK(bottom==pixels&&objects==count);held.swap(pcm.data);for(int p=0;p<PlayUnit::P_COUNT;++p)u.params[p].base=saved[p];++pairs;}}
    palette::set(palette::Theme::Pink);f.more();f.menu_row(14);CHECK(f.pair()<=8192);
    f.cuts();f.tap(90,94);f.tap(250,98);CHECK(f.pair()<=8192);
    g_overlay=Overlay::None;play_focus_capture();CHECK(!play_focus_draw_top());
    std::printf("play editor: %d frozen duals / both themes / no render exports\n",pairs);
}
void dump_play_ui_runtime(const std::filesystem::path&){throw std::runtime_error("old PLAY dump recipe retired; use explicit new editor capture task");}
