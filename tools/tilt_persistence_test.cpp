// Focused actual-engine Tilt SNC1..7, sound-mode isolation, trim routes and rollback.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/tilt_eq.h"
#include "engine/patch_io.h"
#include <algorithm>
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
uint64_t checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Bytes=std::vector<uint8_t>;std::string path;
Bytes read(){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void put(const Bytes& b){std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
uint32_t r32(const Bytes& b,size_t at){CHECK(at+4<=b.size());return b[at]|(uint32_t)b[at+1]<<8|(uint32_t)b[at+2]<<16|(uint32_t)b[at+3]<<24;}
void w16(Bytes& b,size_t at,int value){CHECK(at+2<=b.size());b[at]=(uint8_t)value;b[at+1]=(uint8_t)((uint16_t)value>>8);}
size_t chunk(const Bytes& b,const char* id){for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;CHECK(false);return 0;}
void set(TiltUnit& u,int p,int value){u.params[p].base=(q15)value;u.params[p].snap();}
std::vector<q15> pcm(TiltUnit& u){
    for(int p=0;p<u.num_params;++p)u.params[p].snap();
    u.reset();
    std::vector<q15> audio(8192);uint32_t rng=101;for(auto& s:audio){rng=rng*1664525u+1013904223u;s=(q15)(rng>>16);}
    u.process(audio.data(),317);u.process(audio.data()+317,(int)audio.size()-317);return audio;
}
void check_hidden(const TiltUnit& u){
    CHECK(!u.modern()&&u.num_params==5);
    CHECK(u.params[5].base==tilt_eq::TRIM_UNITY&&u.params[5].effective==tilt_eq::TRIM_UNITY&&u.params[5].target==tilt_eq::TRIM_UNITY);
    CHECK(u.params[5].src==ModSource::None&&u.params[5].mod_depth==0&&!u.params[5].step_lock_override&&!u.params[5].temp_override);
    for(int p=6;p<PARAM_BANK_SIZE;++p)CHECK(u.params[p].base==0&&u.params[p].mod_depth==0&&u.params[p].src==ModSource::None);
}
void roundtrip(){
    for(bool modern:{false,true})for(int range:{0,21846})for(int trim:{0,int(tilt_eq::TRIM_UNITY),32767}){
        auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
        const int values[]={-23177,29001,12553,21177,range,trim};auto* u=new TiltUnit(modern);delete a->replace_unit(0,0,u);
        for(int p=0;p<u->num_params;++p){set(*u,p,values[p]);u->params[p].src=(ModSource)(1+p%5);u->params[p].mod_depth=(q15)(-7000+p*1173);}
        if(!modern)for(int p=5;p<12;++p){set(*u,p,31337);u->params[p].src=ModSource::EnvFollow;u->params[p].mod_depth=32767;}
        a->capture_scene(0,0);for(int p=0;p<u->num_params;++p)set(*u,p,(p*2711+193)%32768);a->capture_scene(0,1);
        for(int p=0;p<u->num_params;++p){set(*u,p,values[p]);CHECK(a->set_step_lock(0,p,0,p,p?17000+p:-17000));}
        a->macro_pads[0].route_count=1;a->macro_pads[0].routes[0]={0,0,5,0,32767};
        CHECK(patchio::save(*a,path.c_str()));const auto before=read();CHECK(before[chunk(before,"UNIT")+8]==(modern?1:0));
        CHECK(patchio::load(*b,path.c_str()));auto* got=static_cast<TiltUnit*>(b->chains[0].units[0]);
        CHECK(got&&got->kind==UnitKind::Tilt&&got->modern()==modern&&got->num_params==(modern?6:5));
        for(int p=0;p<got->num_params;++p){CHECK(got->params[p].base==u->params[p].base);CHECK(got->params[p].src==u->params[p].src&&got->params[p].mod_depth==u->params[p].mod_depth);CHECK(got->params[p].bipolar==(p==0));CHECK(b->pattern.steps[0][p].locks[0].param==p&&b->pattern.steps[0][p].locks[0].value==(p?17000+p:-17000));for(int scene=0;scene<2;++scene)CHECK(b->scenes[0][scene].base[0][p]==a->scenes[0][scene].base[0][p]);}
        if(!modern)check_hidden(*got);
        CHECK(b->macro_pads[0].route_count==1&&b->macro_pads[0].routes[0].param==5);
        CHECK(pcm(*got)==pcm(*u));std::unique_ptr<Unit> clone(clone_unit(got));auto* copy=static_cast<TiltUnit*>(clone.get());
        CHECK(copy&&copy->modern()==modern&&copy->num_params==got->num_params);CHECK(pcm(*copy)==pcm(*got));if(!modern)check_hidden(*copy);
        CHECK(patchio::save(*b,path.c_str()));CHECK(read()==before);
        // Required UNIT can precede CORE; staged trim and its route must survive.
        Bytes reordered=before;size_t pos=chunk(reordered,"UNIT"),end=pos+8+r32(reordered,pos+4);Bytes unit(reordered.begin()+pos,reordered.begin()+end);reordered.erase(reordered.begin()+pos,reordered.begin()+end);reordered.insert(reordered.begin()+5,unit.begin(),unit.end());put(reordered);CHECK(patchio::load(*b,path.c_str()));
        got=static_cast<TiltUnit*>(b->chains[0].units[0]);CHECK(got->modern()==modern);for(int p=0;p<got->num_params;++p)CHECK(got->params[p].base==values[p]);
    }
    std::puts("ok legacy5/modern6 SNC7 flags, both RANGE laws and trim min/unity/max, routes/scenes/locks/macros/clone/PCM/byte resave, UNIT-before-CORE");
}
void historical(){
    auto b=std::make_unique<Engine>();
    for(int version=1;version<=6;++version)for(int range:{0,21846}){
        Bytes old={'S','N','C',(uint8_t)('0'+version),(uint8_t)version};const int raw[]={23177,31000,12000,21000,range,-27777};
        for(int c=0;c<NUM_CHAINS;++c){old.insert(old.end(),{255,127});if(version>=2)old.insert(old.end(),{0,0});old.push_back(0);size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);if(!c){old[first]=(uint8_t)UnitKind::Tilt;for(int p=0;p<6;++p){w16(old,first+3+p*5,raw[p]);old[first+3+p*5+2]=5;w16(old,first+3+p*5+3,19001);}}}
        old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);
        if(version>=3){old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0);}
        if(version>=4){const size_t flags=old.size();old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);old[flags]=255;} // historical bits are not modern sound flags
        if(version>=5){old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0});}
        if(version>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
        put(old);CHECK(patchio::load(*b,path.c_str()));auto* got=static_cast<TiltUnit*>(b->chains[0].units[0]);check_hidden(*got);
        TiltUnit ref(false);for(int p=0;p<5;++p){set(ref,p,raw[p]);CHECK(got->params[p].base==raw[p]);}CHECK(pcm(*got)==pcm(ref));
        CHECK(patchio::save(*b,path.c_str()));auto snc7=read();CHECK(snc7[chunk(snc7,"UNIT")+8]==0);const size_t core=chunk(snc7,"CORE")+8;
        for(int p=5;p<12;++p){w16(snc7,core+5+3+p*5,27777+p);snc7[core+5+3+p*5+2]=5;w16(snc7,core+5+3+p*5+3,29000);}
        put(snc7);CHECK(patchio::load(*b,path.c_str()));got=static_cast<TiltUnit*>(b->chains[0].units[0]);check_hidden(*got);CHECK(pcm(*got)==pcm(ref));
        CHECK(patchio::save(*b,path.c_str()));snc7=read();for(int p=5;p<12;++p)for(int j=0;j<5;++j)CHECK(snc7[core+5+3+p*5+j]==0);
    }
    std::puts("ok SNC1..6 both RANGE laws, old flags never opt in, poisoned sixth slot/SNC7 tails reset to hidden unity/no routes and canonical zero on disk");
}
void corrupt_transactional(){
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();delete a->replace_unit(0,0,new TiltUnit);CHECK(patchio::save(*a,path.c_str()));const Bytes valid=read();
    delete b->replace_unit(1,2,new TiltUnit(false));auto* sentinel=b->chains[1].units[2];sentinel->params[4].base=17831;b->set_bpm(173);
    std::vector<Bytes> bad;for(size_t n:{size_t(0),size_t(4),chunk(valid,"UNIT")+8+3,chunk(valid,"CORE")+8+71})bad.emplace_back(valid.begin(),valid.begin()+n);
    auto changed=valid;const size_t unit=chunk(valid,"UNIT"),core=chunk(valid,"CORE");changed[unit+4]=1;changed[unit+5]=changed[unit+6]=changed[unit+7]=0;bad.push_back(changed);
    changed=valid;changed[core+8+5]=255;bad.push_back(changed);changed=valid;changed.erase(changed.begin()+unit,changed.begin()+unit+8+r32(valid,unit+4));bad.push_back(changed);
    for(int flag:{2,3,128,255}){changed=valid;changed[unit+8]=(uint8_t)flag;bad.push_back(changed);}
    changed=valid;changed.insert(changed.end(),valid.begin()+unit,valid.begin()+unit+8+r32(valid,unit+4));bad.push_back(changed);
    for(const auto& bytes:bad){put(bytes);CHECK(!patchio::load(*b,path.c_str()));CHECK(b->chains[1].units[2]==sentinel&&b->bpm==173&&sentinel->params[4].base==17831);}
    std::puts("ok truncation/missing/duplicate UNIT, unknown Tilt flags, malformed CORE reject without touching installed patch");
}
void hidden_runtime_safety(){
    for(bool modern:{false,true}){auto e=std::make_unique<Engine>();auto* u=new TiltUnit(modern);delete e->replace_unit(0,0,u);
        e->capture_scene(0,0);e->capture_scene(0,1);e->scenes[0][0].base[0][5]=0;e->scenes[0][1].base[0][5]=32767;
        e->apply_scene_morph(0,32767);CHECK(u->params[5].base==(modern?32766:tilt_eq::TRIM_UNITY));
        e->macro_pads[0].route_count=1;e->macro_pads[0].routes[0]={0,0,5,0,0};e->apply_macro_pad(0,32767);CHECK(u->params[5].base==(modern?0:tilt_eq::TRIM_UNITY));
        CHECK(e->set_step_lock(0,0,0,5,19000));CHECK(e->audition_sequence_step(0,0));
        CHECK(u->params[5].audition_override==modern);if(modern)CHECK(u->params[5].audition==19000);else check_hidden(*u);
        e->cancel_sequence_audition();q15 buffer[64]={};e->render(buffer,32);
        CHECK(!u->params[5].audition_override);if(!modern)check_hidden(*u);
        u->params[5].src=ModSource::SigilA;u->params[5].mod_depth=32767;u->params[5].base=30000;
        // Modern trim uses the normal unipolar modulation clamp; old trim is not resolved.
        if(modern){u->params[5].snap();u->params[5].resolve(32767);CHECK(u->params[5].effective==32767);u->params[5].snap();u->params[5].resolve(-32767);CHECK(u->params[5].effective==0);}
    }
    std::puts("ok actual Engine scene/macro/armed-step audition obey num_params; hidden legacy trim inert, modern trim routes/locks and unipolar clamp active");
}
}
int main(){path="/tmp/seance-tilt-persistence-"+std::to_string((long)getpid())+".snc";roundtrip();historical();corrupt_transactional();hidden_runtime_safety();std::remove(path.c_str());std::printf("tilt persistence: 4/4 passed (%llu checks)\n",(unsigned long long)checks);}
