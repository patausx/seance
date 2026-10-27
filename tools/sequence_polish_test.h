// Included by sequencer_test.cpp after the existing extent helpers.
// Narrow current-runtime checks, in-memory only: no media or fabricated UI.
namespace sequence_polish_cases {
u32 reference_ink(int peak) {
    const unsigned amount=(unsigned(std::min(32767,peak))*255u+16383u)/32767u;
    u32 result=m::ink()&0xff000000u;
    for(unsigned shift=0;shift<24;shift+=8) {
        const unsigned a=(m::ink()>>shift)&255u,b=(m::spot()>>shift)&255u;
        result|=((a*(255u-amount)+b*amount+127u)/255u)<<shift;
    }
    return result;
}
void reactive_scope() {
    Fixture f;
    const auto theme=palette::settings();
    for(auto id:{palette::Theme::Pink,palette::Theme::Graphite}) {
        palette::set(id);
        int previous=-1;
        for(int peak=0;peak<=32768;peak+=256) {
            const int amount=seq_wave::accent_amount(unsigned(peak));
            CHECK(amount>=previous);previous=amount;
        }
        CHECK(seq_wave::accent_amount(0)==0 && seq_wave::accent_amount(32768)==255);
        for(int sample:{0,4096,16384,32767,-16384,-32768}) {
            std::fill(std::begin(f.engine->scope),std::end(f.engine->scope),q15(sample));
            f.engine->scope_pos=173;f.refresh();const auto image=draw();
            const int y=22-sample*17/32767;
            const u32 expected=reference_ink(std::abs(sample));
            CHECK(seq_wave::ink(m::ink(),m::spot(),seq_wave::accent_amount(unsigned(std::abs(sample))))==expected);
            for(int x=0;x<320;++x) CHECK(image[y*320+x]==expected);
            g_ui_frame+=300;CHECK(draw()==image); // UI time cannot alter trace or tint
        }
        // One observed transient colors the entire trace, never selected x strips.
        std::fill(std::begin(f.engine->scope),std::end(f.engine->scope),q15(0));
        f.engine->scope[100]=32767;f.engine->scope_pos=0;f.refresh();const auto peak=draw();
        for(int x:{0,45,65,110,149,255,276,319}) CHECK(peak[22*320+x]==m::spot());
        std::fill(std::begin(f.engine->scope),std::end(f.engine->scope),q15(0));
        f.refresh();const auto quiet=draw();
        for(int x=0;x<320;++x) CHECK(quiet[22*320+x]==m::ink());
    }
    palette::set(theme);
}
void centered_rails() {
    static_assert(layout::VALUE_X0+layout::VALUE_X1==320,"centered native rail");
    Fixture f;f.mode(5);
    auto tap=[&](int x,int y=218) {f.frame();sequencer_view_touch_down(x,y);f.release();};
    for(int x:{38,160,282}) {
        tap(x);
        CHECK(f.engine->pattern.microtiming[0][0]==(x==38?-23:x==282?23:0));
        CHECK(!f.trig(0,0));
    }
    const auto saved=f.hash();
    for(int x:{0,37,283,315,319}) {tap(x);CHECK(f.hash()==saved);}
    tap(160,209);CHECK(f.hash()==saved);
    tap(160,227);CHECK(f.hash()==saved);
    tap(160);const auto time=draw();
    CHECK(time[213*320+160]==m::ghost());
    CHECK(time[218*320+160]==m::spot());
    f.frame();sequencer_view_touch_down(160,218);
    sequencer_view_touch_hold(0,218);CHECK(f.engine->pattern.microtiming[0][0]==-23);
    sequencer_view_touch_hold(319,218);CHECK(f.engine->pattern.microtiming[0][0]==23);f.release();
    for(int mode:{1,2}) {
        f.mode(mode);
        for(int x:{38,160,282}) {
            tap(x);draw();
            const int value=mode==1?sequencer_snapshot().sequence_pitch[0][0]:sequencer_snapshot().sequence_value[0][0];
            CHECK(value==(x==38?0:x==282?32767:16383));
            if(x==160)CHECK(pixels[218*320+160]==m::spot());
        }
        const auto before=f.hash();tap(315);CHECK(f.hash()==before);
    }
    // Categorical editors retain their old positions rather than inheriting the
    // new rail bounds. All score rectangles are still the original 19x31 bands.
    f.mode(4);const auto before=f.hash();tap(38);CHECK(f.hash()==before);
    tap(295);CHECK(f.engine->pattern.steps[0][0].condition()==SeqStep::Condition::NotFill);
    f.mode(3);tap(310);CHECK(f.engine->pattern.steps[0][0].retrig_count()==4);
    CHECK(layout::X0==8 && layout::PITCH==19 && layout::CELL_W==18 && layout::CELL_H==31);
    CHECK(layout::ROWS[0]==45 && layout::ROWS[1]==83 && layout::ROWS[2]==121 && layout::ROWS[3]==159);
}
int mark_x(int step) {return 16+(step/16)*48+(step%16)*2;}
void upper_context() {
    Fixture f;f.dense();f.mode(5);
    auto image=draw(true);
    CHECK(has_text("chain 01") && has_text("64 steps / reverse"));
    CHECK(has_text("midi 46") && has_text("01") && has_text("17") && has_text("33") && has_text("49"));
    CHECK(outside_rects==0 && outside_lines==0 && text_overlaps==0);
    check_hero("a#2",true); // old main note, not the rejected large time value
    image=draw(true);
    CHECK(image[185*400+mark_x(0)]==m::spot());
    for(int step=1;step<64;++step)
        CHECK(image[189*400+mark_x(step)]==(step<16?m::ink():m::ghost()));
    for(int x=16;x<192;++x) {
        CHECK(image[181*400+x]==m::paper_color(181)); // stopped: no imaginary head
        CHECK(image[184*400+x]==m::paper_color(184)); // no upper bracket
        CHECK(image[196*400+x]==m::paper_color(196)); // no lower bracket
    }
    f.engine->set_sequence_length(0,9);f.refresh();f.touch(0,15);image=draw(true);
    CHECK(image[189*400+mark_x(9)]==palette::subdued());
    CHECK(image[185*400+mark_x(15)]==m::spot());CHECK(has_text("strike / reverse / inactive"));
    f.tap(KEY_RIGHT);image=draw(true);
    CHECK(sequencer_selected_step()==16 && image[185*400+mark_x(16)]==m::spot());
    f.engine->set_sequence_length(0,64);f.engine->set_sequencer_enabled(true);f.engine->transport_play();f.refresh();
    image=draw(true);const int head=sequencer_snapshot().sequence_step[0];CHECK(head<64);
    CHECK(image[181*400+mark_x(head)]==palette::playhead());
    f.engine->chains[0].mute=true;f.refresh();image=draw(true);
    CHECK(image[181*400+mark_x(head)]==m::paper_color(181));
    f.engine->chains[0].mute=false;f.refresh();
    extent_test_open(f);extent_hold_key(f,KEY_DOWN);extent_test_close(f);
    image=draw(true);CHECK(sequencer_edit_bank()!=f.engine->active_pattern);
    for(int x=16;x<192;++x)CHECK(image[181*400+x]==m::paper_color(181));
    // All six longest route labels plus 'out' fit; the sixth gets its separator.
    for(int slot=0;slot<6;++slot)delete f.engine->replace_unit(0,slot,new StrainUnit);
    f.refresh();draw(true);int modules=0,separators=0;
    for(const auto& t:texts)if(t.y>=45 && t.y<53) {
        modules+=t.text=="strain";separators+=t.text=="/";
        CHECK(t.x>=24 && t.x+t.w<=376);
    }
    CHECK(modules==6 && separators==6 && has_text("out"));
    CHECK(text_overlaps==0 && outside_rects==0 && outside_lines==0);
    // Frozen top draw does not consult the engine or mutated publications.
    const auto frozen=pixels;Engine* engine=g_eng;g_eng=nullptr;g_ui_snapshot={};
    width=400;pixels.assign(400*240,0xff000000);objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();
    top_draw();CHECK(pixels==frozen);g_eng=engine;f.refresh();
    check_dual_budget();
}
} // namespace sequence_polish_cases
