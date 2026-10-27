// Actual portable BIRD source/engine proof. No renderer, audio hardware or device IO.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include "engine/bird_tables.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <sys/stat.h>
#include <unistd.h>
using namespace seance;
namespace {
using B = BirdUnit;
using Pcm = std::vector<q15>;
using Bytes = std::vector<uint8_t>;
constexpr double PI = 3.14159265358979323846;
uint64_t checks = 0, compared = 0;
int endpoint_cases = 0, frequency_cases = 0, retrigger_cases = 0;
double max_frequency_error = 0;
std::string path;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
void set(Unit& u,int p,int v) { u.params[p].base=q15(v);u.params[p].snap(); }
int pitch(double hz) { return int(std::lround(std::log(hz/500)/std::log(12)*32767)); }
int duration(double ms) { return int(std::lround(std::log(ms/40)/std::log(40)*32767)); }
int sweep(int cents) { return int(std::lround(cents*32767./2400)); }
int trill(double hz) { return int(std::lround(hz*32767/40)); }
void configure(B& u,int p=21223,int s=12288,int t=13360,int tr=0,int air=2621,int gain=18022) {
    const int values[6]={p,s,t,tr,air,gain};for(int i=0;i<6;++i)set(u,i,values[i]);u.control_tick();
}
Pcm pcm(Unit& u,int count,int input=0) { Pcm out(count,q15(input));u.process(out.data(),count);return out; }
void equal(const Pcm& a,const Pcm& b) { CHECK(a==b);compared+=a.size(); }
bool silent(const Pcm& a) { return std::all_of(a.begin(),a.end(),[](q15 x){return x==0;}); }
double rms(const Pcm& a,int start,int count) { double sum=0;for(int i=start;i<start+count;++i)sum+=double(a[i])*a[i];return std::sqrt(sum/count); }
double amplitude(const Pcm& a,double hz,int start=1024,int count=24000) {
    double re=0,im=0,ws=0;for(int i=0;i<count;++i) { const double w=.5-.5*std::cos(2*PI*i/(count-1));const double phase=2*PI*hz*(start+i)/ENGINE_SR;re+=w*a[start+i]*std::cos(phase);im-=w*a[start+i]*std::sin(phase);ws+=w; }return 2*std::hypot(re,im)/ws;
}
std::vector<double> crossings(const Pcm& a,int start,int count) {
    std::vector<double> times;for(int i=start;i<start+count-1;++i)if(a[i]<=0&&a[i+1]>0)times.push_back(i-double(a[i])/(int(a[i+1])-a[i]));return times;
}
double frequency(const Pcm& a,int start,int count) { auto times=crossings(a,start,count);CHECK(times.size()>4);return (times.size()-1)*ENGINE_SR/(times.back()-times.front()); }
Bytes read() { std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}}; }
void put(const Bytes& b) { std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good()); }
void w16(Bytes& b,size_t at,int v) { CHECK(at+2<=b.size());b[at]=uint8_t(v);b[at+1]=uint8_t(uint16_t(v)>>8); }
uint32_t r32(const Bytes& b,size_t at) { CHECK(at+4<=b.size());return b[at]|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24; }
size_t chunk(const Bytes& b,const char* id) { for(size_t at=5;at+8<=b.size();at+=8+r32(b,at+4))if(!std::memcmp(b.data()+at,id,4))return at;CHECK(false);return 0; }
void mappings() {
    static_assert(int(UnitKind::Strain)==20&&int(UnitKind::Shift)==21&&int(UnitKind::Bird)==22,"append-only kinds");
    B u;CHECK(u.kind==UnitKind::Bird&&u.num_params==B::P_COUNT);CHECK(!std::strcmp(unit_kind_name(u.kind),"BIRD"));
    for(int p=0;p<B::P_COUNT;++p)CHECK(u.params[p].bipolar==(p==B::P_SWEEP||p==B::P_CONTOUR));
    CHECK(u.params[B::P_CONTOUR].base==0&&u.params[B::P_SYLLABLE].base==0);
    static_assert(sizeof(B::Visual)<128&&sizeof(B::CallShape)<128,"bounded scalar preview");
    CHECK(B::pitch_millihz(0)==500000&&B::pitch_millihz(32767)==6000000&&B::pitch_millihz(-32768)==500000);
    CHECK(B::duration_samples(0)==1280&&B::duration_samples(32767)==51200&&B::duration_samples(-32768)==1280);
    CHECK(B::sweep_cents(0)==0&&B::sweep_cents(-32768)==-2400&&B::sweep_cents(32767)==2400);
    CHECK(B::trill_millihz(0)==0&&B::trill_millihz(32767)==40000&&B::trill_millihz(-32768)==0);
    uint32_t previous_pitch=0,previous_time=0,previous_trill=0;
    for(int p=0;p<=32767;++p) {
        const auto hz=B::pitch_millihz(q15(p)),time=B::duration_samples(q15(p)),tr=B::trill_millihz(q15(p));
        CHECK(hz>=previous_pitch&&time>=previous_time&&tr>=previous_trill);previous_pitch=hz;previous_time=time;previous_trill=tr;
        CHECK(std::abs(hz/(500000*std::pow(12.,p/32767.))-1)<.000190);
        CHECK(std::abs(time/(1280*std::pow(40.,p/32767.))-1)<.0009);
        CHECK(B::sweep_cents(q15(-p))==-B::sweep_cents(q15(p)));
        CHECK(std::abs(B::sweep_cents(q15(p))-2400.*p/32767)<=.500001);
        CHECK(std::abs(tr-40000.*p/32767)<=.500001);
    }
    for(int i=0;i<=64;++i) { CHECK(std::abs(bird_tables::PITCH[i]-500000*std::pow(12.,i/64.))<=.500001);CHECK(std::abs(bird_tables::DURATION[i]-1280*std::pow(40.,i/64.))<=.500001); }
    for(int i=0;i<=128;++i)CHECK(std::abs(bird_tables::RATIO[i]-(1<<20)*std::pow(2.,-2+i/32.))<=.500001);
    CHECK(std::abs(int(B::pitch_millihz(u.params[0].base))-2500000)<1000);CHECK(B::sweep_cents(u.params[1].base)==900);CHECK(std::abs(int(B::duration_samples(u.params[2].base))-5760)<8);
    const auto props=u.sequencer_properties();CHECK(props.source&&!props.gate_processor&&props.lifecycle==SequenceLifecycle::Strike&&props.pitch_param==0&&props.level_param==5&&props.flow_param==-1&&props.duration_param==2);
    std::printf("ok exhaustive mappings/constants; defaults %.3f Hz / %+d cents / %.5f ms / %.3f Hz trill; sizeof BirdUnit=%zu, payload=%zu, tables=%zu bytes (+existing sine primitive)\n",B::pitch_millihz(u.params[0].base)/1000.,B::sweep_cents(u.params[1].base),B::duration_samples(u.params[2].base)/32.,B::trill_millihz(u.params[3].base)/1000.,sizeof(B),sizeof(B)-sizeof(Unit),sizeof(bird_tables::PITCH)+sizeof(bird_tables::DURATION)+sizeof(bird_tables::RATIO));
}
void lifetime_partitions_extremes() {
    B asleep;CHECK(silent(pcm(asleep,90000,32767)));configure(asleep,32767,32767,32767,32767,32767,32767);CHECK(silent(pcm(asleep,10000,-32768)));
    // Cartesian extreme values: every time/air/trill/gain endpoint, both sweep
    // signs/zero and low/high pitch. Oversized blocks must still stop exactly.
    for(int p:{0,32767})for(int s:{-32768,0,32767})for(int t:{0,32767})for(int tr:{0,32767})for(int air:{0,32767})for(int gain:{0,32767}) {
        B a,b;configure(a,p,s,t,tr,air,gain);configure(b,p,s,t,tr,air,gain);a.trigger();b.trigger();const int total=B::duration_samples(q15(t));auto x=pcm(a,total+257,32767);Pcm y(x.size(),-32768);size_t at=0;
        while(at<y.size()) { const int n=std::min<size_t>((at*13+11)%259+1,y.size()-at);b.control_tick();b.process(y.data()+at,n);at+=n; }
        equal(x,y);CHECK(x[0]==0&&x[total-1]==0);for(int i=total;i<int(x.size());++i)CHECK(x[i]==0);CHECK(std::abs(int(x[total-2]))<=4);
        if(gain)CHECK(!silent(x));else CHECK(silent(x));a.reset();CHECK(silent(pcm(a,23,32000)));a.trigger();equal(x,pcm(a,total+257,-1234));++endpoint_cases;
    }
    // Interleaved independent voices cannot perturb each other's local RNG.
    B a,b,intruder;configure(a,21223,12288,13360,28000,32767);configure(b,21223,12288,13360,28000,32767);a.trigger();b.trigger();intruder.trigger();Pcm x(19000),y(19000);a.process(x.data(),int(x.size()));for(size_t i=0;i<y.size();++i) { pcm(intruder,3);b.process(y.data()+i,1); }equal(x,y);
    std::printf("ok %d full endpoint combinations; input overwrite / pretrigger+finite silence / exact split+reset / local RNG; %llu PCM twins\n",endpoint_cases,(unsigned long long)compared);
}
void spectra_and_direction() {
    for(int p:{0,1,127,2048,5000,8192,12000,16384,21223,25000,29000,32766,32767}) {
        B u;configure(u,p,0,32767,0,0,32767);u.trigger();auto x=pcm(u,51200);const double hz=B::pitch_millihz(q15(p))/1000.;const double measured=frequency(x,1024,44000);const double error=std::abs(measured-hz);max_frequency_error=std::max(max_frequency_error,error);CHECK(error<.08);++frequency_cases;
    }
    for(int p:{pitch(900),pitch(2500),32767})for(int s:{-32767,-12288,12288,32767}) {
        B u;configure(u,p,s,32767,0,0,32767);u.trigger();auto x=pcm(u,51200);const double hz=B::pitch_millihz(q15(p))/1000.,cents=B::sweep_cents(q15(s));
        const double early=frequency(x,512,4096),late=frequency(x,46000,4096);CHECK(s>0?early>late:early<late);
        for(int start:{512,16000,32000,46000}) {
            auto times=crossings(x,start,4096);CHECK(times.size()>4);double predicted=0;const int samples=16384;
            for(int i=0;i<samples;++i) { const double sample=times.front()+(i+.5)*(times.back()-times.front())/samples;const double t=1-sample/51199.;predicted+=std::min(10000.,hz*std::pow(2.,cents*t*t/1200)); }
            predicted/=samples;const double measured=(times.size()-1)*ENGINE_SR/(times.back()-times.front());const double error=std::abs(measured-predicted);max_frequency_error=std::max(max_frequency_error,error);if(error>1.5)std::fprintf(stderr,"frequency p=%d sweep=%d start=%d actual=%.5f oracle=%.5f err=%.5f\n",p,s,start,measured,predicted,error);CHECK(error<1.5);++frequency_cases;
        }
    }
    B clean,warble,airy;const int p=pitch(1400);configure(clean,p,0,32767,0,0,32767);configure(warble,p,0,32767,trill(10),0,32767);configure(airy,p,0,32767,0,32767,32767);clean.trigger();warble.trigger();airy.trigger();const auto c=pcm(clean,51200),w=pcm(warble,51200),a=pcm(airy,51200);
    for(int cycle=1;cycle<14;++cycle) { const int peak=cycle*3200,gap=peak+1600;CHECK(rms(w,gap-80,160)<rms(w,peak-80,160)*.13);CHECK(rms(c,gap-80,160)>rms(c,peak-80,160)*.85); }
    const double hz=B::pitch_millihz(q15(p))/1000.;CHECK(amplitude(c,hz)>20000&&amplitude(c,2*hz)<5);CHECK(amplitude(a,2*hz)>1800);
    double clean_air=0,real_air=0;for(int band=0;band<30;++band) { const double f=3511+band*271;clean_air+=std::pow(amplitude(c,f),2);real_air+=std::pow(amplitude(a,f),2); }CHECK(real_air>clean_air*100&&real_air>1000);
    std::printf("ok %d actual PCM frequency checks (13 constant, 48 quadratic/signed/clamped oracle windows), worst %.5f Hz; 13 genuine syllable gaps; AIR harmonic+broadband spectra %.1f vs clean %.1f\n",frequency_cases,max_frequency_error,real_air,clean_air);
}
void latches_gain_retrigger() {
    for(int param=0;param<B::P_COUNT;++param) {
        if(param==B::P_GAIN)continue;
        B a,b;configure(a,21223,12288,13360,12000);configure(b,21223,12288,13360,12000);a.trigger();b.trigger();const auto latched=a.visual();equal(pcm(a,301),pcm(b,301));set(a,param,param==1?-32767:32767);a.control_tick();equal(pcm(a,6000),pcm(b,6000));CHECK(a.visual().latched[param]==latched.latched[param]);a.trigger();b.trigger();CHECK(a.visual().latched[param]==a.params[param].effective);CHECK(pcm(a,7000)!=pcm(b,7000));
    }
    B always,muted;configure(always,21223,0,32767,15000,32767);configure(muted,21223,0,32767,15000,32767);always.trigger();muted.trigger();equal(pcm(always,1000),pcm(muted,1000));set(muted,5,0);muted.control_tick();pcm(always,128);pcm(muted,128);CHECK(silent(pcm(muted,2000)));pcm(always,2000);set(muted,5,18022);muted.control_tick();pcm(always,128);pcm(muted,128);equal(pcm(always,10000),pcm(muted,10000));pcm(muted,60000);set(muted,5,32767);muted.control_tick();CHECK(silent(pcm(muted,7000)));
    // Gain retargets at the same audio timestamps, independent of call partition.
    B a,b;configure(a,32767,32767,32767,32767,32767);configure(b,32767,32767,32767,32767,32767);a.trigger();b.trigger();
    for(int block=0;block<500;++block) { const int n=37+block%97;const int gain=(block%3)*16383;set(a,5,gain);set(b,5,gain);a.control_tick();b.control_tick();auto x=pcm(a,n);Pcm y(n);for(int i=0;i<n;++i) { b.control_tick();b.process(y.data()+i,1); }equal(x,y); }
    // Each rapid restart starts at the exact previous pre-gain sample, not a
    // phase reset discontinuity; at fixed gain the first output is exact too.
    int worst_jump=0;
    for(int p:{0,21223,32767})for(int tr:{0,32767})for(int air:{0,32767}) {
        B u;configure(u,p,32767,0,tr,air,32767);u.trigger();q15 previous=0;
        for(int gap:{1,2,17,63,71,211,419,1237,1,1,5,32}) {
            auto x=pcm(u,gap);previous=x.back();u.trigger();auto first=pcm(u,1);worst_jump=std::max(worst_jump,std::abs(int(first[0])-previous));CHECK(first[0]==previous);++retrigger_cases;
        }
        auto tail=pcm(u,2000);CHECK(std::all_of(tail.begin()+1279,tail.end(),[](q15 x){return x==0;}));
    }
    std::printf("ok next-strike-only seven controls / live128 gain silence+reopen / interrupted gain partition / %d rapid retriggers first-sample jump %d q15\n",retrigger_cases,worst_jump);
}
// Frozen pre-extension one-strike arithmetic, independent of the new shape helpers.
// Deliberately retains the original phase accumulation, tables, AIR and envelope.
Pcm legacy_call(const B& u) {
    auto sine=[](uint32_t p){return shift_dsp::sine(p)>>15;};
    auto smooth=[](int32_t t){const int32_t t2=int32_t((int64_t(t)*t)>>15);return int32_t((int64_t(t2)*(98304-2*t))>>15);};
    auto ratio=[](int32_t position){const uint32_t p=uint32_t(int64_t(position)+(1ll<<30)),i=p>>24,f=p&0xffffffu;return i==128?bird_tables::RATIO[128]:bird_tables::RATIO[i]+uint32_t((uint64_t(bird_tables::RATIO[i+1]-bird_tables::RATIO[i])*f)>>24);};
    const uint32_t duration=B::duration_samples(u.params[2].effective);
    const uint32_t inverse=uint32_t(((1ull<<32)+(duration-2)/2)/(duration-1));
    const uint32_t base=uint32_t((uint64_t(B::pitch_millihz(u.params[0].effective))<<32)/(ENGINE_SR*1000ull));
    const uint32_t trill_inc=uint32_t((uint64_t(B::trill_millihz(u.params[3].effective))<<32)/(ENGINE_SR*1000ull));
    const int32_t sweep_q30=int32_t(int64_t(B::sweep_cents(u.params[1].effective))*(1ll<<30)/2400);
    const int air=std::max(0,int(u.params[4].effective)),gain=std::max(0,int(u.params[5].effective));
    uint32_t phase=0,trill_phase=0,rng=0x42495244u;int32_t low=0,slow=0;Pcm out(duration+17,0);
    for(uint32_t age=0;age<duration;++age) {
        const uint32_t remaining=duration-1-age;
        const int32_t t=int32_t(std::min<uint64_t>(1ull<<30,(uint64_t(remaining)*inverse)>>2));
        const int32_t t2=int32_t((int64_t(t)*t)>>30),sweep=int32_t((int64_t(sweep_q30)*t2)>>30);
        uint64_t increment=(uint64_t(base)*ratio(sweep))>>20;int32_t articulation=32768;
        if(trill_inc) { const int32_t v=sine(trill_phase),lobe=(32768+sine(trill_phase+0x40000000u))>>1,square=int32_t((int64_t(lobe)*lobe)>>15),fourth=int32_t((int64_t(square)*square)>>15);articulation=2621+int32_t((int64_t(30147)*fourth)>>15);increment=uint64_t(int64_t(increment)+((int64_t(increment)*v)>>22)); }
        const uint32_t inc=uint32_t(std::min<uint64_t>((10000ull<<32)/ENGINE_SR,increment));
        const int32_t whistle=sine(phase),harmonic=sine(phase*2u);
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;const int32_t white=int32_t(rng>>16)-32768;
        low+=(white-low)>>2;slow+=(low-slow)>>5;const int32_t noise=std::max<int32_t>(-32768,std::min<int32_t>(32767,low-slow));
        const int32_t harmonic_gain=air>>3,noise_gain=air>>2,whistle_gain=32768-harmonic_gain-noise_gain;
        int32_t sample=int32_t((int64_t(whistle)*whistle_gain+int64_t(harmonic)*harmonic_gain+int64_t(noise)*noise_gain)>>15);
        const int32_t attack=smooth(int32_t(std::min<uint32_t>(128,age)<<8)),release=smooth(int32_t(std::min<uint32_t>(320,remaining)*32768u/320u));
        const int32_t body=14746+int32_t((int64_t(18022)*t)>>30);int32_t envelope=int32_t((int64_t(attack)*release)>>15);
        envelope=int32_t((int64_t(envelope)*body)>>15);envelope=int32_t((int64_t(envelope)*articulation)>>15);
        sample=int32_t((int64_t(sample)*envelope)>>15);out[age]=trackr::fx::sat_q15(int32_t((int64_t(sample)*gain)>>15));
        phase+=inc;trill_phase+=trill_inc;
    }
    return out;
}
void expressive_preview_legacy() {
    const int old[][6]={{21223,12288,13360,0,2621,18022},{32767,32767,32767,32767,32767,32767},{0,-32768,0,17000,32767,23001},{15312,-11711,17892,20321,0,32767}};
    for(const auto& settings:old){B u;for(int p=0;p<6;++p)set(u,p,settings[p]);u.control_tick();const auto ref=legacy_call(u);u.trigger();equal(ref,pcm(u,int(ref.size())));}
    const int expressive[][4]={{-32768,0,14000,32767},{32767,0,12000,-32768},{0,32767,14000,32767},{16384,23123,32767,32767}};
    int worst_pcm=0;
    for(const auto& setting:expressive) {
        B a,b,neutral;for(B* u:{&a,&b,&neutral})configure(*u,setting[2],setting[3],duration(700),trill(12),0,32767);
        CHECK(!a.visual().active&&a.visual().duration==0&&a.visual().age==0);
        for(B* u:{&a,&b}){set(*u,B::P_CONTOUR,setting[0]);set(*u,B::P_SYLLABLE,setting[1]);u->trigger();}neutral.trigger();
        const auto visual=a.visual();const auto shape=B::prepare_shape(visual.latched);const uint32_t count=shape.duration;
        CHECK(visual.active&&visual.age==0&&visual.duration==count&&visual.latched[B::P_CONTOUR]==setting[0]&&visual.latched[B::P_SYLLABLE]==setting[1]);
        CHECK(B::shape_point(shape,0).envelope_q15==0&&B::shape_point(shape,count-1).envelope_q15==0&&B::shape_point(shape,UINT32_MAX).envelope_q15==0);
        const auto x=pcm(a,int(count)+17);Pcm y(x.size());size_t at=0;while(at<y.size()){const int n=std::min<size_t>((at*7+19)%113+1,y.size()-at);b.process(y.data()+at,n);at+=n;}equal(x,y);CHECK(x!=pcm(neutral,int(x.size())));
        CHECK(x[0]==0&&x[count-1]==0&&std::all_of(x.begin()+count,x.end(),[](q15 v){return v==0;}));CHECK(!a.visual().active&&a.visual().age==count);
        uint32_t phase=0;
        for(uint32_t age=0;age<count;++age) {
            const auto point=B::shape_point(shape,age);CHECK(point.frequency_millihz>=124000&&point.frequency_millihz<=10000000&&point.envelope_q15>=0);
            const int32_t sample=int32_t((int64_t(shift_dsp::sine(phase)>>15)*point.envelope_q15)>>15);
            const int expected=int32_t((int64_t(sample)*32767)>>15);worst_pcm=std::max(worst_pcm,std::abs(expected-x[age]));CHECK(std::abs(expected-x[age])<=3);
            phase+=uint32_t((uint64_t(point.frequency_millihz)<<32)/(ENGINE_SR*1000ull));
        }
        // Independent continuous law, checked away from only-zero endpoints.
        auto contour=[&](double t){const double c=std::max(-32767,setting[0])/32767.;const double target=c<0?4*t*(1-t):1-std::pow(1-t,4);return t*t+(target-t*t)*std::abs(c);};
        for(uint32_t age:{count/8,count/3,count/2,count-1}) {
            const double t=1-double(age)/(count-1),phase_r=2*PI*double(uint32_t(shape.trill_inc*age))/4294967296.;
            const double global=contour(t),local=contour((1-std::sin(phase_r))/2)*t;
            const double trajectory=global+(local-global)*(setting[1]/32767.);
            const double hz=B::pitch_millihz(visual.latched[B::P_PITCH])/1000.;
            const double oracle=std::min(10000.,hz*std::pow(2.,B::sweep_cents(visual.latched[B::P_SWEEP])*trajectory/1200)*(1+std::sin(phase_r)/128));
            CHECK(std::abs(B::shape_point(shape,age).frequency_millihz/1000.-oracle)<1.7);
        }
        CHECK(a.visual().age==count);a.reset();CHECK(!a.visual().active&&a.visual().age==0&&a.visual().duration==0);
    }
    B a,b;configure(a);configure(b);set(b,B::P_SYLLABLE,32767);a.trigger();b.trigger();equal(pcm(a,6000),pcm(b,6000));
    CHECK(B::shape_point(B::CallShape{},0).frequency_millihz==0);
    // Historical SNC7 has a fixed-width param bank: the old six-param capture
    // zeroed all unused slots. Recreate that authored layout, not a new format.
    auto old_engine=std::make_unique<Engine>(),loaded=std::make_unique<Engine>();auto* old_bird=new B;delete old_engine->replace_unit(0,0,old_bird);old_bird->num_params=6;configure(*old_bird,12000,-23000,13000,17000,13000,24000);
    CHECK(patchio::save(*old_engine,path.c_str()));CHECK(patchio::load(*loaded,path.c_str()));auto* restored=static_cast<B*>(loaded->chains[0].units[0]);CHECK(restored->num_params==B::P_COUNT&&restored->params[B::P_CONTOUR].base==0&&restored->params[B::P_SYLLABLE].base==0);const auto ref=legacy_call(*old_bird);restored->trigger();equal(ref,pcm(*restored,int(ref.size())));
    std::printf("ok four neutral frozen legacy PCM / expressive contour+syllable partitions / shared pure preview actual PCM <=%d q15 / scalar age+latched / old six-param SNC7 -> neutral eight\n",worst_pcm);
}
struct CountingBird : B { int strikes=0;int pitch_at_strike[16]={},contour_at_strike[16]={},syllable_at_strike[16]={};void trigger() override { if(strikes<16){pitch_at_strike[strikes]=params[P_PITCH].effective;contour_at_strike[strikes]=params[P_CONTOUR].effective;syllable_at_strike[strikes]=params[P_SYLLABLE].effective;}++strikes;B::trigger(); } };
void engine_persistence() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();auto*u=new B;delete a->replace_unit(0,0,u);const int values[B::P_COUNT]={19317,-17421,16731,11012,2981,24000,-29000,25123};
    for(int p=0;p<B::P_COUNT;++p) { set(*u,p,values[p]);u->params[p].src=ModSource(1+p%5);u->params[p].mod_depth=q15(-13000+p*2300); }u->bypass=true;a->capture_scene(0,0);set(*u,1,16321);set(*u,B::P_CONTOUR,29000);set(*u,B::P_SYLLABLE,0);a->capture_scene(0,1);set(*u,1,values[1]);set(*u,B::P_CONTOUR,values[B::P_CONTOUR]);set(*u,B::P_SYLLABLE,values[B::P_SYLLABLE]);
    for(int p=0;p<B::P_COUNT;++p) { CHECK(a->set_step_lock(0,p,0,p,(p==1||p==B::P_CONTOUR)?-32123:13123+p)); }a->macro_pads[0].route_count=1;a->macro_pads[0].routes[0]={0,0,B::P_CONTOUR,-32767,32767};
    CHECK(patchio::save(*a,path.c_str()));const auto saved=read();CHECK(saved[chunk(saved,"UNIT")+8]==0);CHECK(patchio::load(*b,path.c_str()));auto*got=static_cast<B*>(b->chains[0].units[0]);CHECK(got&&got->kind==UnitKind::Bird&&got->num_params==B::P_COUNT&&got->bypass);
    for(int p=0;p<B::P_COUNT;++p) { CHECK(got->params[p].base==u->params[p].base&&got->params[p].src==u->params[p].src&&got->params[p].mod_depth==u->params[p].mod_depth);CHECK(got->params[p].bipolar==(p==1||p==B::P_CONTOUR));CHECK(b->pattern.steps[0][p].lock_count==1); }
    CHECK(b->scenes[0][0].kind[0]==UnitKind::Bird&&b->scenes[0][0].base[0][1]==values[1]);CHECK(b->macro_pads[0].route_count==1);CHECK(patchio::save(*b,path.c_str()));CHECK(read()==saved);
    got->control_tick();got->trigger();CHECK(!silent(pcm(*got,3000)));std::unique_ptr<Unit> clone(clone_unit(got));CHECK(clone&&clone->kind==UnitKind::Bird&&clone->bypass&&silent(pcm(*clone,1000)));B reference;for(int p=0;p<B::P_COUNT;++p) { CHECK(clone->params[p].base==got->params[p].base&&clone->params[p].src==got->params[p].src&&clone->params[p].mod_depth==got->params[p].mod_depth);set(reference,p,got->params[p].base); }clone->control_tick();reference.control_tick();clone->trigger();reference.trigger();equal(pcm(*clone,8000),pcm(reference,8000));
    b->apply_scene_morph(0,16384);CHECK(got->params[1].base>values[1]&&got->params[1].base<16321);CHECK(std::abs(int(got->params[B::P_CONTOUR].base))<4);CHECK(got->params[B::P_SYLLABLE].base>12000&&got->params[B::P_SYLLABLE].base<13000);
    Param& p=got->params[1];p.base=-5000;p.mod_depth=32767;p.snap();p.resolve(-32767);CHECK(p.effective==-32767);p.snap();p.resolve(32767);CHECK(p.effective==27766);p.step_lock=-12345;p.step_lock_override=true;CHECK(p.control_source()==-12345);p.audition=8123;p.audition_override=true;CHECK(p.control_source()==8123);p.performance=23123;p.performance_override=true;CHECK(p.control_source()==23123);p.temp=-32767;p.temp_override=true;CHECK(p.control_source()==-32767);
    auto e=std::make_unique<Engine>();auto*live=new CountingBird;delete e->replace_unit(0,0,live);configure(*live,pitch(1500),0,0,0,0,18022);e->set_sequence_length(0,4);for(int step=0;step<4;++step) { e->pattern.steps[0][step].flags=SeqStep::Trig;CHECK(e->set_step_lock(0,step,0,0,step%2?8000:24000));CHECK(e->set_step_lock(0,step,0,B::P_CONTOUR,step%2?-32767:32767));CHECK(e->set_step_lock(0,step,0,B::P_SYLLABLE,step%2?0:25123)); }e->set_sequencer_enabled(true);e->transport_play();Pcm seq(20001*2);e->render(seq.data(),20001);CHECK(live->strikes==5);CHECK(live->pitch_at_strike[0]==24000&&live->pitch_at_strike[1]==8000&&live->pitch_at_strike[2]==24000);CHECK(live->contour_at_strike[0]==32767&&live->contour_at_strike[1]==-32767&&live->syllable_at_strike[0]==25123&&live->syllable_at_strike[1]==0);CHECK(live->params[0].base==pitch(1500));CHECK(!silent(seq));e->transport_stop();e->transport_reset();pcm(*live,1280);CHECK(silent(pcm(*live,2000)));
    CHECK(e->set_step_lock(0,0,0,1,-31111));CHECK(e->audition_sequence_step(0,0));CHECK(live->params[1].audition_override&&live->params[1].audition==-31111);e->cancel_sequence_audition();CHECK(!live->params[1].audition_override);CHECK(!silent(pcm(*live,1000)));
    auto mod=std::make_unique<Engine>();auto*mu=new B;delete mod->replace_unit(0,0,mu);mu->params[1].src=ModSource::SigilA;mu->params[1].mod_depth=20000;mu->params[1].base=-20000;mu->params[1].snap();for(int p:{B::P_CONTOUR,B::P_SYLLABLE}){mu->params[p].src=ModSource::SigilA;mu->params[p].mod_depth=20000;mu->params[p].base=p==B::P_CONTOUR?-20000:0;mu->params[p].snap();}mod->sigils[0].depth=32767;mod->sigils[0].drawn=true;for(auto&v:mod->sigils[0].table)v=32767;Pcm out(128);mod->render(out.data(),64);CHECK(mu->params[1].target>-10&&mu->params[1].target<10);CHECK(std::abs(int(mu->params[B::P_CONTOUR].target))<10&&mu->params[B::P_SYLLABLE].target>19990);CHECK(silent(out));mod->trigger_chain(0);mod->render(out.data(),64);CHECK(!silent(out));
    std::puts("ok exact SNC7 eight params/resave/clone fresh state/scenes/macro routes / mod+layers / actual five sequencer strikes with alternating pitch locks / stopped audition");
}
void old_patches_and_rollback() {
    auto target=std::make_unique<Engine>();
    for(int version=1;version<=6;++version) {
        Bytes old={'S','N','C',uint8_t('0'+version),uint8_t(version)};for(int c=0;c<NUM_CHAINS;++c) { old.insert(old.end(),{255,127});if(version>=2)old.insert(old.end(),{0,0});old.push_back(0);size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);if(c==0) { old[first]=uint8_t(UnitKind::Osc);old[first+2]=2;const int v[6]={16384,8192,12000,0,0,0};for(int p=0;p<6;++p)w16(old,first+3+p*5,v[p]); } }
        old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);if(version>=3) { old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0); }if(version>=4)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);if(version>=5) { old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0}); }if(version>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);put(old);CHECK(patchio::load(*target,path.c_str()));auto*osc=static_cast<OscUnit*>(target->chains[0].units[0]);CHECK(osc&&osc->kind==UnitKind::Osc&&!osc->shape_morph&&osc->wave==OscUnit::Wave::Saw);OscUnit ref;ref.shape_morph=false;ref.wave=OscUnit::Wave::Saw;const int v[6]={16384,8192,12000,0,0,0};for(int p=0;p<6;++p)set(ref,p,v[p]);osc->control_tick();ref.control_tick();equal(pcm(*osc,4096),pcm(ref,4096));
    }
    auto a=std::make_unique<Engine>();delete a->replace_unit(0,0,new B);CHECK(patchio::save(*a,path.c_str()));const auto saved=read();Unit*sentinel=target->chains[0].units[0];for(bool truncate:{false,true}) { auto bad=saved;if(truncate)bad.resize(bad.size()-1);else bad[chunk(bad,"CORE")+8+5]=255;put(bad);CHECK(!patchio::load(*target,path.c_str()));CHECK(target->chains[0].units[0]==sentinel); }
    std::puts("ok SNC1..6 old oscillator fixture PCM and unknown-kind/truncated transactional rejection; serializer untouched");
}
void wav(const std::string& name,const Pcm& data) { std::ofstream f(name,std::ios::binary);auto u16=[&](uint16_t x){f.put(char(x));f.put(char(x>>8));};auto u32=[&](uint32_t x){u16(uint16_t(x));u16(uint16_t(x>>16));};f.write("RIFF",4);u32(36+data.size()*2);f.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(ENGINE_SR);u32(ENGINE_SR*2);u16(2);u16(16);f.write("data",4);u32(data.size()*2);for(q15 x:data)u16(uint16_t(x));CHECK(f.good()); }
void demos(const std::string& dir) {
    mkdir("ui_mockups/bird_core",0755);mkdir(dir.c_str(),0755);const char*names[4]={"01-clean-chirp.wav","02-syllabic-trill.wav","03-air-and-rasp.wav","04-low-electronic.wav"};
    for(int mode=0;mode<4;++mode) { B u;if(mode==0)configure(u,21223,12288,13360,0,0,18022);if(mode==1)configure(u,pitch(2100),sweep(500),duration(700),trill(12),1000,18022);if(mode==2)configure(u,pitch(1600),sweep(1100),duration(420),trill(7),29000,19000);if(mode==3)configure(u,0,-32767,duration(1100),trill(28),20000,21000);Pcm data(ENGINE_SR*4);for(int at=0;at<int(data.size());at+=32) { if(at%(mode==3?48000:32000)==0)u.trigger();u.process(data.data()+at,32); }wav(dir+"/"+names[mode],data); }
    std::puts("wrote four 4-second actual mono 32kHz PCM WAV demos; not presets, listening or hardware proof");
}
void benchmark() {
    B u;configure(u,32767,32767,32767,32767,32767,32767);q15 block[32];const auto start=std::chrono::steady_clock::now();int64_t checksum=0;for(int i=0;i<32000;++i) { if(i%1000==0)u.trigger();u.process(block,32);checksum+=block[i&31]; }const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::printf("host-only hot BIRD %.3f ns/sample (%lld checksum), no 3DS CPU assertion\n",seconds*1e9/(32000*32),(long long)checksum);
}
}
int main(int argc,char**argv) {
    path="/tmp/seance-bird-"+std::to_string((long)getpid())+".snc";const auto start=std::chrono::steady_clock::now();mappings();lifetime_partitions_extremes();spectra_and_direction();latches_gain_retrigger();expressive_preview_legacy();engine_persistence();old_patches_and_rollback();if(argc==3&&!std::strcmp(argv[1],"--demos"))demos(argv[2]);benchmark();std::remove(path.c_str());std::printf("bird core: 7/7 passed (%llu checks, %llu exact twin PCM), %.3fs host total\n",(unsigned long long)checks,(unsigned long long)compared,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());
}
