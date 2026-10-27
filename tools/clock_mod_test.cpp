// Real portable-engine global-clock tests. Private visibility is confined to this
// test TU, after standard-library headers: probes verify queue identity/phase,
// not a parallel model implementation. ProbeUnit observes actual trigger calls.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>
#define private public
#include "../core/engine/engine.h"
#include "../core/engine/units.h"
#undef private
using namespace seance;
static size_t allocation_calls = 0;
#if defined(__SANITIZE_ADDRESS__)
// ASan observes every allocator family without replacing allocation/deallocation
// pairs (in particular its nothrow-new interceptor).
extern "C" void __sanitizer_malloc_hook(const volatile void*,std::size_t) { ++allocation_calls; }
#else
__attribute__((noinline)) void* operator new(std::size_t size) {
    ++allocation_calls;
    if (void* p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
__attribute__((noinline)) void* operator new[](std::size_t size) { return ::operator new(size); }
__attribute__((noinline)) void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
    ++allocation_calls; return std::malloc(size?size:1);
}
__attribute__((noinline)) void* operator new[](std::size_t size,const std::nothrow_t& tag) noexcept { return ::operator new(size,tag); }
__attribute__((noinline)) void operator delete(void* p) noexcept { std::free(p); }
__attribute__((noinline)) void operator delete[](void* p) noexcept { std::free(p); }
__attribute__((noinline)) void operator delete(void* p,std::size_t) noexcept { std::free(p); }
__attribute__((noinline)) void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
__attribute__((noinline)) void operator delete(void* p,const std::nothrow_t&) noexcept { std::free(p); }
__attribute__((noinline)) void operator delete[](void* p,const std::nothrow_t&) noexcept { std::free(p); }
#endif
static int assertions = 0;
#define CHECK(x) do { ++assertions; if (!(x)) { std::fprintf(stderr,"failed %s:%d: %s\n",__FILE__,__LINE__,#x); std::abort(); } } while(0)
static constexpr uint64_t BEAT = 0x100000000ULL;
struct Hit { uint64_t frame, tick; uint8_t pulse; q15 lock; };
struct ProbeUnit : Unit {
    Engine* engine;
    int chain;
    std::array<Hit, 16384> hits{};
    size_t count = 0;
    explicit ProbeUnit(Engine* e, int c) : engine(e), chain(c) { add_param("probe", 8192); }
    void process(q15* output, int frames) override {
        // Stable nonzero PCM exercises the real mixer/limiter too.
        for (int i=0;i<frames;++i) output[i] = params[0].effective / 8;
    }
    void trigger() override {
        CHECK(count < hits.size());
        hits[count++] = {engine->audio_frame_, engine->active_sequence_tick_[chain],
            engine->clock_retrig_next_[chain], params[0].step_lock};
    }
};
static ProbeUnit* probe(Engine& e, int chain=0) {
    auto* p = new ProbeUnit(&e,chain); e.chains[chain].units[0] = p; return p;
}
static void audio_render(Engine& engine,q15* output,int frames) {
    const size_t before=allocation_calls;
    engine.render(output,frames);
    CHECK(allocation_calls==before);
}
static void render(Engine& e, int frames, int chunk=257) {
    q15 pcm[1024];
    while(frames) { int n=std::min(frames,chunk); CHECK(n<=512); audio_render(e,pcm,n); frames-=n; }
}
static void constant(Sigil& s, q15 value) { s.drawn=true; for(auto& v:s.table) v=value; s.phase_inc=0; }
static void setup_score(Engine& e, bool conditions=false) {
    e.sequencer_enabled=true;
    for(int c=0;c<NUM_CHAINS;++c) for(int i=0;i<SEQ_STEPS;++i) {
        SeqStep& step=e.pattern.steps[c][i]; step.flags=SeqStep::Trig;
        step.set_retrig_count(4); step.lock_count=1; step.locks[0]={0,0,(q15)(1000+i*333)};
        if(conditions) step.set_condition((SeqStep::Condition)(1+i%3));
        e.pattern.microtiming[c][i]=(int8_t)((i%2)?23:-23);
    }
    e.transport_play();
    e.initialize_sequence_clock(true);
}
static void test_api_mapping() {
    auto e=std::make_unique<Engine>();
    CHECK(!e->set_clock_mod(ClockTarget::Count,ModSource::SigilA,1));
    CHECK(!e->set_clock_mod(ClockTarget::Tempo,ModSource::EnvFollow,1));
    CHECK(!e->set_clock_mod(ClockTarget::Tempo,(ModSource)255,1));
    CHECK(e->clock_mod(ClockTarget::Count).src==ModSource::None);
    for(int target=0;target<2;++target) for(int source=1;source<=4;++source)
        for(int depth: {-1000,-25,-1,0,1,25,1000}) {
        const auto t=(ClockTarget)target; const int limit=target?25:280;
        const int clamped=std::max(-limit,std::min(limit,depth));
        e->set_clock_mod(t,(ModSource)source,depth);
        CHECK(e->clock_mod(t).depth==clamped);
        CHECK(!e->set_clock_mod(t,(ModSource)source,depth));
        for(int value: {-32768,-16384,0,16384,32767}) {
            e->sigils[source-1].value=(q15)value;
            ClockInspector out; e->capture_clock_inspector(t,out);
            const int64_t wanted=(int64_t)out.base*65536+(int64_t)value*clamped*65536/(value<0?32768:32767);
            const int64_t low=(target?50:20)*65536,high=(target?75:300)*65536;
            CHECK(out.target_q16==std::max(low,std::min(high,wanted)));
            CHECK(out.limited==(wanted<low||wanted>high));
            CHECK(out.source==value);
            CHECK(out.applied_q16==(target?e->effective_swing_q16():e->effective_bpm_q16()));
        }
    }
    CHECK(e->bpm==120 && e->swing_percent==50);
    ClockInspector invalid; e->capture_clock_inspector((ClockTarget)255,invalid); CHECK(invalid.base==0);
}
static void test_dormant_exact() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
    auto* pa=probe(*a);auto* pb=probe(*b);
    b->set_clock_mod(ClockTarget::Tempo,ModSource::None,280);
    b->set_clock_mod(ClockTarget::Swing,ModSource::SigilD,0);
    CHECK(!b->clock_runtime_);
    a->set_bpm(137); b->set_bpm(137); a->set_swing(67);b->set_swing(67);
    setup_score(*a,true);setup_score(*b,true);
    q15 left[514],right[514];
    for(int i=0;i<4000;++i) {
        audio_render(*a,left,257);audio_render(*b,right,257);
        CHECK(std::memcmp(left,right,sizeof left)==0);
        CHECK(a->sequence_rng_==b->sequence_rng_);
        CHECK(a->transport_frame==b->transport_frame && a->frame_in_beat==b->frame_in_beat);
        CHECK(a->sequence_tick==b->sequence_tick);
        CHECK(pa->count==pb->count);
    }
    for(size_t i=0;i<pa->count;++i) CHECK(pa->hits[i].frame==pb->hits[i].frame && pa->hits[i].lock==pb->hits[i].lock);
}
static void test_queue_identity() {
    auto e=std::make_unique<Engine>();probe(*e);setup_score(*e,true); render(*e,777);
    const auto old_rng=e->sequence_rng_; const auto old_tick=e->sequence_tick;
    std::array<Engine::PendingSequenceEvent,8> copy;int k=0;
    for(const auto& c:e->pending_sequence_events_)for(const auto& p:c)copy[k++]=p;
    e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,280);
    e->set_clock_mod(ClockTarget::Swing,ModSource::SigilB,25);
    constant(e->sigils[0],32767);constant(e->sigils[1],32767);
    for(int i=0;i<20;++i) e->control_step(); // no musical time: no unrelated event due.
    CHECK(e->sequence_clock_active_);CHECK(e->sequence_rng_==old_rng);CHECK(e->sequence_tick==old_tick);
    k=0;for(const auto& c:e->pending_sequence_events_)for(const auto& p:c) {
        CHECK(p.valid==copy[k].valid); CHECK(p.tick==copy[k].tick); CHECK(p.condition_pass==copy[k].condition_pass);
        CHECK(std::memcmp(&p.step,&copy[k].step,sizeof p.step)==0); ++k;
    }
    CHECK(e->bpm==120 && e->swing_percent==50);
    e->set_bpm(100);e->set_swing(60);
    CHECK(e->sequence_rng_==old_rng && e->sequence_clock_active_);
}
static void test_dynamic_score() {
    auto e=std::make_unique<Engine>();auto* p=probe(*e);
    e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,280);
    e->set_clock_mod(ClockTarget::Swing,ModSource::SigilB,25);
    setup_score(*e); uint64_t last_phase=0,last_frame=0;
    q15 pcm[64];
    for(int i=0;i<30000;++i) {
        constant(e->sigils[0],(i/7)%2?-32768:32767);
        constant(e->sigils[1],(i/113)%2?-32768:32767);
        audio_render(*e,pcm,32);
        CHECK(e->clock_phase_>last_phase);CHECK(e->transport_frame==last_frame+32);
        CHECK(e->frame_in_beat<e->frames_per_beat());
        CHECK(e->effective_bpm_q16()>=20*65536 && e->effective_bpm_q16()<=300*65536);
        CHECK(e->effective_swing_q16()>=50*65536 && e->effective_swing_q16()<=75*65536);
        last_phase=e->clock_phase_;last_frame=e->transport_frame;
        uint64_t low=UINT64_MAX, high=0;
        for(const auto& event:e->pending_sequence_events_[0])if(event.valid) {low=std::min(low,event.clock_phase);high=std::max(high,event.clock_phase);}
        CHECK(high>low); // worst +/-micro and swing never reorder/collapse neighbours.
    }
    CHECK(p->count>500);
    for(size_t i=1;i<p->count;++i) {CHECK(p->hits[i].frame>=p->hits[i-1].frame);CHECK(p->hits[i].tick>=p->hits[i-1].tick);}
    for(uint64_t tick=0;tick+1<e->active_sequence_tick_[0];++tick) {
        int count=0;for(size_t i=0;i<p->count;++i)if(p->hits[i].tick==tick) {++count;CHECK(p->hits[i].lock==(q15)(1000+(tick%16)*333));}
        CHECK(count==4);
    }
    CHECK(e->bpm==120 && e->swing_percent==50);
}
static void test_feedback_and_consumers() {
    auto e=std::make_unique<Engine>();
    auto* g=new GrainUnit();auto* s=new StranularUnit();e->chains[0].units[0]=g;e->chains[1].units[0]=s;
    g->clock_sync=true;s->params[StranularUnit::P_SYNC].base=32767;
    for(int i=0;i<4;++i) {constant(e->sigils[i],16384);e->set_sigil_sync(i,true);e->set_sigil_div(i,i);}
    e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilD,37);
    for(int k=0;k<800;++k) {
        const int32_t previous=e->effective_bpm_q16();
        e->control_step();
        for(int i=0;i<4;++i) {Sigil expected;expected.set_rate_sync_q16(previous,i,CONTROL_RATE);CHECK(e->sigils[i].phase_inc==expected.phase_inc);}
        CHECK(g->clock_bpm_q16_==e->effective_bpm_q16());CHECK(s->clock_bpm_q16_==e->effective_bpm_q16());
    }
    CHECK(e->effective_bpm_q16()%65536!=0);
    CHECK(e->frames_per_beat()==(uint32_t)(((uint64_t)ENGINE_SR*60*65536+e->effective_bpm_q16()/2)/e->effective_bpm_q16()));
    e->set_clock_mod(ClockTarget::Tempo,ModSource::None,37);render(*e,32000);
    CHECK(e->effective_bpm_q16()==120*65536);
    e->transport_reset();CHECK(!e->clock_runtime_);CHECK(e->clock_mod(ClockTarget::Tempo).depth==37);
}
static void test_chunking_quantized_and_roll() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();auto* pa=probe(*a);auto* pb=probe(*b);
    for(auto* e:{a.get(),b.get()}) {
        e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,210);
        e->set_clock_mod(ClockTarget::Swing,ModSource::SigilB,25);
        for(int i=0;i<128;++i) {e->sigils[0].table[i]=(q15)(i*511-32700);e->sigils[1].table[i]=(q15)(32700-i*511);}
        e->sigils[0].drawn=e->sigils[1].drawn=true;e->set_sigil_rate(0,32000);e->set_sigil_rate(1,25000);
        setup_score(*e,true);
    }
    render(*a,600000,32);render(*b,600000,7);
    CHECK(a->clock_phase_==b->clock_phase_);CHECK(a->clock_audio_phase_==b->clock_audio_phase_);
    CHECK(a->sequence_rng_==b->sequence_rng_);CHECK(pa->count==pb->count);
    for(size_t i=0;i<pa->count;++i) CHECK(pa->hits[i].frame==pb->hits[i].frame && pa->hits[i].tick==pb->hits[i].tick);
    auto e=std::make_unique<Engine>();auto* p=probe(*e);
    e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,100);constant(e->sigils[0],32767);e->transport_play();
    e->launch_quantize=LaunchQuantize::Beat;e->pending_triggers=1;e->pending_pattern=1;e->pattern_launch_quantize=LaunchQuantize::Beat;
    render(*e,18000,11);CHECK(!e->pending_triggers);CHECK(e->active_pattern==1);CHECK(p->count==1);
    e->transport_stop();CHECK(!e->transport_playing);
    e->set_live_retrigger(0,3,true);
    const size_t initial=p->count;const uint64_t phase=e->clock_audio_phase_;
    render(*e,64000,13);
    const uint64_t elapsed=e->clock_audio_phase_-phase;
    CHECK(p->count-initial==elapsed/(BEAT>>3));
    CHECK(e->transport_frame<18001); // roll follows audio clock even stopped.
}
static void test_record_endpoints_and_probability() {
    for(auto quantize: {LaunchQuantize::Sixteenth,LaunchQuantize::Beat,LaunchQuantize::Bar}) {
        auto e=std::make_unique<Engine>();
        auto* rec=new RecUnit(); e->chains[0].units[0]=new OscUnit();e->chains[0].units[1]=rec;
        rec->params[RecUnit::P_LEN].base=32767;
        CHECK(e->prepare_recorder(*rec,true)==trackr::synth::SampleEditResult::Ok);
        e->transport_play();render(*e,37);
        CHECK(e->request_recorder_record(0,1,true,quantize)==trackr::synth::SampleEditResult::Ok);
        const uint32_t generation=rec->take_generation();
        const uint64_t old_deadline=e->pending_recorder_command_.deadline;
        const uint64_t old_phase=(uint64_t)e->beat_count*BEAT+(uint64_t)e->frame_in_beat*BEAT/e->frames_per_beat();
        const uint64_t expected_phase=old_phase+(old_deadline-e->transport_frame)*BEAT/e->frames_per_beat();
        const auto unrelated=quantize==LaunchQuantize::Sixteenth?LaunchQuantize::Bar:LaunchQuantize::Sixteenth;
        e->request_chain_trigger(3,unrelated);
        e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilC,170);constant(e->sigils[2],32767);
        const uint64_t start_phase=e->clock_recorder_phase_;
        CHECK(start_phase==expected_phase);
        q15 pcm[2];int guard=200000;
        while(!rec->is_recording() && --guard) {
            CHECK(rec->take_generation()==generation);
            CHECK(e->pending_recorder_command_.valid);
            audio_render(*e,pcm,1);
        }
        CHECK(guard>0);CHECK(!e->pending_recorder_command_.valid);
        CHECK(e->clock_phase_>=start_phase && e->clock_phase_-start_phase<e->clock_increment_);
        CHECK(rec->recorded_frames()==0);
        const uint64_t begin=e->audio_frame_;
        CHECK(e->request_recorder_record(0,1,false,quantize)==trackr::synth::SampleEditResult::Ok);
        const uint64_t stop_phase=e->clock_recorder_phase_;
        e->request_chain_trigger(3,unrelated);
        constant(e->sigils[2],0);guard=200000;
        while(rec->is_recording() && --guard) audio_render(*e,pcm,1);
        CHECK(guard>0);CHECK(!e->pending_recorder_command_.valid);
        CHECK(e->clock_phase_>=stop_phase && e->clock_phase_-stop_phase<e->clock_increment_);
        CHECK(rec->recorded_frames()==e->audio_frame_-begin);
        const int slot=rec->sample_slot;e.reset();trackr::synth::SampleBank::instance().clear_slot(slot);
    }
    auto e=std::make_unique<Engine>();auto* p=probe(*e);
    e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilD,280);
    e->set_clock_mod(ClockTarget::Swing,ModSource::SigilB,25);
    setup_score(*e,true);
    std::array<int8_t,4096> decision;decision.fill(-1);decision[0]=e->active_sequence_pass_[0];
    q15 pcm[64];
    for(int block=0;block<30000;++block) {
        for(const auto& event:e->pending_sequence_events_[0])if(event.valid) {
            CHECK(event.tick<decision.size());
            if(decision[event.tick]!=-1) CHECK(decision[event.tick]==event.condition_pass);
            decision[event.tick]=(int8_t)event.condition_pass;
        }
        constant(e->sigils[3],block%67<21?-32768:32767);constant(e->sigils[1],block%200<100?32767:0);
        audio_render(*e,pcm,32);
    }
    uint32_t rng=0x53455131u;
    for(uint64_t i=0;i<e->sequence_plan_tick_*NUM_CHAINS;++i) rng=rng*1664525u+1013904223u;
    CHECK(e->sequence_rng_==rng); // exactly one decision per original planned identity.
    for(uint64_t tick=0;tick<e->active_sequence_tick_[0];++tick) {
        CHECK(decision[tick]>=0);int actual=0;
        for(size_t i=0;i<p->count;++i)if(p->hits[i].tick==tick)++actual;
        CHECK(actual==(decision[tick]?4:0));
    }
}
static void test_midstep_activation_and_grid() {
    for(int bpm: {20,137,300}) for(int swing: {50,75}) for(int offset: {1,777,1250,2490,3700,4960,15001}) {
        auto e=std::make_unique<Engine>();auto* p=probe(*e);e->set_bpm(bpm);e->set_swing(swing);setup_score(*e);
        render(*e,offset,31);const uint64_t frame=e->transport_frame;
        const size_t before=p->count;
        e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilD,280);
        e->set_clock_mod(ClockTarget::Swing,ModSource::SigilC,-25);
        CHECK(e->transport_frame==frame && p->count==before);
        constant(e->sigils[3],32767);constant(e->sigils[2],32767);
        render(*e,317,7);
        e->set_clock_mod(ClockTarget::Tempo,ModSource::None,280);
        e->set_clock_mod(ClockTarget::Swing,ModSource::SigilC,0);
        render(*e,110000,257);
        for(uint64_t tick=0;tick<e->active_sequence_tick_[0];++tick) {
            int count=0;for(size_t i=0;i<p->count;++i)if(p->hits[i].tick==tick)++count;
            CHECK(count==4);
        }
        for(size_t i=1;i<p->count;++i) CHECK(p->hits[i].frame>=p->hits[i-1].frame && p->hits[i].tick>=p->hits[i-1].tick);
    }
    auto e=std::make_unique<Engine>();auto* p=probe(*e);
    e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,1);
    e->sequencer_enabled=true;for(int i=0;i<16;++i)e->pattern.steps[0][i].flags=SeqStep::Trig;
    e->transport_play();e->initialize_sequence_clock(true);const uint32_t increment=e->clock_increment_;
    render(*e,16001,17);CHECK(p->count==5);
    for(size_t i=0;i<p->count;++i) CHECK(p->hits[i].frame==(i*(BEAT/4)+increment-1)/increment);
    e->transport_stop();e->set_live_retrigger(0,3,true);render(*e,137);
    const uint64_t remaining=e->clock_roll_phase_-e->clock_audio_phase_;const size_t hits=p->count;
    e->transport_reset();CHECK(e->clock_roll_phase_-e->clock_audio_phase_==remaining);
    CHECK(p->count==hits);render(*e,31);CHECK(p->count==hits);
}
static void test_temp_swap_reset() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();a->chains[0].units[0]=new OscUnit();
    a->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,130);constant(a->sigils[0],32767);
    CHECK(a->begin_param_temp(0,0,0));
    render(*a,3200);CHECK(a->param_temp_active());a->set_bpm(140);CHECK(a->param_temp_active());
    CHECK(a->effective_bpm()>140);b->set_bpm(73);b->set_swing(64);
    a->swap_patch_state(*b);
    CHECK(a->bpm==73 && a->effective_bpm()==73 && a->effective_swing_q16()==64*65536);
    CHECK(a->clock_mod(ClockTarget::Tempo).src==ModSource::None);CHECK(!a->clock_runtime_);
    CHECK(b->clock_mod(ClockTarget::Tempo).src==ModSource::SigilA);CHECK(b->effective_bpm()==140);
    CHECK(a->transport_frame==0 && b->transport_frame==0);
    b->transport_play();render(*b,1000);b->transport_stop();const uint64_t phase=b->clock_phase_;render(*b,1000);
    CHECK(b->clock_phase_==phase);b->transport_play();render(*b,1000);CHECK(b->clock_phase_>phase);
    b->transport_reset();CHECK(b->clock_phase_==0 && b->effective_bpm()==140);
}
int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    test_api_mapping();std::puts("clock api/mapping passed");
    test_dormant_exact();std::puts("clock dormant exact PCM/state passed");
    test_queue_identity();std::puts("clock queue identity/decisions passed");
    test_dynamic_score();std::puts("clock dynamic events/retrigs/locks passed");
    test_feedback_and_consumers();std::puts("clock causal feedback/fractional consumers passed");
    test_chunking_quantized_and_roll();std::puts("clock chunking/quantization/roll passed");
    test_record_endpoints_and_probability();std::puts("clock exact recorder endpoints/probability ledger passed");
    test_midstep_activation_and_grid();std::puts("clock midstep activation/deactivation/grid/roll-reset passed");
    test_temp_swap_reset();std::puts("clock temp/swap/reset passed");
    std::printf("clock modulation: 9/9 passed; %d assertions; every render allocation-audited\n",assertions);
    std::printf("clock sizes: engine=%zu, hot snapshot=%zu, route=%zu, inspector=%zu bytes\n",
        sizeof(Engine),sizeof(EngineUiSnapshot),sizeof(ClockMod),sizeof(ClockInspector));
}
