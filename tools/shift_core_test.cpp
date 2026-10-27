// Actual portable SHIFT DSP and engine contracts. No platform renderer or device IO.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <cmath>
#include <complex>
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
constexpr double PI = 3.14159265358979323846;
using S = ShiftUnit;
using Pcm = std::vector<q15>;
using Bytes = std::vector<uint8_t>;
uint64_t checks = 0, compared = 0;
double worst_rejection = 200, lowest_gain = 10, highest_gain = 0;
int largest_tail = 0;
std::string path;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
void set(Unit& u, int p, int v) { u.params[p].base = q15(v); u.params[p].snap(); }
int raw(double hz) { return int(std::lround(std::sqrt(std::abs(hz) / 2000.0) * 32767)) * (hz < 0 ? -1 : 1); }
void configure(S& u, int shift, int feedback = 0, int mix = 32767, int damp = 0) {
    set(u,0,shift); set(u,1,feedback); set(u,2,mix); set(u,S::P_DAMP,damp); u.control_tick();
}
Pcm tone(double hz, int frames, double amplitude = 12000) {
    Pcm b(frames); for(int i=0;i<frames;++i) b[i]=q15(std::lround(amplitude*std::sin(2*PI*hz*i/ENGINE_SR))); return b;
}
Pcm noise(int frames) { Pcm b(frames); uint32_t r=1234567; for(auto& x:b) { r^=r<<13;r^=r>>17;r^=r<<5;x=q15(r); } return b; }
Pcm process(S& u, Pcm b) { u.process(b.data(),int(b.size())); return b; }
double amplitude(const Pcm& b, double hz, int skip=16000) {
    double re=0,im=0,ws=0; const int count=int(b.size())-skip;
    for(int i=0;i<count;++i) { const double w=.5-.5*std::cos(2*PI*i/(count-1)); const double a=2*PI*hz*(i+skip)/ENGINE_SR; re+=b[i+skip]*w*std::cos(a);im-=b[i+skip]*w*std::sin(a);ws+=w; }
    return 2*std::hypot(re,im)/ws;
}
Bytes read(){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void put(const Bytes& b){std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
void w16(Bytes& b,size_t p,int v){CHECK(p+2<=b.size());b[p]=uint8_t(v);b[p+1]=uint8_t(uint16_t(v)>>8);}
uint32_t r32(const Bytes& b,size_t p){CHECK(p+4<=b.size());return b[p]|uint32_t(b[p+1])<<8|uint32_t(b[p+2])<<16|uint32_t(b[p+3])<<24;}
size_t chunk(const Bytes& b,const char* id){for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;CHECK(false);return 0;}
// Frozen pre-DAMP audio/control implementation from units.cpp, 2026-09-25.
// Keep this independent of ShiftUnit::process; shared immutable SSB primitives
// and unchanged physical mappings are intentional. No damping branch exists here.
struct LegacyShift {
    shift_dsp::Allpass a_[4], b_[4];
    shift_dsp::Ramp increment_, feedback_amount_, mix_;
    uint32_t phase_ = 0;
    int32_t b_delay_ = 0, feedback_ = 0, input_dc_ = 0, output_dc_ = 0;
    bool ready_ = false;
    void control(int shift, int feedback, int mix) {
        const int32_t inc = int32_t(int64_t(S::shift_millihz(q15(shift))) *
            (int64_t(1) << 32) / (ENGINE_SR * 1000));
        const int32_t fb = int32_t(int64_t(S::feedback_permille(q15(feedback))) *
            (int64_t(1) << 30) / 1000);
        const int32_t amount = int32_t(int64_t(std::max(0,mix)) * (int64_t(1) << 30) / 32767);
        if (!ready_) { increment_.snap(inc); feedback_amount_.snap(fb); mix_.snap(amount); ready_=true; }
        else { increment_.set(inc); feedback_amount_.set(fb); mix_.set(amount); }
    }
    void process(q15* buf, int frames) {
        constexpr int64_t Q30 = int64_t(1) << 30;
        for (int n=0;n<frames;++n) {
            const int32_t dry=int32_t(buf[n])*256, inc=increment_.tick();
            const int32_t fb=feedback_amount_.tick(), mix=mix_.tick();
            const int32_t input=dry+int32_t(int64_t(feedback_)*fb/Q30);
            input_dc_+=(input-input_dc_)/1024;
            int32_t a=input-input_dc_, b=a;
            for(int s=0;s<4;++s){a=a_[s].tick(a,shift_dsp::A[s]);b=b_[s].tick(b,shift_dsp::B[s]);}
            const int32_t quadrature=b_delay_;b_delay_=b;
            const int32_t cosine=shift_dsp::sine(phase_+0x40000000u), sine=shift_dsp::sine(phase_);
            const int32_t shifted=shift_dsp::clamp((int64_t(a)*cosine-int64_t(quadrature)*sine)/Q30,16*shift_dsp::FS);
            phase_+=uint32_t(inc);output_dc_+=(shifted-output_dc_)/1024;
            const int32_t wet=shift_dsp::clamp(int64_t(shifted)-output_dc_,shift_dsp::FS-256);
            feedback_=wet;
            const int64_t output=int64_t(dry)+int64_t(wet-dry)*mix/Q30;
            buf[n]=trackr::fx::sat_q15(int32_t(output/256));
        }
    }
};
void damping() {
    static_assert(S::P_SHIFT==0&&S::P_FEEDBACK==1&&S::P_MIX==2&&S::P_DAMP==3&&S::P_COUNT==4,"append only");
    S metadata;CHECK(metadata.num_params==4&&metadata.params[S::P_DAMP].base==0&&!metadata.params[S::P_DAMP].bipolar);
    CHECK(!std::strcmp(metadata.params[S::P_DAMP].name,"DAMP"));
    const auto input=noise(24001);
    // Zero DAMP remains sample-exact against the frozen implementation under
    // bipolar SHIFT and feedback/mix automation, including interrupted ramps.
    S neutral;LegacyShift old;auto x=input,y=input;size_t at=0;
    for(int event=0;at<x.size();++event){
        const int sh=event%3==0?0:event&1?-32767:32767, fb=event%3?32767:0, mix=event%4?23001:0;
        const int n=int(std::min<size_t>(37+event%197,x.size()-at));
        configure(neutral,sh,fb,mix);old.control(sh,fb,mix);
        neutral.process(x.data()+at,n);old.process(y.data()+at,n);at+=n;
    }
    CHECK(x==y);
    // Steady zero feedback means all first-pass output is exact, even while
    // damping changes. Negative raw input is the same clamped open endpoint.
    S clean,filtered;configure(clean,3663);configure(filtered,3663,0,32767,32767);
    x=input;y=input;
    for(size_t p=0;p<x.size();){const int n=int(std::min<size_t>(53,x.size()-p));set(filtered,S::P_DAMP,(p&1)?0:32767);filtered.control_tick();clean.process(x.data()+p,n);filtered.process(y.data()+p,n);p+=n;}CHECK(x==y);
    S negative,open;configure(negative,3663,32767,32767,-32768);configure(open,3663,32767);CHECK(process(negative,input)==process(open,input));
    S dry;configure(dry,-32767,32767,0,32767);CHECK(process(dry,input)==input);
    // Nonzero alpha ramps are sample-clock invariant under repeated ticks;
    // this also exercises warm tracking through zero feedback and open DAMP.
    S whole,split;configure(whole,-32767,0,32767,32767);configure(split,-32767,0,32767,32767);x=input;y=input;at=0;
    for(int event=0;at<x.size();++event){
        const int n=int(std::min<size_t>(37+event%173,x.size()-at));
        for(S* u:{&whole,&split})configure(*u,event&1?32767:-32767,event%3?32767:0,32767,event%3==0?0:event%3==1?16384:32767);
        whole.process(x.data()+at,n);for(int i=0;i<n;++i){split.control_tick();split.process(y.data()+at+i,1);}at+=n;
    }CHECK(x==y);
    whole.reset();split.reset();CHECK(process(whole,input)==process(split,input));
    S fresh;for(int p=0;p<S::P_COUNT;++p)set(fresh,p,whole.params[p].base);whole.reset();CHECK(process(whole,input)==process(fresh,input));
    // The production Ramp used by alpha reaches BOTH endpoints in exactly 128
    // samples; repeated control ticks cannot restart it.
    shift_dsp::Ramp ramp;ramp.snap(1<<30);ramp.set(1<<24);for(int i=0;i<127;++i){ramp.set(1<<24);ramp.tick();}CHECK(ramp.left==1&&ramp.current>(1<<24));CHECK(ramp.tick()==(1<<24)&&ramp.left==0);ramp.set(1<<30);for(int i=0;i<128;++i)ramp.tick();CHECK(ramp.current==(1<<30));
    // Resolved lines: first wet translation stays; the repeated translations
    // lose high-frequency energy monotonically as feedback damping increases.
    double repeat[3], primary[3];const int damps[3]={0,16384,32767};
    const double shift=S::shift_millihz(q15(raw(150)))/1000.;
    for(int i=0;i<3;++i){S u;configure(u,raw(150),23000,32767,damps[i]);auto out=process(u,tone(1000,64000,1000));primary[i]=amplitude(out,1000+shift);repeat[i]=amplitude(out,1000+2*shift);}
    CHECK(repeat[0]>300&&repeat[1]<repeat[0]*.9&&repeat[1]>repeat[2]*2&&repeat[2]<repeat[0]*.12);
    CHECK(std::abs(primary[2]-primary[0])<primary[0]*.03);
    // One bounded full-range/max-feedback case per nonzero endpoint, not a
    // second large stress matrix. Follow the burst with four seconds of silence.
    int tail=0;
    for(int damp:{16384,32767}){S u;configure(u,-32767,32767,32767,damp);auto out=process(u,input);CHECK(std::all_of(out.begin(),out.end(),[](q15 v){return v>=-32767;}));out=process(u,Pcm(ENGINE_SR*4));int peak=0;for(auto it=out.end()-ENGINE_SR;it!=out.end();++it)peak=std::max(peak,std::abs(int(*it)));CHECK(peak<64);tail=std::max(tail,peak);}
    std::printf("ok damp metadata/legacy PCM/zero-fb/dry/partition/reset/ramp/bounds; repeat %.2f -> %.2f -> %.2f q15 (%.2f dB max damping), tail %d; sizeof=%zu payload=%zu\n",repeat[0],repeat[1],repeat[2],20*std::log10(repeat[2]/repeat[0]),tail,sizeof(S),sizeof(S)-sizeof(Unit));
}
void mappings() {
    static_assert(int(UnitKind::Strain)==20 && int(UnitKind::Shift)==21,"append-only kind");
    S u; CHECK(u.kind==UnitKind::Shift&&u.num_params==4&&u.params[0].bipolar&&!u.params[1].bipolar&&!u.params[2].bipolar&&!u.params[3].bipolar);
    CHECK(u.params[1].base==0&&u.params[2].base==32767&&std::abs(S::shift_millihz(u.params[0].base)-25000)<20);
    CHECK(S::shift_millihz(-32768)==-2000000&&S::shift_millihz(32767)==2000000&&S::shift_millihz(0)==0);
    int previous=0;for(int p=0;p<=32767;++p){const int hz=S::shift_millihz(q15(p));CHECK(hz>=previous);CHECK(S::shift_millihz(q15(-p))==-hz);CHECK(std::abs(hz-2000000.*p*p/(32767.*32767))<=.50001);CHECK(S::feedback_permille(q15(p))==int(std::lround(p*850./32767)));previous=hz;}
    CHECK(S::feedback_permille(-32768)==0);
    for(int i=0;i<=256;++i)CHECK(std::abs(shift_dsp::SINE[i]-std::sin(i*PI/512)*(1ll<<30))<=.50001);
    for(uint64_t p=0;p<(1ull<<32);p+=65537){CHECK(std::abs(shift_dsp::sine(uint32_t(p))/double(1ll<<30)-std::sin(2*PI*p/(1ull<<32)))<.000005);}
    for(int a:{-(1<<28),0,12345,1<<30})for(int b:{-(1<<28),0,2718,1<<30}) {shift_dsp::Ramp ramp;ramp.snap(a);ramp.set(b);for(int i=0;i<128;++i){const auto prior=ramp.current;ramp.set(b);ramp.tick();CHECK(b>=a?ramp.current>=prior:ramp.current<=prior);}CHECK(ramp.current==b&&ramp.left==0);}
    std::printf("ok mappings/carrier/ramp endpoints; default %.3f Hz; sizeof ShiftUnit=%zu, DSP payload=%zu bytes\n",S::shift_millihz(u.params[0].base)/1000.,sizeof(S),sizeof(S)-sizeof(Unit));
}
void spectra() {
    int cases=0;
    for(double f:{30.,50.,100.,220.,1000.,4000.,8000.,12000.,14000.})for(double hz:{-.5,.5,-25.,25.,-500.,500.,-2000.,2000.}) {
        if(std::abs(f+hz)<25 || f+std::abs(hz)>=15500)continue;
        S u;configure(u,raw(hz));
        // Same mHz mapping followed by Q32 integer increment truncation, not a
        // copied audio oracle. Hann projection at desired/opposite frequencies.
        const double increment=double(int64_t(S::shift_millihz(q15(raw(hz))))*(1ll<<32)/(ENGINE_SR*1000));
        const double shift=increment*ENGINE_SR/(1ull<<32);
        auto out=process(u,tone(f,112000));
        const double wanted=amplitude(out,std::abs(f+shift)),image=amplitude(out,std::abs(f-shift));
        const double rejection=20*std::log10(wanted/std::max(.000001,image));
        if(rejection<40)std::fprintf(stderr,"spectral failure f=%g shift=%.6f wanted=%g image=%g reject=%g\n",f,shift,wanted,image,rejection);
        CHECK(rejection>40);CHECK(wanted>10500&&wanted<12100);
        worst_rejection=std::min(worst_rejection,rejection);lowest_gain=std::min(lowest_gain,wanted/12000);highest_gain=std::max(highest_gain,wanted/12000);++cases;
    }
    // A sum of unrelated pitches: both move by the SAME Hz, not a ratio.
    S u;configure(u,raw(137));auto input=tone(311,112000,7000),second=tone(911,112000,6000);for(size_t i=0;i<input.size();++i)input[i]+=second[i];auto out=process(u,input);const double s=S::shift_millihz(q15(raw(137)))/1000.;CHECK(amplitude(out,311+s)>6900&&amplitude(out,911+s)>5900);CHECK(amplitude(out,911*(311+s)/311)<10);
    std::printf("ok %d signed SSB tone cases + two-tone translation; minimum image rejection %.2f dB, gain %.5f..%.5f\n",cases,worst_rejection,lowest_gain,highest_gain);
}
void boundaries_zero() {
    const auto input=noise(100003);
    for(int sh:{-32767,-16384,-519,0,519,3663,32767})for(int feedback:{0,24000,32767}) {
        S a,b;configure(a,sh,feedback,23001);configure(b,sh,feedback,23001);auto x=input,y=input;
        a.process(x.data(),int(x.size()));size_t at=0;while(at<y.size()){const int n=std::min<size_t>((at*13+11)%257+1,y.size()-at);b.control_tick();b.process(y.data()+at,n);at+=n;}
        CHECK(x==y);compared+=x.size();a.reset();CHECK(process(a,input)==x);compared+=x.size();
    }
    // Same automation timestamps, different process boundaries, including ramps
    // interrupted before completion and repeated control calls with no changes.
    S a,b;configure(a,32767,0,0);configure(b,32767,0,0);auto x=input,y=input;size_t at=0;
    for(int block=0;at<x.size();++block){const int n=std::min<size_t>(37+(block%201),x.size()-at);for(S* u:{&a,&b})configure(*u,(block&1)?-32767:32767,block%3?32767:0,(block&1)?32767:0);a.process(x.data()+at,n);for(int i=0;i<n;++i){b.control_tick();b.process(y.data()+at+i,1);}at+=n;}
    CHECK(x==y);compared+=x.size();
    S dry;configure(dry,32767,32767,0);CHECK(process(dry,input)==input);
    set(dry,2,32767);dry.control_tick();Pcm warm(128);dry.process(warm.data(),128);set(dry,2,0);dry.control_tick();dry.process(warm.data(),128);CHECK(process(dry,input)==input);
    S zero;configure(zero,0);auto sine=tone(1000,112000);auto z=process(zero,sine);CHECK(z!=sine);CHECK(amplitude(z,1000)>11970&&amplitude(z,1025)<1);Pcm silent(128000);auto tail=process(zero,silent);CHECK(std::all_of(tail.end()-32000,tail.end(),[](q15 v){return std::abs(int(v))<8;}));
    // Wet keeps running at mix zero: reopening matches always-wet state after the ramp.
    S wet,hidden;configure(wet,3663,20000);configure(hidden,3663,20000,0);process(wet,input);process(hidden,input);set(hidden,2,32767);hidden.control_tick();auto w=process(wet,sine),h=process(hidden,sine);CHECK(std::equal(w.begin()+128,w.end(),h.begin()+128));
    std::printf("ok reset/zero/DC/dry/live wet history; %llu exact split/reset PCM comparisons incl interrupted ramps\n",(unsigned long long)compared);
}
void stress_feedback() {
    for(int shift:{-32767,-3663,0,17,3663,32767})for(int signal=0;signal<4;++signal) {
        S u;configure(u,shift,32767);Pcm input(32000);for(int i=0;i<32000;++i)input[i]=signal==0?32767:signal==1?-32768:signal==2?(i&1?-32768:32767):(i%101==0?32767:0);
        auto out=process(u,input);CHECK(std::all_of(out.begin(),out.end(),[](q15 v){return v>=-32767&&v<=32767;}));
        Pcm silent(ENGINE_SR*4);out=process(u,silent);int peak=0;for(auto it=out.end()-ENGINE_SR;it!=out.end();++it)peak=std::max(peak,std::abs(int(*it)));largest_tail=std::max(largest_tail,peak);if(peak>64)std::fprintf(stderr,"tail shift=%d signal=%d peak=%d\n",shift,signal,peak);CHECK(peak<=64);
    }
    // Actual recirculation produces successive translated lines, unlike mixing
    // unshifted feedback into a standalone ring carrier.
    S u;configure(u,raw(150),23000);auto out=process(u,tone(1000,112000,1000));CHECK(amplitude(out,1150)>900&&amplitude(out,1300)>300&&amplitude(out,1450)>100);
    std::printf("ok 24 full-scale/extreme/max-feedback burst+4s release cases; final-second peak <=%d q15; repeated-shift spectrum\n",largest_tail);
}
void persistence_layers_engine() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();auto* u=new S;delete a->replace_unit(0,0,u);
    const int values[4]={-17421,26731,20212,18432};for(int p=0;p<4;++p){set(*u,p,values[p]);u->params[p].src=ModSource(1+p);u->params[p].mod_depth=q15(-13000+p*2300);}u->bypass=true;
    a->capture_scene(0,0);set(*u,0,16321);set(*u,S::P_DAMP,30000);a->capture_scene(0,1);set(*u,0,values[0]);set(*u,S::P_DAMP,values[3]);CHECK(a->set_step_lock(0,0,0,0,-32123));CHECK(a->set_step_lock(0,0,0,S::P_DAMP,26321));a->macro_pads[0].route_count=1;a->macro_pads[0].routes[0]={0,0,0,-32767,32767};
    CHECK(patchio::save(*a,path.c_str()));const auto saved=read();CHECK(saved[chunk(saved,"UNIT")+8]==0);CHECK(patchio::load(*b,path.c_str()));auto* got=static_cast<S*>(b->chains[0].units[0]);CHECK(got&&got->kind==UnitKind::Shift&&got->num_params==4&&got->bypass);
    for(int p=0;p<4;++p){CHECK(got->params[p].base==u->params[p].base&&got->params[p].src==u->params[p].src&&got->params[p].mod_depth==u->params[p].mod_depth);CHECK(got->params[p].bipolar==(p==0));}
    CHECK(b->pattern.steps[0][0].locks[0].value==-32123&&b->macro_pads[0].route_count==1);CHECK(b->scenes[0][0].kind[0]==UnitKind::Shift&&b->scenes[0][0].base[0][0]==values[0]);CHECK(b->scenes[0][0].base[0][S::P_DAMP]==values[3]&&b->pattern.steps[0][0].locks[1].param==S::P_DAMP&&b->pattern.steps[0][0].locks[1].value==26321);
    CHECK(patchio::save(*b,path.c_str()));CHECK(read()==saved);
    process(*got,noise(10000));std::unique_ptr<Unit> clone(clone_unit(got));CHECK(clone&&clone->kind==UnitKind::Shift&&clone->bypass);S reference;for(int p=0;p<4;++p){CHECK(clone->params[p].base==got->params[p].base&&clone->params[p].src==got->params[p].src&&clone->params[p].mod_depth==got->params[p].mod_depth);set(reference,p,got->params[p].base);}CHECK(process(*static_cast<S*>(clone.get()),noise(1024))==process(reference,noise(1024)));
    b->apply_scene_morph(0,16384);CHECK(got->params[0].base>values[0]&&got->params[0].base<16321);CHECK(got->params[S::P_DAMP].base>values[3]&&got->params[S::P_DAMP].base<30000);
    Param& p=got->params[0];p.base=-5000;p.mod_depth=32767;p.snap();p.resolve(-32767);CHECK(p.effective==-32767);p.snap();p.resolve(32767);CHECK(p.effective==27766);p.step_lock=-12345;p.step_lock_override=true;CHECK(p.control_source()==-12345);p.audition=8123;p.audition_override=true;CHECK(p.control_source()==8123);p.performance=23123;p.performance_override=true;CHECK(p.control_source()==23123);p.temp=-32767;p.temp_override=true;CHECK(p.control_source()==-32767);
    // Bypass is engine-owned; exact output equality against the same engine
    // without SHIFT. Control resolution still runs; audio history is paused.
    auto dry_engine=std::make_unique<Engine>(),bypass_engine=std::make_unique<Engine>();for(Engine* e:{dry_engine.get(),bypass_engine.get()}){auto* osc=new OscUnit;set(*osc,OscUnit::P_GAIN,4000);delete e->replace_unit(0,0,osc);}auto* bypass=new S;bypass->bypass=true;delete bypass_engine->replace_unit(0,1,bypass);Pcm left(20000),right(20000);dry_engine->render(left.data(),10000);bypass_engine->render(right.data(),10000);CHECK(left==right);
    auto* live=static_cast<S*>(bypass_engine->chains[0].units[1]);live->bypass=false;live->params[0].src=ModSource::SigilA;live->params[0].mod_depth=20000;live->params[0].base=-20000;live->params[0].snap();bypass_engine->sigils[0].depth=32767;bypass_engine->sigils[0].drawn=true;for(auto& v:bypass_engine->sigils[0].table)v=32767;bypass_engine->render(right.data(),64);CHECK(live->params[0].target>-10&&live->params[0].target<10);
    set(*live,S::P_DAMP,0);live->params[S::P_DAMP].src=ModSource::SigilA;live->params[S::P_DAMP].mod_depth=20000;bypass_engine->render(right.data(),64);CHECK(live->params[S::P_DAMP].target>19000);
    CHECK(bypass_engine->set_step_lock(0,0,1,0,-31111));CHECK(bypass_engine->set_step_lock(0,0,1,S::P_DAMP,25321));CHECK(bypass_engine->audition_sequence_step(0,0));CHECK(live->params[0].audition_override&&live->params[0].audition==-31111);CHECK(live->params[S::P_DAMP].audition_override&&live->params[S::P_DAMP].audition==25321);bypass_engine->cancel_sequence_audition();CHECK(!live->params[0].audition_override&&!live->params[S::P_DAMP].audition_override);
    std::puts("ok exact SNC7 save/load/resave, clone reset, bipolar mod/layers, locks/scenes/macro routes, real engine bypass+mod+audition");
}
void old_patches() {
    auto target=std::make_unique<Engine>();
    for(int version=1;version<=6;++version){Bytes old={'S','N','C',uint8_t('0'+version),uint8_t(version)};for(int c=0;c<NUM_CHAINS;++c){old.insert(old.end(),{255,127});if(version>=2)old.insert(old.end(),{0,0});old.push_back(0);size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);if(c==0){old[first]=uint8_t(UnitKind::Osc);old[first+2]=2;const int v[6]={16384,8192,12000,0,0,0};for(int p=0;p<6;++p)w16(old,first+3+p*5,v[p]);}}
        old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);if(version>=3){old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0);}if(version>=4)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);if(version>=5){old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0});}if(version>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);put(old);CHECK(patchio::load(*target,path.c_str()));auto* osc=static_cast<OscUnit*>(target->chains[0].units[0]);CHECK(osc&&osc->kind==UnitKind::Osc&&!osc->shape_morph&&osc->wave==OscUnit::Wave::Saw);OscUnit ref;ref.shape_morph=false;ref.wave=OscUnit::Wave::Saw;const int v[6]={16384,8192,12000,0,0,0};for(int p=0;p<6;++p)set(ref,p,v[p]);osc->control_tick();ref.control_tick();Pcm a(4096),b(4096);osc->process(a.data(),4096);ref.process(b.data(),4096);CHECK(a==b);}
    auto a=std::make_unique<Engine>();delete a->replace_unit(0,0,new S);CHECK(patchio::save(*a,path.c_str()));auto bad=read();bad[chunk(bad,"CORE")+8+5]=255;put(bad);Unit* sentinel=target->chains[0].units[0];CHECK(!patchio::load(*target,path.c_str()));CHECK(target->chains[0].units[0]==sentinel);
    // An actual three-param capture reproduces historical zero-filled SNC7 slots.
    auto* legacy = static_cast<S*>(a->chains[0].units[0]);legacy->num_params=3;configure(*legacy,-14321,29000,21345);a->capture_scene(0,0);
    CHECK(patchio::save(*a,path.c_str()));const auto old_shift=read();const size_t slot3=chunk(old_shift,"CORE")+8+5+3+3*5;for(size_t i=slot3;i<slot3+5;++i)CHECK(old_shift[i]==0);
    CHECK(patchio::load(*target,path.c_str()));auto* loaded=static_cast<S*>(target->chains[0].units[0]);CHECK(loaded->num_params==4&&loaded->params[S::P_DAMP].base==0&&loaded->params[S::P_DAMP].src==ModSource::None&&loaded->params[S::P_DAMP].mod_depth==0);CHECK(target->scenes[0][0].base[0][S::P_DAMP]==0);
    CHECK(patchio::save(*target,path.c_str()));CHECK(read()==old_shift);CHECK(process(*legacy,noise(4096))==process(*loaded,noise(4096)));
    std::puts("ok SNC1..6 oscillator PCM / unknown kind rollback / old three-param SHIFT zero-slot SNC7 roundtrip");
}
void wav(const std::string& name,const Pcm& data){std::ofstream f(name,std::ios::binary);auto u16=[&](uint16_t x){f.put(char(x));f.put(char(x>>8));};auto u32=[&](uint32_t x){u16(uint16_t(x));u16(uint16_t(x>>16));};f.write("RIFF",4);u32(36+data.size()*2);f.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(ENGINE_SR);u32(ENGINE_SR*2);u16(2);u16(16);f.write("data",4);u32(data.size()*2);for(q15 x:data)u16(uint16_t(x));CHECK(f.good());}
void demos(const std::string& dir){mkdir("ui_mockups/shift_core",0755);mkdir(dir.c_str(),0755);Pcm in(ENGINE_SR*4);for(size_t i=0;i<in.size();++i){double t=double(i)/ENGINE_SR;double env=std::min(1.,t/.01)*std::min(1.,(4-t)/.2);in[i]=q15(std::lround(7000*env*std::sin(2*PI*110*t+1.7*std::sin(2*PI*220*t))));}wav(dir+"/01-fm-like-input.wav",in);const double hz[4]={.5,25,700,-350};const int fb[4]={0,0,0,27000};const char* names[4]={"02-small-plus-0p5Hz.wav","03-plus-25Hz.wav","04-metal-plus-700Hz.wav","05-feedback-minus-350Hz.wav"};for(int i=0;i<4;++i){S u;configure(u,raw(hz[i]),fb[i],i==0?16384:32767);wav(dir+"/"+names[i],process(u,in));}std::puts("wrote five 4-second mono 32kHz actual DSP WAVs (not listening/hardware proof)");}
}
int main(int argc,char** argv){path="/tmp/seance-shift-"+std::to_string((long)getpid())+".snc";const auto start=std::chrono::steady_clock::now();const bool focused=argc==2&&!std::strcmp(argv[1],"--damp-only");if(!focused){mappings();spectra();boundaries_zero();stress_feedback();}damping();persistence_layers_engine();old_patches();if(argc==3&&!std::strcmp(argv[1],"--demos"))demos(argv[2]);std::remove(path.c_str());std::printf("shift core: %d/%d passed (%llu checks), %.3fs host total\n",focused?3:7,focused?3:7,(unsigned long long)checks,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());}
