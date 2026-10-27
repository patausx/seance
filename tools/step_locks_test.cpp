// Reuse the real engine/controller/native draw harness, but run only this task.
#define main legacy_sequencer_test_main
#include "sequencer_test.cpp"
#undef main
#include "../platform/3ds/sequencer_step_edit.h"
#include "../platform/3ds/sequencer_lock_format.h"
namespace {
void lkey(Fixture& f,u32 key) {
    f.frame(KEY_L|key,KEY_L|key); f.frame(0,0,KEY_L|key); f.frame();
}
void select_cell(Fixture& f,int s) {
    f.frame(KEY_L,KEY_L); sequencer_view_touch_down(layout::X0+s*19+8,layout::ROWS[0]+8);
    sequencer_view_touch_release(true); f.frame(0,0,KEY_L); f.frame();
}
void capture() { sequencer_capture_inspector(); }
int lock_at(const SeqStep& s,int p) { return seq_edit::find(s,0,p); }
void selection_navigation() {
    Fixture f; f.mode(6); capture(); const auto original=f.hash();
    f.touch(0,15); lkey(f,KEY_DRIGHT); capture();
    CHECK(sequencer_selected_step()==16 && sequencer_current_page()==1);
    CHECK(sequencer_inspector().selection==(seq_edit::bit(15)|seq_edit::bit(16)));
    lkey(f,KEY_DLEFT); capture(); CHECK(sequencer_inspector().selection==seq_edit::bit(15));
    const auto selected=sequencer_inspector().selection;
    circle={0,0}; f.frame(); circle={0,-140}; f.frame(KEY_CPAD_DOWN,KEY_CPAD_DOWN); capture();
    CHECK(sequencer_inspector().param==1 && sequencer_selected_step()==15);
    CHECK(sequencer_inspector().selection==selected && f.hash()==original);
    f.frame(KEY_R|KEY_A,KEY_R|KEY_A); f.frame(0,0,KEY_R|KEY_A);
    f.frame(KEY_CPAD_DOWN,KEY_CPAD_DOWN); capture(); CHECK(sequencer_inspector().param==1);
    circle={}; f.frame(); circle={0,-140}; f.frame(KEY_CPAD_DOWN,KEY_CPAD_DOWN); capture();
    CHECK(sequencer_inspector().param==2); circle={}; f.frame();
    // A has navigation-only ownership; L+B never opens banks or clears data.
    f.tap(KEY_A); lkey(f,KEY_B); capture(); CHECK(!sequencer_inspector().selection && f.hash()==original);
    f.touch(0,2); // ordinary navigation clears the prior range
    f.frame(KEY_L,KEY_L);
    sequencer_view_touch_down(layout::X0+2*19+8,layout::ROWS[0]+8);
    sequencer_view_touch_hold(layout::X0+5*19+8,layout::ROWS[0]+8);
    sequencer_view_touch_hold(layout::X0+9*19+8,layout::ROWS[1]+8); // no row leak
    sequencer_view_touch_release(true); f.frame(0,0,KEY_L); f.frame(); capture();
    CHECK(sequencer_inspector().selection==seq_edit::range(2,5));
    CHECK(f.hash()==original);
    // Frozen native DTO/table: both palettes, no output files.
    for(int theme=0;theme<2;++theme) {
        palette::set((palette::Theme)theme);draw(true);CHECK(outside_rects==0);draw();
        // Full-height group outline is exactly one pixel on all four sides.
        // Adjacent selections retain their original one-pixel gap.
        for(int s=2;s<=5;++s) {
            const int x=layout::X0+s*19,y=layout::ROWS[0];
            for(int xx=x;xx<x+18;++xx) {
                CHECK(pixels[(y-2)*320+xx]==m::spot());
                CHECK(pixels[(y+30)*320+xx]==m::spot());
                CHECK(pixels[(y-3)*320+xx]!=m::spot());
            }
            for(int yy=y-2;yy<=y+30;++yy) {
                CHECK(pixels[yy*320+x]==m::spot() && pixels[yy*320+x+17]==m::spot());
                CHECK(pixels[yy*320+x+18]!=m::spot());
            }
        }
    }
    // The transport mark is not a thickened selection bottom: 1px paper gap.
    select_cell(f,0);f.tap(KEY_START);capture();draw();
    const auto& snap=sequencer_snapshot();CHECK(snap.sequencer_enabled && snap.transport_playing);
    for(int chain=0;chain<4;++chain) {
        const int step=snap.sequence_step[chain];CHECK(step<16);
        const int x=layout::X0+step*layout::PITCH+3,y=layout::ROWS[chain]+layout::CELL_H;
        CHECK(pixels[y*320+x]!=palette::playhead() && pixels[y*320+x]!=m::spot());
        CHECK(pixels[(y+1)*320+x]==palette::playhead() && pixels[(y+2)*320+x]==palette::playhead());
    }
    palette::set(palette::Theme::Pink);
}
void group_history() {
    Fixture f; f.mode(6); f.tap(KEY_A); capture(); // FM ratio: continuous
    const int p=sequencer_inspector().param;
    select_cell(f,0); select_cell(f,2); capture(); CHECK(sequencer_inspector().selected_count==2);
    const auto before=f.hash(); lkey(f,KEY_DUP); capture();
    CHECK(f.hash()!=before && sequencer_inspector().rows[p].covered==2);
    CHECK(sequencer_inspector().rows[p].minimum==sequencer_inspector().rows[p].maximum);
    const auto after=f.hash(); lkey(f,KEY_Y); CHECK(f.hash()==before);
    // Redo has its own visible touch control in held-L strip.
    f.frame(KEY_L,KEY_L); sequencer_view_touch_down(230,200); f.release(); f.frame(0,0,KEY_L); f.frame(); CHECK(f.hash()==after);
    // Entire transaction refuses if one member is full, including untouched flags/timing.
    auto& full=f.engine->pattern.steps[0][2]; full.lock_count=4;
    for(int i=0;i<4;++i) full.locks[i]={0,(uint8_t)(i+2),(q15)(1000+i)};
    const auto blocked=f.hash(); lkey(f,KEY_DUP); CHECK(f.hash()==blocked);
    capture(); CHECK(std::string(sequencer_inspector().status).find("full")!=std::string::npos);
    // Restore compatible external context, create unequal stored values then offset.
    full.lock_count=1; full.locks[0]={0,(uint8_t)p,12000};
    f.engine->pattern.steps[0][0].locks[0].value=8000;
    capture(); CHECK(sequencer_inspector().rows[p].minimum==8000 && sequencer_inspector().rows[p].maximum==12000);
    f.frame(KEY_L,KEY_L); sequencer_view_touch_down(130,200); f.release(); f.frame(0,0,KEY_L); f.frame();
    capture(); CHECK(sequencer_inspector().offset);
    lkey(f,KEY_DUP); CHECK(full.locks[0].value-f.engine->pattern.steps[0][0].locks[0].value==4000);
    // One touch gesture -> one undo; navigation cancellation rolls back lock drag.
    const auto drag_before=f.hash(); sequencer_view_touch_down(200,216); sequencer_view_touch_hold(240,216); f.release();
    CHECK(f.hash()!=drag_before); lkey(f,KEY_Y); CHECK(f.hash()==drag_before);
    sequencer_view_touch_down(210,216); sequencer_view_touch_hold(250,216); sequencer_view_cancel_input();
    CHECK(f.hash()==drag_before);
}
void sparse_clipboard() {
    Fixture f; f.mode(6);
    auto& a=f.engine->pattern.steps[0][0]; a.flags=SeqStep::Trig; a.set_retrig_count(3); a.set_condition(SeqStep::Condition::Fill);
    a.lock_count=1; a.locks[0]={0,1,7000}; f.engine->pattern.microtiming[0][0]=-17;
    f.engine->pattern.steps[0][2]=a; f.engine->pattern.steps[0][2].locks[0].value=11000;
    select_cell(f,0); select_cell(f,2); lkey(f,KEY_X);
    capture(); CHECK(sequencer_inspector().clipboard_count==2 && sequencer_inspector().selection);
    f.touch(0,8); capture(); CHECK(sequencer_inspector().paste_mask==(seq_edit::bit(8)|seq_edit::bit(10)));
    const auto before=f.hash(); lkey(f,KEY_A);
    CHECK(f.engine->pattern.steps[0][8].flags==a.flags && f.engine->pattern.microtiming[0][8]==-17);
    CHECK(f.engine->pattern.steps[0][10].locks[0].value==11000 && !f.engine->pattern.steps[0][9].lock_count);
    lkey(f,KEY_Y); CHECK(f.hash()==before);
    // Same-chain cross-bank paste must never write the playing bank.
    f.frame(KEY_B,KEY_B); for(int i=0;i<16;++i) f.frame(0,KEY_B);
    f.frame(KEY_DDOWN,KEY_B|KEY_DDOWN); f.frame(0,0,KEY_B); f.frame();
    capture(); CHECK(sequencer_edit_bank()==1 && f.engine->active_pattern==0);
    lkey(f,KEY_A); CHECK(f.hash()==before); CHECK(f.engine->sequence_pattern(1).steps[0][8].locks[0].value==7000);
    // Incompatible mode and row are explicit refusal, not lock retargeting.
    static_cast<FmUnit*>(f.engine->chains[0].units[0])->extended=false;
    const auto bank_before=pattern_hash(f.engine->sequence_pattern(1)); lkey(f,KEY_A);
    CHECK(pattern_hash(f.engine->sequence_pattern(1))==bank_before);
    capture(); CHECK(std::string(sequencer_inspector().status).find("incompatible")!=std::string::npos);
    // Pure bounded preflight corner cases without another engine fixture.
    seq_edit::Clipboard clip; clip.mask=seq_edit::bit(0)|seq_edit::bit(2); uint64_t out=0;
    CHECK(!clip.destinations(0,63,out) && out==0);
    CHECK(!clip.destinations(seq_edit::range(0,2),0,out));
    clip.mask=seq_edit::bit(0); CHECK(clip.destinations(seq_edit::range(0,63),0,out) && seq_edit::count(out)==64);
    clip.scope=seq_edit::Scope::Parameter; clip.slot=0; clip.param=1;
    seq_edit::Row row; row.steps[0].flags=SeqStep::Trig; row.timing[0]=-5;
    row.steps[0].lock_count=2; row.steps[0].locks[0]={0,1,6000}; row.steps[0].locks[1]={0,2,9000};
    CHECK(clip.paste(row,0,0,out)); CHECK(row.steps[0].lock_count==1 && row.steps[0].locks[0].param==2);
    CHECK(row.steps[0].flags==SeqStep::Trig && row.timing[0]==-5);
}
}
int main() {
    try {
        selection_navigation(); std::puts("ok lock selection/navigation/frozen table");
        group_history(); std::puts("ok atomic group edit/history/offset");
        sparse_clipboard(); std::puts("ok sparse clipboard/compatibility/edit bank");
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL %s\n",error.what()); return 1; }
    return 0;
}
