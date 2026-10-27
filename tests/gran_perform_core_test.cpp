// Focused GRAN perform proof. GRAN_PERFORM_BASELINE_ONLY compiles against the
// retained pre-edit engine snapshot; fixture hashes are captured, not twins.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <unistd.h>
// Read-only runtime probes of accepted real-engine events. No copied scheduler.
#define private public
#include "engine/units.h"
#include "engine/engine.h"
#undef private
#include "engine/patch_io.h"
static size_t allocation_calls=0;
#if defined(__SANITIZE_ADDRESS__)
extern "C" void __sanitizer_malloc_hook(const volatile void*,std::size_t) { ++allocation_calls; }
#endif
using namespace seance;
namespace {
using G = GrainUnit;
using Pcm = std::vector<q15>;
using Bytes = std::vector<uint8_t>;
using trackr::synth::Sample;
using trackr::synth::SampleBank;
std::string workdir;
unsigned long long checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
void set(G& g,int p,int v) { g.params[p].base=(q15)v; g.params[p].snap(); }
void source(uint32_t n,int channels=1) {
    Sample& s=SampleBank::instance().slot(0); s=Sample{}; s.channels=(uint8_t)channels;
    s.data.resize(size_t(n)*channels);
    for(size_t i=0;i<s.data.size();++i) s.data[i]=(q15)(int((i*1931u+i/37u*173u)&65535u)-32768);
}
uint64_t word(uint64_t h,uint64_t value,int bytes) {
    for(int i=0;i<bytes;++i) { h^=(value>>(i*8))&255u; h*=1099511628211ull; } return h;
}
uint32_t random(uint32_t& r) { r^=r<<13; r^=r>>17; r^=r<<5; return r; }
[[maybe_unused]] Pcm pcm(G& g,int n) { Pcm b(n,12345); const size_t before=allocation_calls; g.process(b.data(),n); CHECK(allocation_calls==before); return b; }
void frozen_compatibility(const char* output) {
    std::ofstream file(std::string(output)+"/frozen.pcm",std::ios::binary); CHECK(file.good());
    const uint64_t legacy[6][2]={
        {13697533497414174360ull,3457870170833807644ull},
        {12797975628368176092ull,11029909418462138045ull},
        {5294875777066716196ull,6432844174693943600ull},
        {8715760795036705124ull,16768493585765774048ull},
        {2092638215044945363ull,13824015210851502757ull},
        {240000616536979117ull,7733924737710168545ull}};
    for(int clean=0;clean<=1;++clean) for(int fixture=0;fixture<6;++fixture) {
        source(fixture==4?257:65536,fixture%2+1); G g; g.clean=clean;
        set(g,G::P_SCAN,fixture%2?-32767:32767);
        uint64_t ph=1469598103934665603ull,vh=ph,head_hash=ph; uint64_t frames=0;
        uint32_t seed=0x82abcdefu+fixture;
        for(int block=0;block<800;++block) {
            if(block%11==0) {
                for(int p=0;p<11;++p) { int v=random(seed)&32767; if(p==G::P_PITCH&&fixture%2)v=-v; set(g,p,v); }
                g.clock_sync=fixture==2; g.sync_division=block%4;
                g.set_transport_bpm_q16((20+block%281)*65536+123); g.control_tick();
            }
            if(block%29==0)g.trigger();
            if(block==399)g.reset();
            q15 b[127]; const int n=1+random(seed)%127; g.process(b,n); frames+=n;
            for(int i=0;i<n;++i) { ph=word(ph,(uint16_t)b[i],2); const uint8_t raw[]={uint8_t(b[i]),uint8_t(uint16_t(b[i])>>8)}; file.write((const char*)raw,2); }
            vh=word(vh,g.visual_spawned(),4); vh=word(vh,g.visual_grains(),2);
            vh=word(vh,g.visual_active(),1); vh=word(vh,g.visual_interval(),2);
            for(int i=0;i<8;++i) { vh=word(vh,g.visual_positions()[i],1); vh=word(vh,g.visual_phases()[i],1); vh=word(vh,(uint8_t)g.visual_pitch_offsets()[i],1); }
            if(clean) {
                G::VisualGrain v[8]; g.capture_visual_grains(v); head_hash=word(head_hash,(uint16_t)g.scan_position(),2);
                for(const auto& x:v) { head_hash=word(head_hash,x.active,1); head_hash=word(head_hash,x.position_q16,8); head_hash=word(head_hash,x.start_q16,8); head_hash=word(head_hash,x.end_q16,8); head_hash=word(head_hash,x.age,4); head_hash=word(head_hash,x.length,4); head_hash=word(head_hash,x.attack,4); head_hash=word(head_hash,x.release,4); head_hash=word(head_hash,(uint16_t)x.envelope,2); }
            }
        }
        if(!clean) { CHECK(ph==legacy[fixture][0]); CHECK(vh==legacy[fixture][1]); }
        std::printf("fixture %s-%d pcm=%llu telemetry=%llu heads=%llu frames=%llu\n",clean?"clean":"legacy",fixture,(unsigned long long)ph,(unsigned long long)vh,(unsigned long long)head_hash,(unsigned long long)frames);
    }
    CHECK(file.good()); std::puts("ok frozen pre-edit clean-flow-linear + pre-v2 legacy fixtures");
}
#ifndef GRAN_PERFORM_BASELINE_ONLY
using Mode=G::LaunchMode;
using Window=G::Window;
void config(G& g,Mode mode=Mode::Gate,Window window=Window::Linear,int size=32767) {
    g.set_launch_mode(mode); g.set_window(window); g.set_burst_count(4);
    set(g,G::P_PITCH,8192); set(g,G::P_POS,8192); set(g,G::P_SIZE,size);
    set(g,G::P_ATTACK,0); set(g,G::P_RELEASE,0); set(g,G::P_SPRAY,0);
    set(g,G::P_PITCH_DEV,0); set(g,G::P_JITTER,0); set(g,G::P_DENS,32767);
    set(g,G::P_SCAN,0); set(g,G::P_GAIN,32767); set(g,G::P_VOICES,32767);
    g.control_tick();
}
int capture(G& g,G::VisualGrain (&v)[8]) { g.capture_visual_grains(v); int n=0; for(auto& x:v)n+=x.active; return n; }
void same_heads(G& a,G& b) {
    G::VisualGrain va[8],vb[8]; CHECK(capture(a,va)==capture(b,vb));
    for(int i=0;i<8;++i) { const auto& x=va[i]; const auto& y=vb[i];
        CHECK(x.active==y.active && x.position_q16==y.position_q16 && x.start_q16==y.start_q16 && x.end_q16==y.end_q16);
        CHECK(x.age==y.age && x.length==y.length && x.attack==y.attack && x.release==y.release && x.envelope==y.envelope && x.window==y.window);
    }
    CHECK(a.visual_spawned()==b.visual_spawned()); CHECK(a.scan_position()==b.scan_position());
}
void defaults_legacy() {
    G g; CHECK(g.clean && g.launch_mode==Mode::Flow && g.window==Window::Linear && g.burst_count==4);
    CHECK(G::MAX_GRAINS==8 && G::MAX_TAILS==8 && G::P_COUNT==12);
    source(65536); G a,b; a.clean=false; b.clean=false;
    b.launch_mode=Mode::Burst; b.window=Window::Hann; b.burst_count=8;
    for(int k=0;k<31;++k) { if(k%3==0) { a.trigger(); b.trigger(); } CHECK(pcm(a,137)==pcm(b,137)); }
    b.set_launch_mode(Mode::Gate); b.set_window(Window::Hann); b.set_burst_count(8); CHECK(!b.clean);
    CHECK(b.sequencer_properties().lifecycle==SequenceLifecycle::Flow);
    G invalid; invalid.launch_mode=(Mode)255; invalid.window=(Window)255; invalid.burst_count=0; invalid.control_tick();
    CHECK(invalid.launch_mode==Mode::Flow && invalid.window==Window::Linear && invalid.burst_count>=1 && invalid.burst_count<=16);
    std::puts("ok defaults/legacy dormant config, invalid direct normalization, fixed budgets");
}
void gate_ownership() {
    source(65536); G g; config(g); set(g,G::P_SCAN,8192); g.control_tick();
    CHECK(pcm(g,1000)==Pcm(1000,0)); CHECK(!g.gate_open() && g.visual_spawned()==0 && g.scan_position()!=8192);
    const auto t1=g.pad_begin(); CHECK(t1 && g.gate_open()); pcm(g,1); CHECK(g.visual_spawned()==1);
    const auto t2=g.pad_begin(); CHECK(t2 && t2!=t1); g.pad_end(t1); CHECK(g.gate_open());
    pcm(g,321); CHECK(g.visual_spawned()==3); g.pad_end(0); CHECK(g.gate_open());
    g.pad_end(t2); CHECK(!g.gate_open()); const auto spawned=g.visual_spawned(); const auto scan=g.scan_position();
    G::VisualGrain v[8]; CHECK(capture(g,v)>0); const auto age=v[0].age;
    pcm(g,1000); CHECK(g.visual_spawned()==spawned && g.scan_position()!=scan); CHECK(capture(g,v)>0 && v[0].age==age+1000);
    g.trigger(); CHECK(!g.gate_open()); pcm(g,4000); CHECK(g.visual_spawned()==spawned+1);
    const auto owned=g.pad_begin(); pcm(g,1); const auto before=g.visual_spawned();
    g.trigger(); pcm(g,961); CHECK(g.gate_open() && g.visual_spawned()==before+4); g.pad_end(owned); CHECK(!g.gate_open());
    // First-use source binding must happen before recording the request.
    for(bool pad:{false,true}) { G cold; config(cold); uint32_t token=0; if(pad)token=cold.pad_begin(); else cold.trigger();
        pcm(cold,1); CHECK(cold.visual_spawned()==1); if(pad) { CHECK(cold.gate_open()); cold.pad_end(token); } }
    G lifted; config(lifted); const auto early=lifted.pad_begin(); lifted.pad_end(early); CHECK(pcm(lifted,1000)==Pcm(1000,0) && lifted.visual_spawned()==0);
    // A mode change preserves audible heads but invalidates the old ownership.
    G fresh; config(fresh); const auto old=fresh.pad_begin(); pcm(fresh,17); CHECK(capture(fresh,v)==1);
    fresh.set_launch_mode(Mode::Burst); CHECK(!fresh.gate_open()); CHECK(capture(fresh,v)==1 && v[0].age==17);
    fresh.set_launch_mode(Mode::Gate); const auto now=fresh.pad_begin(); fresh.pad_end(old); CHECK(fresh.gate_open()); fresh.pad_end(now);
    // Controller identity includes an address, but allocators may reuse it. A
    // stale captured token must never own a replacement card at that address.
    alignas(G) unsigned char storage[sizeof(G)];
    auto* first=new(storage) G; config(*first); const auto retired=first->pad_begin(); first->~GrainUnit();
    auto* replacement=new(storage) G; config(*replacement); const auto current=replacement->pad_begin();
    CHECK(retired && current && retired!=current); replacement->pad_end(retired); CHECK(replacement->gate_open());
    replacement->pad_end(current); CHECK(!replacement->gate_open()); replacement->~GrainUnit();
    std::puts("ok gate first-use/manual onset/own-pad stale-token + same-address replacement ownership, lift drain and scan, mode-change tails");
}
void burst_and_flow() {
    source(65536);
    for(int count:{1,4,8,16}) for(bool pad:{false,true}) {
        G g; config(g,Mode::Burst); g.set_burst_count(count);
        uint32_t token=0; if(pad)token=g.pad_begin(); else g.trigger(); CHECK(g.burst_remaining()==count);
        for(int i=1;i<=count;++i) { pcm(g,320); CHECK(g.visual_spawned()==unsigned(i)); CHECK(g.burst_remaining()==count-i); }
        pcm(g,32000); CHECK(g.visual_spawned()==unsigned(count) && g.burst_remaining()==0); g.pad_end(token);
    }
    G g; config(g,Mode::Burst); g.pad_begin(); pcm(g,1); CHECK(g.burst_remaining()==3);
    g.pad_begin(); CHECK(g.burst_remaining()==4); pcm(g,2000); CHECK(g.visual_spawned()==5 && g.burst_remaining()==0);
    G count; config(count,Mode::Burst); count.trigger(); pcm(count,1); count.set_burst_count(16); CHECK(count.burst_remaining()==3);
    pcm(count,2000); CHECK(count.visual_spawned()==4); count.trigger(); CHECK(count.burst_remaining()==16);
    G a,b; config(a,Mode::Flow); set(a,G::P_JITTER,20000); set(a,G::P_SPRAY,10000); a.control_tick(); b=a;
    CHECK(pcm(a,4321)==pcm(b,4321)); a.pad_begin(); b.trigger(); CHECK(pcm(a,12345)==pcm(b,12345)); same_heads(a,b);
    set(a,G::P_SCAN,32767); set(b,G::P_SCAN,32767); a.control_tick(); b.control_tick(); CHECK(pcm(a,1001)==pcm(b,1001));
    a.bypass=true; a.trigger(); b.trigger(); CHECK(a.scan_position()==a.params[G::P_POS].effective && a.scan_position()==b.scan_position());
    a.bypass=false; CHECK(pcm(a,4001)==pcm(b,4001));
    std::puts("ok finite scheduled bursts 1/4/8/16, replacement rather than accumulation, no held repeat; flow restart exact trigger");
}
void partitions() {
    source(65536,2);
    for(Mode mode:{Mode::Flow,Mode::Gate,Mode::Burst}) for(Window window:{Window::Linear,Window::Hann})
    for(int jitter:{0,32767}) for(bool clock:{false,true}) {
        G a; config(a,mode,window); set(a,G::P_JITTER,jitter); set(a,G::P_SCAN,-21345); set(a,G::P_SPRAY,23456); set(a,G::P_PITCH_DEV,12345);
        a.clock_sync=clock; a.set_transport_bpm_q16(137*65536+123); a.sync_division=3; a.control_tick();
        G b=a; a.pad_begin(); b.pad_begin(); const auto expected=pcm(a,19999); Pcm got;
        uint32_t seed=0x81234567u; int remaining=19999;
        while(remaining) { int n=std::min(remaining,1+int(random(seed)%257)); const auto part=pcm(b,n); got.insert(got.end(),part.begin(),part.end()); remaining-=n; }
        CHECK(got==expected); same_heads(a,b); CHECK(a.burst_remaining()==b.burst_remaining());
    }
    std::puts("ok all modes/windows/free+sync/jitter+stereo+spray+pdev+scan process partition exact (24 combinations)");
}
void windows_and_tails() {
    double worst=0;
    for(uint32_t n:{1u,2u,3u,63u,256u,960u,8001u,16000u}) for(uint32_t age=0;age<=n;++age) {
        const int v=G::visual_window_q15(age,n,7,21,Window::Hann);
        const double expected=n<=1||age>=n?0:32767.0*0.5*(1.0-std::cos(2*3.14159265358979323846*age/(n-1)));
        worst=std::max(worst,std::abs(v-expected)); CHECK(std::abs(v-expected)<=4.0);
        CHECK(v==G::visual_window_q15(age,n,0,0,Window::Hann));
        if(age<n)CHECK(v==G::visual_window_q15(n-1-age,n,123,456,Window::Hann));
    }
    for(Window window:{Window::Linear,Window::Hann}) for(int n:{256,1024,65536}) for(int pitch:{-32767,-8192,8192,32767}) {
        source(n); std::fill(SampleBank::instance().slot(0).data.begin(),SampleBank::instance().slot(0).data.end(),32767);
        G g; config(g,Mode::Gate,window); set(g,G::P_PITCH,pitch); set(g,G::P_ATTACK,26847); set(g,G::P_RELEASE,12345); g.control_tick(); g.trigger();
        const int first=G::visual_window_q15(0,std::min<uint32_t>(16000,(uint64_t)(n-3)*65536/std::abs(pitch*8)),1,1,window);
        CHECK(pcm(g,1)[0]==(int64_t((32767*first)>>15)*32767>>18)); G::VisualGrain v[8]; CHECK(capture(g,v)==1);
        const uint32_t length=v[0].length,attack=v[0].attack,release=v[0].release;
        for(uint32_t age=1;age<length;++age) {
            CHECK(capture(g,v)==1 && v[0].age==age && v[0].window==window && v[0].attack==attack && v[0].release==release);
            const int env=G::visual_window_q15(age,length,attack,release,window); CHECK(v[0].envelope==env);
            CHECK(pcm(g,1)[0]==(int64_t((32767*env)>>15)*32767>>18));
            if(age==5) { g.set_window(window==Window::Hann?Window::Linear:Window::Hann); set(g,G::P_ATTACK,0); set(g,G::P_RELEASE,0); set(g,G::P_SIZE,0); g.control_tick(); }
        }
        CHECK(capture(g,v)==0 && pcm(g,1)[0]==0);
    }
    // A stolen Hann head remains Hann after the next head switches to linear.
    source(65536); std::fill(SampleBank::instance().slot(0).data.begin(),SampleBank::instance().slot(0).data.end(),32000);
    G g; config(g,Mode::Gate,Window::Hann,0); set(g,G::P_VOICES,0); g.trigger(); pcm(g,401);
    G::VisualGrain v[8]; CHECK(capture(g,v)==1); const auto old=v[0]; g.set_window(Window::Linear); g.trigger();
    for(uint32_t i=0;i<G::DECLICK_FRAMES;++i) {
        const int env=G::visual_window_q15(old.age+i,old.length,old.attack,old.release,Window::Hann);
        const int fade=(G::DECLICK_FRAMES-1-i)*32767u/(G::DECLICK_FRAMES-1);
        const int shaped=(env*fade)>>15; const int sum=((32000*32767)>>15)+((32000*shaped)>>15);
        CHECK(pcm(g,1)[0]==(int64_t(sum)*32767>>18));
    }
    G eight; config(eight); for(int i=0;i<8;++i) { eight.trigger(); pcm(eight,1); }
    const int sum=8*((32000*32767)>>15); CHECK(pcm(eight,1)[0]==(int64_t(sum)*32767>>18));
    set(eight,G::P_GAIN,8192); CHECK(pcm(eight,1)[0]==(int64_t(sum)*8192>>18));
    G stress; config(stress,Mode::Burst,Window::Hann); set(stress,G::P_GAIN,16384);
    for(int i=0;i<1000;++i) { stress.trigger(); const auto b=pcm(stress,1); CHECK(std::abs(int(b[0]))<32767); }
    std::printf("ok Hann endpoints/reference/symmetry (worst %.6f q15), own-latched exact next-read audio/visual, tail shape and fixed headroom\n",worst);
}
void sources_and_reset() {
    source(65536); G g; config(g); auto token=g.pad_begin(); pcm(g,71); g.reset(); CHECK(!g.gate_open() && !g.burst_remaining());
    auto next=g.pad_begin(); g.pad_end(token); CHECK(g.gate_open()); pcm(g,1); g.pad_end(next);
    for(int change=0;change<6;++change) {
        source(65536); g.sample_slot=0; g.set_launch_mode(Mode::Gate); token=g.pad_begin(); pcm(g,100);
        auto& bank=SampleBank::instance();
        if(change==0)bank.slot(0).logical_frames=32768;
        if(change==1) { Sample fresh=bank.slot(0); bank.swap_sample_unchecked(0,fresh,trackr::synth::SampleOrigin::Runtime); }
        if(change==2) { Sample empty; bank.swap_sample_unchecked(0,empty,trackr::synth::SampleOrigin::Runtime); bank.swap_sample_unchecked(0,empty,trackr::synth::SampleOrigin::Runtime); }
        if(change==3) { bank.slot(1)=bank.slot(0); g.sample_slot=1; }
        if(change==4)bank.slot(0).channels=2;
        if(change==5)g.sample_slot=-1;
        G::VisualGrain v[8]; CHECK(capture(g,v)==0); CHECK(!g.gate_open()); pcm(g,1); CHECK(!g.gate_open() && g.visual_spawned()==0);
        if(change<5) { next=g.pad_begin(); g.pad_end(token); CHECK(g.gate_open()); pcm(g,1); CHECK(g.visual_spawned()==1); g.pad_end(next); }
    }
    source(65536); G burst; config(burst,Mode::Burst); burst.trigger(); pcm(burst,1); CHECK(burst.burst_remaining()==3);
    SampleBank::instance().slot(0).logical_frames=32768; pcm(burst,1000); CHECK(burst.burst_remaining()==0 && burst.visual_spawned()==0);
    std::puts("ok reset/source slot/generation/backing/logical frames/channels invalidate tokens, heads and queued bursts");
}
void render(Engine& e,int frames,int partition=127) {
    q15 b[514]; while(frames) { int n=std::min(frames,partition); CHECK(n<=257); const auto allocations=allocation_calls; e.render(b,n); CHECK(allocation_calls==allocations); frames-=n; }
}
G* install(Engine& e,Mode mode=Mode::Gate) { auto* g=new G; config(*g,mode); delete e.replace_unit(0,0,g); return g; }
void launch_score_now(Engine& e) {
    // transport_play alone deliberately starts at the next grid. An explicit
    // public pattern launch is the normal immediate accepted-event path.
    const int target=e.active_pattern==0?1:0; e.pattern_banks[target]=e.pattern;
    e.request_pattern(target,false);
}
void score(Engine& e,int retrigs=1,bool clock=false) {
    e.set_bpm(120); e.pattern.steps[0][0].flags=SeqStep::Trig; e.pattern.steps[0][0].set_retrig_count(retrigs);
    if(clock) { e.sigils[0].drawn=true; for(auto& v:e.sigils[0].table)v=0; e.set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,37); }
    e.set_sequencer_enabled(true); e.transport_play(); launch_score_now(e);
}
void real_engine() {
    source(65536);
    // Gate lasts the full actual accepted step/span, not a fixed millisecond timer.
    for(bool phase:{false,true}) for(int retrigs:{1,4}) {
        std::printf("case real engine phase=%d retrigs=%d\n",phase,retrigs);
        auto e=std::make_unique<Engine>(); auto* g=install(*e); score(*e,retrigs,phase); render(*e,1,1);
        CHECK(g->gate_open()); CHECK(g->gate_phase_==phase);
        const uint64_t span=phase?e->clock_event_end_[0]-e->clock_event_start_[0]:e->sequence_event_span_[0]; CHECK(span>1);
        CHECK(g->gate_deadline_==span);
        uint64_t latest=span; int renewals=0; const int end=phase?7100:7001;
        for(int frame=1;frame<end;++frame) {
            const uint64_t before_deadline=g->gate_deadline_; render(*e,1,1);
            if(g->gate_deadline_!=UINT64_MAX && g->gate_deadline_!=before_deadline) { CHECK(g->gate_deadline_>latest); latest=g->gate_deadline_; ++renewals; }
            const uint64_t now=phase?e->clock_phase_:e->transport_frame;
            if(now<latest)CHECK(g->gate_open());
            if(now>latest+(phase?e->clock_increment_:0))CHECK(!g->gate_open());
        }
        CHECK(renewals==retrigs-1); CHECK(!g->gate_open());
        const auto n=g->visual_spawned(); render(*e,1000); CHECK(g->visual_spawned()==n);
    }
    // Swing and a shifted following event produce a non-nominal accepted span.
    for(bool phase:{false,true}) {
        auto e=std::make_unique<Engine>(); auto* g=install(*e); e->set_swing(65); e->set_step_microtiming(0,1,23); score(*e,1,phase); render(*e,1,1);
        const uint64_t span=phase?e->clock_event_end_[0]-e->clock_event_start_[0]:e->sequence_event_span_[0];
        CHECK(span>(phase?(1ull<<30):4000ull) && g->gate_deadline_==span);
        const uint64_t end=phase?e->clock_frame_for_phase(g->gate_deadline_):g->gate_deadline_;
        render(*e,int(end-e->transport_frame)-1); CHECK(g->gate_open()); render(*e,2,1); CHECK(!g->gate_open());
        const auto n=g->visual_spawned(); render(*e,1000); CHECK(g->visual_spawned()==n);
    }
    // Tempo changes preserve remaining musical time in the frame scheduler.
    {
        auto e=std::make_unique<Engine>(); auto* g=install(*e); score(*e); render(*e,1000);
        CHECK(g->gate_deadline_==4000); e->set_bpm(240);
        CHECK(g->gate_deadline_==2500); render(*e,1500); render(*e,1,1); CHECK(!g->gate_open());
    }
    // Clock activation converts a live permit to musical phase. Removing the
    // route keeps the existing sticky phase scheduler until transport reset.
    {
        auto e=std::make_unique<Engine>(); auto* g=install(*e); score(*e); render(*e,777);
        e->sigils[0].drawn=true; for(auto& v:e->sigils[0].table)v=0;
        e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,37); CHECK(g->gate_phase_ && g->gate_open());
        const auto deadline=g->gate_deadline_; render(*e,512); e->set_clock_mod(ClockTarget::Tempo,ModSource::None,37);
        CHECK(g->gate_phase_ && g->gate_open() && g->gate_deadline_==deadline);
        const auto end=e->clock_frame_for_phase(deadline); CHECK(end>e->transport_frame);
        render(*e,int(end-e->transport_frame)+2); CHECK(!g->gate_open());
        e->transport_reset(); CHECK(!g->gate_open() && !e->clock_runtime_);
    }
    // Own pad and sequencer are independent permits. Neither lift, manual onset,
    // nor stopping one owner may close a different active owner.
    {
        auto e=std::make_unique<Engine>(); auto* g=install(*e); score(*e); render(*e,100);
        const auto deadline=g->gate_deadline_; auto token=g->pad_begin(); render(*e,1); g->pad_end(token); CHECK(g->gate_open() && g->gate_deadline_==deadline);
        e->trigger_chain(0); render(*e,1); CHECK(g->gate_open() && g->gate_deadline_==deadline);
        token=g->pad_begin(); e->transport_stop(); CHECK(g->gate_open()); const auto n=g->visual_spawned(); render(*e,1000); CHECK(g->visual_spawned()>n);
        g->pad_end(token); CHECK(!g->gate_open()); const auto stopped=g->visual_spawned(); render(*e,1000); CHECK(g->visual_spawned()==stopped);
        e->trigger_chain(0); render(*e,3000); CHECK(g->visual_spawned()==stopped+1 && !g->gate_open());
        token=g->pad_begin(); render(*e,10); set(*g,G::P_SCAN,32767); g->control_tick(); const auto scan=g->scan_position();
        g->bypass=true; render(*e,1000); CHECK(!g->gate_open() && g->scan_position()==scan); g->bypass=false; render(*e,1000); CHECK(!g->gate_open());
        token=g->pad_begin(); delete e->replace_unit(0,1,new FilterUnit); CHECK(!g->gate_open());
        token=g->pad_begin(); CHECK(e->move_unit_before(0,0,UNITS_PER_CHAIN)>=0); CHECK(!g->gate_open());
        (void)token;
    }
    // Sequencer off revokes only its permit, leaving an independently held pad.
    {
        auto e=std::make_unique<Engine>(); auto* g=install(*e); score(*e); render(*e,1,1); const auto token=g->pad_begin();
        e->set_sequencer_enabled(false); render(*e,1,1); CHECK(g->gate_open() && g->gate_deadline_==UINT64_MAX);
        g->pad_end(token); const auto n=g->visual_spawned(); render(*e,1000); CHECK(!g->gate_open() && g->visual_spawned()==n);
    }
    // Refused conditions/lock-only events cannot create sequence permits.
    for(bool lock_only:{false,true}) {
        auto e=std::make_unique<Engine>(); auto* g=install(*e); score(*e);
        e->transport_stop(); e->transport_reset();
        e->pattern.steps[0][0].flags=lock_only?0:SeqStep::Trig;
        if(!lock_only)e->pattern.steps[0][0].set_condition(SeqStep::Condition::Fill);
        CHECK(e->set_step_lock(0,0,0,G::P_GAIN,20000)); e->transport_play(); launch_score_now(*e); render(*e,1000);
        CHECK(!g->gate_open() && g->visual_spawned()==0);
    }
    // Live-roll/manual impulses do not acquire indefinite gate permission.
    {
        auto e=std::make_unique<Engine>(); auto* g=install(*e); e->set_live_retrigger(0,3,true); render(*e,10001);
        CHECK(!g->gate_open() && g->visual_spawned()>1); e->set_live_retrigger(0,3,false);
        // Legacy non-phase rolls dispatch at the end of a render fragment. A
        // final accepted onset may await its first sample when the roll stops.
        const auto before=g->visual_spawned(); const auto accepted=g->pending_launches_; CHECK(accepted<=1);
        render(*e,1,1); CHECK(g->visual_spawned()==before+accepted && !g->gate_open());
        const auto n=g->visual_spawned(); render(*e,5000); CHECK(g->visual_spawned()==n);
    }
    for(bool manual:{false,true}) {
        auto e=std::make_unique<Engine>(); auto* g=install(*e,Mode::Burst); set(*g,G::P_DENS,0); g->control_tick(); score(*e); render(*e,1);
        CHECK(g->burst_remaining()==3); render(*e,4001); CHECK(g->burst_remaining()==3); // the full-step deadline does not truncate a sequence burst
        if(manual)g->pad_begin();
        e->transport_stop();
        CHECK(g->burst_remaining()==(manual?4:0)); render(*e,32001); CHECK(g->burst_remaining()==0);
    }
    std::puts("ok real engine accepted full-step/span gates + retrig renewal, bpm rescale, phase conversion, stop/bypass/structural/manual/pad independence, conditions/roll/sequence bursts");
}
uint32_t r32(const Bytes& b,size_t p) { CHECK(p+4<=b.size()); return b[p]|uint32_t(b[p+1])<<8|uint32_t(b[p+2])<<16|uint32_t(b[p+3])<<24; }
void w32(Bytes& b,size_t p,uint32_t n) { CHECK(p+4<=b.size()); for(int i=0;i<4;++i)b[p+i]=(n>>(i*8))&255u; }
size_t chunk(const Bytes& b,const char* id,bool required=true) {
    for(size_t p=5;p+8<=b.size();) { const auto n=r32(b,p+4); CHECK(n<=b.size()-p-8); if(!std::memcmp(b.data()+p,id,4))return p; p+=8+n; }
    CHECK(!required); return b.size();
}
Bytes read_file(const std::string& path) { std::ifstream f(path,std::ios::binary); CHECK(f.good()); return {std::istreambuf_iterator<char>(f),{}}; }
void put_file(const std::string& path,const Bytes& b) { std::ofstream f(path,std::ios::binary); f.write((const char*)b.data(),b.size()); CHECK(f.good()); }
void persistence() {
    source(65536); const auto path=workdir+"/patch-fixture.snc", sentinel_path=workdir+"/patch-sentinel.snc";
    for(Mode mode:{Mode::Flow,Mode::Gate,Mode::Burst}) for(Window window:{Window::Linear,Window::Hann}) for(int count:{1,4,16}) {
        auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>(); auto* g=install(*a,mode); g->set_window(window); g->set_burst_count(count);
        g->clock_sync=true; g->sync_division=3; set(*g,G::P_SCAN,-12345); const auto token=g->pad_begin(); pcm(*g,37);
        CHECK(a->set_step_lock(0,0,0,G::P_SCAN,11111)); a->capture_scene(0,0);
        CHECK(patchio::save(*a,path.c_str())); const auto bytes=read_file(path);
        const bool extra=mode!=Mode::Flow||window!=Window::Linear||count!=4;
        const auto at=chunk(bytes,"GRNP",false); CHECK((at<bytes.size())==extra);
        if(extra) { CHECK(r32(bytes,at+4)==7 && bytes[at+8]==1 && bytes[at+9]==1); CHECK(bytes[at+10]==0 && bytes[at+11]==0 && bytes[at+12]==unsigned(mode) && bytes[at+13]==unsigned(window) && bytes[at+14]==count); }
        CHECK(patchio::load(*b,path.c_str())); auto* loaded=static_cast<G*>(b->chains[0].units[0]);
        CHECK(loaded && loaded->clean && loaded->launch_mode==mode && loaded->window==window && loaded->burst_count==count);
        CHECK(loaded->clock_sync && loaded->sync_division==3 && loaded->params[G::P_SCAN].base==-12345);
        CHECK(!loaded->gate_open() && !loaded->burst_remaining() && loaded->visual_spawned()==0);
        CHECK(b->pattern.steps[0][0].lock_count==1 && b->scenes[0][0].valid);
        std::unique_ptr<Unit> copy(clone_unit(g)); auto* cloned=static_cast<G*>(copy.get()); G::VisualGrain v[8];
        CHECK(cloned->launch_mode==mode && cloned->window==window && cloned->burst_count==count && cloned->clean);
        CHECK(!cloned->gate_open() && !cloned->burst_remaining() && capture(*cloned,v)==0 && cloned->visual_spawned()==0 && cloned->scan_position()==g->params[G::P_POS].effective);
        g->pad_end(token);
        if(extra) {
            Bytes no=bytes; no.erase(no.begin()+at,no.begin()+at+15); put_file(path,no); CHECK(patchio::load(*b,path.c_str())); loaded=static_cast<G*>(b->chains[0].units[0]);
            CHECK(loaded->clean && loaded->launch_mode==Mode::Flow && loaded->window==Window::Linear && loaded->burst_count==4);
            Bytes first=bytes,record(bytes.begin()+at,bytes.begin()+at+15); first.erase(first.begin()+at,first.begin()+at+15); first.insert(first.begin()+5,record.begin(),record.end());
            put_file(path,first); CHECK(patchio::load(*b,path.c_str())); loaded=static_cast<G*>(b->chains[0].units[0]); CHECK(loaded->launch_mode==mode && loaded->window==window && loaded->burst_count==count);
        }
    }
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>(); auto* g=install(*a,Mode::Burst); g->set_window(Window::Hann); g->set_burst_count(16);
    auto* sparse=new G; config(*sparse,Mode::Gate,Window::Hann); delete a->replace_unit(3,5,sparse);
    CHECK(patchio::save(*a,path.c_str())); const Bytes good=read_file(path); const size_t at=chunk(good,"GRNP"); CHECK(r32(good,at+4)==12);
    CHECK(patchio::load(*b,path.c_str())); auto* live=static_cast<G*>(b->chains[0].units[0]); live->set_launch_mode(Mode::Gate); const auto token=live->pad_begin(); pcm(*live,13);
    b->set_bpm(137); b->transport_frame=987; const auto transport=b->transport_frame; const auto spawned=live->visual_spawned(); const auto scan=live->scan_position();
    CHECK(patchio::save(*b,sentinel_path.c_str())); const auto sentinel_bytes=read_file(sentinel_path);
    unsigned rejected=0;
    const auto reject=[&](const Bytes& bad) {
        put_file(path,bad); CHECK(!patchio::load(*b,path.c_str())); CHECK(b->chains[0].units[0]==live && b->transport_frame==transport && b->bpm==137);
        CHECK(live->gate_open() && live->pad_token_==token && live->visual_spawned()==spawned && live->scan_position()==scan);
        CHECK(patchio::save(*b,sentinel_path.c_str())); CHECK(read_file(sentinel_path)==sentinel_bytes); ++rejected;
    };
    for(auto field:std::vector<std::pair<int,int>>{{8,0},{8,2},{9,0},{9,25},{10,4},{11,6},{12,3},{12,255},{13,2},{13,255},{14,0},{14,17},{14,255}}) { Bytes bad=good; bad[at+field.first]=field.second; reject(bad); }
    { Bytes bad=good; bad[at+15]=bad[at+10]; bad[at+16]=bad[at+11]; reject(bad); } // duplicate chain/slot
    { Bytes bad=good; bad[at+11]=1; reject(bad); } // empty slot
    { Bytes bad=good; bad[chunk(bad,"UNIT")+8]&=7; reject(bad); } // record on legacy GRAN
    { Bytes bad=good; bad.insert(bad.end(),good.begin()+at,good.begin()+at+20); reject(bad); } // duplicate GRNP
    for(int n=0;n<12;++n) { Bytes bad(good.begin(),good.begin()+at+8+n); reject(bad); }
    for(uint32_t size:{1u,11u,13u,0xffffffffu}) { Bytes bad=good; w32(bad,at+4,size); reject(bad); }
    { Bytes bad=good; bad.insert(bad.begin()+at+20,0); w32(bad,at+4,13); reject(bad); }
    // Legacy stores neither perform opt-in nor runtime; no clean auto-upgrade.
    g->clean=false; g->launch_mode=Mode::Burst; g->window=Window::Hann; g->burst_count=16; sparse->clean=false;
    CHECK(patchio::save(*a,path.c_str())); const auto legacy=read_file(path); CHECK(chunk(legacy,"GRNP",false)==legacy.size());
    CHECK(patchio::load(*b,path.c_str())); auto* old=static_cast<G*>(b->chains[0].units[0]); CHECK(!old->clean && old->launch_mode==Mode::Flow && old->window==Window::Linear);
    std::unique_ptr<Unit> copy(clone_unit(old)); CHECK(!static_cast<G*>(copy.get())->clean);
    std::remove(path.c_str()); std::remove(sentinel_path.c_str());
    std::printf("ok optional GRNP v1 18 roundtrips, absence/order/clean-only, config-only clone, sparse records; %u malformed transactional rejections\n",rejected);
}
void modern_tests() { defaults_legacy(); gate_ownership(); burst_and_flow(); partitions(); windows_and_tails(); sources_and_reset(); real_engine(); persistence(); }
#endif
} // namespace
int main(int argc,char** argv) {
    CHECK(argc==2); workdir=argv[1]; frozen_compatibility(argv[1]);
#ifndef GRAN_PERFORM_BASELINE_ONLY
    modern_tests();
#endif
    std::printf("gran perform core: passed (%llu checks); GrainUnit=%zu bytes\n",checks,sizeof(G));
}
