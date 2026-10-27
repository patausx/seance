// Focused MIC core proof: exact legacy OFF, non-consuming native capture,
// actual output/clip telemetry, fixed first-order cut and targeted persistence.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <array>
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
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
void set(MicUnit& u, int p, int v) { u.params[p].base=(q15)v; u.params[p].snap(); }
q15 saturate(int64_t v) { return (q15)std::max<int64_t>(-32768,std::min<int64_t>(32767,v)); }
uint16_t peak(const std::vector<q15>& a) { int p=0; for (int s:a) p=std::max(p,std::abs(s)); return (uint16_t)p; }

// Frozen old MIC arithmetic, independent of the runtime interpolation helper.
struct LegacyMic {
    std::array<q15,4096> ring{}; uint32_t wr=0; uint64_t read=0; bool reset_pending=false;
    void push(const std::vector<q15>& in) { for (q15 s:in) ring[wr++ & 4095]=s; }
    void reset() { wr=0; ring.fill(0); reset_pending=true; }
    std::vector<q15> process(int n, int gain, bool open) {
        if (reset_pending) { read=(uint64_t)(wr-std::min(wr,512u))<<16; reset_pending=false; }
        if (wr-(uint32_t)(read>>16)>4096) read=(uint64_t)(wr-std::min(wr,512u))<<16;
        std::vector<q15> out(n);
        for (auto& s:out) {
            int32_t sample=0; const uint32_t at=(uint32_t)(read>>16);
            if (wr-at>=2) {
                const int32_t a=ring[at&4095], b=ring[(at+1)&4095];
                sample=a+(int32_t)(((int64_t)(b-a)*(uint16_t)read)>>16); read+=67028;
            }
            s=open?saturate(((int64_t)sample*std::max(gain,0))>>13):0;
        }
        return out;
    }
};
void off_exact() {
    MicUnit::reset_input(); MicUnit a,b; LegacyMic oracle;
    CHECK(a.num_params==3 && a.params[MicUnit::P_LOW_CUT].base==0);
    uint32_t rng=61; std::vector<q15> in(128),out_a(64),out_b(64);
    std::array<q15,MicUnit::INPUT_VISUAL_SAMPLES> raw{};
    for (int block=0;block<100;++block) {
        if (block==71) { MicUnit::reset_input(); oracle.reset(); }
        for (auto& s:in) { rng=rng*1664525u+1013904223u; s=(q15)(rng>>16); }
        MicUnit::push(in.data(),(int)in.size()); oracle.push(in);
        const int gains[]={8192,32767,0,16384,-32768}; const int gain=gains[block%5];
        const int mon=block%9==0?16384:32767;
        set(a,0,gain);set(b,0,gain);set(a,1,mon);set(b,1,mon);
        const int count=MicUnit::copy_input(raw.data(),raw.size()); CHECK(count>0);
        const uint32_t serial=MicUnit::input_serial();
        a.process(out_a.data(),19); a.process(out_a.data()+19,45);
        CHECK(MicUnit::copy_input(raw.data(),raw.size())==count && MicUnit::input_serial()==serial);
        b.process(out_b.data(),64);
        CHECK(out_a==out_b); CHECK(out_a==oracle.process(64,gain,mon>16384));
        CHECK(b.visual_output_peak()==peak(out_b));
    }
    CHECK(a.visual_dropped()>0 && a.visual_dropped()==b.visual_dropped());
    std::puts("ok OFF exact old gain/source/interpolation, two independent readers, wrap/stale/reset and non-consuming captures");
}
void capture_boundaries() {
    std::array<q15,MicUnit::INPUT_VISUAL_SAMPLES+2> dst; dst.fill(12345);
    uint32_t serial=MicUnit::input_serial(); MicUnit::reset_input(); CHECK(MicUnit::input_serial()!=serial);
    CHECK(MicUnit::copy_input(dst.data(),dst.size())==0 && dst[0]==12345);
    serial=MicUnit::input_serial(); q15 one=-32768;
    MicUnit::push(nullptr,4);MicUnit::push(&one,0);MicUnit::push(&one,-3);
    CHECK(MicUnit::input_serial()==serial);
    CHECK(MicUnit::copy_input(nullptr,100)==0 && MicUnit::copy_input(dst.data(),0)==0 && MicUnit::copy_input(dst.data(),-1)==0);
    for (int n:{1,2,1047,1048,1049,5001}) {
        MicUnit::reset_input(); std::vector<q15> in(n); for (int i=0;i<n;++i) in[i]=(q15)(i-1800);
        serial=MicUnit::input_serial();MicUnit::push(in.data(),n);CHECK(MicUnit::input_serial()!=serial);
        dst.fill(12345); const int want=std::min(n,MicUnit::INPUT_VISUAL_SAMPLES);
        CHECK(MicUnit::copy_input(dst.data(),dst.size())==want);
        CHECK(std::equal(dst.begin(),dst.begin()+want,in.end()-want)); CHECK(dst[want]==12345);
        CHECK(MicUnit::copy_input(dst.data(),1)==1 && dst[0]==in.back());
        std::vector<q15> more(4093);for(int i=0;i<(int)more.size();++i)more[i]=(q15)(9000+i);
        MicUnit::push(more.data(),more.size());CHECK(MicUnit::copy_input(dst.data(),dst.size())==1048);
        CHECK(std::equal(dst.begin(),dst.begin()+1048,more.end()-1048));
    }
    MicUnit::reset_input();CHECK(MicUnit::copy_input(dst.data(),dst.size())==0);
    std::puts("ok native capture empty/startup/1/1047/1048/1049, bounded writes, chronological physical wrap, reset and serial");
}
void output_telemetry() {
    MicUnit::reset_input(); MicUnit a,b; set(a,0,16384);set(b,0,16384);
    const q15 in[]={20000,20000};MicUnit::push(in,2);q15 sample=0;
    a.process(&sample,1);CHECK(sample==32767 && a.visual_output_peak()==32767 && a.visual_clip());
    b.process(&sample,1);CHECK(b.visual_clip());
    set(a,0,0);set(b,0,0);std::vector<q15> silent(15999);
    a.process(silent.data(),15999);CHECK(a.visual_clip() && a.visual_output_peak()==0);
    for(int left=15999;left>0;) {const int n=std::min(left,37);b.process(silent.data(),n);left-=n;}
    CHECK(b.visual_clip());a.process(&sample,1);b.process(&sample,1);CHECK(!a.visual_clip() && !b.visual_clip());
    const uint32_t serial=a.visual_process_serial();a.process(nullptr,1);a.process(&sample,0);CHECK(a.visual_process_serial()==serial);
    MicUnit::reset_input();MicUnit::push(in,2);set(a,0,32767);a.process(&sample,1);CHECK(a.visual_clip());
    set(a,1,16384);a.process(&sample,1);CHECK(!a.visual_clip() && sample==0 && a.visual_output_peak()==0 && a.visual_peak()>0);
    std::array<q15,2> raw{};CHECK(MicUnit::copy_input(raw.data(),2)==2 && raw[0]==20000); // source mute never changes raw
    for (int endpoint:{32767,-32768}) {
        MicUnit::reset_input();const q15 endpoints[]={(q15)endpoint,(q15)endpoint};MicUnit::push(endpoints,2);
        set(a,0,8192);set(a,1,32767);a.process(&sample,1);
        CHECK(sample==endpoint && !a.visual_clip() && a.visual_output_peak()==std::abs(endpoint));
    }
    std::puts("ok actual postgain peak, exact endpoints vs arithmetic clipping, sample-count 500ms hold/block partition and source mute");
}
void high_pass() {
    CHECK(MicUnit::low_cut_mode(-32768)==0 && MicUnit::low_cut_mode(0)==0);
    CHECK(MicUnit::low_cut_mode(10922)==0 && MicUnit::low_cut_mode(10923)==1);
    CHECK(MicUnit::low_cut_mode(16384)==1 && MicUnit::low_cut_mode(21845)==1 && MicUnit::low_cut_mode(21846)==2 && MicUnit::low_cut_mode(32767)==2);
    constexpr double pi=3.14159265358979323846;
    for (int mode:{1,2}) for (int frequency:{0,20,80,160,4000}) {
        MicUnit::reset_input();MicUnit dry,cut;set(cut,2,mode==1?16384:32767);
        uint32_t pushed=0;double dry_energy=0,cut_energy=0;
        std::array<q15,256> d{},w{};
        for (int block=0;block<32;++block) {
            const uint32_t required=(uint32_t)(((uint64_t)(block+1)*256*67028)>>16)+2;
            std::vector<q15> in(required-pushed);
            for (auto& s:in) {s=frequency?(q15)std::lround(12000*std::sin(2*pi*frequency*pushed/32728.498)):12000;++pushed;}
            MicUnit::push(in.data(),in.size());dry.process(d.data(),d.size());cut.process(w.data(),w.size());
            if (block>=8) for (int i=0;i<256;++i) {dry_energy+=(double)d[i]*d[i];cut_energy+=(double)w[i]*w[i];}
        }
        if (!frequency) {CHECK(std::abs((int)w.back())<=2 && d.back()==12000 && cut_energy/dry_energy<1e-6);}
        else {
            const double ratio=std::sqrt(cut_energy/dry_energy);
            const double r=std::tan(pi*frequency/32000)/std::tan(pi*(mode==1?80:160)/32000);
            const double expected=r/std::sqrt(1+r*r);CHECK(std::abs(ratio-expected)<0.015);
        }
        // OFF is immediate sample-exact even after a charged filter, not a wet fade.
        set(cut,2,0);std::vector<q15> next(300,12000);MicUnit::push(next.data(),next.size());
        dry.process(d.data(),d.size());cut.process(w.data(),w.size());CHECK(d==w);
    }
    // Settled DC stays settled on 80->160 and during source mute (no state reset).
    MicUnit::reset_input();MicUnit cut;set(cut,2,16384);std::vector<q15> dc(4096,12000),out(3000);
    MicUnit::push(dc.data(),dc.size());cut.process(out.data(),out.size());CHECK(std::abs((int)out.back())<=2);
    set(cut,2,32767);cut.process(out.data(),32);CHECK(std::abs((int)out[0])<=2);
    set(cut,1,0);cut.process(out.data(),32);set(cut,1,32767);cut.process(out.data(),32);CHECK(std::abs((int)out[0])<=2);
    std::puts("ok OFF/80/160 exact thresholds, DC rejection, four nominal sine responses, immediate dry OFF and retained mode/mute state");
}

