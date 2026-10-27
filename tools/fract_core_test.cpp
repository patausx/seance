// Narrow portable FRACT contract test; no rendering, device IO, or timing benchmark.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>
using namespace seance;
using F = FractUnit;
using Pcm = std::vector<q15>;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"fract FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
static void set(Unit& u, int p, int value) { u.params[p].base=q15(value); u.params[p].snap(); }
static Pcm process(Unit& u, int n, int value) { Pcm x(n,q15(value)); u.process(x.data(),n); return x; }
static bool all(const Pcm& x, q15 v) { return std::all_of(x.begin(),x.end(),[&](q15 a){return a==v;}); }
static void dry_hold_release() {
    F f; CHECK(f.ready()); static_assert(int(UnitKind::Bird)==22 && int(UnitKind::Fract)==23,"append-only");
    CHECK(f.num_params==8 && !std::strcmp(unit_kind_name(f.kind),"FRACT"));
    CHECK(!f.sequencer_properties().source && !f.sequencer_properties().gate_processor);
    CHECK(F::speed_q16(8192)==65536 && F::speed_q16(-8192)==-65536 && F::speed_q16(0)==16384);
    CHECK(F::size_frames(0,false,120*65536)==64 && F::size_frames(32767,false,120*65536)==32000);
    CHECK(F::size_frames(16384,true,120*65536)==4000 && F::length_frames(21065,120*65536)==16000);
    for(int value:{-32768,-19281,0,19321,32767}) CHECK(all(process(f,257,value),q15(value)));
    process(f,32000,12345); f.set_manual_grab(true); process(f,128,-19000);
    const auto frozen=f.visual(); CHECK(frozen.active && frozen.manual && frozen.capture_frames==4000);
    CHECK(all(process(f,32000*4,-19000),12345)); // frozen stable across >4 live-ring turns
    const auto later=f.visual(); CHECK(!std::memcmp(frozen.wave,later.wave,sizeof(frozen.wave)) && later.repeats>20);
    f.set_manual_grab(false); const auto tail=process(f,128,-19000);
    CHECK(tail.back()==-19000 && !f.visual().active && all(process(f,97,-23456),-23456));
    // Current live input, not repeat output, is captured on the next grab.
    process(f,4000,-23456); f.set_manual_grab(true); process(f,128,30000);
    CHECK(all(process(f,6000,30000),-23456));
    set(f,F::P_MIX,0); f.control_tick(); process(f,128,20000); CHECK(all(process(f,500,-32768),-32768));
    f.reset(); f.trigger(); CHECK(!f.visual().manual && !f.visual().active);
}
static void warmup_reverse_damage() {
    F empty; empty.set_manual_grab(true); CHECK(all(process(empty,64,123),123));
    CHECK(!empty.visual().active); process(empty,128,123); CHECK(empty.visual().capture_frames==64);
    F f; set(f,F::P_SYNC,0); set(f,F::P_SIZE,16384); set(f,F::P_SPEED,-8192); f.control_tick();
    const uint32_t length=F::size_frames(16384,false,120*65536);
    Pcm ramp(length); for(unsigned i=0;i<length;++i)ramp[i]=q15(int(i)*10-15000);
    f.process(ramp.data(),ramp.size()); f.set_manual_grab(true); const auto reverse=process(f,256,30000);
    CHECK(f.visual().capture_frames==length);
    CHECK(reverse[180]==q15(int(length-1-180)*10-15000) && reverse[220]<reverse[180]);
    set(f,F::P_DAMAGE,32767); f.control_tick(); process(f,64000,0);
    CHECK(f.visual().loop_frames==64 && f.visual().capture_frames==length);
    // Hold remains independent from momentary grab; stop cancels it without
    // mutating the saved parameter, and an off/on edge explicitly rearms.
    set(f,F::P_HOLD,32767); f.control_tick(); f.set_manual_grab(false); process(f,64,0);
    CHECK(f.visual().held && f.visual().active); f.stop_transport(); process(f,128,321);
    CHECK(!f.visual().active && f.params[F::P_HOLD].base==32767);
    f.control_tick(); process(f,1024,321); CHECK(!f.visual().active);
    set(f,F::P_HOLD,0); f.control_tick(); set(f,F::P_HOLD,32767); f.control_tick(); process(f,128,321);
    CHECK(f.visual().held && f.visual().active);
    f.set_enabled(false); CHECK(!f.visual().active && !f.visual().fill);
    f.set_enabled(true); f.control_tick(); CHECK(all(process(f,1024,789),789));
    f.set_manual_grab(true); process(f,128,789); CHECK(f.visual().manual && f.visual().active);
}
static void exact_repeat_period() {
    const char* labels[] = {"1/128","1/64","1/32","1/16","1/8","1/4","1/2"};
    for (int i=0;i<7;++i) CHECK(!std::strcmp(F::size_label(q15((i*32768+6)/7)),labels[i]));
    CHECK(!std::strcmp(F::size_label(-32768),labels[0]) && !std::strcmp(F::size_label(32767),labels[6]));
    for (int length : {64,4000}) for (int direction : {-1,1}) {
        F f; set(f,F::P_SYNC,length==4000?32767:0); set(f,F::P_SIZE,length==4000?16384:0);
        set(f,F::P_SPEED,direction*8192); set(f,F::P_DAMAGE,0); f.control_tick();
        Pcm input(length); for (int i=0;i<length;++i) input[i]=q15((i*317)%30001-15000);
        f.process(input.data(),length); f.set_manual_grab(true);
        process(f,length-1,123); CHECK(f.visual().repeats==0 && f.visual().loop_frames==unsigned(length));
        process(f,1,123); CHECK(f.visual().repeats==1);
        const uint16_t boundary=f.visual().phase;
        const Pcm out=process(f,length*5,123);
        CHECK(f.visual().repeats==6 && f.visual().phase==boundary);
        // Exact audible period after the 128-frame wet-entry ramp, not merely
        // an unchanged size label or a nominal repeat counter.
        for (size_t i=128;i+length<out.size();++i) CHECK(out[i]==out[i+length]);
        CHECK(!all(out,123));
        const auto visual=f.visual(); CHECK(visual.wave[0]==input.front() && visual.wave[47]==input.back());
    }
}
static void clock_partition() {
    F a,b; for(F* f:{&a,&b}) { set(*f,F::P_CHANCE,17000); set(*f,F::P_LENGTH,0); f->control_tick(); process(*f,32000,5111); }
    const uint32_t fpb=14159; // deliberately not an exact phase divisor
    Pcm one(90000),split(90000); for(size_t i=0;i<one.size();++i)one[i]=q15((i*73)%51001-25500); split=one;
    a.set_transport_clock(true,0,uint32_t((1ull<<32)/fpb),0,fpb); a.process(one.data(),one.size());
    for(size_t at=0;at<split.size();) {
        const int n=std::min<size_t>((at*17+11)%229+1,split.size()-at);
        const uint64_t scaled=uint64_t(at)*(1ull<<32);
        b.set_transport_clock(true,scaled/fpb,uint32_t((1ull<<32)/fpb),uint32_t(scaled%fpb),fpb);
        b.process(split.data()+at,n); at+=n;
    }
    CHECK(one==split);
    bool altered=false; for(size_t i=0;i<one.size();++i) altered |= one[i]!=q15((i*73)%51001-25500);
    CHECK(altered);
    F exact; set(exact,F::P_CHANCE,32767); set(exact,F::P_LENGTH,0); exact.control_tick(); process(exact,4000,10000);
    exact.set_transport_clock(true,0,uint32_t((1ull<<32)/16000),0,16000);
    CHECK(all(process(exact,4000,-10000),-10000)); CHECK(!exact.visual().active);
    process(exact,1,-10000); CHECK(exact.visual().active && exact.visual().capture_frames==4000);
    // Auto duration exactly one sixteenth beat (1000frames) plus 128 release.
    process(exact,999,-10000); CHECK(exact.visual().active); process(exact,128,-10000); CHECK(!exact.visual().active);
    exact.set_transport_clock(false,0,0); CHECK(all(process(exact,16000,456),456));
}
class Input final : public Unit {
public: uint32_t at=0; Input(){kind=UnitKind::Empty;}
    void process(q15* out,int n) override {for(int i=0;i<n;++i)out[i]=q15(int((at++*71)%24001)-12000);}
};
static F* setup(Engine& e) {
    delete e.replace_unit(0,0,new Input); auto*f=new F; delete e.replace_unit(0,1,f);
    set(*f,F::P_CHANCE,32767); set(*f,F::P_LENGTH,0); e.set_bpm(137); e.transport_play(); return f;
}
static void engine_patch() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>(); F*fa=setup(*a); F*fb=setup(*b);
    Pcm x(40002),y(40002); a->render(x.data(),20001);
    for(int at=0;at<20001;) {const int n=std::min(1+at%173,20001-at); b->render(y.data()+at*2,n); at+=n;}
    CHECK(x==y && fa->visual().capture_frames==fb->visual().capture_frames);
    CHECK(fa->transport_bpm_q16()==137*65536);
    fa->set_manual_grab(true); a->render(x.data(),256); CHECK(fa->visual().active);
    a->transport_stop(); a->render(x.data(),128); CHECK(!fa->visual().active);
    fa->set_manual_grab(true); a->render(x.data(),256); CHECK(fa->visual().manual);
    a->chains[0].mute=true; a->render(x.data(),32); CHECK(!fa->visual().active && !fa->visual().fill);
    a->chains[0].mute=false; a->render(x.data(),256); CHECK(!fa->visual().active);
    fa->set_manual_grab(true); a->render(x.data(),256); fa->bypass=true; a->render(x.data(),32);
    CHECK(!fa->visual().active); fa->bypass=false; a->render(x.data(),256); CHECK(!fa->visual().active);
    // Save only real factory kinds, all eight params/mods, signed speed and hold.
    delete a->replace_unit(0,0,make_unit(UnitKind::Osc));
    for(int p=0;p<F::P_COUNT;++p) {set(*fa,p,p==F::P_SPEED?-20000:22000+p*100); fa->params[p].src=ModSource::SigilA; fa->params[p].mod_depth=q15(-3000-p); CHECK(a->set_step_lock(0,p,1,p,p==F::P_SPEED?-12345:12345));}
    a->capture_scene(0,0); fa->set_manual_grab(true);
    const std::string path="/tmp/seance-fract-"+std::to_string(long(getpid()))+".snc";
    CHECK(patchio::save(*a,path.c_str()) && patchio::load(*b,path.c_str())); std::remove(path.c_str());
    const auto*got=static_cast<F*>(b->chains[0].units[1]); CHECK(got && got->kind==UnitKind::Fract && !got->visual().manual && !got->visual().active);
    for(int p=0;p<F::P_COUNT;++p) {CHECK(got->params[p].base==fa->params[p].base && got->params[p].src==fa->params[p].src && got->params[p].mod_depth==fa->params[p].mod_depth); CHECK(got->params[p].bipolar==(p==F::P_SPEED)); CHECK(b->pattern.steps[0][p].lock_count==1);}
    CHECK(b->scenes[0][0].kind[1]==UnitKind::Fract);
    std::unique_ptr<Unit> clone(clone_unit(got)); CHECK(clone && clone->kind==UnitKind::Fract && clone->params[F::P_HOLD].base==got->params[F::P_HOLD].base);
    CHECK(!static_cast<F*>(clone.get())->visual().manual && !static_cast<F*>(clone.get())->visual().active);
    a->transport_reset(); CHECK(!fa->visual().active && !fa->visual().fill);
}
int main() {
    dry_hold_release(); warmup_reverse_damage(); exact_repeat_period(); clock_partition(); engine_patch();
    std::printf("fract core: pass (dry/stable hold/live release/reverse/shrink/warmup/exact64+4000periods/grid+partition/engine/patch+clone); object %zu + PCM %u bytes\n",sizeof(F),F::CAPACITY*2*unsigned(sizeof(q15)));
}
