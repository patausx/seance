#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include "engine/tilt_eq.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
using namespace seance;using T=TiltUnit;
static size_t checks=0;static std::string directory;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"failed %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)
static void save(const std::string& name,const void* p,size_t n){std::ofstream f(directory+"/"+name,std::ios::binary);f.write(static_cast<const char*>(p),n);REQUIRE(bool(f));}
static std::vector<uint8_t> read(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void param(T& r,int p,int v){r.params[p].base=q15(v);r.params[p].snap();}
static std::vector<q15> input(int n){std::vector<q15> a(n);uint32_t rng=1234567;for(int i=0;i<n;++i){rng=rng*1664525u+1013904223u;a[i]=i%5==0?q15(int(rng>>16)-32768):q15((i%256-128)*192);}return a;}
static std::vector<q15> render(T& r,int n,bool split=false){auto a=input(n);for(int i=0;i<n;){int count=split?std::min(n-i,1+(i*7+11)%197):n;r.process(a.data()+i,count);i+=count;}return a;}
static void old_laws(){
 for(int law=0;law<3;++law)for(int variant=0;variant<12;++variant){T r(law==2);for(int p=0;p<6;++p)param(r,p,(variant*7919+p*1701)%32768);param(r,0,variant%2?-32767:32767);param(r,4,law==0?0:variant%3?21846:32767);if(variant<6)param(r,variant,0);if(law==0)param(r,4,0);if(law==1&&r.params[4].base==0)param(r,4,1);auto a=render(r,8192,true);param(r,2,0);param(r,0,0);auto tail=render(r,4096,true);a.insert(a.end(),tail.begin(),tail.end());r.reset();tail=render(r,4096,true);a.insert(a.end(),tail.begin(),tail.end());save("old-"+std::to_string(law)+"-"+std::to_string(variant)+".pcm",a.data(),a.size()*2);}
 for(int law=0;law<3;++law){auto e=std::make_unique<Engine>();auto* r=new T(law==2);e->chains[0].units[0]=r;param(*r,0,-18000);param(*r,4,law?21846:0);if(law==2)param(*r,5,24000);r->params[0].src=ModSource::SigilB;r->params[0].mod_depth=3333;REQUIRE(patchio::save(*e,(directory+"/old-"+std::to_string(law)+".snc").c_str()));}
}
#ifndef TILT_BASELINE
#include "engine/tilt_extended.h"
#include "engine/tilt_edit.h"
namespace eq=tilt_extended;
static size_t chunk(const std::vector<uint8_t>& b,const char* id){for(size_t at=5;at+8<=b.size();){uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(b[at+4+i])<<(8*i);if(!std::memcmp(b.data()+at,id,4))return at;at+=8+n;}REQUIRE(false);return 0;}
static std::complex<double> transfer(const eq::Coefficients& c,double hz){const auto z=std::exp(std::complex<double>(0,-6.283185307179586*hz/32000));return (double(c.b0)+double(c.b1)*z)/(double(eq::COEFF_ONE)+double(c.a1)*z);}
static void maps(){
 REQUIRE(T::P_COUNT==6&&PARAM_BANK_SIZE==12);T a,b(true),c(false);REQUIRE(a.extended()&&a.modern()&&!b.extended()&&b.modern()&&!c.modern());
 REQUIRE(eq::range_db(0)==3&&eq::range_db(32767)==12);REQUIRE(eq::amount_db(32767,32767)==12&&eq::amount_db(-32768,32767)==-12);
 REQUIRE(std::abs(eq::nominal_hz(0)-125)<.0001&&std::abs(eq::nominal_hz(32767)-8000)<.0001);
 REQUIRE(eq::drive_db(0)==0&&eq::drive_db(32767)>12.04&&eq::drive_db(32767)<12.05);
 for(int raw=1;raw<=32767;++raw){REQUIRE(eq::range_q16(raw)>eq::range_q16(raw-1));REQUIRE(eq::pivot_q28(raw)>eq::pivot_q28(raw-1));}
 double worst=0;
 for(int tilt:{-32767,-19000,-1,0,1,19000,32767})for(int freq:{0,2000,8192,16384,25000,32767})for(int range:{0,8192,23000,32767}){
  const auto co=eq::coefficients(q15(tilt),q15(freq),q15(range));REQUIRE(std::abs(int64_t(co.a1))<eq::COEFF_ONE);
  const double pivot=eq::nominal_hz(freq),g=std::pow(10,eq::amount_db(tilt,range)/20.),k=std::tan(3.141592653589793*pivot/32000);
  REQUIRE(std::abs(std::abs(transfer(co,pivot))-1)<.00004);
  for(double hz:{0.,20.,125.,1000.,8000.,16000.}){
   auto s=std::complex<double>(0,std::tan(3.141592653589793*hz/32000));auto expected=hz==16000?std::complex<double>(g):(k+g*s)/(k*g+s);
   const auto actual=transfer(co,hz);const double error=std::abs(actual-expected);worst=std::max(worst,error);REQUIRE(error<.00008);
   REQUIRE(std::abs(actual*transfer(eq::coefficients(-tilt,freq,range),hz)-1.0)<.00006);
  }
 }
 std::printf("coefficient complex-response worst error %.9f\n",worst);
}
static void sound(){
 // Full int16 neutral and exact dry, not only small-signal or snapped percentages.
 for(int range:{0,32767})for(int freq:{0,32767}){T t;param(t,4,range);param(t,1,freq);std::vector<q15>a(65536);for(int i=0;i<65536;++i)a[i]=q15(i-32768);const auto original=a;t.process(a.data(),a.size());REQUIRE(a==original);param(t,0,32767);param(t,2,32767);param(t,3,0);a=original;t.process(a.data(),a.size());REQUIRE(a==original);}
 for(int v=0;v<20;++v){T a,b;for(T* t:{&a,&b}){for(int p=0;p<6;++p)param(*t,p,(v*7919+p*1701)%32768);param(*t,0,v%2?-32767:32767);}auto out=render(a,16384),split=render(b,16384,true);REQUIRE(out==split);save("extended-"+std::to_string(v)+".pcm",out.data(),out.size()*2);a.reset();REQUIRE(render(a,16384)==out);}
 // Direct recurrence oracle for the unchanged fixed coefficient transfer; wide clean EQ -> trim.
 for(int tilt:{-32767,13000,32767})for(int freq:{0,16384,32767}){T t;param(t,0,tilt);param(t,1,freq);param(t,4,32767);param(t,5,10000);auto co=eq::coefficients(tilt,freq,32767);double px=0,py=0;const double gain=tilt_eq::trim_gain_q16(10000)/65536.;auto a=input(8192),dry=a;t.process(a.data(),a.size());for(size_t i=0;i<a.size();++i){const double y=(co.b0*double(dry[i])+co.b1*px-co.a1*py)/eq::COEFF_ONE;px=dry[i];py=y;const double expected=std::max(-32768.,std::min(32767.,y*gain));REQUIRE(std::abs(a[i]-expected)<3.);}}
 // At -12dB trim a +12dB bass shelf can recover a loud DC input WITHOUT an intermediate q15 clip.
 {T t;param(t,0,-32767);param(t,4,32767);param(t,5,tilt_eq::TRIM_UNITY/2);std::vector<q15>a(16000,20000);t.process(a.data(),a.size());REQUIRE(a.back()>19980&&a.back()<20020);}
 // Actual signal -> silence tests, both signs/extreme poles/nonlinearity; not merely reset zeros.
 for(int tilt:{-32767,32767})for(int freq:{0,32767})for(int drive:{0,32767})for(int sign:{-1,1}){T t;param(t,0,tilt);param(t,1,freq);param(t,4,32767);param(t,2,drive);std::vector<q15>a(4096,q15(sign*20000));t.process(a.data(),a.size());std::vector<q15> tail(32000);t.process(tail.data(),tail.size());for(int i=30000;i<32000;++i)REQUIRE(tail[i]==0);save("tail-"+std::to_string(tilt)+"-"+std::to_string(freq)+"-"+std::to_string(drive)+"-"+std::to_string(sign)+".pcm",tail.data(),tail.size()*2);}
 // Continuous odd drive; the old tiny-positive knee switch is absent.
 for(int drive:{0,1,3,1000,16000,32767})for(int x=-130000;x<=130000;x+=211){REQUIRE(eq::colour(x,drive)==-eq::colour(-x,drive));if(drive==0)REQUIRE(eq::colour(x,drive)==x);if(drive<=3)REQUIRE(std::abs(eq::colour(x,drive)-x)<30);}
 // Semantic endpoints settle locally; global effective remains historically stalled.
 for(int p=0;p<6;++p){T t;param(t,p,12000);t.params[p].resolve(0);t.params[p].base=p==5?tilt_eq::TRIM_UNITY:0;for(int i=0;i<100;++i){t.params[p].resolve(0);t.control_tick();}REQUIRE(t.params[p].effective!=t.params[p].target);REQUIRE(t.control_value(p)==t.params[p].target);}
 {T t;param(t,0,20000);param(t,2,18000);for(auto& p:t.params)p.resolve(0);t.params[0].base=t.params[2].base=0;for(int i=0;i<100;++i){for(auto& p:t.params)p.resolve(0);t.control_tick();}REQUIRE(render(t,65536)==input(65536));}
 {T t(true);auto a=render(t,1000);t.set_revision(T::Revision::Extended);q15 v=12000;t.process(&v,1);REQUIRE(v==a.back());std::vector<q15> rest(127,12000);t.process(rest.data(),rest.size());v=12000;t.process(&v,1);REQUIRE(v==12000);}
 // Bounded changing coefficients under all controls; addressed/UB sanitizer covers arithmetic.
 {T t;std::array<q15,32>a{};for(int k=0;k<4000;++k){for(int p=0;p<6;++p)param(t,p,(k*251+p*7919)%32768);param(t,0,k%2?-32767:32767);for(auto&v:a)v=k%2?-32768:32767;t.process(a.data(),32);}}
}
static void engine_endpoints(){
 for(int group=0;group<2;++group){auto e=std::make_unique<Engine>();e->chains[0].units[0]=new OscUnit;auto* t=new T;e->chains[0].units[1]=t;param(*t,T::P_DRIVE,12000);param(*t,T::P_TRIM,24000);q15 stored[6];for(int p=0;p<6;++p)stored[p]=t->params[p].base;
  const q15 lock[6]={-24000,0,0,0,32767,tilt_eq::TRIM_UNITY};e->pattern.steps[0][0].flags=SeqStep::Trig;e->set_sequence_length(0,1);
  for(int p=group*3;p<group*3+3;++p)REQUIRE(e->set_step_lock(0,0,1,p,lock[p]));
  e->set_sequencer_enabled(true);e->transport_reset();e->transport_play();std::array<q15,256> out{};for(int i=0;i<100;++i)e->render(out.data(),128);
  for(int p=group*3;p<group*3+3;++p){REQUIRE(t->params[p].step_lock_override&&t->control_value(p)==lock[p]&&t->params[p].base==stored[p]);}
  e->set_sequencer_enabled(false);e->transport_stop();for(int i=0;i<100;++i)e->render(out.data(),128);for(int p=0;p<6;++p)REQUIRE(!t->params[p].step_lock_override&&t->control_value(p)==stored[p]);
 }
}
static void extension(){
 for(bool clear:{false,true}){auto e=std::make_unique<Engine>();auto* t=new T(clear);e->chains[0].units[0]=t;const int first=t->num_params;
  for(int p=0;p<6;++p){param(*t,p,1000+p*3000);t->params[p].src=ModSource::SigilF;t->params[p].mod_depth=1111;}
  for(auto& scene:e->scenes[0]){scene.kind[0]=UnitKind::Tilt;scene.kind[1]=UnitKind::Tilt;for(int p=0;p<12;++p){scene.base[0][p]=q15(1234+p);scene.base[1][p]=q15(6789+p);}}
  for(auto& bank:e->pattern_banks)for(int p=0;p<12;++p){auto& s=bank.steps[0][p];s.lock_count=1;s.locks[0]={0,uint8_t(p),1234};}
  e->pattern=e->pattern_banks[0];auto& pad=e->macro_pads[0];pad.route_count=2;pad.routes[0]={0,0,5,0,32767};pad.routes[1]={0,1,5,0,32767};
  const auto a=render(*t,500);REQUIRE(tilt_edit::extend(*e,0,0));REQUIRE(!tilt_edit::extend(*e,0,0)&&t->extended()&&t->num_params==6);REQUIRE(!tilt_edit::extend(*e,-1,0)&&!tilt_edit::extend(*e,0,1));
  for(int p=0;p<first;++p){REQUIRE(t->params[p].base==1000+p*3000&&t->params[p].src==ModSource::SigilF);for(auto&scene:e->scenes[0])REQUIRE(scene.base[0][p]==1234+p);}
  if(!clear){REQUIRE(t->params[5].base==tilt_eq::TRIM_UNITY&&t->params[5].src==ModSource::None);for(auto&scene:e->scenes[0])REQUIRE(scene.base[0][5]==tilt_eq::TRIM_UNITY);}
  REQUIRE(pad.route_count==(clear?2:1));for(auto& bank:e->pattern_banks)for(int p=first;p<12;++p)REQUIRE(bank.steps[0][p].lock_count==0);
  for(auto&scene:e->scenes[0])for(int p=0;p<12;++p)REQUIRE(scene.base[1][p]==6789+p);
  q15 v=12000;t->process(&v,1);REQUIRE(v==a.back());
 }
}
static void persistence(){
 auto e=std::make_unique<Engine>();auto* t=new T;e->chains[0].units[0]=t;
 for(int p=0;p<6;++p){param(*t,p,p?1000+p*2000:-12000);t->params[p].src=ModSource::SigilG;t->params[p].mod_depth=p*151;t->params[p].mod_enabled=p%2;}
 for(auto&scene:e->scenes[0]){scene.kind[0]=UnitKind::Tilt;for(int p=0;p<6;++p)scene.base[0][p]=q15(p*1999);}
 for(auto&bank:e->pattern_banks)for(int p=0;p<12;++p){auto&s=bank.steps[0][p];s.lock_count=p<6?1:0;if(p<6)s.locks[0]={0,uint8_t(p),q15(p*1700)};}
 e->pattern=e->pattern_banks[0];e->macro_pads[0].route_count=1;e->macro_pads[0].routes[0]={0,0,5,0,32767};
 REQUIRE(patchio::save(*e,(directory+"/extended.snc").c_str()));auto bytes=read(directory+"/extended.snc");REQUIRE(bytes[3]=='E'&&bytes[4]==14&&bytes[chunk(bytes,"UNIT")+8]==2);
 auto loaded=std::make_unique<Engine>();REQUIRE(patchio::load(*loaded,(directory+"/extended.snc").c_str()));auto* got=static_cast<T*>(loaded->chains[0].units[0]);REQUIRE(got->extended()&&got->num_params==6);
 for(int p=0;p<6;++p){REQUIRE(got->params[p].base==t->params[p].base&&got->params[p].src==t->params[p].src&&got->params[p].mod_depth==t->params[p].mod_depth&&got->params[p].mod_enabled==t->params[p].mod_enabled);for(const auto& bank:loaded->pattern_banks)REQUIRE(bank.steps[0][p].lock_count==1);for(const auto&scene:loaded->scenes[0])REQUIRE(scene.base[0][p]==p*1999);}
 REQUIRE(loaded->macro_pads[0].route_count==1);std::unique_ptr<Unit> clone(clone_unit(got));REQUIRE(static_cast<T*>(clone.get())->extended());REQUIRE(render(*got,8192)==render(*static_cast<T*>(clone.get()),8192));
 REQUIRE(patchio::save(*loaded,(directory+"/extended-roundtrip.snc").c_str()));REQUIRE(bytes==read(directory+"/extended-roundtrip.snc"));
 {auto v=bytes;size_t at=chunk(v,"UNIT"),n=8+NUM_CHAINS*UNITS_PER_CHAIN;std::vector<uint8_t> u(v.begin()+at,v.begin()+at+n);v.erase(v.begin()+at,v.begin()+at+n);v.insert(v.begin()+5,u.begin(),u.end());save("reordered.snc",v.data(),v.size());REQUIRE(patchio::load(*loaded,(directory+"/reordered.snc").c_str()));REQUIRE(static_cast<T*>(loaded->chains[0].units[0])->extended());}
 for(int flags:{3,4,128,255}){auto v=bytes;v[chunk(v,"UNIT")+8]=uint8_t(flags);save("invalid.snc",v.data(),v.size());auto* previous=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/invalid.snc").c_str())&&loaded->chains[0].units[0]==previous);}
 for(int ver=7;ver<14;++ver){auto v=bytes;v[3]=uint8_t(ver<10?'0'+ver:'A'+ver-10);v[4]=uint8_t(ver);save("invalid-version.snc",v.data(),v.size());auto* previous=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/invalid-version.snc").c_str())&&loaded->chains[0].units[0]==previous);}
 // Malformed/truncated inputs remain transactional.
 for(size_t n:{size_t(0),size_t(4),chunk(bytes,"UNIT")+10,chunk(bytes,"CORE")+15}){save("truncated.snc",bytes.data(),n);auto* previous=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/truncated.snc").c_str())&&loaded->chains[0].units[0]==previous);}
 for(int law=0;law<3;++law){auto name="old-"+std::to_string(law)+".snc";REQUIRE(patchio::load(*loaded,(directory+"/"+name).c_str()));got=static_cast<T*>(loaded->chains[0].units[0]);REQUIRE(!got->extended()&&got->modern()==(law==2));clone.reset(clone_unit(got));REQUIRE(static_cast<T*>(clone.get())->revision()==got->revision());REQUIRE(patchio::save(*loaded,(directory+"/old-roundtrip-"+std::to_string(law)+".snc").c_str()));REQUIRE(read(directory+"/"+name)==read(directory+"/old-roundtrip-"+std::to_string(law)+".snc"));}
 // Legacy flag in any chunked container cleans stale trim ONLY, after scenes/macros/banks.
 for(int flag:{0,1}){auto v=bytes;v[chunk(v,"UNIT")+8]=uint8_t(flag);save("padding.snc",v.data(),v.size());REQUIRE(patchio::load(*loaded,(directory+"/padding.snc").c_str()));got=static_cast<T*>(loaded->chains[0].units[0]);REQUIRE(!got->extended()&&got->num_params==(flag?6:5));REQUIRE(loaded->macro_pads[0].route_count==(flag?1:0));for(auto&bank:loaded->pattern_banks)REQUIRE(bank.steps[0][5].lock_count==(flag?1:0));for(auto&scene:loaded->scenes[0])REQUIRE(scene.base[0][5]==(flag?9995:0));}
 // Conditional format does not force older clean Tilt-only patches to14.
 auto other=std::make_unique<Engine>();other->chains[0].units[0]=new T(true);REQUIRE(patchio::save(*other,(directory+"/clear7.snc").c_str()));REQUIRE(read(directory+"/clear7.snc")[4]==7);
 e->chains[0].units[1]=new FoldUnit;REQUIRE(patchio::save(*e,(directory+"/tilt-fold14.snc").c_str()));REQUIRE(read(directory+"/tilt-fold14.snc")[4]==14);
 e->chains[0].units[2]=new VcaUnit;REQUIRE(patchio::save(*e,(directory+"/all15.snc").c_str()));REQUIRE(read(directory+"/all15.snc")[4]==15);REQUIRE(patchio::load(*loaded,(directory+"/all15.snc").c_str()));REQUIRE(static_cast<T*>(loaded->chains[0].units[0])->extended()&&static_cast<FoldUnit*>(loaded->chains[0].units[1])->modern()&&static_cast<VcaUnit*>(loaded->chains[0].units[2])->modern());
}
static void historical(){
 auto loaded=std::make_unique<Engine>();
 for(int version=1;version<=6;++version)for(int range:{0,21846}){
  std::vector<uint8_t> b={'S','N','C',uint8_t('0'+version),uint8_t(version)};const int raw[]={23177,31000,12000,21000,range,-27777};
  auto w16=[&](size_t at,int value){b[at]=uint8_t(value);b[at+1]=uint8_t(uint16_t(value)>>8);};
  for(int c=0;c<NUM_CHAINS;++c){b.insert(b.end(),{255,127});if(version>=2)b.insert(b.end(),{0,0});b.push_back(0);size_t first=b.size();b.insert(b.end(),UNITS_PER_CHAIN*(3+6*5),0);if(!c){b[first]=uint8_t(UnitKind::Tilt);for(int p=0;p<6;++p){w16(first+3+p*5,raw[p]);b[first+3+p*5+2]=5;w16(first+3+p*5+3,19001);}}}
  b.insert(b.end(),NUM_SIGILS*(1+SIGIL_POINTS*2+2+2)+NUM_SIGILS*3,0);
  if(version>=3){b.insert(b.end(),{120,0});b.insert(b.end(),NUM_SIGILS*3,0);}
  if(version>=4){size_t at=b.size();b.insert(b.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);b[at]=255;}
  if(version>=5){b.insert(b.end(),patchio::SampleManifest::SLOT_COUNT*4,0);b.insert(b.end(),{0,4,0});}
  if(version>=6)b.insert(b.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
  save("historical.snc",b.data(),b.size());REQUIRE(patchio::load(*loaded,(directory+"/historical.snc").c_str()));auto* got=static_cast<T*>(loaded->chains[0].units[0]);REQUIRE(!got->modern()&&!got->extended()&&got->num_params==5);T reference(false);
  for(int p=0;p<5;++p){param(reference,p,raw[p]);REQUIRE(got->params[p].base==raw[p]);got->params[p].snap();}REQUIRE(got->params[5].base==tilt_eq::TRIM_UNITY&&got->params[5].src==ModSource::None);REQUIRE(render(*got,8192,true)==render(reference,8192,true));
 }
 // All inactive tails, scene routes, macro routes and each of eight bank records.
 for(int revision=0;revision<3;++revision){auto e=std::make_unique<Engine>();auto* t=new T(T::Revision(revision));e->chains[0].units[0]=t;const int first=t->num_params;
  for(auto&scene:e->scenes[0]){scene.kind[0]=UnitKind::Tilt;for(int p=0;p<12;++p)scene.base[0][p]=q15(1000+p);}
  for(auto&bank:e->pattern_banks)for(int p=0;p<12;++p){auto& step=bank.steps[0][p];step.lock_count=1;step.locks[0]={0,uint8_t(p),1234};}
  e->pattern=e->pattern_banks[0];
  for(int i=0;i<8;++i){e->macro_pads[i].route_count=1;e->macro_pads[i].routes[0]={0,0,uint8_t(i+4),0,32767};}
  REQUIRE(patchio::save(*e,(directory+"/poison.snc").c_str()));auto b=read(directory+"/poison.snc");const size_t at=chunk(b,"CORE")+8+5+3;
  for(int p=first;p<12;++p){b[at+p*5]=0x55;b[at+p*5+1]=0x55;b[at+p*5+2]=5;b[at+p*5+3]=0x33;b[at+p*5+4]=0x33;}
  save("poison.snc",b.data(),b.size());REQUIRE(patchio::load(*loaded,(directory+"/poison.snc").c_str()));auto* got=static_cast<T*>(loaded->chains[0].units[0]);REQUIRE(int(got->revision())==revision);
  for(int p=first;p<12;++p){REQUIRE(got->params[p].base==(p==5?tilt_eq::TRIM_UNITY:0)&&got->params[p].src==ModSource::None);for(auto&bank:loaded->pattern_banks)REQUIRE(bank.steps[0][p].lock_count==0);for(auto&scene:loaded->scenes[0])REQUIRE(scene.base[0][p]==0);}
  for(int i=0;i<8;++i)REQUIRE(loaded->macro_pads[i].route_count==(i+4<first?1:0));
 }
}
static void extended_tests(){maps();sound();engine_endpoints();extension();persistence();historical();}
#endif
int main(int argc,char** argv){if(argc==3&&std::string(argv[1])=="--old-reader"){auto e=std::make_unique<Engine>();e->chains[0].units[0]=new T(true);Unit* previous=e->chains[0].units[0];REQUIRE(!patchio::load(*e,argv[2])&&e->chains[0].units[0]==previous);std::puts("actual pre-edit reader rejects extended Tilt without mutation");return 0;}REQUIRE(argc==2);directory=argv[1];old_laws();
#ifndef TILT_BASELINE
 extended_tests();
#endif
 std::printf("tilt focused: %zu checks passed\n",checks);
}
