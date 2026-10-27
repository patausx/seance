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
using namespace seance; using F=FoldUnit;
static size_t checks=0; static std::string directory;
#define REQUIRE(x) do { ++checks; if(!(x)){std::fprintf(stderr,"failed %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)
static void save(const std::string& name,const void* p,size_t n){std::ofstream f(directory+"/"+name,std::ios::binary);f.write(static_cast<const char*>(p),n);REQUIRE(bool(f));}
static std::vector<uint8_t> read(const std::string& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void param(F& f,int p,int v){f.params[p].base=q15(v);f.params[p].snap();}
static std::unique_ptr<F> old_unit(){
#ifdef FOLD_BASELINE
 return std::make_unique<F>();
#else
 return std::make_unique<F>(false);
#endif
}
static std::vector<q15> input(int n){std::vector<q15> a(n);uint32_t rng=67191;for(int i=0;i<n;++i){rng=rng*1664525u+1013904223u;a[i]=i%3?q15((i%256-128)*255):q15(int(rng>>16)-32768);}return a;}
static std::vector<q15> render(F& f,int n,bool split=false){auto a=input(n);for(int i=0;i<n;){int count=split?std::min(n-i,1+(i*7+11)%197):n;f.process(a.data()+i,count);i+=count;}return a;}
static void legacy(){
 for(int v=0;v<32;++v){auto f=old_unit();if(v){for(int p=0;p<5;++p)param(*f,p,(v*7919+p*1701)%32768);param(*f,F::P_BIAS,v%2?-32767:32767);if(v<11)param(*f,(v-1)/2,v%2?0:32767);}
  auto a=render(*f,8192,true);param(*f,F::P_MODE,32767);param(*f,F::P_BIAS,-16384);std::vector<q15> silence(4096);f->process(silence.data(),4096);a.insert(a.end(),silence.begin(),silence.end());f->reset();auto b=render(*f,4096,true);a.insert(a.end(),b.begin(),b.end());save("legacy-"+std::to_string(v)+".pcm",a.data(),a.size()*2);
 }
 auto e=std::make_unique<Engine>();auto f=old_unit();param(*f,F::P_BIAS,-12345);f->params[0].src=ModSource::SigilB;f->params[0].mod_depth=3333;e->chains[0].units[0]=f.release();REQUIRE(patchio::save(*e,(directory+"/legacy.snc").c_str()));
}
#ifndef FOLD_BASELINE
static size_t chunk(const std::vector<uint8_t>& b,const char* id){for(size_t at=5;at+8<=b.size();){uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(b[at+4+i])<<(8*i);if(!std::memcmp(b.data()+at,id,4))return at;at+=8+n;}REQUIRE(false);return 0;}
static void maps(){
 REQUIRE(F::P_COUNT==7&&F::LEGACY_COUNT==5&&PARAM_BANK_SIZE==12);
 REQUIRE(F::drive_q16(-32767)==16384&&F::drive_q16(0)==65536&&F::drive_q16(32767)==524288);
 REQUIRE(F::drive_q16(-32768)==16384);REQUIRE(F::tone_hz(0)==100&&F::tone_hz(32766)==16000&&F::tone_hz(32767)==0);
 for(int v=-32766;v<=32767;++v)REQUIRE(F::drive_q16(q15(v))>=F::drive_q16(q15(v-1)));
 for(int v=1;v<=32766;++v)REQUIRE(F::tone_hz(q15(v))>=F::tone_hz(q15(v-1)));
 F modern;F old(false);REQUIRE(modern.modern()&&modern.num_params==7&&modern.params[0].bipolar);REQUIRE(!old.modern()&&old.num_params==5&&!old.params[0].bipolar);
}
static void sound(){
 for(int variant=0;variant<24;++variant){F a,b;for(F* f:{&a,&b}){for(int p=0;p<7;++p)param(*f,p,(variant*7919+p*1701)%32768);param(*f,F::P_DRIVE,variant%2?-16000:30000);param(*f,F::P_BIAS,variant%3?-22000:21000);f->reset();}auto x=render(a,16384),y=render(b,16384,true);REQUIRE(x==y);save("modern-"+std::to_string(variant)+".pcm",x.data(),x.size()*2);a.reset();REQUIRE(render(a,16384)==x);}
 for(int p=0;p<7;++p){F a,b;param(b,p,p==F::P_TRIM||p==F::P_MIX?0:p==F::P_TONE?12000:p==F::P_DRIVE?-32767:30000);a.reset();b.reset();REQUIRE(render(a,8192)!=render(b,8192));}
 for(int mode:{0,16384,32767})for(int bias:{-32767,0,32767}){
  F f;param(f,F::P_MODE,mode);param(f,F::P_BIAS,bias);param(f,F::P_DRIVE,32767);param(f,F::P_MIX,0);f.reset();REQUIRE(render(f,8192,true)==input(8192));
  param(f,F::P_MIX,32767);param(f,F::P_TRIM,0);REQUIRE(render(f,8192,true)==std::vector<q15>(8192));
  param(f,F::P_TRIM,32767);f.reset();std::vector<q15> silence(64000);f.process(silence.data(),int(silence.size()));int peak=0;for(size_t i=60000;i<silence.size();++i)peak=std::max(peak,std::abs(int(silence[i])));REQUIRE(peak<=2);
 }
 // Endpoints from the ordinary shared slew, without changing shared Param.
 for(int p:{F::P_DRIVE,F::P_TRIM,F::P_MIX,F::P_MODE,F::P_BIAS,F::P_SMOOTH,F::P_TONE}){F f;param(f,p,12000);f.params[p].resolve(0);f.params[p].base=0;for(int i=0;i<100;++i){f.params[p].resolve(0);f.control_tick();}REQUIRE(f.params[p].effective>0&&f.control_value(p)==0);}
 {F f(false);auto x=render(f,1000);const q15 last=x.back();f.set_modern(true);param(f,F::P_TONE,32767);q15 sample=10000;f.process(&sample,1);REQUIRE(sample==last&&f.params[0].bipolar&&f.num_params==7);}
 // Dry/mute take priority over the conversion bridge.
 {F f(false);render(f,1000);f.set_modern(true);param(f,F::P_MIX,0);REQUIRE(render(f,8192)==input(8192));}
 for(int muted:{F::P_MIX,F::P_TRIM}){F a,b;param(a,muted,0);render(a,4096);render(b,4096);param(a,muted,32767);REQUIRE(render(a,4096)==render(b,4096));}
 {F f;std::array<q15,32>a{};for(int tick=0;tick<2500;++tick){for(int p=0;p<7;++p){param(f,p,(tick*173+p*7919)%32768);if((p==0||p==3)&&tick%2)param(f,p,-f.params[p].base);}f.process(a.data(),int(a.size()));}}
 // Full nonlinear transfer includes the actual drive/bias/smooth law, no filters.
 {q15 p[7]={0,32767,0,0,32767,0,32767};for(int i=-100;i<=100;++i)REQUIRE(std::abs(F::transfer(float(i)/100,p)-float(i)/100)<.00001f);p[F::P_MODE]=32767;REQUIRE(std::abs(F::transfer(-.5f,p)-.125f)<.00001f);p[F::P_BIAS]=-16384;REQUIRE(F::transfer(0,p)>.62f);}
}
static double amplitude(const std::vector<q15>& data,double hz){double re=0,im=0;constexpr int warm=1024;for(size_t i=warm;i<data.size();++i){const double phase=6.283185307179586*hz*double(i)/ENGINE_SR;re+=data[i]*std::cos(phase);im-=data[i]*std::sin(phase);}return 2*std::sqrt(re*re+im*im)/(double(data.size()-warm)*32768.);}
static void spectral(){
 // One deliberately narrow folded-third diagnostic, not an alias-free claim.
 F f;std::vector<q15> data(33792),naive(data.size());q15 values[7];for(int p=0;p<7;++p)values[p]=f.params[p].base;
 for(size_t i=0;i<data.size();++i){data[i]=q15(std::lround(24575*std::sin(6.283185307179586*6250*double(i)/ENGINE_SR)));naive[i]=q15(std::lround(F::transfer(float(data[i])/32768,values)*32767));}
 f.reset();f.process(data.data(),int(data.size()));const double modern=amplitude(data,13250),old=amplitude(naive,13250),fundamental=amplitude(data,6250);
 std::printf("fold6250hz diagnostic: folded3rd %.6f vs naive %.6f; fundamental %.6f\n",modern,old,fundamental);
 REQUIRE(old>.05&&modern<old*.8&&fundamental>.05);save("alias-probe.pcm",data.data(),data.size()*2);
 // Tone is a real post-shaper filter. .25x drive stays in the linear fold zone.
 F a,b;for(F* u:{&a,&b})param(*u,F::P_DRIVE,-32767);param(b,F::P_TONE,0);
 for(size_t i=0;i<data.size();++i)data[i]=q15(std::lround(20000*std::sin(6.283185307179586*2000*double(i)/ENGINE_SR)));
 naive=data;a.reset();b.reset();a.process(data.data(),int(data.size()));b.process(naive.data(),int(naive.size()));REQUIRE(amplitude(naive,2000)<amplitude(data,2000)*.1);
}
static void engine_locks(){
 for(int group=0;group<3;++group){auto e=std::make_unique<Engine>();auto* f=new F;e->chains[0].units[0]=new OscUnit;e->chains[0].units[1]=f;q15 stored[7];for(int p=0;p<7;++p)stored[p]=f->params[p].base;const q15 values[7]={-20000,20000,16384,-10000,24000,27000,10000};e->pattern.steps[0][0].flags=SeqStep::Trig;e->set_sequence_length(0,1);
  for(int p=group*3;p<std::min(7,group*3+3);++p)REQUIRE(e->set_step_lock(0,0,1,p,values[p]));
  e->set_sequencer_enabled(true);e->transport_reset();e->transport_play();std::array<q15,256>out{};for(int i=0;i<80;++i)e->render(out.data(),128);
  for(int p=group*3;p<std::min(7,group*3+3);++p){REQUIRE(f->params[p].step_lock_override&&f->control_value(p)==values[p]);REQUIRE(f->params[p].base==stored[p]);}
  e->set_sequencer_enabled(false);e->transport_stop();for(int i=0;i<40;++i)e->render(out.data(),128);for(int p=0;p<7;++p)REQUIRE(!f->params[p].step_lock_override&&f->control_value(p)==stored[p]);
 }
}
static void persistence(){
 auto e=std::make_unique<Engine>();auto* f=new F;e->chains[0].units[0]=f;
 for(int p=0;p<7;++p){param(*f,p,p==0||p==3?-12345:1000+p*2200);f->params[p].src=ModSource::SigilG;f->params[p].mod_depth=q15(p*151);f->params[p].mod_enabled=p%2;}
 for(auto& scene:e->scenes[0]){scene.kind[0]=UnitKind::Fold;for(int p=0;p<12;++p)scene.base[0][p]=q15(p<7?p*1999:0);}
 for(auto& bank:e->pattern_banks)for(int p=0;p<7;++p){auto&s=bank.steps[0][p];s.lock_count=1;s.locks[0]={0,uint8_t(p),q15(p==0?-12345:p*1900)};}
 for(int p=0;p<7;++p){auto&s=e->pattern.steps[0][p];s.lock_count=1;s.locks[0]={0,uint8_t(p),q15(p==0?-12345:p*1900)};}
 e->macro_pads[0].route_count=1;e->macro_pads[0].routes[0].chain=0;e->macro_pads[0].routes[0].slot=0;e->macro_pads[0].routes[0].param=6;
 REQUIRE(patchio::save(*e,(directory+"/modern.snc").c_str()));auto bytes=read(directory+"/modern.snc");REQUIRE(bytes[3]=='D'&&bytes[4]==13&&bytes[chunk(bytes,"UNIT")+8]==1);
 auto loaded=std::make_unique<Engine>();REQUIRE(patchio::load(*loaded,(directory+"/modern.snc").c_str()));auto* got=static_cast<F*>(loaded->chains[0].units[0]);REQUIRE(got->modern()&&got->num_params==7&&got->params[0].bipolar&&got->params[3].bipolar);
 for(int p=0;p<7;++p){REQUIRE(got->params[p].base==f->params[p].base&&got->params[p].src==f->params[p].src&&got->params[p].mod_depth==f->params[p].mod_depth&&got->params[p].mod_enabled==f->params[p].mod_enabled);for(const auto& bank:loaded->pattern_banks)REQUIRE(bank.steps[0][p].lock_count==1);}
 REQUIRE(loaded->scenes[0][0].base[0][6]==11994&&loaded->macro_pads[0].route_count==1);std::unique_ptr<Unit> clone(clone_unit(got));REQUIRE(static_cast<F*>(clone.get())->modern());for(int p=0;p<7;++p)REQUIRE(clone->params[p].base==got->params[p].base);
 REQUIRE(patchio::save(*loaded,(directory+"/modern-roundtrip.snc").c_str()));REQUIRE(bytes==read(directory+"/modern-roundtrip.snc"));
 {auto v=bytes;size_t at=chunk(v,"UNIT"),n=8+NUM_CHAINS*UNITS_PER_CHAIN;std::vector<uint8_t>u(v.begin()+at,v.begin()+at+n);v.erase(v.begin()+at,v.begin()+at+n);v.insert(v.begin()+5,u.begin(),u.end());save("reordered.snc",v.data(),v.size());REQUIRE(patchio::load(*loaded,(directory+"/reordered.snc").c_str()));REQUIRE(static_cast<F*>(loaded->chains[0].units[0])->modern());}
 for(int version:{7,8,9,10,11,12}){auto v=bytes;v[3]=uint8_t(version<10?'0'+version:'A'+version-10);v[4]=uint8_t(version);v[chunk(v,"UNIT")+8]=0;save("old-padding.snc",v.data(),v.size());REQUIRE(patchio::load(*loaded,(directory+"/old-padding.snc").c_str()));got=static_cast<F*>(loaded->chains[0].units[0]);REQUIRE(!got->modern()&&got->num_params==5&&!got->params[0].bipolar);for(int p=5;p<12;++p){REQUIRE(got->params[p].base==0&&got->params[p].src==ModSource::None);for(const auto& bank:loaded->pattern_banks)if(p<7)REQUIRE(bank.steps[0][p].lock_count==0);for(const auto&scene:loaded->scenes[0])REQUIRE(scene.base[0][p]==0);}REQUIRE(loaded->macro_pads[0].route_count==0);clone.reset(clone_unit(got));REQUIRE(!static_cast<F*>(clone.get())->modern()&&clone->num_params==5);}
 for(int flags:{2,3,128}){auto bad=bytes;bad[chunk(bad,"UNIT")+8]=uint8_t(flags);save("bad.snc",bad.data(),bad.size());Unit* previous=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/bad.snc").c_str())&&loaded->chains[0].units[0]==previous);}
 {auto bad=bytes;bad[3]='C';bad[4]=12;save("bad-version.snc",bad.data(),bad.size());Unit* previous=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/bad-version.snc").c_str())&&loaded->chains[0].units[0]==previous);}
 REQUIRE(patchio::load(*loaded,(directory+"/legacy.snc").c_str()));REQUIRE(patchio::save(*loaded,(directory+"/legacy-roundtrip.snc").c_str()));REQUIRE(read(directory+"/legacy.snc")==read(directory+"/legacy-roundtrip.snc"));
 e->chains[0].units[1]=new RingUnit;e->chains[0].units[2]=new StrainUnit;e->chains[0].units[3]=new BitUnit;e->chains[0].units[4]=new NoiseUnit;e->chains[0].units[5]=new MotorUnit;REQUIRE(patchio::save(*e,(directory+"/all-modern.snc").c_str()));REQUIRE(patchio::load(*loaded,(directory+"/all-modern.snc").c_str()));REQUIRE(static_cast<RingUnit*>(loaded->chains[0].units[1])->modern()&&static_cast<StrainUnit*>(loaded->chains[0].units[2])->modern());
 auto neighbor=std::make_unique<Engine>();neighbor->chains[0].units[0]=new RingUnit;neighbor->chains[0].units[1]=new F(false);REQUIRE(patchio::save(*neighbor,(directory+"/ring12.snc").c_str()));REQUIRE(read(directory+"/ring12.snc")[3]=='C');
 // Higher concurrent container revisions must not erase Fold's own modern marker.
 for(bool vca:{false,true}){auto mixed=std::make_unique<Engine>();auto* fold=new F;param(*fold,F::P_DRIVE,-17000);mixed->chains[0].units[0]=fold;
  mixed->chains[0].units[1]=vca?static_cast<Unit*>(new VcaUnit):static_cast<Unit*>(new TiltUnit);
  const std::string name=directory+(vca?"/fold-vca15.snc":"/fold-tilt14.snc");REQUIRE(patchio::save(*mixed,name.c_str()));const auto file=read(name);REQUIRE(file[3]==(vca?'F':'E')&&file[4]==(vca?15:14));REQUIRE(patchio::load(*loaded,name.c_str()));auto* found=static_cast<F*>(loaded->chains[0].units[0]);REQUIRE(found->modern()&&found->params[F::P_DRIVE].base==-17000&&found->params[F::P_DRIVE].bipolar);
 }
}
#endif
int main(int argc,char**argv){if(argc==3&&std::string(argv[1])=="--old-reader"){auto e=std::make_unique<Engine>();e->chains[0].units[0]=new F;Unit*previous=e->chains[0].units[0];REQUIRE(!patchio::load(*e,argv[2])&&e->chains[0].units[0]==previous);std::puts("actual pre-edit reader rejects extended Fold transactionally");return 0;}REQUIRE(argc==2);directory=argv[1];legacy();
#ifndef FOLD_BASELINE
 maps();sound();spectral();engine_locks();persistence();
#endif
 std::printf("fold focused: %zu checks passed\n",checks);
}
