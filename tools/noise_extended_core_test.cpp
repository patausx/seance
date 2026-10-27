// Focused NOISE modern sound + pre-edit legacy fixtures; no broad suite.
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
using namespace seance;
using N=NoiseUnit;
static size_t checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"failed %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)
static std::string directory;
static void save(const std::string& name,const void* data,size_t bytes){std::ofstream f(directory+"/"+name,std::ios::binary);f.write(static_cast<const char*>(data),bytes);REQUIRE(bool(f));}
static std::vector<uint8_t> read(const std::string& name){std::ifstream f(name,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void param(N& n,int p,int value){n.params[p].base=q15(value);n.params[p].snap();}
static std::vector<q15> pcm(N& n,int frames,bool partition=false){std::vector<q15> result(frames);int offset=0;while(offset<frames){int count=partition?std::min(frames-offset,1+(offset*7+11)%193):frames;n.process(result.data()+offset,count);offset+=count;}return result;}
static void legacy_fixtures(){
 for(int colour=0;colour<4;++colour)for(int variant=0;variant<5;++variant){
#ifdef NOISE_BASELINE
  N n;
#else
  N n(false);
#endif
  const int defaults[8]={colour*32767/3,20316,16384,24576,0,32767,0,32767};
  for(int p=0;p<8;++p)param(n,p,defaults[p]);
  if(variant){param(n,1,variant==1?0:variant==2?32767:500);param(n,4,variant*7000);param(n,5,variant==1?0:variant==2?16384:32767);param(n,6,variant>2?16384:0);param(n,7,variant==4?0:16384);}
  n.reset();n.control_tick();if(variant)n.trigger();auto out=pcm(n,8192,true);
  n.trigger();auto tail=pcm(n,8192,true);out.insert(out.end(),tail.begin(),tail.end());
  save("legacy-"+std::to_string(colour)+"-"+std::to_string(variant)+".pcm",out.data(),out.size()*2);
 }
 auto e=std::make_unique<Engine>();
#ifdef NOISE_BASELINE
 e->chains[0].units[0]=new N;
#else
 e->chains[0].units[0]=new N(false);
#endif
 auto* n=static_cast<N*>(e->chains[0].units[0]);param(*n,6,3210);param(*n,7,16384);
 n->params[2].src=ModSource::SigilA;n->params[2].mod_depth=1111;
 REQUIRE(patchio::save(*e,(directory+"/legacy.snc").c_str()));
}
#ifndef NOISE_BASELINE
static bool quiet(const std::vector<q15>& out){return std::all_of(out.begin(),out.end(),[](q15 v){return v==0;});}
static void maps_and_envelope(){
 REQUIRE(N::P_COUNT==12&&N::LEGACY_COUNT==8&&PARAM_BANK_SIZE==12);
 REQUIRE(N::grain_millihz(0)==30000&&N::grain_millihz(32767)==32000000);
 REQUIRE(N::dust_millihz(0)==0&&N::dust_millihz(1)==100&&N::dust_millihz(32767)==8000000);
 REQUIRE(N::tone_millihz(0)==25000&&N::tone_millihz(32767)==14000000);
 REQUIRE(N::body_millihz(0)==0&&N::body_millihz(1)==20000&&N::body_millihz(32767)==6000000);
 REQUIRE(N::decay_frames(0)==160&&N::decay_frames(32767)==320000);
 REQUIRE(N::sweep_frames(0)==32&&N::sweep_frames(32767)==160000);
 REQUIRE(N::event_frames(0)==32&&N::event_frames(32767)==8000);
 REQUIRE(N::sweep_cents(-32768)==-2400&&N::sweep_cents(32767)==2400&&N::sweep_cents(0)==0);
 using Map=uint32_t(*)(q15);const Map maps[]={N::grain_millihz,N::dust_millihz,N::tone_millihz,N::body_millihz,N::decay_frames,N::sweep_frames,N::event_frames};
 for(auto map:maps){uint32_t before=0;for(int v=0;v<=32767;++v){const auto next=map(q15(v));REQUIRE(next>=before);before=next;}}
 for(int attack:{0,1,16384,32767})for(int decay:{0,16384,32767}){
  N n;param(n,N::P_ATTACK,attack);param(n,N::P_DECAY,decay);param(n,N::P_MODE,16384);n.reset();n.control_tick();
  REQUIRE(n.visual_envelope()==0&&quiet(pcm(n,512)));n.trigger();
  uint32_t time=0,total=N::attack_frames(q15(attack))+N::decay_frames(q15(decay));
  while(time<total){const uint32_t count=std::min<uint32_t>(997,total-time);pcm(n,int(count));time+=count;
   int expected=int(N::model_envelope(time,N::attack_frames(q15(attack)),N::decay_frames(q15(decay)),false)*32767.f);
   REQUIRE(std::abs(int(n.visual_envelope())-expected)<=1);
  }
  REQUIRE(n.visual_envelope()==0&&quiet(pcm(n,256)));
 }
 {N n;param(n,N::P_MODE,16384);param(n,N::P_ATTACK,16384);n.reset();n.trigger();pcm(n,3200);auto before=n.visual_envelope();n.trigger();REQUIRE(n.visual_envelope()==before);pcm(n,100);REQUIRE(n.visual_envelope()>=before);}
 {N n;param(n,N::P_MODE,16384);param(n,N::P_ATTACK,0);param(n,N::P_DECAY,32767);n.reset();n.trigger();pcm(n,160000);REQUIRE(std::abs(int(n.visual_envelope())-4095)<=1);pcm(n,160000);REQUIRE(n.visual_envelope()==0);}
 {N n;param(n,N::P_MODE,16384);n.control_tick();REQUIRE(quiet(pcm(n,512))&&n.visual_envelope()==0);}
 // Ordinary shared Param slew (not snap helper) must reach true semantic zero.
 for(int p:{N::P_GAIN,N::P_BODY,N::P_DENSITY,N::P_ATTACK,N::P_SWEEP}){
  N n;param(n,p,20000);n.params[p].resolve(0);n.params[p].base=0;
  for(int i=0;i<80;++i){n.params[p].resolve(0);n.control_tick();pcm(n,32);}
  REQUIRE(n.params[p].effective>0&&n.params[p].effective<=3&&n.control_value(p)==0);
  if(p==N::P_GAIN)REQUIRE(quiet(pcm(n,512)));
 }
}
static void sound(){
 for(int colour:{0,10922,21844,32767})for(int edge:{0,16384,32767}){
  N a,b;for(N* n:{&a,&b}){for(int p=0;p<N::P_COUNT;++p)param(*n,p,edge);param(*n,N::P_COLOUR,colour);param(*n,N::P_MODE,16384);param(*n,N::P_GAIN,32767);n->reset();n->control_tick();n->trigger();}
  auto out=pcm(a,32768),split=pcm(b,32768,true);REQUIRE(out==split);
  save("modern-"+std::to_string(colour)+"-"+std::to_string(edge)+".pcm",out.data(),out.size()*2);
 }
 for(int feature:{N::P_DENSITY,N::P_RESONANCE,N::P_SWEEP,N::P_SWEEP_TIME,N::P_EVENT_LENGTH}){
  N a,b;for(N* n:{&a,&b}){param(*n,N::P_MODE,16384);param(*n,N::P_COLOUR,feature==N::P_EVENT_LENGTH?21844:10922);param(*n,N::P_DENSITY,feature==N::P_EVENT_LENGTH?4000:25000);param(*n,N::P_SWEEP,18000);}
  param(b,feature,feature==N::P_RESONANCE?30000:0);a.reset();b.reset();a.trigger();b.trigger();REQUIRE(pcm(a,32000)!=pcm(b,32000));
 }
 {N a,b;param(a,N::P_MODE,32767);param(b,N::P_MODE,32767);param(b,N::P_SWEEP,32767);param(b,N::P_DECAY,0);a.reset();b.reset();REQUIRE(pcm(a,4096)==pcm(b,4096));}
 {N a(false),b(false);for(int p=8;p<12;++p)param(b,p,32767);a.reset();b.reset();a.control_tick();b.control_tick();REQUIRE(pcm(a,4096)==pcm(b,4096));}
 {N n(false);n.control_tick();auto out=pcm(n,100);n.set_modern(true);q15 v;n.process(&v,1);REQUIRE(v==out.back());}
 {N n;param(n,N::P_COLOUR,21844);param(n,N::P_DENSITY,0);n.reset();n.trigger();REQUIRE(quiet(pcm(n,64000)));}
 // Sparse actual events: isolated short pulse + almost-open filter. Count groups
 // of nontrivial output, not claims about truly random/Poisson spectral quality.
 {N n;int density=1;while(N::dust_millihz(q15(density))<4000)++density;
  param(n,N::P_COLOUR,21844);param(n,N::P_DENSITY,density);param(n,N::P_TONE,32767);param(n,N::P_EVENT_LENGTH,0);n.reset();auto out=pcm(n,32000*60);
  int groups=0,last=-10000;for(int i=0;i<int(out.size());++i)if(std::abs(int(out[i]))>32){if(i-last>80)++groups;last=i;}
  std::printf("sparse ~4/s: %d resolved output groups / 60s\n",groups);REQUIRE(groups>=170&&groups<=300);
 }
 for(int tone:{0,16000,32767})for(int body:{0,16000,32767})for(int res:{0,16000,32767})for(int cents:{-2400,0,2400})for(float hz:{30.f,300.f,3000.f,15000.f})REQUIRE(std::isfinite(N::model_response_db(hz,q15(tone),q15(body),q15(res),cents)));
 // Exercise live modulation extremes/control updates; UBSAN also checks casts.
 {N n;std::array<q15,32> block{};for(int tick=0;tick<4000;++tick){for(int p=0;p<12;++p){n.params[p].base=q15((tick*177+p*7919)%32768);n.params[p].resolve(0);}if(tick%89==0)n.trigger();n.control_tick();n.process(block.data(),32);} }
}
static void engine_events(){
 auto e=std::make_unique<Engine>();auto* n=new N;e->chains[0].units[0]=n;
 param(*n,N::P_MODE,16384);param(*n,N::P_ATTACK,0);std::array<q15,1024> out{};
 e->render(out.data(),512);REQUIRE(n->visual_envelope()==0&&quiet({out.begin(),out.end()}));
 e->request_chain_trigger(0,LaunchQuantize::Immediate);e->render(out.data(),512);
 REQUIRE(n->visual_envelope()>0&&n->visual_activity()>0);
 e->transport_stop();n->reset();e->pattern.steps[0][0].flags=SeqStep::Trig;
 REQUIRE(e->set_step_lock(0,0,0,N::P_ATTACK,16384));e->set_sequencer_enabled(true);e->transport_reset();e->transport_play();
 // Lazy sequence startup schedules step0 at the next sixteenth, not frame0.
 for(int i=0;i<80;++i){e->render(out.data(),50);}
 e->render(out.data(),512);
 REQUIRE(n->params[N::P_ATTACK].base==0&&n->params[N::P_ATTACK].step_lock_override);
 REQUIRE(n->visual_envelope()>0&&n->visual_envelope()<2500); // real lock resolved before trigger
}
static size_t chunk(const std::vector<uint8_t>& b,const char* id){for(size_t at=5;at+8<=b.size();){uint32_t size=0;for(int i=0;i<4;++i)size|=uint32_t(b[at+4+i])<<(i*8);if(!std::memcmp(b.data()+at,id,4))return at;at+=8+size;}REQUIRE(false);return 0;}
static void persistence(){
 auto e=std::make_unique<Engine>();auto* n=new N;e->chains[0].units[0]=n;
 for(int p=0;p<12;++p){param(*n,p,p==9?-23456:1000+p*2300);n->params[p].src=ModSource::SigilG;n->params[p].mod_depth=q15(p*91);n->params[p].mod_enabled=p%2;}
 for(auto& scene:e->scenes[0]){scene.kind[0]=UnitKind::Noise;for(int p=0;p<12;++p)scene.base[0][p]=q15(p==9?-12000:p*1234);}
 for(auto& bank:e->pattern_banks)for(int p=6;p<12;++p){auto& step=bank.steps[0][p];step.lock_count=1;step.locks[0]={0,uint8_t(p),q15(p==9?-23000:p*1900)};}
 for(int p=6;p<12;++p){auto& step=e->pattern.steps[0][p];step.lock_count=1;step.locks[0]={0,uint8_t(p),q15(p==9?-23000:p*1900)};}
 auto& pad=e->macro_pads[0];pad.route_count=1;pad.routes[0].chain=0;pad.routes[0].slot=0;pad.routes[0].param=11;
 REQUIRE(patchio::save(*e,(directory+"/modern.snc").c_str()));auto bytes=read(directory+"/modern.snc");REQUIRE(bytes[3]=='9'&&bytes[4]==9&&bytes[chunk(bytes,"UNIT")+8]==0x83);
 auto loaded=std::make_unique<Engine>();REQUIRE(patchio::load(*loaded,(directory+"/modern.snc").c_str()));auto* got=static_cast<N*>(loaded->chains[0].units[0]);REQUIRE(got->modern()&&got->num_params==12&&got->params[9].bipolar);
 for(int p=0;p<12;++p){REQUIRE(got->params[p].base==n->params[p].base&&got->params[p].src==n->params[p].src&&got->params[p].mod_depth==n->params[p].mod_depth&&got->params[p].mod_enabled==n->params[p].mod_enabled);}
 for(const auto& bank:loaded->pattern_banks)for(int p=6;p<12;++p)REQUIRE(bank.steps[0][p].lock_count==1);
 REQUIRE(loaded->scenes[0][0].base[0][9]==-12000&&loaded->macro_pads[0].route_count==1);
 std::unique_ptr<Unit> cloned(clone_unit(got));REQUIRE(static_cast<N*>(cloned.get())->modern());for(int p=0;p<12;++p)REQUIRE(cloned->params[p].base==got->params[p].base);
 REQUIRE(patchio::save(*loaded,(directory+"/modern-roundtrip.snc").c_str()));REQUIRE(bytes==read(directory+"/modern-roundtrip.snc"));
 // UNIT can precede CORE without losing the synthesis revision or parameters.
 {auto reordered=bytes;size_t at=chunk(bytes,"UNIT"),size=8+NUM_CHAINS*UNITS_PER_CHAIN;std::vector<uint8_t> unit(bytes.begin()+at,bytes.begin()+at+size);reordered.erase(reordered.begin()+at,reordered.begin()+at+size);reordered.insert(reordered.begin()+5,unit.begin(),unit.end());save("reordered.snc",reordered.data(),reordered.size());REQUIRE(patchio::load(*loaded,(directory+"/reordered.snc").c_str()));REQUIRE(static_cast<N*>(loaded->chains[0].units[0])->modern());}
 for(int flags:{0x81,0x80,0}){
  auto old=bytes;old[3]='7';old[4]=7;old[chunk(old,"UNIT")+8]=uint8_t(flags);save("old-padding.snc",old.data(),old.size());REQUIRE(patchio::load(*loaded,(directory+"/old-padding.snc").c_str()));got=static_cast<N*>(loaded->chains[0].units[0]);REQUIRE(!got->modern()&&got->num_params==8);
  int first=flags==0x81?8:6;
  for(int p=first;p<12;++p){REQUIRE(got->params[p].base==0&&got->params[p].src==ModSource::None);for(const auto& bank:loaded->pattern_banks)REQUIRE(bank.steps[0][p].lock_count==0);for(const auto& scene:loaded->scenes[0])REQUIRE(scene.base[0][p]==0);}
  REQUIRE(loaded->macro_pads[0].route_count==0);
  cloned.reset(clone_unit(got));REQUIRE(!static_cast<N*>(cloned.get())->modern()&&cloned->num_params==8);
  static_cast<N*>(cloned.get())->set_modern(true);REQUIRE(cloned->params[N::P_SWEEP].bipolar);
 }
 for(int flags:{1,2,0x82,0x87}){auto bad=bytes;bad[chunk(bad,"UNIT")+8]=uint8_t(flags);save("bad.snc",bad.data(),bad.size());Unit* previous=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/bad.snc").c_str())&&loaded->chains[0].units[0]==previous);}
 for(int version:{7,8}){auto bad=bytes;bad[3]=uint8_t('0'+version);bad[4]=uint8_t(version);save("bad-version.snc",bad.data(),bad.size());REQUIRE(!patchio::load(*loaded,(directory+"/bad-version.snc").c_str()));}
 REQUIRE(patchio::load(*loaded,(directory+"/legacy.snc").c_str()));REQUIRE(patchio::save(*loaded,(directory+"/legacy-roundtrip.snc").c_str()));REQUIRE(read(directory+"/legacy.snc")==read(directory+"/legacy-roundtrip.snc"));
 // Conditional versions and simultaneous modern MOTOR/NOISE revisions.
 auto m=std::make_unique<Engine>();m->chains[0].units[0]=new N(false);m->chains[0].units[1]=new MotorUnit;
 REQUIRE(patchio::save(*m,(directory+"/motor8.snc").c_str()));REQUIRE(read(directory+"/motor8.snc")[3]=='8');REQUIRE(patchio::load(*loaded,(directory+"/motor8.snc").c_str()));REQUIRE(!static_cast<N*>(loaded->chains[0].units[0])->modern()&&static_cast<MotorUnit*>(loaded->chains[0].units[1])->modern());
 static_cast<N*>(m->chains[0].units[0])->set_modern(true);REQUIRE(patchio::save(*m,(directory+"/both9.snc").c_str()));REQUIRE(patchio::load(*loaded,(directory+"/both9.snc").c_str()));REQUIRE(static_cast<N*>(loaded->chains[0].units[0])->modern()&&static_cast<MotorUnit*>(loaded->chains[0].units[1])->modern());
}
#endif
int main(int argc,char** argv){
 if(argc==3&&std::string(argv[1])=="--old-reader"){
  auto e=std::make_unique<Engine>();e->chains[0].units[0]=new N;Unit* previous=e->chains[0].units[0];REQUIRE(!patchio::load(*e,argv[2])&&e->chains[0].units[0]==previous);std::puts("actual pre-edit reader rejects extended NOISE without mutation");return 0;
 }
 REQUIRE(argc==2);directory=argv[1];legacy_fixtures();
#ifndef NOISE_BASELINE
 maps_and_envelope();sound();engine_events();persistence();
#endif
 std::printf("noise focused: %zu checks passed\n",checks);
}
