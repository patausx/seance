// Native FM expansion proof; never links the platform UI or changes sample-bank IO.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include "engine/fm_extended.h"
#include "fm_legacy_reference.h"
#include <algorithm>
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
using F = FmUnit;
using Old = fm_legacy_reference::LegacyFmUnit;
using Bytes = std::vector<uint8_t>;
uint64_t checks=0, twin_samples=0, oracle_samples=0;
std::string path;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
void set(Unit& u,int p,int v) { u.params[p].base=q15(v);u.params[p].snap(); }
std::vector<q15> pcm(Unit& u,int n) { std::vector<q15> out(n);u.process(out.data(),n);return out; }
void compare(Unit& a,Unit& b,int n) { const auto x=pcm(a,n),y=pcm(b,n);CHECK(x==y);twin_samples+=n; }
Bytes read() { std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}}; }
void put(const Bytes& b) { std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good()); }
uint32_t r32(const Bytes&b,size_t at) { CHECK(at+4<=b.size());return b[at]|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24; }
void w16(Bytes&b,size_t at,int v) { CHECK(at+2<=b.size());b[at]=uint8_t(v);b[at+1]=uint8_t(uint16_t(v)>>8); }
size_t chunk(const Bytes&b,const char* id) { for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;CHECK(false);return 0; }
Bytes unit_first(Bytes b) { const size_t p=chunk(b,"UNIT"),n=8+r32(b,p+4);Bytes unit(b.begin()+p,b.begin()+p+n);b.erase(b.begin()+p,b.begin()+p+n);b.insert(b.begin()+5,unit.begin(),unit.end());return b; }
int wave(int which,uint32_t phase) { const int p=int(phase>>16);const int t=p<32768?p*2-32768:32767-(p-32768)*2;if(which==1)return t;if(which==2)return p-32768;if(which==3)return phase<0x80000000u?-32767:32767;return int((int64_t(t)*(65536-std::abs(t)))>>15); }
void configure(Unit&u,int config) {
    static const int values[4][10]={{16384,8192,13926,13762,12000,22000,0,0,0,0},{0,0,0,0,32767,32767,0,0,-32767,0},{32767,32767,32767,32767,0,32767,32767,32767,32767,32767},{23981,12345,23671,9517,31337,16813,1137,861,139,-917}};
    for(int p=0;p<10;++p)set(u,p,values[config%4][p]);
    if(config>=4) { set(u,F::P_ATTACK,(config&1)?0:16900);set(u,F::P_FEEDBACK_FALL,(config&2)?0:17377); }
    u.control_tick();
}
void legacy_neutral() {
    F fresh;CHECK(fresh.extended&&fresh.num_params==12);CHECK(fresh.params[10].bipolar&&fresh.params[10].base==0);CHECK(F::pitch_env_samples(fresh.params[11].base)==3200);
    for(int cfg=0;cfg<8;++cfg)for(int policy=0;policy<8;++policy)for(bool ext:{false,true}) {
        Old old;F now;now.set_extended(ext);old.phase_reset=now.phase_reset=policy&1;old.alias_safe=now.alias_safe=policy&2;old.drone=now.drone=policy&4;
        configure(old,cfg);configure(now,cfg);
        if(!ext) { now.carrier_wave=F::Wave::Pulse;now.mod_wave=F::Wave::Saw;set(now,10,32767);set(now,11,32767); }
        old.trigger();now.trigger();
        for(int block=0;block<128;++block) {
            if(block==9||block==61) { set(old,0,block==9?31999:183);set(now,0,block==9?31999:183);set(old,8,block==9?-23839:18931);set(now,8,block==9?-23839:18931); }
            if(block==21||block==85) { old.drone=now.drone=!old.drone; }
            if(block==13||block==63||block==94) { old.trigger();now.trigger(); }
            old.control_tick();now.control_tick();compare(old,now,32);
            CHECK(old.visual_phase()==now.visual_phase());CHECK(old.visual_envelope()==now.visual_envelope());CHECK(old.visual_index_envelope()==now.visual_index_envelope());CHECK(old.visual_feedback_envelope()==now.visual_feedback_envelope());
        }
        old.reset();now.reset();old.trigger();now.trigger();compare(old,now,2048);
    }
    std::puts("ok exact frozen original FM / ten params / all phase-guard-drone policies / new neutral + ignored dormant controls");
}
void mappings_envelope() {
    CHECK(F::pitch_env_cents(-32768)==-4800&&F::pitch_env_cents(-32767)==-4800&&F::pitch_env_cents(32767)==4800&&F::pitch_env_cents(0)==0);
    CHECK(F::pitch_env_samples(-1)==32&&F::pitch_env_samples(0)==32&&F::pitch_env_samples(32767)==64000&&F::pitch_env_samples(F::DEFAULT_PITCH_TIME)==3200);
    uint32_t previous=0;for(int p=0;p<=32767;++p) { const auto n=F::pitch_env_samples(q15(p));CHECK(n>=previous&&n>=32&&n<=64000);previous=n;CHECK(F::pitch_env_cents(q15(-p))==-F::pitch_env_cents(q15(p))); }
    // Check offline constants against independent double math; production never uses floating-point here.
    for(int i=0;i<=256;++i)CHECK(std::abs(double(fm_extended::EXP2_Q24[i])-std::pow(2.0,i/256.0)*(1<<24))<=0.500001);
    for(int i=0;i<=32;++i)CHECK(std::abs(double(fm_extended::TIME_SAMPLES[i])-32*std::pow(2000.0,i/32.0))<=0.500001);
    for(int depth:{-32768,-32767,-16537,-1,0,1,13619,32767})for(int duration:{0,1,8192,int(F::DEFAULT_PITCH_TIME),32767}) {
        F a,b;for(F*u:{&a,&b}) { set(*u,10,depth);set(*u,11,duration);u->control_tick();u->trigger(); }
        const int cents=F::pitch_env_cents(q15(depth));const uint32_t total=cents?F::pitch_env_samples(q15(duration)):0;
        CHECK(a.visual_pitch_cents()==cents&&a.visual_pitch_remaining()==total);
        const auto x=pcm(a,int(total)+33);std::vector<q15> y(x.size());uint32_t at=0;
        while(at<y.size()) { const uint32_t n=std::min<uint32_t>((at*17+13)%97+1,y.size()-at);b.process(y.data()+at,n);at+=n; }
        CHECK(x==y&&a.visual_pitch_cents()==0&&b.visual_pitch_remaining()==0);twin_samples+=x.size();
        a.reset();a.trigger();int last=std::abs(cents);for(uint32_t i=0;i<total;++i) { CHECK(a.visual_pitch_remaining()==total-i);const int expected=int(std::lround(double(cents)*(total-i)/total));CHECK(std::abs(a.visual_pitch_cents()-expected)<=1);CHECK(std::abs(a.visual_pitch_cents())<=last);last=std::abs(a.visual_pitch_cents());q15 v;a.process(&v,1); }CHECK(a.visual_pitch_cents()==0&&a.visual_pitch_remaining()==0);
    }
    std::puts("ok signed +/-48st / deterministic monotone 1..2000ms / exact frame endpoint / linear musical decay / arbitrary partitions");
}
uint32_t factor_at(int cents,uint32_t elapsed,uint32_t total) {
    const int64_t initial=int64_t(cents)*(1<<24)/1200;const int sign=initial<0?-1:1;const int64_t magnitude=std::abs(initial);
    const int64_t log=elapsed>=total?0:sign*(magnitude-int64_t(elapsed)*magnitude/total);
    const int octave=int(std::floor(double(log)/(1<<24)));const uint32_t fraction=uint32_t(log)&0xffffffu;
    const uint32_t index=fraction>>16,rem=fraction&65535u;
    const uint32_t a=uint32_t(std::llround(std::pow(2.0,index/256.0)*(1<<24))),b=uint32_t(std::llround(std::pow(2.0,(index+1)/256.0)*(1<<24)));
    uint32_t f=a+uint32_t(uint64_t(b-a)*rem/65536u);return octave<0?f>>-octave:f<<octave;
}
void waves_and_pitch_oracle() {
    for(int c=0;c<4;++c)for(int m=0;m<4;++m)for(bool drone:{false,true})for(int extreme=0;extreme<3;++extreme) {
        F u;u.carrier_wave=F::Wave(c);u.mod_wave=F::Wave(m);u.drone=drone;u.alias_safe=extreme==2;set(u,0,extreme==2?32767:4317);set(u,1,extreme==1?0:32767);set(u,8,extreme==1?-32767:12345);set(u,2,extreme?32767:13331);set(u,3,extreme==1?0:32767);set(u,4,32767);set(u,5,32767);set(u,10,extreme==1?-32767:32767);set(u,11,0);u.control_tick();u.trigger();
        const int n=1024;auto output=pcm(u,n);uint32_t cp=0,mp=0;int last=0,env=drone?0:32767;const uint32_t base=fm_legacy_reference::pitch_phase_increment(u.params[0].effective,true);
        for(int i=0;i<n;++i) {
            uint32_t ci=base;if(!drone&&i<32)ci=uint32_t(std::min<uint64_t>((uint64_t(base)*factor_at(F::pitch_env_cents(u.params[10].base),i,32))>>24,UINT32_MAX));
            const uint32_t mi=Old::modulation_increment(ci,u.params[1].effective,u.params[8].effective,drone,u.alias_safe);mp+=mi;
            const uint32_t feedback=uint32_t(int64_t(last)*u.params[3].effective*2);last=wave(m,mp+feedback);const int modulation=int((int64_t(last)*u.params[2].effective*8)>>15);cp+=ci;const int carrier=wave(c,cp+uint32_t(int64_t(modulation)*32768));const q15 expected=trackr::fx::sat_q15(int((int64_t(carrier)*env*32767)>>30));CHECK(output[i]==expected);++oracle_samples;
            if(drone)env=std::min(32767,env+64);else if(env>0)env=std::max(0,env-std::max(1,(env*16)>>15));
        }
        CHECK(u.visual_phase()==uint16_t(cp>>16)&&u.visual_modulator()==last);
        F split;split.carrier_wave=u.carrier_wave;split.mod_wave=u.mod_wave;split.drone=drone;split.alias_safe=u.alias_safe;for(int p=0;p<12;++p)set(split,p,u.params[p].base);split.control_tick();split.trigger();std::vector<q15> out(n);int at=0;for(int size:{1,17,63,211,732}) { split.process(out.data()+at,size);at+=size; }CHECK(at==n&&output==out);
    }
    // Continuous-pitch oracle across many durations and live base-note updates;
    // base updates stay on the old note grid, only the latched envelope is smooth.
    for(int depth:{-32767,-8173,16237,32767})for(int time:{0,7000,19824,32767}) {
        F u;set(u,0,5000);set(u,1,12000);set(u,2,0);set(u,3,0);set(u,4,32767);set(u,5,32767);set(u,10,depth);set(u,11,time);u.control_tick();u.trigger();const uint32_t n=F::pitch_env_samples(q15(time));uint32_t phase=0,base=fm_legacy_reference::pitch_phase_increment(u.params[0].effective,true);
        for(uint32_t i=0;i<n+33;++i) { if(i==n/2) { set(u,0,11700);u.control_tick();base=fm_legacy_reference::pitch_phase_increment(u.params[0].effective,true); }uint32_t inc=i<n?uint32_t(std::min<uint64_t>((uint64_t(base)*factor_at(F::pitch_env_cents(q15(depth)),i,n))>>24,UINT32_MAX)):base;phase+=inc;q15 v;u.process(&v,1);CHECK(u.visual_phase()==uint16_t(phase>>16));++oracle_samples; }
    }
    std::puts("ok independent raw operator/feedback/phase PCM oracle / all16 wave pairs / strike+drone+guard/extremes / smooth pitch and live base-note updates");
}
void live_latches() {
    F a,b;for(F*u:{&a,&b}) { set(*u,10,32767);set(*u,11,19824);u->phase_reset=true;u->control_tick();u->trigger(); }compare(a,b,47);const auto left=a.visual_pitch_remaining();
    set(a,10,-16384);set(a,11,0);a.control_tick();CHECK(a.visual_pitch_remaining()==left);compare(a,b,777);a.trigger();CHECK(a.visual_pitch_cents()==F::pitch_env_cents(-16384)&&a.visual_pitch_remaining()==32);pcm(a,32);CHECK(a.visual_pitch_remaining()==0);
    a.drone=true;pcm(a,13);CHECK(a.visual_pitch_cents()==0);const auto phase=a.visual_phase();a.trigger();CHECK(a.visual_phase()==phase&&a.visual_pitch_remaining()==0);a.drone=false;pcm(a,1);CHECK(a.visual_pitch_remaining()==0);a.trigger();CHECK(a.visual_pitch_remaining()==32);a.set_extended(false);CHECK(!a.extended&&a.num_params==10&&a.params[10].base==-16384&&a.params[11].base==0&&a.visual_pitch_remaining()==0);a.set_extended(true);CHECK(a.num_params==12&&a.params[10].base==-16384);a.reset();CHECK(a.visual_pitch_remaining()==0);
    // First sample of each live transition is the exact old mixture, even if
    // another edit interrupts the64-frame fade; no phase reset/old-target jump.
    for(bool mod:{false,true})for(int next=1;next<4;++next) {
        F old,changed;for(F*u:{&old,&changed}) { u->drone=true;set(*u,2,mod?18000:0);set(*u,3,0);u->control_tick();pcm(*u,701); }
        if(mod)changed.mod_wave=F::Wave(next);else changed.carrier_wave=F::Wave(next);compare(old,changed,1);CHECK(old.visual_phase()==changed.visual_phase());
    }
    // Verify actual operator crossfade against separate weighted-wave oracle.
    for(bool mod:{false,true}) {
        F u;u.drone=true;set(u,0,7123);set(u,2,0);set(u,3,0);set(u,5,32767);u.control_tick();pcm(u,700);const uint32_t ci=fm_legacy_reference::pitch_phase_increment(u.params[0].effective,true),mi=Old::modulation_increment(ci,u.params[1].effective,u.params[8].effective,true,false);uint32_t phase=(mod?mi:ci)*700u;
        int weights[4]={65536,0,0,0},start[4]={65536,0,0,0},target=0,remaining=0;
        for(int i=0;i<220;++i) {
            if(i==0||i==17||i==43||i==135) { target=i==0?2:i==17?3:i==43?1:0;for(int j=0;j<4;++j)start[j]=weights[j];remaining=64;if(mod)u.mod_wave=F::Wave(target);else u.carrier_wave=F::Wave(target); }
            phase+=mod?mi:ci;int value=wave(target,phase);if(remaining) { int64_t sum=0;for(int j=0;j<4;++j)sum+=int64_t(weights[j])*wave(j,phase);value=trackr::fx::sat_q15(int(sum>>16));const int elapsed=65-remaining;for(int j=0;j<4;++j)weights[j]=start[j]+((j==target?65536:0)-start[j])*elapsed/64;--remaining; }
            q15 out;u.process(&out,1);if(mod)CHECK(u.visual_modulator()==value);else CHECK(out==trackr::fx::sat_q15(int((int64_t(value)*32767*32767)>>30)));
        }
    }
    std::puts("ok per-strike depth/time latching / retrigger/reset/drone dormancy / nonrewriting opt-in / exact64sample live and interrupted wave transitions");
}
void roundtrip_and_legacy() {
    auto source=std::make_unique<Engine>(),target=std::make_unique<Engine>();auto*u=new F;delete source->replace_unit(0,0,u);u->phase_reset=u->alias_safe=true;u->carrier_wave=F::Wave::Saw;u->mod_wave=F::Wave::Pulse;
    for(int p=0;p<12;++p) { set(*u,p,p==10?-17131:p==8?-999:1700+p*1913);u->params[p].src=ModSource(1+p%5);u->params[p].mod_depth=q15(-4300+p*521); }
    source->capture_scene(0,0);set(*u,10,13000);set(*u,11,29000);source->capture_scene(0,1);set(*u,10,-17131);set(*u,11,22743);CHECK(source->set_step_lock(0,0,0,10,-7000));CHECK(source->set_step_lock(0,0,0,11,31931));CHECK(source->set_step_lock(0,1,0,8,761));source->macro_pads[0].route_count=3;source->macro_pads[0].routes[0]={0,0,10,-32767,32767};source->macro_pads[0].routes[1]={0,0,11,0,32767};source->macro_pads[0].routes[2]={0,0,8,-32767,32767};
    CHECK(patchio::save(*source,path.c_str()));const Bytes saved=read();CHECK(saved[chunk(saved,"UNIT")+8]==uint8_t(0x80|2<<3|3<<5|3));
    for(bool reversed:{false,true}) {
        put(reversed?unit_first(saved):saved);CHECK(patchio::load(*target,path.c_str()));auto*got=static_cast<F*>(target->chains[0].units[0]);CHECK(got&&got->extended&&got->num_params==12&&got->carrier_wave==F::Wave::Saw&&got->mod_wave==F::Wave::Pulse&&got->phase_reset&&got->alias_safe);
        for(int p=0;p<12;++p) { CHECK(got->params[p].base==u->params[p].base);CHECK(got->params[p].src==u->params[p].src&&got->params[p].mod_depth==u->params[p].mod_depth); }
        CHECK(target->pattern.steps[0][0].lock_count==2&&target->macro_pads[0].route_count==3);for(int scene=0;scene<2;++scene)for(int p=10;p<12;++p)CHECK(target->scenes[0][scene].base[0][p]==source->scenes[0][scene].base[0][p]);
        std::unique_ptr<Unit> copy(clone_unit(got));auto*fm=static_cast<F*>(copy.get());CHECK(fm&&fm->extended&&fm->carrier_wave==got->carrier_wave&&fm->mod_wave==got->mod_wave&&fm->num_params==12);for(int p=0;p<12;++p)CHECK(fm->params[p].base==got->params[p].base&&fm->params[p].src==got->params[p].src&&fm->params[p].mod_depth==got->params[p].mod_depth);got->control_tick();fm->control_tick();got->trigger();fm->trigger();compare(*got,*fm,4096);
        target->apply_scene_morph(0,16384);CHECK(got->params[10].base>-17131&&got->params[10].base<13000&&got->params[11].base>u->params[11].base&&got->params[11].base<29000);
    }
    for(bool reversed:{false,true}) {
        Bytes old=saved;old[chunk(old,"UNIT")+8]&=0x7f;put(reversed?unit_first(old):old);CHECK(patchio::load(*target,path.c_str()));auto*got=static_cast<F*>(target->chains[0].units[0]);CHECK(!got->extended&&got->num_params==10&&got->carrier_wave==F::Wave::Sine&&got->mod_wave==F::Wave::Sine);CHECK(got->params[10].base==0&&got->params[10].bipolar&&got->params[11].base==F::DEFAULT_PITCH_TIME);for(int p=10;p<12;++p)CHECK(got->params[p].src==ModSource::None&&got->params[p].mod_depth==0);CHECK(target->pattern.steps[0][0].lock_count==0&&target->pattern.steps[0][1].lock_count==1&&target->macro_pads[0].route_count==1&&target->macro_pads[0].routes[0].param==8);for(int scene=0;scene<2;++scene) { CHECK(target->scenes[0][scene].base[0][10]==0&&target->scenes[0][scene].base[0][11]==F::DEFAULT_PITCH_TIME); }
        Old ref;ref.phase_reset=ref.alias_safe=true;for(int p=0;p<10;++p)set(ref,p,got->params[p].base);ref.control_tick();got->control_tick();ref.trigger();got->trigger();compare(ref,*got,4096);CHECK(patchio::save(*target,path.c_str()));const auto resaved=read();CHECK(resaved[chunk(resaved,"UNIT")+8]==3);CHECK(patchio::load(*target,path.c_str()));got=static_cast<F*>(target->chains[0].units[0]);std::unique_ptr<Unit> cp(clone_unit(got));CHECK(!static_cast<F*>(cp.get())->extended&&cp->num_params==10);got->set_extended(true);CHECK(got->params[10].base==0&&got->params[11].base==F::DEFAULT_PITCH_TIME);
    }
    // Historical fixed-six formats; all upper flag bits are untrusted even in SNC4..6.
    for(int version=1;version<=6;++version) {
        Bytes old={'S','N','C',uint8_t('0'+version),uint8_t(version)};const int values[]={11000,16000,17000,8100,26000,31000};
        for(int c=0;c<NUM_CHAINS;++c) { old.insert(old.end(),{255,127});if(version>=2)old.insert(old.end(),{0,0});old.push_back(0);const size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);if(!c) { old[first]=uint8_t(UnitKind::Fm);for(int p=0;p<6;++p)w16(old,first+3+p*5,values[p]); } }
        old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);if(version>=3) { old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0); }if(version>=4) { const size_t flags=old.size();old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);old[flags]=0xff; }if(version>=5) { old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0}); }if(version>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
        put(old);CHECK(patchio::load(*target,path.c_str()));auto*got=static_cast<F*>(target->chains[0].units[0]);CHECK(got&&!got->extended&&got->num_params==10&&got->carrier_wave==F::Wave::Sine&&got->mod_wave==F::Wave::Sine);Old ref;ref.phase_reset=ref.alias_safe=ref.drone=version>=4;for(int p=0;p<6;++p)set(ref,p,values[p]);ref.control_tick();got->control_tick();ref.trigger();got->trigger();compare(ref,*got,8192);
    }
    // Transactional truncated-file rejection remains intact.
    Bytes bad=saved;bad.resize(bad.size()-1);put(bad);Unit*sentinel=target->chains[0].units[0];CHECK(!patchio::load(*target,path.c_str())&&target->chains[0].units[0]==sentinel);
    std::puts("ok SNC7 twelve params/policies/shapes/clone/scenes/locks/routes / reversed chunks / legacy padding canonicalization+resave / SNC1..6 exact PCM / rollback");
}
void layers() {
    F u;Param&depth=u.params[10];depth.base=0;depth.src=ModSource::SigilA;depth.mod_depth=32767;depth.resolve(-32767);CHECK(depth.effective<0);depth.slew_ready=false;depth.step_lock=-8192;depth.step_lock_override=true;depth.mod_depth=0;depth.resolve(0);CHECK(depth.effective==-8192);u.control_tick();u.trigger();CHECK(u.visual_pitch_cents()==F::pitch_env_cents(-8192));const auto cents=u.visual_pitch_cents();depth.step_lock_override=false;depth.slew_ready=false;depth.resolve(0);u.control_tick();CHECK(u.visual_pitch_cents()==cents);u.trigger();CHECK(u.visual_pitch_cents()==0);
    depth.performance=-16384;depth.performance_override=true;depth.audition=1717;depth.audition_override=true;CHECK(depth.control_source()==-16384);depth.temp=8192;depth.temp_override=true;depth.slew_ready=false;depth.resolve(0);u.trigger();CHECK(u.visual_pitch_cents()==F::pitch_env_cents(8192));CHECK(depth.base==0);
    auto e=std::make_unique<Engine>();auto*fm=new F;delete e->replace_unit(0,0,fm);CHECK(e->set_step_lock(0,0,0,10,-32767)&&e->set_step_lock(0,0,0,11,0));CHECK(e->audition_sequence_step(0,0));CHECK(fm->visual_pitch_cents()==-4800&&fm->visual_pitch_remaining()==32);e->cancel_sequence_audition();CHECK(fm->visual_pitch_remaining()==32&&fm->params[10].base==0);fm->set_extended(false);CHECK(e->set_step_lock(0,1,0,10,9999));CHECK(e->audition_sequence_step(0,1));CHECK(fm->visual_pitch_remaining()==0&&!fm->params[10].audition_override);e->cancel_sequence_audition();
    std::puts("ok ordinary bipolar resolve / step-lock latch / modulation and performance-audition-temp precedence / legacy inactive-lock audition exclusion");
}
void wav(const std::string& filename,const std::vector<q15>& data) {
    std::ofstream out(filename,std::ios::binary);auto u16=[&](uint16_t v) { out.put(char(v));out.put(char(v>>8)); };auto u32=[&](uint32_t v) { u16(uint16_t(v));u16(uint16_t(v>>16)); };out.write("RIFF",4);u32(36+uint32_t(data.size()*2));out.write("WAVEfmt ",8);u32(16);u16(1);u16(1);u32(32000);u32(64000);u16(2);u16(16);out.write("data",4);u32(uint32_t(data.size()*2));for(q15 v:data)u16(uint16_t(v));CHECK(out.good());
}
void demos(const char* directory) {
    const char* names[]={"01_clean","02_bass","03_kick","04_metal","05_drone"};
    for(int demo=0;demo<5;++demo) { F u;u.phase_reset=true;set(u,0,demo==2?7282:demo==1?10923:16384);set(u,1,demo==3?28000:2184);set(u,2,demo==0?0:demo==2?2000:14000);set(u,3,demo==3?15500:0);set(u,4,32000);set(u,5,22000);set(u,6,demo==3?1200:demo==1?2200:0);if(demo==1)u.mod_wave=F::Wave::Triangle;if(demo==3)u.mod_wave=F::Wave::Saw;if(demo==4) { u.drone=true;u.carrier_wave=F::Wave::Triangle;set(u,1,5000);set(u,8,17413); }if(demo==2) { set(u,10,23000);set(u,11,17000); }u.control_tick();std::vector<q15> data(64000);for(int at=0;at<64000;at+=32) { if(at%16000==0)u.trigger();u.process(data.data()+at,32); }wav(std::string(directory)+"/"+names[demo]+".wav",data); }
    std::puts("host-only 32khz mono WAV demo bank written; not hardware or listening acceptance");
}
}
int main(int argc,char**argv) {
    path="/tmp/seance-fm-extended-"+std::to_string((long)getpid())+".snc";
    legacy_neutral();mappings_envelope();waves_and_pitch_oracle();live_latches();roundtrip_and_legacy();layers();
    if(argc==3&&!std::strcmp(argv[1],"--demos"))demos(argv[2]);
    std::remove(path.c_str());
    std::printf("fm extended core: 6/6 passed (%llu checks, %llu exact twin samples, %llu independent oracle samples)\n",(unsigned long long)checks,(unsigned long long)twin_samples,(unsigned long long)oracle_samples);
}
