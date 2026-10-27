// Focused portable ORDER contract: real render/clock/patchio, no controller model.
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
#include <unistd.h>
#include "engine/engine.h"
#include "engine/patch_io.h"
using namespace seance;
static size_t allocations = 0;
#if defined(__SANITIZE_ADDRESS__)
extern "C" void __sanitizer_malloc_hook(const volatile void*, size_t) { ++allocations; }
#else
void* operator new(size_t n) { ++allocations; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
#endif
static unsigned long long checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
namespace order_cases {
struct Hit { uint64_t frame; q15 value; bool operator==(const Hit& b) const { return frame == b.frame && value == b.value; } };
struct Probe : Unit {
    Engine& e; std::array<Hit, 8192> hits{}; size_t count = 0;
    explicit Probe(Engine& engine) : e(engine) { kind = UnitKind::Osc; add_param("ONE", 1000); }
    void trigger() override { CHECK(count < hits.size()); hits[count++] = {e.transport_frame, params[0].effective}; }
    void process(q15* output, int frames) override { std::fill(output, output + frames, (q15)(params[0].effective / 32)); }
};
std::unique_ptr<Engine> engine(bool phase, bool probe = false) {
    auto e = std::make_unique<Engine>(); e->set_bpm(240); e->beats_per_bar = 2;
    if (probe) {
        e->chains[0].units[0] = new Probe(*e);
        for (int bank = 0; bank < NUM_PATTERNS; ++bank) {
            auto& p = e->sequence_pattern(bank); p.length[0] = (uint8_t)(3 + bank);
            for (int s = 0; s < SEQ_STEPS; ++s) {
                auto& step = p.steps[0][s]; step.flags = SeqStep::Trig;
                step.set_condition((SeqStep::Condition)(s % 8)); step.set_retrig_count(1 + s % 4);
                step.lock_count = 1; step.locks[0] = {0, 0, (q15)(1000 + bank * 2000 + s)};
                p.microtiming[0][s] = (int8_t)(s % 3 == 0 ? -23 : s % 3 == 1 ? 23 : 0);
            }
        }
    }
    if (phase) {
        e->sigils[0].drawn = true; e->sigils[0].depth = 32767;
        std::fill(std::begin(e->sigils[0].table), std::end(e->sigils[0].table), 0);
        CHECK(e->set_clock_mod(ClockTarget::Tempo, ModSource::SigilA, 7));
    }
    e->set_sequencer_enabled(true); return e;
}
void route(Engine& e, std::initializer_list<PatternChainStep> entries) {
    e.clear_pattern_chain(); int i = 0;
    for (auto step : entries) CHECK(e.set_pattern_chain_step(i++, step.pattern, step.bars));
    CHECK(e.set_pattern_chain_length(i));
}
void render(Engine& e, uint64_t frames, int partition = 257) {
    q15 audio[514];
    while (frames) {
        const int n = (int)std::min<uint64_t>(frames, partition); const auto before = allocations;
        e.render(audio, n); CHECK(allocations == before); frames -= n;
    }
}
EngineUiSnapshot snap(const Engine& e) { EngineUiSnapshot s; e.capture_ui_snapshot(s); return s; }
// Independent exact constant-tempo oracle, including real Q32 rounding.
uint64_t bar_frame(const Engine& e, bool phase, unsigned bar) {
    if (!phase) return uint64_t(bar) * e.beats_per_bar * e.frames_per_beat();
    const uint64_t increment = ((uint64_t(e.bpm) * 65536 * 65536) + ENGINE_SR * 30u) / (ENGINE_SR * 60u);
    const uint64_t target = uint64_t(bar) * e.beats_per_bar * (1ull << 32);
    return (target + increment - 1) / increment;
}
void until(Engine& e, uint64_t frame) { CHECK(frame >= e.transport_frame); render(e, frame - e.transport_frame); }
void at_bar(Engine& e, bool phase, unsigned bar) { until(e, bar_frame(e, phase, bar)); CHECK(e.beat_count == uint64_t(bar) * e.beats_per_bar); }
void state(const Engine& e, int position, int bank, int remaining, int total) {
    const auto s = snap(e);
    CHECK(e.pattern_chain_position == position && s.pattern_chain_position == position);
    CHECK(e.active_pattern == bank && s.pattern_chain_current_pattern == bank);
    CHECK(e.pattern_chain_current_pattern == bank);
    CHECK(e.pattern_chain_bars_remaining == remaining && s.pattern_chain_bars_remaining == remaining);
    CHECK(e.pattern_chain_current_bars == total && s.pattern_chain_current_bars == total);
    if (!s.pattern_chain_next_pending) CHECK(s.pattern_chain_next_position==0xff && s.pattern_chain_next_pattern==0xff && s.pattern_chain_next_bars==0);
    if (!s.pattern_chain_start_pending) CHECK(s.pattern_chain_armed_pattern==0xff && s.pattern_chain_armed_bars==0);
}
void duration() {
    for (bool phase : {false, true}) {
        auto e = engine(phase); route(*e, {{0,1},{1,2},{2,1}});
        CHECK(e->start_pattern_chain()); CHECK(!e->transport_playing); e->transport_play();
        state(*e,0,0,1,1); at_bar(*e,phase,1); state(*e,1,1,2,2);
        at_bar(*e,phase,2); state(*e,1,1,1,2); at_bar(*e,phase,3); state(*e,2,2,1,1);
        at_bar(*e,phase,4); state(*e,0,0,1,1); CHECK(e->pattern_chain_playing);
        e->pattern_chain_loop=false; at_bar(*e,phase,8);
        CHECK(!e->pattern_chain_playing && e->transport_playing && e->active_pattern==2);
    }
    std::puts("ok duration: both schedulers, full bars, loop and nonloop end without stopping transport");
}
void identity_edits() {
    for (bool phase : {false,true}) {
        auto e=engine(phase); route(*e,{{0,1},{0,3},{1,2},{2,4}});
        CHECK(e->resume_pattern_chain(1)); e->transport_play(); at_bar(*e,phase,1); state(*e,1,0,2,3);
        const auto frame=e->transport_frame, tick=e->sequence_tick;
        CHECK(e->move_pattern_chain_step(1,3)); state(*e,3,0,2,3);
        CHECK(e->insert_pattern_chain_step(0,5,4)); state(*e,4,0,2,3);
        CHECK(e->remove_pattern_chain_step(1)); state(*e,3,0,2,3);
        CHECK(!e->remove_pattern_chain_step(3)); CHECK(!e->set_pattern_chain_length(3));
        CHECK(e->set_pattern_chain_step(3,7,16)); CHECK(e->set_pattern_chain_step(0,6,2));
        CHECK(e->pattern_chain_playing && e->transport_frame==frame && e->sequence_tick==tick);
        auto s=snap(*e); CHECK(s.pattern_chain_steps[3].pattern==7 && s.pattern_chain_steps[3].bars==16);
        state(*e,3,0,2,3); at_bar(*e,phase,2); state(*e,3,0,1,3);
        until(*e,bar_frame(*e,phase,3)-1); state(*e,3,0,1,3); render(*e,1); state(*e,0,6,2,2);
        CHECK(e->remove_pattern_chain_step(3)); CHECK(e->set_pattern_chain_length(2));
        CHECK(e->pattern_chain_playing); state(*e,0,6,2,2);
    }
    std::puts("ok live edits: repeated-bank exact identity, reorder/insert/remove/resize, current tuple latched and future route live");
}
void exhaustive_identity() {
    // Every move direction/range; all banks deliberately identical. Distinct
    // durations identify the exact current and explicitly queued target cards.
    for (int from=0;from<16;++from) for(int to=0;to<16;++to) {
        auto e=engine(false);
        for(int i=0;i<16;++i) CHECK(e->set_pattern_chain_step(i,0,i+1));
        CHECK(e->set_pattern_chain_length(16)); CHECK(e->resume_pattern_chain(7)); CHECK(e->next_pattern_chain());
        std::array<int,16> ids; for(int i=0;i<16;++i) ids[i]=i;
        const int id=ids[from];
        if(from<to) { for(int i=from;i<to;++i) ids[i]=ids[i+1]; }
        else { for(int i=from;i>to;--i) ids[i]=ids[i-1]; }
        ids[to]=id;
        const int current=(int)(std::find(ids.begin(),ids.end(),7)-ids.begin());
        const int next=(int)(std::find(ids.begin(),ids.end(),8)-ids.begin());
        const auto before=allocations;
        CHECK(e->move_pattern_chain_step(from,to)); state(*e,current,0,8,8);
        CHECK(!e->remove_pattern_chain_step(current)); CHECK(!e->remove_pattern_chain_step(next));
        CHECK(e->set_pattern_chain_step(next,7,16)); CHECK(e->next_pattern_chain());
        const auto queued=snap(*e);
        CHECK(queued.pattern_chain_next_pending && queued.pattern_chain_next_position==next);
        CHECK(queued.pattern_chain_next_pattern==0 && queued.pattern_chain_next_bars==9);
        CHECK(allocations==before); e->transport_play(); at_bar(*e,false,1); state(*e,next,0,9,9);
    }
    std::puts("ok exhaustive identity: all256 move permutations retain exact current/queued cards despite16 identical-bank cards");
}
void hold_and_next() {
    for (bool phase : {false,true}) {
        auto e=engine(phase); route(*e,{{0,3},{1,2},{1,4},{2,1}});
        CHECK(e->start_pattern_chain()); e->transport_play(); CHECK(e->set_pattern_chain_hold(true));
        at_bar(*e,phase,3); state(*e,0,0,3,3); CHECK(snap(*e).pattern_chain_held);
        CHECK(e->set_pattern_chain_hold(false)); at_bar(*e,phase,4); state(*e,0,0,2,3);
        CHECK(e->set_pattern_chain_hold(true)); render(*e,123); CHECK(e->next_pattern_chain());
        CHECK(e->set_pattern_chain_step(1,6,16)); CHECK(e->move_pattern_chain_step(1,3));
        CHECK(e->insert_pattern_chain_step(0,5,1)); CHECK(e->remove_pattern_chain_step(2));
        state(*e,1,0,2,3); CHECK(!e->remove_pattern_chain_step(3)); CHECK(!e->set_pattern_chain_length(3));
        CHECK(e->next_pattern_chain()); CHECK(snap(*e).pattern_chain_next_pending);
        CHECK(snap(*e).pattern_chain_next_position==3 && snap(*e).pattern_chain_next_pattern==1 && snap(*e).pattern_chain_next_bars==2);
        until(*e,bar_frame(*e,phase,5)-1); state(*e,1,0,2,3);
        render(*e,1); state(*e,3,1,2,2); CHECK(!e->pattern_chain_held && !e->pattern_chain_next_pending);
        e->pattern_chain_loop=false; CHECK(e->set_pattern_chain_hold(true)); CHECK(e->next_pattern_chain());
        // An accepted end is also latched, not retargeted by inserting a new tail.
        CHECK(e->insert_pattern_chain_step(4,7,1));
        CHECK(snap(*e).pattern_chain_next_pending && snap(*e).pattern_chain_next_position==0xff && snap(*e).pattern_chain_next_pattern==0xff && snap(*e).pattern_chain_next_bars==0);
        at_bar(*e,phase,6);
        CHECK(!e->pattern_chain_playing && !e->pattern_chain_held && !e->pattern_chain_next_pending);
        CHECK(e->transport_playing && e->active_pattern==1);
        CHECK(!e->next_pattern_chain() && !e->set_pattern_chain_hold(true));
    }
    std::puts("ok hold/next: frozen countdown, release, held bypass, queued identity/content/end latches and coalescing");
}
void compare(Engine& a, Engine& b, int frames) {
    q15 x[514], y[514];
    while (frames) {
        int n=std::min(frames,257); const auto before=allocations; a.render(x,n); b.render(y,n);
        CHECK(allocations==before); CHECK(std::equal(x,x+n*2,y));
        CHECK(a.transport_frame==b.transport_frame && a.sequence_tick==b.sequence_tick); frames-=n;
    }
    auto& xh=*static_cast<Probe*>(a.chains[0].units[0]); auto& yh=*static_cast<Probe*>(b.chains[0].units[0]);
    CHECK(xh.count==yh.count && std::equal(xh.hits.begin(),xh.hits.begin()+xh.count,yh.hits.begin()));
}
void takeover_audio() {
    for (bool phase : {false,true}) {
        auto a=engine(phase,true), b=engine(phase,true);
        for (auto* e : {a.get(),b.get()}) { route(*e,{{0,4},{1,2}}); CHECK(e->start_pattern_chain()); e->transport_play(); }
        compare(*a,*b,1234); CHECK(a->set_pattern_chain_hold(true)); CHECK(a->next_pattern_chain());
        const auto frame=a->transport_frame, tick=a->sequence_tick, beat=a->beat_count; const auto inbeat=a->frame_in_beat;
        CHECK(a->take_over_pattern_chain()); b->stop_pattern_chain();
        state(*a,0,0,4,4); CHECK(a->pattern_chain_paused && !a->pattern_chain_playing);
        CHECK(!a->pattern_chain_next_pending && !snap(*a).pattern_chain_start_pending);
        CHECK(a->transport_frame==frame && a->sequence_tick==tick && a->beat_count==beat && a->frame_in_beat==inbeat);
        CHECK(!a->take_over_pattern_chain()); CHECK(a->pattern_chain_paused);
        compare(*a,*b,35000); state(*a,0,0,4,4);
        for (auto* e : {a.get(),b.get()}) CHECK(e->set_pattern_transition(0,5,16384));
        // Arbitrary ORDER edits cannot touch live manual transition endpoints or sound.
        CHECK(a->move_pattern_chain_step(0,1)); CHECK(a->set_pattern_chain_step(1,7,16));
        CHECK(a->insert_pattern_chain_step(0,3,2)); CHECK(a->remove_pattern_chain_step(1));
        CHECK(a->take_over_pattern_chain()==false); CHECK(!a->resume_pattern_chain(1));
        CHECK(a->pattern_transition().source==0 && a->pattern_transition().destination==5 && a->pattern_transition().position==16384);
        compare(*a,*b,35000);
    }
    std::puts("ok takeover: byte-exact PCM/events versus progression-only stop; manual blend endpoints/queued tuples survive ORDER edits");
}
void resume_and_manual() {
    for (bool phase : {false,true}) {
        auto e=engine(phase); route(*e,{{0,2},{1,2},{0,4}}); CHECK(e->start_pattern_chain()); e->transport_play(); render(*e,1000);
        CHECK(!e->set_pattern_transition(0,5,16000)); CHECK(e->take_over_pattern_chain());
        CHECK(e->set_pattern_transition(0,5,16000)); const auto before=snap(*e);
        CHECK(!e->resume_pattern_chain(2)); CHECK(e->pattern_chain_position==before.pattern_chain_position);
        CHECK(e->set_pattern_transition(0,5,32767)); CHECK(!e->resume_pattern_chain(2)); CHECK(e->finish_pattern_transition());
        CHECK(e->active_pattern==5); CHECK(!e->resume_pattern_chain(-1) && !e->resume_pattern_chain(3));
        CHECK(e->resume_pattern_chain(2)); state(*e,2,5,4,4);
        CHECK(snap(*e).pattern_chain_start_pending && !e->pattern_chain_paused);
        auto armed=snap(*e);
        CHECK(armed.pattern_chain_armed_pattern==0 && armed.pattern_chain_armed_bars==4 && armed.pattern_chain_current_pattern==5);
        CHECK(!e->set_pattern_transition(5,3,16000)); CHECK(!e->next_pattern_chain());
        CHECK(e->set_pattern_chain_step(2,6,16)); CHECK(e->move_pattern_chain_step(2,0));
        e->capture_ui_snapshot(armed);
        CHECK(armed.pattern_chain_position==0 && armed.pattern_chain_steps[0].pattern==6 && armed.pattern_chain_steps[0].bars==16);
        CHECK(armed.pattern_chain_armed_pattern==0 && armed.pattern_chain_armed_bars==4 && armed.pattern_chain_current_pattern==5);
        CHECK(!e->remove_pattern_chain_step(0)); until(*e,bar_frame(*e,phase,1)-1); state(*e,0,5,4,4);
        CHECK(snap(*e).pattern_chain_armed_pattern==0 && snap(*e).pattern_chain_armed_bars==4);
        render(*e,1); state(*e,0,0,4,4); CHECK(!snap(*e).pattern_chain_start_pending);
        e->capture_ui_snapshot(armed);
        CHECK(armed.pattern_chain_armed_pattern==0xff && armed.pattern_chain_armed_bars==0 && armed.pattern_chain_current_pattern==0);
        render(*e,13); e->request_pattern(3,true); state(*e,0,0,4,4);
        CHECK(e->pattern_chain_paused && !e->pattern_chain_playing && e->pending_pattern==3);
        CHECK(!e->resume_pattern_chain(0)); at_bar(*e,phase,2); state(*e,0,3,4,4);
        CHECK(e->resume_pattern_chain(0)); CHECK(snap(*e).pattern_chain_start_pending); // exact boundary still next, never early
        until(*e,bar_frame(*e,phase,3)-1); state(*e,0,3,16,16); render(*e,1); state(*e,0,6,16,16);
        e->request_pattern(6,false); CHECK(e->pattern_chain_paused && e->pending_pattern==255); // same bank is explicit too
        e->transport_stop(); CHECK(!e->pattern_chain_paused && !e->pattern_chain_held);
        const auto frame=e->transport_frame; CHECK(e->resume_pattern_chain(1)); state(*e,1,0,2,2);
        CHECK(!e->transport_playing && e->transport_frame==frame && !snap(*e).pattern_chain_start_pending);
        render(*e,123); CHECK(e->transport_frame==frame); e->stop_pattern_chain(); CHECK(e->active_pattern==0);
    }
    std::puts("ok resume: explicit keep required, exact repeated card, full bars, strict next-bar arm/content latch, pending-manual refusal, stopped prepare");
}
void cancellation() {
    for (bool phase : {false,true}) for (int action=0;action<5;++action) {
        auto e=engine(phase), other=engine(phase); route(*e,{{0,4},{1,2}}); CHECK(e->start_pattern_chain());
        e->transport_play(); render(*e,123); CHECK(e->set_pattern_chain_hold(true)); CHECK(e->next_pattern_chain());
        CHECK(e->take_over_pattern_chain()); CHECK(e->resume_pattern_chain(1)); CHECK(e->set_pattern_chain_hold(true));
        if (action==0) e->transport_stop();
        if (action==1) e->transport_reset();
        if (action==2) e->stop_pattern_chain();
        if (action==3) e->swap_patch_state(*other);
        if (action==4) e->clear_pattern_chain();
        auto s=snap(*e); CHECK(!s.pattern_chain_playing && !s.pattern_chain_paused && !s.pattern_chain_held);
        CHECK(!s.pattern_chain_start_pending && !s.pattern_chain_next_pending && s.pattern_chain_bars_remaining==0);
        CHECK(s.pattern_chain_current_pattern==e->active_pattern);
        if (action==3) { auto t=snap(*other); CHECK(!t.pattern_chain_playing && !t.pattern_chain_paused && !t.pattern_chain_held && !t.pattern_chain_start_pending && !t.pattern_chain_next_pending); }
    }
    // Takeover cancels an armed start without switching bank or losing its cursor.
    for (bool phase : {false,true}) {
        auto e=engine(phase); route(*e,{{4,3},{6,2}}); e->transport_play(); render(*e,123);
        CHECK(e->resume_pattern_chain(1)); auto armed=snap(*e);
        CHECK(armed.pattern_chain_armed_pattern==6 && armed.pattern_chain_armed_bars==2 && armed.pattern_chain_current_pattern==0);
        CHECK(e->set_pattern_chain_step(1,5,16)); CHECK(e->move_pattern_chain_step(1,0));
        e->capture_ui_snapshot(armed);
        CHECK(armed.pattern_chain_armed_pattern==6 && armed.pattern_chain_armed_bars==2 && armed.pattern_chain_position==0);
        CHECK(e->take_over_pattern_chain()); state(*e,0,0,2,2);
        e->capture_ui_snapshot(armed);
        CHECK(!armed.pattern_chain_start_pending && armed.pattern_chain_armed_pattern==0xff && armed.pattern_chain_armed_bars==0 && armed.pattern_chain_current_pattern==0);
        at_bar(*e,phase,1); state(*e,0,0,2,2); CHECK(e->pattern_chain_paused);
    }
    std::puts("ok lifecycle: stop/reset/project-swap/clear cancel hold/pause/start/next; takeover cancels arm causally");
}
std::vector<char> bytes(const std::string& p) { std::ifstream f(p,std::ios::binary); CHECK(f.good()); return {std::istreambuf_iterator<char>(f),{}}; }
void persistence(const std::string& work) {
    static_assert(sizeof(PatternChainStep)==2,"unchanged PCHN tuple");
    auto e=engine(false); for (int i=0;i<16;++i) CHECK(e->set_pattern_chain_step(i,i%8,i+1));
    CHECK(e->set_pattern_chain_length(16)); e->pattern_chain_loop=false;
    const std::string path=work+"/order.snc", second=work+"/order-loaded.snc";
    CHECK(patchio::save(*e,path.c_str())); const auto saved=bytes(path);
    bool found=false;
    for (size_t p=5;p+8<=saved.size();) {
        const auto* b=(const unsigned char*)saved.data()+p;
        uint32_t size=b[4]|uint32_t(b[5])<<8|uint32_t(b[6])<<16|uint32_t(b[7])<<24;
        CHECK(size<=saved.size()-p-8);
        if (!std::memcmp(b,"PCHN",4)) {
            CHECK(size==36 && b[8]==1 && b[9]==16 && b[10]==0 && b[11]==16);
            for (int i=0;i<16;++i) CHECK(b[12+i*2]==i%8 && b[13+i*2]==i+1);
            found=true;
        }
        p+=8+size;
    }
    CHECK(found); CHECK(e->start_pattern_chain()); CHECK(e->set_pattern_chain_hold(true)); CHECK(e->next_pattern_chain());
    CHECK(patchio::save(*e,path.c_str())); CHECK(bytes(path)==saved);
    CHECK(e->take_over_pattern_chain()); CHECK(patchio::save(*e,path.c_str())); CHECK(bytes(path)==saved);
    CHECK(patchio::load(*e,path.c_str())); auto s=snap(*e);
    CHECK(!s.pattern_chain_playing && !s.pattern_chain_paused && !s.pattern_chain_held && !s.pattern_chain_next_pending && !s.pattern_chain_start_pending);
    CHECK(s.pattern_chain_position==0 && s.pattern_chain_bars_remaining==0 && s.pattern_chain_current_bars==0);
    CHECK(s.pattern_chain_length==16 && !s.pattern_chain_loop);
    for (int i=0;i<16;++i) CHECK(s.pattern_chain_steps[i].pattern==i%8 && s.pattern_chain_steps[i].bars==i+1);
    CHECK(patchio::save(*e,second.c_str())); CHECK(bytes(second)==saved);
    CHECK(!e->insert_pattern_chain_step(0,0)); CHECK(!e->set_pattern_chain_step(-1,0,1)); CHECK(!e->set_pattern_chain_step(0,8,1));
    CHECK(e->set_pattern_chain_step(0,0,0) && e->pattern_chain[0].bars==1);
    CHECK(e->set_pattern_chain_step(0,0,100) && e->pattern_chain[0].bars==16);
    CHECK(e->set_pattern_chain_length(100) && e->pattern_chain_length==16);
    std::puts("ok persistence: unchanged SNC7/PCHN v1 36-byte payload, all16cards/8banks/1..16bars, runtime omitted, loaded flags cleared, byte-exact roundtrip");
}
} // namespace order_cases
// Compile/run the existing transition regression against the same fresh core,
// using only its minimal real-unit storage fixture, not the platform/UI harness.
uint64_t pattern_hash(const Pattern& p) {
    uint64_t h=1469598103934665603ull; auto add=[&](unsigned v){h=(h^v)*1099511628211ull;};
    for (int c=0;c<NUM_CHAINS;++c) { add(p.length[c]); add((unsigned)p.direction[c]);
        for(int s=0;s<SEQ_STEPS;++s) { auto& step=p.steps[c][s]; add(step.flags); add(step.lock_count); add((uint8_t)p.microtiming[c][s]);
            for(auto& l:step.locks) {add(l.slot);add(l.param);add((uint16_t)l.value);} } }
    return h;
}
struct Fixture {
    std::unique_ptr<Engine> engine=std::make_unique<Engine>();
    Fixture() { engine->chains[0].units[0]=new FmUnit; engine->chains[1].units[0]=new OscUnit;
        engine->chains[1].units[1]=new VcaUnit; engine->chains[2].units[0]=new PlayUnit; engine->chains[3].units[0]=new OscUnit; }
};
#include "transition_core_test.h"
int main(int argc,char** argv) {
    CHECK(argc==2); order_cases::duration(); order_cases::identity_edits(); order_cases::exhaustive_identity(); order_cases::hold_and_next();
    order_cases::takeover_audio(); order_cases::resume_and_manual(); order_cases::cancellation(); order_cases::persistence(argv[1]);
    test_transition_core();
    std::printf("perform order core: passed 8 ORDER groups + unchanged transition-core regression (%llu checks); snapshot=%zu bytes\n",checks,sizeof(EngineUiSnapshot));
}
