// Focused real-core Chorus pair proof. Baseline macro compiles against pre-edit sources.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/chorus_motion.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace seance;
namespace motion = seance::chorus_motion;
namespace {
uint64_t checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
using Bytes = std::vector<uint8_t>;
std::string directory;
void write_bytes(const std::string& name, const void* data, size_t count) {
    std::ofstream f(directory + "/" + name, std::ios::binary);
    f.write(static_cast<const char*>(data), (std::streamsize)count); CHECK(f.good());
}
Bytes read_bytes(const std::string& name) {
    std::ifstream f(directory + "/" + name, std::ios::binary); CHECK(f.good());
    return {std::istreambuf_iterator<char>(f), {}};
}
void w16(Bytes& b, size_t at, int v) {
    CHECK(at + 2 <= b.size()); b[at] = (uint8_t)v; b[at+1] = (uint8_t)((uint16_t)v >> 8);
}
uint32_t random_word(uint32_t& seed) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
q15 stimulus(int i, uint32_t& seed) {
    if (i % 11 == 0) return -32768;
    if (i % 11 == 1) return 32767;
    return (q15)((int)(random_word(seed) & 65535u) - 32768);
}
void set(ChorusUnit& u, int p, int raw) { u.params[p].base = (q15)raw; u.params[p].snap(); }
void parameters(ChorusUnit& u, int rate, int depth, int mix) {
    set(u,0,rate); set(u,1,depth); set(u,2,mix);
}
std::vector<q15> input_pcm(size_t count, uint32_t seed = 0x917333u) {
    std::vector<q15> pcm(count);
    for (size_t i = 0; i < count; ++i) pcm[i] = stimulus((int)i,seed);
    return pcm;
}
std::vector<q15> reset_pcm(ChorusUnit& u) {
    u.reset(); for (int p=0;p<u.num_params;++p) u.params[p].snap();
    auto pcm=input_pcm(35000); u.process(pcm.data(),317); u.process(pcm.data()+317,(int)pcm.size()-317); return pcm;
}
void store_pcm(const std::string& name, const std::vector<q15>& pcm) {
    // Explicit little-endian bytes: artifact format is independent of host endianness.
    Bytes bytes(pcm.size()*2);
    for (size_t i=0;i<pcm.size();++i) w16(bytes,i*2,pcm[i]);
    write_bytes(name,bytes.data(),bytes.size());
}
void frozen_direct() {
    ChorusUnit u; CHECK(u.ready()); std::vector<q15> total; uint32_t seed=0x736a31u; int index=0;
    for (int rate : {0,7000,32767}) for (int depth : {0,12000,32767})
    for (int mix : {0,1,12000,16384,32766,32767}) {
        parameters(u,rate,depth,mix); std::vector<q15> pcm(1024);
        for (auto& s:pcm) s=stimulus(index++,seed);
        u.process(pcm.data(),317); u.process(pcm.data()+317,707); total.insert(total.end(),pcm.begin(),pcm.end());
    }
    parameters(u,32767,32767,12000); auto wrap=input_pcm(50000); u.process(wrap.data(),(int)wrap.size());
    total.insert(total.end(),wrap.begin(),wrap.end()); store_pcm("frozen-direct.pcm",total);
    std::puts("ok frozen direct PCM: 54 parameter combinations plus 50000-sample ring/phase wrap");
}
void legacy_fixtures() {
    for (int version=1;version<=6;++version) {
        const std::string prefix="snc"+std::to_string(version);
        Bytes old={'S','N','C',(uint8_t)('0'+version),(uint8_t)version};
        const int raw[]={23177,32000,28000};
        for (int c=0;c<NUM_CHAINS;++c) {
            old.insert(old.end(),{255,127}); if (version>=2) old.insert(old.end(),{0,0}); old.push_back(0);
            size_t first=old.size(); old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);
            if (!c) { old[first]=(uint8_t)UnitKind::Chorus; for (int p=0;p<3;++p) {
                const size_t at=first+3+p*5; w16(old,at,raw[p]); old[at+2]=(uint8_t)(p+1); w16(old,at+3,1200+p*1173);
            }}
        }
        // SNC1..6 have FOUR sigils, not today's NUM_SIGILS=8.
        old.insert(old.end(),4*(1+SIGIL_POINTS*2+2+2)+4*3,0);
        if (version>=3) { old.insert(old.end(),{137,0}); old.insert(old.end(),4*3,0); }
        if (version>=4) old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);
        if (version>=5) { old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0); old.insert(old.end(),{0,4,0}); }
        if (version>=6) old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
        write_bytes(prefix+"-old.snc",old.data(),old.size()); auto e=std::make_unique<Engine>();
        CHECK(patchio::load(*e,(directory+"/"+prefix+"-old.snc").c_str()));
        auto* u=static_cast<ChorusUnit*>(e->chains[0].units[0]); CHECK(u&&u->kind==UnitKind::Chorus);
        for (int p=0;p<3;++p) { CHECK(u->params[p].base==raw[p]); CHECK(u->params[p].src==(ModSource)(p+1)); CHECK(u->params[p].mod_depth==1200+p*1173); }
        CHECK(u->params[3].base==0&&u->params[3].mod_depth==0&&u->params[3].src==ModSource::None);
        const auto pcm=reset_pcm(*u); store_pcm(prefix+".pcm",pcm);
        CHECK(patchio::save(*e,(directory+"/"+prefix+"-roundtrip.snc").c_str()));
        auto loaded=std::make_unique<Engine>(); CHECK(patchio::load(*loaded,(directory+"/"+prefix+"-roundtrip.snc").c_str()));
        CHECK(reset_pcm(*static_cast<ChorusUnit*>(loaded->chains[0].units[0]))==pcm);
    }
    auto e=std::make_unique<Engine>(); auto* u=new ChorusUnit; delete e->replace_unit(0,0,u);
    parameters(*u,19023,24771,23009);
    for(int p=0;p<3;++p) { u->params[p].src=(ModSource)(p+1); u->params[p].mod_depth=(q15)(-7117+p*331); }
    e->capture_scene(0,0); parameters(*u,4193,1391,17777); e->capture_scene(0,1);
    parameters(*u,19023,24771,23009);
    for (int bank=0;bank<NUM_PATTERNS;++bank) for (int p=0;p<3;++p) {
        auto& step=e->sequence_pattern(bank).steps[0][p];
        step.lock_count=1; step.locks[0]={0,(uint8_t)p,(q15)(13000+bank*101+p)};
    }
    CHECK(patchio::save(*e,(directory+"/snc7-old.snc").c_str()));
    auto loaded=std::make_unique<Engine>(); CHECK(patchio::load(*loaded,(directory+"/snc7-old.snc").c_str()));
    auto* got=static_cast<ChorusUnit*>(loaded->chains[0].units[0]); CHECK(got->params[3].base==0);
    for (int which=0;which<2;++which) CHECK(loaded->scenes[0][which].base[0][3]==0);
    CHECK(reset_pcm(*got)==reset_pcm(*u)); store_pcm("snc7.pcm",reset_pcm(*got));
    CHECK(patchio::save(*loaded,(directory+"/snc7-roundtrip.snc").c_str()));
    CHECK(read_bytes("snc7-old.snc")==read_bytes("snc7-roundtrip.snc"));
    std::puts("ok synthetic SNC1..7 import/PCM/roundtrip; zero-padded slot3, old routes/scene/bank fields retained");
}
#ifndef CHORUS_PAIR_BASELINE_ONLY
constexpr double TAU = 6.283185307179586476925286766559;
uint32_t reference_sine(uint32_t phase) {
    // Analytically regenerate knots, then quantize using floor; no production table/helpers.
    const double p=(double)phase/16777216.0;
    const int index=(int)std::floor(p);
    const double fraction=std::floor((p-index)*65536.0)/65536.0;
    const double a=std::floor(32768.0*(1.0+std::sin(TAU*index/256.0))+.5);
    const double b=std::floor(32768.0*(1.0+std::sin(TAU*(index+1)/256.0))+.5);
    return (uint32_t)std::floor(a+(b-a)*fraction);
}
uint32_t reference_spread(int raw) {
    raw=std::max(-32767,std::min(32767,raw));
    return (uint32_t)(1073741824.0+std::trunc((double)raw*1073741824.0/32767.0));
}
struct Reference {
    uint32_t phase=0, delays[2]={96u*65536u,96u*65536u};
    std::vector<q15> history;
    int at(int64_t i) const { return i<0?0:history[(size_t)i]; }
    q15 sample(q15 dry,int rate,int depth,int mix,int spread) {
        rate=std::max(0,std::min(32767,rate)); depth=std::max(0,std::min(32767,depth)); mix=std::max(0,std::min(32767,mix));
        phase=(uint32_t)((uint64_t)phase+5000u+(uint32_t)rate*20u);
        int voice[2];
        for (int v=0;v<2;++v) {
            const uint32_t p=(uint32_t)((uint64_t)phase+(v?reference_spread(spread):0));
            delays[v]=96u*65536u+(16u+(uint32_t)(depth*300/32768))*reference_sine(p);
            const double position=(double)history.size()-(double)delays[v]/65536.0;
            const int64_t lower=(int64_t)std::floor(position); const double fraction=position-lower;
            voice[v]=(int)std::floor(at(lower)*(1.0-fraction)+at(lower+1)*fraction);
        }
        const int wet=(voice[0]+voice[1])/2;
        const double weight=mix==32767?1.0:(double)mix/32768.0;
        history.push_back(dry); return (q15)(dry+(int)std::floor((wet-dry)*weight));
    }
};
void mappings_reference() {
    static_assert(ChorusUnit::P_RATE==0&&ChorusUnit::P_DEPTH==1&&ChorusUnit::P_MIX==2&&ChorusUnit::P_SPREAD==3&&ChorusUnit::P_COUNT==4);
    ChorusUnit u; CHECK(u.num_params==4&&u.ready()); CHECK(u.params[3].bipolar&&u.params[3].base==0);
    CHECK(std::strcmp(u.params[3].name,"SPRD")==0);
    CHECK(u.params[0].base==7000&&u.params[1].base==12000&&u.params[2].base==12000);
    CHECK(!u.dry_compare_requested()&&u.visual_compare_q15()==0&&!u.visual_ready());
    CHECK(motion::spread_phase(-32767)==0&&motion::spread_phase(0)==0x40000000u&&motion::spread_phase(32767)==0x80000000u);
    CHECK(motion::spread_degrees(-32767)==0&&motion::spread_degrees(0)==90&&motion::spread_degrees(32767)==180);
    CHECK(motion::spread_phase(INT32_MIN)==0&&motion::spread_phase(INT32_MAX)==0x80000000u);
    uint32_t previous=0; uint16_t degrees=0;
    for (int raw=-32768;raw<=32767;++raw) {
        const uint32_t p=motion::spread_phase(raw); CHECK(p==reference_spread(raw)); CHECK(p>=previous); previous=p;
        CHECK(motion::spread_degrees(raw)>=degrees&&motion::spread_degrees(raw)<=180); degrees=motion::spread_degrees(raw);
        if(raw>=-32767) CHECK(p+motion::spread_phase(-raw)==0x80000000u);
    }
    Reference ref; uint32_t seed=0x482920u; int count=0;
    for (int rate:{0,7000,32767}) for(int depth:{0,12000,32767}) for(int mix:{0,1,12000,16384,32766,32767})
    for(int spread:{-32768,-32767,-19001,-1,0,1,19001,32767}) {
        parameters(u,rate,depth,mix); set(u,3,spread);
        for (int i=0;i<257;++i) {
            const q15 dry=stimulus(count++,seed); q15 output=dry; u.process(&output,1);
            CHECK(output==ref.sample(dry,rate,depth,mix,spread));
            CHECK(u.visual_phase()==ref.phase/65536u&&u.visual_ready());
            CHECK(u.visual_effective(3)==std::max(-32767,spread));
            CHECK(u.visual_effective(100)==u.visual_effective(3)&&u.visual_effective(-100)==rate);
            for(int v=0;v<2;++v) {
                CHECK(u.visual_delay_q16(v)==ref.delays[v]);
                CHECK(ref.delays[v]>=96u*65536u&&ref.delays[v]<=411u*65536u);
            }
            if(spread<=-32767) CHECK(u.visual_delay_q16(0)==u.visual_delay_q16(1));
            if(mix==0) CHECK(output==dry);
        }
    }
    // Full-scale constant DC must remain normalized at all separation endpoints.
    for(int spread:{-32767,0,32767}) for(q15 dc:{(q15)-32768,(q15)-1,(q15)0,(q15)1,(q15)32767}) {
        u.reset(); parameters(u,32767,32767,32767); set(u,3,spread);
        std::vector<q15> pcm(7000,dc); u.process(pcm.data(),(int)pcm.size());
        for(size_t i=412;i<pcm.size();++i) CHECK(pcm[i]==dc);
    }
    std::printf("ok spread complete raw mapping, independent PCM/fractional delays/telemetry: %d samples; DC normalized\n",count);
}
void compare_reference() {
    ChorusUnit u; Reference ref; uint32_t seed=0x11929u; int lease=0, gain=0;
    auto request=[&](bool held) { u.set_dry_compare(held); lease=held?8000:0; };
    parameters(u,19023,32000,32767); set(u,3,-17771);
    for(int i=0;i<30000;++i) {
        if(i==1000||i==2057||i==2300||i==2500||i==14000||(i>=14512&&i<20000&&(i-14000)%512==0)) request(true);
        if(i==2000||i==2200||i==20000) request(false);
        // Stored value intentionally differs from the effective control-rate value.
        // Change live mix/phase during a held comparison to catch stale restoration.
        if(i%997==0) {
            u.params[2].base=7777; u.params[2].effective=(q15)((i*47)%32768);
            u.params[3].base=-17111; u.params[3].effective=(q15)((i*13)%65535-32767);
        }
        Param snapshot[PARAM_BANK_SIZE]; std::memcpy(snapshot,u.params,sizeof snapshot);
        const q15 dry=stimulus(i,seed); q15 out=dry;
        const q15 mixed=ref.sample(dry,u.params[0].effective,u.params[1].effective,u.params[2].effective,u.params[3].effective);
        if(lease>0) { --lease; gain=std::min(32768,gain+256); } else gain=std::max(0,gain-256);
        const q15 wanted=(q15)(mixed+(int)std::floor((double)(dry-mixed)*gain/32768.0));
        u.process(&out,1); CHECK(out==wanted); CHECK(u.visual_compare_q15()==gain);
        CHECK(u.dry_compare_requested()==(lease>0)); CHECK(std::memcmp(snapshot,u.params,sizeof snapshot)==0);
        CHECK(u.visual_delay_q16(0)==ref.delays[0]&&u.visual_delay_q16(1)==ref.delays[1]);
        if(gain==32768) CHECK(out==dry);
        if(gain==0) CHECK(out==mixed);
    }
    // Explicit full-scale wet/dry difference in both directions exercises int32 limits.
    for(q15 dc:{(q15)-32768,(q15)32767}) {
        u.reset(); parameters(u,0,0,32767); set(u,3,0);
        std::vector<q15> fill(4096,dc); u.process(fill.data(),(int)fill.size()); u.set_dry_compare(true);
        for(int n=0;n<128;++n) { q15 out=dc<0?32767:-32768; u.process(&out,1); }
        CHECK(u.visual_compare_q15()==32768); q15 out=dc<0?32767:-32768; const q15 dry=out; u.process(&out,1); CHECK(out==dry);
    }
    std::puts("ok independent 4ms compare ramp/reversal/full-dry/refresh/250ms expiry/current effective mix restore/warm LFO+ring; params untouched");
}
void split_reset() {
    ChorusUnit whole,split; parameters(whole,32767,32767,23210); parameters(split,32767,32767,23210);
    set(whole,3,32767); set(split,3,32767); whole.set_dry_compare(true); split.set_dry_compare(true);
    auto a=input_pcm(50000), b=a; whole.process(a.data(),(int)a.size()); size_t at=0; uint32_t seed=9371;
    while(at<b.size()) {
        const int n=(int)std::min(b.size()-at,(size_t)(1+random_word(seed)%4097)); split.process(b.data()+at,n); at+=(size_t)n;
        const auto phase=split.visual_phase(); const auto gain=split.visual_compare_q15(); const auto req=split.dry_compare_requested();
        q15 sentinel=17123; split.process(nullptr,32); split.process(nullptr,0); split.process(&sentinel,0); split.process(&sentinel,-1);
        CHECK(sentinel==17123&&split.visual_phase()==phase&&split.visual_compare_q15()==gain&&split.dry_compare_requested()==req);
    }
    CHECK(a==b&&whole.visual_phase()==split.visual_phase()); CHECK(whole.visual_compare_q15()==0&&!whole.dry_compare_requested());
    CHECK(whole.visual_delay_q16(0)==split.visual_delay_q16(0)&&whole.visual_delay_q16(1)==split.visual_delay_q16(1));
    whole.set_dry_compare(true); q15 sample=17; whole.process(&sample,1); CHECK(whole.visual_compare_q15()==256);
    whole.reset(); CHECK(!whole.visual_ready()&&whole.visual_phase()==0); CHECK(!whole.dry_compare_requested()&&whole.visual_compare_q15()==0);
    CHECK(whole.visual_delay_q16(0)==96u*65536u&&whole.visual_delay_q16(1)==96u*65536u);
    for(int p=0;p<4;++p) CHECK(whole.visual_effective(p)==0);
    ChorusUnit fresh; parameters(fresh,32767,32767,23210); set(fresh,3,32767);
    a=input_pcm(10000); b=a; whole.process(a.data(),(int)a.size()); fresh.process(b.data(),(int)b.size()); CHECK(a==b);
    // Cancellation before any audio never starts a ramp, and zero-sized calls cannot consume lease.
    whole.reset(); whole.set_dry_compare(true); whole.process(nullptr,0); CHECK(whole.dry_compare_requested());
    whole.set_dry_compare(false); a=input_pcm(8192); b=a; fresh.reset(); whole.process(a.data(),(int)a.size()); fresh.process(b.data(),(int)b.size()); CHECK(a==b);
    std::puts("ok arbitrary block splits/lease deadline/invalid calls/reset clears history+compare/immediate cancellation");
}
int clamp_spread(int value) { return std::max(-32767,std::min(32767,value)); }
void layers_engine() {
    ChorusUnit u; auto& p=u.params[3]; p.base=-17000; p.step_lock=-4000; p.audition=4000; p.performance=11000; p.temp=23000;
    for(int mask=0;mask<16;++mask) for(int enabled:{0,1}) for(int depth:{-32767,16000,32767}) for(int mod:{-32767,0,32767}) {
        p.step_lock_override=(mask&1)!=0; p.audition_override=(mask&2)!=0;
        p.performance_override=(mask&4)!=0; p.temp_override=(mask&8)!=0; p.mod_enabled=enabled!=0;
        p.mod_depth=(q15)depth; p.slew_ready=false;
        const int control=mask&8?23000:mask&4?11000:mask&2?4000:mask&1?-4000:-17000;
        const int delta=enabled?(int)std::floor((double)mod*depth/32768.0):0;
        p.resolve((q15)mod); CHECK(p.target==clamp_spread(control+delta)&&p.effective==p.target); CHECK(p.base==-17000);
        const q15 expected=p.effective; p.slew_ready=true; p.resolve((q15)-mod);
        CHECK(p.effective==expected+((int)p.target-expected)/4); CHECK(p.base==-17000);
    }
    auto e=std::make_unique<Engine>(); auto* c=new ChorusUnit; delete e->replace_unit(0,0,c); auto& sp=c->params[3];
    sp.base=-14000; sp.src=ModSource::SigilA; sp.mod_depth=23000; sp.slew_ready=false;
    q15 stereo[64]={}; e->render(stereo,32);
    CHECK(sp.target==clamp_spread(sp.base+(int)std::floor((double)e->sigils[0].value*sp.mod_depth/32768.0)));
    CHECK(c->visual_effective(3)==sp.effective&&sp.base==-14000);
    sp.mod_enabled=false; e->render(stereo,32); CHECK(sp.target==-14000&&sp.src==ModSource::SigilA&&sp.mod_depth==23000);
    CHECK(e->set_step_lock(0,0,0,3,-21001)); CHECK(e->audition_sequence_step(0,0));
    CHECK(sp.audition_override&&sp.audition==-21001&&sp.base==-14000);
    CHECK(e->begin_param_temp(0,0,3)); CHECK(e->set_param_temp(25001)); e->render(stereo,32);
    CHECK(sp.temp_override&&sp.target==25001&&sp.base==-14000);
    c->set_dry_compare(true); for(int i=0;i<4;++i)e->render(stereo,32); CHECK(c->visual_compare_q15()==32768);
    // Cancel temp reveals the CURRENT audition, not a captured base; dry does not own Param.
    e->cancel_param_temp(); CHECK(sp.audition_override&&!sp.temp_override); e->render(stereo,32); CHECK(sp.target==-21001);
    e->cancel_sequence_audition(); c->set_dry_compare(false); for(int i=0;i<4;++i)e->render(stereo,32);
    CHECK(!sp.audition_override&&sp.target==-14000&&sp.base==-14000&&c->visual_compare_q15()==0);
    set(*c,3,-32767); e->capture_scene(0,0); set(*c,3,32767); e->capture_scene(0,1);
    e->apply_scene_morph(0,0); CHECK(sp.base==-32767);
    // Existing Q15 scene law uses 32767/32768 at its upper endpoint; do not change it here.
    e->apply_scene_morph(0,32767); CHECK(sp.base==32765);
    e->apply_scene_morph(0,16384); CHECK(std::abs((int)sp.base)<=1);
    // Actual scheduled spread lock is resolved without base mutation.
    sp.base=17771; sp.slew_ready=false; e->set_sequencer_enabled(true); e->transport_reset(); e->transport_play();
    // Lazy clock schedules the first accepted event at the next 1/16 boundary.
    for(int i=0;i<128;++i) e->render(stereo,32);
    CHECK(sp.step_lock_override&&sp.step_lock==-21001&&sp.target==-21001&&sp.base==17771);
    e->transport_stop(); e->render(stereo,32); CHECK(!sp.step_lock_override&&sp.target==17771&&sp.base==17771);
    std::puts("ok signed routing/clamps/slew/all Param priorities; real engine modulation, lock audition/temp, scenes and scheduled lock restore");
}
void persistence_current() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>(); auto* u=new ChorusUnit; delete a->replace_unit(0,0,u);
    const int values[]={17031,27009,23119,-20117};
    for(int p=0;p<4;++p) { set(*u,p,values[p]); u->params[p].src=sigil_mod_source(p+4); u->params[p].mod_depth=(q15)(-17000+p*2311); u->params[p].mod_enabled=p%2==0; }
    set(*u,3,-32767); a->capture_scene(0,0); set(*u,3,32767); a->capture_scene(0,1); set(*u,3,values[3]);
    for(int bank=0;bank<NUM_PATTERNS;++bank) for(int p=0;p<4;++p) {
        auto& step=a->sequence_pattern(bank).steps[0][p]; step.lock_count=1;
        step.locks[0]={0,(uint8_t)p,(q15)(p==3?-29000+bank*311:10000+bank*211+p)};
    }
    a->macro_pads[0].route_count=1; a->macro_pads[0].routes[0]={0,0,3,-32767,32767};
    const auto file=directory+"/spread-normal.snc"; CHECK(patchio::save(*a,file.c_str())); const auto stored=read_bytes("spread-normal.snc");
    u->set_dry_compare(true); auto pcm=input_pcm(128); u->process(pcm.data(),128); CHECK(u->visual_compare_q15()==32768);
    CHECK(patchio::save(*a,(directory+"/spread-held.snc").c_str())); CHECK(read_bytes("spread-held.snc")==stored);
    CHECK(patchio::load(*b,file.c_str())); auto* got=static_cast<ChorusUnit*>(b->chains[0].units[0]);
    CHECK(got&&got->num_params==4&&got->params[3].bipolar&&!got->dry_compare_requested()&&got->visual_compare_q15()==0);
    for(int p=0;p<4;++p) {
        CHECK(got->params[p].base==values[p]); CHECK(got->params[p].src==u->params[p].src);
        CHECK(got->params[p].mod_depth==u->params[p].mod_depth&&got->params[p].mod_enabled==u->params[p].mod_enabled);
        for(int which=0;which<2;++which) CHECK(a->scenes[0][which].base[0][p]==b->scenes[0][which].base[0][p]);
        for(int bank=0;bank<NUM_PATTERNS;++bank) {
            const auto& from=a->sequence_pattern(bank).steps[0][p]; const auto& to=b->sequence_pattern(bank).steps[0][p];
            CHECK(from.lock_count==to.lock_count&&to.locks[0].slot==0&&to.locks[0].param==p&&from.locks[0].value==to.locks[0].value);
        }
    }
    CHECK(b->macro_pads[0].route_count==1&&b->macro_pads[0].routes[0].param==3&&b->macro_pads[0].routes[0].minimum==-32767&&b->macro_pads[0].routes[0].maximum==32767);
    CHECK(patchio::save(*b,(directory+"/spread-roundtrip.snc").c_str())); CHECK(read_bytes("spread-roundtrip.snc")==stored);
    // Clone from held source must copy persistent params, never compare/ring/LFO/runtime layers.
    std::unique_ptr<Unit> cloned(clone_unit(u)); auto* copy=static_cast<ChorusUnit*>(cloned.get());
    CHECK(copy&&copy->num_params==4&&!copy->dry_compare_requested()&&copy->visual_compare_q15()==0&&!copy->visual_ready());
    CHECK(reset_pcm(*copy)==reset_pcm(*got));
    b->apply_macro_pad(0,0); CHECK(got->params[3].base==-32767); b->apply_macro_pad(0,32767); CHECK(got->params[3].base>=32764);
    std::puts("ok SNC7 signed spread/routes/mute/scenes/all8 banks/macro range, exact resave, held-save byteidentical, compare-free clone/load");
}
void focused() {
    mappings_reference(); compare_reference(); split_reset(); layers_engine(); persistence_current();
}
#endif
} // namespace
int main(int argc,char** argv) {
    CHECK(argc==2); directory=argv[1]; frozen_direct(); legacy_fixtures();
#ifndef CHORUS_PAIR_BASELINE_ONLY
    focused();
#endif
    std::printf("chorus pair core: passed (%llu checks)\n",(unsigned long long)checks);
}
