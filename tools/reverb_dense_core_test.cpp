// Focused REV proof; no UI, device, audio listening or unrelated suite.
#include "engine/engine.h"
#include "engine/patch_io.h"
#include "engine/reverb_dsp.h"
#include <algorithm>
#include <array>
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
using namespace seance;
using trackr::fx::Q15_ONE;
using trackr::fx::sat_q15;
namespace rd = seance::reverb_dsp;

// Pinned BEFORE editing units.cpp. The script hashes this body after replacing
// OldReverb::process with ReverbUnit::process, and compares production legacy.
// sha256: 8d809bc4d9200e597a93c0ded8b4913d960a437c96fe3b2e2ac2b5e0115f5bcd
class OldReverb : public Unit {
public:
    enum { P_SIZE = 0, P_DECAY, P_MIX, P_DAMP, P_FREEZE };
    OldReverb() {
        kind = UnitKind::Reverb;
        add_param("SIZE", 18000); add_param("DEC", 20000); add_param("MIX", 9000);
        add_param("DAMP", 9000); add_param("FRZ", 0);
        buf_ = new(std::nothrow) q15[N];
        if (buf_) std::memset(buf_, 0, N * sizeof(q15));
    }
    ~OldReverb() override { delete[] buf_; }
    void reset() override {
        if (buf_) std::memset(buf_, 0, N * sizeof(q15));
        a_ = b_ = c_ = 0;
        damp_a_ = damp_b_ = damp_c_ = 0;
        freeze_mix_ = 0;
        visual_energy_ = visual_damping_ = 0;
    }
    void process(q15* buf, int frames) override;
    q15 energy() const { return visual_energy_; }
    q15 damping() const { return visual_damping_; }
    q15 freeze() const { return freeze_mix_; }
private:
    static constexpr int N = 8192;
    q15* buf_ = nullptr;
    uint32_t a_ = 0, b_ = 0, c_ = 0;
    q31 damp_a_ = 0, damp_b_ = 0, damp_c_ = 0;
    q15 freeze_mix_ = 0;
    q15 visual_energy_ = 0;
    q15 visual_damping_ = 0;
};
void OldReverb::process(q15* buf, int frames) {
    if (!buf_) return;
    uint32_t scale = 16384 + (uint16_t)params[P_SIZE].effective;
    uint32_t la = 733 + (1499 * scale >> 16);
    uint32_t lb = 997 + (1901 * scale >> 16);
    uint32_t lc = 1201 + (2309 * scale >> 16);
    if (la > 2000) la = 2000;
    if (lb > 2400) lb = 2400;
    if (lc > 3000) lc = 3000;
    const q15 decay_fb = (q15)(6000 + ((uint32_t)(uint16_t)params[P_DECAY].effective * 24000 >> 15));
    const q15 mix = params[P_MIX].effective;
    // DAMP maps to a one-pole coefficient from open (~0.5) to dark (~0.03).
    // The state is q15 stored in q31 to keep the update bounded and cheap.
    const q15 damp = params[P_DAMP].effective;
    const int32_t open_coeff = 16384;
    const int32_t dark_coeff = 1024;
    const int32_t damp_coeff = open_coeff -
        (int32_t)(((int64_t)(uint16_t)damp * (open_coeff - dark_coeff)) >> 15);
    visual_damping_ = damp;
    const bool want_freeze = params[P_FREEZE].effective > 16384;
    const q15 freeze_target = want_freeze ? Q15_ONE : 0;
    q31 peak = 0;

    for (int i = 0; i < frames; ++i) {
        // Roughly 2 ms transition at 32 kHz. It suppresses the click from abruptly
        // removing excitation/changing loop gain, while still feeling immediate.
        if (freeze_mix_ != freeze_target) {
            int32_t next = freeze_mix_ + (freeze_target > freeze_mix_ ? 512 : -512);
            if ((freeze_target > freeze_mix_ && next > freeze_target) ||
                (freeze_target < freeze_mix_ && next < freeze_target)) next = freeze_target;
            freeze_mix_ = (q15)next;
        }

        const q15 wa = buf_[a_];
        const q15 wb = buf_[2048 + b_];
        const q15 wc = buf_[4608 + c_];
        const q31 wet_acc = ((q31)wa + wb + wc) / 3;
        const q15 wet = sat_q15(wet_acc);
        const q31 magnitude = wet < 0 ? -(q31)wet : wet;
        if (magnitude > peak) peak = magnitude;

        // Freeze crossfades the damped state toward raw loop memory. At full
        // freeze: no excitation, exact unity feedback, no damping loss.
        damp_a_ += (((q31)wa - damp_a_) * damp_coeff) >> 15;
        damp_b_ += (((q31)wb - damp_b_) * damp_coeff) >> 15;
        damp_c_ += (((q31)wc - damp_c_) * damp_coeff) >> 15;
        q15 fa, fb_line, fc, feedback, excitation;
        if (freeze_mix_ == Q15_ONE) {
            // Q15_ONE is 32767, so an ordinary >>15 multiply still loses one
            // least-significant bit per loop. Full freeze must copy raw memory
            // directly or a supposedly sealed chamber slowly dies.
            fa = wa; fb_line = wb; fc = wc;
            feedback = Q15_ONE;
            excitation = 0;
            buf_[a_] = fa;
            buf_[2048 + b_] = fb_line;
            buf_[4608 + c_] = fc;
        } else {
            fa = sat_q15(damp_a_ + ((((q31)wa - damp_a_) * freeze_mix_) >> 15));
            fb_line = sat_q15(damp_b_ + ((((q31)wb - damp_b_) * freeze_mix_) >> 15));
            fc = sat_q15(damp_c_ + ((((q31)wc - damp_c_) * freeze_mix_) >> 15));
            feedback = (q15)(decay_fb + ((((q31)Q15_ONE - decay_fb) * freeze_mix_) >> 15));
            excitation = (q15)(((q31)buf[i] * (Q15_ONE - freeze_mix_)) >> 15);
            buf_[a_] = sat_q15((q31)excitation + (((q31)fa * feedback) >> 15));
            buf_[2048 + b_] = sat_q15((q31)excitation + (((q31)fb_line * feedback) >> 15));
            buf_[4608 + c_] = sat_q15((q31)excitation + (((q31)fc * feedback) >> 15));
        }
        if (++a_ >= la) a_ = 0;
        if (++b_ >= lb) b_ = 0;
        if (++c_ >= lc) c_ = 0;
        buf[i] = sat_q15((q31)buf[i] + ((((q31)wet - buf[i]) * mix) >> 15));
    }
    // Fast attack / slow release peak for a truthful chamber-energy marker.
    if (peak > visual_energy_) visual_energy_ = (q15)(peak > 32767 ? 32767 : peak);
    else if (visual_energy_ > 0) {
        q15 release = (q15)(visual_energy_ / 64);
        if (release < 1) release = 1;
        visual_energy_ = (q15)(visual_energy_ - release);
    }
}

