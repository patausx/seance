// GRAN v2 focused core proof. The baseline mode was executed against the
// pre-v2 units.cpp before implementation; its constants are not twin comparisons.
#include "engine/units.h"
#include "engine/engine.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>
using namespace seance;
namespace {
using G = GrainUnit;
using Pcm = std::vector<q15>;
using trackr::synth::Sample;
using trackr::synth::SampleBank;
unsigned long long checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
void set(G& g, int p, int v) { g.params[p].base = (q15)v; g.params[p].snap(); }
void source(uint32_t n, int channels = 1) {
    Sample& s = SampleBank::instance().slot(0);
    s = Sample{}; s.channels = (uint8_t)channels;
    s.data.resize((size_t)n * channels);
    for (size_t i = 0; i < s.data.size(); ++i)
        s.data[i] = (q15)((int)((i * 1931u + i / 37u * 173u) & 65535u) - 32768);
}
uint64_t word(uint64_t h, uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) { h ^= (value >> (i * 8)) & 255u; h *= 1099511628211ull; }
    return h;
}
uint32_t random(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; }
void frozen_legacy() {
    for (int fixture = 0; fixture < 6; ++fixture) {
        source(fixture == 4 ? 257 : 65536, fixture % 2 + 1);
        G g;
        g.clean = false;
        set(g, G::P_SCAN, fixture % 2 ? -32767 : 32767); // ignored by legacy
        uint64_t pcm_hash = 1469598103934665603ull, visual_hash = pcm_hash;
        uint32_t seed = 0x82abcdefu + fixture;
        for (int block = 0; block < 800; ++block) {
            if (block % 11 == 0) {
                for (int p = 0; p < 11; ++p) {
                    int v = random(seed) & 32767;
                    if (p == G::P_PITCH && fixture % 2) v = -v;
                    set(g, p, v);
                }
                g.clock_sync = fixture == 2;
                g.sync_division = block % 4;
                g.set_transport_bpm_q16((20 + block % 281) * 65536 + 123);
                g.control_tick();
            }
            if (block % 29 == 0) g.trigger();
            if (block == 399) g.reset();
            q15 b[127]; const int n = 1 + random(seed) % 127;
            g.process(b, n);
            for (int i = 0; i < n; ++i) pcm_hash = word(pcm_hash, (uint16_t)b[i], 2);
            visual_hash = word(visual_hash, g.visual_spawned(), 4);
            visual_hash = word(visual_hash, g.visual_grains(), 2);
            visual_hash = word(visual_hash, g.visual_active(), 1);
            visual_hash = word(visual_hash, g.visual_interval(), 2);
            for (int i = 0; i < 8; ++i) {
                visual_hash = word(visual_hash, g.visual_positions()[i], 1);
                visual_hash = word(visual_hash, g.visual_phases()[i], 1);
                visual_hash = word(visual_hash, (uint8_t)g.visual_pitch_offsets()[i], 1);
            }
        }
        // Captured before ANY units.cpp/h changes; both audio and last-block UI.
        const uint64_t expected[6][2] = {
            {13697533497414174360ull, 3457870170833807644ull},
            {12797975628368176092ull, 11029909418462138045ull},
            {5294875777066716196ull, 6432844174693943600ull},
            {8715760795036705124ull, 16768493585765774048ull},
            {2092638215044945363ull, 13824015210851502757ull},
            {240000616536979117ull, 7733924737710168545ull}
        };
        CHECK(pcm_hash == expected[fixture][0]);
        CHECK(visual_hash == expected[fixture][1]);
    }
    std::puts("ok frozen pre-v2 PCM + end-block telemetry, scan dormant in legacy");
}
Pcm pcm(G& g, int n) { Pcm b(n, 12345); g.process(b.data(), n); return b; }
void quiet(G& g, int pitch = 8192, int pos = 16384, int size = 32767, int atk = 32767, int rel = 32767) {
    set(g,G::P_PITCH,pitch); set(g,G::P_POS,pos); set(g,G::P_SIZE,size);
    set(g,G::P_ATTACK,atk); set(g,G::P_RELEASE,rel);
    set(g,G::P_SPRAY,0); set(g,G::P_PITCH_DEV,0); set(g,G::P_JITTER,0);
    g.clock_sync = true; g.sync_interval_frames = 1000000; g.control_tick();
}
int capture(G& g, G::VisualGrain (&v)[8]) {
    g.capture_visual_grains(v); int count = 0;
    for (const auto& x : v) count += x.active;
    return count;
}
void boundaries_windows() {
    double worst = 0;
    for (int nf : {256,257,1024,65536}) for (int pitch : {1,8192,32767,-1,-8192,-32767}) for (int pos : {0,32767}) {
        source(nf); std::fill(SampleBank::instance().slot(0).data.begin(),SampleBank::instance().slot(0).data.end(),32767);
        G g; quiet(g,pitch,pos); CHECK(pcm(g,1)[0] == 0);
        G::VisualGrain v[8]; CHECK(capture(g,v) == 1);
        const uint32_t len = v[0].length;
        const int32_t inc = (pitch < 0 ? -1 : 1) * std::max(1024,std::abs(pitch)*8);
        const uint32_t expected = std::min<uint32_t>(16000, (uint64_t)(nf-3)*65536/std::abs(inc));
        CHECK(len == expected);
        for (uint32_t age = 1; age < len; ++age) {
            CHECK(capture(g,v) == 1 && v[0].age == age);
            CHECK(v[0].position_q16 >= 0 && (v[0].position_q16 >> 16) + 1 < nf);
            double ideal = 32767;
            if (v[0].attack && age < v[0].attack) ideal = std::min(ideal,double(age)*32767/v[0].attack);
            const uint32_t left = len - 1 - age;
            if (v[0].release && left < v[0].release) ideal = std::min(ideal,double(left)*32767/v[0].release);
            const double error = std::abs(v[0].envelope-ideal); worst = std::max(worst,error); CHECK(error < 1.0);
            const int shaped = (32767 * int(v[0].envelope)) >> 15;
            CHECK(pcm(g,1)[0] == (int64_t(shaped)*32767 >> 18));
            if (age == 5) { // whole envelope and trajectory remain latched
                set(g,G::P_ATTACK,0); set(g,G::P_RELEASE,0); set(g,G::P_SIZE,0);
                set(g,G::P_PITCH,-pitch); set(g,G::P_POS,32767-pos); g.control_tick();
            }
        }
        CHECK(capture(g,v) == 0); CHECK(pcm(g,1)[0] == 0);
    }
    // The prior ~20% join discontinuity at attack=6554 is specifically covered.
    source(65536); G g; quiet(g,8192,0,32767,26847,26847);
    pcm(g,1); G::VisualGrain v[8]; CHECK(capture(g,v)==1 && v[0].attack==6554);
    pcm(g,6552); CHECK(capture(g,v)==1); const int before=v[0].envelope;
    pcm(g,1); CHECK(capture(g,v)==1 && v[0].envelope-before<=5);
    for (int nf : {0,1,2,3,255}) { source(nf); G tiny; quiet(tiny,-32767,0); for(q15 x:pcm(tiny,300)) CHECK(x==0); }
    std::printf("ok clean reverse/forward edges and short sources, latched precise windows (worst %.8f q15)\n",worst);
}
void headroom_tails() {
    source(65536); auto& s=SampleBank::instance().slot(0); std::fill(s.data.begin(),s.data.end(),32000);
    G g; quiet(g,8192,0,32767,0,0);
    for(int i=0;i<8;++i) { g.trigger(); pcm(g,1); }
    CHECK(g.visual_active()==8);
    const int sum=8*((32000*32767)>>15);
    CHECK(pcm(g,1)[0] == (int64_t(sum)*32767 >> 18));
    set(g,G::P_GAIN,8192); CHECK(pcm(g,1)[0] == (int64_t(sum)*8192 >> 18));
    CHECK(pcm(g,1)[0] > 7990); // not legacy sat(sum)*gain/8 (~1023)
    // At one voice, replacing a fully open head retains a 3ms tail at true level.
    G one; quiet(one,8192,0,32767,0,0); set(one,G::P_VOICES,0);
    pcm(one,20); one.trigger(); auto tail=pcm(one,96);
    CHECK(tail.front()>7900 && tail.back()<4100);
    for(size_t i=1;i<tail.size();++i) CHECK(tail[i]<=tail[i-1] && tail[i-1]-tail[i]<45);
    // Coherent burst can fill tails as well. Gain remains capable of avoiding clip.
    G burst; quiet(burst,8192,0,32767,0,0); set(burst,G::P_GAIN,16384);
    for(int i=0;i<16;++i) { burst.trigger(); CHECK(std::abs(int(pcm(burst,1)[0]))<32767); }
    set(burst,G::P_GAIN,0); for(q15 x:pcm(burst,32)) CHECK(x==0);
    std::puts("ok fixed headroom, pre-saturation gain and latched declick tails");
}
int normalized(int pos, int nf, int64_t offset) {
    const int64_t span=int64_t(nf)*65536;
    int64_t center=(int64_t(pos)*nf*2+offset)%span; if(center<0)center+=span;
    return int(center/(int64_t(nf)*2));
}
void scan_clock() {
    CHECK(G::P_SCAN==11 && G::P_COUNT==12);
    CHECK(G::scan_increment_q16(0)==0 && G::scan_increment_q16(32767)==262144 && G::scan_increment_q16(-32767)==-262144);
    for(int rate : {0,1,-1,8192,-8192,32767,-32767}) {
        source(1024); G whole; quiet(whole,128,11331,0,0,0); set(whole,G::P_SCAN,rate); whole.control_tick(); G split=whole;
        CHECK(whole.scan_position()==11331);
        const auto a=pcm(whole,1707); Pcm b;
        for(int n:{1,17,32,113,512,3,1029}) { const auto p=pcm(split,n); b.insert(b.end(),p.begin(),p.end()); }
        CHECK(a==b);
        const int64_t off=int64_t(G::scan_increment_q16(rate))*1707;
        CHECK(whole.scan_position()==normalized(11331,1024,off)); CHECK(split.scan_position()==whole.scan_position());
        set(whole,G::P_SCAN,0); whole.control_tick(); const int held=whole.scan_position(); pcm(whole,500); CHECK(whole.scan_position()==held);
        set(whole,G::P_POS,22001); whole.control_tick(); CHECK(whole.scan_position()==normalized(22001,1024,off));
        whole.trigger(); CHECK(whole.scan_position()==22001); pcm(whole,1); CHECK(whole.scan_position()==22001);
        whole.reset(); CHECK(whole.scan_position()==22001); G::VisualGrain v[8]; CHECK(capture(whole,v)==0);
    }
    // No scanner dependency on process partition, pitch, envelope, or cloud count.
    source(65536); G a,b; quiet(a,8192,10000,0,0,0); quiet(b,-4096,10000,32767,32767,0);
    set(a,G::P_SCAN,12345); set(b,G::P_SCAN,12345); a.control_tick(); b.control_tick();
    pcm(a,777); pcm(b,777); CHECK(a.scan_position()==b.scan_position());
    // Consecutive spawns use the sample-clock center, not a control-rate display.
    G g; quiet(g,128,8192,0,0,0); g.clock_sync=false; set(g,G::P_DENS,32767); set(g,G::P_SCAN,32767); g.control_tick();
    pcm(g,321); G::VisualGrain v[8]; CHECK(capture(g,v)==2);
    CHECK(v[0].start_q16==int64_t(16384)*65536 && v[1].start_q16==int64_t(16384+320*4)*65536);
    for(int nf:{1,2,3})for(int rate:{32767,-32767}) { source(nf); G tiny; quiet(tiny); set(tiny,G::P_SCAN,rate); tiny.control_tick(); pcm(tiny,31); CHECK(tiny.scan_position()==normalized(16384,nf,int64_t(G::scan_increment_q16(rate))*31)); }
    std::puts("ok independent signed sample-clock scan/hold/wrap, POS anchoring, trigger/reset and process partitions");
}
void source_identity_and_pause() {
    source(65536); G g; quiet(g); set(g,G::P_SCAN,32767); g.control_tick(); pcm(g,500);
    CHECK(g.scan_position()!=16384); G::VisualGrain v[8]; CHECK(capture(g,v)==1);
    // Same slot and same physical pointer, but new logical frames: capture cannot
    // project old source trajectories; the next render restarts at anchored POS.
    auto& bank=SampleBank::instance(); bank.slot(0).logical_frames=32768;
    CHECK(g.scan_position()==16384 && capture(g,v)==0); pcm(g,1);
    CHECK(g.scan_position()==normalized(16384,32768,262144) && g.visual_spawned()==1);
    Sample retired; retired=bank.slot(0); // generation + backing pointer swap
    bank.swap_sample_unchecked(0,retired,trackr::synth::SampleOrigin::Runtime);
    CHECK(g.scan_position()==16384 && capture(g,v)==0); pcm(g,1); CHECK(g.visual_spawned()==1);
    // A generation-only replacement (the same backing storage comes back) is
    // still a source change; pointer/frame guards alone must not miss it.
    Sample empty; bank.swap_sample_unchecked(0,empty,trackr::synth::SampleOrigin::Runtime);
    bank.swap_sample_unchecked(0,empty,trackr::synth::SampleOrigin::Runtime);
    CHECK(g.scan_position()==16384 && capture(g,v)==0); pcm(g,1); CHECK(g.visual_spawned()==1);
    bank.slot(1)=bank.slot(0); g.sample_slot=1;
    CHECK(g.scan_position()==16384 && capture(g,v)==0); pcm(g,1); CHECK(g.visual_spawned()==1);
    g.sample_slot=-1; CHECK(pcm(g,32)==Pcm(32,0)); CHECK(capture(g,v)==0);
    source(65536); auto e=std::make_unique<Engine>(); auto* live=new G; delete e->replace_unit(0,0,live);
    quiet(*live); set(*live,G::P_SCAN,32767); q15 out[64]; e->render(out,32); const int started=live->scan_position();
    live->bypass=true; e->render(out,32); CHECK(live->scan_position()==started);
    live->bypass=false; e->chains[0].mute=true; e->render(out,32); CHECK(live->scan_position()!=started); // existing mute runs DSP
    std::puts("ok source identity guards (logical frames/generation/pointer/slot), bypass pause and existing mute-flow semantics");
}
using Bytes=std::vector<uint8_t>;
std::string path;
Bytes read() { std::ifstream f(path,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
void put(const Bytes& b) { std::ofstream f(path,std::ios::binary); f.write((const char*)b.data(),b.size()); CHECK(f.good()); }
uint32_t r32(const Bytes& b,size_t p) { CHECK(p+4<=b.size()); return b[p]|uint32_t(b[p+1])<<8|uint32_t(b[p+2])<<16|uint32_t(b[p+3])<<24; }
void w16(Bytes& b,size_t p,int value) { CHECK(p+2<=b.size()); b[p]=uint8_t(value); b[p+1]=uint8_t(uint16_t(value)>>8); }
size_t chunk(const Bytes& b,const char* id) { for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p; CHECK(false); return 0; }
void persistence_layers() {
    source(65536); path="/tmp/seance-gran-v2-"+std::to_string(long(getpid()))+".snc";
    for(int flags : {0,7,8,15}) {
        auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>(); auto* u=new G; delete a->replace_unit(0,0,u);
        u->clean=(flags&8)!=0; u->clock_sync=(flags&1)!=0; u->sync_division=(flags>>1)&3;
        set(*u,G::P_SCAN,-10000); u->params[G::P_SCAN].src=ModSource::SigilB; u->params[G::P_SCAN].mod_depth=-2345;
        CHECK(a->set_step_lock(0,0,0,G::P_SCAN,19000));
        a->macro_pads[0].route_count=1; a->macro_pads[0].routes[0]={0,0,G::P_SCAN,-32767,32767};
        a->capture_scene(0,0); set(*u,G::P_SCAN,22000); a->capture_scene(0,1);
        u->params[G::P_SCAN].temp=-30000; u->params[G::P_SCAN].temp_override=true;
        CHECK(patchio::save(*a,path.c_str())); const Bytes bytes=read(); CHECK(bytes[chunk(bytes,"UNIT")+8]==flags);
        CHECK(patchio::load(*b,path.c_str())); auto* v=static_cast<G*>(b->chains[0].units[0]);
        CHECK(v->clean==u->clean && v->clock_sync==u->clock_sync && v->sync_division==u->sync_division);
        CHECK(!v->params[G::P_SCAN].temp_override && v->params[G::P_SCAN].bipolar);
        CHECK(v->params[G::P_SCAN].base==(u->clean?22000:0));
        CHECK(v->params[G::P_SCAN].src==(u->clean?ModSource::SigilB:ModSource::None));
        CHECK(b->pattern.steps[0][0].lock_count==(u->clean?1:0)); CHECK(b->macro_pads[0].route_count==(u->clean?1:0));
        CHECK(b->scenes[0][0].base[0][G::P_SCAN]==(u->clean?-10000:0));
        std::unique_ptr<Unit> clone(clone_unit(v)); auto* c=static_cast<G*>(clone.get());
        CHECK(c->clean==v->clean && c->clock_sync==v->clock_sync && c->sync_division==v->sync_division);
        CHECK(c->params[G::P_SCAN].base==v->params[G::P_SCAN].base && c->visual_spawned()==0);
        b->apply_scene_morph(0,16384); CHECK(v->clean==u->clean);
        CHECK(v->params[G::P_SCAN].base==(u->clean?6000:0));
        // Chunks can be reordered. UNIT before CORE must not lose opt-in.
        Bytes moved=bytes; const size_t at=chunk(moved,"UNIT"), len=8+r32(moved,at+4);
        Bytes unit(moved.begin()+at,moved.begin()+at+len); moved.erase(moved.begin()+at,moved.begin()+at+len); moved.insert(moved.begin()+5,unit.begin(),unit.end());
        put(moved); CHECK(patchio::load(*b,path.c_str())); CHECK(static_cast<G*>(b->chains[0].units[0])->clean==u->clean);
        moved=bytes; moved[at+8]&=7; put(moved); CHECK(patchio::load(*b,path.c_str())); CHECK(!static_cast<G*>(b->chains[0].units[0])->clean);
        Bytes bad=bytes; bad[at+8]|=16; put(bad); Unit* sentinel=b->chains[0].units[0]; CHECK(!patchio::load(*b,path.c_str()) && b->chains[0].units[0]==sentinel);
    }
    for(int version=1;version<=6;++version) {
        Bytes old={'S','N','C',uint8_t('0'+version),uint8_t(version)}; const int raw[]={13000,18000,7000,-8192,20000,19000};
        for(int c=0;c<NUM_CHAINS;++c) {
            old.insert(old.end(),{255,127}); if(version>=2)old.insert(old.end(),{0,0}); old.push_back(0);
            const size_t first=old.size(); old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);
            if(!c) { old[first]=uint8_t(UnitKind::Grain); for(int p=0;p<6;++p)w16(old,first+3+p*5,raw[p]); }
        }
        old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);
        if(version>=3) { old.insert(old.end(),{120,0}); old.insert(old.end(),NUM_SIGILS*3,0); }
        if(version>=4) { const size_t at=old.size(); old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0); old[at]=8; } // historical padding cannot opt in
        if(version>=5) { old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0); old.insert(old.end(),{0,4,0}); }
        if(version>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
        put(old); auto e=std::make_unique<Engine>(); CHECK(patchio::load(*e,path.c_str())); auto* g=static_cast<G*>(e->chains[0].units[0]);
        CHECK(g && !g->clean && g->params[G::P_SCAN].base==0);
        G reference; reference.clean=false; for(int p=0;p<6;++p)set(reference,p,raw[p]); reference.control_tick(); g->control_tick(); CHECK(pcm(*g,8192)==pcm(reference,8192));
    }
    G g; quiet(g); auto& p=g.params[G::P_SCAN];
    p.base=-10000; p.step_lock=-5000; p.step_lock_override=true; p.audition=0; p.audition_override=true;
    p.performance=10000; p.performance_override=true; p.temp=20000; p.temp_override=true; p.src=ModSource::SigilA; p.mod_depth=1000;
    for(int expected : {20999,10999,999,-4001,-9001}) {
        p.slew_ready=false; p.resolve(32767); CHECK(p.effective==expected); g.control_tick(); g.reset(); pcm(g,37);
        CHECK(g.scan_position()==normalized(16384,65536,int64_t(G::scan_increment_q16(expected))*37));
        if(p.temp_override)p.temp_override=false; else if(p.performance_override)p.performance_override=false;
        else if(p.audition_override)p.audition_override=false; else p.step_lock_override=false;
    }
    p.mod_depth=32767; p.base=-32767; p.slew_ready=false; p.resolve(-32767); CHECK(p.effective==-32767);
    p.slew_ready=false; p.base=32767; p.resolve(32767); CHECK(p.effective==32767);
    p.src=ModSource::None; p.mod_depth=0; p.base=0; p.slew_ready=false; p.resolve(0); p.base=32767; p.resolve(0); CHECK(p.effective==8191);
    p.base=0; for(int tick=0;tick<100;++tick) { p.resolve(0); g.control_tick(); pcm(g,32); }
    CHECK(p.effective==0); const int held=g.scan_position(); pcm(g,1000); CHECK(g.scan_position()==held);
    std::remove(path.c_str());
    std::puts("ok SNC1-7 legacy-first flags/chunk order/rollback, clone, scenes/routes/locks and signed scan layer priority/slew");
}
} // namespace
int main() {
    frozen_legacy(); boundaries_windows(); headroom_tails(); scan_clock(); source_identity_and_pause(); persistence_layers();
    std::printf("gran clean core: 6/6 passed (%llu checks); GrainUnit=%zu bytes\n",checks,sizeof(G));
}
