// Included after the real menu-stage1 harness helpers. No copied controller/UI.
namespace {
int nav_clock_peak = 0, nav_clock_band_probes = 0, nav_clock_layout_probes = 0;
NavChoice clock_key(u32 key) { NavChoice out;nav_buttons(key,out);return out; }
void clock_begin(int x=220,int y=12) {
    clock_key(0);NavChoice out;nav_touch_down(x,y,out);CHECK(out==NavChoice::None&&nav_is_open());
}
void clock_values(Fixture& f,int bpm=120,int swing=58) {
    nav_close();f.engine->set_bpm(bpm);f.engine->set_swing(swing);mutations=0;nav_open();clock_key(0);
}
void raw_text_audit(const Frame& f) {
    for(size_t i=0;i<f.raw_texts.size();++i) {
        const auto& a=f.raw_texts[i];
        CHECK(a.x>=0&&a.y>=0&&a.x+a.w<=f.width&&a.y+a.h<=240);
        for(size_t j=0;j<i;++j) {
            const auto& b=f.raw_texts[j];
            CHECK(!(a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h));
        }
    }
}
Frame clock_frame(bool capture=true) {
    if(capture) nav_capture();
    auto im=render(320,nav_draw);palette(im);paper_edges(im);raw_text_audit(im);
    nav_clock_peak=std::max(nav_clock_peak,im.objects);return im;
}
void clock_navigation() {
    test_nav();Fixture f;
    const std::pair<u32,int> arrows[]={{KEY_DLEFT,-1},{KEY_DUP,-1},{KEY_DRIGHT,1},{KEY_DDOWN,1},
        {KEY_CPAD_LEFT,-1},{KEY_CPAD_UP,-1},{KEY_CPAD_RIGHT,1},{KEY_CPAD_DOWN,1}};
    for(const auto& direction:arrows)for(int cursor=0;cursor<NAV_CHOICE_COUNT;++cursor) {
        nav_open();clock_key(0);NavChoice out;nav_touch_down(80,64+cursor*20,out);CHECK(out==CHOICES[cursor]);
        nav_open();for(int n=0;n<NAV_CHOICE_COUNT;++n) {CHECK(clock_key(direction.first)==NavChoice::None&&nav_is_open());clock_key(0);}
        CHECK(clock_key(KEY_A)==CHOICES[cursor]);
        nav_open();CHECK(clock_key(direction.first)==NavChoice::None);
        CHECK(clock_key(KEY_A)==CHOICES[(cursor+direction.second+NAV_CHOICE_COUNT)%NAV_CHOICE_COUNT]);
        CHECK(mutations==0&&g_view==View::Chains&&g_overlay==Overlay::None);
    }
}
void clock_defaults_frozen() {
    Fixture f;CHECK(f.engine->bpm==120&&f.engine->swing_percent==50);
    nav_open();auto im=clock_frame(false);CHECK(text_has(im,"120")&&text_has(im,"50%"));
    f.engine->set_bpm(137);f.engine->set_swing(61);
    // Audio-worker snapshot deliberately stale; header must use guarded fresh base.
    g_ui_snapshot.bpm=299;g_ui_snapshot.swing_percent=75;
    im=clock_frame();CHECK(text_has(im,"137")&&text_has(im,"61%"));
    auto* eng=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
    auto frozen=clock_frame(false);CHECK(frozen.pixels==im.pixels);g_eng=eng;g_audio=audio;
    CHECK(mutations==0);
    clock_begin();nav_touch_hold(236,12);im=clock_frame();CHECK(text_has(im,"145"));
    g_eng=nullptr;g_audio=nullptr;frozen=clock_frame(false);CHECK(frozen.pixels==im.pixels);g_eng=eng;g_audio=audio;
    nav_touch_release();CHECK(f.engine->bpm==145&&mutations==1);
}
void clock_keyboard() {
    Fixture f;clock_values(f);CHECK(clock_key(KEY_X)==NavChoice::None);
    CHECK(clock_key(KEY_DRIGHT)==NavChoice::None);CHECK(f.engine->bpm==121&&mutations==1);
    clock_key(0);clock_key(0);CHECK(f.engine->bpm==121&&mutations==1);
    clock_key(KEY_DUP);CHECK(f.engine->bpm==126&&mutations==2);
    clock_key(KEY_DLEFT);clock_key(KEY_DDOWN);CHECK(f.engine->bpm==120&&mutations==4);
    clock_key(KEY_X);clock_key(KEY_DRIGHT);clock_key(KEY_DUP);CHECK(f.engine->swing_percent==64&&mutations==6);
    clock_key(KEY_DLEFT);clock_key(KEY_DDOWN);CHECK(f.engine->swing_percent==58&&mutations==8);
    CHECK(clock_key(KEY_B)==NavChoice::None&&nav_is_open());
    CHECK(clock_key(KEY_X)==NavChoice::None);CHECK(clock_key(KEY_A)==NavChoice::None&&nav_is_open());
    clock_key(KEY_B);CHECK(!nav_is_open());
    for(int bpm:{20,300}) {
        clock_values(f,bpm,50);clock_key(KEY_X);
        const u32 outward=bpm==20?KEY_DLEFT:KEY_DRIGHT,coarse=bpm==20?KEY_DDOWN:KEY_DUP;
        clock_key(outward);clock_key(coarse);CHECK(f.engine->bpm==bpm&&mutations==0);
        for(int n=0;n<80;++n)clock_key(bpm==20?KEY_DUP:KEY_DDOWN);
        CHECK(f.engine->bpm==(bpm==20?300:20)&&mutations==56);
    }
    for(int swing:{50,75}) {
        clock_values(f,120,swing);clock_key(KEY_X);clock_key(KEY_X);
        clock_key(swing==50?KEY_DLEFT:KEY_DRIGHT);clock_key(swing==50?KEY_DDOWN:KEY_DUP);
        CHECK(f.engine->swing_percent==swing&&mutations==0);
        for(int n=0;n<10;++n)clock_key(swing==50?KEY_DUP:KEY_DDOWN);
        CHECK(f.engine->swing_percent==(swing==50?75:50)&&mutations==5);
    }
    clock_values(f);clock_key(KEY_X);clock_key(KEY_SELECT);CHECK(!nav_is_open()&&mutations==0);
}
void clock_bands() {
    Fixture f;
    for(int x:{167,168,169,314,315,316}) for(int y:{1,2,3,26,27,28,29,30,31,54,55,56,57}) {
        clock_values(f);NavChoice out;nav_touch_down(x,y,out);CHECK(out==NavChoice::None&&nav_is_open());
        const bool tempo=x>=168&&x<316&&y>=2&&y<28;
        const bool swing=x>=168&&x<316&&y>=30&&y<56;
        // A leftwards draft works from the right edge and remains header-owned.
        nav_touch_hold(x-8,y);CHECK(f.engine->bpm==120&&f.engine->swing_percent==58&&mutations==0);
        nav_touch_release();CHECK(f.engine->bpm==(tempo?116:120));
        CHECK(f.engine->swing_percent==(swing?56:58));CHECK(mutations==int(tempo||swing));++nav_clock_band_probes;
    }
    for(int i=0;i<NAV_CHOICE_COUNT;++i)for(int y:{58+i*20,77+i*20})for(int x:{38,299}) {
        clock_values(f);NavChoice out;nav_touch_down(x,y,out);CHECK(out==CHOICES[i]&&!nav_is_open());++nav_clock_band_probes;
    }
    // All eight rows are live after adding patchbay; exit y198..217,
    // immediately followed by the unchanged theme footer at y218.
    for(int y:{58,79,101,211})for(int x:{37,300}) {
        clock_values(f);NavChoice out;nav_touch_down(x,y,out);CHECK(out==NavChoice::None&&nav_is_open());++nav_clock_band_probes;
    }
}
void clock_gestures() {
    Fixture f;clock_values(f);clock_begin();
    for(int x:{221,222,230,240}) {nav_touch_hold(x,12);nav_capture();CHECK(f.engine->bpm==120&&mutations==0);}
    CHECK(text_has(clock_frame(),"130"));nav_touch_release();CHECK(f.engine->bpm==130&&mutations==1);
    nav_touch_release();CHECK(mutations==1);
    clock_values(f);clock_begin();nav_touch_hold(240,12);nav_touch_hold(220,12);nav_touch_release();CHECK(mutations==0&&f.engine->bpm==120);
    clock_begin();nav_touch_hold(221,12);nav_touch_release();CHECK(mutations==0);
    clock_begin(220,38);nav_touch_hold(244,38);CHECK(f.engine->swing_percent==58);nav_touch_release();CHECK(f.engine->swing_percent==64&&mutations==1);
    clock_values(f);clock_begin();nav_touch_hold(-90000,12);nav_touch_release();CHECK(f.engine->bpm==20&&mutations==1);
    clock_values(f);clock_begin(168,12);nav_touch_hold(90000,12);nav_touch_release();CHECK(f.engine->bpm==195&&mutations==1);
    clock_values(f,20,50);clock_begin();nav_touch_hold(0,12);nav_touch_release();CHECK(mutations==0&&f.engine->bpm==20);
    clock_values(f,300,75);clock_begin();nav_touch_hold(319,12);nav_touch_release();CHECK(mutations==0&&f.engine->bpm==300);
    clock_begin(220,38);nav_touch_hold(319,38);nav_touch_release();CHECK(mutations==0&&f.engine->swing_percent==75);
}
void clock_cancellation() {
    Fixture f;
    for(u32 key:{u32(KEY_B),u32(KEY_A),u32(KEY_SELECT),u32(KEY_X),u32(KEY_START),u32(KEY_Y),u32(KEY_L),u32(KEY_R),u32(KEY_ZR)}) {
        clock_values(f);clock_begin();nav_touch_hold(240,12);CHECK(clock_key(key)==NavChoice::None);
        NavChoice out;nav_touch_down(80,64,out);CHECK(out==NavChoice::None);
        nav_touch_hold(280,12);nav_touch_release();CHECK(f.engine->bpm==120&&mutations==0);
    }
    // A directional edge discards draft; the next fresh keyboard edge can nudge.
    clock_values(f);clock_begin();nav_touch_hold(240,12);clock_key(KEY_DRIGHT);nav_touch_release();CHECK(f.engine->bpm==120&&mutations==0);
    clock_key(0);clock_key(KEY_DRIGHT);CHECK(f.engine->bpm==121&&mutations==1);
    clock_values(f);clock_begin();nav_touch_hold(240,12);clock_key(KEY_B|KEY_DRIGHT);nav_touch_release();CHECK(f.engine->bpm==120&&mutations==0&&nav_is_open());
    clock_values(f);clock_begin();nav_touch_hold(240,12);clock_key(KEY_SELECT|KEY_A|KEY_DRIGHT);nav_touch_release();CHECK(f.engine->bpm==120&&mutations==0&&!nav_is_open());
    for(int delta:{-24,24,200}) {
        clock_values(f);clock_begin();nav_touch_hold(240,12+delta);nav_touch_hold(260,12);nav_touch_release();CHECK(f.engine->bpm==120&&mutations==0&&nav_is_open());
    }
    clock_values(f);clock_begin();nav_touch_hold(240,35);nav_touch_release();CHECK(f.engine->bpm==130&&mutations==1);
    clock_values(f);clock_begin();nav_touch_hold(240,12);nav_close();nav_open();nav_touch_release();CHECK(f.engine->bpm==120&&mutations==0);
    // Captured touch entering seven menu bands cannot navigate on hold or lift.
    clock_values(f);clock_begin();nav_touch_hold(80,64);nav_touch_release();CHECK(nav_is_open()&&f.engine->bpm==120&&mutations==0);
    // Screenshot is explicitly not a control edge and must preserve a draft.
    clock_values(f);clock_begin();nav_touch_hold(240,12);clock_key(KEY_ZL);nav_touch_release();CHECK(f.engine->bpm==130&&mutations==1);
    for(u32 key:{u32(KEY_START),u32(KEY_Y),u32(KEY_L),u32(KEY_R),u32(KEY_ZR)}) {
        clock_values(f);CHECK(clock_key(key)==NavChoice::None&&nav_is_open());
        NavChoice out;nav_touch_down(80,64,out);CHECK(out==NavChoice::None&&nav_is_open()&&mutations==0);
    }
}
void clock_identity() {
    Fixture f;
    for(int action=0;action<6;++action)for(int detection=0;detection<3;++detection) {
        clock_values(f);clock_begin();nav_touch_hold(240,12);
        auto other=std::make_unique<Engine>();platform::AudioOut other_audio;
        if(action==0) f.engine->set_bpm(131);
        if(action==1) f.engine->set_swing(62);
        if(action==2) g_eng=other.get();
        if(action==3) g_eng=nullptr;
        if(action==4) g_audio=nullptr;
        if(action==5) g_audio=&other_audio;
        // Every ownership failure is detected by capture, hold and direct lift.
        if(detection==0)nav_capture();
        if(detection==1)nav_touch_hold(260,12);
        if(detection==2)nav_touch_release();
        g_eng=f.engine.get();g_audio=&f.audio;
        nav_touch_hold(260,12);nav_touch_release();
        CHECK(f.engine->bpm==(action==0?131:120));
        CHECK(f.engine->swing_percent==(action==1?62:58));CHECK(mutations==0);
    }
    for(bool engine_null:{false,true}) {
        clock_values(f);if(engine_null)g_eng=nullptr;else g_audio=nullptr;
        clock_key(KEY_X);clock_key(KEY_DRIGHT);clock_begin();nav_touch_hold(240,12);nav_touch_release();
        g_eng=f.engine.get();g_audio=&f.audio;CHECK(f.engine->bpm==120&&mutations==0);
    }
}
void setup_running_clock(Engine& e) {
    e.chains[0].units[0]=make_unit(UnitKind::Osc);e.set_swing(58);
    for(int i=0;i<16;++i) {
        auto& step=e.pattern.steps[0][i];step.flags=SeqStep::Trig;step.set_retrig_count(3);
        e.set_step_lock(0,i,0,0,q15(12000+i*700));e.set_step_microtiming(0,i,i%2?-18:17);
    }
    e.set_sequencer_enabled(true);e.transport_play();
    e.request_chain_trigger(1,LaunchQuantize::Bar);
}
void clock_running() {
    Fixture f;auto control=std::make_unique<Engine>();setup_running_clock(*f.engine);setup_running_clock(*control);
    std::vector<q15> a(512),b(512);
    auto equal_audio=[&] {
        f.engine->render(a.data(),256);control->render(b.data(),256);CHECK(a==b);
        CHECK(f.engine->transport_frame==control->transport_frame&&f.engine->sequence_tick==control->sequence_tick);
        CHECK(f.engine->pending_triggers==control->pending_triggers);
        for(int c=0;c<4;++c)CHECK(f.engine->sequence_step(c)==control->sequence_step(c));
    };
    for(int i=0;i<12;++i)equal_audio();
    nav_open();clock_begin();
    for(int i=0;i<96;++i) {
        nav_touch_hold(222+i%80,12);nav_capture();CHECK(f.engine->bpm==120&&f.engine->swing_percent==58&&mutations==0);equal_audio();
    }
    nav_touch_hold(240,12);nav_touch_release();CHECK(f.engine->bpm==130&&mutations==1&&f.engine->transport_playing);
    // Same legacy setter at the same moment => identical reschedule behavior.
    control->set_bpm(130);for(int i=0;i<64;++i)equal_audio();
    clock_begin(220,38);nav_touch_hold(244,38);for(int i=0;i<32;++i)equal_audio();
    CHECK(f.engine->swing_percent==58&&mutations==1);nav_touch_release();control->set_swing(64);
    CHECK(f.engine->swing_percent==64&&mutations==2);for(int i=0;i<64;++i)equal_audio();
}
std::vector<char> clock_patch(const Engine& e,const std::filesystem::path& path) {
    CHECK(patchio::save(e,path.c_str()));std::ifstream in(path,std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in),{});
}
void clock_persistence() {
    Fixture f;f.four();
    auto& bank=trackr::synth::SampleBank::instance();auto sample_before=bank.slot(63);
    bank.slot(63).data={1,-2,3,-4,5,-6};bank.slot(63).chops[0]=0;bank.slot(63).chops[1]=3;
    const auto pcm=bank.slot(63).data;const auto generation=bank.generation(63);
    f.engine->chains[0].units[0]->params[0].src=ModSource::SigilA;
    f.engine->chains[0].units[0]->params[0].mod_depth=12345;
    f.engine->pattern.steps[0][5].flags=SeqStep::Trig;f.engine->set_step_lock(0,5,0,0,12500);
    f.engine->set_sigil_sync(0,true);f.engine->set_sigil_div(0,3);f.engine->set_sigil_mod(2,ModSource::SigilA,1234);
    const auto dir=std::filesystem::temp_directory_path()/"seance-nav-clock-persistence";
    std::filesystem::create_directories(dir);const auto path=dir/"patch.snc";
    clock_values(f);const auto before=clock_patch(*f.engine,path);
    clock_begin();nav_touch_hold(250,12);CHECK(clock_patch(*f.engine,path)==before);nav_touch_release();
    clock_begin(220,38);nav_touch_hold(248,38);nav_touch_release();CHECK(f.engine->bpm==135&&f.engine->swing_percent==65&&mutations==2);
    const auto edited=clock_patch(*f.engine,path);CHECK(edited!=before);
    auto loaded=std::make_unique<Engine>();CHECK(patchio::load(*loaded,path.c_str()));
    CHECK(loaded->bpm==135&&loaded->swing_percent==65&&loaded->sigil_sync[0]&&loaded->sigil_div[0]==3);
    CHECK(loaded->chains[0].units[0]->params[0].src==ModSource::SigilA);
    CHECK(loaded->chains[0].units[0]->params[0].mod_depth==12345);
    CHECK(loaded->pattern.steps[0][5].flags==SeqStep::Trig&&loaded->pattern.steps[0][5].lock_count==1);
    CHECK(clock_patch(*loaded,dir/"roundtrip.snc")==edited);
    // Restoring only the two legitimate fields recovers every serialized byte:
    // no parameters, Sigil tables/routes, triggers or patterns were authored.
    f.engine->set_bpm(120);f.engine->set_swing(58);CHECK(clock_patch(*f.engine,path)==before);
    CHECK(bank.slot(63).data==pcm&&bank.slot(63).chops[0]==0&&bank.slot(63).chops[1]==3&&bank.generation(63)==generation);
    bank.slot(63)=std::move(sample_before);std::filesystem::remove_all(dir);
}
void clock_raster() {
    Fixture f;f.four();
    for(int bpm:{20,99,100,120,299,300})for(int swing:{50,58,75})for(int mode=0;mode<4;++mode) {
        clock_values(f,bpm,swing);
        if(mode==1)clock_key(KEY_X);
        if(mode==2){clock_key(KEY_X);clock_key(KEY_X);}
        if(mode==3){clock_begin();nav_touch_hold(240,12);}
        auto bot=clock_frame();auto top=render(400,chains_context_draw_top);raw_text_audit(top);
        CHECK(bot.objects+top.objects<8192);nav_clock_peak=std::max(nav_clock_peak,bot.objects+top.objects);
        CHECK(!text_has(bot,"a")&&!text_has(bot,"b")&&!text_has(bot,"project"));++nav_clock_layout_probes;
    }
}
void test_nav_clock() {
    const std::pair<const char*,void(*)()> tests[]={
        {"seven choices/navigation/button focus/no stress label",clock_navigation},
        {"fresh defaults/stale snapshot/frozen null-engine and audio",clock_defaults_frozen},
        {"x clock focus/dpad edges/limits/no-op",clock_keyboard},
        {"half-open clock/header gaps/seven 20px navigation bands/no eighth row",clock_bands},
        {"draft-on-hold/one lift commit/relative/native clamps/no-op",clock_gestures},
        {"button priority/vertical sticky/close-reopen/no nav leak/screenshot",clock_cancellation},
        {"fresh pair/engine/audio ownership/capture-hold-lift invalidation",clock_identity},
        {"running pending PCM+sequencer equality/legacy commit reschedule",clock_running},
        {"existing patch roundtrip/only clock edits/PCM immutable",clock_persistence},
        {"strict raw text/primitive/paper/palette/all-value paired budget",clock_raster}
    };
    for(const auto& t:tests){t.second();std::printf("pass nav-clock %s\n",t.first);}
    std::printf("pass nav-clock 10/10; bands=%d layouts=%d peak=%d/8192\n",nav_clock_band_probes,nav_clock_layout_probes,nav_clock_peak);
}
void dump_nav_clock(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);std::ofstream report(dir/"frames.tsv"),textfile(dir/"raw_text.tsv");
    report<<"name\ttop_objects\tbottom_objects\tout_of_bounds\n";
    textfile<<"name\tscreen\tx\ty\tw\th\ttext\n";
    Fixture f;f.four();
    // Match legacy nav_0 chain selection before the approved default capture.
    nav_open();clock_key(0);NavChoice o;nav_touch_down(80,64,o);nav_close();
    auto pair=[&](const char* name,bool freeze=false) {
        f.refresh();nav_capture();auto top=render(400,chains_context_draw_top);raw_text_audit(top);palette(top);paper_edges(top);
        auto* eng=g_eng;auto* audio=g_audio;if(freeze){g_eng=nullptr;g_audio=nullptr;}
        auto bot=clock_frame(false);g_eng=eng;g_audio=audio;
        CHECK(top.objects+bot.objects<8192);nav_clock_peak=std::max(nav_clock_peak,top.objects+bot.objects);
        ppm(dir/(std::string(name)+"_top.ppm"),top);ppm(dir/(std::string(name)+"_bottom.ppm"),bot);
        report<<name<<'\t'<<top.objects<<'\t'<<bot.objects<<'\t'<<top.outside+bot.outside<<'\n';
        const std::pair<const char*,const Frame*> screens[]={{"top",&top},{"bottom",&bot}};
        for(const auto& sf:screens)for(const auto& t:sf.second->raw_texts)
            textfile<<name<<'\t'<<sf.first<<'\t'<<t.x<<'\t'<<t.y<<'\t'<<t.w<<'\t'<<t.h<<'\t'<<t.text<<'\n';
    };
    clock_values(f);pair("01_approved_idle");
    clock_key(KEY_X);pair("02_tempo_focus");clock_key(KEY_X);pair("03_swing_focus");
    clock_begin();nav_touch_hold(240,12);pair("04_tempo_pending");pair("05_pending_frozen",true);
    nav_touch_release();clock_key(KEY_B);pair("06_tempo_committed");
    clock_values(f);clock_begin(220,38);nav_touch_hold(244,38);pair("07_swing_pending");
    nav_touch_release();clock_key(KEY_B);pair("08_swing_committed");
    clock_values(f,20,50);pair("09_minimum");clock_values(f,300,75);pair("10_maximum");
    clock_values(f);clock_begin();nav_touch_hold(240,60);nav_touch_hold(260,12);pair("11_vertical_cancelled");nav_touch_release();
    clock_values(f);f.engine->transport_play();pair("12_running_idle");
    clock_begin();nav_touch_hold(240,12);pair("13_running_pending");nav_touch_release();clock_key(KEY_B);pair("14_running_committed");
    std::ofstream summary(dir/"summary.json");summary<<"{\"suites\":10,\"band_probes\":"<<nav_clock_band_probes<<",\"layout_probes\":"<<nav_clock_layout_probes
        <<",\"max_paired_objects\":"<<nav_clock_peak<<",\"raw_text_overlaps\":0,\"raw_text_outside\":0,\"primitive_outside\":0,\"frozen_null_engine_audio\":true}\n";
}
} // namespace