// Focused cases follow below; legacy code above is a byte-pinned oracle.
namespace {
uint64_t checks = 0, frames_done = 0, heap_calls = 0, modern_allocations = 0;
bool guarded = false, reject_modern = false, reject_legacy = false;
size_t dense_bytes = 0, last_nothrow = 0;
}
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t,size_t);
extern "C" void* __real_realloc(void*,size_t);
extern "C" void __real_free(void*);
extern "C" void* __real__Znwm(size_t);
extern "C" void* __real__Znam(size_t);
extern "C" void* __real__ZnwmRKSt9nothrow_t(size_t,const std::nothrow_t&);
extern "C" void* __real__ZnamRKSt9nothrow_t(size_t,const std::nothrow_t&);
extern "C" void* __wrap_malloc(size_t n){if(guarded)++heap_calls;return __real_malloc(n);}
extern "C" void* __wrap_calloc(size_t n,size_t s){if(guarded)++heap_calls;return __real_calloc(n,s);}
extern "C" void* __wrap_realloc(void* p,size_t n){if(guarded)++heap_calls;return __real_realloc(p,n);}
extern "C" void __wrap_free(void* p){if(guarded&&p)++heap_calls;__real_free(p);}
extern "C" void* __wrap__Znwm(size_t n){if(guarded)++heap_calls;return __real__Znwm(n);}
extern "C" void* __wrap__Znam(size_t n){if(guarded)++heap_calls;return __real__Znam(n);}
extern "C" void* __wrap__ZnwmRKSt9nothrow_t(size_t n,const std::nothrow_t& t){
    if(guarded)++heap_calls;
    last_nothrow=n;
    if(n==dense_bytes){++modern_allocations;if(reject_modern)return nullptr;}
    return __real__ZnwmRKSt9nothrow_t(n,t);
}
extern "C" void* __wrap__ZnamRKSt9nothrow_t(size_t n,const std::nothrow_t& t){
    if(guarded)++heap_calls;
    if(reject_legacy&&n==16384)return nullptr;
    return __real__ZnamRKSt9nothrow_t(n,t);
}
namespace {
#define CHECK(x) do { ++checks; if(!(x)){std::fprintf(stderr,"reverb FAIL %d: %s\n",__LINE__,#x);std::exit(1);} } while(0)
using Pcm=std::vector<q15>;
using R=ReverbUnit;
struct Guard {
    Guard(){CHECK(!guarded);guarded=true;}
    ~Guard(){guarded=false;CHECK(heap_calls==0);}
};
void set(Unit& u,int p,int value){u.params[p].base=q15(value);u.params[p].snap();}
void configure(Unit& u,int size=18000,int decay=20000,int damp=0,int mix=32767){
    set(u,R::P_SIZE,size);set(u,R::P_DECAY,decay);set(u,R::P_DAMP,damp);
    set(u,R::P_MIX,mix);set(u,R::P_FREEZE,0);u.reset();
}
void render(Unit& u,q15* data,int count){Guard g;u.process(data,count);if(count>0&&data)frames_done+=unsigned(count);}
q15 tick(Unit& u,q15 value=0){render(u,&value,1);return value;}
uint32_t random(uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
Pcm input(int count,int amplitude=20000){Pcm out(count);uint32_t rng=912341;for(auto& s:out)s=q15(int32_t(random(rng)%unsigned(2*amplitude+1))-amplitude);return out;}
Pcm silence(Unit& u,int count){Pcm out(count,0);render(u,out.data(),count);return out;}
bool zero(const Pcm& data){return std::all_of(data.begin(),data.end(),[](q15 x){return x==0;});}
uint64_t energy(const Pcm& data,size_t first,size_t last){uint64_t e=0;for(size_t i=first;i<last;++i)e+=uint64_t(int64_t(data[i])*data[i]);return e;}
int peak(const Pcm& data){int p=0;for(q15 s:data)p=std::max(p,std::abs(int(s)));return p;}
void divided(Unit& u,Pcm& data,uint32_t& rng){for(size_t i=0;i<data.size();){const int n=int(std::min(data.size()-i,size_t(1+random(rng)%211)));render(u,data.data()+i,n);i+=unsigned(n);}}
void maps_and_scatter(){
    static_assert(int(UnitKind::Reverb)==12 && R::P_SIZE==0 && R::P_DECAY==1 && R::P_MIX==2 && R::P_DAMP==3 && R::P_FREEZE==4);
    static_assert(rd::STORAGE_SAMPLES*sizeof(int32_t)==33024);
    R unit;dense_bytes=last_nothrow;
    CHECK(unit.modern()&&unit.ready()&&unit.num_params==5&&dense_bytes>=33024&&dense_bytes<=34*1024);
    const int defaults[]={18000,20000,9000,9000,0};
    for(int p=0;p<5;++p)CHECK(unit.params[p].base==defaults[p]&&!unit.params[p].bipolar);
    uint32_t decay=200,previous[4]={443,575,725,911};int32_t alpha=rd::ONE;
    for(int raw=-32768;raw<=32767;++raw){
        const q15 q=q15(raw);const double u=std::max(0,raw)/32767.;
        CHECK(rd::decay_ms(q)==unsigned(200+std::llround(7800*u*u)));
        CHECK(rd::decay_ms(q)>=decay&&rd::decay_ms(q)<=8000);decay=rd::decay_ms(q);
        CHECK(rd::damping_alpha(q)<=alpha&&rd::damping_alpha(q)>=rd::ONE/25);alpha=rd::damping_alpha(q);
        for(int lane=0;lane<4;++lane){const auto n=rd::length_samples(q,lane);CHECK(n>=previous[lane]&&n<rd::CAPACITY[lane]);previous[lane]=n;}
    }
    CHECK(rd::decay_ms(0)==200&&rd::decay_ms(32767)==8000);
    CHECK(rd::damping_alpha(0)==rd::ONE&&rd::amount(0)==0&&rd::amount(32767)==rd::ONE);
    CHECK(rd::length_samples(16384,-1)==rd::length_samples(16384,0));
    CHECK(rd::length_samples(16384,44)==rd::length_samples(16384,3));
    double max_error=0,max_t60_error=0;
    for(int size=0;size<=32767;size+=257)for(int raw=0;raw<=32767;raw+=251)for(int lane=0;lane<4;++lane){
        const uint32_t length=rd::length_samples(q15(size),lane),ms=rd::decay_ms(q15(raw));
        const double reference=std::pow(.001,double(length)/(32.*ms));
        const int32_t mapped=rd::attenuation(length,ms);const double got=double(mapped)/rd::ONE;
        CHECK(mapped>0&&mapped<rd::ONE&&std::fabs(got-reference)<.0002);
        max_error=std::max(max_error,std::fabs(got-reference));
        const double measured=-std::log(1000.)*length/(32.*std::log(got));
        max_t60_error=std::max(max_t60_error,std::fabs(measured/ms-1));
        CHECK(std::fabs(measured/ms-1)<.015);
    }
    uint32_t rng=11937;
    for(int i=0;i<20000;++i){
        int32_t x[4],y[4],neg[4],yn[4];uint64_t before=0,after=0;
        for(int k=0;k<4;++k){x[k]=int32_t(random(rng)%(2u*rd::LIMIT+1u))-rd::LIMIT;neg[k]=-x[k];before+=uint64_t(int64_t(x[k])*x[k]);}
        rd::scatter(x,y);rd::scatter(neg,yn);
        const int signs[4][4]={{1,1,1,1},{1,-1,1,-1},{1,1,-1,-1},{1,-1,-1,1}};
        for(int k=0;k<4;++k){int64_t scalar=0;for(int p=0;p<4;++p)scalar+=int64_t(x[p])*signs[k][p];CHECK(y[k]==scalar/2&&yn[k]==-y[k]);after+=uint64_t(int64_t(y[k])*y[k]);}
        CHECK(after<=before);
    }
    std::printf("ok laws: 65536 raw values, four delays, attenuation maxerr=%.8f/t60=%.4f%%; 20000 contractive/odd scatter vectors; state=%zuB, unit=%zuB\n",max_error,100*max_t60_error,dense_bytes,sizeof(R));
}
void legacy_exact(){
    R actual(false);OldReverb reference;uint32_t rng=123871;uint64_t hash=1469598103934665603ull;unsigned samples=0;
    CHECK(actual.ready()&&!actual.modern()&&actual.num_params==5);
    const int settings[][5]={{18000,20000,9000,9000,0},{0,0,32767,0,0},{32767,32767,32767,32767,0},
        {17911,23179,32767,14141,32767},{900,32767,0,8192,32767},{31111,1,19391,0,0},
        {0,32767,32767,32767,32767},{32767,32767,32767,0,0}};
    for(int phase=0;phase<40;++phase){
        const auto& p=settings[phase%8];for(int k=0;k<5;++k){set(actual,k,p[k]);set(reference,k,p[k]);}
        if(phase==19){actual.reset();reference.reset();}
        for(int block=0;block<24;++block){
            const int n=1+int(random(rng)%1024);Pcm a(n),b;
            for(q15& s:a)s=phase%3? q15(random(rng)>>16):0;
            b=a;
            render(actual,a.data(),n);render(reference,b.data(),n);
            CHECK(a==b&&actual.visual_energy()==reference.energy()&&actual.visual_damping()==reference.damping()&&actual.visual_freeze()==reference.freeze());
            for(q15 s:a){hash^=uint16_t(s);hash*=1099511628211ull;}samples+=unsigned(n);
        }
    }
    std::printf("ok pinned old PCM+meters exact: %u frames, valid automation/reset/freeze, fnv64=%016llx\n",samples,(unsigned long long)hash);
}
void endpoints_and_silence(){
    R dry,wet,mixed;configure(dry,0,32767,0,0);configure(wet,0,32767,0);configure(mixed,0,32767,0,0);
    auto feed=input(12000,32767),a=feed,b=feed,c=feed;
    render(dry,a.data(),int(a.size()));render(wet,b.data(),int(b.size()));render(mixed,c.data(),int(c.size()));
    CHECK(a==feed&&c==feed);
    for(unsigned i=0;i<rd::length_samples(0,0);++i)CHECK(b[i]==0); // no dry leakage at full wet
    CHECK(peak(b)>0);
    for(int direction=0;direction<2;++direction){
        set(mixed,R::P_MIX,direction?0:32767);
        for(int i=1;i<=64;++i){const q15 in=q15((i*1879)%60001-30000);const q15 d=tick(dry,in),w=tick(wet,in),m=tick(mixed,in);
            const double t=direction?1-i/64.:i/64.;CHECK(d==in&&std::fabs(m-(d+(w-d)*t))<=2);
            if(i==64)CHECK(m==(direction?d:w));
        }
    }
    set(wet,R::P_FREEZE,32767);tick(wet);const auto before_freeze=wet.visual_freeze(),before_energy=wet.visual_energy();
    q15 untouched=12713;render(wet,nullptr,512);render(wet,&untouched,0);render(wet,&untouched,-1);
    CHECK(untouched==12713&&wet.visual_freeze()==before_freeze&&wet.visual_energy()==before_energy);
    for(bool modern:{false,true}){R u(modern);configure(u);auto noise=input(12345);render(u,noise.data(),int(noise.size()));u.reset();CHECK(u.visual_energy()==0&&u.visual_freeze()==0);CHECK(zero(silence(u,32000)));}
    R empty;configure(empty,32767,32767,0);set(empty,R::P_FREEZE,32767);CHECK(zero(silence(empty,64)));CHECK(empty.visual_freeze()==32767);
    auto unwanted=input(32000,32767);render(empty,unwanted.data(),int(unwanted.size()));CHECK(zero(unwanted));
    set(empty,R::P_FREEZE,0);CHECK(zero(silence(empty,32000)));
    std::puts("ok exact dry/wet endpoints, finite64 mix automation, null/zero/negative no-op, reset silence and no delayed held-input leakage");
}
double measured_t60(const Pcm& pcm){
    std::vector<double> integral(pcm.size()+1);for(size_t i=pcm.size();i--;)integral[i]=integral[i+1]+double(pcm[i])*pcm[i];
    CHECK(integral[0]>0);size_t a=0,b=0;
    while(a<pcm.size()&&integral[a]>integral[0]*std::pow(10.,-.5))++a;
    while(b<pcm.size()&&integral[b]>integral[0]*std::pow(10.,-3.5))++b;
    CHECK(b>a&&b<pcm.size());return 2.*double(b-a)/32000.;
}
void density_decay_damping(){
    R modern,legacy(false);configure(modern,18000,22000);configure(legacy,18000,22000);
    Pcm a(32000),b(32000);a[0]=b[0]=28000;render(modern,a.data(),int(a.size()));render(legacy,b.data(),int(b.size()));
    unsigned dense=0,sparse=0;for(int i=8000;i<24000;++i){dense+=std::abs(int(a[i]))>=2;sparse+=std::abs(int(b[i]))>=2;}
    CHECK(dense>9000&&dense>3*sparse);
    std::printf("ok impulse occupancy (|pcm|>=2,250..750ms): modern=%u/16000, legacy=%u/16000; not a listening claim\n",dense,sparse);
    double shortest=10,longest=0;
    for(int size:{0,16384,32767}){
        R u;configure(u,size,12000);Pcm response(96000);response[0]=30000;render(u,response.data(),int(response.size()));
        const double measured=measured_t60(response),nominal=rd::decay_ms(12000)/1000.;
        CHECK(measured>nominal*.75&&measured<nominal*1.4);shortest=std::min(shortest,measured);longest=std::max(longest,measured);
        std::printf("ok open-return energy-integral t60 size=%d measured=%.4fs nominal=%.4fs\n",size,measured,nominal);
    }
    CHECK(longest<shortest*1.4);
    uint64_t tail[2]={};
    for(int mode=0;mode<2;++mode){R u;configure(u,18000,mode?26000:6800);auto data=input(32000,0);auto burst=input(128,24000);std::copy(burst.begin(),burst.end(),data.begin());render(u,data.data(),int(data.size()));tail[mode]=energy(data,16000,32000);}
    CHECK(tail[1]>10*tail[0]&&tail[1]>0);
    R open,dark;configure(open,0,25000,0);configure(dark,0,25000,32767);
    Pcm bright(24000),damped;for(int i=0;i<1000;++i)bright[i]=q15(i%2?24000:-24000);damped=bright;
    render(open,bright.data(),int(bright.size()));render(dark,damped.data(),int(damped.size()));
    for(unsigned i=0;i<2*rd::length_samples(0,0);++i)CHECK(bright[i]==damped[i]);
    const auto high=energy(bright,4000,24000),low=energy(damped,4000,24000);
    CHECK(high>8*low&&high>0);CHECK(open.visual_damping()==0&&dark.visual_damping()==32767);
    std::printf("ok longer nominal decay has greater late energy=%llu/%llu; feedback-only dark/open late energy=%llu/%llu\n",(unsigned long long)tail[1],(unsigned long long)tail[0],(unsigned long long)low,(unsigned long long)high);
}
void freeze_and_size_capture(){
    for(int size:{0,32767}){
        R a,b;configure(a,size,32767);configure(b,size,32767);auto prime=input(24000,11000),copy=prime;
        render(a,prime.data(),int(prime.size()));render(b,copy.data(),int(copy.size()));CHECK(prime==copy&&peak(prime)>0);
        set(a,R::P_FREEZE,32767);set(b,R::P_FREEZE,32767);
        for(int n=1;n<=64;++n){CHECK(tick(a)==tick(b));CHECK(a.visual_freeze()==q15((int64_t(n)*32767)/64));}
        CHECK(a.visual_freeze()==32767&&!a.visual_transition());
        uint64_t first=0,last=0;uint32_t rng=777;
        std::array<q15,512> x{},y{};
        for(int block=0;block<3750;++block){ // 60 seconds of full freeze per size
            if(block%29==0){set(a,R::P_SIZE,block%2?0:32767);set(a,R::P_DAMP,block%2?32767:-32768);set(a,R::P_DECAY,block%2?0:32767);}
            for(auto& value:x)value=q15(random(rng)>>16);
            y.fill(0);
            render(a,x.data(),512);render(b,y.data(),512);CHECK(x==y); // no excitation AND no hidden send-state ingestion
            CHECK(a.visual_size()==size&&a.visual_damping()==0&&a.visual_freeze()==32767&&!a.visual_transition());
            for(q15 value:x){const auto e=uint64_t(int64_t(value)*value);if(block<313)first+=e;if(block>=3437)last+=e;}
        }
        CHECK(first>1000000&&last>first*.8&&last<first*1.2);
        std::printf("ok freeze size=%d:60s, exact no-input/held-input equivalence; last/first5s energy=%.6f (finite precision, not bit-lossless)\n",size,double(last)/first);
        for(int p:{R::P_SIZE,R::P_DECAY,R::P_DAMP})set(b,p,a.params[p].effective);
        set(a,R::P_FREEZE,0);set(b,R::P_FREEZE,0);
        for(int n=1;n<=64;++n)CHECK(tick(a)==tick(b));
        CHECK(a.visual_freeze()==0);auto release_a=silence(a,32000),release_b=silence(b,32000);CHECK(release_a==release_b);
    }
    R u;configure(u,0,32767);auto prime=input(12000,5000);render(u,prime.data(),int(prime.size()));
    set(u,R::P_SIZE,32767);tick(u);CHECK(u.visual_transition()&&u.visual_size()>0&&u.visual_size()<32767);
    for(int n=1;n<200;++n)tick(u);
    const q15 midpoint=u.visual_size();CHECK(midpoint>9000&&midpoint<12000);
    // Request a newer size and freeze while the first target is still in flight.
    set(u,R::P_SIZE,8000);set(u,R::P_FREEZE,32767);set(u,R::P_DAMP,32767);
    for(int n=0;n<440;++n){tick(u);CHECK(u.visual_freeze()==0);} // one committed crossfade finishes
    CHECK(!u.visual_transition()&&u.visual_size()==32767);
    for(int n=1;n<=64;++n){tick(u);CHECK(u.visual_freeze()==q15((int64_t(n)*32767)/64));}
    CHECK(u.visual_damping()==0&&u.visual_size()==32767);
    set(u,R::P_MIX,0);for(int i=0;i<64;++i)tick(u,21000);for(int i=0;i<128;++i)CHECK(tick(u,-21000)==-21000);
    CHECK(u.visual_freeze()==32767); // MIX remains live without opening the tank
    set(u,R::P_FREEZE,0);silence(u,64);CHECK(u.visual_freeze()==0);
    tick(u);CHECK(u.visual_transition());silence(u,639);CHECK(!u.visual_transition()&&u.visual_size()==8000&&u.visual_damping()==32767);
    u.reset();CHECK(!u.visual_transition()&&u.visual_freeze()==0&&u.visual_energy()==0);CHECK(zero(silence(u,32000)));
    std::puts("ok in-flight size freeze capture: gates now/commits only active target/then64 unity; live mix, coalesced release and reset");
}
void partition_stress_and_dc(){
    R a,b;configure(a,0,32767);configure(b,0,32767);uint32_t rng=3911;int largest=0;
    for(int phase=0;phase<210;++phase){
        for(int p=0;p<5;++p){int raw=int(random(rng)%32768);if(phase%7==0)raw=-32768;if(phase%11==0)raw=32767;
            if(p==R::P_FREEZE)raw=phase%17>11?32767:0;
            set(a,p,raw);set(b,p,raw);
        }
        const int count=51+int(random(rng)%1277);auto x=input(count,phase<80?14000:0),y=x;
        render(a,x.data(),count);divided(b,y,rng);CHECK(x==y);
        CHECK(a.visual_freeze()==b.visual_freeze()&&a.visual_damping()==b.visual_damping()&&a.visual_energy()==b.visual_energy());
        CHECK(a.visual_size()==b.visual_size()&&a.visual_transition()==b.visual_transition());largest=std::max(largest,peak(x));
    }
    // Modulating size with no continuing excitation must not create large bursts.
    R moving;configure(moving,0,32767);auto seed=input(16000,3500);render(moving,seed.data(),int(seed.size()));int delta=0,last=seed.back(),moving_peak=0;
    for(int block=0;block<6000;++block){set(moving,R::P_SIZE,block%2?0:32767);auto data=silence(moving,64);for(q15 s:data){moving_peak=std::max(moving_peak,std::abs(int(s)));delta=std::max(delta,std::abs(int(s)-last));last=s;}}
    CHECK(moving_peak<12000&&delta<18000);
    for(int sign:{-1,1})for(int damp:{0,32767}){
        R u;configure(u,32767,32767,damp);Pcm constant(320000,q15(sign>0?32767:-32768));render(u,constant.data(),int(constant.size()));
        int64_t sum=0;for(int i=288000;i<320000;++i)sum+=constant[i];CHECK(std::abs(double(sum)/32000)<2.);
        auto tail=silence(u,384000);CHECK(peak(tail)<32768);CHECK(energy(tail,352000,384000)==0);
        u.reset();CHECK(zero(silence(u,6400)));
    }
    R floor;configure(floor,0,32767,32767);Pcm dust(6400,1);render(floor,dust.data(),int(dust.size()));auto tail=silence(floor,640000);CHECK(energy(tail,608000,640000)==0);
    std::printf("ok deterministic block subdivision under/extreme negative automation; size tail peak=%d jump=%d, general peak=%d; full-scale signed DC/high decay and no audible integer floor\n",moving_peak,delta,largest);
}
void allocation_and_clone(){
    for(bool modern:{false,true}){
        R a(modern),b(modern);configure(a,23111,27913,11779);configure(b,23111,27913,11779);
        auto prime=input(21000),copy=prime;render(a,prime.data(),int(prime.size()));render(b,copy.data(),int(copy.size()));
        const q15 before=a.visual_energy();reject_modern=!modern;reject_legacy=modern;
        CHECK(!a.set_modern(!modern));reject_modern=reject_legacy=false;
        CHECK(a.modern()==modern&&a.ready()&&a.num_params==5&&a.visual_energy()==before);
        CHECK(silence(a,8192)==silence(b,8192));
        for(int p=0;p<5;++p){a.params[p].src=ModSource::SigilB;a.params[p].mod_depth=q15(1211*p-2000);a.params[p].mod_enabled=p%2;}
        std::unique_ptr<Unit> clone(clone_unit(&a));CHECK(clone&&static_cast<R*>(clone.get())->modern()==modern&&clone->num_params==5);
        for(int p=0;p<5;++p){CHECK(clone->params[p].base==a.params[p].base&&clone->params[p].src==a.params[p].src&&clone->params[p].mod_depth==a.params[p].mod_depth&&clone->params[p].mod_enabled==a.params[p].mod_enabled);}
        CHECK(zero(silence(*clone,32000))); // distinct clean tail, not borrowed memory
        CHECK(a.set_modern(!modern)&&a.modern()!=modern&&a.ready()&&a.visual_energy()==0);CHECK(zero(silence(a,32000)));
    }
    reject_modern=true;R failed;CHECK(!failed.ready());q15 value=17839;render(failed,&value,1);CHECK(value==17839);
    CHECK(make_unit(UnitKind::Reverb)==nullptr);reject_modern=false;
    CHECK(failed.set_modern(true)&&failed.ready());
    R old(false);const auto allocations=modern_allocations;reject_modern=true;
    std::unique_ptr<Unit> old_copy(clone_unit(&old));CHECK(old_copy&&!static_cast<R*>(old_copy.get())->modern()&&modern_allocations==allocations);
    reject_modern=false;
    std::puts("ok exact live-tail preservation on both allocation failures, failed factory passthrough/retry, fresh-tail clone + legacy-only allocation");
}
using Bytes=std::vector<uint8_t>;
std::string path;
Bytes read_file(){std::ifstream file(path,std::ios::binary);CHECK(file.good());return {std::istreambuf_iterator<char>(file),{}};}
void write_file(const Bytes& bytes){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));CHECK(file.good());}
uint32_t r32(const Bytes& bytes,size_t at){CHECK(at+4<=bytes.size());return bytes[at]|uint32_t(bytes[at+1])<<8|uint32_t(bytes[at+2])<<16|uint32_t(bytes[at+3])<<24;}
void w16(Bytes& bytes,size_t at,int value){CHECK(at+2<=bytes.size());bytes[at]=uint8_t(value);bytes[at+1]=uint8_t(unsigned(value)>>8);}
size_t chunk(const Bytes& bytes,const char* id){for(size_t p=5;p+8<=bytes.size();p+=8+r32(bytes,p+4))if(!std::memcmp(bytes.data()+p,id,4))return p;CHECK(false);return 0;}
Pcm pcm(Unit& u){for(int p=0;p<u.num_params;++p)u.params[p].snap();u.reset();auto data=input(42000,16000);std::fill(data.begin()+12000,data.end(),0);render(u,data.data(),177);render(u,data.data()+177,int(data.size())-177);return data;}
void persistence(){
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();const int values[]={23179,27671,25319,7183,0};
    for(bool modern:{false,true}){
        auto* u=new R(modern);delete a->replace_unit(0,0,u);
        for(int p=0;p<5;++p){set(*u,p,values[p]);u->params[p].src=ModSource::SigilC;u->params[p].mod_depth=q15(-3201+p*1413);u->params[p].mod_enabled=p%2!=0;}
        a->capture_scene(0,0);for(int p=0;p<5;++p)set(*u,p,1000+p*2711);a->capture_scene(0,1);
        for(int p=0;p<5;++p){set(*u,p,values[p]);CHECK(a->set_step_lock(0,p,0,p,q15(19731+p)));}
        auto& pad=a->macro_pads[2];std::snprintf(pad.name,sizeof pad.name,"rev");pad.route_count=5;
        for(int p=0;p<5;++p)pad.routes[p]={0,0,uint8_t(p),q15(231*p),q15(31900-p*17)};
        CHECK(patchio::save(*a,path.c_str()));const auto before=read_file();CHECK(before[chunk(before,"UNIT")+8]==unsigned(modern));
        CHECK(patchio::load(*b,path.c_str()));auto* got=static_cast<R*>(b->chains[0].units[0]);
        CHECK(got&&got->kind==UnitKind::Reverb&&got->modern()==modern&&got->num_params==5);
        for(int p=0;p<5;++p){
            const auto& x=got->params[p];const auto& y=u->params[p];
            CHECK(x.base==y.base&&x.src==y.src&&x.mod_depth==y.mod_depth&&x.mod_enabled==y.mod_enabled);
            CHECK(b->pattern.steps[0][p].lock_count==1&&b->pattern.steps[0][p].locks[0].param==p&&b->pattern.steps[0][p].locks[0].value==19731+p);
            for(int which=0;which<2;++which)CHECK(b->scenes[0][which].base[0][p]==a->scenes[0][which].base[0][p]);
            CHECK(b->macro_pads[2].route_count==5&&b->macro_pads[2].routes[p].param==p&&b->macro_pads[2].routes[p].minimum==231*p);
        }
        CHECK(pcm(*got)==pcm(*u));CHECK(patchio::save(*b,path.c_str()));CHECK(read_file()==before);
        {
            Guard guard;EngineUiSnapshot snapshot{};b->capture_ui_snapshot(snapshot,0,0);
            CHECK(snapshot.reverb_focus_valid&&snapshot.reverb_energy==got->visual_energy()&&snapshot.reverb_freeze==got->visual_freeze());
            for(int p=0;p<5;++p)CHECK(snapshot.focus_effective[p]==got->params[p].effective);
            CHECK(b->remove_unit_with_undo(0,0)&&b->structural_undo_available());
            CHECK(b->undo_structural_edit()&&b->chains[0].units[0]==got&&got->modern()==modern);
        }
        // Fresh clone (clipboard path) preserves mode/params, not the live tail.
        std::unique_ptr<Unit> clone(clone_unit(got));CHECK(clone&&static_cast<R*>(clone.get())->modern()==modern);CHECK(zero(silence(*clone,6000)));
        auto reordered=before;const size_t pos=chunk(reordered,"UNIT"),end=pos+8+r32(reordered,pos+4);
        const Bytes flags(reordered.begin()+pos,reordered.begin()+end);reordered.erase(reordered.begin()+pos,reordered.begin()+end);reordered.insert(reordered.begin()+5,flags.begin(),flags.end());
        write_file(reordered);CHECK(patchio::load(*b,path.c_str()));got=static_cast<R*>(b->chains[0].units[0]);CHECK(got->modern()==modern&&pcm(*got)==pcm(*u));
    }
    std::puts("ok actual SNC7:modern/legacy flags, raw values/routes/mute/scenes/locks/macro indices, PCM+byte-stable resave, UNIT-before-CORE, snapshot and owned-instance undo");
}
void historical_and_failures(){
    auto target=std::make_unique<Engine>();constexpr int old_sigils=4;const int values[]={29173,27137,24689,13071,0};
    for(int version=1;version<=6;++version){
        Bytes old={'S','N','C',uint8_t('0'+version),uint8_t(version)};
        for(int chain=0;chain<NUM_CHAINS;++chain){
            old.insert(old.end(),{255,127});if(version>=2)old.insert(old.end(),{0,0});old.push_back(0);
            const size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);
            if(chain==0){old[first]=uint8_t(UnitKind::Reverb);for(int p=0;p<5;++p)w16(old,first+3+p*5,values[p]);}
        }
        old.insert(old.end(),old_sigils*(1+SIGIL_POINTS*2+4)+old_sigils*3,0);
        if(version>=3){old.insert(old.end(),{120,0});old.insert(old.end(),old_sigils*3,0);}
        if(version>=4){const size_t flags=old.size();old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);old[flags]=1;} // old bit MUST NOT opt in
        if(version>=5){old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0});}
        if(version>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
        write_file(old);const auto allocations=modern_allocations;reject_modern=true;CHECK(patchio::load(*target,path.c_str()));reject_modern=false;
        CHECK(modern_allocations==allocations);auto* got=static_cast<R*>(target->chains[0].units[0]);CHECK(!got->modern()&&got->num_params==5);
        OldReverb reference;for(int p=0;p<5;++p){CHECK(got->params[p].base==values[p]);set(reference,p,values[p]);}CHECK(pcm(*got)==pcm(reference));
        CHECK(patchio::save(*target,path.c_str()));const auto saved=read_file();CHECK(saved[chunk(saved,"UNIT")+8]==0);
    }
    auto source=std::make_unique<Engine>();delete source->replace_unit(0,0,new R);CHECK(patchio::save(*source,path.c_str()));const auto valid=read_file();
    const size_t flags=chunk(valid,"UNIT"),core=chunk(valid,"CORE");
    auto zero_flag=valid;zero_flag[flags+8]=0;write_file(zero_flag);const auto allocations=modern_allocations;reject_modern=true;
    CHECK(patchio::load(*target,path.c_str()));reject_modern=false;CHECK(modern_allocations==allocations);
    auto* old=static_cast<R*>(target->chains[0].units[0]);CHECK(!old->modern());OldReverb reference;CHECK(pcm(*old)==pcm(reference));
    delete target->replace_unit(1,2,new R(false));Unit* sentinel=target->chains[1].units[2];sentinel->params[3].base=19171;target->set_bpm(173);
    std::vector<Bytes> bad;
    for(size_t cut:{size_t(0),size_t(4),core+8+71,flags+8+2})bad.emplace_back(valid.begin(),valid.begin()+cut);
    for(int value:{2,3,128,255}){auto changed=valid;changed[flags+8]=uint8_t(value);bad.push_back(changed);}
    auto changed=valid;changed.erase(changed.begin()+flags,changed.begin()+flags+8+r32(valid,flags+4));bad.push_back(changed); // required whole chunk
    changed=valid;changed.insert(changed.end(),valid.begin()+flags,valid.begin()+flags+8+r32(valid,flags+4));bad.push_back(changed);
    changed=valid;changed[flags+4]=1;changed[flags+5]=changed[flags+6]=changed[flags+7]=0;bad.push_back(changed);
    changed=valid;changed[core+8+5]=255;bad.push_back(changed);
    for(const auto& bytes:bad){write_file(bytes);CHECK(!patchio::load(*target,path.c_str()));CHECK(target->chains[1].units[2]==sentinel&&target->bpm==173&&sentinel->params[3].base==19171);}
    write_file(valid);reject_modern=true;CHECK(!patchio::load(*target,path.c_str()));reject_modern=false;
    CHECK(target->chains[1].units[2]==sentinel&&target->bpm==173&&sentinel->params[3].base==19171);
    std::puts("ok SNC1..6 with historical bit0 and zero-flag SNC7 exact legacy/no modern allocations; malformed/truncated/missing/duplicate UNIT, unknown REV bits and allocation failure transactionally isolated");
}
} // namespace
int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    path="/tmp/seance-reverb-core-"+std::to_string(long(getpid()))+".snc";
    maps_and_scatter();legacy_exact();endpoints_and_silence();density_decay_damping();
    freeze_and_size_capture();partition_stress_and_dc();allocation_and_clone();persistence();historical_and_failures();
    CHECK(std::remove(path.c_str())==0&&heap_calls==0);
    std::printf("reverb core:9/9 groups; %llu checks, %llu processed frames, %llu guarded heap operations; state=%zuB\n",(unsigned long long)checks,(unsigned long long)frames_done,(unsigned long long)heap_calls,dense_bytes);
}
