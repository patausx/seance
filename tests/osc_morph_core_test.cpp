// Focused actual OSC morph DSP, frozen legacy PCM and SNC1-7 compatibility proof.
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
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using Bytes=std::vector<uint8_t>;std::string path;
Bytes read(){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void put(const Bytes&b){std::ofstream f(path,std::ios::binary);f.write((const char*)b.data(),b.size());CHECK(f.good());}
uint32_t r32(const Bytes&b,size_t at){CHECK(at+4<=b.size());return b[at]|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24;}
void w16(Bytes&b,size_t at,int value){CHECK(at+2<=b.size());b[at]=uint8_t(value);b[at+1]=uint8_t(uint16_t(value)>>8);}
size_t chunk(const Bytes&b,const char*id){for(size_t p=5;p+8<=b.size();p+=8+r32(b,p+4))if(!std::memcmp(b.data()+p,id,4))return p;CHECK(false);return 0;}
void set(OscUnit&u,int p,int value){u.params[p].base=q15(value);u.params[p].effective=q15(value);u.params[p].target=q15(value);}
uint64_t hash(const q15*p,int n){uint64_t x=1469598103934665603ull;for(int i=0;i<n;++i){const uint16_t v=uint16_t(p[i]);x^=v&255;x*=1099511628211ull;x^=v>>8;x*=1099511628211ull;}return x;}
void config(OscUnit&u,int cfg){u.clean=false;set(u,OscUnit::P_PITCH,cfg?24576:16384);set(u,OscUnit::P_PW,cfg==2?30000:8192);set(u,OscUnit::P_GAIN,cfg==1?32767:14000);set(u,OscUnit::P_FOLD,cfg==2?25000:cfg==1?7000:0);set(u,OscUnit::P_SUB,cfg==1?11000:0);set(u,OscUnit::P_DRIFT,cfg==2?32767:0);u.control_tick();}
std::vector<q15> pcm(OscUnit&u,int n=4096){std::vector<q15>b(n);u.process(b.data(),n);return b;}
int raw(int wave,uint32_t phase,uint32_t pw){if(wave==2)return int32_t(phase>>16)-32768;if(wave==3)return phase<pw?-32767:32767;const uint32_t p=phase>>16;const int t=p<0x8000u?int(p)*2-32768:32767-(int(p)-32768)*2;if(wave==1)return t;const int a=t<0?-t:t;return std::max(-32768,std::min(32767,int((int64_t(t)*(65536-a))>>15)));}
int morph(int shape,uint32_t phase,uint32_t pw){int a=0,b=1,start=0,span=10922;if(shape>21845){a=2;b=3;start=21845;span=10922;}else if(shape>10922){a=1;b=2;start=10922;span=10923;}const int x=raw(a,phase,pw),y=raw(b,phase,pw);return x+int(int64_t(y-x)*(shape-start)/span);}
uint32_t increment(q15 pitch){const int note100=2400+int((int64_t(uint16_t(pitch))*7200)>>15);const uint32_t semis=uint32_t(note100/100);static const uint32_t table[12]={65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715};uint64_t freq=(uint64_t(1071616)*table[semis%12])>>16;freq<<=semis/12;return uint32_t((freq<<16)/ENGINE_SR);}
void defaults_and_legacy(){
    OscUnit fresh;CHECK(fresh.num_params==OscUnit::P_COUNT&&fresh.clean&&fresh.shape_morph&&fresh.params[OscUnit::P_SHAPE].base==0);
    static const uint64_t frozen[4][3]={{0xd006ea9ef071e265ull,0x8fe595c6c9b26232ull,0x983c57838ee5f262ull},{0x7f0e4ddffde9deb5ull,0x1b90b4deb9c1017dull,0xb61ebbd7119238e5ull},{0x75393619e145ccbdull,0x438df2fdea0b71cbull,0x5016b56e0cbbba5eull},{0xb2209fdcf8819c9dull,0xc17f73591b3ba6ffull,0x4faea6af60e70151ull}};
    for(int wave=0;wave<4;++wave)for(int cfg=0;cfg<3;++cfg){OscUnit u;u.shape_morph=false;u.wave=OscUnit::Wave(wave);config(u,cfg);auto out=pcm(u);CHECK(hash(out.data(),out.size())==frozen[wave][cfg]);}
    std::puts("ok frozen pre-change discrete OSC hashes / constructor morph default");
}
void anchors_and_interpolation(){
    const int anchors[]={0,10922,21845,32767};
    for(int wave=0;wave<4;++wave)for(int cfg=0;cfg<3;++cfg){OscUnit old,newer;old.shape_morph=false;old.wave=OscUnit::Wave(wave);newer.shape_morph=true;set(newer,OscUnit::P_SHAPE,anchors[wave]);config(old,cfg);config(newer,cfg);CHECK(pcm(old)==pcm(newer));}
    for(int shape:{1,5431,10921,10923,16384,21844,21846,27306,32766}){OscUnit u;u.clean=false;u.shape_morph=true;set(u,OscUnit::P_PITCH,0);set(u,OscUnit::P_PW,23117);set(u,OscUnit::P_GAIN,32767);set(u,OscUnit::P_FOLD,0);set(u,OscUnit::P_SUB,0);set(u,OscUnit::P_DRIFT,0);set(u,OscUnit::P_SHAPE,shape);u.control_tick();auto out=pcm(u,5000);uint32_t phase=0,inc=increment(0),pw=0x40000000u+(uint32_t(23117)<<15);for(int i=0;i<(int)out.size();++i){phase+=inc;const int shaped=int((int64_t(morph(shape,phase,pw))*32767)>>15);const q15 expect=q15((int64_t(shaped)*32767)>>15);CHECK(out[i]==expect);}}
    for(int shape:{0,8192,10922,19000,21845,30000,32767}){OscUnit whole,split;for(OscUnit*u:{&whole,&split}){u->shape_morph=true;set(*u,OscUnit::P_SHAPE,shape);config(*u,2);}auto a=pcm(whole,8192);std::vector<q15>b(8192);int at=0;for(int n:{1,31,32,257,4095,3776}){split.process(b.data()+at,n);at+=n;}CHECK(at==8192&&a==b);}
    std::puts("ok exact four anchors / independent adjacent interpolation / partition invariance");
}
void roundtrip(){
    for(bool modern:{false,true}){auto a=std::make_unique<Engine>(),b=std::make_unique<Engine>();auto*u=new OscUnit;delete a->replace_unit(0,0,u);u->clean=false;u->shape_morph=modern;u->wave=OscUnit::Wave::Saw;u->pitch_quantize=true;
        const int values[]={21031,29001,28553,13177,9500,2200,17777};for(int p=0;p<u->num_params;++p){set(*u,p,values[p]);u->params[p].src=ModSource(1+p%5);u->params[p].mod_depth=q15(-7000+p*1173);}
        if(!modern){set(*u,OscUnit::P_SHAPE,31337);u->params[OscUnit::P_SHAPE].src=ModSource::EnvFollow;u->params[OscUnit::P_SHAPE].mod_depth=32767;}
        a->capture_scene(0,0);set(*u,OscUnit::P_SHAPE,modern?29999:31337);a->capture_scene(0,1);set(*u,OscUnit::P_SHAPE,modern?17777:31337);
        CHECK(a->set_step_lock(0,0,0,OscUnit::P_SHAPE,23456));a->macro_pads[0].route_count=1;a->macro_pads[0].routes[0]={0,0,OscUnit::P_SHAPE,0,32767};
        CHECK(patchio::save(*a,path.c_str()));const Bytes before=read();const size_t unit=chunk(before,"UNIT");CHECK(before[unit+8]==uint8_t(modern?3:1));
        const size_t core=chunk(before,"CORE")+8;CHECK(before[core+7]==uint8_t(modern?OscUnit::nearest_shape_anchor(17777):OscUnit::Wave::Saw));
        CHECK(patchio::load(*b,path.c_str()));auto*got=static_cast<OscUnit*>(b->chains[0].units[0]);CHECK(got&&!got->clean&&got->shape_morph==modern&&got->pitch_quantize&&got->wave==OscUnit::Wave::Saw);
        if(modern){for(int p=0;p<OscUnit::P_COUNT;++p){CHECK(got->params[p].base==u->params[p].base);CHECK(got->params[p].src==u->params[p].src&&got->params[p].mod_depth==u->params[p].mod_depth);}CHECK(b->pattern.steps[0][0].lock_count==1);CHECK(b->macro_pads[0].route_count==1);for(int scene=0;scene<2;++scene)CHECK(b->scenes[0][scene].base[0][OscUnit::P_SHAPE]==a->scenes[0][scene].base[0][OscUnit::P_SHAPE]);}
        else{CHECK(got->params[OscUnit::P_SHAPE].base==OscUnit::shape_anchor(OscUnit::Wave::Saw));CHECK(got->params[OscUnit::P_SHAPE].src==ModSource::None&&got->params[OscUnit::P_SHAPE].mod_depth==0);CHECK(b->pattern.steps[0][0].lock_count==0&&b->macro_pads[0].route_count==0);}
        std::unique_ptr<Unit> clone(clone_unit(got));auto*copy=static_cast<OscUnit*>(clone.get());CHECK(copy&&!copy->clean&&copy->shape_morph==modern&&copy->wave==got->wave&&copy->pitch_quantize==got->pitch_quantize);for(int p=0;p<OscUnit::P_COUNT;++p)CHECK(copy->params[p].base==got->params[p].base);
        if(modern){b->capture_scene(0,0);set(*got,OscUnit::P_SHAPE,30000);b->capture_scene(0,1);b->apply_scene_morph(0,16384);CHECK(got->shape_morph&&got->params[OscUnit::P_SHAPE].base>17777&&got->params[OscUnit::P_SHAPE].base<30000);}
    }
    std::puts("ok SNC7 flag/nearest fallback / params routes locks scenes clone / dormant legacy canonicalization");
}
void historical_and_rollback(){
    auto target=std::make_unique<Engine>();
    for(int version=1;version<=6;++version)for(int wave=0;wave<4;++wave){Bytes old={'S','N','C',uint8_t('0'+version),uint8_t(version)};const int rawp[]={16384,22111,14000,7000,11000,0};
        for(int c=0;c<NUM_CHAINS;++c){old.insert(old.end(),{255,127});if(version>=2)old.insert(old.end(),{0,0});old.push_back(0);const size_t first=old.size();old.insert(old.end(),UNITS_PER_CHAIN*(3+6*5),0);if(!c){old[first]=uint8_t(UnitKind::Osc);old[first+2]=uint8_t(wave);for(int p=0;p<6;++p){w16(old,first+3+p*5,rawp[p]);old[first+3+p*5+2]=p?0:1;w16(old,first+3+p*5+3,p?0:1234);}}}
        old.insert(old.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);if(version>=3){old.insert(old.end(),{120,0});old.insert(old.end(),NUM_SIGILS*3,0);}if(version>=4){const size_t flags=old.size();old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);old[flags]=2;}if(version>=5){old.insert(old.end(),patchio::SampleManifest::SLOT_COUNT*4,0);old.insert(old.end(),{0,4,0});}if(version>=6)old.insert(old.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
        put(old);CHECK(patchio::load(*target,path.c_str()));auto*got=static_cast<OscUnit*>(target->chains[0].units[0]);CHECK(got&&!got->clean&&!got->shape_morph&&got->wave==OscUnit::Wave(wave)&&!got->pitch_quantize);CHECK(got->params[OscUnit::P_SHAPE].base==OscUnit::shape_anchor(OscUnit::Wave(wave)));OscUnit ref;ref.clean=false;ref.shape_morph=false;ref.wave=OscUnit::Wave(wave);for(int p=0;p<6;++p)set(ref,p,rawp[p]);ref.control_tick();got->control_tick();CHECK(pcm(*got)==pcm(ref));}
    auto good=std::make_unique<Engine>();delete good->replace_unit(0,0,new OscUnit);CHECK(patchio::save(*good,path.c_str()));Bytes valid=read(),bad=valid;bad[chunk(bad,"UNIT")+8]=8;put(bad);auto installed=std::make_unique<Engine>();auto*sentinel=new OscUnit;sentinel->params[0].base=12345;delete installed->replace_unit(1,2,sentinel);CHECK(!patchio::load(*installed,path.c_str()));CHECK(installed->chains[1].units[2]==sentinel&&sentinel->params[0].base==12345);
    std::puts("ok SNC1-6 all waves ignore old bit1 / exact PCM / unknown SNC7 flag rollback");
}
void parameter_layers(){OscUnit u;Param&p=u.params[OscUnit::P_SHAPE];p.base=16000;p.src=ModSource::SigilA;p.mod_depth=20000;p.snap();p.resolve(32767);CHECK(p.effective==32767);p.snap();p.resolve(-32767);CHECK(p.effective==0);p.step_lock=21845;p.step_lock_override=true;CHECK(p.control_source()==21845);p.performance=10922;p.performance_override=true;CHECK(p.control_source()==10922);p.temp=32767;p.temp_override=true;CHECK(p.control_source()==32767);CHECK(OscUnit::nearest_shape_anchor(0)==OscUnit::Wave::Sine&&OscUnit::nearest_shape_anchor(32767)==OscUnit::Wave::Pulse);std::puts("ok ordinary unipolar modulation clamp / lock-performance-temp layer priority");}
}
int main(){path="/tmp/seance-osc-morph-"+std::to_string((long)getpid())+".snc";defaults_and_legacy();anchors_and_interpolation();roundtrip();historical_and_rollback();parameter_layers();std::remove(path.c_str());std::printf("osc morph core: 5/5 passed (%llu checks)\n",(unsigned long long)checks);}
