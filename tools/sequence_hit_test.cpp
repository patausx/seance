// Real-engine sequence-hit proof. ProbeUnit observes Unit::trigger and resolved
// p-locks; telemetry is compared to that boundary, never to its own latch.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <vector>
#define private public
#include "engine/engine.h"
#include "engine/units.h"
#undef private
#include "engine/patch_io.h"
using namespace seance;
namespace {
uint64_t checks=0, snapshots=0, observed=0, allocation_calls=0, pcm_samples=0;
bool watching=false;
std::string directory;
FILE *event_log=nullptr, *pcm_log=nullptr;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"fail line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
struct Guard {Guard(){watching=true;}~Guard(){watching=false;}};
struct Hit { uint64_t frame; int value; };
struct Probe : Unit {
    Engine& e; int chain; std::array<Hit,4096> hits{}; size_t count=0;
    bool source=true, gate=false; SequenceLifecycle lifecycle=SequenceLifecycle::Strike;
    Probe(Engine& engine,int c):e(engine),chain(c){add_param("identity",1000);}
    SequencerProperties sequencer_properties() const override {return {source,gate,lifecycle,0,0,-1,-1};}
    void process(q15* out,int frames) override {for(int i=0;i<frames;++i) out[i]=params[0].effective/16;}
    void trigger() override {CHECK(count<hits.size()); hits[count++]={e.audio_frame_,params[0].control_source()};++observed;}
};
void render(Engine& e,int frames,int chunk=31) {
    q15 pcm[514];
    while(frames){int n=std::min(chunk,frames);CHECK(n<=257);{Guard g;e.render(pcm,n);}frames-=n;}
}
void activate(Engine& e,bool phase){
    if(phase){CHECK(e.set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,1));
        e.sigils[0].drawn=true;for(auto& v:e.sigils[0].table)v=0;e.sigils[0].phase_inc=0;e.control_step();CHECK(e.clock_runtime_);}
    e.set_sequencer_enabled(true);e.transport_play();e.initialize_sequence_clock(true);
}
int identity(int bank,int step){return 1000+bank*1000+step;}
void score(Engine& e,int length,int retrigs,SequenceDirection dir,bool conditions,bool micro){
    for(int bank=0;bank<NUM_PATTERNS;++bank){auto& p=e.sequence_pattern(bank);
        for(int c=0;c<NUM_CHAINS;++c){p.length[c]=uint8_t(length);p.direction[c]=dir;
            for(int s=0;s<SEQ_STEPS;++s){auto& step=p.steps[c][s];step.flags=SeqStep::Trig;step.set_retrig_count(retrigs);
                step.lock_count=1;step.locks[0]={0,0,q15(identity(bank,s))};
                if(conditions)step.set_condition(SeqStep::Condition((s+c)%8));
                if(micro)p.microtiming[c][s]=int8_t((s%2)?23:-23);
            }
        }
    }
}
#ifndef SEQUENCE_HIT_BASELINE
void inactive(const SequenceHit& h){CHECK(h.age_ms==65535&&h.step==255&&h.bank==255&&h.duration_ms==140);}
void same(const SequenceHit& a,const SequenceHit& b){CHECK(a.age_ms==b.age_ms&&a.step==b.step&&a.bank==b.bank&&a.duration_ms==b.duration_ms);}
void verify(Engine& e,const std::array<Probe*,4>& probes) {
    EngineUiSnapshot s;{Guard g;e.capture_sequence_snapshot(s);}++snapshots;
    for(int c=0;c<4;++c){const auto& p=*probes[c];const auto& h=s.sequence_hit[c];
        if(p.count && e.audio_frame_-p.hits[p.count-1].frame<32 && e.transport_playing && e.sequencer_enabled && p.source && p.lifecycle!=SequenceLifecycle::Flow) CHECK(h.step!=255);
        if(h.step==255){inactive(h);continue;}
        CHECK(p.count>0);const auto& real=p.hits[p.count-1];
        CHECK(h.step==(real.value-1000)%1000);CHECK(h.bank==(real.value-1000)/1000);
        CHECK(h.age_ms==(e.audio_frame_-real.frame)/32);CHECK(h.age_ms<h.duration_ms);CHECK(h.duration_ms>=1&&h.duration_ms<=140);
        CHECK(e.transport_playing&&e.sequencer_enabled&&p.source&&p.lifecycle!=SequenceLifecycle::Flow);
    }
    // Different edit-bank captures retain the event tuple, generic publication too.
    if(snapshots%79==0){EngineUiSnapshot other;{Guard g;e.capture_ui_snapshot(other);}for(int c=0;c<4;++c)same(s.sequence_hit[c],other.sequence_hit[c]);
        for(int bank=-1;bank<9;++bank){Guard g;e.capture_sequence_snapshot(other,bank);for(int c=0;c<4;++c)same(s.sequence_hit[c],other.sequence_hit[c]);}}
}
#else
void verify(Engine&,const std::array<Probe*,4>&){}
#endif
void matrix(bool phase,int variant) {
    auto e=std::make_unique<Engine>();std::array<Probe*,4> p;
    for(int c=0;c<4;++c){p[c]=new Probe(*e,c);e->chains[c].units[0]=p[c];e->chains[c].volume=1000;}
    int length=variant==0?1:variant==1?5:16;int retrigs=variant%4+1;
    auto direction=SequenceDirection(variant%3);
    score(*e,length,retrigs,direction,variant>=3,variant>=2);
    e->set_bpm(variant==0?20:variant==1?300:137);e->set_swing(variant%2?75:50);
    if(variant==5)CHECK(e->set_pattern_transition(0,7,16384));
    activate(*e,phase);verify(*e,p);
    q15 pcm[62];
    for(int block=0;block<6500;++block){
        if(block==1600)e->set_sequencer_fill(true);
        if(block==2900)e->set_sequencer_fill(false);
        if(variant==4&&block==2200)e->set_bpm(211);
        if(variant==4&&block==3500)e->set_swing(69);
        if(variant==5&&block==900)CHECK(e->set_pattern_transition(0,7,32767));
        if(variant==5&&block==1100)CHECK(e->finish_pattern_transition());
        if(variant==5&&block==3000)e->request_pattern(3,false);
        {Guard g;e->render(pcm,31);}verify(*e,p);
        CHECK(std::fwrite(pcm,sizeof(q15),62,pcm_log)==62);pcm_samples+=62;
    }
    for(int c=0;c<4;++c){CHECK(p[c]->count>0);
        for(size_t i=0;i<p[c]->count;++i)std::fprintf(event_log,"%d %d %d %llu %d\n",phase,variant,c,(unsigned long long)p[c]->hits[i].frame,p[c]->hits[i].value);
    }
    std::fprintf(event_log,"state %d %d %u %llu %llu\n",phase,variant,e->sequence_rng_,(unsigned long long)e->sequence_tick,(unsigned long long)e->transport_frame);
    // Exact direction independently observed in resolved locks, no queue fields.
    if(variant<3){for(int c=0;c<4;++c)for(size_t n=0;n<std::min(size_t(40),p[c]->count);++n){size_t tick=n/retrigs;int expected=int(tick%length);
        if(direction==SequenceDirection::Reverse)expected=length-1-expected;
        if(direction==SequenceDirection::PingPong){int phase2=int(tick%(2*(length-1)));expected=phase2<length?phase2:2*(length-1)-phase2;}
        CHECK(p[c]->hits[n].value==identity(0,expected));}}
}
#ifndef SEQUENCE_HIT_BASELINE
void precise(bool phase){
    for(int bpm: {20,120,300})for(int count: {1,2,3,4}) {
        auto e=std::make_unique<Engine>();auto* p=new Probe(*e,0);e->chains[0].units[0]=p;
        score(*e,1,count,SequenceDirection::Forward,false,false);e->set_bpm(bpm);activate(*e,phase);
        EngineUiSnapshot s;e->capture_sequence_snapshot(s);CHECK(p->count==1);CHECK(s.sequence_hit[0].age_ms==0&&s.sequence_hit[0].step==0&&s.sequence_hit[0].bank==0);
        const int expected=std::min(140,15000*85/(bpm*100*count));CHECK(s.sequence_hit[0].duration_ms==expected);
        for(uint32_t n=1;n<=2*e->frames_per_beat();++n){size_t before=p->count;render(*e,1,1);{Guard g;e->capture_sequence_snapshot(s);}++snapshots;
            const auto& last=p->hits[p->count-1];uint64_t age=(e->audio_frame_-last.frame)/32;
            if(age<uint64_t(expected)){CHECK(s.sequence_hit[0].step==0&&s.sequence_hit[0].age_ms==age);CHECK(s.sequence_hit[0].duration_ms==expected);}
            else inactive(s.sequence_hit[0]);
            if(p->count>before){CHECK(s.sequence_hit[0].age_ms==0);if(p->count>1){uint64_t gap=last.frame-p->hits[p->count-2].frame;CHECK(uint64_t(expected)*32*100<=gap*85+3200);}}
        }
        CHECK(p->count>=size_t(count*8));
    }
}
void lifecycle(bool phase){
    auto e=std::make_unique<Engine>();auto* p=new Probe(*e,0);e->chains[0].units[0]=p;
    score(*e,1,4,SequenceDirection::Forward,false,false);EngineUiSnapshot s;
    auto capture=[&](){Guard g;e->capture_sequence_snapshot(s);};
    capture();for(auto h:s.sequence_hit)inactive(h);
    e->trigger_chain(0);CHECK(p->count==1);capture();inactive(s.sequence_hit[0]);
    CHECK(e->audition_sequence_step(0,0));capture();inactive(s.sequence_hit[0]);e->cancel_sequence_audition();
    activate(*e,phase);capture();CHECK(s.sequence_hit[0].step==0);
    e->set_sequencer_enabled(false);capture();inactive(s.sequence_hit[0]);e->set_sequencer_enabled(true);capture();inactive(s.sequence_hit[0]);
    e->transport_reset();capture();inactive(s.sequence_hit[0]);e->initialize_sequence_clock(true);capture();CHECK(s.sequence_hit[0].step==0);
    // Re-reading never progresses age; manual triggers never renew the old hit.
    render(*e,64);capture();auto last=s.sequence_hit[0];e->trigger_chain(0);capture();same(last,s.sequence_hit[0]);
    e->transport_stop();capture();inactive(s.sequence_hit[0]);e->transport_play();capture();inactive(s.sequence_hit[0]);
    e->transport_reset();e->initialize_sequence_clock(true);capture();CHECK(s.sequence_hit[0].step==0);
    e->pattern_banks[1]=Pattern{};e->request_pattern(1,false);capture();inactive(s.sequence_hit[0]);
    // Empty and lock-only steps are not hits, even when continuous audio exists.
    auto& step=e->pattern.steps[0][0];step.lock_count=1;step.locks[0]={0,0,1500};e->pattern.length[0]=1;e->transport_reset();e->initialize_sequence_clock(true);
    size_t before=p->count;render(*e,10000);capture();inactive(s.sequence_hit[0]);CHECK(p->count==before);
    step.flags=SeqStep::Trig;e->transport_reset();e->initialize_sequence_clock(true);capture();CHECK(s.sequence_hit[0].step==0);
    p->lifecycle=SequenceLifecycle::Flow;capture();inactive(s.sequence_hit[0]);
    p->lifecycle=SequenceLifecycle::Strike;p->source=false;capture();inactive(s.sequence_hit[0]);
    p->source=true;p->bypass=true;capture();inactive(s.sequence_hit[0]);p->bypass=false;
    auto* old=e->replace_unit(0,0,nullptr);capture();inactive(s.sequence_hit[0]);e->replace_unit(0,0,old);capture();inactive(s.sequence_hit[0]);
    auto other=std::make_unique<Engine>();e->swap_patch_state(*other);capture();inactive(s.sequence_hit[0]);other->capture_sequence_snapshot(s);inactive(s.sequence_hit[0]);
}
void conditions(bool phase){
    for(int which=0;which<8;++which){auto e=std::make_unique<Engine>();auto* p=new Probe(*e,0);e->chains[0].units[0]=p;
        score(*e,1,1,SequenceDirection::Forward,false,false);e->pattern.steps[0][0].set_condition(SeqStep::Condition(which));e->set_bpm(300);activate(*e,phase);
        EngineUiSnapshot s;size_t count=p->count;
        for(int n=0;n<32000;++n){if(n==16000)e->set_sequencer_fill(true);render(*e,1,1);e->capture_sequence_snapshot(s);++snapshots;
            if(p->count!=count){CHECK(s.sequence_hit[0].age_ms==0);count=p->count;}
            if(which==6&&n<16000){CHECK(!p->count);inactive(s.sequence_hit[0]);}
        }
        CHECK(p->count>0);if(which==0)CHECK(p->count>=20);else CHECK(p->count<20);
    }
}
void public_start_and_microtiming(bool phase){
    auto e=std::make_unique<Engine>();auto* p=new Probe(*e,0);e->chains[0].units[0]=p;
    score(*e,4,1,SequenceDirection::Forward,false,false);e->set_bpm(120);e->set_swing(75);
    if(phase){e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,1);e->sigils[0].drawn=true;for(auto& v:e->sigils[0].table)v=0;e->sigils[0].phase_inc=0;e->control_step();}
    e->set_sequencer_enabled(true);e->transport_play();EngineUiSnapshot s;
    for(int n=1;n<4000;++n){render(*e,1,1);e->capture_sequence_snapshot(s);++snapshots;inactive(s.sequence_hit[0]);CHECK(p->count==0);}
    render(*e,3,1);e->capture_sequence_snapshot(s);CHECK(p->count==1&&s.sequence_hit[0].age_ms==0);
    CHECK(p->hits[0].frame>=4000&&p->hits[0].frame<=4001);
    // Musical onset oracle independent from scheduler queues and telemetry. At
    // 120 BPM the 1/16 grid is4000 frames, odd 75%-swing cells add2000. Native
    // legacy offsets round nearest; Q32 phase rounds its due sample upward.
    e->transport_reset();const uint64_t origin=e->audio_frame_;p->count=0;int offsets[4]={23,-23,12,-12};
    for(int n=0;n<4;++n)e->pattern.microtiming[0][n]=int8_t(offsets[n]);
    e->initialize_sequence_clock(true);e->capture_sequence_snapshot(s);inactive(s.sequence_hit[0]);
    size_t last=0;
    for(int n=1;n<=28000;++n){render(*e,1,1);e->capture_sequence_snapshot(s);++snapshots;
        if(p->count!=last){CHECK(p->count==last+1);int tick=int(last);int micro=offsets[tick%4]*4000;
            int delta=micro>=0?(micro+48)/96:-((-micro+48)/96);
            uint64_t expected=uint64_t(tick*4000+(tick%2?2000:0)+delta);
            if(phase){constexpr int64_t quarter=1073741824;constexpr uint64_t increment=268435;
                const uint64_t due=uint64_t(tick*quarter+(tick%2?quarter/2:0)+quarter*offsets[tick%4]/96);
                expected=(due+increment-1)/increment;}
            CHECK(p->hits[last].frame==origin+expected);
            CHECK(s.sequence_hit[0].step==tick%4&&s.sequence_hit[0].bank==0&&s.sequence_hit[0].age_ms==0);last=p->count;
        }
    }
    CHECK(p->count>=7);
}
void transition_identity(bool phase){
    auto e=std::make_unique<Engine>();std::array<Probe*,4> p;
    for(int c=0;c<4;++c){p[c]=new Probe(*e,c);e->chains[c].units[0]=p[c];}
    score(*e,7,4,SequenceDirection::Reverse,false,true);CHECK(e->set_pattern_transition(0,7,0));activate(*e,phase);
    CHECK(e->set_pattern_transition(0,7,32767));CHECK(e->finish_pattern_transition());CHECK(e->active_pattern==7);
    bool old_after_keep=false,new_after_keep=false;
    for(int n=0;n<22000;++n){render(*e,1,1);verify(*e,p);EngineUiSnapshot s;e->capture_sequence_snapshot(s,7);
        if(s.sequence_hit[0].bank==0)old_after_keep=true;
        if(s.sequence_hit[0].bank==7)new_after_keep=true;}
    CHECK(old_after_keep&&new_after_keep);
}
#endif
void native_equivalence(bool phase){
    auto e=std::make_unique<Engine>();
    for(int c=0;c<4;++c){auto* osc=new OscUnit();osc->params[OscUnit::P_GAIN].base=1200;osc->params[OscUnit::P_GAIN].snap();e->chains[c].units[0]=osc;
        auto* vca=new VcaUnit();e->chains[c].units[1]=vca;e->chains[c].volume=1500;e->chains[c].pan=q15(c*4000-6000);}
    score(*e,7,3,SequenceDirection::PingPong,true,true);e->set_bpm(143);e->set_swing(71);activate(*e,phase);
    q15 pcm[514];EngineUiSnapshot s;
    for(int n=0;n<3000;++n){if(n==100)e->set_sequencer_fill(true);if(n==200)CHECK(e->set_pattern_transition(0,7,12000));
        if(n==800){CHECK(e->set_pattern_transition(0,7,32767));CHECK(e->finish_pattern_transition());}
        if(n==1000)e->set_bpm(173);
        if(n==1500)e->request_pattern(3,false);
        if(n==1700)e->set_swing(58);
        if(n==2100){e->transport_stop();e->transport_play();}if(n==2400)e->transport_reset();
        {Guard g;e->render(pcm,257);e->capture_ui_snapshot(s);e->capture_sequence_snapshot(s,n%8);}
        CHECK(std::fwrite(pcm,sizeof(q15),514,pcm_log)==514);pcm_samples+=514;
    }
    CHECK(patchio::save(*e,(directory+(phase?"/phase.snc":"/legacy.snc")).c_str()));
#ifndef SEQUENCE_HIT_BASELINE
    CHECK(patchio::load(*e,(directory+(phase?"/phase.snc":"/legacy.snc")).c_str()));e->capture_ui_snapshot(s);for(auto h:s.sequence_hit)inactive(h);
#endif
}
}
extern "C" void* __real_malloc(size_t);extern "C" void* __real_calloc(size_t,size_t);extern "C" void* __real_realloc(void*,size_t);
extern "C" void* __wrap_malloc(size_t n){if(watching)++allocation_calls;return __real_malloc(n);}
extern "C" void* __wrap_calloc(size_t n,size_t s){if(watching)++allocation_calls;return __real_calloc(n,s);}
extern "C" void* __wrap_realloc(void* p,size_t n){if(watching)++allocation_calls;return __real_realloc(p,n);}
extern "C" void* __real__Znwm(size_t);extern "C" void* __real__Znam(size_t);extern "C" void* __real__ZnwmRKSt9nothrow_t(size_t,const std::nothrow_t&);
extern "C" void* __wrap__Znwm(size_t n){if(watching)++allocation_calls;return __real__Znwm(n);}
extern "C" void* __wrap__Znam(size_t n){if(watching)++allocation_calls;return __real__Znam(n);}
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(size_t n,const std::nothrow_t& t){if(watching)++allocation_calls;return __real__ZnwmRKSt9nothrow_t(n,t);}
int main(int argc,char** argv){CHECK(argc==2);directory=argv[1];event_log=std::fopen((directory+"/events.txt").c_str(),"w");pcm_log=std::fopen((directory+"/output.pcm").c_str(),"wb");CHECK(event_log&&pcm_log);
    for(bool phase:{false,true}){for(int variant=0;variant<6;++variant)matrix(phase,variant);native_equivalence(phase);
#ifndef SEQUENCE_HIT_BASELINE
        precise(phase);lifecycle(phase);conditions(phase);public_start_and_microtiming(phase);transition_identity(phase);
#endif
    }
    CHECK(std::fclose(event_log)==0);CHECK(std::fclose(pcm_log)==0);CHECK(allocation_calls==0);
    std::printf("pass checks=%llu snapshots=%llu observed_triggers=%llu pcm_q15=%llu guarded_allocations=%llu\n",(unsigned long long)checks,(unsigned long long)snapshots,(unsigned long long)observed,(unsigned long long)pcm_samples,(unsigned long long)allocation_calls);
    std::printf("sizeof Engine=%zu EngineUiSnapshot=%zu PendingSequenceEvent=%zu\n",sizeof(Engine),sizeof(EngineUiSnapshot),sizeof(Engine::PendingSequenceEvent));
#ifndef SEQUENCE_HIT_BASELINE
    std::printf("sizeof SequenceHit=%zu snapshot_hit_array=%zu\n",sizeof(SequenceHit),sizeof(EngineUiSnapshot::sequence_hit));
#endif
}
