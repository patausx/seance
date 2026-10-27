// Focused real-engine VCA SNC1..7 compatibility, authored AHD roundtrip and failure isolation.
#include "engine/engine.h"
#include "engine/units.h"
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
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Bytes=std::vector<uint8_t>;
std::string path;
Bytes read() { std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}}; }
void put(const Bytes& b) {std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
uint32_t r32(const Bytes& b,size_t at) {CHECK(at+4<=b.size());return b[at]|(uint32_t)b[at+1]<<8|(uint32_t)b[at+2]<<16|(uint32_t)b[at+3]<<24;}
void w16(Bytes& b,size_t at,int value) {CHECK(at+2<=b.size());b[at]=(uint8_t)value;b[at+1]=(uint8_t)(value>>8);}
size_t chunk(const Bytes& b,const char* id) {for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;CHECK(false);return 0;}
void set(VcaUnit& u,int p,int value) {u.params[p].base=(q15)value;u.params[p].snap();}
std::vector<q15> pcm(VcaUnit& u) {
    u.reset();for(int p=0;p<u.num_params;++p)u.params[p].snap();u.trigger();
    std::vector<q15> audio(20000);uint32_t rng=101;
    for(auto& s:audio) {rng=rng*1664525u+1013904223u;s=(q15)(rng>>16);}
    u.process(audio.data(),317);u.trigger();u.process(audio.data()+317,(int)audio.size()-317);return audio;
}
void roundtrip() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
    const int modern_values[]={27001,10731,28553,13177,9500,-25139,17391,32767};
    for(bool flexible:{false,true}) {
        auto* u=new VcaUnit;u->set_flexible(flexible);delete a->replace_unit(0,0,u);
        for(int p=0;p<u->num_params;++p) {set(*u,p,modern_values[p]);u->params[p].src=(ModSource)(1+p%4);u->params[p].mod_depth=(q15)(-7000+p*1793);}
        // Polluted unused tail must not become a visible legacy control on reload.
        if(!flexible)for(int p=3;p<8;++p)set(*u,p,31337);
        a->capture_scene(0,0);
        for(int p=0;p<u->num_params;++p)set(*u,p,p==5?31111:p==6?-29000:(p*2711+193)%32768);
        a->capture_scene(0,1);
        for(int p=0;p<u->num_params;++p)set(*u,p,modern_values[p]);
        for(int p=0;p<u->num_params;++p)CHECK(a->set_step_lock(0,p,0,p,p==5?-32000:p==6?29000:17000+p));
        CHECK(patchio::save(*a,path.c_str()));const auto before=read();
        CHECK(before[3]=='7' && before[4]==7);CHECK(before[chunk(before,"UNIT")+8]==(flexible?1:0));
        CHECK(patchio::load(*b,path.c_str()));
        auto* got=static_cast<VcaUnit*>(b->chains[0].units[0]);CHECK(got && got->kind==UnitKind::Vca);
        CHECK(got->flexible==flexible && got->num_params==(flexible?8:3));CHECK(!got->visual_ready());
        for(int p=0;p<got->num_params;++p) {
            CHECK(got->params[p].base==u->params[p].base);
            CHECK(got->params[p].src==u->params[p].src && got->params[p].mod_depth==u->params[p].mod_depth);
            CHECK(got->params[p].bipolar==(p==5||p==6));
            CHECK(b->pattern.steps[0][p].locks[0].param==p);
            CHECK(b->pattern.steps[0][p].locks[0].value==(p==5?-32000:p==6?29000:17000+p));
            for(int scene=0;scene<2;++scene)CHECK(b->scenes[0][scene].base[0][p]==a->scenes[0][scene].base[0][p]);
        }
        if(!flexible)for(int p=3;p<8;++p)CHECK(got->params[p].base==0);
        CHECK(pcm(*got)==pcm(*u));
        std::unique_ptr<Unit> clone(clone_unit(got));auto* copy=static_cast<VcaUnit*>(clone.get());
        CHECK(copy && copy->flexible==flexible && copy->num_params==got->num_params);
        CHECK(pcm(*copy)==pcm(*got));
        CHECK(patchio::save(*b,path.c_str()));CHECK(read()==before);
    }
    std::puts("ok modern/legacy SNC7 bit0, all8 values/routes/bipolar curves/duck/scenes/locks, clone, PCM and byte-stable re-save");
}
void historical() {
    auto b=std::make_unique<Engine>();
    for(int version=1;version<=6;++version)for(bool gain_only:{false,true}) {
        Bytes old={'S','N','C',(uint8_t)('0'+version),(uint8_t)version};
        const int raw[]={23177,gain_only?0:4096,gain_only?127:12000,32000,31300,-27000};
        for(int c=0;c<NUM_CHAINS;++c) {
            old.insert(old.end(),{255,127});if(version>=2)old.insert(old.end(),{0,0});old.push_back(0);
            const size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);
            if(!c) {old[first]=(uint8_t)UnitKind::Vca;for(int p=0;p<6;++p)w16(old,first+3+p*5,raw[p]);}
        }
        old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);
        if(version>=3){old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0);}
        if(version>=4)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);
        if(version>=5){old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0});}
        if(version>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
        put(old);CHECK(patchio::load(*b,path.c_str()));
        auto* got=static_cast<VcaUnit*>(b->chains[0].units[0]);CHECK(!got->flexible && got->num_params==3);
        VcaUnit ref;ref.set_flexible(false);for(int p=0;p<3;++p)set(ref,p,raw[p]);
        CHECK(pcm(*got)==pcm(ref));
        CHECK(patchio::save(*b,path.c_str()));auto snc7=read();CHECK(snc7[chunk(snc7,"UNIT")+8]==0);
        // Historical SNC7 unused tail may contain arbitrary persisted bytes; no upgrade.
        const size_t core=chunk(snc7,"CORE")+8;
        for(int p=3;p<8;++p)w16(snc7,core+5+3+p*5,27777+p);
        put(snc7);CHECK(patchio::load(*b,path.c_str()));got=static_cast<VcaUnit*>(b->chains[0].units[0]);
        CHECK(!got->flexible && got->num_params==3);CHECK(pcm(*got)==pcm(ref));
    }
    std::puts("ok SNC1..6 absent/zero flags -> exact legacy, SNC7 legacy poisoned tail stays hidden, historical gain-only preserved");
}
void corrupt_transactional() {
    auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();
    delete a->replace_unit(0,0,new VcaUnit);CHECK(patchio::save(*a,path.c_str()));const Bytes valid=read();
    delete b->replace_unit(1,2,new VcaUnit);auto* sentinel=b->chains[1].units[2];sentinel->params[4].base=17831;
    b->set_bpm(173);
    std::vector<Bytes> bad;
    for(size_t n:{size_t(0),size_t(4),chunk(valid,"UNIT")+8+3,chunk(valid,"CORE")+8+71})bad.emplace_back(valid.begin(),valid.begin()+n);
    auto changed=valid;size_t unit=chunk(valid,"UNIT");changed[unit+4]=1;changed[unit+5]=changed[unit+6]=changed[unit+7]=0;bad.push_back(changed);
    changed=valid;const size_t core=chunk(valid,"CORE");changed[core+8+5]=255;bad.push_back(changed);
    // Removing required UNIT is corrupt SNC7, not a legacy format migration.
    changed=valid;changed.erase(changed.begin()+unit,changed.begin()+unit+8+r32(valid,unit+4));bad.push_back(changed);
    for(const auto& bytes:bad) {put(bytes);CHECK(!patchio::load(*b,path.c_str()));CHECK(b->chains[1].units[2]==sentinel);CHECK(b->bpm==173 && sentinel->params[4].base==17831);}
    std::puts("ok truncated/malformed/missing UNIT and invalid CORE reject transactionally without touching installed patch");
}
}
int main() {
    path="/tmp/seance-vca-persistence-"+std::to_string((long)getpid())+".snc";
    roundtrip();historical();corrupt_transactional();std::remove(path.c_str());
    std::printf("vca persistence: 3/3 passed (%llu assertions)\n",(unsigned long long)checks);
}
