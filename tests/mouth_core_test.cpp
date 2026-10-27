// Focused MOUTH contracts only. No media export, device IO or performance claims.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <unistd.h>
using namespace seance;
using M=MouthUnit;
using Pcm=std::vector<q15>;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"mouth FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
static bool near(float a,float b,float tolerance=.003f) { return std::fabs(a-b)<=tolerance; }
static void set(Unit& u,int p,int value) { u.params[p].base=q15(value); u.params[p].snap(); }
static Pcm process(Unit& u,int n,int value) { Pcm x(n,q15(value)); u.process(x.data(),n); return x; }
static bool all(const Pcm& x,q15 v) { return std::all_of(x.begin(),x.end(),[&](q15 a){return a==v;}); }
static void same(const M::Model& a,const M::Model& b) {
    for(int i=0;i<3;++i) CHECK(a.hz[i]==b.hz[i] && a.b0[i]==b.b0[i] && a.b2[i]==b.b2[i] && a.a1[i]==b.a1[i] && a.a2[i]==b.a2[i]);
    CHECK(a.wet_gain==b.wet_gain && a.mix==b.mix && a.output_gain==b.output_gain);
}
static void stable(const M::Model& m) {
    CHECK(std::isfinite(m.wet_gain) && m.wet_gain>=.65f && m.wet_gain<=1.593f);
    CHECK(std::isfinite(m.output_gain) && m.output_gain>0.f && m.output_gain<4.f);
    CHECK(m.mix>=0.f && m.mix<=1.f);
    for(int i=0;i<3;++i) {
        CHECK(std::isfinite(m.hz[i]) && m.hz[i]>=135.f && m.hz[i]<=6020.f);
        CHECK(std::isfinite(m.b0[i]) && m.b0[i]==-m.b2[i]);
        CHECK(std::fabs(m.a2[i])<1.f && 1.f+m.a1[i]+m.a2[i]>0.f && 1.f-m.a1[i]+m.a2[i]>0.f);
    }
}
static double reference(const M::Model& m,double hz) {
    const auto z=std::polar(1.,-6.283185307179586*hz/ENGINE_SR);
    std::complex<double> wet=0.;
    for(int i=0;i<3;++i) wet+=(double(m.b0[i])+double(m.b2[i])*z*z)/(1.+double(m.a1[i])*z+double(m.a2[i])*z*z);
    const auto out=1.-double(m.mix)+double(m.mix)*m.wet_gain*m.output_gain*wet;
    return std::max(-96.,20.*std::log10(std::max(1.e-12,std::abs(out))));
}
static void mapping_and_model() {
    static_assert(int(UnitKind::Comp)==24 && int(UnitKind::Mouth)==25 && int(UnitKind::Count)==26,"append-only kinds");
    static_assert(std::is_trivially_copyable<M::Model>::value,"snapshot scalar model");
    M m; CHECK(m.num_params==5 && !std::strcmp(unit_kind_name(m.kind),"MOUTH"));
    const q15 defaults[]={2785,16384,20316,32767,16384};
    for(int p=0;p<M::P_COUNT;++p) CHECK(m.params[p].base==defaults[p] && !m.params[p].bipolar);
    same(m.visual_model(),M::model_from_values(defaults));
    CHECK(M::size_ratio(0)==.5f && M::size_ratio(16384)==1.f && M::size_ratio(32767)==2.f);
    CHECK(M::output_db(0)==-18.f && M::output_db(16384)==0.f && M::output_db(32767)==12.f);
    CHECK(M::resonance_q(0)==2.f && M::resonance_q(32767)==12.f);
    CHECK(M::size_ratio(-32768)==.5f && M::output_db(-32768)==-18.f);
    q15 values[]={0,16384,20316,32767,16384};
    const float anchors[5][3]={{730,1090,2440},{530,1840,2480},{270,2290,3010},{570,840,2410},{300,870,2240}};
    for(int vowel=0;vowel<5;++vowel) {
        values[0]=q15((vowel*32767+2)/4); const auto model=M::model_from_values(values);
        for(int i=0;i<3;++i) CHECK(near(model.hz[i],anchors[vowel][i],.16f));
    }
    values[0]=4096; auto model=M::model_from_values(values);
    CHECK(near(model.hz[0],std::sqrt(730.f*530.f),.02f));
    for(int v=0;v<=32767;v+=2047) for(int size:{0,16384,32767}) for(int q:{0,32767}) {
        values[0]=q15(v);values[1]=q15(size);values[2]=q15(q); model=M::model_from_values(values); stable(model);
        for(float hz:{0.f,135.f,730.f,1700.f,4000.f,12000.f}) {
            CHECK(near(M::response_db(model,hz),float(reference(model,hz)),.018f));
            model.mix=.37f; CHECK(near(M::response_db(model,hz),float(reference(model,hz)),.018f)); model.mix=1.f;
        }
    }
    model.mix=0.f; model.output_gain=3.98f; CHECK(M::response_db(model,730.f)==0.f);
    CHECK(M::response_db(M::Model{},1000.f)==0.f);
    std::puts("ok append-only kind/defaults/maps/anchors/geometric vowel/no wrap/stable poles/complex dry-wet model");
}
static void audio_and_response() {
    M silence; CHECK(all(process(silence,32000,0),0));
    for(int vowel:{0,16384,32767}) for(int size:{0,32767}) for(int res:{0,32767}) {
        M m; set(m,0,vowel);set(m,1,size);set(m,2,res); m.control_tick(); stable(m.visual_model());
        const auto dc=process(m,32000,32767);
        CHECK(std::all_of(dc.end()-1024,dc.end(),[](q15 v){return std::abs(int(v))<=1;}));
        const auto tail=process(m,32000,0);
        CHECK(std::any_of(tail.begin(),tail.begin()+160,[](q15 v){return v!=0;}));
        CHECK(all(Pcm(tail.end()-1024,tail.end()),0));
        m.reset(); CHECK(all(process(m,1024,0),0));
    }
    // Measured steady sine gain agrees with the frozen complex model, not a
    // sum of Gaussian envelopes or a sum of magnitudes. Internal test PCM only.
    for(int mix:{12288,32767}) for(int freq:{500,1000,3000}) {
        M m; set(m,M::P_MIX,mix);set(m,M::P_OUT,11000); m.control_tick();
        Pcm samples(32000); double input_power=0.,output_power=0.;
        for(int n=0;n<int(samples.size());++n) samples[n]=q15(std::lround(4000.*std::sin(6.283185307179586*freq*n/ENGINE_SR)));
        for(int n=16000;n<32000;++n) input_power+=double(samples[n])*samples[n];
        m.process(samples.data(),int(samples.size()));
        for(int n=16000;n<32000;++n) output_power+=double(samples[n])*samples[n];
        CHECK(near(float(10.*std::log10(output_power/input_power)),M::response_db(m.visual_model(),float(freq)),.025f));
    }
    // Frequent control-rate extreme retargets must stay finite and drain fully.
    M stress; Pcm x(32);
    for(int block=0;block<4096;++block) {
        for(int p=0;p<M::P_COUNT;++p) set(stress,p,(block&(1<<p)) ? 32767 : 0);
        stress.control_tick();
        for(int n=0;n<32;++n) x[n]=q15(int((block*103+n*1777)%65536)-32768);
        stress.process(x.data(),32); stable(stress.visual_model());
    }
    set(stress,M::P_MIX,32767); stress.control_tick(); const auto quiet=process(stress,64000,0);
    CHECK(all(Pcm(quiet.end()-1024,quiet.end()),0));
    std::puts("ok silence/DC rejection/extreme poles+tails/reset/phase-correct measured response/rapid-retarget drain");
}
static void ramps_and_partitions() {
    M m; process(m,1,0); const auto before=m.visual_model();
    set(m,M::P_VOWEL,32767);set(m,M::P_SIZE,32767);set(m,M::P_OUT,32767);set(m,M::P_MIX,0);m.control_tick();
    same(m.visual_model(),before); q15 values[]={32767,32767,20316,0,32767}; const auto target=M::model_from_values(values);
    process(m,1,0); const auto first=m.visual_model();
    CHECK(near(first.hz[0],before.hz[0]+(target.hz[0]-before.hz[0])/M::RAMP_FRAMES));
    CHECK(near(first.output_gain,before.output_gain+(target.output_gain-before.output_gain)/M::RAMP_FRAMES));
    CHECK(first.mix>0.f && first.mix<1.f); process(m,M::RAMP_FRAMES-1,0); same(m.visual_model(),target);
    for(int v:{-32768,-1,0,1,32767}) CHECK(all(process(m,17,v),q15(v))); // trim wet-only
    set(m,M::P_MIX,32767);m.reset();values[M::P_MIX]=32767;same(m.visual_model(),M::model_from_values(values));
    CHECK(all(process(m,1024,0),0));
    std::puts("ok control retarget does not jump/exact 160-frame endpoints/wet-only trim/reset snaps effective");
}
static void partition_and_bypass() {
    M a,b; Pcm one(20017),split;
    for(size_t i=0;i<one.size();++i) one[i]=q15(int((i*197)%65536)-32768);
    split=one;
    for(size_t at=0;at<one.size();) {
        const size_t end=std::min(one.size(),at+337);
        for(M* m:{&a,&b}) {
            for(int p=0;p<M::P_COUNT;++p) set(*m,p,int((at*17+p*6291)%32768));
            m->bypass=(at/337)%3==1;m->control_tick();
        }
        a.process(one.data()+at,int(end-at));
        while(at<end) { const int n=int(std::min(end-at,size_t((at*17+11)%97+1)));b.trigger();b.control_tick();b.process(split.data()+at,n);at+=n; }
        same(a.visual_model(),b.visual_model());
    }
    CHECK(one==split);
    M warm; process(warm,1000,7000);warm.bypass=true;process(warm,M::RAMP_FRAMES,7000);CHECK(warm.visual_model().mix==0.f);
    CHECK(all(process(warm,32,-32768),-32768));warm.bypass=false;process(warm,1,0);
    CHECK(warm.visual_model().mix>0.f && warm.visual_model().mix<.01f);
    process(warm,M::RAMP_FRAMES-1,0);CHECK(warm.visual_model().mix==1.f);
    M uninterrupted,triggered;process(uninterrupted,32,7000);process(triggered,32,7000);triggered.trigger();
    CHECK(process(uninterrupted,256,0)==process(triggered,256,0));
    std::puts("ok sample-time coefficient+gain ramps/exact dry+warm bypass/trigger continuity/arbitrary partition identity");
}
class Input final : public Unit {
public:
    Input(){kind=UnitKind::Empty;}
    void process(q15* out,int n) override { std::fill(out,out+n,q15(16000)); }
};
static void render(Engine& e,int n) { Pcm out(n*2);e.render(out.data(),n); }
static void engine_and_persistence() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();auto* m=static_cast<M*>(make_unit(UnitKind::Mouth));CHECK(m);
    delete a->replace_unit(0,0,new Input);delete a->replace_unit(0,1,m);
    m->bypass=true;render(*a,1000);CHECK(m->visual_model().mix==0.f);
    set(*m,M::P_VOWEL,32767);m->control_tick();render(*a,1000);CHECK(m->visual_model().hz[0]==300.f); // bypass must still process
    m->bypass=false;
    set(*m,0,12000);m->params[0].src=ModSource::SigilH;m->params[0].mod_depth=6000;
    a->sigils[7].drawn=true;a->sigils[7].depth=32767;for(auto& v:a->sigils[7].table)v=32767;
    render(*a,3200);CHECK(m->params[0].target>17990 && m->params[0].base==12000);
    m->params[0].src=ModSource::None;m->params[0].mod_depth=0;m->params[0].snap();
    a->capture_scene(0,0);for(int p=0;p<M::P_COUNT;++p)set(*m,p,22000+p*500);a->capture_scene(0,1);
    a->apply_scene_morph(0,32767);CHECK(m->params[0].base>=21999);
    a->transport_reset();a->set_bpm(120);
    set(*m,0,12000);CHECK(a->set_step_lock(0,0,1,0,5000));a->pattern.steps[0][0].flags=SeqStep::Trig;a->pattern.length[0]=2;
    a->set_sequencer_enabled(true);a->transport_play();render(*a,4000);
    CHECK(m->params[0].effective==5000 && m->params[0].base==12000);render(*a,4000);
    CHECK(!m->params[0].step_lock_override && m->params[0].effective==12000);
    a->transport_stop();a->set_sequencer_enabled(false);delete a->replace_unit(0,0,make_unit(UnitKind::Osc));
    for(int p=0;p<M::P_COUNT;++p) {
        set(*m,p,17000+p*631);m->params[p].src=sigil_mod_source(p);m->params[p].mod_depth=q15(-3000-p*71);
        m->params[p].mod_enabled=p!=2;CHECK(a->set_step_lock(0,p,1,p,8000+p));
    }
    m->bypass=true;a->capture_scene(0,0);a->macro_pads[0].route_count=1;a->macro_pads[0].routes[0]={0,1,M::P_OUT,0,32767};
    const std::string path="/tmp/seance-mouth-"+std::to_string(long(getpid()))+".snc";
    CHECK(patchio::save(*a,path.c_str()) && patchio::load(*b,path.c_str()));std::remove(path.c_str());
    auto* loaded=static_cast<M*>(b->chains[0].units[1]);CHECK(loaded && loaded->kind==UnitKind::Mouth && loaded->bypass);
    for(int p=0;p<M::P_COUNT;++p) {
        CHECK(loaded->params[p].base==m->params[p].base && loaded->params[p].src==m->params[p].src && loaded->params[p].mod_depth==m->params[p].mod_depth);
        CHECK(loaded->params[p].mod_enabled==m->params[p].mod_enabled && !loaded->params[p].bipolar);
        CHECK(b->pattern.steps[0][p].lock_count>=1 && b->scenes[0][0].base[1][p]==m->params[p].base);
    }
    CHECK(b->scenes[0][0].kind[1]==UnitKind::Mouth && b->macro_pads[0].routes[0].param==M::P_OUT);
    m->bypass=false;m->control_tick();process(*m,1024,17000);
    m->params[0].temp_override=true;m->params[0].temp=300;m->params[0].slew_ready=false;m->params[0].resolve(0);m->control_tick();
    std::unique_ptr<Unit> clone(clone_unit(m));CHECK(clone && clone->kind==UnitKind::Mouth && !clone->bypass);
    q15 stored[M::P_COUNT];for(int p=0;p<M::P_COUNT;++p) { stored[p]=m->params[p].base;CHECK(clone->params[p].base==stored[p] && !clone->params[p].temp_override && !clone->params[p].step_lock_override); }
    same(static_cast<M*>(clone.get())->visual_model(),M::model_from_values(stored));CHECK(all(process(*clone,1024,0),0));
    std::printf("ok engine warm bypass/sigil-h/scene morph/step lock/SNC5param+scene+route/clean clone; object %zu model %zu bytes\n",sizeof(M),sizeof(M::Model));
}
int main() {
    mapping_and_model();audio_and_response();ramps_and_partitions();partition_and_bypass();engine_and_persistence();
    std::puts("mouth core: 5 focused groups pass");
}
