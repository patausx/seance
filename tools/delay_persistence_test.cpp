// Focused real-engine Delay SNC1..7, modern flags, clock integration and failure isolation.
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/delay_time.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <unistd.h>
using namespace seance;
static bool reject_modern=false;static size_t modern_allocations=0;
void* operator new[](size_t n){if(n==256004){++modern_allocations;if(reject_modern)throw std::bad_alloc();}if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void* operator new[](size_t n,const std::nothrow_t&) noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete[](void* p,size_t) noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept{std::free(p);}
namespace {
uint64_t checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Bytes=std::vector<uint8_t>;std::string path;
Bytes read(){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void put(const Bytes& b){std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
uint32_t r32(const Bytes& b,size_t at){CHECK(at+4<=b.size());return b[at]|(uint32_t)b[at+1]<<8|(uint32_t)b[at+2]<<16|(uint32_t)b[at+3]<<24;}
void w16(Bytes& b,size_t at,int value){CHECK(at+2<=b.size());b[at]=(uint8_t)value;b[at+1]=(uint8_t)(value>>8);}
size_t chunk(const Bytes& b,const char* id){for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;CHECK(false);return 0;}
void set(DelayUnit& u,int p,int value){u.params[p].base=(q15)value;u.params[p].snap();}
std::vector<q15> pcm(DelayUnit& u){
    for(int p=0;p<u.num_params;++p) { u.params[p].snap(); }
    u.set_clock_bpm_q16(137<<16);u.control_tick();u.reset();
    std::vector<q15> audio(35000);uint32_t rng=101;for(auto& s:audio){rng=rng*1664525u+1013904223u;s=(q15)(rng>>16);}
    u.process(audio.data(),317);u.process(audio.data()+317,(int)audio.size()-317);return audio;
}
void roundtrip(){
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
    const int values[]={21031,29001,28553,13177,9500,0,25139,32767,delay_time::division_value(3),delay_time::feel_value(2),32767};
    for(bool modern:{false,true}){
        auto* u=new DelayUnit(modern);delete a->replace_unit(0,0,u);
        for(int p=0;p<u->num_params;++p){set(*u,p,values[p]);u->params[p].src=(ModSource)(1+p%4);u->params[p].mod_depth=(q15)(-7000+p*1173);}
        if(!modern)for(int p=6;p<12;++p)set(*u,p,31337);
        a->capture_scene(0,0);for(int p=0;p<u->num_params;++p)set(*u,p,(p*2711+193)%32768);a->capture_scene(0,1);
        for(int p=0;p<u->num_params;++p){set(*u,p,values[p]);CHECK(a->set_step_lock(0,p,0,p,17000+p));}
        CHECK(patchio::save(*a,path.c_str()));const auto before=read();CHECK(before[chunk(before,"UNIT")+8]==(modern?1:0));
        CHECK(patchio::load(*b,path.c_str()));auto* got=static_cast<DelayUnit*>(b->chains[0].units[0]);
        CHECK(got&&got->kind==UnitKind::Delay&&got->modern()==modern&&got->num_params==(modern?11:6));
        for(int p=0;p<got->num_params;++p){CHECK(got->params[p].base==u->params[p].base);CHECK(got->params[p].src==u->params[p].src&&got->params[p].mod_depth==u->params[p].mod_depth);CHECK(!got->params[p].bipolar);CHECK(b->pattern.steps[0][p].locks[0].param==p&&b->pattern.steps[0][p].locks[0].value==17000+p);for(int scene=0;scene<2;++scene)CHECK(b->scenes[0][scene].base[0][p]==a->scenes[0][scene].base[0][p]);}
        if(!modern)for(int p=6;p<12;++p)CHECK(got->params[p].base==0);
        CHECK(pcm(*got)==pcm(*u));std::unique_ptr<Unit> clone(clone_unit(got));auto* copy=static_cast<DelayUnit*>(clone.get());
        CHECK(copy&&copy->modern()==modern&&copy->num_params==got->num_params);CHECK(pcm(*copy)==pcm(*got));
        CHECK(patchio::save(*b,path.c_str()));CHECK(read()==before);
        // UNIT may precede CORE: modern tail must be staged, not dropped or overwritten.
        Bytes reordered=before;size_t pos=chunk(reordered,"UNIT"),end=pos+8+r32(reordered,pos+4);Bytes unit(reordered.begin()+pos,reordered.begin()+end);reordered.erase(reordered.begin()+pos,reordered.begin()+end);reordered.insert(reordered.begin()+5,unit.begin(),unit.end());put(reordered);CHECK(patchio::load(*b,path.c_str()));
        got=static_cast<DelayUnit*>(b->chains[0].units[0]);CHECK(got->modern()==modern);for(int p=0;p<got->num_params;++p)CHECK(got->params[p].base==values[p]);
    }
    std::puts("ok modern11/legacy6 SNC7 flags, values/routes/scenes/locks, clone/PCM/byte-stable resave and UNIT-before-CORE");
}
void historical(){
    auto b=std::make_unique<Engine>();
    for(int version=1;version<=6;++version){
        Bytes old={'S','N','C',(uint8_t)('0'+version),(uint8_t)version};const int raw[]={23177,32000,28000,21000,31300,version%2?0:32767};
        for(int c=0;c<NUM_CHAINS;++c){old.insert(old.end(),{255,127});if(version>=2)old.insert(old.end(),{0,0});old.push_back(0);size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);if(!c){old[first]=(uint8_t)UnitKind::Delay;for(int p=0;p<6;++p)w16(old,first+3+p*5,raw[p]);}}
        old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);
        if(version>=3){old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0);}
        if(version>=4){const size_t flags=old.size();old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);old[flags]=1;} // pre-SNC7 bit must not opt in
        if(version>=5){old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0});}
        if(version>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
        put(old);const size_t allocations=modern_allocations;reject_modern=true;CHECK(patchio::load(*b,path.c_str()));reject_modern=false;CHECK(modern_allocations==allocations);
        auto* got=static_cast<DelayUnit*>(b->chains[0].units[0]);CHECK(!got->modern()&&got->num_params==6);DelayUnit ref(false);for(int p=0;p<6;++p){set(ref,p,raw[p]);CHECK(got->params[p].base==raw[p]);}CHECK(pcm(*got)==pcm(ref));
        CHECK(patchio::save(*b,path.c_str()));auto snc7=read();CHECK(snc7[chunk(snc7,"UNIT")+8]==0);const size_t core=chunk(snc7,"CORE")+8;
        for(int p=6;p<12;++p){w16(snc7,core+5+3+p*5,27777+p);snc7[core+5+3+p*5+2]=4;w16(snc7,core+5+3+p*5+3,29000);}
        put(snc7);reject_modern=true;CHECK(patchio::load(*b,path.c_str()));reject_modern=false;CHECK(modern_allocations==allocations);got=static_cast<DelayUnit*>(b->chains[0].units[0]);CHECK(!got->modern()&&got->num_params==6);CHECK(pcm(*got)==pcm(ref));
        for(int p=6;p<12;++p)CHECK(got->params[p].base==0&&got->params[p].mod_depth==0&&got->params[p].src==ModSource::None);
        CHECK(patchio::save(*b,path.c_str()));snc7=read();for(int p=6;p<12;++p)for(int j=0;j<5;++j)CHECK(snc7[core+5+3+p*5+j]==0);
    }
    std::puts("ok SNC1..6 exact legacy6/PCM even with old bit1, legacy-only64kb allocation, SNC7 poisoned tail ignored/canonical zero");
}
void corrupt_transactional(){
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();delete a->replace_unit(0,0,new DelayUnit);CHECK(patchio::save(*a,path.c_str()));const Bytes valid=read();
    delete b->replace_unit(1,2,new DelayUnit(false));auto* sentinel=b->chains[1].units[2];sentinel->params[4].base=17831;b->set_bpm(173);
    std::vector<Bytes> bad;for(size_t n:{size_t(0),size_t(4),chunk(valid,"UNIT")+8+3,chunk(valid,"CORE")+8+71})bad.emplace_back(valid.begin(),valid.begin()+n);
    auto changed=valid;const size_t unit=chunk(valid,"UNIT"),core=chunk(valid,"CORE");changed[unit+4]=1;changed[unit+5]=changed[unit+6]=changed[unit+7]=0;bad.push_back(changed);
    changed=valid;changed[core+8+5]=255;bad.push_back(changed);changed=valid;changed.erase(changed.begin()+unit,changed.begin()+unit+8+r32(valid,unit+4));bad.push_back(changed);
    for(int flag:{2,3,128,255}){changed=valid;changed[unit+8]=(uint8_t)flag;bad.push_back(changed);}
    changed=valid;changed.insert(changed.end(),valid.begin()+unit,valid.begin()+unit+8+r32(valid,unit+4));bad.push_back(changed);
    for(const auto& bytes:bad){put(bytes);CHECK(!patchio::load(*b,path.c_str()));CHECK(b->chains[1].units[2]==sentinel&&b->bpm==173&&sentinel->params[4].base==17831);}
    put(valid);reject_modern=true;CHECK(!patchio::load(*b,path.c_str()));reject_modern=false;CHECK(b->chains[1].units[2]==sentinel&&b->bpm==173&&sentinel->params[4].base==17831);
    std::puts("ok truncation/missing/duplicate UNIT, unknown Delay flags, malformed CORE and modern allocation failure transactionally isolated");
}
void clock_integration(){
    auto e=std::make_unique<Engine>();auto* u=new DelayUnit;delete e->replace_unit(0,0,u);set(*u,7,32767);set(*u,8,delay_time::division_value(4));set(*u,9,delay_time::feel_value(0));e->set_bpm(173);
    q15 audio[64]={};e->render(audio,32);auto v=u->visual_state();CHECK(v.bpm_q16==e->effective_bpm_q16());CHECK(v.target_frames==delay_time::sync_frames(u->params[8].target,u->params[9].target,v.bpm_q16));
    CHECK(e->set_clock_mod(ClockTarget::Tempo,ModSource::SigilA,37));e->set_bpm(137);
    for(int i=0;i<20;++i){e->render(audio,32);v=u->visual_state();CHECK(v.bpm_q16==e->effective_bpm_q16());CHECK(v.target_frames==delay_time::sync_frames(u->params[8].target,u->params[9].target,v.bpm_q16));}
    CHECK(v.bpm_q16!=(e->bpm<<16));std::puts("ok real Engine applied Q16 clock, not base/target tempo, reaches Delay before next audio fragment");
}
}
int main(){path="/tmp/seance-delay-persistence-"+std::to_string((long)getpid())+".snc";roundtrip();historical();corrupt_transactional();clock_integration();std::remove(path.c_str());std::printf("delay persistence: 4/4 passed (%llu checks)\n",(unsigned long long)checks);}
