// Narrow portable GLITCH contract: no device IO, media, broad suite, or timing claims.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>
using namespace seance;
using G = GlitchUnit;
using Pcm = std::vector<q15>;
using Bytes = std::vector<uint8_t>;
constexpr uint64_t BEAT=1ull<<32, GRID=BEAT/4, BAR=BEAT*4;
static unsigned assertions=0;
#define CHECK(x) do { ++assertions; if (!(x)) { std::fprintf(stderr,"glitch FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
static void set(Unit& u,int p,int value) {u.params[p].base=q15(value);u.params[p].snap();}
static bool same(const G::PatternState& a,const G::PatternState& b) {
    if(a.rng!=b.rng||a.revision!=b.revision)return false;
    for(int i=0;i<G::STEPS;++i) {
        const auto& x=a.cells[i];const auto& y=b.cells[i];
        if(x.op!=y.op||x.source!=y.source||x.semitones!=y.semitones||x.repeats!=y.repeats)return false;
    }return true;
}
static Pcm process(G& g,int n,int value) {Pcm out(n,q15(value));g.process(out.data(),n);return out;}
static bool all(const Pcm& pcm,int value) {return std::all_of(pcm.begin(),pcm.end(),[=](q15 x){return x==value;});}
static void legacy(G& g,uint64_t frame,uint32_t fpb) {
    const uint64_t scaled=frame*BEAT;
    g.set_transport_clock(true,scaled/fpb,uint32_t(BEAT/fpb),uint32_t(scaled%fpb),fpb);
}
static G::PatternState map(G& g,G::Op op=G::Op::Jump,int source=15) {
    auto state=g.pattern_state();
    for(int i=0;i<16;++i)state.cells[i]={op,uint8_t(source),int8_t(op==G::Op::Pitch?12:0),uint8_t(op==G::Op::Repeat?4:1)};
    set(g,G::P_KEEP,32767);CHECK(g.restore_pattern(state));return state;
}
static q15 source_value(int step,int local) {return q15(-14000+step*1400+local/2);}
static void operations() {
    G g; CHECK(g.ready()); static_assert(int(UnitKind::Arp)==26&&int(UnitKind::Glitch)==27,"append only");
    CHECK(g.num_params==G::P_COUNT&&!std::strcmp(unit_kind_name(g.kind),"GLITCH"));
    CHECK(!g.sequencer_properties().source&&!g.sequencer_properties().gate_processor);
    for(int value:{-32768,-19001,0,19731,32767})CHECK(all(process(g,200,value),value));
    auto state=map(g,G::Op::Dry);
    for(int i=0;i<16;++i)state.cells[i].source=uint8_t(i);
    state.cells[1]={G::Op::Jump,12,0,1};state.cells[2]={G::Op::Repeat,4,0,4};
    state.cells[3]={G::Op::Drop,3,0,1};state.cells[4]={G::Op::Reverse,8,0,1};
    state.cells[5]={G::Op::Pitch,10,12,1};state.cells[6]={G::Op::Pitch,6,-12,1};
    CHECK(g.restore_pattern(state));legacy(g,0,16000);
    Pcm input(64000);for(int i=0;i<64000;++i)input[i]=source_value(i/4000,i%4000);
    auto warm=input;g.process(warm.data(),warm.size());CHECK(warm==input&&!g.visual().ready);
    auto out=process(g,64000,-21000);CHECK(g.visual().ready&&same(state,g.pattern_state()));
    for(int i=0;i<64;++i)CHECK(g.visual().trace[i]==input[(i/4)*4000+(i%4)*3999/3]);
    for(int t:{200,750,1500,2500,3500}) {
        CHECK(out[t]==-21000); // unaltered: CURRENT live input
        CHECK(out[4000+t]==source_value(12,t));
        CHECK(out[8000+t]==source_value(4,t%1000));
        CHECK(out[12000+t]==0);
        CHECK(out[16000+t]==source_value(8,3999-t));
        CHECK(out[20000+t]==source_value(10,(t*2)%4000));
        CHECK(out[24000+t]==source_value(6,t/2));
        CHECK(out[28000+t]==-21000);
    }
    // KEEP holds cells, not audio. Bank swap must capture raw live input, not wet.
    out=process(g,64000,23456);CHECK(out[4200]==-21000&&out[8200]==-21000&&out[12200]==0);
    CHECK(out[200]==23456&&same(state,g.pattern_state()));
    for(q15 v:g.visual().trace)CHECK(v==-21000);
    set(g,G::P_MIX,0);g.control_tick();process(g,5000,32767);
    CHECK(all(process(g,2000,-32768),-32768));
    set(g,G::P_MIX,32767);g.control_tick();CHECK(process(g,1000,-32768).back()==23456);
    auto invalid=state;invalid.cells[0].source=16;
    CHECK(!g.restore_pattern(invalid)&&same(state,g.pattern_state())&&g.visual().ready);
    g.trigger();CHECK(same(state,g.pattern_state()));g.reset();CHECK(!g.visual().ready&&same(state,g.pattern_state()));
    std::puts("ok 1/6 actual previous-bar jump/repeat/drop/reverse/pitch, dry/full mix, pattern not PCM");
}
static void history_tempo() {
    G g;map(g);const uint32_t inc=uint32_t(BEAT/96000);const uint32_t frames=uint32_t((BAR+inc-1)/inc);
    CHECK(frames>=384000&&frames<G::CAPACITY);
    g.set_transport_clock(true,0,inc);CHECK(all(process(g,frames,10001),10001));CHECK(!g.visual().ready);
    CHECK(process(g,512,-19000).back()==10001&&g.visual().ready);
    process(g,frames-512,-19000);CHECK(process(g,512,17000).back()==-19000);
    process(g,frames-512,17000);CHECK(process(g,512,-27000).back()==17000); // reused banks never overwrite reader
    g.reset();map(g);legacy(g,0,96000);
    // Record half a slow bar then half a fast bar. All 17 source cuts are actual
    // captured frame indices, not reconstructed later from the new tempo.
    Pcm slow(192000);for(int i=0;i<192000;++i)slow[i]=source_value(i/24000,i%24000);
    auto raw=slow;g.process(slow.data(),slow.size());CHECK(slow==raw);
    g.set_transport_clock(true,2*BEAT,uint32_t(BEAT/6400),0,6400);
    Pcm fast(12800);for(int i=0;i<12800;++i)fast[i]=source_value(8+i/1600,i%1600);
    raw=fast;g.process(fast.data(),fast.size());CHECK(fast==raw);
    auto out=process(g,25600,-30000);CHECK(g.visual().ready);
    CHECK(out[300]==source_value(15,300)&&out[1900]==source_value(15,300));
    const auto trace=g.visual();CHECK(trace.trace[0]==source_value(0,0)&&trace.trace[63]==source_value(15,1599));
    for(int i=0;i<16;++i)for(int k=0;k<4;++k)
        CHECK(trace.trace[i*4+k]==source_value(i,k*((i<8?24000:1600)-1)/3));
    // Legacy retiming can jump inside the same cell. Retain valid captured source
    // and cursor, reject the discontinuous recording, never reschedule mid-cell.
    process(g,500,-30000);
    g.set_transport_clock(true,8*BEAT+GRID/3,uint32_t(BEAT/96000),0,96000);
    CHECK(g.visual().ready);CHECK(process(g,500,25000).back()==-30000);
    g.stop_transport();process(g,128,17);CHECK(all(process(g,100,17),17)&&!g.visual().ready);
    // Enable mid-bar: partial input may not become a previous "complete" bar.
    legacy(g,1234,6400);CHECK(all(process(g,25600-1234,311),311));CHECK(!g.visual().ready);
    CHECK(all(process(g,25600,412),412)&&!g.visual().ready);
    CHECK(process(g,512,-1311).back()==412&&g.visual().ready);
    // Arbitrary seek invalidates immediately, even with previously ready PCM.
    legacy(g,500,6400);CHECK(!g.visual().ready&&all(process(g,500,733),733));
    g.stop_transport();legacy(g,1,6400);process(g,25600,875);
    CHECK(!g.visual().ready); // starting only one sample late is still an incomplete bar
    std::puts("ok 2/6 applied20 capacity, repeated bank reuse, 20->300 recorded cuts, retime and midbar warmup");
}
static void pattern_controls() {
    G g;legacy(g,0,6400);process(g,517,40);auto held=g.pattern_state();
    set(g,G::P_KEEP,32767);g.params[G::P_KEEP].effective=0;g.control_tick(); // target, not slew
    CHECK(g.visual().kept&&same(held,g.pattern_state()));
    set(g,G::P_DENSITY,32767);for(int p=G::P_JUMP;p<=G::P_PITCH;++p)set(g,p,p==G::P_DROP?32767:0);
    g.control_tick();process(g,25600-517+3200,100);CHECK(same(held,g.pattern_state()));
    g.request_reroll();g.request_reroll();CHECK(g.visual().pending&&same(held,g.pattern_state()));
    process(g,25600-3200,100);CHECK(same(held,g.pattern_state())&&g.visual().pending);
    process(g,1,100);CHECK(!g.visual().pending&&g.pattern_state().revision==held.revision+1);
    for(auto c:g.pattern_state().cells)CHECK(c.op==G::Op::Drop);
    const auto dropped=g.pattern_state();set(g,G::P_DENSITY,0);g.control_tick();process(g,25600,100);
    CHECK(same(dropped,g.pattern_state()));g.request_reroll();g.stop_transport();
    CHECK(!g.visual().pending&&g.pattern_state().revision==dropped.revision+1);
    for(auto c:g.pattern_state().cells)CHECK(c.op==G::Op::Dry);
    auto stopped=g.pattern_state();g.request_reroll();CHECK(g.pattern_state().revision==stopped.revision+1);
    set(g,G::P_KEEP,0);set(g,G::P_DENSITY,32767);for(int p=G::P_JUMP;p<=G::P_PITCH;++p)set(g,p,0);
    g.control_tick();legacy(g,0,6400);process(g,25601,23);
    for(auto c:g.pattern_state().cells)CHECK(c.op==G::Op::Dry); // zero weights, no hidden operation fallback
    std::puts("ok 3/6 immediate target KEEP, held generation controls, coalesced nextbar reroll and stopped commit");
}
class Input final : public Unit {
public: uint32_t frame=0,triggers=0;
    Input(){kind=UnitKind::Empty;}
    void process(q15* out,int n) override {for(int i=0;i<n;++i)out[i]=q15(int((frame++*71u)%48001)-24000);}
    void trigger() override {++triggers;}
};
static G* setup(Engine& e,bool phase,bool dry=false) {
    delete e.replace_unit(0,0,new Input);auto* g=new G;delete e.replace_unit(0,1,g);
    set(*g,G::P_DENSITY,dry?0:32767);e.set_bpm(139);
    if(phase){e.sigil_config[0].legacy=true;e.sigils[0].drawn=true;for(auto& x:e.sigils[0].table)x=20000;e.set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,87);}
    e.transport_play();return g;
}
static Pcm render(Engine& e,int frames,bool split=false) {
    Pcm pcm(frames*2);for(int at=0;at<frames;){const int n=split?std::min(1+(at*17+11)%227,frames-at):frames-at;e.render(pcm.data()+at*2,n);at+=n;}return pcm;
}
static void partitions() {
    for(bool phase:{false,true}) {
        G a,b;Pcm x(160003),y;
        for(int i=0;i<int(x.size());++i)x[i]=q15((i*73)%51001-25500);
        y=x;
        const uint32_t fpb=14159,inc=uint32_t(BEAT/fpb);
        if(phase)a.set_transport_clock(true,0,inc);else legacy(a,0,fpb);
        a.process(x.data(),x.size());
        for(size_t at=0;at<y.size();) {
            const int n=std::min<size_t>((at*17+11)%229+1,y.size()-at);
            if(phase)b.set_transport_clock(true,uint64_t(at)*inc,inc);else legacy(b,at,fpb);
            b.process(y.data()+at,n);at+=n;
        }
        CHECK(x==y&&same(a.pattern_state(),b.pattern_state())&&a.visual().ready&&b.visual().ready);
        auto ea=std::make_unique<Engine>(),eb=std::make_unique<Engine>();auto*ga=setup(*ea,phase);auto*gb=setup(*eb,phase);
        CHECK(render(*ea,150003)==render(*eb,150003,true));
        CHECK(ga->visual().ready&&same(ga->pattern_state(),gb->pattern_state()));
        CHECK(ga->visual().phase==gb->visual().phase&&ga->visual().step==gb->visual().step);
        if(phase)CHECK(ea->effective_bpm_q16()>139*65536);
    }
    std::puts("ok 4/6 direct+engine block partitions, legacy carry, applied fractional clock, independent rng");
}
static void lifecycle() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();G*g=setup(*a,false,true);setup(*b,false,true);
    delete b->replace_unit(0,1,nullptr);CHECK(render(*a,90000)==render(*b,90000,true));
    // Without an audible new module, the existing input/clock/master path is exact.
    CHECK(g->visual().ready);a->chains[0].mute=true;render(*a,32);CHECK(!g->visual().ready&&!g->visual().running);
    a->chains[0].mute=false;render(*a,1000);CHECK(!g->visual().ready);render(*a,140000);CHECK(g->visual().ready);
    g->bypass=true;render(*a,256);CHECK(!g->visual().ready&&g->visual().wet==0);
    g->bypass=false;render(*a,300);CHECK(!g->visual().ready);render(*a,140000);CHECK(g->visual().ready);
    a->transport_stop();CHECK(!g->visual().ready);render(*a,200);CHECK(!g->visual().running);
    a->transport_play();render(*a,400);CHECK(!g->visual().ready);render(*a,140000);CHECK(g->visual().ready);
    a->transport_reset();CHECK(!g->visual().ready&&!g->visual().fill);
    CHECK(static_cast<Input*>(a->chains[0].units[0])->triggers==static_cast<Input*>(b->chains[0].units[0])->triggers);
    std::puts("ok 5/6 old dry path identity, engine mute/bypass/stop/reset invalidate stale PCM, no trigger ownership");
}
static Bytes read(const std::string& path) {
    FILE*f=fopen(path.c_str(),"rb");CHECK(f);CHECK(fseek(f,0,SEEK_END)==0);const long n=ftell(f);CHECK(n>=0);rewind(f);Bytes x(n);CHECK(fread(x.data(),1,x.size(),f)==x.size());fclose(f);return x;
}
static void write(const std::string& path,const Bytes& x) {FILE*f=fopen(path.c_str(),"wb");CHECK(f);CHECK(fwrite(x.data(),1,x.size(),f)==x.size());CHECK(!fclose(f));}
static uint32_t u32(const Bytes& x,size_t at){CHECK(at+4<=x.size());return uint32_t(x[at])|(uint32_t(x[at+1])<<8)|(uint32_t(x[at+2])<<16)|(uint32_t(x[at+3])<<24);}
static void put32(Bytes& x,size_t at,uint32_t v){for(int i=0;i<4;++i)x[at+i]=uint8_t(v>>(i*8));}
static size_t chunk(const Bytes& x,const char* id){for(size_t at=5;at+8<=x.size();at+=8+u32(x,at+4))if(!std::memcmp(x.data()+at,id,4))return at;return x.size();}
static void persistence() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();auto*g=new G;delete a->replace_unit(0,0,g);
    auto state=map(*g);state.cells[1]={G::Op::Repeat,2,0,4};state.cells[2]={G::Op::Pitch,5,-7,1};CHECK(g->restore_pattern(state));
    for(int p=0;p<G::P_COUNT;++p){g->params[p].src=ModSource::SigilA;g->params[p].mod_depth=q15(-1200-p);CHECK(a->set_step_lock(0,p,0,p,12345));}
    a->capture_scene(0,0);const std::string path="/tmp/seance-glitch-"+std::to_string(long(getpid()))+".snc";
    patchio::PatchDocument captured;CHECK(patchio::capture(*a,captured));g->request_reroll();
    CHECK(patchio::save(captured,path.c_str())&&patchio::load(*b,path.c_str()));
    auto* loaded=static_cast<G*>(b->chains[0].units[0]);CHECK(loaded&&loaded->kind==UnitKind::Glitch&&same(state,loaded->pattern_state()));
    CHECK(!loaded->visual().ready&&!loaded->visual().pending&&loaded->visual().kept);
    for(int p=0;p<G::P_COUNT;++p){CHECK(loaded->params[p].base==g->params[p].base&&loaded->params[p].src==ModSource::SigilA&&loaded->params[p].mod_depth==q15(-1200-p));CHECK(b->pattern.steps[0][p].lock_count==1);}
    CHECK(b->scenes[0][0].kind[0]==UnitKind::Glitch);
    std::unique_ptr<Unit> clone(clone_unit(loaded));auto*cg=static_cast<G*>(clone.get());CHECK(cg&&same(state,cg->pattern_state())&&!cg->visual().ready);
    const Bytes valid=read(path);const size_t at=chunk(valid,"GLCH"),len=u32(valid,at+4);CHECK(at<valid.size()&&len==76);
    // Reordering proves extension validation/association does not depend on CORE preceding it.
    Bytes reordered=valid;Bytes record(valid.begin()+at,valid.begin()+at+8+len);reordered.erase(reordered.begin()+at,reordered.begin()+at+8+len);reordered.insert(reordered.begin()+5,record.begin(),record.end());
    write(path,reordered);CHECK(patchio::load(*b,path.c_str()));loaded=static_cast<G*>(b->chains[0].units[0]);CHECK(same(state,loaded->pattern_state()));
    auto reject=[&](Bytes bad){Unit*before=b->chains[0].units[0];write(path,bad);CHECK(!patchio::load(*b,path.c_str()));CHECK(b->chains[0].units[0]==before&&same(state,static_cast<G*>(before)->pattern_state()));};
    for(auto mutation:std::vector<std::pair<size_t,uint8_t>>{{at+8,2},{at+9,25},{at+10,4},{at+11,6},{at+20,6},{at+21,16},{at+22,25},{at+23,0}}){auto bad=valid;bad[mutation.first]=mutation.second;reject(bad);}
    auto bad=valid;put32(bad,at+12,0);reject(bad); // zero xorshift state
    bad=valid;bad.insert(bad.end(),record.begin(),record.end());reject(bad); // duplicate extension
    bad=valid;bad[at+9]=2;put32(bad,at+4,150);bad.insert(bad.begin()+at+8+len,valid.begin()+at+10,valid.begin()+at+8+len);reject(bad); // duplicate slot
    bad=valid;bad.resize(at+8+len-1);reject(bad); // truncated record
    bad=valid;bad[at+11]=1;reject(bad); // record points to empty/non-glitch slot
    // Optional extension absent: constructor default map; no hidden historical PCM.
    Bytes absent=valid;absent.erase(absent.begin()+at,absent.begin()+at+8+len);write(path,absent);CHECK(patchio::load(*b,path.c_str()));G defaults;
    loaded=static_cast<G*>(b->chains[0].units[0]);CHECK(same(defaults.pattern_state(),loaded->pattern_state())&&!loaded->visual().ready);
    // A patch with no new kind produces no extension; old OSC mode/default values
    // and exact PCM survive load, independent of the appended kind.
    a=std::make_unique<Engine>();delete a->replace_unit(0,0,make_unit(UnitKind::Osc));
    auto*osc=static_cast<OscUnit*>(a->chains[0].units[0]);osc->clean=false;osc->shape_morph=false;
    CHECK(patchio::save(*a,path.c_str()));const auto old=read(path);CHECK(chunk(old,"GLCH")==old.size());CHECK(patchio::load(*b,path.c_str()));
    CHECK(!static_cast<OscUnit*>(b->chains[0].units[0])->clean);
    a->transport_play();b->transport_play();CHECK(render(*a,10000)==render(*b,10000,true));
    std::remove(path.c_str());std::puts("ok 6/6 immutable capture/clone, SNC7 exact cells, bounds/duplicate/truncation transactional rejection, absent/old defaults");
}
int main(){operations();history_tempo();pattern_controls();partitions();lifecycle();persistence();std::printf("glitch core: 6/6 pass, %u assertions; object %zu + PCM %u bytes\n",assertions,sizeof(G),G::CAPACITY*2u*unsigned(sizeof(q15)));}
