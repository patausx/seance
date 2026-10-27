#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
using namespace seance;using B=BitUnit;
static size_t checks=0;static std::string directory;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"failed %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)
static void save(const std::string& name,const void* p,size_t n){std::ofstream f(directory+"/"+name,std::ios::binary);f.write(static_cast<const char*>(p),n);REQUIRE(bool(f));}
static std::vector<uint8_t> read(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void param(B& b,int p,int value){b.params[p].base=q15(value);b.params[p].snap();}
static std::vector<q15> input(int n){std::vector<q15> a(n);uint32_t s=1234567;for(int i=0;i<n;++i){s=s*1664525u+1013904223u;a[i]=i%5==0?q15(int(s>>16)-32768):q15((i%256-128)*192);}return a;}
static std::vector<q15> render(B& b,int n,bool split=false){auto a=input(n);int i=0;while(i<n){int count=split?std::min(n-i,1+(i*7+11)%197):n;b.process(a.data()+i,count);i+=count;}return a;}
static void legacy(){
 for(int variant=0;variant<20;++variant){
#ifdef BIT_BASELINE
  B b;
#else
  B b(false);
#endif
  const int values[]={20000,28000,32767,0,0,0,0};for(int p=0;p<7;++p)param(b,p,values[p]);
  if(variant){for(int p=0;p<7;++p)param(b,p,(variant*7919+p*1701)%32768);param(b,5,variant%2?-16000:32767);if(variant<8)param(b,variant-1,variant%2?0:32767);}
  auto a=render(b,8192,true);param(b,5,0);auto tail=render(b,8192,true);a.insert(a.end(),tail.begin(),tail.end());save("legacy-"+std::to_string(variant)+".pcm",a.data(),a.size()*2);
 }
 auto e=std::make_unique<Engine>();
#ifdef BIT_BASELINE
 auto* b=new B;
#else
 auto* b=new B(false);
#endif
 e->chains[0].units[0]=b;param(*b,5,-12345);b->params[1].src=ModSource::SigilB;b->params[1].mod_depth=3333;
 REQUIRE(patchio::save(*e,(directory+"/legacy.snc").c_str()));
}
#ifndef BIT_BASELINE
static size_t chunk(const std::vector<uint8_t>& b,const char* id){for(size_t at=5;at+8<=b.size();){uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(b[at+4+i])<<(8*i);if(!std::memcmp(b.data()+at,id,4))return at;at+=8+n;}REQUIRE(false);return 0;}
static void neutral(B& b){for(int p=0;p<B::P_COUNT;++p)param(b,p,0);param(b,B::P_BITS,32767);param(b,B::P_RATE,32767);param(b,B::P_TONE,32767);param(b,B::P_GAIN,26214);param(b,B::P_MIX,32767);b.reset();}
static void maps(){
 REQUIRE(B::P_COUNT==12&&B::LEGACY_COUNT==7&&PARAM_BANK_SIZE==12);
 REQUIRE(B::bit_depth(0)==1&&B::bit_depth(32767)==16);
 REQUIRE(B::rate_millihz(0)==100000&&B::rate_millihz(32767)==32000000);
 REQUIRE(B::tone_millihz(0)==100000&&B::tone_millihz(32766)==14000000&&B::tone_millihz(32767)==0);
 REQUIRE(B::drive_centidb(0)==0&&B::drive_centidb(32767)==2400);
 REQUIRE(B::gain_centidb(0)==-2400&&B::gain_centidb(26214)==0&&B::gain_centidb(32767)==600);
 REQUIRE(B::drift_millihz(-32768)==-8000&&B::drift_millihz(32767)==8000&&B::drift_millihz(0)==0);
 REQUIRE(B::drop_frames(0)==32&&B::drop_frames(32767)==8000);
 for(int raw=1;raw<=32767;++raw){REQUIRE(B::bit_depth(q15(raw))>=B::bit_depth(q15(raw-1)));REQUIRE(B::rate_millihz(q15(raw))>=B::rate_millihz(q15(raw-1)));REQUIRE(B::drop_frames(q15(raw))>=B::drop_frames(q15(raw-1)));if(raw<32767)REQUIRE(B::tone_millihz(q15(raw))>=B::tone_millihz(q15(raw-1)));}
}
static void sound(){
 {B b;neutral(b);REQUIRE(render(b,32000)==input(32000));}
 for(int edge:{0,32767}){B b;for(int p=0;p<12;++p)param(b,p,edge);param(b,2,0);b.reset();REQUIRE(render(b,8192)==input(8192));}
 {B b;param(b,B::P_DROPOUT,32767);param(b,B::P_MIX,32767);param(b,B::P_TONE,15000);b.reset();auto a=render(b,32000);REQUIRE(std::all_of(a.begin(),a.end(),[](q15 v){return v==0;}));}
 for(int variant=0;variant<16;++variant){B a,b;for(B* n:{&a,&b}){for(int p=0;p<12;++p)param(*n,p,(variant*7919+p*1701)%32768);param(*n,B::P_DRIFT,variant%2?-17000:12000);n->reset();}auto one=render(a,32768),split=render(b,32768,true);REQUIRE(one==split);save("modern-"+std::to_string(variant)+".pcm",one.data(),one.size()*2);}
 // Every audible parameter has an independently observed effect (dry is checked above).
 for(int p=0;p<12;++p){B a,b;for(B* n:{&a,&b}){neutral(*n);param(*n,B::P_BITS,6000);param(*n,B::P_RATE,19000);param(*n,B::P_DROPOUT,p==B::P_DROP_TIME?500:0);n->reset();}param(b,p,p==B::P_TONE?10000:p==B::P_GAIN?0:p==B::P_BITS?0:p==B::P_DROP_TIME?32767:24000);REQUIRE(render(a,32000)!=render(b,32000));}
 {B a,b;for(B* n:{&a,&b}){neutral(*n);param(*n,B::P_RATE,18000);}param(b,B::P_JITTER,1000);REQUIRE(render(a,32000)!=render(b,32000));} // below old25% dead region
 // Events from actual processor: dropout intervals are exact fixed durations and fullclock reports captures.
 {B b;neutral(b);std::vector<q15> a(1024,12000);std::vector<uint8_t> events(a.size());b.process_with_events(a.data(),events.data(),int(a.size()));REQUIRE(std::all_of(events.begin(),events.end(),[](uint8_t v){return v==1;}));}
 for(int length:{0,16000,32767}){B b;neutral(b);param(b,B::P_DROPOUT,60);param(b,B::P_DROP_TIME,length);std::vector<q15> a(320000,12000);std::vector<uint8_t> ev(a.size());b.process_with_events(a.data(),ev.data(),int(a.size()));int run=0,groups=0;for(uint8_t v:ev){if(v&2)++run;else if(run){REQUIRE(run%int(B::drop_frames(q15(length)))==0);run=0;++groups;}}REQUIRE(groups>3);}
 // Long holds retain the last captured period across blocks with no capture.
 {B b;neutral(b);param(b,B::P_RATE,0);q15 sample=12000;uint8_t event=0;b.process_with_events(&sample,&event,1);REQUIRE(event==1&&b.visual_period()==255);sample=12000;b.process_with_events(&sample,&event,1);REQUIRE(event==0&&b.visual_period()==255);}
 // Fractional capture counts follow the nominal mapping, not the old32 hold bins.
 for(int raw:{0,7000,19000,32767}){B b;neutral(b);param(b,B::P_RATE,raw);std::vector<q15> a(32000,12000);std::vector<uint8_t> ev(a.size());b.process_with_events(a.data(),ev.data(),int(a.size()));int count=0;for(auto v:ev)count+=bool(v&1);REQUIRE(std::abs(double(count)-double(B::rate_millihz(q15(raw)))/1000.)<=2.);}
 // Tiny centered samples must not become the old negative quantizer step.
 {B b;neutral(b);param(b,B::P_BITS,5000);std::vector<q15> a(4096);for(size_t i=0;i<a.size();++i)a[i]=i%2?1:-1;b.process(a.data(),int(a.size()));REQUIRE(std::all_of(a.begin(),a.end(),[](q15 v){return v==0;}));}
 // Shared integer slew stays untouched; local meaningful endpoints reach their target.
 for(int p:{B::P_MIX,B::P_DRIFT,B::P_JITTER,B::P_DROPOUT,B::P_DITHER}){B b;param(b,p,12000);b.params[p].resolve(0);b.params[p].base=0;for(int t=0;t<100;++t){b.params[p].resolve(0);b.control_tick();}REQUIRE(b.params[p].effective>0&&b.control_value(p)==0);}
 {B b;neutral(b);param(b,B::P_DRIFT,20000);render(b,1234);param(b,B::P_DRIFT,0);auto a=render(b,4096);REQUIRE(a==input(4096));}
 {B b(false);auto a=render(b,101);b.set_modern(true);q15 v=12000;b.process(&v,1);REQUIRE(v==a.back());}
 // Mod extremes, all parameter endpoints and reset determinism under sanitizers.
 {B b;std::array<q15,32> a{};for(int tick=0;tick<3000;++tick){for(int p=0;p<12;++p){b.params[p].base=q15((tick*173+p*7919)%32768);b.params[p].resolve(0);}b.control_tick();b.process(a.data(),int(a.size()));}}
 {B b;auto a=render(b,8192);b.reset();REQUIRE(render(b,8192)==a);}
}
static void persistence(){
 auto e=std::make_unique<Engine>();auto* b=new B;e->chains[0].units[0]=b;
 for(int p=0;p<12;++p){param(*b,p,p==5?-12345:1000+p*2200);b->params[p].src=ModSource::SigilG;b->params[p].mod_depth=q15(p*151);b->params[p].mod_enabled=p%2;}
 for(auto& scene:e->scenes[0]){scene.kind[0]=UnitKind::Bitcrush;for(int p=0;p<12;++p)scene.base[0][p]=q15(p==5?-12345:p*1999);}
 for(auto& bank:e->pattern_banks)for(int p=0;p<12;++p){auto& s=bank.steps[0][p];s.lock_count=1;s.locks[0]={0,uint8_t(p),q15(p==5?-12345:p*1900)};}
 for(int p=0;p<12;++p){auto& s=e->pattern.steps[0][p];s.lock_count=1;s.locks[0]={0,uint8_t(p),q15(p==5?-12345:p*1900)};}
 e->macro_pads[0].route_count=1;e->macro_pads[0].routes[0].chain=0;e->macro_pads[0].routes[0].slot=0;e->macro_pads[0].routes[0].param=11;
 REQUIRE(patchio::save(*e,(directory+"/modern.snc").c_str()));auto bytes=read(directory+"/modern.snc");REQUIRE(bytes[3]=='A'&&bytes[4]==10&&bytes[chunk(bytes,"UNIT")+8]==1);
 auto loaded=std::make_unique<Engine>();REQUIRE(patchio::load(*loaded,(directory+"/modern.snc").c_str()));auto* got=static_cast<B*>(loaded->chains[0].units[0]);REQUIRE(got->modern()&&got->num_params==12&&got->params[5].bipolar);
 for(int p=0;p<12;++p){REQUIRE(got->params[p].base==b->params[p].base&&got->params[p].src==b->params[p].src&&got->params[p].mod_depth==b->params[p].mod_depth&&got->params[p].mod_enabled==b->params[p].mod_enabled);for(const auto& bank:loaded->pattern_banks)REQUIRE(bank.steps[0][p].lock_count==1);}
 REQUIRE(loaded->scenes[0][0].base[0][11]==21989&&loaded->macro_pads[0].route_count==1);
 std::unique_ptr<Unit> clone(clone_unit(got));REQUIRE(static_cast<B*>(clone.get())->modern());for(int p=0;p<12;++p)REQUIRE(clone->params[p].base==got->params[p].base);
 REQUIRE(patchio::save(*loaded,(directory+"/modern-roundtrip.snc").c_str()));REQUIRE(bytes==read(directory+"/modern-roundtrip.snc"));
 {auto v=bytes;size_t at=chunk(v,"UNIT"),n=8+NUM_CHAINS*UNITS_PER_CHAIN;std::vector<uint8_t> u(v.begin()+at,v.begin()+at+n);v.erase(v.begin()+at,v.begin()+at+n);v.insert(v.begin()+5,u.begin(),u.end());save("reordered.snc",v.data(),v.size());REQUIRE(patchio::load(*loaded,(directory+"/reordered.snc").c_str()));REQUIRE(static_cast<B*>(loaded->chains[0].units[0])->modern());}
 for(int version:{7,8,9}){auto v=bytes;v[3]=uint8_t('0'+version);v[4]=uint8_t(version);v[chunk(v,"UNIT")+8]=0;save("old-padding.snc",v.data(),v.size());REQUIRE(patchio::load(*loaded,(directory+"/old-padding.snc").c_str()));got=static_cast<B*>(loaded->chains[0].units[0]);REQUIRE(!got->modern()&&got->num_params==7);for(int p=7;p<12;++p){REQUIRE(got->params[p].base==0&&got->params[p].src==ModSource::None);for(const auto& bank:loaded->pattern_banks)REQUIRE(bank.steps[0][p].lock_count==0);for(const auto& scene:loaded->scenes[0])REQUIRE(scene.base[0][p]==0);}REQUIRE(loaded->macro_pads[0].route_count==0);clone.reset(clone_unit(got));REQUIRE(!static_cast<B*>(clone.get())->modern()&&clone->num_params==7);}
 for(int flags:{2,3,128}){auto bad=bytes;bad[chunk(bad,"UNIT")+8]=uint8_t(flags);save("bad.snc",bad.data(),bad.size());Unit* previous=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/bad.snc").c_str())&&loaded->chains[0].units[0]==previous);}
 REQUIRE(patchio::load(*loaded,(directory+"/legacy.snc").c_str()));REQUIRE(patchio::save(*loaded,(directory+"/legacy-roundtrip.snc").c_str()));REQUIRE(read(directory+"/legacy.snc")==read(directory+"/legacy-roundtrip.snc"));
 // Existing conditional versions and modern neighbors still work.
 e->chains[0].units[1]=new NoiseUnit;e->chains[0].units[2]=new MotorUnit;REQUIRE(patchio::save(*e,(directory+"/all-modern.snc").c_str()));REQUIRE(patchio::load(*loaded,(directory+"/all-modern.snc").c_str()));REQUIRE(static_cast<NoiseUnit*>(loaded->chains[0].units[1])->modern()&&static_cast<MotorUnit*>(loaded->chains[0].units[2])->modern());
 auto neighbors=std::make_unique<Engine>();neighbors->chains[0].units[0]=new NoiseUnit;neighbors->chains[0].units[1]=new B(false);REQUIRE(patchio::save(*neighbors,(directory+"/noise9.snc").c_str()));REQUIRE(read(directory+"/noise9.snc")[3]=='9');
}
#endif
int main(int argc,char** argv){if(argc==3&&std::string(argv[1])=="--old-reader"){auto e=std::make_unique<Engine>();e->chains[0].units[0]=new B;Unit* previous=e->chains[0].units[0];REQUIRE(!patchio::load(*e,argv[2])&&e->chains[0].units[0]==previous);std::puts("actual pre-edit reader rejects extended BIT without mutation");return 0;}REQUIRE(argc==2);directory=argv[1];legacy();
#ifndef BIT_BASELINE
 maps();sound();persistence();
#endif
 std::printf("bit focused: %zu checks passed\n",checks);
}
