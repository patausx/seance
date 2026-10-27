// Included by the existing sequencer host harness; no second UI or DSP model.
struct PerformanceFixture : Fixture {
    PerformanceFixture() {g_view=View::Performance;g_overlay=Overlay::None;g_drag=Drag::None;performance_view_reset();refresh();}
    ~PerformanceFixture(){performance_view_leave();}
    void release(bool commit=true){performance_view_touch_release(commit);refresh();}
    void tap_at(int x,int y){performance_view_touch_down(x,y);release();}
    void save_scene(int s){const int x=s?285:25;performance_view_touch_down(x,210);g_ui_frame+=24;performance_view_touch_hold(x,210);release();}
};
std::vector<u32> performance_surface(bool top) {
    width=top?400:320;objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();pixels.assign(width*240,0xff000000u);
    if(top)top_draw();else performance_view_draw();
    CHECK(outside_rects==0&&outside_lines==0&&text_overlaps==0);
    if(top)maximum_top_objects=std::max(maximum_top_objects,objects);else maximum_objects=std::max(maximum_objects,objects);
    return pixels;
}
void test_performance_faders() {
    PerformanceFixture f;
    for(int c=0;c<4;++c) {
        const int x=40+c*78;const int dirty=mutations;const auto pattern=f.hash();
        performance_view_touch_down(x,100);CHECK(g_sel_chain==c&&mutations==dirty);
        performance_view_touch_drag(x+30,100);CHECK(f.engine->chains[c].volume==32767&&mutations==dirty);
        performance_view_touch_drag(x+200,145);CHECK(f.engine->chains[c].volume>6000&&f.engine->chains[c].volume<9000);
        performance_view_touch_drag(x,240);CHECK(f.engine->chains[c].volume==0);
        performance_view_touch_drag(x,-100);CHECK(f.engine->chains[c].volume==32767);
        f.release();CHECK(f.hash()==pattern);
        for(int other=0;other<4;++other)CHECK(f.engine->chains[other].volume==32767);
        f.engine->chains[c].volume=12345;const int start=mutations;
        performance_view_touch_down(x,120);performance_view_touch_drag(x+30,120);CHECK(f.engine->chains[c].volume==12345&&mutations==start);
        performance_view_touch_drag(x,110);performance_view_touch_drag(x,120);CHECK(f.engine->chains[c].volume==12345);
        performance_view_leave();performance_view_touch_drag(x,220);CHECK(f.engine->chains[c].volume==12345);
        f.tap_at(x,182);CHECK(f.engine->chains[c].mute&&f.engine->chains[c].volume==12345);
        f.tap_at(x,182);CHECK(!f.engine->chains[c].mute);f.engine->chains[c].volume=32767;
    }
    // L's current temporary-return contract permits faders, then restores on
    // key-up. Overlays still cancel ownership and cannot leak a later drag.
    performance_view_touch_down(40,110);performance_view_buttons(KEY_L,KEY_L,0);
    performance_view_touch_down(40,110);performance_view_touch_drag(40,220);CHECK(f.engine->chains[0].volume==0);
    performance_view_buttons(0,0,KEY_L);CHECK(f.engine->chains[0].volume==32767);
    performance_view_touch_down(40,110);g_overlay=Overlay::PatchBrowser;
    performance_view_touch_drag(40,220);CHECK(f.engine->chains[0].volume==32767);g_overlay=Overlay::None;
    performance_view_touch_drag(40,220);CHECK(f.engine->chains[0].volume==32767);
    performance_view_buttons(KEY_X,KEY_X,0);CHECK(f.engine->chains[g_sel_chain].mute);performance_view_buttons(KEY_X,KEY_X,0);
    performance_view_buttons(KEY_RIGHT,KEY_RIGHT,0);CHECK(g_sel_chain==1);performance_view_buttons(KEY_LEFT,KEY_LEFT,0);CHECK(g_sel_chain==0);
}
struct MixerConstant : Unit {int trigs=0;void process(q15* b,int n)override{std::fill(b,b+n,q15(4096));}void trigger()override{++trigs;}};
void test_performance_morph() {
    PerformanceFixture f;delete f.engine->replace_unit(0,0,new MixerConstant);auto* unit=static_cast<MixerConstant*>(f.engine->chains[0].units[0]);
    f.refresh();performance_view_touch_down(30,35);CHECK(unit->trigs==0);f.release();CHECK(unit->trigs==1);
    performance_view_touch_down(30,35);performance_view_touch_hold(100,100);f.release();CHECK(unit->trigs==1);
    performance_view_touch_down(30,35);performance_view_leave();f.release();CHECK(unit->trigs==1);
    const int dirty=mutations;f.tap_at(25,210);CHECK(!f.engine->scenes[0][0].valid&&mutations==dirty);
    f.save_scene(1);CHECK(!f.engine->scenes[0][1].valid&&mutations==dirty);
    f.tap_at(60,211);CHECK(mutations==dirty);f.save_scene(0);
    const int captured=mutations;performance_view_touch_down(25,210);g_ui_frame+=23;performance_view_touch_hold(25,210);CHECK(mutations==captured);performance_view_touch_hold(100,210);g_ui_frame+=24;performance_view_touch_hold(25,210);f.release();CHECK(mutations==captured);
    f.engine->chains[1].units[0]->params[0].base=4000;f.engine->chains[0].volume=6000;f.save_scene(1);
    CHECK(f.engine->scene_morph[0]==32767&&f.engine->chains[0].volume==6000);
    f.engine->chains[0].volume=10000;f.engine->chains[1].volume=15000;
    for(int x:{60,160,260,60}) {
        performance_view_touch_down(x,211);f.release();
        CHECK(f.engine->chains[0].volume==10000&&f.engine->chains[1].volume==15000);
        CHECK(f.engine->scene_morph[0]==norm_h(x,60,200,false));
        const auto& a=f.engine->scenes[1][0];const auto& b=f.engine->scenes[1][1];const int morph=f.engine->scene_morph[1];
        CHECK(f.engine->chains[1].units[0]->params[0].base==a.base[0][0]+(((b.base[0][0]-a.base[0][0])*morph)>>15));
    }
    const int before=mutations;delete f.engine->replace_unit(1,0,new NoiseUnit);f.tap_at(260,211);CHECK(mutations==before&&f.engine->scene_morph[0]==0);
}
void test_performance_gain_ramp() {
    auto e=std::make_unique<Engine>(),oracle=std::make_unique<Engine>(),partition=std::make_unique<Engine>();
    for(auto* p:{e.get(),oracle.get(),partition.get()}){p->chains[0].units[0]=new MixerConstant;p->chains[0].volume=0;}
    std::array<q15,512> a{},b{},expected{};
    e->set_chain_volume_smooth(0,32767);partition->set_chain_volume_smooth(0,32767);
    e->render(a.data(),256);int offset=0;for(int n:{1,17,31,7,72,128}){partition->render(b.data()+offset*2,n);offset+=n;}CHECK(offset==256&&a==b);
    for(int i=0;i<256;++i){oracle->chains[0].volume=q15(32767-32767*std::max(0,127-i)/128);oracle->render(expected.data()+i*2,1);}CHECK(a==expected);
    CHECK(a[0]>0&&a[0]<100&&a[254]==a[510]);
    e->set_chain_volume_smooth(0,0);e->render(a.data(),64);const q15 last=a[126];e->set_chain_volume_smooth(0,32767);e->render(b.data(),128);CHECK(b[0]>=last&&b[0]-last<40&&b[254]==expected[510]);
    e->set_chain_volume_smooth(0,0);e->render(a.data(),128);CHECK(a[254]==0&&a[255]==0);
    e->chains[0].mute=true;e->set_chain_volume_smooth(0,32767);e->render(a.data(),128);CHECK(std::all_of(a.begin(),a.begin()+256,[](q15 v){return v==0;}));
    e->chains[0].mute=false;e->render(a.data(),1);CHECK(a[0]==expected[510]);
    e->set_chain_volume_smooth(0,0);e->chains[0].volume=32767;e->render(a.data(),1);CHECK(a[0]==expected[510]);
    // Swapping a same-target patch must also discard any stale ramp.
    e->set_chain_volume_smooth(0,1000);oracle->chains[0].volume=1000;e->swap_patch_state(*oracle);
    e->render(a.data(),1);partition->chains[0].volume=1000;partition->render(b.data(),1);CHECK(a[0]==b[0]);
    e->set_chain_volume_smooth(-1,0);e->set_chain_volume_smooth(4,0);CHECK(e->chains[0].volume==1000);
}
void test_performance_feedback() {
    PerformanceFixture f;
    auto marker_y=[&](int chain,u32 color) {
        const auto image=performance_surface(false);const int x=8+chain*78+56;
        for(int y=80;y<=164;++y)if(image[y*320+x]==color)return y;
        return -1;
    };
    auto tick=[&](u32 frames,q15 peak) {g_ui_frame+=frames;g_ui_snapshot.chain_peak[0]=peak;performance_view_tick();};
    const int dirty=mutations;const auto pattern=f.hash();
    tick(1,32767);CHECK(marker_y(0,m::ink())==80);
    tick(60,0);CHECK(marker_y(0,m::ink())==80); // full one-second hold
    int last=80;
    for(int frame=4;frame<=120;frame+=4) {
        tick(4,0);const int y=marker_y(0,m::ink());
        if(frame<120){CHECK(y>=last&&y-last<=3);last=y;}else CHECK(y==-1);
    }
    // Frame skipping/wrap use the same absolute release timeline.
    g_ui_frame=0xfffffff0u;tick(0,32767);tick(90,0);CHECK(marker_y(0,m::ink())==101);
    tick(1,30000);CHECK(marker_y(0,m::ink())==88);tick(60,0);CHECK(marker_y(0,m::ink())==88);
    g_ui_snapshot.chain_mute[0]=true;tick(30,32767);CHECK(marker_y(0,m::ghost())>88); // muted history falls; no new peak
    tick(120,32767);CHECK(marker_y(0,m::ghost())==-1);
    g_ui_snapshot.chain_mute[0]=false;g_ui_snapshot.chain_peak[0]=0;
    g_ui_snapshot.chain_peak[1]=16384;performance_view_tick();CHECK(marker_y(1,m::ink())==122&&marker_y(0,m::ink())==-1);
    g_ui_snapshot.chain_peak[1]=0;g_ui_frame+=180;performance_view_tick();CHECK(marker_y(1,m::ink())==-1);
    tick(1,32767);g_ui_snapshot.chain_peak[0]=0;performance_view_reset();CHECK(marker_y(0,m::ink())==-1);
    // A just-published rise after tick cannot put the marker below the live bar.
    g_ui_snapshot.chain_peak[0]=32767;CHECK(marker_y(0,m::ink())==80);g_ui_snapshot.chain_peak[0]=0;
    CHECK(mutations==dirty&&f.hash()==pattern);
    for(int c=0;c<4;++c)CHECK(f.engine->chains[c].volume==32767);
    f.save_scene(1);performance_surface(false);CHECK(has_text("retry")&&!has_text("saved"));CHECK(mutations==dirty);
    for(int s=0;s<2;++s) {
        const int x=s?285:25, before=mutations;
        performance_view_touch_down(x,210);g_ui_frame+=12;performance_view_touch_hold(x,210);
        const auto progress=performance_surface(false);CHECK(has_text(s?"b 50%":"a 50%")&&!has_text("saved")&&mutations==before);
        const int bx=s?270:8;CHECK(progress[221*320+bx+2]==(theme_test::gray()?rgb(48,48,48):theme_test::branch())); // palette-specific progress fill
        g_ui_frame+=11;performance_view_touch_hold(x,210);performance_surface(false);CHECK(has_text(s?"b 95%":"a 95%")&&mutations==before);
        ++g_ui_frame;performance_view_touch_hold(x,210);f.release();
        const auto saved=performance_surface(false);CHECK(has_text("saved")&&mutations==before+1);
        CHECK(saved[221*320+bx+2]==m::spot());
        auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
        CHECK(performance_surface(false)==saved);g_eng=engine;g_audio=audio;
        g_ui_frame+=60;performance_view_tick();performance_surface(false);CHECK(!has_text("saved"));
    }
    const int captures=mutations;
    performance_view_touch_down(25,210);g_ui_frame+=12;performance_view_touch_hold(100,210);f.release();
    performance_surface(false);CHECK(!has_text("a 50%")&&!has_text("saved")&&mutations==captures);
    performance_view_touch_down(25,210);g_ui_frame+=12;performance_view_buttons(KEY_SELECT,KEY_SELECT,0);f.release();
    performance_surface(false);CHECK(!has_text("a 50%")&&!has_text("saved")&&mutations==captures);
}
void test_performance_motion() {
    PerformanceFixture f;
    const u64 old_time=host_time_ms;host_time_ms=1000;g_sel_chain=0;
    performance_view_capture();
    const auto idle_top=performance_surface(true),idle_bottom=performance_surface(false);
    // Exact column edges and the quiet, opaque selection continue across LCDs.
    for(int c=0;c<4;++c) {
        const u32 edge=c==0?m::spot():m::branch();
        CHECK(idle_top[239*400+44+78*c]==edge&&idle_bottom[100*320+4+78*c]==edge);
        // Full numeric mask sits 12px lower, not just its animated underline.
        const auto* glyph=note_display::find(char('1'+c));CHECK(glyph&&!glyph->small);
        const int left=81+78*c-glyph->width/2,stride=(glyph->width+7)/8;
        for(int y=0;y<glyph->height;++y)for(int x=0;x<glyph->width;++x)
            if(note_display::bitmap[glyph->offset+y*stride+x/8]&(0x80u>>(x&7)))
                CHECK(idle_top[(84+glyph->top+y)*400+left+x]==(c==0?m::spot():m::ink()));
    }
    auto top_accent=[&](){return performance_surface(true)[92*400+81];};
    auto bottom_accent=[&](){return performance_surface(false)[47*320+41];};
    const u32 top_off=top_accent(),bottom_off=bottom_accent();
    // A confirmed tap starts at full strength, wall time (not draw count) releases.
    f.tap_at(30,35);performance_view_capture();CHECK(top_accent()==m::spot()&&bottom_accent()==m::spot());
    const auto down=performance_surface(false);
    for(int y=74;y<171;++y)for(int x=0;x<320;++x)CHECK(down[y*320+x]==idle_bottom[y*320+x]);
    host_time_ms+=75;performance_view_capture();CHECK(top_accent()!=top_off&&top_accent()!=m::spot());
    auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
    const auto frozen=performance_surface(true);host_time_ms+=500;
    CHECK(performance_surface(true)==frozen); // draw does not read time or engine
    performance_view_capture();CHECK(top_accent()==top_off&&bottom_accent()==bottom_off);
    g_eng=engine;g_audio=audio;
    performance_view_touch_down(30,35);performance_view_touch_release(false);performance_view_capture();CHECK(top_accent()==top_off);
    // Step movement alone is not a strike. Accepted hits can be from a different
    // transition bank; trust the event age, not the displayed step/bank identity.
    g_ui_snapshot.transport_playing=g_ui_snapshot.sequencer_enabled=true;
    g_ui_snapshot.sequence_step[0]=7;g_ui_snapshot.sequence_hit[0]={};
    performance_view_capture();CHECK(top_accent()==top_off);
    g_ui_snapshot.sequence_hit[0]={0,11,7,140};performance_view_capture();CHECK(top_accent()==m::spot());
    g_ui_snapshot.sequence_hit[0].age_ms=70;performance_view_capture();CHECK(top_accent()!=top_off&&top_accent()!=m::spot());
    g_ui_snapshot.sequence_hit[0].age_ms=140;performance_view_capture();CHECK(top_accent()==top_off);
    g_ui_snapshot.sequence_hit[0].age_ms=0;g_ui_snapshot.sequencer_enabled=false;
    performance_view_capture();CHECK(top_accent()==top_off);
    g_ui_snapshot.sequencer_enabled=true;g_ui_snapshot.transport_playing=false;
    performance_view_capture();CHECK(top_accent()==top_off);
    g_ui_snapshot.transport_playing=true;g_ui_snapshot.chain_mute[0]=true;
    performance_view_capture();CHECK(top_accent()==top_off);
    CHECK(performance_surface(true)[235*400+81]==m::spot());
    CHECK(performance_surface(false)[194*320+41]==m::spot());
    host_time_ms+=160;performance_view_capture();CHECK(performance_surface(true)[235*400+81]!=m::spot());
    g_ui_snapshot.sequence_hit[0]={};g_ui_snapshot.chain_mute[0]=false;performance_view_capture();
    // Modal/view changes cancel old manual feedback instead of replaying it.
    f.tap_at(30,35);performance_view_capture();CHECK(top_accent()==m::spot());
    g_overlay=Overlay::PatchBrowser;performance_view_capture();g_overlay=Overlay::None;
    performance_view_capture();CHECK(top_accent()==top_off);
    f.tap_at(30,35);nav_open();performance_view_capture();nav_close();performance_view_capture();CHECK(top_accent()==top_off);
    f.tap_at(30,35);g_view=View::Sigils;performance_view_capture();g_view=View::Performance;
    performance_view_capture();CHECK(top_accent()==top_off);
    // All six route labels fit; inactive/morphing banks have no invented head.
    for(int c=0;c<4;++c)for(int slot=0;slot<6;++slot)g_ui_snapshot.unit_kind[c][slot]=(u8)UnitKind::Glitch;
    g_ui_snapshot.sequence_step[0]=255;performance_view_capture();performance_surface(true);
    CHECK(has_text("glitch"));
    for(int c=0;c<4;++c)for(int slot=0;slot<6;++slot) {
        bool found=false;
        for(const auto& text:texts)found|=text.text=="glitch"&&
            text.call_x==81+78*c-ui::text_w("glitch")/2&&text.call_y==97+13*slot;
        CHECK(found);
    }
    const int top_objects=objects;performance_surface(false);
    maximum_shared_objects=std::max(maximum_shared_objects,top_objects+objects);CHECK(top_objects+objects<8192);
    f.refresh();performance_view_reset();host_time_ms=old_time;
}
void test_performance_frames() {
    test_performance_motion();
    test_performance_feedback();
    PerformanceFixture f;f.save_scene(0);f.save_scene(1);
    for(int c=0;c<4;++c){f.engine->chains[c].volume=q15(c*10922);f.engine->chains[c].mute=c==2;f.engine->pattern.length[c]=uint8_t(16+c*16);}
    f.refresh();performance_view_tick();const auto top=performance_surface(true);const int count=objects;const auto bottom=performance_surface(false);
    maximum_shared_objects=std::max(maximum_shared_objects,count+objects);CHECK(count+objects<8192);
    // Published scalars suffice: drawing neither reads engine nor takes audio guard.
    auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
    CHECK(performance_surface(true)==top&&performance_surface(false)==bottom);g_eng=engine;g_audio=audio;
}
void dump_performance_mixer(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\ttop_objects\tbottom_objects\tshared_objects\n";
    PerformanceFixture f;
    auto save=[&](const char* name){f.refresh();performance_view_tick();performance_surface(true);const int top=objects;dump_surface(directory,std::string(name)+"_top");performance_surface(false);CHECK(top+objects<8192);dump_surface(directory,std::string(name)+"_bottom");manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\n';};
    save("01_mixer");f.save_scene(0);f.engine->chains[0].units[0]->params[0].base=18000;f.save_scene(1);f.tap_at(160,211);
    for(int c=0;c<4;++c){f.engine->chains[c].volume=q15(c*10922);f.engine->chains[c].mute=c==2;f.engine->pattern.length[c]=uint8_t(16+c*16);f.engine->chain_peak[c]=q15(3000+c*6000);}
    f.engine->pattern.direction[1]=SequenceDirection::PingPong;f.engine->pattern.direction[3]=SequenceDirection::Reverse;g_sel_chain=1;
    f.engine->set_sequencer_enabled(true);f.engine->transport_play();save("02_live_levels");
}
void dump_performance_feedback(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.tsv");manifest<<"frame\ttop_objects\tbottom_objects\tshared_objects\n";
    PerformanceFixture f;
    auto save=[&](const char* name) {f.refresh();performance_view_tick();performance_surface(true);const int top=objects;dump_surface(directory,std::string(name)+"_top");performance_surface(false);CHECK(top+objects<8192);dump_surface(directory,std::string(name)+"_bottom");manifest<<name<<'\t'<<top<<'\t'<<objects<<'\t'<<top+objects<<'\n';};
    for(int c=0;c<4;++c)f.engine->chain_peak[c]=q15(32767-c*4000);
    f.refresh();performance_view_tick();
    for(int c=0;c<4;++c)f.engine->chain_peak[c]=q15(1800+c*1500);
    performance_view_touch_down(25,210);g_ui_frame+=12;performance_view_touch_hold(25,210);save("01_capture_half_peak_hold");
    g_ui_frame+=12;performance_view_touch_hold(25,210);f.release();save("02_saved_peak_hold");
    g_ui_frame+=96;save("03_peak_falling");
}
