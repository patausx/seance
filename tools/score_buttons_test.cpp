// Focused score button/group contract. The legacy harness supplies hardware
// boundaries and real engine fixtures, but its old test main is never executed.
#define main legacy_sequencer_test_main
#include "sequencer_test.cpp"
#undef main
#include "../platform/3ds/sequencer_step_edit.h"
#include "../platform/3ds/sequence_note.h"
#include "../platform/3ds/editor_style.h"

static unsigned button_auditions=0,button_auditions_ok=0;
static int last_audition_chain=-1,last_audition_step=-1,last_audition_bank=-1;
extern "C" bool __real__ZN6seance6Engine22audition_sequence_stepEiii(Engine*,int,int,int);
extern "C" bool __wrap__ZN6seance6Engine22audition_sequence_stepEiii(Engine* e,int c,int s,int b) {
    ++button_auditions;last_audition_chain=c;last_audition_step=s;last_audition_bank=b;
    const bool ok=__real__ZN6seance6Engine22audition_sequence_stepEiii(e,c,s,b);
    button_auditions_ok+=ok;return ok;
}
namespace score_buttons {
// Test-only read access: explicit pointer-to-member instantiation does not alter
// Engine's class definition/layout or replace/modify its implementation.
struct SequenceRng { using type=uint32_t Engine::*;friend type member(SequenceRng); };
template<class Tag,typename Tag::type Pointer> struct ReadMember { friend typename Tag::type member(Tag){return Pointer;} };
template struct ReadMember<SequenceRng,&Engine::sequence_rng_>;
uint32_t rng(const Engine& e) { return e.*member(SequenceRng{}); }
unsigned cases=0,nonzero_pcm=0,native_surfaces=0,persisted_pairs=0;
std::filesystem::path evidence;
std::vector<char> persistent(Engine& e,const std::string& id) {
    const auto path=evidence/(id+".snc");CHECK(!std::filesystem::exists(path));
    CHECK(patchio::save(e,path.c_str()));std::ifstream in(path,std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
}
void capture() { sequencer_capture_inspector(); }
struct Score : Fixture {
    Score() { g_overlay=Overlay::None;g_drag=Drag::None;host_time_ms=10000;frame(); }
    Pattern& bank() { return engine->sequence_pattern(sequencer_edit_bank()); }
    SeqStep& step(int s=0) { return bank().steps[sequencer_selected_chain()][s]; }
    void lkey(u32 key) { frame(KEY_L|key,KEY_L|key);frame(0,0,KEY_L|key);frame();capture(); }
    void select(int local,int chain=0) {
        frame(KEY_L,KEY_L);sequencer_view_touch_down(layout::X0+local*layout::PITCH+8,layout::ROWS[chain]+8);
        sequencer_view_touch_release(true);frame(0,0,KEY_L);frame();capture();
    }
    void xy(int x,int y) { frame();sequencer_view_touch_down(x,y);release();frame();capture(); }
    void hold_b() { frame(KEY_B,KEY_B);for(int i=0;i<15;++i)frame(0,KEY_B); }
    void bank_next() { hold_b();frame(KEY_DDOWN,KEY_B|KEY_DDOWN);frame(0,0,KEY_B|KEY_DDOWN);frame();capture(); }
    void set_target(int p) {
        mode(6);capture();
        for(int i=0;i<PARAM_BANK_SIZE && sequencer_inspector().param!=p;++i) {
            circle={};frame();circle={0,-140};frame(KEY_CPAD_DOWN,KEY_CPAD_DOWN);circle={};frame();capture();
        }
        CHECK(sequencer_inspector().param==p);
    }
};
std::array<uint64_t,NUM_PATTERNS> banks(Score& f) {
    std::array<uint64_t,NUM_PATTERNS> out{};
    for(int b=0;b<NUM_PATTERNS;++b)out[b]=pattern_hash(f.engine->sequence_pattern(b));
    return out;
}
q15 value(Score& f,int step,int param,int slot=0) {
    const auto& s=f.bank().steps[sequencer_selected_chain()][step];const int i=seq_edit::find(s,slot,param);
    return i<0?f.engine->chains[sequencer_selected_chain()].units[slot]->params[param].base:s.locks[i].value;
}
void put(Score& f,int step,int p,q15 v,int slot=0) {
    auto& s=f.step(step);const int i=seq_edit::find(s,slot,p);
    CHECK(i>=0 || s.lock_count<SEQ_LOCKS);
    if(i>=0)s.locks[i].value=v;else s.locks[s.lock_count++]={(uint8_t)slot,(uint8_t)p,v};
}
std::vector<q15> bases(Score& f) {
    std::vector<q15> out;
    for(const auto& c:f.engine->chains)for(const Unit* u:c.units)if(u)for(int p=0;p<u->num_params;++p)out.push_back(u->params[p].base);
    return out;
}
void no_change(Score& f,const std::function<void()>& action,bool history=true) {
    capture();const auto before=banks(f);const auto base=bases(f);const int dirty=mutations;
    const bool undo=sequencer_inspector().can_undo,redo=sequencer_inspector().can_redo;
    const unsigned previews=button_auditions;action();capture();
    CHECK(banks(f)==before && bases(f)==base && mutations==dirty && button_auditions==previews);
    if(history)CHECK(sequencer_inspector().can_undo==undo && sequencer_inspector().can_redo==redo);
}
void assert_other_cells(const Pattern& before,const Pattern& after,int chain,uint64_t mask) {
    auto expected=before;
    for(int s=0;s<64;++s)if(mask&seq_edit::bit(s)) {
        expected.steps[chain][s]=after.steps[chain][s];expected.microtiming[chain][s]=after.microtiming[chain][s];
    }
    CHECK(pattern_hash(expected)==pattern_hash(after));
}
void single_buttons() {
    for(int mode=0;mode<7;++mode) {
        Score f;f.mode(mode);if(mode==6)f.set_target(FmUnit::P_RATIO);
        f.select(0);f.select(2);const uint64_t selection=sequencer_inspector().selection;
        f.step(2).set_retrig_count(2);f.step(2).set_condition(SeqStep::Condition::Half);
        f.bank().microtiming[0][2]=3;
        if(mode==1)put(f,2,FmUnit::P_PITCH,sequence_note::value(UnitKind::Fm,60));
        if(mode==2)put(f,2,FmUnit::P_GAIN,12000);
        if(mode==6)put(f,2,FmUnit::P_RATIO,12000);
        capture();const auto before=f.bank();const auto base=bases(f);const auto previews=button_auditions;
        const int p=mode==1?FmUnit::P_PITCH:mode==2?FmUnit::P_GAIN:FmUnit::P_RATIO;
        const q15 previous=value(f,2,p);const int dirty=mutations;
        f.tap(KEY_A);capture();CHECK(mutations==dirty+1 && sequencer_inspector().selection==selection);
        assert_other_cells(before,f.bank(),0,seq_edit::bit(2));CHECK(bases(f)==base);
        if(mode==0)CHECK(f.step(2).flags&SeqStep::Trig);
        if(mode==1)CHECK(sequence_note::midi(UnitKind::Fm,value(f,2,p))==61);
        if(mode==2 || mode==6)CHECK(value(f,2,p)>previous);
        if(mode==3)CHECK(f.step(2).retrig_count()==3);
        if(mode==4)CHECK(f.step(2).condition()==SeqStep::Condition::Quarter);
        if(mode==5)CHECK(f.bank().microtiming[0][2]==4);
        if(mode==6)CHECK(sequencer_inspector().param==FmUnit::P_RATIO && sequencer_inspector().slot==0);
        CHECK(button_auditions==previews+(mode==1));
        const auto plus=banks(f);f.frame(KEY_B,KEY_B);CHECK(banks(f)==plus);
        for(int i=0;i<4;++i)f.frame(0,KEY_B);
        CHECK(banks(f)==plus);
        f.frame(0,0,KEY_B);f.frame();capture();
        CHECK(sequencer_inspector().selection==selection && bases(f)==base);
        assert_other_cells(before,f.bank(),0,seq_edit::bit(2));
        if(mode==0)CHECK(!(f.step(2).flags&SeqStep::Trig));
        if(mode==1)CHECK(sequence_note::midi(UnitKind::Fm,value(f,2,p))==60);
        if(mode==2 || mode==6)CHECK(value(f,2,p)==previous);
        if(mode==3)CHECK(f.step(2).retrig_count()==2);
        if(mode==4)CHECK(f.step(2).condition()==SeqStep::Condition::Half);
        if(mode==5)CHECK(f.bank().microtiming[0][2]==3);
        CHECK(button_auditions==previews+2*(mode==1));++cases;
    }
}
void clamps_and_empty_group() {
    for(int mode=0;mode<8;++mode) {
        Score f;f.mode(mode);if(mode==6)f.set_target(FmUnit::P_RATIO);
        no_change(f,[&]{f.lkey(KEY_DUP);});capture();CHECK(sequencer_inspector().status[0]);
        no_change(f,[&]{f.lkey(KEY_DDOWN);});
        f.select(0);f.select(2);
        no_change(f,[&]{f.lkey(KEY_DUP|KEY_DDOWN);});
        f.lkey(KEY_B);CHECK(!sequencer_inspector().selection);
        if(mode==7) { no_change(f,[&]{f.tap(KEY_A);f.tap(KEY_B);});continue; }
        for(bool high:{false,true}) {
            if(mode==0)f.step().flags=high?SeqStep::Trig:0;
            if(mode==1)put(f,0,FmUnit::P_PITCH,sequence_note::value(UnitKind::Fm,high?sequence_note::maximum(UnitKind::Fm):sequence_note::minimum(UnitKind::Fm)));
            if(mode==2)put(f,0,FmUnit::P_GAIN,high?32767:0);
            if(mode==3)f.step().set_retrig_count(high?4:1);
            if(mode==5)f.bank().microtiming[0][0]=high?23:-23;
            if(mode==6)put(f,0,FmUnit::P_RATIO,high?32767:0);
            // Focus remained 2 after selection: move to 0 through actual D-pad.
            while(sequencer_selected_step()!=0)f.tap(KEY_DLEFT);
            capture();if(mode!=4)no_change(f,[&]{f.tap(high?KEY_A:KEY_B);},false);
        }
        if(mode==4) {
            f.step().set_condition(SeqStep::Condition::NotFill);f.tap(KEY_A);CHECK(f.step().condition()==SeqStep::Condition::Always);
            f.tap(KEY_B);CHECK(f.step().condition()==SeqStep::Condition::NotFill);
        }
        ++cases;
    }
}
void group_modes_history() {
    for(int mode=0;mode<7;++mode) {
        Score f;f.mode(mode);if(mode==6)f.set_target(FmUnit::P_RATIO);
        for(int s:{0,2}) {
            f.step(s).set_retrig_count(s?3:1);f.step(s).set_condition(s?SeqStep::Condition::NotFill:SeqStep::Condition::Half);
            f.bank().microtiming[0][s]=s?23:-10;
            if(mode==1)put(f,s,FmUnit::P_PITCH,sequence_note::value(UnitKind::Fm,s?72:48));
            if(mode==2)put(f,s,FmUnit::P_GAIN,s?32760:10000);
            if(mode==6)put(f,s,FmUnit::P_RATIO,s?12000:8000);
        }
        f.select(0);f.select(2);const auto selection=sequencer_inspector().selection;
        const auto before=f.bank();const auto base=bases(f);const auto preview=button_auditions;const int dirty=mutations;
        f.lkey(KEY_DUP);CHECK(mutations==dirty+1 && button_auditions==preview && bases(f)==base);
        CHECK(sequencer_inspector().selection==selection && sequencer_inspector().can_undo);
        assert_other_cells(before,f.bank(),0,selection);
        if(mode==0)CHECK((f.step(0).flags&SeqStep::Trig)&&(f.step(2).flags&SeqStep::Trig));
        if(mode==1)CHECK(sequence_note::midi(UnitKind::Fm,value(f,0,FmUnit::P_PITCH))==49 && sequence_note::midi(UnitKind::Fm,value(f,2,FmUnit::P_PITCH))==73);
        if(mode==2)CHECK(value(f,0,FmUnit::P_GAIN)>10000 && value(f,2,FmUnit::P_GAIN)==32767);
        if(mode==3)CHECK(f.step(0).retrig_count()==2 && f.step(2).retrig_count()==4);
        if(mode==4)CHECK(f.step(0).condition()==SeqStep::Condition::Quarter && f.step(2).condition()==SeqStep::Condition::Always);
        if(mode==5)CHECK(f.bank().microtiming[0][0]==-9 && f.bank().microtiming[0][2]==23);
        if(mode==6)CHECK(value(f,0,FmUnit::P_RATIO)==8328 && value(f,2,FmUnit::P_RATIO)==12328);
        const auto after=banks(f);f.lkey(KEY_Y);CHECK(pattern_hash(f.bank())==pattern_hash(before));
        CHECK(!sequencer_inspector().can_undo && sequencer_inspector().can_redo);
        // Existing visible redo control, not a direct history API.
        f.frame(KEY_L,KEY_L);sequencer_view_touch_down(230,200);f.release();f.frame(0,0,KEY_L);f.frame();capture();
        CHECK(banks(f)==after && sequencer_inspector().can_undo && !sequencer_inspector().can_redo);
        f.lkey(KEY_DDOWN);CHECK(button_auditions==preview && bases(f)==base);++cases;
    }
}
void atomic_capacity_and_bases() {
    for(int mode:{1,2,6}) {
        Score f;f.mode(mode);if(mode==6)f.set_target(FmUnit::P_RATIO);
        const int p=mode==1?FmUnit::P_PITCH:mode==2?FmUnit::P_GAIN:FmUnit::P_RATIO;
        auto& param=f.engine->chains[0].units[0]->params[p];param.base=mode==1?sequence_note::value(UnitKind::Fm,60):9000;
        param.effective=27000;param.target=25000;
        f.select(0);f.select(2);const auto base=bases(f);f.lkey(KEY_DUP);
        CHECK(seq_edit::find(f.step(0),0,p)>=0 && seq_edit::find(f.step(2),0,p)>=0 && bases(f)==base);
        CHECK(value(f,0,p)==value(f,2,p));f.lkey(KEY_Y);CHECK(!f.step(0).lock_count && !f.step(2).lock_count);
        auto& full=f.step(2);full.lock_count=4;int n=0;
        for(int pp=0;pp<6 && n<4;++pp)if(pp!=p)full.locks[n++]={0,(uint8_t)pp,5000};
        capture();no_change(f,[&]{f.lkey(KEY_DUP);});CHECK(std::string(sequencer_inspector().status).find("full")!=std::string::npos);
        CHECK(!f.step(0).lock_count);++cases;
    }
    for(int mode:{1,2,6,7}) {
        Score f;delete f.engine->replace_unit(0,0,nullptr);f.mode(mode);f.select(0);f.select(2);
        no_change(f,[&]{f.lkey(KEY_DUP);f.lkey(KEY_DDOWN);});CHECK(sequencer_inspector().status[0]);++cases;
    }
}
void note_sources() {
    for(int source=0;source<8;++source) {
        const UnitKind kinds[]={UnitKind::Fm,UnitKind::Osc,UnitKind::Inst,UnitKind::Bird,UnitKind::Grain,UnitKind::SamplePlayer,UnitKind::Stranular,UnitKind::SamplePlayer};
        const UnitKind kind=kinds[source];Score f;Unit* u=nullptr;
        switch(kind) {
        case UnitKind::Fm:u=new FmUnit;break;case UnitKind::Osc:u=new OscUnit;break;case UnitKind::Inst:u=new InstUnit;break;
        case UnitKind::Bird:u=new BirdUnit;break;case UnitKind::Grain:u=new GrainUnit;break;case UnitKind::SamplePlayer:u=new PlayUnit;break;
        default:u=new StranularUnit;break;
        }
        if(source==7)static_cast<PlayUnit*>(u)->set_pitch_mode(PlayUnit::PitchMode::Normal);
        delete f.engine->replace_unit(0,0,u);f.refresh();f.mode(1);capture();
        const int p=sequencer_snapshot().sequence_pitch_param[0];CHECK(p>=0);
        const bool absolute=sequence_note::absolute(kind,p);const int low=kind==UnitKind::Bird?80:48;
        for(int s:{0,2})put(f,s,p,absolute?sequence_note::value(kind,low+s,kind==UnitKind::Grain?-8192:0):kind==UnitKind::Stranular?q15((s?6:-6)*32767/24):q15(s?-8192:-4096));
        f.select(0);f.select(2);const q15 a=value(f,0,p),b=value(f,2,p);const auto base=bases(f);const unsigned previews=button_auditions;
        f.lkey(KEY_DUP);CHECK(bases(f)==base && button_auditions==previews);
        if(absolute) {
            CHECK(sequence_note::midi(kind,value(f,0,p))==low+1 && sequence_note::midi(kind,value(f,2,p))==low+3);
            if(kind==UnitKind::Grain)CHECK(value(f,0,p)<0 && value(f,2,p)<0);
        } else if(kind==UnitKind::Stranular)CHECK(value(f,0,p)>a && value(f,2,p)>b);
        else CHECK(value(f,0,p)<a && value(f,2,p)<b); // reverse speed retains its sign
        ++cases;
    }
}
void slice_sparse() {
    SampleFixture sample;sample.sample.channels=1;
    for(auto& chop:sample.sample.chops)chop=UINT32_MAX;
    sample.sample.chops[1]=0;sample.sample.chops[6]=8000;sample.sample.chops[15]=16000;
    Score f;auto* play=new PlayUnit;play->sample_slot=63;delete f.engine->replace_unit(0,0,play);f.refresh();f.mode(7);
    CHECK(seq_slice::valid_mask(sample.sample)==((1u<<1)|(1u<<6)|(1u<<15)));
    for(int want:{2,7,16}) { const auto previews=button_auditions;f.tap(KEY_A);CHECK(seq_slice::decode(value(f,0,PlayUnit::P_SLICE))==want && button_auditions==previews+1); }
    no_change(f,[&]{f.tap(KEY_A);});
    for(int want:{7,2,0}) { f.tap(KEY_B);CHECK(seq_slice::decode(value(f,0,PlayUnit::P_SLICE))==want); }
    no_change(f,[&]{f.tap(KEY_B);});
    put(f,0,PlayUnit::P_SLICE,seq_slice::encode(2));put(f,2,PlayUnit::P_SLICE,seq_slice::encode(7));
    f.select(0);f.select(2);const auto previews=button_auditions;const auto before=f.bank();f.lkey(KEY_DUP);
    CHECK(seq_slice::decode(value(f,0,PlayUnit::P_SLICE))==7 && seq_slice::decode(value(f,2,PlayUnit::P_SLICE))==16 && button_auditions==previews);
    f.lkey(KEY_Y);CHECK(pattern_hash(f.bank())==pattern_hash(before));
    f.lkey(KEY_DDOWN);CHECK(seq_slice::decode(value(f,0,PlayUnit::P_SLICE))==0 && seq_slice::decode(value(f,2,PlayUnit::P_SLICE))==2);
    for(bool high:{false,true}) {
        put(f,0,PlayUnit::P_SLICE,seq_slice::encode(high?16:0));put(f,2,PlayUnit::P_SLICE,seq_slice::encode(high?16:0));
        no_change(f,[&]{f.lkey(high?KEY_DUP:KEY_DDOWN);},false);
    }
    f.step(0).lock_count=0;f.step(2).lock_count=4;
    for(int i=0;i<4;++i)f.step(2).locks[i]={0,(uint8_t)i,9000};
    no_change(f,[&]{f.lkey(KEY_DUP);},false);CHECK(!f.step(0).lock_count);++cases;
}
void lock_relative_and_comp() {
    Score f;f.set_target(FmUnit::P_RATIO);put(f,0,FmUnit::P_RATIO,8000);put(f,2,FmUnit::P_RATIO,12000);
    f.select(0);f.select(2);
    for(int trial=0;trial<2;++trial) {
        if(trial) { f.frame(KEY_L,KEY_L);sequencer_view_touch_down(130,200);f.release();f.frame(0,0,KEY_L);f.frame();capture(); }
        const int a=value(f,0,FmUnit::P_RATIO),b=value(f,2,FmUnit::P_RATIO);f.lkey(KEY_DUP);
        CHECK(value(f,0,FmUnit::P_RATIO)==a+328 && value(f,2,FmUnit::P_RATIO)==b+328);
    }
    // Existing touch OFFSET remains relative to the gesture start; SET assigns
    // one absolute value. This is intentionally distinct from button nudges.
    CHECK(sequencer_inspector().offset);const auto touch_before=banks(f);
    const int a=value(f,0,FmUnit::P_RATIO),b=value(f,2,FmUnit::P_RATIO);
    sequencer_view_touch_down(200,216);sequencer_view_touch_hold(220,216);f.release();capture();
    CHECK(value(f,0,FmUnit::P_RATIO)>a && value(f,2,FmUnit::P_RATIO)>b && value(f,2,FmUnit::P_RATIO)-value(f,0,FmUnit::P_RATIO)==4000);
    f.lkey(KEY_Y);CHECK(banks(f)==touch_before);
    f.frame(KEY_L,KEY_L);sequencer_view_touch_down(130,200);f.release();f.frame(0,0,KEY_L);f.frame();capture();
    CHECK(!sequencer_inspector().offset);f.xy(200,216);CHECK(value(f,0,FmUnit::P_RATIO)==value(f,2,FmUnit::P_RATIO));
    f.lkey(KEY_Y);CHECK(banks(f)==touch_before);++cases;
    { Score s;delete s.engine->replace_unit(0,0,new CompUnit);s.refresh();s.set_target(CompUnit::P_RELEASE);
      put(s,0,CompUnit::P_RELEASE,10000);put(s,2,CompUnit::P_RELEASE,10000);
      put(s,0,CompUnit::P_CLOCK,0);put(s,2,CompUnit::P_CLOCK,32767);s.select(0);s.select(2);
      const int index=CompUnit::release_index(10000);s.lkey(KEY_DUP);
      CHECK(value(s,0,CompUnit::P_RELEASE)==10328 && CompUnit::release_index(value(s,2,CompUnit::P_RELEASE))==index+1);
      CHECK(value(s,0,CompUnit::P_CLOCK)==0 && value(s,2,CompUnit::P_CLOCK)==32767);++cases; }
    { Score s;delete s.engine->replace_unit(0,0,new NoiseUnit);s.refresh();s.set_target(NoiseUnit::P_COLOUR);
      put(s,0,NoiseUnit::P_COLOUR,0);put(s,2,NoiseUnit::P_COLOUR,18000);s.select(0);s.select(2);s.lkey(KEY_DUP);
      CHECK(int(value(s,0,NoiseUnit::P_COLOUR))*4/32768==1 && int(value(s,2,NoiseUnit::P_COLOUR))*4/32768==3);++cases; }
}
void clipboard_and_hold() {
    Score f;f.mode(2);put(f,0,FmUnit::P_GAIN,12000);f.select(0);f.select(2);f.lkey(KEY_X);
    const int clipboard=sequencer_inspector().clipboard_count;CHECK(clipboard==2);const auto selection=sequencer_inspector().selection;
    f.tap(KEY_A);f.tap(KEY_B);f.tap(KEY_Y);capture();CHECK(sequencer_inspector().clipboard_count==clipboard && sequencer_inspector().selection==selection);
    no_change(f,[&]{f.hold_b();f.frame(0,0,KEY_B);f.frame();});capture();CHECK(sequencer_inspector().clipboard_count==clipboard);
    // Full hold opens at >=15 elapsed frames; shorter 14-frame release decrements.
    const q15 v=value(f,2,FmUnit::P_GAIN);f.frame(KEY_B,KEY_B);for(int i=0;i<13;++i)f.frame(0,KEY_B);f.frame(0,0,KEY_B);f.frame();CHECK(value(f,2,FmUnit::P_GAIN)<v);
    // Unsigned UI timestamp wrap must preserve the 15-frame threshold.
    g_ui_frame=UINT32_MAX-7;no_change(f,[&]{f.hold_b();f.frame(0,0,KEY_B);f.frame();});
    // A/Y/touch held tails from B ownership cannot leak to score edits/audition.
    no_change(f,[&]{f.hold_b();f.frame(0,KEY_A|KEY_Y,KEY_B);f.frame(KEY_A|KEY_Y,KEY_A|KEY_Y);sequencer_view_touch_down(200,216);f.release();f.frame();});
    no_change(f,[&]{f.frame(KEY_B,KEY_B);sequencer_view_cancel_input();f.frame(0,0,KEY_B);f.frame();});
    no_change(f,[&]{f.frame(KEY_B,KEY_B);f.frame(KEY_SELECT,KEY_B|KEY_SELECT);f.frame(0,0,KEY_B|KEY_SELECT);f.frame();});
    no_change(f,[&]{f.r_hold();f.frame(KEY_B,KEY_R|KEY_B);f.frame(0,0,KEY_R|KEY_B);f.frame();});
    f.lkey(KEY_B);CHECK(!sequencer_inspector().selection && sequencer_inspector().clipboard_count==clipboard);
    f.select(0);f.select(2);const auto group=sequencer_inspector().selection;f.lkey(KEY_DUP);capture();CHECK(sequencer_inspector().can_undo);
    const auto all=banks(f);const bool undo=sequencer_inspector().can_undo;const int dirty=mutations;
    f.frame(KEY_L,KEY_L);sequencer_view_touch_down(270,233);f.release();capture();
    CHECK(!sequencer_inspector().clipboard_count && sequencer_inspector().selection==group && sequencer_inspector().can_undo==undo && banks(f)==all && mutations==dirty);
    f.frame(0,0,KEY_L);f.frame();const auto before=f.bank();f.tap(KEY_X);capture();
    CHECK(!sequencer_inspector().selection && !f.step(2).flags && !f.step(2).lock_count && !f.bank().microtiming[0][2]);
    assert_other_cells(before,f.bank(),0,seq_edit::bit(2));++cases;
    { Score s;s.mode(2);s.lkey(KEY_X);const int clip=sequencer_inspector().clipboard_count;
      const auto original=s.bank();const int original_dirty=mutations;s.hold_b();
      s.frame(KEY_DRIGHT,KEY_B|KEY_DRIGHT);s.frame(KEY_A,KEY_B|KEY_A);
      CHECK(pattern_hash(s.bank())==pattern_hash(original));s.frame(0,0,KEY_B|KEY_A);s.frame();capture();
      auto expected=original;expected.length[0]++;expected.direction[0]=SequenceDirection::Reverse;
      CHECK(pattern_hash(s.bank())==pattern_hash(expected) && mutations==original_dirty+1 && sequencer_inspector().clipboard_count==clip);
      no_change(s,[&]{s.frame(KEY_R|KEY_B,KEY_R|KEY_B);s.frame(0,0,KEY_R|KEY_B);s.frame();});
      ++cases; }
}
void cross_page_bank() {
    Score f;f.mode(5);f.bank_next();CHECK(sequencer_edit_bank()==1 && f.engine->active_pattern==0);
    f.tap(KEY_DLEFT);CHECK(sequencer_selected_step()==63);
    // Real range navigation traverses all four pages; toggle a middle member on
    // the final page to make a sparse explicit group spanning 0..63.
    for(int i=0;i<63;++i)f.lkey(KEY_DLEFT);
    f.select(2);capture();const auto mask=~seq_edit::bit(2);CHECK(sequencer_inspector().selection==mask);
    for(int s=0;s<64;++s)f.bank().microtiming[0][s]=s%2?23:-10;
    const auto all=banks(f);const auto before=f.bank();f.lkey(KEY_DUP);
    for(int s=0;s<64;++s)CHECK(f.bank().microtiming[0][s]==(s==2?-10:s%2?23:-9));
    assert_other_cells(before,f.bank(),0,mask);for(int b=0;b<NUM_PATTERNS;++b)if(b!=1)CHECK(pattern_hash(f.engine->sequence_pattern(b))==all[b]);
    f.lkey(KEY_Y);CHECK(banks(f)==all);
    f.mode(1);capture();CHECK(sequencer_inspector().selection==mask);
    for(int s=0;s<64;++s)put(f,s,FmUnit::P_PITCH,sequence_note::value(UnitKind::Fm,48+s%24));
    const auto notes=banks(f);const auto previews=button_auditions;const auto base=bases(f);f.lkey(KEY_DUP);
    for(int s=0;s<64;++s)CHECK(sequence_note::midi(UnitKind::Fm,value(f,s,FmUnit::P_PITCH))==48+s%24+(s!=2));
    CHECK(button_auditions==previews && bases(f)==base);for(int b=0;b<NUM_PATTERNS;++b)if(b!=1)CHECK(pattern_hash(f.engine->sequence_pattern(b))==notes[b]);
    f.lkey(KEY_Y);CHECK(banks(f)==notes);
    auto& full=f.step(63);full.lock_count=4;for(int i=0;i<4;++i)full.locks[i]={0,(uint8_t)(i+1),7000};
    no_change(f,[&]{f.lkey(KEY_DUP);},false);CHECK(std::string(sequencer_inspector().status).find("full")!=std::string::npos);++cases;
}
void audition() {
    for(int mode=0;mode<8;++mode) {
        Score f;f.mode(mode);f.bank_next();put(f,0,FmUnit::P_PITCH,sequence_note::value(UnitKind::Fm,67));
        f.step(0).set_condition(SeqStep::Condition::Quarter);f.step(0).set_retrig_count(4);CHECK(!(f.step(0).flags&SeqStep::Trig));
        f.select(0);f.select(2);f.select(2); // retain group {0}, focus2; then focus0 without editing
        f.select(0);f.select(0);capture();CHECK(sequencer_selected_step()==0);
        const auto stored=banks(f);const auto base=bases(f);const int dirty=mutations;const auto ui_rng=g_ui_rng;
        const auto tick=f.engine->sequence_tick,clock=f.engine->transport_frame;const auto previews=button_auditions,accepted=button_auditions_ok;
        const uint32_t random=rng(*f.engine);const auto selected=sequencer_inspector().selection;
        const auto doc=persistent(*f.engine,"audition-"+std::to_string(mode)+"-before");
        f.frame(KEY_Y,KEY_Y);for(int i=0;i<4;++i)f.frame(0,KEY_Y);f.frame(0,0,KEY_Y);capture();
        CHECK(button_auditions==previews+1 && button_auditions_ok==accepted+1);
        CHECK(last_audition_bank==1 && last_audition_chain==0 && last_audition_step==0);
        CHECK(banks(f)==stored && bases(f)==base && mutations==dirty && g_ui_rng==ui_rng);
        CHECK(rng(*f.engine)==random && sequencer_inspector().selection==selected);
        CHECK(doc==persistent(*f.engine,"audition-"+std::to_string(mode)+"-after"));++persisted_pairs;
        CHECK(!f.engine->transport_playing && f.engine->sequence_tick==tick && f.engine->transport_frame==clock);
        CHECK(f.engine->chains[0].units[0]->params[FmUnit::P_PITCH].audition==sequence_note::value(UnitKind::Fm,67));
        q15 pcm[1024]={};f.engine->render(pcm,512);unsigned count=0;for(q15 v:pcm)count+=v!=0;
        CHECK(count>0);nonzero_pcm+=count;CHECK(banks(f)==stored && bases(f)==base);++cases;
    }
    for(int refusal=0;refusal<4;++refusal) {
        Score f;f.mode(2);auto* u=f.engine->chains[0].units[0];
        if(refusal==0)f.engine->chains[0].mute=true;
        if(refusal==1)u->bypass=true;
        if(refusal==2)f.engine->pending_rec_start=1;
        if(refusal==3)f.engine->pending_rec_stop=1;
        const auto stored=banks(f);const auto base=bases(f);const auto accepted=button_auditions_ok;f.tap(KEY_Y);
        CHECK(button_auditions_ok==accepted && !u->params[0].audition_override && banks(f)==stored && bases(f)==base);++cases;
    }
    for(bool paused:{false,true}) {
        SampleFixture sample;Score f;auto* rec=new RecUnit;rec->sample_slot=63;delete f.engine->replace_unit(0,1,rec);
        CHECK(rec->prepare_slot()==trackr::synth::SampleEditResult::Ok);
        CHECK(f.engine->request_recorder_record(0,1,true,LaunchQuantize::Immediate)==trackr::synth::SampleEditResult::Ok);
        CHECK(rec->is_recording());if(paused)CHECK(rec->pause());
        const auto generation=rec->take_generation(),frames=rec->recorded_frames();const auto accepted=button_auditions_ok;const auto all=banks(f);
        f.tap(KEY_Y);CHECK(button_auditions_ok==accepted && rec->writer_open() && rec->is_paused()==paused);
        CHECK(rec->take_generation()==generation && rec->recorded_frames()==frames && banks(f)==all);
        rec->stop();++cases;
    }
    { Score f;f.engine->transport_play();f.engine->set_sequencer_enabled(true);
      for(int s=0;s<64;++s){f.step(s).flags=SeqStep::Trig;f.step(s).set_condition(SeqStep::Condition::Quarter);}
      q15 pcm[512]={};f.engine->render(pcm,256);const auto tick=f.engine->sequence_tick,clock=f.engine->transport_frame;
      const auto random=rng(*f.engine);const auto all=banks(f);f.tap(KEY_Y);
      CHECK(f.engine->transport_playing && f.engine->sequencer_enabled && f.engine->sequence_tick==tick && f.engine->transport_frame==clock);
      CHECK(rng(*f.engine)==random && banks(f)==all);++cases; }
}
void browse_and_touch() {
    Score f;f.set_target(FmUnit::P_RATIO);f.select(0);f.select(2);const auto selection=sequencer_inspector().selection;const auto all=banks(f);
    circle={};f.frame();circle={0,-140};f.frame(KEY_CPAD_DOWN,KEY_CPAD_DOWN);circle={};f.frame();capture();
    CHECK(sequencer_inspector().param==FmUnit::P_INDEX && sequencer_inspector().selection==selection && banks(f)==all);
    f.xy(30,200);f.xy(204,216);CHECK(sequencer_inspector().param==FmUnit::P_FEEDBACK && banks(f)==all);
    f.xy(270,216);f.lkey(KEY_B);f.mode(2);
    f.xy(layout::VALUE_X0,215);CHECK(value(f,2,FmUnit::P_GAIN)==0);
    f.xy(layout::VALUE_X1,215);CHECK(value(f,2,FmUnit::P_GAIN)==32767);++cases;
}
std::filesystem::path output;std::ofstream surfaces;
void take(Score& f,const std::string& name,bool top) {
    capture();width=top?400:320;pixels.assign(width*240,0xff000000u);objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();
    const auto before=banks(f);const auto base=bases(f);const int dirty=mutations;const auto now=host_time_ms;const auto previews=button_auditions;
    auto* engine=g_eng;auto* audio=g_audio;g_eng=nullptr;g_audio=nullptr;
    if(top)top_draw();else sequencer_view_draw();
    const auto first=pixels;const int commands=objects;const auto labels=texts;
    CHECK(!outside_rects && !outside_lines && objects>0);
    CHECK(pixels[30*width+3]!=0xff000000u); // actual initialized paper, not a black mock
    const auto label=[&](const char* text){return std::any_of(labels.begin(),labels.end(),[&](const TextRecord& t){return t.text==text;});};
    if(!top && name.find("normal-")!=std::string::npos)CHECK(label("a+ b- / y listen"));
    if(!top && name.find("normal-0")!=std::string::npos)CHECK(label("a on / b off"));
    if(!top && name.find("tools")!=std::string::npos)CHECK(label("clear clip") && label("l+^v group / <> range"));
    if(!top && name.find("held-b")!=std::string::npos)CHECK(label("hold b") && label("release: keep"));
    if(top && name.find("normal-6")!=std::string::npos)CHECK(label("a+ b- / y listen / l tools / r mode"));
    pixels.assign(width*240,0xff000000u);objects=outside_rects=outside_lines=text_overlaps=0;texts.clear();
    if(top)top_draw();else sequencer_view_draw();
    CHECK(pixels==first && objects==commands && !outside_rects && !outside_lines);
    g_eng=engine;g_audio=audio;
    CHECK(banks(f)==before && bases(f)==base && mutations==dirty && button_auditions==previews && host_time_ms==now);
    for(const auto& text:labels)CHECK(text.x>=0 && text.y>=0 && text.x+text.w<=width && text.y+text.h<=240);
    const auto file=output/(name+(top?"-top.ppm":"-bottom.ppm"));std::ofstream stream(file,std::ios::binary);stream<<"P6\n"<<width<<" 240\n255\n";
    for(u32 color:first) { const char rgb[]={char(color),char(color>>8),char(color>>16)};stream.write(rgb,3); }
    surfaces<<file.filename().string()<<'\t'<<width<<'\t'<<commands<<'\t'<<labels.size()<<'\n';
    std::ofstream textfile(file.string()+".txt");for(const auto& t:labels)textfile<<t.call_x<<','<<t.call_y<<' '<<t.text<<'\n';++native_surfaces;
}
void captures(const char* path) {
    output=path;CHECK(!std::filesystem::exists(output));std::filesystem::create_directories(output);surfaces.open(output/"surfaces.tsv");
    surfaces<<"file\twidth\tobjects\tlabels\n";
    for(auto theme:{palette::Theme::Graphite,palette::Theme::Pink}) {
        palette::set(theme);editor_style::init();editor_style::capture();Score f;
        m::patch_capture(false,false,host_time_ms);host_time_ms+=300;m::patch_capture(false,false,host_time_ms);
        const std::string p=theme==palette::Theme::Graphite?"graphite-":"pink-";
        for(int mode=0;mode<8;++mode) { f.mode(mode);for(bool upper:{false,true})take(f,p+"normal-"+std::to_string(mode),upper); }
        f.set_target(FmUnit::P_RATIO);put(f,0,FmUnit::P_RATIO,8000);put(f,2,FmUnit::P_RATIO,12000);f.select(0);f.select(2);
        for(bool upper:{false,true})take(f,p+"group-lock",upper);
        f.lkey(KEY_X);f.frame(KEY_L,KEY_L);for(bool upper:{false,true})take(f,p+"tools",upper);
        f.frame(0,0,KEY_L);f.frame();f.hold_b();for(bool upper:{false,true})take(f,p+"held-b",upper);f.frame(0,0,KEY_B);f.frame();
    }
    ++cases;
}
} // namespace score_buttons

// Exact production global-Y statement is extracted by the runner, not recopied
// or regex-only asserted. Unrelated navigation boundaries are observable stubs.
namespace main_y_probe {
using namespace seance::platform::ui3ds;
int chains_cancel=0,seq_cancel=0,perf_leave=0;
struct Session { bool has_pending()const{return false;} void cancel_pending(){CHECK(false);} } g_project_session;
void chains_view_touch_release(bool commit){CHECK(!commit);++chains_cancel;}
void sequencer_view_cancel_input(){++seq_cancel;seance::platform::ui3ds::sequencer_view_cancel_input();}
void performance_view_leave(){++perf_leave;}
void dispatch(u32& down,u32 held,bool& stage_skip_touch) {
#include "score_main_y.inc"
}
void run() {
    score_buttons::Score f;
    for(View view:{View::Chains,View::Sequencer,View::Performance,View::Sigils,View::Bounce,View::Patchbay})
    for(Overlay overlay:{Overlay::None,Overlay::Editor,Overlay::Picker,Overlay::SampleBrowser,Overlay::PatchBrowser,Overlay::ConfirmDiscard})
    for(u32 modifier:{0u,u32(KEY_L),u32(KEY_R)}) {
        g_view=view;g_overlay=overlay;g_seq_lock_armed=true;g_seq_lock_flash=20;g_drag=Drag::ParamBase;
        u32 down=KEY_Y|KEY_ZL;bool skip=false;const int cc=chains_cancel,sc=seq_cancel,pc=perf_leave;
        dispatch(down,KEY_Y|modifier,skip);
        const bool moves=modifier==0 && overlay!=Overlay::PatchBrowser && overlay!=Overlay::ConfirmDiscard && !(view==View::Sequencer && overlay==Overlay::None);
        CHECK(skip==moves && chains_cancel==cc+moves && seq_cancel==sc+moves);
        CHECK(perf_leave==pc+(moves&&view==View::Performance));
        if(moves)CHECK(g_view==(view==View::Chains?View::Sigils:View::Chains) && g_overlay==Overlay::None && down==KEY_ZL && g_drag==Drag::None);
        else CHECK(g_view==view && g_overlay==overlay && down==(KEY_Y|KEY_ZL));
        if(moves && view==View::Sequencer)CHECK(!g_seq_lock_armed && !g_seq_lock_flash);
        ++score_buttons::cases;
    }
    g_view=View::Sequencer;g_overlay=Overlay::None;g_seq_lock_armed=false;f.frame();
    u32 down=KEY_Y;bool skip=false;const auto previews=button_auditions;dispatch(down,KEY_Y,skip);f.frame(down,KEY_Y);
    CHECK(button_auditions==previews+1 && !skip && g_view==View::Sequencer);
}
}
int main(int argc,char** argv) {
    try {
        CHECK(argc==2);score_buttons::evidence=std::filesystem::path(argv[1]).parent_path()/"persistent";
        CHECK(!std::filesystem::exists(score_buttons::evidence));std::filesystem::create_directories(score_buttons::evidence);
        struct Test { const char* name;void(*run)(); };
        const Test tests[]={
            {"single +/- current focus with selection",score_buttons::single_buttons},
            {"clamps categorical cycle empty group opposites",score_buttons::clamps_and_empty_group},
            {"group modes atomic undo redo",score_buttons::group_modes_history},
            {"inherited bases capacity rollback no targets",score_buttons::atomic_capacity_and_bases},
            {"note source semitones and sign",score_buttons::note_sources},
            {"sparse slice IDs and audition",score_buttons::slice_sparse},
            {"lock per-step offset discrete comp clock",score_buttons::lock_relative_and_comp},
            {"clipboard hold drain cancel wrap X",score_buttons::clipboard_and_hold},
            {"sparse cross-page inactive bank",score_buttons::cross_page_bank},
            {"Y real audition PCM stored state refusal",score_buttons::audition},
            {"CPAD touch target navigation",score_buttons::browse_and_touch},
            {"exact main global Y execution",main_y_probe::run},
        };
        int failed=0;
        for(const auto& test:tests) {
            try {test.run();std::printf("ok %s\n",test.name);}
            catch(const std::exception& e){++failed;std::fprintf(stderr,"FAIL %s: %s\n",test.name,e.what());}
        }
        try {score_buttons::captures(argv[1]);std::puts("ok native graphite/pink frozen captures");}
        catch(const std::exception& e){++failed;std::fprintf(stderr,"FAIL captures: %s\n",e.what());}
        std::printf("score buttons: %d/13 groups; %u cases; %u nonzero PCM words; %u native surfaces; %u persisted patch pairs; inherited suites 0\n",13-failed,score_buttons::cases,score_buttons::nonzero_pcm,score_buttons::native_surfaces,score_buttons::persisted_pairs);
        return failed?1:0;
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL setup: %s\n",e.what());return 1;}
}
