// Focused portable ARP proof; no device, media export, or broad regression suite.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <unistd.h>
#define private public
#include "engine/engine.h"
#include "engine/patch_io.h"
#undef private
using namespace seance;
using A=ArpUnit;
static size_t allocations=0;
#if defined(__SANITIZE_ADDRESS__)
extern "C" void __sanitizer_malloc_hook(const volatile void*,std::size_t){++allocations;}
#endif
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"arp failure line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
constexpr uint64_t BEAT=1ull<<32;
void set(Unit& u,int p,int value){u.params[p].base=q15(value);u.params[p].snap();}
void render(Engine& e,int frames,int partition=257){
    q15 out[1024];while(frames){int n=std::min(frames,partition);CHECK(n<=512);const size_t before=allocations;e.render(out,n);CHECK(allocations==before);frames-=n;}
}
std::vector<q15> pcm(Engine& e,int frames,bool odd){
    std::vector<q15> out(size_t(frames)*2);int done=0;
    while(done<frames){const int n=std::min(frames-done,odd ? 1+(done*37)%263 : 512);const size_t before=allocations;e.render(out.data()+done*2,n);CHECK(allocations==before);done+=n;}return out;
}
struct Hit{uint64_t frame,phase;int semitone;};
struct Probe:OscUnit{
    Engine& e;std::array<Hit,1024> hits{};std::array<uint64_t,1024> falls{};int count=0,fall_count=0;bool was_gate=false;
    explicit Probe(Engine& engine):e(engine){clean=false;}
    void control_tick()override{}
    void trigger()override{CHECK(count<int(hits.size()));hits[count++]={e.audio_frame_,e.arp_phase(),arp_voice.semitones};}
    void process(q15* out,int n)override{
        if(was_gate&&!arp_voice.gate){CHECK(fall_count<int(falls.size()));falls[fall_count++]=e.audio_frame_;}
        was_gate=arp_voice.gate;std::fill(out,out+n,q15(12000));
    }
};
struct RecProbe:RecUnit{int hits=0;void trigger()override{++hits;}void process(q15*,int)override{}};
struct GateProbe:VcaUnit{int hits=0;void trigger()override{++hits;}void process(q15*,int)override{}};
struct Tail:Unit{int32_t memory=0;void process(q15* b,int n)override{for(int i=0;i<n;++i){memory+=(int(b[i])-memory)/128;b[i]=q15(memory);}}};
struct Rig{
    std::unique_ptr<Engine> e=std::make_unique<Engine>();A* a=new A;Probe* p=new Probe(*e);
    Rig(){e->chains[0].units[0]=a;e->chains[0].units[1]=p;e->sequencer_enabled=false;}
    void play(){e->transport_play();}
};
void mappings_orders(){
    static_assert(int(UnitKind::Arp)==26&&int(UnitKind::Mouth)==25,"append-only");
    static_assert(sizeof(A::Visual)<64,"bounded visual");
    A a;CHECK(a.num_params==10&&!std::strcmp(unit_kind_name(a.kind),"ARP"));
    for(int i=0;i<12;++i)CHECK(A::rate_index(A::rate_value(i))==i);
    for(int i=0;i<6;++i)CHECK(A::order_index(A::order_value(i))==i);
    for(int i=1;i<=3;++i)CHECK(A::octaves(A::octaves_value(i))==i);
    for(int i=5;i<=100;++i)CHECK(A::gate_percent(A::gate_value(i))==i);
    for(int i=-24;i<=24;++i)CHECK(A::note_semitones(A::note_value(i))==i);
    CHECK(A::note_semitones(0)==A::NOTE_OFF&&A::note_value(A::NOTE_OFF)==0);
    for(int n=-24;n<=48;++n)CHECK(std::abs(Unit::arp_ratio_q24(n)/16777216./std::pow(2.,n/12.)-1)<.000001);
    q15 pass[4]={-32768,171,0,32767},saved[4];std::memcpy(saved,pass,sizeof(pass));a.process(pass,4);CHECK(!std::memcmp(saved,pass,sizeof(pass)));
    set(a,A::P_NOTE1,A::note_value(7));set(a,A::P_NOTE2,A::note_value(0));set(a,A::P_NOTE3,A::note_value(4));
    const int orders[4][4]={{0,4,7,0},{7,4,0,0},{0,4,7,4},{7,0,4,0}};
    for(int order=0;order<4;++order){set(a,A::P_ORDER,A::order_value(order));a.reset();auto v=a.visual();CHECK(v.count==(order==2?4:3));for(int i=0;i<v.count;++i)CHECK(v.notes[i]==orders[order][i]);}
    set(a,A::P_OCTAVES,A::octaves_value(3));set(a,A::P_NOTE4,A::note_value(12));set(a,A::P_ORDER,A::order_value(2));a.reset();CHECK(a.visual().count==22);
    // Duplicate pitches are deliberately distinct slots, even across octaves.
    set(a,A::P_ORDER,A::order_value(5));a.reset();auto v=a.visual();std::sort(v.notes,v.notes+v.count);CHECK(v.count==12);for(int i=1;i<v.count;++i)CHECK(v.notes[i]>=v.notes[i-1]);
    set(a,A::P_ORDER,A::order_value(4));a.reset();auto first=a.visual();a.reset();CHECK(!std::memcmp(first.notes,a.visual().notes,sizeof(first.notes)));
    for(int p=A::P_NOTE1;p<=A::P_NOTE4;++p){set(a,p,A::note_value(A::NOTE_OFF));}a.reset();CHECK(a.visual().count==0);
    std::puts("ok mappings, all six orders, octave expansion/duplicates, pass-through and deterministic random");
}
void timing(){
    const unsigned numer[12]={1,1,1,1,2,2,2,2,3,3,3,3},denom[12]={1,2,4,8,3,6,12,24,2,4,8,16};
    for(bool phase:{false,true})for(int rate=0;rate<12;++rate){
        Rig r;r.e->set_bpm(137);if(phase)r.e->activate_clock_runtime();set(*r.a,A::P_RATE,A::rate_value(rate));set(*r.a,A::P_GATE,A::gate_value(37));r.play();
        const uint64_t fpb=r.e->frames_per_beat(),increment=r.e->clock_increment_;render(*r.e,int(fpb*4),257);
        CHECK(r.p->count>=3&&r.p->fall_count>=2);
        auto frame=[&](uint64_t p){return phase ? (p+increment-1)/increment : (p*fpb+BEAT-1)/BEAT;};
        for(int i=0;i<r.p->count;++i){const uint64_t start=uint64_t(i)*BEAT*numer[rate]/denom[rate];CHECK(r.p->hits[i].frame==frame(start));CHECK(r.p->hits[i].semitone==(i%3==0?0:i%3==1?4:7));}
        for(int i=0;i<r.p->fall_count;++i){const uint64_t start=uint64_t(i)*BEAT*numer[rate]/denom[rate],next=uint64_t(i+1)*BEAT*numer[rate]/denom[rate];CHECK(r.p->falls[i]==frame(start+(next-start)*37/100));}
    }
    Rig a,b;for(Rig* r:{&a,&b}){r->e->set_bpm(139);r->e->sigil_config[0].legacy=true;r->e->sigils[0].drawn=true;for(auto& x:r->e->sigils[0].table)x=20000;r->e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,87);set(*r->a,A::P_RATE,A::rate_value(6));set(*r->a,A::P_ORDER,A::order_value(5));r->play();}
    CHECK(pcm(*a.e,75003,false)==pcm(*b.e,75003,true));CHECK(a.p->count==b.p->count&&a.p->fall_count==b.p->fall_count);for(int i=0;i<a.p->count;++i)CHECK(a.p->hits[i].frame==b.p->hits[i].frame&&a.p->hits[i].semitone==b.p->hits[i].semitone);
    CHECK(a.e->effective_bpm_q16()>139*65536);
    // Applied Q32 phase crosses each rational deadline at the first sample only.
    for(int i=0;i<a.p->count;++i){const uint64_t due=uint64_t(i)*BEAT/6;CHECK(a.p->hits[i].phase>=due);CHECK(a.p->hits[i].phase-due<700000);}
    std::puts("ok 12 rates at odd bpm, exact note/gate deadlines in both clocks, modulated clock irregular partition PCM identity; zero render allocation");
}
void trigger_lifecycle(){
    Rig r;auto* rec=new RecProbe;auto* gate=new GateProbe;auto* tail=new Tail;r.e->chains[0].units[2]=gate;r.e->chains[0].units[3]=tail;r.e->chains[0].units[4]=rec;
    r.e->sequencer_enabled=true;for(auto& step:r.e->pattern.steps[0]){step.flags=SeqStep::Trig;step.set_retrig_count(4);}
    r.e->trigger_chain(0);CHECK(!r.p->count&&!rec->hits&&!gate->hits); // stopped: no invented chord latch
    r.play();render(*r.e,17000);CHECK(r.p->count==int(r.a->visual().events)&&gate->hits==r.p->count&&rec->hits==0);
    int count=r.p->count;r.e->trigger_chain(0);render(*r.e,1);CHECK(r.p->count==count); // reset free
    set(*r.a,A::P_RESET,32767);r.a->control_tick();r.e->trigger_chain(0);render(*r.e,1);CHECK(r.p->count==count+1);
    r.e->transport_stop();render(*r.e,33);CHECK(r.p->arp_voice.semitones==0&&!r.p->arp_voice.gate&&r.p->arp_voice.gain==0);CHECK(tail->memory>0);CHECK(!r.a->visual().active);
    r.e->transport_play();render(*r.e,1);CHECK(r.p->arp_voice.gate&&r.a->visual().current==0);
    r.a->bypass=true;render(*r.e,1);CHECK(r.p->arp_voice.owner<0&&!r.p->arp_voice.semitones&&r.p->arp_voice.gain==32768);CHECK(r.a->visual().state==A::State::Bypassed);
    r.a->bypass=false;r.e->chains[0].mute=true;render(*r.e,1);CHECK(r.p->arp_voice.owner<0);r.e->chains[0].mute=false;render(*r.e,1);CHECK(r.p->arp_voice.owner==0);
    for(int p=A::P_NOTE1;p<=A::P_NOTE4;++p){set(*r.a,p,0);}r.a->control_tick();render(*r.e,64);CHECK(r.a->visual().state==A::State::AllOff&&r.p->arp_voice.gain==0&&r.p->arp_voice.semitones==0);
    delete r.e->remove_unit(0,0);r.a=nullptr;CHECK(r.p->arp_voice.owner<0&&r.p->arp_voice.semitones==0);
    Rig second;auto* q=new Probe(*second.e);second.e->chains[0].units[3]=q;second.play();render(*second.e,1);second.p->bypass=true;render(*second.e,1);CHECK(second.a->visual().target_slot==3&&q->arp_voice.owner==0&&second.p->arp_voice.owner<0);
    second.p->bypass=false;render(*second.e,1);CHECK(q->arp_voice.owner<0&&second.p->arp_voice.owner==0);
    auto* near=new A;second.e->chains[0].units[2]=near;second.p->bypass=true;render(*second.e,1);CHECK(second.a->visual().state==A::State::Conflict&&q->arp_voice.owner==2);
    CHECK(second.e->move_unit_before(0,2,0)==0);CHECK(q->arp_voice.owner<0);render(*second.e,1);
    delete second.e->replace_unit(0,3,nullptr);render(*second.e,1); // target replacement has no stale pointer
    std::puts("ok sequencer/manual arbitration, gate-processor notes, no REC commands, source-only tails, all-off and lifecycle/reorder/retarget");
}
double frequency(const std::vector<q15>& x,int begin=1024){
    double first=0,last=0;int count=0;for(int i=begin;i+1<int(x.size());++i)if(x[i]<=0&&x[i+1]>0){const double t=i-double(x[i])/(int(x[i+1])-x[i]);if(!count)first=t;last=t;++count;}CHECK(count>8);return (count-1)*ENGINE_SR/(last-first);
}
std::vector<q15> source_pcm(Unit& u,int n=12000){std::vector<q15>x(n);u.process(x.data(),n);return x;}
void source_pitch_and_play(){
    for(bool clean:{false,true}){
        OscUnit a,b;a.clean=b.clean=clean;set(a,OscUnit::P_PITCH,16384);set(b,OscUnit::P_PITCH,16384);set(a,OscUnit::P_GAIN,16000);set(b,OscUnit::P_GAIN,16000);a.control_tick();b.control_tick();b.set_arp_pitch(7);
        const double root=frequency(source_pcm(a)),pitched=frequency(source_pcm(b));CHECK(std::abs(pitched/root-std::pow(2.,7./12.))<.0003);CHECK(a.params[0].base==b.params[0].base);
        b.set_arp_pitch(48);set(b,0,32767);b.control_tick();CHECK(clean ? b.clean_target_[0]<=14000.f : b.inc_<=0x70000000u);
    }
    FmUnit fm0,fm1;for(FmUnit* f:{&fm0,&fm1}){f->drone=true;set(*f,FmUnit::P_PITCH,16384);set(*f,FmUnit::P_INDEX,0);set(*f,FmUnit::P_FEEDBACK,0);set(*f,FmUnit::P_GAIN,14000);f->control_tick();}fm1.set_arp_pitch(12);CHECK(std::abs(frequency(source_pcm(fm1))/frequency(source_pcm(fm0))-2)<.0003);
    BirdUnit b0,b1;for(BirdUnit* b:{&b0,&b1}){set(*b,BirdUnit::P_PITCH,0);set(*b,BirdUnit::P_SWEEP,0);set(*b,BirdUnit::P_TIME,32767);set(*b,BirdUnit::P_TRILL,0);set(*b,BirdUnit::P_AIR,0);b->control_tick();}b1.set_arp_pitch(12);b0.trigger();b1.trigger();CHECK(std::abs(frequency(source_pcm(b1))/frequency(source_pcm(b0))-2)<.0003);
    auto& sample=trackr::synth::SampleBank::instance().slot(0);sample=trackr::synth::Sample{};sample.channels=1;sample.data.resize(65536);for(size_t i=0;i<sample.data.size();++i)sample.data[i]=q15(12000*std::sin(double(i)*6.283185307179586/128.));
    PlayUnit p0,p1;for(PlayUnit* p:{&p0,&p1}){set(*p,PlayUnit::P_LOOP,32767);p->control_tick();p->trigger();}p1.set_arp_pitch(12);CHECK(std::abs(frequency(source_pcm(p1))/frequency(source_pcm(p0))-2)<.0003);
    // Real PLAY source, restarted sample versus uninterrupted latched voice.
    auto e=std::make_unique<Engine>();auto* a=new A;auto* play=new PlayUnit;e->chains[0].units[0]=a;e->chains[0].units[1]=play;e->sequencer_enabled=false;set(*a,A::P_NOTE2,A::note_value(12));set(*a,A::P_NOTE3,0);set(*a,A::P_GATE,32767);set(*play,PlayUnit::P_LOOP,32767);e->transport_play();render(*e,4001);CHECK(play->visual_source_frame()<8&&play->inc_q16_>120000);
    e->transport_stop();e->transport_reset();set(*a,A::P_PLAY_MODE,32767);render(*e,32);e->transport_play();render(*e,4001);CHECK(play->visual_source_frame()>3900&&play->inc_q16_>120000);
    // Legato finite exhaustion is not secretly retriggered each note. Explicit reset rearms it.
    e->transport_stop();sample.data.resize(128);set(*play,PlayUnit::P_LOOP,0);set(*a,A::P_RESET,32767);render(*e,32);e->transport_reset();e->transport_play();render(*e,4500);CHECK(!play->playback_active());e->trigger_chain(0);render(*e,1);CHECK(play->playback_active());CHECK(play->gate_deadline_==UINT64_MAX);
    std::puts("ok actual OSC clean/legacy, FM, BIRD and PLAY PCM semitones; synth range clamp; PLAY restart/legato/exhaustion reset");
}
void clocked_config_edits(){
    for(bool phase:{false,true}){
        Rig r;if(phase)r.e->activate_clock_runtime();r.play();render(*r.e,129);
        const auto accepted=r.a->visual();const uint64_t edge=r.a->next_phase_,gate=r.a->gate_phase_;
        const uint32_t rng=r.a->rng_;
        const uint64_t edge_frame=phase ? r.e->clock_frame_for_phase(edge) : (edge*r.e->frames_per_beat()+BEAT-1)/BEAT;
        for(int edit=0;edit<3;++edit){
            if(edit==0)set(*r.a,A::P_NOTE1,A::note_value(9));
            else if(edit==1)set(*r.a,A::P_ORDER,A::order_value(1));
            else set(*r.a,A::P_OCTAVES,A::octaves_value(2));
            r.a->control_tick();render(*r.e,137);
            const auto held=r.a->visual();
            CHECK(held.events==accepted.events&&r.p->count==1&&held.current==accepted.current&&held.semitone==accepted.semitone);
            CHECK(held.count==accepted.count&&!std::memcmp(held.notes,accepted.notes,sizeof(held.notes))&&held.gate==accepted.gate);
            CHECK(r.a->next_phase_==edge&&r.a->gate_phase_==gate&&r.a->rng_==rng&&r.a->voice_started_);
        }
        render(*r.e,int(edge_frame-r.e->audio_frame_));CHECK(r.p->count==1&&!r.a->visual().gate);
        render(*r.e,1);const int expected[6]={21,19,16,9,7,4};auto current=r.a->visual();
        CHECK(r.p->count==2&&r.p->hits[1].frame==edge_frame&&current.current==0&&current.semitone==21&&current.count==6);
        for(int i=0;i<6;++i)CHECK(current.notes[i]==expected[i]);
        // Rebuilding a pending random figure consumes the current RNG stream,
        // not a fresh seed; no consumption occurs at intermediate control edits.
        render(*r.e,137);r.a->rng_=0x12345678u;uint32_t expected_rng=r.a->rng_;
        set(*r.a,A::P_ORDER,A::order_value(4));r.a->control_tick();CHECK(r.a->rng_==expected_rng);
        const uint64_t next=r.a->next_phase_;
        const uint64_t next_frame=phase ? r.e->clock_frame_for_phase(next) : (next*r.e->frames_per_beat()+BEAT-1)/BEAT;
        render(*r.e,int(next_frame-r.e->audio_frame_));CHECK(r.p->count==2);
        for(int i=0;i<6;++i){expected_rng^=expected_rng<<13;expected_rng^=expected_rng>>17;expected_rng^=expected_rng<<5;}
        render(*r.e,1);CHECK(r.p->count==3&&r.p->hits[2].frame==next_frame&&r.a->rng_==expected_rng);
    }
    // Actual PLAY legato must retain its cursor/window across a pending pool edit.
    auto& sample=trackr::synth::SampleBank::instance().slot(0);sample=trackr::synth::Sample{};sample.channels=1;sample.data.assign(65536,q15(12000));
    auto e=std::make_unique<Engine>();auto* a=new A;auto* play=new PlayUnit;
    e->chains[0].units[0]=a;e->chains[0].units[1]=play;e->sequencer_enabled=false;
    set(*a,A::P_PLAY_MODE,32767);set(*a,A::P_GATE,32767);set(*play,PlayUnit::P_LOOP,32767);e->transport_play();render(*e,401);
    const uint64_t edge=a->next_phase_;const uint32_t accepted=a->visual().events;
    for(int note:{7,12}){
        set(*a,A::P_NOTE1,A::note_value(note));set(*a,A::P_ORDER,A::order_value(3));a->control_tick();
        const auto cursor=play->visual_source_frame();render(*e,199);
        CHECK(play->visual_source_frame()>cursor&&a->visual().events==accepted&&a->next_phase_==edge);
    }
    render(*e,int(4000-e->audio_frame_));const auto before=play->visual_source_frame();CHECK(a->visual().events==1);
    render(*e,1);CHECK(a->visual().events==2&&a->visual().semitone==12&&a->visual().current==0);
    CHECK(play->visual_source_frame()>=before&&play->visual_source_frame()>3900&&play->inc_q16_>120000);
    std::puts("ok active config edits commit latest figure on existing clock edge, preserve accepted visual/deadlines/RNG and PLAY legato cursor");
}
void persistence_layers_identity(){
    Rig r;set(*r.a,A::P_RATE,A::rate_value(9));set(*r.a,A::P_NOTE1,A::note_value(-7));r.a->params[A::P_NOTE2].src=ModSource::SigilB;r.a->params[A::P_NOTE2].mod_depth=7000;r.a->params[A::P_NOTE2].mod_enabled=false;
    CHECK(r.e->set_step_lock(0,0,0,A::P_NOTE4,A::note_value(19)));r.e->capture_scene(0,0);
    r.play();render(*r.e,200);std::unique_ptr<Unit> clone(clone_unit(r.a));auto* copied=static_cast<A*>(clone.get());CHECK(!copied->visual().active&&copied->visual().events==0);
    auto loaded=std::make_unique<Engine>();const std::string path="/tmp/seance-arp-"+std::to_string(long(getpid()))+".snc";CHECK(patchio::save(*r.e,path.c_str())&&patchio::load(*loaded,path.c_str()));std::remove(path.c_str());auto* a=static_cast<A*>(loaded->chains[0].units[0]);CHECK(a&&a->kind==UnitKind::Arp&&!a->visual().active&&a->visual().events==0);
    for(int p=0;p<A::P_COUNT;++p){CHECK(a->params[p].base==r.a->params[p].base&&a->params[p].src==r.a->params[p].src&&a->params[p].mod_depth==r.a->params[p].mod_depth&&a->params[p].mod_enabled==r.a->params[p].mod_enabled);CHECK(clone->params[p].base==r.a->params[p].base);}
    CHECK(loaded->pattern.steps[0][0].locks[0].param==A::P_NOTE4&&loaded->scenes[0][0].kind[0]==UnitKind::Arp);
    // Note layer does not steal or mutate any of the source's established overrides.
    OscUnit root;root.clean=false;Param& p=root.params[0];p.base=8192;p.step_lock_override=true;p.step_lock=11000;p.audition_override=true;p.audition=13000;p.performance_override=true;p.performance=15000;p.temp_override=true;p.temp=17000;p.mod_depth=10000;p.slew_ready=false;p.resolve(12000);root.control_tick();const uint32_t original=root.inc_;const Param before=p;root.set_arp_pitch(12);CHECK(root.inc_==original*2);CHECK(!std::memcmp(&before,&p,sizeof(p)));root.set_arp_pitch(0);CHECK(root.inc_==original);
    // ARP's ordinary target bank accepts locks+mod independently from its base.
    set(*r.a,A::P_NOTE1,A::note_value(0));auto& note=r.a->params[A::P_NOTE1];note.step_lock_override=true;note.step_lock=A::note_value(12);note.mod_depth=1200;note.resolve(16384);r.a->control_tick();CHECK(note.base==A::note_value(0)&&A::note_semitones(note.target)!=0);
    // Empty and bypassed-ARP engines remain sample-identical for actual sources.
    for(UnitKind kind:{UnitKind::Osc,UnitKind::Fm,UnitKind::Bird,UnitKind::SamplePlayer}){
        auto x=std::make_unique<Engine>(),y=std::make_unique<Engine>();auto* bypass=new A;bypass->bypass=true;y->chains[0].units[0]=bypass;
        x->chains[0].units[1]=make_unit(kind);y->chains[0].units[1]=clone_unit(x->chains[0].units[1]);x->sequencer_enabled=y->sequencer_enabled=false;x->trigger_chain(0);y->trigger_chain(0);x->transport_play();y->transport_play();CHECK(pcm(*x,12003,false)==pcm(*y,12003,false));CHECK(!x->clock_runtime_&&!y->clock_runtime_);
    }
    std::puts("ok SNC7/clone fresh runtime, scene+lock params, all root overrides/mods independent, bypass/no-arp actual-source PCM identity");
}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);mappings_orders();timing();trigger_lifecycle();source_pitch_and_play();clocked_config_edits();persistence_layers_identity();std::printf("arp core 6/6 groups passed; %u assertions\n",checks);}
