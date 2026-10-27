#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include "engine/vca_envelope.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#ifndef VCA_BASELINE
#include "engine/vca_timing.h"
#endif
using namespace seance;
using V=VcaUnit;
namespace ve=vca_envelope;
namespace {
unsigned long long checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
std::filesystem::path folder;
using Bytes=std::vector<uint8_t>;
void set(V& u,int p,int v){u.params[p].base=q15(v);u.params[p].snap();}
q15 tick(V& u,q15 in=32767){u.process(&in,1);return in;}
Bytes read(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void write(const std::filesystem::path& p,const Bytes& b){std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
void empty(Engine& e){for(int c=0;c<NUM_CHAINS;++c)for(int s=0;s<UNITS_PER_CHAIN;++s)delete e.replace_unit(c,s,nullptr);}
void frozen(){
    for(bool ahd:{false,true})for(int k=0;k<12;++k){
        V u;u.set_flexible(ahd);
        const int p[8]={k==0?32767:25000,k==0?0:(k*2971)%32768,k==0?0:(k*6553)%32768,
            k%3?ve::time_value((k+1)*32,true):0,k%4?0:7000,(k%3-1)*24000,(k%3-1)*-17000,k>=8?32767:0};
        for(int n=0;n<8;++n)set(u,n,p[n]);
        u.reset();uint32_t rng=1709;std::vector<q15> pcm(32768);
        for(int pos=0;pos<int(pcm.size());pos+=32){
            if(pos==0||pos==224||pos==1024||pos==6400)u.trigger();
            if(k==11){u.params[V::P_GAIN].base=q15(pos%19000);for(int n=0;n<u.num_params;++n)u.params[n].resolve(0);}
            for(int i=0;i<32;++i){rng=rng*1664525u+1013904223u;pcm[pos+i]=q15(rng>>16);}
            u.process(pcm.data()+pos,32);
        }
        std::ofstream f(folder/((ahd?"ahd-":"legacy-")+std::to_string(k)+".pcm"),std::ios::binary);
        f.write((const char*)pcm.data(),pcm.size()*sizeof(q15));CHECK(f.good());
    }
    auto e=std::make_unique<Engine>();empty(*e);
    for(int i=0;i<2;++i){auto* u=new V;u->set_flexible(i!=0);delete e->replace_unit(0,i,u);
        for(int p=0;p<u->num_params;++p)set(*u,p,p==5?-19000:p*3221+1500);
        e->capture_scene(0,0);e->set_step_lock(0,i,i,2,12345);
    }
    CHECK(patchio::save(*e,(folder/"old.snc").c_str()));
    auto loaded=std::make_unique<Engine>();CHECK(patchio::load(*loaded,(folder/"old.snc").c_str()));
    CHECK(!static_cast<V*>(loaded->chains[0].units[0])->flexible);
    CHECK(static_cast<V*>(loaded->chains[0].units[1])->flexible);
    std::puts("24 frozen legacy AD/flexible AHD PCM traces and old SNC");
}
#ifndef VCA_BASELINE
namespace vt=vca_timing;
void setup(V& u,int a=32,int h=0,int d=32) {
    const int values[V::P_COUNT]={32767,ve::time_value(a),ve::time_value(d),ve::time_value(h,true),0,0,0,0,0,0,0};
    for(int p=0;p<V::P_COUNT;++p)set(u,p,values[p]);
}
uint32_t r32(const Bytes& b,size_t i){CHECK(i+4<=b.size());return uint32_t(b[i])|(uint32_t(b[i+1])<<8)|(uint32_t(b[i+2])<<16)|(uint32_t(b[i+3])<<24);}
size_t chunk(const Bytes& b,const char* name){for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,name,4))return p;CHECK(false);return 0;}
void mappings(){
    static_assert(V::P_GAIN==0&&V::P_DUCK==7&&V::P_SYNC==8&&V::P_RETRIGGER==9&&V::P_RUN==10&&V::P_COUNT==11);
    V u;CHECK(u.modern()&&u.flexible&&u.num_params==11);
    CHECK(ve::time_frames(u.params[V::P_ATTACK].base)==480);
    CHECK(ve::time_frames(u.params[V::P_DECAY].base)==8000);
    for(int i=0;i<vt::COUNT;++i)for(bool hold:{false,true}){
        CHECK(vt::division(vt::division_value(i,hold),hold)==i);
        CHECK(vt::division_name(i)[0]);
    }
    CHECK(vt::division(0,true)==-1&&vt::sync_frames(0,120<<16,true)==0);
    for(int bpm:{20<<16,120<<16,137*65536+12345,300<<16})for(bool hold:{false,true}){
        uint32_t prev=0;
        for(int raw=0;raw<32768;++raw){
            const uint32_t f=vt::sync_frames(q15(raw),bpm,hold);CHECK(f>=prev);prev=f;
            const int div=vt::division(q15(raw),hold);
            if(div>=0){const double exact=32000.*60.*65536.*vt::DIVISIONS[div].ticks/(double(bpm)*192.);CHECK(std::abs(double(f)-exact)<=.50001);}
        }
    }
    for(int i=0;i<3;++i)CHECK(vt::retrigger(vt::retrigger_value(i))==i);
    CHECK(vt::retrigger(-32768)==0&&vt::retrigger(32767)==2);
    CHECK(!vt::enabled(16383)&&vt::enabled(16384));
    std::puts("integer sync29 divisions, holdoff, fractional Q16 tempo and endpoints");
}
void timing(){
    for(int bpm:{20<<16,120<<16,300<<16})for(int div:{0,10,16,28}){
        V u;setup(u);u.set_clock_bpm_q16(bpm);set(u,V::P_SYNC,32767);
        set(u,V::P_ATTACK,vt::division_value(div));set(u,V::P_DECAY,vt::division_value(div));
        const uint32_t n=vt::sync_frames(u.params[V::P_ATTACK].base,bpm);u.trigger();
        for(uint32_t i=1;i<=n;++i){tick(u);CHECK(u.visual_stage()==1);CHECK(u.visual_progress_q16()==uint32_t((uint64_t(i)<<16)/n));}
        CHECK(u.visual_envelope()==32767&&u.visual_stage_frames(V::P_ATTACK)==n);
        for(uint32_t i=1;i<=n;++i){tick(u);CHECK(u.visual_stage()==3);CHECK(u.visual_progress_q16()==uint32_t((uint64_t(i)<<16)/n));}
        CHECK(u.visual_envelope()==0&&u.visual_stage_frames(V::P_HOLD)==0);CHECK(tick(u)==0&&u.visual_stage()==0);
    }
    V u;setup(u);set(u,V::P_SYNC,32767);set(u,V::P_ATTACK,vt::division_value(16));
    set(u,V::P_DECAY,vt::division_value(16));set(u,V::P_HOLD,vt::division_value(10,true));
    u.set_clock_bpm_q16(120<<16);u.trigger();for(int i=0;i<8000;++i)tick(u);
    u.set_clock_bpm_q16(240<<16);set(u,V::P_ATTACK,vt::division_value(0));
    for(int i=8000;i<16000;++i)tick(u);
    CHECK(u.visual_stage()==1&&u.visual_envelope()==32767&&u.visual_stage_frames(V::P_ATTACK)==16000);
    tick(u);CHECK(u.visual_stage()==2&&u.visual_stage_frames(V::P_HOLD)==2000);
    for(int i=1;i<2000;++i){tick(u);}tick(u);CHECK(u.visual_stage()==3&&u.visual_stage_frames(V::P_DECAY)==8000);
    std::puts("actual DSP exact free/sync stage timing incl48s stage and stage-entry tempo latching");
}
void launch(){
    V soft;setup(soft,320,0,640);soft.trigger();for(int i=0;i<640;++i)tick(soft);
    const int origin=soft.visual_envelope();CHECK(origin>16000&&origin<17000);
    soft.trigger();CHECK(tick(soft)>=origin&&soft.visual_stage()==1);
    V hard;setup(hard,320,0,640);set(hard,V::P_RETRIGGER,vt::retrigger_value(1));hard.trigger();
    for(int i=0;i<640;++i){tick(hard);}const int last=hard.visual_gain();hard.trigger();tick(hard);
    CHECK(hard.visual_envelope()<200&&std::abs(int(hard.visual_gain())-last)<=513);
    for(int i=1;i<64;++i){tick(hard);}CHECK(hard.visual_gain()==hard.visual_envelope());
    V ignore,reference;setup(ignore,320,0,640);setup(reference,320,0,640);
    set(ignore,V::P_RETRIGGER,vt::retrigger_value(2));ignore.trigger();reference.trigger();
    for(int i=0;i<1000;++i){if(i==100||i==320||i==700)ignore.trigger();CHECK(tick(ignore)==tick(reference));}
    ignore.trigger();CHECK(tick(ignore)>0&&ignore.visual_stage()==1);
    V cycle;setup(cycle);set(cycle,V::P_RUN,32767);
    for(int i=0;i<200;++i){CHECK(tick(cycle)==0);}CHECK(!cycle.visual_cycling());
    cycle.trigger();std::vector<q15> first(64);for(auto& s:first)s=tick(cycle);
    for(int n=0;n<4;++n)for(auto s:first)CHECK(tick(cycle)==s);
    CHECK(cycle.visual_cycling());tick(cycle);set(cycle,V::P_RUN,0);
    for(int i=0;i<64;++i){tick(cycle);}CHECK(!cycle.visual_cycling()&&cycle.visual_stage()==0);
    set(cycle,V::P_RUN,32767);CHECK(tick(cycle)==0);cycle.trigger();tick(cycle);cycle.stop_cycle();
    for(int i=0;i<64;++i){tick(cycle);}CHECK(cycle.visual_stage()==0);
    cycle.trigger();tick(cycle);cycle.reset();CHECK(!cycle.visual_cycling()&&!cycle.visual_ready());
    std::puts("soft/reset/ignore +2ms multiplier bridge; trigger-armed exact periodic cycle, finish on off/stop, reset");
}
void endpoints_and_blocks(){
    V u;setup(u,32000,32000,32);set(u,V::P_FLOOR,32767);for(int i=0;i<64;++i)tick(u);
    for(int p=0;p<u.num_params;++p)u.params[p].resolve(0);
    u.params[V::P_GAIN].base=0;u.params[V::P_HOLD].base=0;u.params[V::P_FLOOR].base=0;u.params[V::P_ATTACK].base=32767;
    for(int i=0;i<200;++i){for(int p=0;p<u.num_params;++p)u.params[p].resolve(0);q15 block[32]={};u.process(block,32);}
    CHECK(u.params[V::P_GAIN].effective>0&&u.visual_gain()==0);
    u.trigger();tick(u);CHECK(u.visual_stage_frames(V::P_ATTACK)==256000);
    set(u,V::P_ATTACK,0);u.trigger();for(int i=0;i<33;++i)CHECK(tick(u)==0);
    CHECK(u.visual_stage()==3&&u.visual_stage_frames(V::P_HOLD)==0);
    V a,b;setup(a,320,96,640);setup(b,320,96,640);set(a,V::P_RUN,32767);set(b,V::P_RUN,32767);
    set(a,V::P_RETRIGGER,vt::retrigger_value(1));set(b,V::P_RETRIGGER,vt::retrigger_value(1));
    uint32_t rng=1521;
    for(int k=0;k<50;++k){if(k%3==0){a.trigger();b.trigger();}const int n=137+k*7;std::vector<q15>x(n),y;
        for(auto& s:x){rng=rng*1664525u+1013904223u;s=q15(rng>>16);}y=x;
        a.process(x.data(),n);for(int pos=0;pos<n;){int m=std::min(17,n-pos);b.process(y.data()+pos,m);pos+=m;}CHECK(x==y);
    }
    V old(false),fresh;setup(fresh,480,0,8000);for(int p=0;p<8;++p)set(old,p,fresh.params[p].base);
    for(int k=0;k<24000;++k){if(k%1000==0){old.trigger();fresh.trigger();}CHECK(tick(old)==tick(fresh));}
    for(int gain:{0,1,16000,32767})for(int floor:{0,10000,32767})for(int duck:{0,32767}){
        V v;setup(v);set(v,V::P_GAIN,gain);set(v,V::P_FLOOR,floor);set(v,V::P_DUCK,duck);v.trigger();
        for(int i=0;i<100;++i){const q15 out=tick(v);CHECK(out>=0&&out<=gain);CHECK(v.visual_envelope()>=floor);}
    }
    std::puts("real shared slew endpoint resolution, zero hold/mute, neutral old-AHD equivalence, block invariance and bounds");
}
void engine(){
    for(int group=0;group<4;++group){auto e=std::make_unique<Engine>();empty(*e);auto* u=new V;
        delete e->replace_unit(0,0,new OscUnit);delete e->replace_unit(0,1,u);e->set_bpm(137);
        CHECK(u->clock_bpm_q16()==(137<<16));q15 before[V::P_COUNT];for(int p=0;p<V::P_COUNT;++p)before[p]=u->params[p].base;
        const q15 locks[V::P_COUNT]={27000,21000,18000,0,5000,-15000,15000,32767,32767,32767,32767};
        e->pattern.steps[0][0].flags=SeqStep::Trig;e->set_sequence_length(0,1);
        for(int p=group*3;p<std::min<int>(V::P_COUNT,group*3+3);++p)CHECK(e->set_step_lock(0,0,1,p,locks[p]));
        e->set_sequencer_enabled(true);e->transport_reset();e->transport_play();q15 out[256]={};
        for(int i=0;i<80;++i)e->render(out,128);
        for(int p=group*3;p<std::min<int>(V::P_COUNT,group*3+3);++p){CHECK(u->params[p].step_lock_override);CHECK(vt::resolved(u->params[p],p>=V::P_SYNC)==locks[p]);CHECK(u->params[p].base==before[p]);}
        e->transport_stop();CHECK(!u->visual_cycling());e->set_sequencer_enabled(false);
        for(int i=0;i<100;++i)e->render(out,128);
        for(int p=0;p<V::P_COUNT;++p)CHECK(!u->params[p].step_lock_override);
    }
    // Downstream extended duck/floor accepts ARP notes without pretending to be a gate.
    auto e=std::make_unique<Engine>();empty(*e);delete e->replace_unit(0,0,new ArpUnit);
    delete e->replace_unit(0,1,new OscUnit);auto* u=new V;delete e->replace_unit(0,2,u);
    setup(*u,32000,0,32000);set(*u,V::P_DUCK,32767);set(*u,V::P_FLOOR,8000);
    CHECK(!u->sequencer_properties().gate_processor);e->transport_play();q15 out[256]={};
    bool active=false;for(int i=0;i<100;++i){e->render(out,128);active|=u->visual_stage()!=0;}CHECK(active);
    e->transport_reset();CHECK(!u->visual_ready());
    std::puts("actual Engine all11 sequence locks, clock handoff, stop/reset and downstream ARP duck/floor events");
}
void persistence(){
    auto e=std::make_unique<Engine>();empty(*e);auto* u=new V;delete e->replace_unit(0,0,u);
    for(int p=0;p<V::P_COUNT;++p){set(*u,p,p==5?-12345:1500+p*1700);u->params[p].src=ModSource::SigilG;u->params[p].mod_depth=q15(p*333);u->params[p].mod_enabled=p%2;}
    for(auto& s:e->scenes[0]){s.kind[0]=UnitKind::Vca;for(int p=0;p<V::P_COUNT;++p)s.base[0][p]=q15(p*1700);}
    for(auto& b:e->pattern_banks)for(int p=0;p<V::P_COUNT;++p){auto& s=b.steps[0][p];s.lock_count=1;s.locks[0]={0,uint8_t(p),q15(p*1600)};}
    for(int p=0;p<V::P_COUNT;++p){auto& s=e->pattern.steps[0][p];s.lock_count=1;s.locks[0]={0,uint8_t(p),q15(p*1600)};}
    e->macro_pads[0].route_count=1;e->macro_pads[0].routes[0].chain=0;e->macro_pads[0].routes[0].slot=0;e->macro_pads[0].routes[0].param=10;
    CHECK(patchio::save(*e,(folder/"extended.snc").c_str()));const auto bytes=read(folder/"extended.snc");
    CHECK(bytes[3]=='F'&&bytes[4]==15&&bytes[chunk(bytes,"UNIT")+8]==3);
    auto loaded=std::make_unique<Engine>();CHECK(patchio::load(*loaded,(folder/"extended.snc").c_str()));
    auto* got=static_cast<V*>(loaded->chains[0].units[0]);CHECK(got->modern()&&got->num_params==11&&got->params[5].bipolar&&got->params[6].bipolar);
    for(int p=0;p<V::P_COUNT;++p){CHECK(got->params[p].base==u->params[p].base&&got->params[p].src==u->params[p].src&&got->params[p].mod_depth==u->params[p].mod_depth&&got->params[p].mod_enabled==u->params[p].mod_enabled);for(const auto& b:loaded->pattern_banks)CHECK(b.steps[0][p].lock_count==1);}
    CHECK(loaded->macro_pads[0].route_count==1&&loaded->scenes[0][0].base[0][10]==17000);
    std::unique_ptr<Unit> copy(clone_unit(got));CHECK(static_cast<V*>(copy.get())->modern()&&copy->num_params==11);
    CHECK(patchio::save(*loaded,(folder/"extended-roundtrip.snc").c_str()));CHECK(bytes==read(folder/"extended-roundtrip.snc"));
    const auto reordered=[&](Bytes b){const size_t at=chunk(b,"UNIT"),n=8+NUM_CHAINS*UNITS_PER_CHAIN;Bytes unit(b.begin()+at,b.begin()+at+n);b.erase(b.begin()+at,b.begin()+at+n);b.insert(b.begin()+5,unit.begin(),unit.end());return b;};
    write(folder/"reordered.snc",reordered(bytes));CHECK(patchio::load(*loaded,(folder/"reordered.snc").c_str()));CHECK(static_cast<V*>(loaded->chains[0].units[0])->modern());
    for(int flags:{0,1}){auto old=bytes;old[3]='7';old[4]=7;old[chunk(old,"UNIT")+8]=uint8_t(flags);old=reordered(old);
        write(folder/"old-padding.snc",old);CHECK(patchio::load(*loaded,(folder/"old-padding.snc").c_str()));got=static_cast<V*>(loaded->chains[0].units[0]);
        const int count=flags?8:3;CHECK(!got->modern()&&got->num_params==count&&got->flexible==bool(flags));
        for(int p=count;p<PARAM_BANK_SIZE;++p){CHECK(got->params[p].base==0&&got->params[p].src==ModSource::None);for(const auto& s:loaded->scenes[0])CHECK(s.base[0][p]==0);if(p<11)for(const auto& b:loaded->pattern_banks)CHECK(b.steps[0][p].lock_count==0);}
        CHECK(loaded->macro_pads[0].route_count==0);copy.reset(clone_unit(got));CHECK(!static_cast<V*>(copy.get())->modern()&&copy->num_params==count);
    }
    for(int flags:{2,4,7,128,255}){auto bad=bytes;bad[chunk(bad,"UNIT")+8]=uint8_t(flags);write(folder/"bad.snc",bad);auto* sentinel=loaded->chains[0].units[0];CHECK(!patchio::load(*loaded,(folder/"bad.snc").c_str())&&loaded->chains[0].units[0]==sentinel);}
    for(int ver=7;ver<15;++ver){auto bad=bytes;bad[3]=uint8_t(ver<10?'0'+ver:'A'+ver-10);bad[4]=uint8_t(ver);write(folder/"bad-version.snc",bad);auto* sentinel=loaded->chains[0].units[0];CHECK(!patchio::load(*loaded,(folder/"bad-version.snc").c_str())&&loaded->chains[0].units[0]==sentinel);}
    delete e->replace_unit(0,1,new FoldUnit);delete e->replace_unit(0,2,new TiltUnit);
    CHECK(patchio::save(*e,(folder/"combined.snc").c_str()));CHECK(read(folder/"combined.snc")[4]==15);
    delete e->replace_unit(0,0,nullptr);CHECK(patchio::save(*e,(folder/"tilt-neighbor.snc").c_str()));CHECK(read(folder/"tilt-neighbor.snc")[4]==14);
    delete e->replace_unit(0,2,nullptr);CHECK(patchio::save(*e,(folder/"fold-neighbor.snc").c_str()));CHECK(read(folder/"fold-neighbor.snc")[4]==13);
    std::puts("SNCF15 and flag3; all11 params/routes/scenes/banks/clone; chunk reorder; old tails and unknown/pre15 flag refusal; concurrent format priorities");
}
void extended_tests(){mappings();timing();launch();endpoints_and_blocks();engine();persistence();}
#endif
}
int main(int argc,char** argv){
    if(argc==3&&!std::strcmp(argv[1],"--old-reader")){
        auto e=std::make_unique<Engine>();e->set_bpm(173);
        CHECK(!patchio::load(*e,argv[2]));CHECK(e->bpm==173);
        std::puts("retained old reader rejects extended file transactionally");return 0;
    }
    CHECK(argc==2);folder=argv[1];frozen();
#ifndef VCA_BASELINE
    extended_tests();
#endif
    std::printf("vca core %llu checks passed\n",checks);
}
