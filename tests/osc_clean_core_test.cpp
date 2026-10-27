// Focused clean OSC DSP/persistence/collector proof; --quality is a small numeric
// candidate comparison, not a media export or a hardware performance claim.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include "dsp/osc_clean.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>
using namespace seance;
namespace {
using O = OscUnit; using Pcm = std::vector<q15>; using Bytes = std::vector<uint8_t>;
constexpr double PI = 3.14159265358979323846;
unsigned long long checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
void set(O& o,int p,int v) { o.params[p].base=q15(v);o.params[p].snap(); }
Pcm pcm(O& o,int n) { Pcm b(n,12345);o.process(b.data(),n);return b; }
void configure(O& o,int pitch=16384,int shape=0,int width=16384,int fold=0,int sub=0,int drift=0) {
    const int values[]={pitch,width,32767,fold,sub,drift,shape};
    for(int i=0;i<O::P_COUNT;++i)set(o,i,values[i]);
    o.control_tick();
}
double frequency(const Pcm& b,int start=512) {
    std::vector<double> t;for(int i=start;i+1<int(b.size());++i)if(b[i]<=0&&b[i+1]>0)t.push_back(i-double(b[i])/(int(b[i+1])-b[i]));
    CHECK(t.size()>4);return (t.size()-1)*ENGINE_SR/(t.back()-t.front());
}
double mean(const Pcm& b,int start=512) { double sum=0;for(size_t i=start;i<b.size();++i)sum+=b[i];return sum/(b.size()-start); }
double phase_at(const Pcm& b,double hz) {
    double re=0,im=0;for(size_t i=512;i<b.size();++i){double p=2*PI*hz*(double(i)+1)/ENGINE_SR;re+=b[i]*std::cos(p);im+=b[i]*std::sin(p);}return std::atan2(re,im);
}
void pitch_and_shapes() {
    double worst=0;
    for(bool qnt:{false,true})for(int raw:{0,1,8192,16384,16385,16480,20001,32767}) {
        O o;o.pitch_quantize=qnt;configure(o,raw);const double want=osc_clean::pitch_hz(raw,qnt);
        const auto b=pcm(o,24000);const double error=std::abs(frequency(b)-want);worst=std::max(worst,error);CHECK(error<.08);
    }
    CHECK(std::abs(osc_clean::pitch_hz(16384,false)-261.625565)<.001);
    CHECK(osc_clean::pitch_hz(16385,false)>osc_clean::pitch_hz(16384,false));
    CHECK(osc_clean::pitch_hz(16385,true)==osc_clean::pitch_hz(16384,true));
    O sine;configure(sine);const double reference=phase_at(pcm(sine,32000),osc_clean::pitch_hz(16384,false));
    for(int shape:{0,5461,10922,16384,21845,27306,32767})for(int width:{0,16384,32767}) {
        O o;configure(o,16384,shape,width);auto b=pcm(o,32000);
        CHECK(std::abs(phase_at(b,osc_clean::pitch_hz(16384,false))-reference)<.025);
        CHECK(std::abs(mean(b))<75);
    }
    O sub;configure(sub,16384,0,16384,0,32767);CHECK(std::abs(frequency(pcm(sub,32000))-130.81278)<.01);
    std::printf("ok continuous/qnt measured pitch (worst %.5f Hz), coherent morph/PW fundamentals and sine sub\n",worst);
}
void events_fold_bounds() {
    float a=-1,b=1;osc_clean::step_event(.9f,1.1f,2,a,b);CHECK(std::abs(a+.75f)<1e-6&&std::abs(b-.75f)<1e-6);
    a=1;b=-1;osc_clean::step_event(.1f,-.1f,2,a,b);CHECK(std::abs(a-.75f)<1e-6&&std::abs(b+.75f)<1e-6);
    double worst=0;
    for(float center:{-7.f,-5.f,-3.f,-1.f,0.f,1.f,3.f,5.f,7.f})for(float dx:{0.f,1e-6f,.0001f,.00099f,.00101f,.1f,1.f,4.f,14.f}) {
        const float x=std::min(7.f,center+dx/2),previous=std::max(-7.f,center-dx/2);
        const float actual=osc_clean::fold_adaa(x,previous);CHECK(std::isfinite(actual)&&std::abs(actual)<=1.000001);
        double reference=0;for(int i=0;i<1000;++i)reference+=osc_clean::fold(previous+(x-previous)*(i+.5f)/1000)/1000.;
        worst=std::max(worst,std::abs(actual-reference));CHECK(std::abs(actual-reference)<.0006);
    }
    // Endpoint + continuously moving PWM, gain, fold, sub and morph. Every output
    // is actual q15 source PCM; sanitizer also checks internal indexing/casts.
    O o;configure(o,28000,32767,0,32767,0);Pcm all;
    for(int tick=0;tick<3000;++tick) {
        set(o,O::P_PW,int((.5+.5*std::sin(2*PI*tick/1003))*32767));
        set(o,O::P_FOLD,tick<1500?0:32767);
        o.control_tick();auto b=pcm(o,32);all.insert(all.end(),b.begin(),b.end());
        for(q15 x:b)CHECK(std::abs(int(x))<32767);
    }
    CHECK(std::abs(mean(Pcm(all.begin(),all.begin()+48000)))<350); // unfurled PWM; fold may create DC
    // Static pulse extremes: zero-mean subtraction must not clip the asymmetric
    // side; fixed normalization and FIR L1 headroom leave no integer saturation.
    for(int width:{0,8192,16384,24576,32767}) { O u;configure(u,16384,32767,width);auto b=pcm(u,96000);CHECK(std::abs(mean(b))<40); }
    std::printf("ok forward/backward event timing, moving PWM DC/bounds, ADAA corner/equal fallback (max %.7f FS)\n",worst);
}
void ramps_and_drift() {
    O whole;configure(whole,8192,32000,8000,26000,12000,32767);O split=whole;
    Pcm a,b;
    for(int tick=0;tick<300;++tick) {
        for(O* u:{&whole,&split}) {
            set(*u,O::P_PITCH,tick%2?22000:9000);set(*u,O::P_PW,tick%3?31000:1000);
            set(*u,O::P_FOLD,tick%2?30000:0);set(*u,O::P_SHAPE,(tick*719)%32768);u->control_tick();
        }
        auto x=pcm(whole,32);a.insert(a.end(),x.begin(),x.end());
        for(int n:{1,3,11,2,15}){auto y=pcm(split,n);b.insert(b.end(),y.begin(),y.end());}
    }
    CHECK(a==b);
    O ramp;configure(ramp,0);ramp.set_visual_capture(true);pcm(ramp,64);set(ramp,O::P_PITCH,32767);ramp.control_tick();
    uint32_t previous=ramp.visual().frequency_millihz;
    for(int i=0;i<32;++i){pcm(ramp,1);CHECK(ramp.visual().frequency_millihz>previous);previous=ramp.visual().frequency_millihz;}
    CHECK(std::abs(previous/1000.-osc_clean::pitch_hz(32767,false))<.002);
    O x,y;configure(x,16384,0,16384,0,0,32767);configure(y,16384,0,16384,0,0,32767);
    x.set_visual_capture(true);y.set_visual_capture(true);auto first=pcm(x,20000);CHECK(first!=pcm(y,20000));
    x.reset();x.control_tick();CHECK(first==pcm(x,20000));
    x.set_visual_capture(true);set(x,O::P_DRIFT,0);x.control_tick();pcm(x,1);CHECK(x.visual().frequency_millihz==261626);
    O small;configure(small,16384,0,16384,0,0,1);O zero=small;set(zero,O::P_DRIFT,0);zero.control_tick();
    CHECK(pcm(small,18000)!=pcm(zero,18000)); // even one raw LSB, not a coarse mHz meter
    // Trigger remains flow/no-reset; an external trigger cannot reset phase/drift.
    O triggered=whole,flow=whole;triggered.trigger();CHECK(pcm(triggered,100)==pcm(flow,100));
    std::puts("ok audio-time 32-frame ramps, exact process partitions, independent deterministic full-precision drift and immediate depth0");
}
void collector() {
    static_assert(O::Visual::COLUMNS==96&&O::Visual::HISTORY==48,"UI contract");
    for(int pitch:{0,16384,32767}) {
        O u;configure(u,pitch);O unfocused=u;u.set_visual_capture(true);CHECK(!u.visual().valid);
        auto out=pcm(u,6000);CHECK(out==pcm(unfocused,6000));const auto& v=u.visual();CHECK(v.valid&&v.generation>0&&v.pitch_count>0);
        const double expected=2*ENGINE_SR/osc_clean::pitch_hz(pitch,false);CHECK(std::abs(v.samples_per_cycle-expected)<1.1);
        int occupied=0;for(int i=0;i<96;++i)if(v.occupied[i]) {
            ++occupied;CHECK(v.pre_min[i]<=v.pre_max[i]&&v.out_min[i]<=v.out_max[i]);
            CHECK(std::abs(int(v.pre_min[i])-v.out_min[i])<=1&&std::abs(int(v.pre_max[i])-v.out_max[i])<=1);
            CHECK(std::find(out.begin(),out.end(),v.out_min[i])!=out.end());CHECK(std::find(out.begin(),out.end(),v.out_max[i])!=out.end());
        }
        CHECK(occupied<=v.samples_per_cycle);if(pitch==32767)CHECK(occupied<40);else CHECK(occupied==96);
        const auto gen=v.generation;u.set_visual_capture(false);CHECK(!u.visual().valid&&!u.visual().pitch_count&&u.visual().generation>gen);
        pcm(u,6000);CHECK(!u.visual().valid);u.set_visual_capture(true);pcm(u,6000);CHECK(u.visual().valid);
        u.bypass=true;u.control_tick();CHECK(!u.visual().valid);u.bypass=false;u.set_visual_capture(true);u.reset();CHECK(!u.visual().valid);
    }
    O old;old.clean=false;old.control_tick();old.set_visual_capture(true);pcm(old,10000);CHECK(!old.visual().valid);
    auto engine=std::make_unique<Engine>();auto* osc=new O;delete engine->replace_unit(0,0,osc);configure(*osc);osc->set_visual_capture(true);
    q15 stereo[128];for(int n=0;n<100;++n)engine->render(stereo,64);CHECK(osc->visual().valid);
    osc->bypass=true;engine->render(stereo,64);CHECK(!osc->visual().valid);
    std::printf("ok completed phase captures, sparse occupancy, exact audible samples, focus/bypass/reset invalidation; sizeof OSC=%zu Visual=%zu\n",sizeof(O),sizeof(O::Visual));
}
Bytes read(const std::string& path) { std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}}; }
size_t chunk(const Bytes& b,const char* id) { for(size_t p=5;p+8<=b.size();) { if(!std::memcmp(b.data()+p,id,4))return p;const size_t n=b[p+4]|uint32_t(b[p+5])<<8|uint32_t(b[p+6])<<16|uint32_t(b[p+7])<<24;p+=8+n; }CHECK(false);return 0; }
void persistence_layers() {
    const std::string path="/tmp/seance-osc-clean-"+std::to_string(long(getpid()))+".snc";
    for(int flags=0;flags<8;++flags) {
        auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();auto* u=new O;delete a->replace_unit(0,0,u);
        u->clean=(flags&4)!=0;u->shape_morph=(flags&2)!=0;u->pitch_quantize=(flags&1)!=0;u->wave=O::Wave::Saw;
        configure(*u,18000,23000,27000,5000,8000,300);
        for(int p=0;p<O::P_COUNT;++p){u->params[p].src=ModSource::SigilA;u->params[p].mod_depth=q15(2000+p*101);CHECK(a->set_step_lock(0,p,0,p,17000+p*500));}
        a->macro_pads[0].route_count=1;a->macro_pads[0].routes[0]={0,0,O::P_PW,0,32767};a->capture_scene(0,0);
        set(*u,O::P_PITCH,21000);a->capture_scene(0,1);u->params[O::P_PITCH].temp=27000;u->params[O::P_PITCH].temp_override=true;
        CHECK(patchio::save(*a,path.c_str()));const auto bytes=read(path);CHECK(bytes[chunk(bytes,"UNIT")+8]==flags);
        CHECK(patchio::load(*b,path.c_str()));auto* v=static_cast<O*>(b->chains[0].units[0]);CHECK(v->clean==u->clean&&v->shape_morph==u->shape_morph&&v->pitch_quantize==u->pitch_quantize);
        CHECK(v->params[O::P_PITCH].base==21000&&!v->params[O::P_PITCH].temp_override);CHECK(b->macro_pads[0].route_count==1);
        for(int p=0;p<O::P_COUNT;++p)if(p!=O::P_SHAPE||u->shape_morph){CHECK(v->params[p].base==u->params[p].base);CHECK(v->params[p].src==u->params[p].src&&v->params[p].mod_depth==u->params[p].mod_depth);CHECK(b->pattern.steps[0][p].lock_count==1);}
        std::unique_ptr<Unit> clone(clone_unit(v));auto* c=static_cast<O*>(clone.get());CHECK(c->clean==v->clean&&c->shape_morph==v->shape_morph&&c->pitch_quantize==v->pitch_quantize);
        CHECK(!c->visual().valid);b->apply_scene_morph(0,16384);CHECK(v->params[O::P_PITCH].base>18000&&v->params[O::P_PITCH].base<21000);CHECK(v->clean==bool(flags&4));
        set(*v,O::P_SHAPE,12345);v->control_tick();CHECK(v->clean==bool(flags&4)); // shape cannot opt in
    }
    std::remove(path.c_str());
    O u;configure(u);auto& p=u.params[O::P_PITCH];p.base=10000;p.step_lock=15000;p.step_lock_override=true;p.performance=20000;p.performance_override=true;p.temp=25000;p.temp_override=true;p.src=ModSource::SigilA;p.mod_depth=1000;
    u.set_visual_capture(true);
    for(int expected:{25999,20999,15999,10999}) { p.slew_ready=false;p.resolve(32767);CHECK(p.effective==expected);u.control_tick();pcm(u,32);CHECK(std::abs(u.visual().frequency_millihz/1000.-osc_clean::pitch_hz(expected,false))<.002);if(p.temp_override)p.temp_override=false;else if(p.performance_override)p.performance_override=false;else p.step_lock_override=false; }
    std::puts("ok all UNIT flag combinations, clone, scene/lock/route persistence, transient exclusion, layers and no implicit shape conversion");
}
void quality() {
    constexpr int N=320;constexpr double f=1700,drive=7;
    for(int method=0;method<3;++method) {
        std::vector<double> values(N);float ring[osc_clean::TAPS]={},previous=0;int head=0;
        const int rate=method?64000:32000;
        for(int i=0;i<2*N;++i) {
            float out=0;
            for(int os=0;os<(method?2:1);++os) {
                const int t=i*(method?2:1)+os;
                float x=float(drive*std::sin(2*PI*f*t/rate));float y=method==2?osc_clean::fold_adaa(x,previous):osc_clean::fold(x);previous=x;
                if(method){ring[head]=y;out=osc_clean::decimate(ring,head)/osc_clean::HEADROOM;if(++head==osc_clean::TAPS)head=0;}else out=y;
            }
            if(i>=N)values[i-N]=out;
        }
        double total=0,alias=0,fundamental=0;
        for(int k=1;k<=N/2;++k) {
            double re=0,im=0;for(int i=0;i<N;++i){re+=values[i]*std::cos(2*PI*k*i/N);im+=values[i]*std::sin(2*PI*k*i/N);}double energy=re*re+im*im;total+=energy;if(k%17)alias+=energy;if(k==17)fundamental=2*std::sqrt(energy)/N;
        }
        std::printf("quality fold sine 1700Hz drive7, %s: illegal-bin/total %.3f dB, fundamental %.6f FS (headroom removed)\n",method==0?"naive 1x":method==1?"naive 2x+FIR":"ADAA 2x+FIR",10*std::log10(alias/total),fundamental);
    }
    volatile int checksum=0;
    for(bool clean:{false,true}) {
        O o;o.clean=clean;configure(o,20000,28000,8000,32767,7000,8000);q15 block[32];
        const auto start=std::chrono::steady_clock::now();for(int tick=0;tick<4000;++tick){o.control_tick();o.process(block,32);checksum+=block[17];}
        const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        std::printf("host cost %s, focus off, 128000 samples incl 1kHz control: %lld us (not ARM/fps)\n",clean?"clean":"legacy",(long long)us);
    }
    std::printf("cost checksum %d\n",int(checksum));
}
}
int main(int argc,char** argv) {
    if(argc>1&&!std::strcmp(argv[1],"--quality")){quality();return 0;}
    pitch_and_shapes();events_fold_bounds();ramps_and_drift();collector();persistence_layers();
    std::printf("osc clean core: 5/5 passed (%llu checks)\n",checks);
}
