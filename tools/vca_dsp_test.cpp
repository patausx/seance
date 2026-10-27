// Focused real VCA DSP tests; independent pow oracle and frozen old PCM model.
// g++ -O2 -std=gnu++17 -Wall -Wextra -ffunction-sections -fdata-sections -Icore
// tools/vca_dsp_test.cpp core/engine/units.cpp -Wl,--gc-sections -o /tmp/vca_dsp_test
#include "engine/units.h"
#include "engine/vca_envelope.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>

using seance::VcaUnit;
using seance::q15;
namespace v = seance::vca_envelope;
namespace {
uint64_t assertions = 0;
#define CHECK(x) do { ++assertions; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
void set(VcaUnit& u, int p, int raw) { u.params[p].base = (q15)raw; u.params[p].snap(); }
void setup(VcaUnit& u, int attack=0, int hold=0, int decay=0, int gain=32767, int floor=0, int ac=0, int dc=0, int duck=0) {
    u.set_modern(false); // retain this original suite as the frozen eight-control AHD oracle
    const int raw[] = {gain,attack,decay,hold,floor,ac,dc,duck};
    for (int p=0;p<VcaUnit::AHD_COUNT;++p) set(u,p,raw[p]);
}
q15 tick(VcaUnit& u, q15 input=32767) { u.process(&input,1); return input; }
uint32_t random(uint32_t& s) { s^=s<<13; s^=s>>17; s^=s<<5; return s; }

// Independently expressed arithmetic from the pre-AHD VCA, frozen forever.
struct Legacy {
    int level=0; bool active=false, rising=false;
    void trigger(int a) { active=true; rising=true; if (a<128) { level=32767; rising=false; } }
    q15 sample(q15 input,int gain,int a,int d) {
        if (a<128 && d<128) return (q15)std::floor((double)input*gain/32768.0);
        const int an=(int)std::floor(std::floor((double)a*a/32768.0)*32000.0/32768.0)+1;
        const int dn=(int)std::floor(std::floor((double)d*d/32768.0)*32000.0/32768.0)+1;
        if(active) {
            if(rising) { level+=32767/an; if(level>=32767) {level=32767;rising=false;} }
            else { level-=(32767+dn-1)/dn; if(level<=0) {level=0;active=false;} }
        }
        const int g=(int)std::floor((double)gain*level/32768.0);
        return (q15)std::floor((double)input*g/32768.0);
    }
};
void legacy_exact() {
    uint64_t hash=1469598103934665603ull;
    uint32_t rng=1937;
    for(int a:{0,127,128,4096,32767}) for(int d:{0,127,128,12000,32767}) {
        VcaUnit u; u.set_flexible(false); setup(u,a,31000,d,26000,29000,13000,-21000,32767);
        Legacy old;
        CHECK(u.num_params==3 && !u.flexible);
        for(int i=0;i<48000;++i) {
            if(i==0 || i==170 || i==9311 || i==17391) {u.trigger();old.trigger(a);}
            const q15 in=(q15)random(rng);
            const q15 out=tick(u,in); CHECK(out==old.sample(in,26000,a,d));
            hash^=(uint16_t)out;hash*=1099511628211ull;
        }
    }
    CHECK(hash==0x484eb1aabf948439ull);
    std::printf("ok legacy independent PCM/gain-only/retrigger; frozen fnv64 %016llx\n",(unsigned long long)hash);
}
void mappings_curves() {
    static_assert(VcaUnit::P_GAIN==0&&VcaUnit::P_ATTACK==1&&VcaUnit::P_DECAY==2&&VcaUnit::P_HOLD==3&&VcaUnit::P_FLOOR==4&&VcaUnit::P_A_CURVE==5&&VcaUnit::P_D_CURVE==6&&VcaUnit::P_DUCK==7&&VcaUnit::AHD_COUNT==8);
    VcaUnit u(false); CHECK(u.flexible && u.num_params==8 && !u.visual_ready());
    CHECK(v::time_frames(u.params[1].base)==480);
    CHECK(std::abs((int)v::time_frames(u.params[2].base)-8000)<=1);
    CHECK(u.params[5].bipolar && u.params[6].bipolar);
    CHECK(u.sequencer_properties().gate_processor);
    CHECK(v::time_frames(0)==32 && v::time_frames(32767)==256000);
    CHECK(v::time_frames(0,true)==0 && v::time_frames(1,true)==32 && v::time_frames(32767,true)==256000);
    CHECK(v::time_frames(-32768)==32 && v::time_frames(-32768,true)==0);
    double time_error=0;
    for(bool hold:{false,true}) {
        uint32_t prev=0;
        for(int raw=hold?1:0;raw<=32767;++raw) {
            const uint32_t t=v::time_frames((q15)raw,hold); CHECK(t>=prev && t%32==0);prev=t;
            const double exact=32*std::pow(8000.,(double)(raw-(hold?1:0))/(hold?32766:32767));
            time_error=std::max(time_error,std::abs(t-exact)); CHECK(std::abs(t-exact)<17.1);
            const int round=(int)v::time_frames(v::time_value(t,hold),hold); CHECK(round==(int)t);
        }
    }
    for(uint32_t ms:{15u,180u,240u,250u,780u}) for(bool hold:{false,true})
        CHECK(v::time_frames(v::time_value(ms*32,hold),hold)==ms*32);
    double worst=0, low=0;
    // Full progress coverage at 129 bends, plus full bend coverage near zero.
    for(int j=0;j<=128;++j) {
        const q15 b=(q15)(-32767+j*65534/128);
        int previous=0;
        for(int p=0;p<=32767;++p) {
            const int y=v::curve((q15)p,b);CHECK(y>=previous && y<=32767);previous=y;
            const double exact=32767*std::pow(p/32767.,std::pow(2.,2.*b/32767.));
            worst=std::max(worst,std::abs(y-exact)); CHECK(std::abs(y-exact)<1.1);
            if(!p) CHECK(y==0);
            if(p==32767) CHECK(y==32767);
            if(!b) CHECK(y==p);
        }
    }
    for(int b=-32767;b<=32767;++b) for(uint64_t phase:{1ull,16777ull,33554ull,65536ull,131072ull,4294967295ull}) {
        const int y=v::curve_phase(phase,(q15)b);
        const double exact=32767*std::pow((double)phase/4294967296.,std::pow(2.,2.*b/32767.));
        low=std::max(low,std::abs(y-exact)); CHECK(std::abs(y-exact)<1.1);
    }
    CHECK(v::exponent_q20(-32767)==262144u && v::exponent_q20(0)==1048576u && v::exponent_q20(32767)==4194304u);
    CHECK(v::curve_phase(1ull<<32,-32768)==32767);
    CHECK(v::curve(-32768,32767)==0);
    std::printf("ok curve full-progress/129 bends and near-zero/all bends: errors %.6f / %.6f q15 LSB; logarithmic time max error %.6f frames\n",worst,low,time_error);
}
void exact_timing_retrigger() {
    for(int raw:{0,10000,32767}) for(int h:{0,1,32767}) for(int bend:{-32767,0,32767}) {
        VcaUnit u;setup(u,raw,h,raw,32767,0,bend,bend);u.trigger();
        const uint32_t a=v::time_frames((q15)raw), d=a, hold=v::time_frames((q15)h,true);
        int previous=0;
        for(uint32_t i=1;i<=a;++i) {
            const int y=tick(u);CHECK(y>=previous);previous=y;
            CHECK(u.visual_stage()==1);CHECK(u.visual_progress_q16()==(uint32_t)(((uint64_t)i<<16)/a));
            if(i==a) CHECK(y==32767);
        }
        for(uint32_t i=1;i<=hold;++i) {CHECK(tick(u)==32767);CHECK(u.visual_stage()==2);CHECK(u.visual_state()&VcaUnit::VisualHolding);}
        for(uint32_t i=1;i<=d;++i) {
            const int y=tick(u);CHECK(y<=previous);previous=y;
            CHECK(u.visual_stage()==3);CHECK(u.visual_progress_q16()==(uint32_t)(((uint64_t)i<<16)/d));
            if(i==d) CHECK(y==0 && !(u.visual_state()&VcaUnit::VisualActive));
        }
        CHECK(tick(u)==0 && u.visual_stage()==0);
    }
    VcaUnit u;setup(u);u.trigger();
    for(int i=0;i<48;++i) tick(u); // midpoint decay
    const int origin=u.visual_envelope(); CHECK(origin>15000 && origin<17000);
    u.trigger(); CHECK(tick(u)>=origin);CHECK(u.visual_stage()==1);
    for(int i=1;i<31;++i) {tick(u);CHECK(u.visual_envelope()<32767);}
    CHECK(tick(u)==32767);CHECK(u.visual_progress_q16()==65536);
    // The first modern 8s concave sample isn't rounded to zero Q15 progress.
    setup(u,32767,0,32767,32767,0,-32767,0);u.reset();u.trigger();
    CHECK(tick(u)>1400);
    std::puts("ok exact minimum/maximum AHD sample counts, zero hold, monotonic endpoints, 8s fractional start, full-time soft retrigger");
}
void latching_and_live() {
    VcaUnit u; setup(u);u.trigger();CHECK(tick(u)>0);
    set(u,VcaUnit::P_ATTACK,32767);set(u,VcaUnit::P_A_CURVE,32767);
    set(u,VcaUnit::P_HOLD,1);set(u,VcaUnit::P_DECAY,32767);set(u,VcaUnit::P_D_CURVE,-32767);
    for(int i=1;i<32;++i) tick(u);
    CHECK(u.visual_envelope()==32767 && u.visual_effective(1)==0 && u.visual_effective(5)==0);
    tick(u);CHECK(u.visual_stage()==2 && u.visual_effective(3)==1);
    set(u,VcaUnit::P_HOLD,32767);
    for(int i=1;i<32;++i) tick(u);
    tick(u);CHECK(u.visual_stage()==3 && u.visual_effective(3)==1);
    CHECK(u.visual_effective(2)==32767 && u.visual_effective(6)==-32767);
    set(u,VcaUnit::P_D_CURVE,32767);set(u,VcaUnit::P_DECAY,0);tick(u);
    CHECK(u.visual_effective(2)==32767 && u.visual_effective(6)==-32767);
    u.trigger(); CHECK(u.visual_effective(1)==0 && u.visual_effective(5)==0);
    tick(u);CHECK(u.visual_effective(1)==32767 && u.visual_effective(5)==32767);
    // Finite gain/floor interpolation, with sample-rate steps rather than blocks.
    u.reset();setup(u,0,0,0,32767,32767);CHECK(tick(u)==32767);
    set(u,VcaUnit::P_GAIN,0);int previous=32767;
    for(int i=1;i<=64;++i) {int now=tick(u);CHECK(now<=previous && previous-now<=513);previous=now;}
    CHECK(previous==0 && u.visual_effective(0)==0);
    set(u,VcaUnit::P_GAIN,32767);for(int i=0;i<64;++i)tick(u);CHECK(u.visual_gain()==32767);
    set(u,VcaUnit::P_FLOOR,0);previous=32767;
    for(int i=0;i<64;++i) {int now=tick(u);CHECK(now<=previous && previous-now<=513);previous=now;}
    CHECK(previous==0 && u.visual_effective(4)==0);
    set(u,VcaUnit::P_DUCK,32767);previous=0;
    for(int i=0;i<64;++i) {int now=tick(u);CHECK(now>=previous && now-previous<=513);previous=now;}
    CHECK(previous==32767 && u.visual_effective(7)==32767);
    set(u,VcaUnit::P_DUCK,0);previous=32767;
    for(int i=0;i<64;++i) {int now=tick(u);CHECK(now<=previous && previous-now<=513);previous=now;}
    CHECK(previous==0 && u.visual_effective(7)==0);
    // Engine-resolved modulation is interpolated, never mutates base.
    set(u,VcaUnit::P_FLOOR,1000);u.params[4].mod_depth=32767;u.params[4].resolve(20000);
    CHECK(u.params[4].effective>1000);for(int i=0;i<64;++i)tick(u);
    CHECK(u.visual_effective(4)==u.params[4].effective && u.params[4].base==1000);
    std::puts("ok stage-entry latching/modulation, finite64 gain/floor/polarity interpolation and limits");
}
void bounds_blocks_reset() {
    for(int gain:{0,1,16384,32767})for(int floor:{0,1,16384,32767})for(int duck:{0,32767}) {
        VcaUnit u;setup(u,0,0,0,gain,floor,-32767,32767,duck);u.trigger();
        for(int i=0;i<100;++i) {const int out=tick(u);CHECK(out>=0 && out<=gain);CHECK(u.visual_gain()==out);CHECK(u.visual_envelope()>=floor);if(!gain)CHECK(out==0);}
    }
    for(q15 in:{(q15)-32768,(q15)-1,(q15)0,(q15)1,(q15)32767}) {
        VcaUnit u;setup(u,0,0,0,32767,32767);CHECK(tick(u,in)==in);
    }
    VcaUnit a,b;setup(a,7000,11000,19000,32767,0,-32767,32000);setup(b,7000,11000,19000,32767,0,-32767,32000);
    a.trigger();b.trigger();uint32_t rng=1729;
    for(int block=0;block<100;++block) {
        const int count=1+random(rng)%1499;
        std::vector<q15> x((size_t)count),y;
        for(auto& sample:x)sample=(q15)random(rng);
        y=x;
        const int gain=random(rng)%32768, floor=random(rng)%32768, duck=(block%3==0)?32767:0;
        for(VcaUnit* u:{&a,&b}) {set(*u,0,gain);set(*u,4,floor);set(*u,7,duck);if(block%7==0)u->trigger();}
        a.process(x.data(),count);
        for(int i=0;i<count;) {const int n=std::min(count-i,1+(int)(random(rng)%97));b.process(y.data()+i,n);i+=n;}
        CHECK(x==y && a.visual_gain()==b.visual_gain() && a.visual_progress_q16()==b.visual_progress_q16());
    }
    q15 untouched=17000;const auto env=a.visual_envelope();a.process(nullptr,5);a.process(&untouched,0);a.process(&untouched,-1);CHECK(untouched==17000 && a.visual_envelope()==env);
    a.reset();CHECK(!a.visual_ready());CHECK(a.visual_stage()==0 && a.visual_envelope()==0 && a.visual_gain()==0);
    for(int p=0;p<8;++p)CHECK(a.visual_effective(p)==0);
    std::vector<q15> silent(20000,0);a.trigger();a.process(silent.data(),(int)silent.size());for(q15 x:silent)CHECK(!x);
    std::printf("ok gain/floor/duck bounds/unity/silence/reset/null no-op/block subdivision; sizeof VcaUnit=%zu bytes\n",sizeof(VcaUnit));
}
void cost() {
    VcaUnit u;setup(u,32767,0,32767,26000,3000,24000,-21000);u.trigger();
    std::vector<q15> pcm(32000,16000);
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<128;++i) {if(i%8==0)u.trigger();u.process(pcm.data(),(int)pcm.size());}
    const double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/(128*32000);
    std::printf("host-only curved processing %.2f ns/sample; LUT ROM %zu bytes (not a 3DS timing claim)\n",ns,sizeof(v::LOG_Q20)+sizeof(v::EXP_NEG_Q31));
}
}
int main() { legacy_exact();mappings_curves();exact_timing_retrigger();latching_and_live();bounds_blocks_reset();cost();std::printf("vca dsp: 6/6 passed (%llu assertions)\n",(unsigned long long)assertions); }
