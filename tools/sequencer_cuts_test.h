// New shared editor entered through the actual sequencer. Historical old-panel
// fixture preserved in the before-play-editor backup; IDs/safety remain covered.
namespace cuts_cases {
using slice_cases::CHAIN;using slice_cases::SLOT;using slice_cases::center;
struct Setup:slice_cases::Setup{
    Setup(int n=4):slice_cases::Setup(n){open();}
    ~Setup(){g_overlay=Overlay::None;play_editor_capture();}
    void open(){control(135,200);CHECK(g_view==View::Chains&&g_overlay==Overlay::Editor);play_editor_capture();}
    void point(int x,int y){CHECK(play_editor_touch(play,x,y));play_editor_release();g_drag=Drag::None;}
    int x(uint32_t at)const{return 12+int(uint64_t(at)*296/pcm.sample.playable_frames());}
    uint32_t at(int x)const{return uint32_t(uint64_t(x-12)*pcm.sample.playable_frames()/296);}
    void select(int id){uint32_t first=pcm.sample.chops[id-1],end=pcm.sample.playable_frames();for(auto p:pcm.sample.chops)if(p>first&&p<end)end=p;point(x(first+(end-first)/2),120);}
    void add(int pixel){point(25,94);point(pixel,120);}
    void down(int pixel){CHECK(play_editor_touch(play,pixel,176));}
    void drag(int pixel){play_editor_drag(play,pixel,176);}
    void lift(){play_editor_release();g_drag=Drag::None;}
    void automatic(int n){point(85,94);point(n==4?100:n==8?200:280,98);}
    void swipe(int from,int to){CHECK(play_editor_touch(play,180,from));play_editor_drag(play,180,to);lift();}
    std::vector<u32> surface(bool top){play_editor_capture();width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);
        if(top)CHECK(play_editor_top());else play_editor_draw();
        if(outside_rects||outside_lines||text_overlaps)std::fprintf(stderr,"cuts geometry top%d rect%d line%d text%d\n",top,outside_rects,outside_lines,text_overlaps);
        CHECK(objects>0&&objects<8192&&outside_rects==0&&outside_lines==0&&text_overlaps==0);return pixels;}
};
}
void test_cuts_navigation(){using namespace cuts_cases;Setup f;f.stored(3);auto before=f.hash();const int dirty=mutations;
    f.select(2);CHECK(!f.play.params[PlayUnit::P_SLICE].audition_override&&f.hash()==before&&mutations==dirty);
    CHECK(play_editor_back());CHECK(g_view==View::Sequencer&&g_overlay==Overlay::None&&sequencer_selected_step()==0);
    f.open();f.point(230,46);f.point(30,190);CHECK(f.play.bypass);play_editor_capture();CHECK(play_editor_top());f.point(30,190);CHECK(!f.play.bypass);
    CHECK(play_editor_back());f.point(250,232);CHECK(g_view==View::Sequencer&&f.hash()==before);
}
void test_cuts_context_keys(){using namespace cuts_cases;Setup f;
    CHECK(play_editor_back());f.touch(CHAIN,5);f.stored(3,5);f.stored(2,6);
    CHECK(f.engine->set_step_lock(CHAIN,5,SLOT,PlayUnit::P_GAIN,12345));
    const auto active=f.hash(),pcm=f.sample_hash();
    f.engine->sequence_pattern(2)=f.engine->pattern;
    // Edit bank 2 while bank 0 remains active, through the real held-B chooser.
    f.frame(KEY_B,KEY_B);for(int i=0;i<15;++i)f.frame(0,KEY_B);
    for(int i=0;i<2;++i){f.frame(KEY_DOWN,KEY_B|KEY_DOWN);f.frame(0,KEY_B,KEY_DOWN);}
    f.frame(0,0,KEY_B);f.frame();f.open();
    CHECK(sequencer_edit_bank()==2&&sequencer_selected_step()==5&&f.engine->active_pattern==0);
    const int clean=mutations,exports=rec_export_calls;
    for(u32 key:{KEY_X,KEY_Y,KEY_R}){
        CHECK(play_editor_context_keys(key,key)==key);
        CHECK(play_editor_context_keys(0,key)==0); // held is not a second edge
        CHECK(f.hash()==active&&pattern_hash(f.engine->sequence_pattern(2))==active);
        CHECK(f.sample_hash()==pcm&&mutations==clean&&rec_export_calls==exports);
        CHECK(f.engine->chains[CHAIN].units[SLOT]==&f.play&&g_view==View::Chains&&g_overlay==Overlay::Editor);
    }
    const u32 unowned=KEY_A|KEY_B|KEY_SELECT|KEY_L|KEY_TOUCH;
    CHECK(play_editor_context_keys(unowned,unowned)==0);
    // A load leaves SEQ disabled: START must enable the score, not just its clock.
    f.engine->transport_stop();f.engine->set_sequencer_enabled(false);
    CHECK(play_editor_context_keys(KEY_START,KEY_START)==KEY_START);
    CHECK(f.engine->transport_playing&&f.engine->sequencer_enabled&&g_overlay==Overlay::Editor);
    CHECK(play_editor_context_keys(0,KEY_START)==0&&f.engine->transport_playing);
    CHECK(play_editor_context_keys(KEY_START,KEY_START)==KEY_START&&!f.engine->transport_playing);
    CHECK(mutations==clean&&f.hash()==active&&pattern_hash(f.engine->sequence_pattern(2))==active);
    // Only this step's SLCE in the edit bank is removed. Gain, adjacent step,
    // other chains and the active bank remain byte-independent of this action.
    auto expected=f.engine->sequence_pattern(2);auto&event=expected.steps[CHAIN][5];
    CHECK(event.lock_count==2&&event.locks[0].param==PlayUnit::P_SLICE);
    event.locks[0]=event.locks[1];event.locks[1]={};event.lock_count=1;
    u32 down=KEY_L|KEY_X;
    const u32 owned=play_editor_context_keys(down,down);down&=~owned;
    CHECK(owned==KEY_X&&down==KEY_L);
    // Exact main short-circuit/order is separately extracted in test_nav_instant.py.
    const bool skip=owned!=0||param_lens_input(down,KEY_L|KEY_X,0,false);
    CHECK(skip&&!param_lens_is_open()&&mutations==clean+1);
    CHECK(f.hash()==active&&pattern_hash(f.engine->sequence_pattern(2))==pattern_hash(expected));
    CHECK(play_editor_context_keys(KEY_X,KEY_L|KEY_X)==KEY_X&&mutations==clean+1);
    CHECK(play_editor_context_keys(0,KEY_L)==0);lens_frame(0,KEY_L);
    CHECK(!param_lens_is_open());lens_frame(0,0,KEY_L);
    // An already-open held-MOD panel keeps X ownership; no slice-lock deletion.
    f.engine->sequence_pattern(2)=f.engine->pattern;lens_quick_begin();
    CHECK(play_editor_context_keys(KEY_X,KEY_L|KEY_X)==0);
    down=KEY_X;param_lens_input(down,KEY_L|KEY_X,0,false);
    CHECK(!(down&KEY_X)&&param_lens_is_open()&&pattern_hash(f.engine->sequence_pattern(2))==active);
    lens_frame(0,0,KEY_L);CHECK(!param_lens_is_open());
    const u32 keys=KEY_X|KEY_Y|KEY_R|KEY_START;
    for(Overlay overlay:{Overlay::SampleBrowser,Overlay::PatchBrowser,Overlay::ConfirmDiscard}){
        g_overlay=overlay;CHECK(play_editor_context_keys(keys,keys|KEY_L)==0);
    }
    g_overlay=Overlay::Editor;g_seq_lock_armed=true;
    CHECK(play_editor_context_keys(keys,keys|KEY_L)==0);g_seq_lock_armed=false;
    CHECK(play_editor_back());CHECK(play_editor_context_keys(keys,keys)==0);
    play_editor_open_cuts(CHAIN,SLOT,false); // ordinary PLAY is not seq-origin
    CHECK(play_editor_context_keys(keys,keys|KEY_L)==0);
    CHECK(f.hash()==active&&f.sample_hash()==pcm&&mutations==clean+1&&rec_export_calls==exports);
}
void test_cuts_insert(){using namespace cuts_cases;Setup f(0);auto&s=f.pcm.sample;s.chops[0]=16000;s.chops[3]=0;s.chops[8]=24000;s.slice_reverse_mask=257;
    play_editor_capture();auto before=f.hash();f.add(90);CHECK(s.chops[1]==f.at(90)&&s.chops[0]==16000&&s.chops[3]==0&&s.chops[8]==24000&&s.slice_reverse_mask==257);
    auto hash=f.sample_hash();f.add(90);CHECK(f.sample_hash()==hash);f.set_cuts(16);play_editor_capture();hash=f.sample_hash();f.add(80);CHECK(f.sample_hash()==hash&&f.hash()==before);
}
void test_cuts_drag(){using namespace cuts_cases;Setup f;auto&s=f.pcm.sample;auto before=f.hash();
    f.down(f.x(8000));f.drag(f.x(8000));CHECK(s.chops[1]==8000);f.drag(-100);CHECK(s.chops[1]==2);f.drag(700);CHECK(s.chops[1]==15998);f.lift();CHECK(f.hash()==before);
    f.down(f.x(s.chops[1]));g_drag=Drag::None;auto hash=f.sample_hash();f.drag(150);CHECK(f.sample_hash()==hash);f.lift();
    f.down(f.x(s.chops[1]));trackr::synth::Sample replacement;replacement.data.assign(64000,1000);std::swap(s,replacement);hash=f.sample_hash();f.drag(200);CHECK(f.sample_hash()==hash);f.lift();std::swap(s,replacement);
}
void test_cuts_merge_reverse(){using namespace cuts_cases;Setup f(0);auto&s=f.pcm.sample;s.chops[0]=16000;s.chops[3]=0;s.chops[8]=24000;s.slice_reverse_mask=8;play_editor_capture();
    auto*shared=new PlayUnit;shared->sample_slot=63;delete f.engine->replace_unit(1,3,shared);CHECK(f.engine->set_step_lock(1,5,3,PlayUnit::P_SLICE,center(9)));
    auto before=f.hash();f.select(1);f.point(180,214);CHECK(s.slice_reverse_mask==9);f.point(270,214);CHECK(s.chops[0]==UINT32_MAX&&s.chops[3]==0&&s.chops[8]==24000&&s.slice_reverse_mask==8&&f.hash()==before);
}
void test_cuts_auto_clear(){using namespace cuts_cases;Setup f;auto before=f.hash();for(int n:{4,8,16}){auto hash=f.sample_hash();f.automatic(n);CHECK(f.sample_hash()==hash);f.point(250,203);
        for(int i=0;i<16;++i)CHECK(f.pcm.sample.chops[i]==(i<n?uint32_t(uint64_t(i)*32000/n):UINT32_MAX));}
    f.point(90,94);f.point(30,175);CHECK(f.pcm.sample.chops[0]==0);f.point(250,203);for(auto p:f.pcm.sample.chops)CHECK(p==UINT32_MAX);CHECK(f.hash()==before);
}
void test_cuts_source_recording(){using namespace cuts_cases;Setup f;auto hash=f.sample_hash();f.engine->pending_rec_start=1;f.add(80);CHECK(f.sample_hash()==hash);f.engine->pending_rec_start=0;
    f.down(f.x(8000));delete f.engine->replace_unit(CHAIN,3,new OscUnit);f.drag(120);CHECK(f.sample_hash()==hash);f.lift();play_editor_capture();CHECK(!play_editor_top());
}
void test_cuts_capture(){using namespace cuts_cases;
    // Streamed min/max is exhaustive, including one-frame antiphase transients.
    play_editor::Envelope<297> envelope;envelope.reset(0,10003);std::vector<int16_t> pcm(10003*2,0);pcm[2222*2]=32767;pcm[2222*2+1]=-32768;
    for(uint32_t at=0;at<10003;at+=257)envelope.feed(pcm.data()+at*2,at,std::min<uint32_t>(257,10003-at),2);
    CHECK(envelope.ready);int column=int(uint64_t(2222)*297/10003);CHECK(envelope.high[column]==32767&&envelope.low[column]==-32768);
    play_editor::Envelope<297> tiny;tiny.reset(0,2);int16_t pair[2]={-30000,30000};tiny.feed(pair,0,2,1);int8_t low[297],high[297];tiny.freeze(low,high,256);
    CHECK(low[0]<0&&high[296]>0);
    play_editor::Attacks detector;std::vector<int16_t> impulses(32000,0);impulses[4096]=30000;impulses[12032]=22000;
    detector.feed(impulses.data(),0,32000,1);auto proposed=detector.cuts(32000,100);CHECK(proposed.count(32000)>=3&&proposed.at[1]==4096);
    Setup f(16);f.select(12);for(int i=0;i<4;++i)play_editor_capture();f.surface(true);int top=objects;auto bottom=f.surface(false);CHECK(top+objects<8192);
    std::vector<q15>retired;retired.swap(f.pcm.sample.data);Engine*engine=g_eng;g_eng=nullptr;
    width=320;objects=0;texts.clear();pixels.assign(width*HEIGHT,0xff000000u);play_editor_draw();CHECK(pixels==bottom);g_eng=engine;retired.swap(f.pcm.sample.data);
}
void test_cuts_play_bridge(){using namespace cuts_cases;Setup f;f.stored(3);const auto before=f.hash(),sample=f.sample_hash();
    std::array<uint64_t,NUM_PATTERNS> patterns{};for(int i=0;i<NUM_PATTERNS;++i)patterns[i]=pattern_hash(f.engine->sequence_pattern(i));
    f.select(3);f.point(180,232);f.point(300,94);for(int i=0;i<4;++i)play_editor_capture();const auto viewport=f.surface(false);
    f.point(280,20);f.point(280,20);const auto closed=f.surface(false);
    CHECK(std::equal(viewport.begin()+320*105,viewport.begin()+320*225,closed.begin()+320*105)); // cut selection, grips and zoom survive close
    f.point(280,20);CHECK(play_editor_back());const auto backed=f.surface(false);
    CHECK(std::equal(viewport.begin()+320*105,viewport.begin()+320*225,backed.begin()+320*105));
    // Even swipes beginning on save or detach rows must not execute IO or change the document.
    char temp[]="/tmp/seance-cuts-menu-XXXXXX";CHECK(mkdtemp(temp));const char*old_dir=RECORDING_DIR;RECORDING_DIR=temp;
    const int clean=mutations;f.point(280,20);
    CHECK(play_editor_touch(f.play,40,194));play_focus_release();CHECK(std::filesystem::is_empty(temp));
    f.lift();CHECK(std::filesystem::is_empty(temp)&&f.sample_hash()==sample&&mutations==clean);
    CHECK(play_editor_touch(f.play,40,194));g_drag=Drag::None;f.lift();CHECK(std::filesystem::is_empty(temp)&&mutations==clean);
    CHECK(play_editor_touch(f.play,40,194));CHECK(std::filesystem::is_empty(temp)&&f.play.sample_slot==63);
    play_editor_drag(f.play,40,98);f.lift();CHECK(std::filesystem::is_empty(temp)&&f.sample_hash()==sample&&mutations==clean);
    // Scroll through the entire flat list, including the former distribute-menu area.
    for(int i=0;i<3;++i)f.swipe(194,98);
    for(int i=0;i<3;++i)f.swipe(98,194);
    CHECK(std::filesystem::is_empty(temp)&&f.play.sample_slot==63&&f.sample_hash()==sample&&mutations==clean);
    f.surface(false);for(const auto&t:texts)CHECK(t.text.find("distribute")==std::string::npos);
    const auto speed=f.play.params[PlayUnit::P_SPEED].base;f.point(40,122);CHECK(f.pcm.sample.slice_reverse_mask==(1u<<2)&&f.play.params[PlayUnit::P_SPEED].base==speed);
    f.point(220,72);CHECK(f.pcm.sample.slice_reverse_mask==0); // undo keeps selected cut and does not close menu
    CHECK(play_editor_touch(f.play,40,194));CHECK(std::filesystem::is_empty(temp)&&f.play.sample_slot==63);f.lift();
    auto&bank=trackr::synth::SampleBank::instance();int copied=-1;
    for(int s=0;s<trackr::synth::SAMPLE_BANK_SIZE;++s)if(std::strstr(bank.provenance(s).path,temp)){CHECK(copied<0);copied=s;CHECK(bank.slot(s).playable_frames()==8000);}
    CHECK(copied>=0&&f.play.sample_slot==63&&!f.play.playback_active()&&f.sample_hash()==sample);bank.clear_slot(copied);RECORDING_DIR=old_dir;std::filesystem::remove_all(temp);
    CHECK(f.hash()==before);for(int i=0;i<NUM_PATTERNS;++i)CHECK(pattern_hash(f.engine->sequence_pattern(i))==patterns[i]);
    CHECK(play_editor_back());f.point(250,232);CHECK(g_view==View::Sequencer&&g_overlay==Overlay::None&&f.hash()==before);
}
void dump_cuts(const std::filesystem::path&){throw std::runtime_error("old cuts dump retired; shared PLAY editor capture is an explicit task");}
