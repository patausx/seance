// Included by sequencer_test.cpp: real controllers/engine/framebuffer, no copied UI.
namespace slice_cases {
namespace ss = seance::platform::ui3ds::seq_slice;
constexpr int CHAIN = 2, SLOT = 0;
int id(q15 value) { return value <= 0 ? 0 : ((unsigned)value * 17u) >> 15; }
q15 center(int value) { return value <= 0 ? 0 : (q15)((value * 32768 + 16384) / 17); }
const StepLock* lock(const Fixture& f, int chain = CHAIN, int step = -1, int slot = SLOT) {
    if (step < 0) step = sequencer_selected_step();
    const auto& event = f.engine->pattern.steps[chain][step];
    for (int i = 0; i < std::min((int)event.lock_count, SEQ_LOCKS); ++i)
        if (event.locks[i].slot == slot && event.locks[i].param == PlayUnit::P_SLICE) return &event.locks[i];
    return nullptr;
}
struct Setup : Fixture {
    SampleFixture pcm;
    PlayUnit& play = *static_cast<PlayUnit*>(engine->chains[CHAIN].units[SLOT]);
    Setup(int cuts = 4) {
        play.sample_slot = 63;
        // Deterministic REAL per-hit PCM: eight varied attacks and quadratic decays.
        // Four/eight/sixteen cut fixtures therefore cut the same audio differently.
        const int peaks[]={28000,14000,22000,17000,26000,15000,23000,19000};
        for (size_t i=0;i<pcm.sample.data.size();++i) {
            const int hit=(int)i/4000, phase=(int)i%4000, period=37+hit*11;
            const int triangle=(phase%period)*2-period;
            const int attack=std::min(32767,phase*32767/24);
            const int tail=(4000-phase)*32767/4000;
            const int envelope=(int)((int64_t)attack*tail/32767*tail/32767);
            pcm.sample.data[i]=(q15)((int64_t)triangle*peaks[hit]*envelope/period/32767);
        }
        set_cuts(cuts); mode(ss::MODE); touch(CHAIN, 0);
    }
    void set_cuts(int cuts) {
        std::fill(std::begin(pcm.sample.chops), std::end(pcm.sample.chops), UINT32_MAX);
        pcm.sample.slice_reverse_mask = 0;
        for (int i = 0; i < cuts; ++i) pcm.sample.chops[i] = (uint32_t)((uint64_t)i * pcm.sample.playable_frames() / cuts);
        refresh();
    }
    void stored(int value, int step = -1, int slot = SLOT) {
        if (step < 0) step = sequencer_selected_step();
        CHECK(engine->set_step_lock(CHAIN, step, slot, PlayUnit::P_SLICE, center(value))); refresh();
    }
    void control(int x, int y = 215) { frame(); sequencer_view_touch_down(x, y); release(); }
    int selected() const {
        const auto* l = lock(*this); return id(l ? l->value : play.params[PlayUnit::P_SLICE].base);
    }
    ss::Frame capture() { sequencer_capture_inspector(); return sequencer_slices(); }
    uint64_t sample_hash() const {
        uint64_t h = 1469598103934665603ull;
        auto add = [&](uint32_t v) { h = (h ^ v) * 1099511628211ull; };
        add(pcm.sample.channels); add(pcm.sample.logical_frames); add(pcm.sample.slice_reverse_mask);
        add(pcm.sample.loop_start); add(pcm.sample.loop_end); add(pcm.sample.root_note); add(pcm.sample.reversed);
        for (auto chop : pcm.sample.chops) add(chop);
        for (auto value : pcm.sample.data) add((uint16_t)value);
        return h;
    }
};
std::vector<u32> frozen_surface(bool top) {
    width = top ? 400 : 320; objects = outside_rects = outside_lines = text_overlaps = 0; texts.clear();
    pixels.assign(width * HEIGHT, 0xff000000u);
    if (top) top_draw(); else sequencer_view_draw();
    if (outside_rects || outside_lines || text_overlaps) {
        std::fprintf(stderr,"slice surface %s objects=%d outside=%d/%d overlaps=%d\n",top?"top":"bottom",objects,outside_rects,outside_lines,text_overlaps);
        for(size_t i=0;i<texts.size();++i)for(size_t j=i+1;j<texts.size();++j){const auto&a=texts[i];const auto&b=texts[j];
            if(a.x<b.x+b.w&&a.x+a.w>b.x&&a.y<b.y+b.h&&a.y+a.h>b.y)
                std::fprintf(stderr,"  text collision: %s (%d,%d %dx%d) / %s (%d,%d %dx%d)\n",a.text.c_str(),a.x,a.y,a.w,a.h,b.text.c_str(),b.x,b.y,b.w,b.h);
        }
    }
    CHECK(objects > 0 && outside_rects == 0 && outside_lines == 0 && text_overlaps == 0);
    for (const auto& t : texts) CHECK(t.x >= 0 && t.y >= 0 && t.x + t.w <= width && t.y + t.h <= HEIGHT);
    if (top) maximum_top_objects = std::max(maximum_top_objects, objects);
    else maximum_objects = std::max(maximum_objects, objects);
    return pixels;
}
void bounded_pair() {
    sequencer_capture_inspector();
    frozen_surface(true); const int top = objects;
    frozen_surface(false);
    maximum_shared_objects = std::max(maximum_shared_objects, top + objects);
    CHECK(top + objects < 8192);
}
}
void test_slice_chooser() {
    using namespace slice_cases;
    // Shared equal-angle geometry; south slice owns a normal hitbox now.
    CHECK(layout::SPOKE_X[7] == layout::RING_X);
    CHECK(layout::SPOKE_Y[7] == layout::RING_Y + layout::NODE_RADIUS);
    const int x=layout::MODE_X[7], y=layout::MODE_Y[7];
    Setup f; const auto before = f.hash(); const int dirty = mutations;
    f.mode(1); circle = {0,-127}; f.r_hold(); f.frame(KEY_CPAD_DOWN,KEY_R|KEY_CPAD_DOWN);
    f.frame(0,0,KEY_R|KEY_CPAD_DOWN); circle = {};
    CHECK(sequencer_selected_layer() == 7);
    for (auto xy : {std::pair<int,int>{x,y},{x+layout::MODE_W-1,y+layout::MODE_H-1},
                    {layout::SPOKE_X[7],layout::SPOKE_Y[7]}}) {
        f.mode(0); f.r_hold(); sequencer_view_touch_down(xy.first,xy.second); f.release();
        bounded_pair(); f.frame(0,0,KEY_R); CHECK(sequencer_selected_layer() == 7);
    }
    for (auto xy : {std::pair<int,int>{x-1,y+20},{x+layout::MODE_W,y+20},{160,y+layout::MODE_H}}) {
        f.mode(2); f.r_hold(); sequencer_view_touch_down(xy.first,xy.second); f.release(); f.frame(0,0,KEY_R);
        CHECK(sequencer_selected_layer() == 2);
    }
    f.mode(2); f.frame(KEY_R,KEY_R); f.frame(KEY_L,KEY_R|KEY_L); f.frame(0,0,KEY_R|KEY_L);
    CHECK(sequencer_selected_layer() == 7);
    f.mode(2); f.r_hold(); sequencer_view_touch_down(layout::SPOKE_X[7],layout::SPOKE_Y[7]); sequencer_view_cancel_input(); f.release(); f.frame(0,0,KEY_R);
    CHECK(sequencer_selected_layer() == 2 && f.hash() == before && mutations == dirty);
}
void test_slice_centers_sparse() {
    using namespace slice_cases;
    for (int raw = -32768; raw <= 32767; ++raw) CHECK(ss::decode((q15)raw) == id((q15)raw));
    for (int value = 0; value <= 16; ++value) {
        CHECK(ss::encode(value) == center(value)); CHECK(id(ss::encode(value)) == value);
    }
    Setup f(16); const auto pcm_before = f.sample_hash(); const q15 base = f.play.params[PlayUnit::P_SLICE].base;
    for (int i = 1; i <= 16; ++i) {
        f.tap(KEY_A); CHECK(f.selected() == i); CHECK(lock(f)->value == center(i));
        CHECK(!f.trig(CHAIN,0) && f.play.params[PlayUnit::P_SLICE].base == base);
        const auto p = play_window(f.pcm.sample,8192,0,32767,lock(f)->value,false);
        CHECK(p.slice == i && p.start == (uint32_t)(i-1)*2000 && p.end == (uint32_t)i*2000);
    }
    const auto end = f.hash(); const int dirty = mutations; f.tap(KEY_A); CHECK(f.hash() == end && mutations == dirty);
    for (int i = 15; i >= 0; --i) { f.tap(KEY_L|KEY_DDOWN); CHECK(f.selected() == i); }
    const auto beginning = f.hash(); f.tap(KEY_L|KEY_DDOWN); CHECK(f.hash() == beginning);
    // Out-of-order sparse IDs. Tiny interval 5 (one frame) and out-of-range 8 are invalid.
    f.set_cuts(0); auto& s = f.pcm.sample;
    s.chops[0]=12000; s.chops[3]=0; s.chops[4]=100; s.chops[7]=40000; s.chops[8]=101; s.chops[15]=24000;
    f.refresh(); const uint16_t mask = (1u<<0)|(1u<<3)|(1u<<8)|(1u<<15);
    CHECK(ss::valid_mask(s) == mask && ss::count(mask) == 4);
    const int ids[] = {1,4,9,16};
    for (int value : ids) { f.tap(KEY_A); CHECK(f.selected() == value); }
    for (int x=102; x<=267; ++x) {
        f.control(x); const int expected=ids[(x-102)*4/166]; CHECK(f.selected() == expected);
    }
    f.control(8); CHECK(f.selected() == 0); f.control(278); CHECK(f.selected() == 1);
    f.control(58); CHECK(f.selected() == 0);
    CHECK(f.play.params[PlayUnit::P_SLICE].base == base);
    // Pure helper agrees with the DSP's actual playable interval for every sparse ID.
    for (int i=1;i<=16;++i) {
        const auto p=play_window(s,8192,0,32767,center(i),false);
        CHECK((p.slice == i) == ((mask & (1u<<(i-1))) != 0));
    }
    // No PCM/chop mutation by all dense controls; restore metadata for hash comparison.
    f.set_cuts(16); CHECK(f.sample_hash() == pcm_before);
}
void test_slice_locks() {
    using namespace slice_cases;
    Setup f(8); const auto before=f.hash(); const int clean=mutations;
    for (int page=0;page<4;++page) {
        for (int c=0;c<4;++c) for (int s=0;s<16;++s) f.touch(c,s);
        f.tap(KEY_L|KEY_DRIGHT);
    }
    CHECK(f.hash()==before && mutations==clean && !f.play.params[PlayUnit::P_SLICE].audition_override);
    f.touch(CHAIN,0); f.control(8); CHECK(f.hash()==before && mutations==clean && !lock(f));
    f.stored(3); // Noncanonical legacy raw value in the same bucket must be a semantic no-op.
    CHECK(f.engine->set_step_lock(CHAIN,0,SLOT,PlayUnit::P_SLICE,(q15)(center(3)+20))); f.refresh();
    const auto legacy=f.hash(); const int legacy_dirty=mutations;
    f.control(102+2*166/8+5); CHECK(f.selected()==3 && f.hash()==legacy && mutations==legacy_dirty);
    f.engine->clear_step_locks(CHAIN,0);
    const int other[]={PlayUnit::P_SPEED,PlayUnit::P_START,PlayUnit::P_LEN,PlayUnit::P_LOOP};
    for (int p:other) CHECK(f.engine->set_step_lock(CHAIN,0,SLOT,p,10000));
    f.refresh();
    const auto full=f.hash(); const int full_dirty=mutations;
    f.tap(KEY_A); CHECK(f.hash()==full && mutations==full_dirty && g_seq_lock_flash>0);
    CHECK(!f.play.params[PlayUnit::P_SLICE].audition_override);
    CHECK(f.engine->remove_step_lock(CHAIN,0,SLOT,PlayUnit::P_LOOP)); f.refresh();
    f.tap(KEY_A); CHECK(f.selected()==1 && f.engine->pattern.steps[CHAIN][0].lock_count==4);
    f.tap(KEY_A); CHECK(f.selected()==2 && f.engine->pattern.steps[CHAIN][0].lock_count==4);
    f.tap(KEY_L|KEY_X); CHECK(!lock(f) && f.engine->pattern.steps[CHAIN][0].lock_count==3);
    for (int i=0;i<3;++i) { const auto& l=f.engine->pattern.steps[CHAIN][0].locks[i]; CHECK(l.param==other[i]&&l.value==10000); }
    const auto removed=f.hash(); const int removed_dirty=mutations; f.tap(KEY_L|KEY_X);
    CHECK(f.hash()==removed && mutations==removed_dirty && !f.trig(CHAIN,0));
    // Last page/last step preserves first-page metadata and stores only on step64.
    f.tap(KEY_L|KEY_DLEFT); f.touch(CHAIN,15); CHECK(sequencer_selected_step()==63);
    f.control(315); CHECK(lock(f,CHAIN,63) && !lock(f,CHAIN,0));
    const auto valid=f.hash();
    for (int x:{7,52,57,96,101,268,277,316}) f.control(x);
    f.control(267,209); f.control(267,227); CHECK(f.hash()==valid);
    f.control(267,226); CHECK(f.selected()==8 && f.hash()!=valid);
    f.frame(); sequencer_view_touch_down(102,210); const auto drag=f.hash();
    sequencer_view_cancel_input(); sequencer_view_touch_hold(267,215); f.release(); CHECK(f.hash()==drag);
    const auto chord=f.hash(); f.frame(KEY_R|KEY_L,KEY_R|KEY_L);
    sequencer_view_touch_down(267,215); f.release(); CHECK(f.hash()==chord); f.frame(0,0,KEY_R|KEY_L);
}
void test_slice_source_revalidation() {
    using namespace slice_cases;
    Setup f;
    auto attempt_noop=[&](){const auto h=f.hash();const int dirty=mutations;sequencer_view_buttons(KEY_A,KEY_A,0);CHECK(f.hash()==h&&mutations==dirty);};
    // Snapshot still points at PLAY, but a freshly installed overwrite-source masks it.
    f.engine->chains[CHAIN].units[2]=new FmUnit; attempt_noop();
    CHECK(f.capture().rows[CHAIN].slot<0);
    f.engine->chains[CHAIN].units[2]->bypass=true;
    sequencer_view_buttons(KEY_A,KEY_A,0); CHECK(lock(f) && id(lock(f)->value)==1);
    // A later PLAY owns the slice destination; downstream processors never steal it.
    auto* later=new PlayUnit; later->sample_slot=63; f.engine->chains[CHAIN].units[4]=later;
    f.engine->chains[CHAIN].units[5]=new FilterUnit;
    sequencer_view_buttons(KEY_A,KEY_A,0); CHECK(lock(f,CHAIN,0,4) && id(lock(f,CHAIN,0,4)->value)==1);
    CHECK(f.capture().rows[CHAIN].slot==4);
    // No fallback to upstream PLAY even if the last PLAY is missing its sample.
    later->sample_slot=-1; attempt_noop(); CHECK(f.capture().rows[CHAIN].slot==4 && !f.capture().loaded);
    later->sample_slot=64; attempt_noop();
    later->sample_slot=63; later->bypass=true;
    sequencer_view_buttons(KEY_A,KEY_A,0); CHECK(id(lock(f)->value)==2);
    // A stale source deletion cannot leave a dangling target.
    f.play.bypass=true; attempt_noop(); CHECK(f.capture().rows[CHAIN].slot<0);
    f.play.bypass=false; f.set_cuts(0); attempt_noop();
    f.set_cuts(4); f.pcm.sample.logical_frames=0; attempt_noop(); CHECK(!f.capture().loaded);
    f.pcm.sample.logical_frames=UINT32_MAX;
    // Several edits without worker publication must advance the real stored value.
    sequencer_view_buttons(KEY_A,KEY_A,0); sequencer_view_buttons(KEY_A,KEY_A,0);
    CHECK(id(lock(f)->value)==4);
}
void test_slice_audition() {
    using namespace slice_cases;
    Setup f; auto& p=f.play.params[PlayUnit::P_SLICE]; const q15 base=p.base;
    const auto pcm=f.sample_hash(); f.tap(KEY_A); CHECK(p.audition_override && id(p.audition)==1);
    CHECK(!f.engine->transport_playing && p.base==base && !f.trig(CHAIN,0));
    std::array<q15,512> out{}; f.engine->render(out.data(),256);
    CHECK(std::any_of(out.begin(),out.end(),[](q15 x){return x!=0;}));
    for (int i=0;i<30;++i) f.engine->render(out.data(),256);
    CHECK(p.audition_override); // 7936 frames: preview has not expired early.
    f.engine->render(out.data(),63); CHECK(p.audition_override); // 7999
    f.engine->render(out.data(),1); // exactly 8000 frames = existing 250 ms audition.
    CHECK(!p.audition_override && !f.engine->transport_playing && f.sample_hash()==pcm);
    f.tap(KEY_A); CHECK(p.audition_override); f.touch(CHAIN,1); CHECK(!p.audition_override);
    f.tap(KEY_A); CHECK(p.audition_override); sequencer_view_cancel_input(); CHECK(!p.audition_override);
    f.tap(KEY_A); CHECK(p.audition_override); f.mode(2); CHECK(!p.audition_override);
    f.mode(7); f.engine->chains[CHAIN].mute=true; f.tap(KEY_A);
    CHECK(!p.audition_override && f.selected()==3 && p.base==base && !f.trig(CHAIN,1));
    f.engine->chains[CHAIN].mute=false; f.tap(KEY_A); CHECK(p.audition_override);
    f.tap(KEY_L|KEY_X); CHECK(!p.audition_override && !lock(f));
}
void test_slice_capture() {
    using namespace slice_cases;
    Setup f(16); f.stored(3);
    auto frame=f.capture(); CHECK(sizeof(frame)<=1024 && frame.chain==CHAIN && frame.step==0 && frame.first==0);
    CHECK(frame.frames==32000 && frame.loaded && frame.rows[CHAIN].valid==65535 && frame.requested==3 && frame.applied==3 && frame.locked);
    CHECK(frame.window_valid && frame.pcm_reads>0 && frame.pcm_reads<=ss::WAVE_COLUMNS*ss::WAVE_PROBES*2);
    CHECK(frame.begin[2]<frame.end[2] && frame.rows[CHAIN].cells[0].slice==3 && frame.rows[CHAIN].cells[0].locked);
    f.pcm.sample.slice_reverse_mask=1u<<2; frame=f.capture(); CHECK(frame.reverse && (frame.reversed&(1u<<2)));
    CHECK(f.engine->set_step_lock(CHAIN,0,SLOT,PlayUnit::P_SPEED,-8192)); frame=f.capture();
    CHECK(!frame.reverse && frame.applied==3); // reverse cut * negative speed = forward.
    const auto h=f.hash(); const int dirty=mutations;
    const auto top=frozen_surface(true); const int top_objects=objects;
    const auto lower=frozen_surface(false); CHECK(top_objects+objects<8192);
    std::vector<q15> retired; retired.swap(f.pcm.sample.data);
    Engine* saved=g_eng; g_eng=nullptr;
    CHECK(frozen_surface(true)==top); CHECK(frozen_surface(false)==lower);
    g_eng=saved; retired.swap(f.pcm.sample.data);
    CHECK(f.hash()==h && mutations==dirty);
    // Re-read PCM/chops on every capture, including changes without a worker snapshot.
    std::fill(f.pcm.sample.data.begin(),f.pcm.sample.data.end(),0); frame=f.capture();
    CHECK(std::all_of(std::begin(frame.low),std::end(frame.low),[](int8_t x){return x==0;}));
    CHECK(std::all_of(std::begin(frame.high),std::end(frame.high),[](int8_t x){return x==0;}));
    CHECK(frozen_surface(true)!=top);
    f.pcm.sample.chops[2]=UINT32_MAX; frame=f.capture(); CHECK(frame.requested==3 && frame.applied==0 && !(frame.rows[CHAIN].valid&(1u<<2)));
    bounded_pair(); f.set_cuts(0); frame=f.capture(); CHECK(frame.loaded && frame.rows[CHAIN].valid==0); bounded_pair();
    f.pcm.sample.logical_frames=0; frame=f.capture(); CHECK(!frame.loaded && frame.pcm_reads==0); bounded_pair();
    // Stereo antiphase checks both channels, not a fabricated mono oscillator or a vanished sum.
    f.pcm.sample.channels=2; f.pcm.sample.logical_frames=UINT32_MAX;
    for(size_t i=0;i<f.pcm.sample.data.size();i+=2){f.pcm.sample.data[i]=24000;f.pcm.sample.data[i+1]=-24000;}
    f.set_cuts(8); frame=f.capture(); CHECK(frame.frames==16000 && frame.pcm_reads<=ss::WAVE_COLUMNS*ss::WAVE_PROBES*2);
    CHECK(std::any_of(std::begin(frame.low),std::end(frame.low),[](int8_t x){return x<0;}));
    CHECK(std::any_of(std::begin(frame.high),std::end(frame.high),[](int8_t x){return x>0;}));
    bounded_pair(); f.frame(KEY_L,KEY_L); bounded_pair(); CHECK(has_text("slice") || has_text("window")); f.frame(0,0,KEY_L);
    bounded_pair(); f.tap(KEY_L|KEY_DLEFT); f.touch(CHAIN,15); frame=f.capture(); CHECK(frame.first==48&&frame.step==63); bounded_pair();
    // Same slot replacement invalidates old envelope and chop extents immediately.
    trackr::synth::Sample replacement;replacement.data.assign(64000,16384);replacement.chops[15]=40000;
    std::swap(f.pcm.sample,replacement);frame=f.capture();
    CHECK(frame.frames==64000&&frame.rows[CHAIN].valid==(1u<<15)&&frame.pcm_reads==ss::WAVE_COLUMNS*ss::WAVE_PROBES);
    const int scaled = (int)(16384u * frame.visual_gain_q8 / 65536u);
    CHECK(frame.visual_gain_q8==384 && scaled==96); // adaptive boost precedes projection; PCM remains 16384.
    CHECK(std::all_of(std::begin(frame.low),std::end(frame.low),[&](int8_t x){return x==scaled;}));
    CHECK(std::all_of(std::begin(frame.high),std::end(frame.high),[&](int8_t x){return x==scaled;}));
    for(int channels:{0,3}){f.pcm.sample.channels=(uint8_t)channels;frame=f.capture();CHECK(!frame.loaded&&frame.pcm_reads==0&&frame.rows[CHAIN].valid==0);}
    f.pcm.sample.channels=1;f.pcm.sample.data.resize(1000000,16384);frame=f.capture();
    CHECK(frame.frames==1000000&&frame.pcm_reads==ss::WAVE_COLUMNS*ss::WAVE_PROBES);bounded_pair();
    f.mode(1);frame=f.capture();CHECK(frame.pcm_reads==0&&!frame.loaded); // old modes never scan PCM.
}
void test_slice_dispatch() {
    using namespace slice_cases;
    Setup f(4);
    for(int c=0;c<NUM_CHAINS;++c)if(c!=CHAIN)f.engine->chains[c].mute=true;
    for(size_t i=0;i<f.pcm.sample.data.size();++i)f.pcm.sample.data[i]=i<8000?12000:i<16000?-12000:i<24000?6000:-6000;
    auto& slice=f.play.params[PlayUnit::P_SLICE];slice.base=center(3);
    for(int step=0;step<3;++step)f.engine->pattern.steps[CHAIN][step].flags=SeqStep::Trig;
    f.stored(1,0);f.stored(2,1);f.engine->set_sequence_length(CHAIN,4);
    f.engine->set_bpm(120);f.engine->set_sequencer_enabled(true);f.engine->transport_reset();f.engine->transport_play();
    std::array<q15,512> out{};
    const auto hash=f.hash(),pcm=f.sample_hash();
    for(int step=0;step<3;++step){
        bool reached=false;
        for(int n=0;n<40;++n){f.engine->render(out.data(),256);f.refresh();if(g_ui_snapshot.sequence_step[CHAIN]==step&&f.engine->sequence_tick>=(uint64_t)step+1){reached=true;break;}}
        CHECK(reached&&f.play.playback_active());
        CHECK(slice.step_lock_override==(step<2));CHECK(id(slice.control_source())==step+1);
        CHECK(slice.base==center(3));
        const auto expected=play_window(f.pcm.sample,8192,0,32767,center(step+1),false);
        CHECK(f.play.visual_start()==expected.normalized_start&&f.play.visual_end()==expected.normalized_end);
        // Fresh next buffer is entirely inside this actual PLAY interval.
        f.engine->render(out.data(),256);
        int64_t sum=0;for(q15 value:out)sum+=value;CHECK(step==1?sum<0:sum>0);
    }
    CHECK(f.hash()==hash&&f.sample_hash()==pcm);f.engine->transport_stop();
}
void test_slice_persistence_dense() {
    using namespace slice_cases;
    Setup f(16);
    for(int c=0;c<NUM_CHAINS;++c){
        if(c!=CHAIN){for(int s=0;s<UNITS_PER_CHAIN;++s){delete f.engine->chains[c].units[s];f.engine->chains[c].units[s]=nullptr;}
            auto* play=new PlayUnit;play->sample_slot=63;f.engine->chains[c].units[0]=play;}
        f.engine->pattern.length[c]=64;
        for(int step=0;step<64;++step){
            auto& event=f.engine->pattern.steps[c][step];event.flags=SeqStep::Trig;event.set_retrig_count(step%4+1);event.set_condition((SeqStep::Condition)(step%8));
            f.engine->pattern.microtiming[c][step]=(int8_t)(step%47-23);
            CHECK(f.engine->set_step_lock(c,step,0,PlayUnit::P_SLICE,center(step%17)));
            CHECK(f.engine->set_step_lock(c,step,0,PlayUnit::P_SPEED,step%2?8192:-8192));
            CHECK(f.engine->set_step_lock(c,step,0,PlayUnit::P_START,8192));
            CHECK(f.engine->set_step_lock(c,step,0,PlayUnit::P_GAIN,24000));
        }
    }
    f.refresh();f.touch(CHAIN,15);f.tap(KEY_L|KEY_DLEFT);
    for(int selected=0;selected<=16;++selected){f.stored(selected);bounded_pair();}
    for(int c=0;c<NUM_CHAINS;++c){f.touch(c,15);bounded_pair();f.frame(KEY_L,KEY_L);bounded_pair();f.frame(0,0,KEY_L);}
    f.r_hold();bounded_pair();f.frame(0,0,KEY_R);
    const auto before=f.hash(), pcm=f.sample_hash();const int dirty=mutations;
    struct TempPatch {
        char path[64]="/tmp/seance-slice-locks-XXXXXX";
        TempPatch(){const int fd=mkstemp(path);CHECK(fd>=0);close(fd);}
        ~TempPatch(){std::remove(path);std::remove((std::string(path)+".tmp").c_str());}
    } saved;
    CHECK(patchio::save(*f.engine,saved.path));auto loaded=std::make_unique<Engine>();
    auto manifest=std::make_unique<patchio::SampleManifest>();CHECK(patchio::load(*loaded,saved.path,manifest.get()));
    CHECK(pattern_hash(loaded->pattern)==before&&f.hash()==before&&f.sample_hash()==pcm&&mutations==dirty);
    CHECK(manifest->has_chops);
    for(int i=0;i<16;++i)CHECK(manifest->chops[63][i]==f.pcm.sample.chops[i]);
}
void dump_slices(const std::filesystem::path& directory) {
    using namespace slice_cases;
    std::filesystem::create_directories(directory);
    std::ofstream manifest(directory/"manifest.tsv");
    manifest << "frame\tobjects\ttop_objects\tshared_objects\tclipped_primitives\ttop_clipped_primitives\ttext_overlaps\ttop_text_overlaps\tpcm_reads\tframe_bytes\trequested\tapplied\tvalid_mask\tloaded\treverse\tlocked\n";
    auto save=[&](Setup& f,const char* name){
        const auto sf=f.capture();
        Engine* saved=g_eng; g_eng=nullptr; // all visible surfaces use the one frozen capture.
        frozen_surface(true); dump_surface(directory,std::string(name)+"_top"); const int upper=objects;
        frozen_surface(false); dump_surface(directory,name); const int lower=objects; g_eng=saved;
        CHECK(upper+lower<8192); maximum_shared_objects=std::max(maximum_shared_objects,upper+lower);
        manifest<<name<<'\t'<<lower<<'\t'<<upper<<'\t'<<lower+upper<<"\t0\t0\t0\t0\t"<<sf.pcm_reads<<'\t'<<sizeof(sf)<<'\t'<<(int)sf.requested<<'\t'<<(int)sf.applied<<'\t'<<sf.rows[CHAIN].valid<<'\t'<<sf.loaded<<'\t'<<sf.reverse<<'\t'<<sf.locked<<'\n';
    };
    for(int cuts:{4,8,16}) {
        Setup f(cuts); for(int c=0;c<4;++c)for(int s=0;s<16;++s)if((s+c)%4==0)f.engine->pattern.steps[c][s].flags=SeqStep::Trig;
        for(int s=0;s<16;++s)if(s%3!=2)f.stored(1+s%cuts,s);
        f.touch(CHAIN,5); f.stored(std::min(cuts,3)); f.refresh();
        save(f, cuts==4?"01_four_cuts":cuts==8?"02_eight_cuts":"03_sixteen_cuts");
        if(cuts==8){f.pcm.sample.slice_reverse_mask=1u<<2;save(f,"04_reverse_cut");
            CHECK(f.engine->set_step_lock(CHAIN,5,SLOT,PlayUnit::P_SPEED,-8192));save(f,"05_reverse_negative_forward");}
    }
    Setup f(0); auto& s=f.pcm.sample;
    s.chops[0]=0;s.chops[1]=130;s.chops[2]=8000;s.chops[3]=8010;s.chops[4]=8011;s.chops[8]=29000;s.chops[15]=15000;
    f.stored(16);save(f,"06_uneven_tiny_sparse");f.stored(4);save(f,"07_missing_tiny_slice");
    f.engine->clear_step_locks(CHAIN,0);f.play.params[PlayUnit::P_SLICE].base=center(9);save(f,"08_base_slice");
    f.stored(0);CHECK(f.engine->set_step_lock(CHAIN,0,SLOT,PlayUnit::P_START,8192));CHECK(f.engine->set_step_lock(CHAIN,0,SLOT,PlayUnit::P_LEN,12000));save(f,"09_start_length_window");
    f.engine->clear_step_locks(CHAIN,0);for(int p:{PlayUnit::P_SPEED,PlayUnit::P_START,PlayUnit::P_LEN,PlayUnit::P_LOOP})CHECK(f.engine->set_step_lock(CHAIN,0,SLOT,p,8192));f.tap(KEY_A);save(f,"10_lock_full");
    f.engine->clear_step_locks(CHAIN,0);f.stored(16);f.engine->chains[CHAIN].mute=true;f.refresh();save(f,"11_muted");f.engine->chains[CHAIN].mute=false;
    f.r_hold();save(f,"12_eighth_mode_chooser");f.frame(0,0,KEY_R);
    f.frame(KEY_L,KEY_L);save(f,"13_hold_l");f.frame(0,0,KEY_L);
    f.tap(KEY_L|KEY_DLEFT);f.touch(CHAIN,15);f.stored(9);save(f,"14_page_four_step64");
    f.set_cuts(0);save(f,"15_no_chops");s.logical_frames=0;save(f,"16_no_sample");s.logical_frames=UINT32_MAX;
    f.play.sample_slot=-1;save(f,"17_missing_sample_slot");f.play.sample_slot=63;
    f.engine->chains[CHAIN].units[3]=new OscUnit;save(f,"18_masked_play_no_source");
    f.engine->chains[CHAIN].units[3]->bypass=true;f.play.bypass=true;save(f,"19_bypassed_play");
    std::printf("slice frames: %s (actual C++ frozen dual frames; software raster, not hardware)\n",directory.string().c_str());
}
