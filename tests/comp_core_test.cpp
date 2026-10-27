// Focused COMP contract only: no rendering, device IO or performance claims.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>
using namespace seance;
using C=CompUnit;
using Pcm=std::vector<q15>;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"comp FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
static bool near(float a,float b,float tolerance=.003f) { return std::fabs(a-b)<=tolerance; }
static void set(Unit& u,int p,int value) { u.params[p].base=q15(value); u.params[p].snap(); }
static Pcm process(Unit& u,int n,int value) { Pcm x(n,q15(value)); u.process(x.data(),n); return x; }
static bool all(const Pcm& x,q15 v) { return std::all_of(x.begin(),x.end(),[&](q15 a){return a==v;}); }
static float law(float input,const C& c) {
    const float t=C::threshold_db(c.params[C::P_THRESHOLD].effective);
    const float r=C::ratio(c.params[C::P_RATIO].effective);
    const float k=C::knee_db(c.params[C::P_KNEE].effective),x=input-t,slope=1.f-1.f/r;
    return x>=k*.5f ? slope*x : x<=-k*.5f ? 0.f : slope*(x+k*.5f)*(x+k*.5f)/(2.f*k);
}
static void mapping_transfer() {
    static_assert(int(UnitKind::Fract)==23 && int(UnitKind::Comp)==24,"append-only kind");
    C base; CHECK(base.num_params==9 && !std::strcmp(unit_kind_name(base.kind),"COMP"));
    CHECK(near(C::threshold_db(0),-48) && near(C::threshold_db(32767),0));
    CHECK(near(C::ratio(0),1) && near(C::ratio(32767),20));
    CHECK(near(C::attack_ms(0),.1f) && near(C::attack_ms(32767),100,.001f));
    CHECK(near(C::release_ms(0,false,0),20) && near(C::release_ms(32767,false,0),2000,.01f));
    CHECK(near(C::makeup_db(0),-12) && C::makeup_db(16384)==0 && near(C::makeup_db(32767),12));
    CHECK(C::hp_hz(0)==0 && near(C::hp_hz(1),20) && near(C::hp_hz(32767),300));
    CHECK(near(C::threshold_db(base.params[0].base),-18) && near(C::ratio(base.params[1].base),4));
    CHECK(near(C::attack_ms(base.params[2].base),10) && near(C::release_ms(base.params[3].base,false,0),120,.01f));
    const char* labels[]={"1/64","1/32","1/16","1/8","1/4","1/2","1/1","2/1"};
    for(int i=0;i<C::RELEASE_DIVISIONS;++i) {
        CHECK(C::release_index(C::release_value(i))==i && !std::strcmp(C::release_label(C::release_value(i)),labels[i]));
        CHECK(near(C::release_ms(C::release_value(i),true,120*65536),31.25f*float(1<<i)));
    }
    CHECK(C::release_index(-32768)==0 && C::release_index(32767)==7);
    for(int ratio:{0,5174,32767}) for(int knee:{0,16384,32767}) for(int db:{-36,-24,-21,-18,-15,-6,0}) {
        C c; set(c,C::P_RATIO,ratio); set(c,C::P_KNEE,knee); set(c,C::P_ATTACK,0); c.control_tick();
        const q15 input=q15(std::min(32767,int(std::lround(32768.f*std::pow(10.f,float(db)/20.f)))));
        const float expected=law(20.f*std::log10(float(input)/32768.f),c);
        const auto output=process(c,4096,input);
        CHECK(near(c.visual().gr_db,expected));
        const int reference=int(std::lround(float(input)*std::pow(10.f,-expected/20.f)));
        CHECK(std::abs(int(output.back())-reference)<=15);
        if(ratio==0) CHECK(all(output,input));
    }
    std::puts("ok mappings/defaults/8 whole-note divisions/static transfer and soft knee/ratio endpoints");
}
static void timing_and_detector() {
    C c; set(c,C::P_KNEE,0); c.control_tick();
    const float desired=law(20.f*std::log10(30000.f/32768.f),c);
    process(c,320,30000);
    CHECK(near(c.visual().gr_db,desired*(1.f-std::exp(-320.f/(32.f*C::attack_ms(c.params[C::P_ATTACK].base))))));
    set(c,C::P_RELEASE,0); c.control_tick();
    float before=c.visual().gr_db; process(c,640,0); CHECK(near(c.visual().gr_db,before/std::exp(1.f)));
    set(c,C::P_CLOCK,32767); set(c,C::P_RELEASE,C::release_value(7)); c.set_transport_bpm_q16(20*65536); c.control_tick();
    CHECK(C::release_ms(c.params[C::P_RELEASE].effective,true,c.transport_bpm_q16())==24000.f);
    process(c,4000,30000); before=c.visual().gr_db; process(c,768000,0);
    CHECK(near(c.visual().gr_db,before/std::exp(1.f),.02f)); //24s tau, not free2s cap
    const float live=c.visual().gr_db; c.set_transport_bpm_q16(240*65536); CHECK(c.visual().gr_db==live);
    process(c,64000,0); CHECK(near(c.visual().gr_db,live/std::exp(1.f),.008f));
    C hp; set(hp,C::P_HP,32767); set(hp,C::P_MIX,0); hp.control_tick();
    CHECK(all(process(hp,32000,24000),24000)); CHECK(hp.visual().gr_db<.01f);
    C meter; set(meter,C::P_MIX,0); meter.control_tick(); process(meter,1,16384);
    const float peak=meter.visual().input_db; process(meter,3200,0); CHECK(meter.visual().input_db==peak);
    process(meter,32000,0); CHECK(near(meter.visual().input_db,peak-12,.03f));
    std::puts("ok one-pole tau/free+24s sync/applied-tempo retarget/HP detector-only/100ms+12dBs peak");
}
static void ramps_clip_and_lifecycle() {
    C c; set(c,C::P_RATIO,0); c.control_tick(); process(c,1,5000);
    set(c,C::P_MAKEUP,32767); c.control_tick(); const auto ramp=process(c,C::RAMP_FRAMES,5000);
    CHECK(ramp.front()>5000 && ramp.front()<5300 && ramp.back()>19890 && ramp.back()<19920);
    for(size_t i=1;i<ramp.size();++i) CHECK(ramp[i]>=ramp[i-1] && ramp[i]-ramp[i-1]<250);
    set(c,C::P_MIX,0); c.control_tick(); const auto dry=process(c,C::RAMP_FRAMES,5000); CHECK(dry.front()>5000 && dry.back()==5000);
    for(int x:{-32768,-29000,-1,0,1,30000,32767}) CHECK(all(process(c,17,x),q15(x)));
    CHECK(!c.visual().clip); // makeup cannot clip the unprocessed dry endpoint
    set(c,C::P_MIX,32767); c.control_tick(); process(c,64,30000); CHECK(c.visual().clip);
    process(c,15999,0); CHECK(c.visual().clip); process(c,1,0); CHECK(!c.visual().clip);
    c.bypass=true; const auto bypass=process(c,64,5000); CHECK(bypass.front()>5000 && bypass.back()==5000);
    CHECK(all(process(c,100,-32768),-32768)); c.bypass=false; const auto wet=process(c,64,5000);
    CHECK(wet.front()>5000 && wet.front()<5300 && wet.back()==ramp.back());
    C continuous; process(continuous,4096,30000); const auto before=continuous.visual();
    continuous.trigger(); CHECK(continuous.visual().gr_db==before.gr_db);
    Param& p=continuous.params[C::P_THRESHOLD]; p.step_lock_override=true; p.step_lock=0; p.slew_ready=false; p.resolve(0);
    continuous.control_tick(); continuous.trigger(); CHECK(p.effective==0 && continuous.visual().gr_db==before.gr_db);
    process(continuous,1,30000); CHECK(continuous.visual().gr_db>before.gr_db);
    p.step_lock_override=false; p.slew_ready=false; p.resolve(0); continuous.control_tick();
    CHECK(p.effective==p.base); continuous.bypass=true; process(continuous,64,30000);
    const float warm=continuous.visual().gr_db; process(continuous,3200,30000); CHECK(continuous.visual().gr_db>warm*.99f);
    continuous.reset(); CHECK(!continuous.visual().history_count && !continuous.visual().clip && continuous.visual().gr_db==0);
    std::puts("ok exact dry/makeup wet-only/2ms ramps+warm bypass/500ms clip/trigger+lock continuity/reset");
}
static void same_visual(const C::Visual& a,const C::Visual& b) {
    CHECK(a.input_db==b.input_db && a.output_db==b.output_db && a.detector_db==b.detector_db && a.gr_db==b.gr_db);
    CHECK(a.clip==b.clip && a.history_count==b.history_count && !std::memcmp(a.history,b.history,sizeof a.history));
}
static void partition_history() {
    C a,b; Pcm one(71017),split;
    for(size_t i=0;i<one.size();++i) one[i]=q15(int((i*197)%65536)-32768);
    split=one;
    for(size_t at=0;at<one.size();) {
        const size_t end=std::min(one.size(),at+11437);
        for(C* c:{&a,&b}) { set(*c,C::P_HP,int(at%32768)); set(*c,C::P_MIX,int((at+1537)%32768)); set(*c,C::P_MAKEUP,int((at+21000)%32768)); c->bypass=(at/11437)%3==1; c->control_tick(); }
        a.process(one.data()+at,int(end-at));
        while(at<end) { const int n=int(std::min(end-at,size_t((at*17+11)%229+1))); b.trigger(); b.process(split.data()+at,n); at+=n; }
        same_visual(a.visual(),b.visual());
    }
    CHECK(one==split);
    C exact; std::vector<float> maxima; float partial=0;
    for(int i=0;i<66*C::HISTORY_FRAMES+17;++i) {
        q15 sample=i%2000<600 ? 30000 : 0; exact.process(&sample,1); partial=std::max(partial,exact.visual().gr_db);
        if((i+1)%C::HISTORY_FRAMES==0) { maxima.push_back(partial); partial=0; }
    }
    const auto v=exact.visual(); CHECK(v.history_count==64);
    for(int i=0;i<63;++i) CHECK(v.history[i]==maxima[maxima.size()-63+i]);
    CHECK(v.history[63]==partial);
    // Complete current bin and ensure no unmeasured zero point is appended.
    process(exact,1007,0); CHECK(exact.visual().history_count==64 && exact.visual().history[63]>=partial);
    std::puts("ok sample-identical arbitrary partitions/real1024-frame max bins+partial/wrapped chronological history");
}
class Input final : public Unit {
public: int value=24000; unsigned hits=0;
    Input(){kind=UnitKind::Empty;}
    void process(q15* out,int n) override { std::fill(out,out+n,q15(value)); }
    void trigger() override { ++hits; }
};
static void render(Engine& e,int n) { Pcm out(n*2); e.render(out.data(),n); }
static void engine_and_persistence() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>(); auto* input=new Input;
    auto* c=new C; delete a->replace_unit(0,0,input); delete a->replace_unit(0,1,c);
    c->bypass=true; render(*a,10000); CHECK(c->visual().gr_db>8.f); //engine must call bypassed COMP
    a->chains[0].mute=true; const float before=c->visual().gr_db; render(*a,2048); CHECK(c->visual().gr_db>=before);
    a->chains[0].mute=false; c->bypass=false; a->transport_play(); render(*a,64); a->transport_stop();
    const float stopped=c->visual().gr_db; render(*a,32); CHECK(c->visual().gr_db>=stopped);
    a->transport_reset(); CHECK(c->visual().gr_db>8.f);
    a->set_live_retrigger(0,3,true); render(*a,5000); CHECK(input->hits>1 && c->visual().gr_db>8.f); a->set_live_retrigger(0,3,false);
    set(*c,C::P_THRESHOLD,12000); c->params[0].src=ModSource::SigilA; c->params[0].mod_depth=6000;
    a->sigils[0].drawn=true; a->sigils[0].depth=32767; for(auto& v:a->sigils[0].table) v=32767;
    a->set_bpm(137); a->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,60); render(*a,3200);
    CHECK(c->params[0].target>17990 && c->params[0].base==12000);
    CHECK(c->transport_bpm_q16()==a->effective_bpm_q16() && c->transport_bpm_q16()>137*65536);
    c->params[0].src=ModSource::None; c->params[0].mod_depth=0; c->params[0].snap();
    a->set_clock_mod(ClockTarget::Tempo,ModSource::None,0); a->set_bpm(120); a->transport_reset();
    // Legacy scheduler fires step0 on the first sixteenth boundary (frame4000).
    {
        auto seq=std::make_unique<Engine>(); auto* sc=new C; seq->set_bpm(120);
        delete seq->replace_unit(0,0,new Input); delete seq->replace_unit(0,1,sc);
        set(*sc,C::P_THRESHOLD,12000); sc->control_tick(); process(*sc,4096,24000);
        CHECK(seq->set_step_lock(0,0,1,0,5000)); seq->pattern.steps[0][0].flags=SeqStep::Trig;
        seq->pattern.steps[0][0].set_retrig_count(4); seq->pattern.length[0]=2;
        seq->set_sequencer_enabled(true); seq->transport_play(); const float prior=sc->visual().gr_db;
        render(*seq,4000); CHECK(sc->params[0].effective==5000 && sc->params[0].base==12000 && sc->visual().gr_db>prior*.99f);
        render(*seq,3999); CHECK(sc->visual().gr_db>8.f); render(*seq,1);
        CHECK(!sc->params[0].step_lock_override && sc->params[0].effective==12000);
    }
    a->transport_stop(); a->set_sequencer_enabled(false); delete a->replace_unit(0,0,make_unit(UnitKind::Osc));
    for(int p=0;p<C::P_COUNT;++p) {
        set(*c,p,17000+p*631); c->params[p].src=ModSource(1+p%5); c->params[p].mod_depth=q15(-3000-p*71);
        CHECK(a->set_step_lock(0,p,1,p,8000+p));
    }
    c->bypass=true; a->capture_scene(0,0);
    a->macro_pads[0].route_count=1; a->macro_pads[0].routes[0]={0,1,C::P_HP,0,32767};
    const std::string path="/tmp/seance-comp-"+std::to_string(long(getpid()))+".snc";
    CHECK(patchio::save(*a,path.c_str()) && patchio::load(*b,path.c_str())); std::remove(path.c_str());
    const auto* loaded=static_cast<C*>(b->chains[0].units[1]); CHECK(loaded && loaded->kind==UnitKind::Comp && loaded->bypass);
    CHECK(loaded->visual().history_count==0 && loaded->visual().gr_db==0);
    for(int p=0;p<C::P_COUNT;++p) {
        CHECK(loaded->params[p].base==c->params[p].base && loaded->params[p].src==c->params[p].src && loaded->params[p].mod_depth==c->params[p].mod_depth);
        CHECK(!loaded->params[p].bipolar && b->pattern.steps[0][p].lock_count>=1);
        CHECK(b->scenes[0][0].base[1][p]==c->params[p].base);
    }
    CHECK(b->scenes[0][0].kind[1]==UnitKind::Comp && b->macro_pads[0].routes[0].param==C::P_HP);
    std::unique_ptr<Unit> clone(clone_unit(c)); CHECK(clone && clone->kind==UnitKind::Comp && clone->bypass);
    const auto clean=static_cast<C*>(clone.get())->visual(); CHECK(clean.gr_db==0 && !clean.history_count && !clean.clip);
    for(int p=0;p<C::P_COUNT;++p) CHECK(clone->params[p].base==c->params[p].base && !clone->params[p].step_lock_override);
    std::printf("ok engine warm bypass/mute/stop/retrig/lock boundary/sigil+applied clock/SNC9params+scene+route/clone; object%zu bytes\n",sizeof(C));
}
int main() {
    mapping_transfer(); timing_and_detector(); ramps_clip_and_lifecycle(); partition_history(); engine_and_persistence();
    std::puts("comp core: 5 focused groups pass");
}
