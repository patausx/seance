// Focused modern/legacy Delay DSP. Frozen independent old arithmetic; no UI.
#include "engine/units.h"
#include "engine/delay_time.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <vector>
using seance::DelayUnit; using seance::q15;
namespace dt = seance::delay_time;
static size_t array_calls=0, last_array_bytes=0;
static bool reject_modern=false;
void* operator new[](size_t n) {++array_calls;last_array_bytes=n;if(reject_modern&&n==256004)throw std::bad_alloc();if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {std::free(p);}
namespace {
uint64_t checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
void set(DelayUnit& u,int p,int raw){u.params[p].base=(q15)raw;u.params[p].snap();}
q15 tick(DelayUnit& u,q15 input=0){u.process(&input,1);return input;}
uint32_t random(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
void setup(DelayUnit& u,uint32_t time=32,int feedback=20000,int tone=32767){
    set(u,0,dt::free_value(time));set(u,1,feedback);set(u,2,32767);set(u,3,0);set(u,4,0);set(u,5,0);set(u,6,tone);set(u,7,0);set(u,8,dt::division_value(3));set(u,9,dt::feel_value(0));set(u,10,0);u.control_tick();u.reset();
}
// Frozen before-modern Delay source re-expressed outside production helpers.
// Signed division/shift rounding intentionally matches the original q15 engine.
struct OldDelay {
    std::vector<q15> ring=std::vector<q15>(32000,0);uint32_t write=0,phase=0;int32_t current=8000<<16;int32_t lp=0;
    static q15 clip(int64_t value){
        if(value>=-24000&&value<=24000)return (q15)value;
        const bool negative=value<0;const int64_t over=std::min<int64_t>((negative?-value:value)-24000,8767*4);
        const int64_t r=over*32767/(8767*4), r2=r*r>>15, r3=r2*r>>15;
        const int32_t result=24000+(int32_t)((r-r3/3)*8767>>15);
        return (q15)(negative?-result:result);
    }
    q15 sample(q15 input,const int* p){
        const int32_t target=(32+(uint32_t)(((uint64_t)(uint16_t)p[0]*31967)>>15))<<16;
        current+=(target-current)>>7;phase+=977u+((uint32_t)(uint16_t)p[3]>>5);
        const int32_t tri=(phase&0x80000000u)?32767-(int32_t)((phase>>15)&65535):(int32_t)((phase>>15)&65535)-32768;
        const int32_t flutter=(int32_t)((phase*17u)>>24)-128;
        const int32_t wobble=(int32_t)(((int64_t)(tri+flutter*32)*p[3]*(64<<16))>>30);
        const int32_t delay=current+wobble;const int age=std::clamp(delay>>16,1,31998);
        const uint32_t a=(write+32000-age)%32000,b=(a+31999)%32000;
        const int wet=ring[a]+(int32_t)(((int64_t)(ring[b]-ring[a])*(uint16_t)delay)>>16);
        lp+=(wet-lp)/4;const bool hold=p[5]>16384;
        const int feedback=lp*(hold?32600:p[1])>>15;
        const int into=hold?feedback:input+feedback;
        ring[write]=clip((int64_t)into*(32767+p[4]*3)>>15);write=(write+1)%32000;
        return (q15)std::clamp(input+((wet-input)*p[2]>>15),-32768,32767);
    }
};
void legacy_exact(){
    DelayUnit u(false);CHECK(!u.modern()&&u.num_params==6&&last_array_bytes==64000);OldDelay oracle;
    uint32_t rng=1937;uint64_t hash=1469598103934665603ull;
    const int changes[][6]={{8192,23000,21000,23000,12000,0},{0,32767,32767,32767,32767,0},{32000,16000,28000,8192,8192,32767},{32767,31000,19000,32767,0,0},{2000,29000,32767,0,13000,32767},{9137,12345,16384,16000,30000,0}};
    for(const auto& p:changes){for(int k=0;k<6;++k)set(u,k,p[k]);u.control_tick();for(int i=0;i<12000;++i){q15 input=(q15)random(rng);q15 got=tick(u,input);CHECK(got==oracle.sample(input,p));hash^=(uint16_t)got;hash*=1099511628211ull;}}
    CHECK(hash==0x310f7ae0e4225258ull);
    std::printf("ok frozen independent legacy PCM 72000 samples fnv64=%016llx\n",(unsigned long long)hash);
}
void mappings_memory(){
    static_assert(DelayUnit::P_TIME==0&&DelayUnit::P_HOLD==5&&DelayUnit::P_TONE==6&&DelayUnit::P_MODE==10&&DelayUnit::P_COUNT==11);
    static_assert(sizeof(DelayUnit::VisualState)<=64&&std::is_trivially_copyable<DelayUnit::VisualState>::value);
    DelayUnit modern;CHECK(modern.modern()&&modern.num_params==11&&last_array_bytes==256004);
    CHECK(dt::free_frames(-32768)==32&&dt::free_frames(0)==32&&dt::free_frames(32767)==128000);
    uint32_t previous=0;
    for(int raw=0;raw<=32767;raw+=37){uint32_t f=dt::free_frames((q15)raw);CHECK(f>=previous);previous=f;CHECK(std::abs((int)dt::free_frames(dt::free_value(f))-(int)f)<=17);}
    CHECK(dt::tone_hz(0)==200&&dt::tone_hz(32767)==12000);
    for(int div=0;div<8;++div)for(int feel=0;feel<3;++feel){
        const q15 d=dt::division_value(div),f=dt::feel_value(feel);CHECK(dt::division(d)==div&&dt::feel(f)==feel);
        bool limited=false;const uint32_t got=dt::sync_frames(d,f,120<<16,&limited);
        const double expected=1000.*(1<<div)*(feel==1?1.5:feel==2?2./3:1.);
        CHECK(got==std::min(128000u,(uint32_t)(expected+0.5)));CHECK(limited==(expected>128000));
    }
    bool limited=false;CHECK(dt::sync_frames(32767,dt::feel_value(1),20<<16,&limited)==128000&&limited);
    CHECK(dt::sync_frames(0,dt::feel_value(2),2147483647,&limited)==32&&limited);
    setup(modern);set(modern,7,32767);set(modern,8,dt::division_value(4));set(modern,9,dt::feel_value(2));modern.control_tick();modern.set_clock_bpm_q16((137<<16)+32768);
    CHECK(modern.visual_state().target_frames==dt::sync_frames(modern.params[8].target,modern.params[9].target,(137<<16)+32768));
    CHECK(modern.params[0].base==0);set(modern,7,0);modern.control_tick();CHECK(modern.visual_state().target_frames==32);
    DelayUnit old(false);const int raw=old.params[0].base;reject_modern=true;CHECK(!old.set_modern(true));reject_modern=false;
    CHECK(old.ready()&&!old.modern()&&old.num_params==6&&old.params[0].base==raw);
    CHECK(old.set_modern(true)&&old.num_params==11);CHECK(old.set_modern(false)&&old.num_params==6&&last_array_bytes==64000);
    std::puts("ok logarithmic endpoints/inverse, 8x3 notes and Q16 BPM/clamps, transactional64kb/256004b allocation");
}
void four_seconds_tone(){
    DelayUnit u;setup(u,128000,0);std::vector<q15> audio(128003,0);audio[0]=12000;
    const size_t allocations=array_calls;u.process(audio.data(),(int)audio.size());CHECK(array_calls==allocations);
    for(int i=0;i<128000;++i) { CHECK(audio[i]==0); }
    CHECK(audio[128000]==11999);CHECK(audio[128001]==0);
    CHECK(u.visual_state().read_a_q8==(128000u<<8)&&u.visual_delay_frames()==65535);
    uint64_t energy[2]={};
    for(int tone=0;tone<2;++tone){DelayUnit d;setup(d,32,30000,tone?32767:0);q15 input[640]={};for(int i=0;i<32;++i)input[i]=(i&1)?16000:-16000;d.process(input,640);for(int i=64;i<640;++i)energy[tone]+=(int64_t)input[i]*input[i];}
    CHECK(energy[1]>energy[0]*20);
    std::printf("ok exact4s impulse, safe ring endpoint/snapshot saturation; feedback LP energy200/12000hz=%llu/%llu\n",(unsigned long long)energy[0],(unsigned long long)energy[1]);
}
void hold_clean(){
    DelayUnit u;setup(u);tick(u);set(u,5,32767);int previous=0;
    for(int i=1;i<=320;++i){tick(u);const int h=u.visual_state().hold_amount;CHECK(h==32767*i/320&&h>=previous&&h-previous<=103);previous=h;}
    set(u,5,0);for(int i=1;i<=320;++i){tick(u);CHECK(u.visual_state().hold_amount==32767-32767*i/320);}CHECK(!u.visual_hold());
    set(u,10,32767);set(u,0,dt::free_value(8000));u.control_tick();tick(u);auto state=u.visual_state();
    CHECK(state.crossfading&&state.read_a_q8==(32u<<8));const auto destination=state.read_b_q8;
    set(u,0,dt::free_value(16000));u.control_tick();for(int i=1;i<640;++i){tick(u);if(i<639)CHECK(u.visual_state().read_b_q8==destination);}
    CHECK(!u.visual_state().crossfading&&u.visual_state().read_a_q8==destination);
    tick(u);CHECK(u.visual_state().crossfading&&u.visual_state().read_b_q8==(dt::free_frames(dt::free_value(16000))<<8));
    set(u,10,0);u.control_tick();for(int i=1;i<640;++i)tick(u);CHECK(!u.visual_state().crossfading);const auto arrived=u.visual_state().read_a_q8;
    set(u,0,0);u.control_tick();tick(u);CHECK(u.visual_state().read_a_q8<arrived&&u.visual_state().read_a_q8>arrived-100000);
    std::puts("ok finite320-sample hold enter/exit, clean640 fixed heads/latching/coalescing and safe mode handoff");
}
void clear_blocks(){
    DelayUnit a,b;setup(a,1000);setup(b,1000);uint32_t rng=771;
    for(int block=0;block<60;++block){const int n=300+(random(rng)%1300);std::vector<q15> x(n),y;
        for(auto& s:x) { s=(q15)random(rng); }
        y=x;
        for(DelayUnit* u:{&a,&b}){set(*u,0,(int)(random(rng)%32768));}
        b.params[0]=a.params[0];
        for(DelayUnit* u:{&a,&b}){set(*u,10,block%3?32767:0);set(*u,3,block%2?32767:0);set(*u,5,block%4?0:32767);u->control_tick();if(block%11==0)u->request_clear();}
        const size_t allocations=array_calls;a.process(x.data(),n);for(int i=0;i<n;){int count=std::min(n-i,1+(int)(random(rng)%97));b.process(y.data()+i,count);i+=count;}
        CHECK(array_calls==allocations&&x==y);CHECK(a.visual_state().read_a_q8>=256&&a.visual_state().read_a_q8<=(128000u<<8));
    }
    setup(a,32,32000);for(int i=0;i<500;++i)tick(a,12000);q15 bases[11];for(int p=0;p<11;++p)bases[p]=a.params[p].base;
    a.bypass=true;a.request_clear();CHECK(a.visual_state().clearing);CHECK(a.visual_state().clearing);a.bypass=false;
    const size_t allocations=array_calls;
    for(int i=0;i<640;++i){q15 out=tick(a);if(i>=319)CHECK(out==0);if(i<639)CHECK(a.visual_state().clearing);if(i==10)a.request_clear();}
    CHECK(!a.visual_state().clearing&&array_calls==allocations);
    std::vector<q15> silence(128010,0);a.process(silence.data(),(int)silence.size());for(q15 s:silence)CHECK(s==0);
    for(int p=0;p<11;++p)CHECK(a.params[p].base==bases[p]);
    q15 untouched=1234;a.process(nullptr,3);a.process(&untouched,-1);a.process(&untouched,0);CHECK(untouched==1234);a.reset();CHECK(!a.visual_state().ready);
    std::puts("ok PCM block subdivision, extrema/wow/read safety, clear pending+640 fade/history invalidation/no array allocations, silence/null/reset");
}
}
int main(){legacy_exact();mappings_memory();four_seconds_tone();hold_clean();clear_blocks();std::printf("delay dsp: 5/5 passed (%llu checks); visual=%zu bytes\n",(unsigned long long)checks,sizeof(DelayUnit::VisualState));}