using Bytes=std::vector<uint8_t>;std::string path;
Bytes read_file(){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void put(const Bytes& b){std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
uint32_t r32(const Bytes& b,size_t at){CHECK(at+4<=b.size());return b[at]|(uint32_t)b[at+1]<<8|(uint32_t)b[at+2]<<16|(uint32_t)b[at+3]<<24;}
void w16(Bytes& b,size_t at,int value){CHECK(at+2<=b.size());b[at]=(uint8_t)value;b[at+1]=(uint8_t)((uint16_t)value>>8);}
size_t chunk(const Bytes& b,const char* id){for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;CHECK(false);return 0;}
MicUnit& mic(Engine& e){Unit* u=e.chains[0].units[0];CHECK(u && u->kind==UnitKind::MicIn && u->num_params==3);return *static_cast<MicUnit*>(u);}
void clear_tail(const MicUnit& u,int first){for(int p=first;p<PARAM_BANK_SIZE;++p)CHECK(u.params[p].base==0 && u.params[p].effective==0 && u.params[p].target==0 && u.params[p].src==ModSource::None && u.params[p].mod_depth==0 && !u.params[p].step_lock_override && !u.params[p].temp_override);}
void persistence() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();delete a->replace_unit(0,0,new MicUnit);auto& u=mic(*a);
    set(u,0,12001);set(u,2,16384);u.params[2].src=ModSource::SigilB;u.params[2].mod_depth=-5011;
    a->capture_scene(0,0);set(u,2,32767);a->capture_scene(0,1);set(u,2,16384);
    CHECK(a->set_step_lock(0,0,0,0,10000) && a->set_step_lock(0,0,0,2,32767));
    a->pattern_banks[1].steps[0][0].lock_count=1;a->pattern_banks[1].steps[0][0].locks[0]={0,2,16384};
    auto& pad=a->macro_pads[0];pad.route_count=2;pad.routes[0]={0,0,0,0,8192};pad.routes[1]={0,0,2,0,32767};
    CHECK(patchio::save(*a,path.c_str()));const Bytes modern=read_file();const size_t flags=chunk(modern,"UNIT")+8,core=chunk(modern,"CORE")+8;
    CHECK(modern[flags]==1 && patchio::load(*b,path.c_str()));auto& got=mic(*b);
    CHECK(got.params[2].base==16384 && got.params[2].src==ModSource::SigilB && got.params[2].mod_depth==-5011);
    CHECK(b->scenes[0][1].base[0][2]==32767 && b->pattern.steps[0][0].lock_count==2 && b->pattern_banks[1].steps[0][0].locks[0].param==2 && b->macro_pads[0].route_count==2);
    std::unique_ptr<Unit> copy(clone_unit(&got));CHECK(copy && copy->num_params==3 && copy->params[2].base==16384 && copy->params[2].src==got.params[2].src);
    CHECK(patchio::save(*b,path.c_str()));CHECK(read_file()==modern);
    // UNIT before CORE must not erase staged modern low-cut or its route.
    Bytes reordered=modern;const size_t up=flags-8,ue=up+8+r32(modern,up+4);Bytes unit(reordered.begin()+up,reordered.begin()+ue);
    reordered.erase(reordered.begin()+up,reordered.begin()+ue);reordered.insert(reordered.begin()+5,unit.begin(),unit.end());put(reordered);
    CHECK(patchio::load(*b,path.c_str()) && mic(*b).params[2].base==16384);
    Bytes old7=modern;old7[flags]=0;
    for(int p=2;p<12;++p){w16(old7,core+5+3+p*5,30000);old7[core+5+3+p*5+2]=(uint8_t)ModSource::SigilA;w16(old7,core+5+3+p*5+3,32767);}
    put(old7);CHECK(patchio::load(*b,path.c_str()));clear_tail(mic(*b),2);CHECK(mic(*b).params[0].base==12001);
    CHECK(b->scenes[0][0].base[0][2]==0 && b->scenes[0][1].base[0][2]==0);
    CHECK(b->macro_pads[0].route_count==1 && b->macro_pads[0].routes[0].param==0);
    CHECK(b->pattern.steps[0][0].lock_count==1 && b->pattern.steps[0][0].locks[0].param==0 && b->pattern_banks[1].steps[0][0].lock_count==0);
    b->apply_scene_morph(0,32767);b->apply_macro_pad(0,32767);CHECK(mic(*b).params[2].base==0);
    CHECK(patchio::save(*b,path.c_str()));const Bytes migrated=read_file();CHECK(migrated[chunk(migrated,"UNIT")+8]==1);
    CHECK(patchio::load(*b,path.c_str()));clear_tail(mic(*b),2);
    // Modern unused tails remain canonical too; p2 must still survive.
    Bytes dirty=modern;w16(dirty,core+5+3+3*5,30000);dirty[core+5+3+3*5+2]=1;put(dirty);
    CHECK(patchio::load(*b,path.c_str()));clear_tail(mic(*b),3);CHECK(mic(*b).params[2].base==16384);
    dirty=modern;dirty[flags]=2;put(dirty);Unit* sentinel=b->chains[0].units[0];CHECK(!patchio::load(*b,path.c_str()) && b->chains[0].units[0]==sentinel);
    // Targeted SNC6 fixture: historical flags/padding are not an opt-in.
    Bytes old={'S','N','C','6',6};
    for(int c=0;c<NUM_CHAINS;++c){old.insert(old.end(),{255,127,0,0,0});const size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);if(!c){old[first]=(uint8_t)UnitKind::MicIn;for(int p=0;p<6;++p){w16(old,first+3+p*5,p==0?12001:32767);old[first+3+p*5+2]=(uint8_t)ModSource::SigilA;w16(old,first+3+p*5+3,19001);}}}
    old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0);
    const size_t legacy_flags=old.size();old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);old[legacy_flags]=255;
    old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0});old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);put(old);
    CHECK(patchio::load(*b,path.c_str()));auto& legacy=mic(*b);clear_tail(legacy,2);CHECK(legacy.params[0].base==12001 && legacy.params[0].src==ModSource::SigilA && legacy.params[1].base==32767);
    MicUnit::reset_input();LegacyMic ref;ref.reset();std::vector<q15> input(512),actual(400);for(int i=0;i<512;++i)input[i]=(q15)(i*123-32000);
    MicUnit::push(input.data(),input.size());ref.push(input);legacy.process(actual.data(),actual.size());CHECK(actual==ref.process(actual.size(),12001,true));
    std::puts("ok SNC7 low-cut/routes/scenes/locks/clone/byte roundtrip, chunk order, old7+SNC6 hidden-tail/route clearing, old PCM and unknown flag rollback");
}
}
int main(){path="/tmp/seance-mic-core-"+std::to_string((long)getpid())+".snc";off_exact();capture_boundaries();output_telemetry();high_pass();persistence();std::remove(path.c_str());std::printf("mic core: 5/5 passed (%u checks)\n",checks);}
