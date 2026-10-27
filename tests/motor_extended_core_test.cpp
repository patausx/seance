// Focused MOTOR legacy fixtures plus current extension tests (not broad suites).
#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include "engine/musical_note.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
using namespace seance;
using M=MotorUnit;
static size_t checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"failed %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)
static std::string directory;
static void save_bytes(const std::string& name,const void* p,size_t bytes){std::ofstream f(directory+"/"+name,std::ios::binary);f.write(static_cast<const char*>(p),bytes);REQUIRE(bool(f));}
static void legacy_fixtures(){
 for(int source=0;source<3;++source)for(int variant=0;variant<5;++variant){
#ifdef MOTOR_BASELINE
  M m;
#else
  M m(false);
#endif
  const int values[5][5]={{0,0,0,0,32767},{10000,8192,19660,18000,18000},{32767,32767,32767,32767,32767},{23000,12500,28000,17000,0},{14000,0,32767,32767,24000}};
  for(int p=0;p<5;++p){m.params[p].base=q15(values[variant][p]);m.params[p].snap();}
  m.params[5].base=q15(source==0?0:source==1?16384:32767);m.params[5].snap();m.reset();m.control_tick();
  std::vector<q15> out(8192);m.process(out.data(),int(out.size()));
  save_bytes("legacy-"+std::to_string(source)+"-"+std::to_string(variant)+".pcm",out.data(),out.size()*2);
 }
 auto e=std::make_unique<Engine>();
#ifdef MOTOR_BASELINE
 e->chains[0].units[0]=new M;
#else
 e->chains[0].units[0]=new M(false);
#endif
 e->chains[0].units[0]->params[M::P_RPM].base=12345;
 REQUIRE(patchio::save(*e,(directory+"/legacy.snc").c_str()));
}
#ifndef MOTOR_BASELINE
static std::vector<char> read_bytes(const std::string& name){std::ifstream f(name,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void param(M& m,int p,int v){m.params[p].base=q15(v);m.params[p].snap();m.control_tick();}
static std::vector<q15> pcm(M& m,int n=16384,bool split=false,bool raw=false){
 std::vector<q15> result(n),tap(n);int offset=0;
 while(offset<n){int count=split ? std::min(n-offset,1+(offset*7+11)%173) : n;
  m.process_with_raw(result.data()+offset,raw?tap.data()+offset:nullptr,count);offset+=count;
 }
 return raw ? tap : result;
}
static void parameter_tests(){
 REQUIRE(M::rate_millihz(0)==18000&&M::rate_millihz(32767)==760000);
 REQUIRE(M::note_millihz(musical_note::encode(69))==440000);
 REQUIRE(M::note_millihz(musical_note::encode(45))==110000);
 REQUIRE(M::width_q32(16384)==0x80000000u);
 REQUIRE(M::sync_ratio_q16(0)==65536&&M::sync_ratio_q16(32767)==1048576);
 REQUIRE(M::jitter_millihz(0)==100&&M::jitter_millihz(32767)==125000);
 uint32_t width=0,sync=0,jrate=0;
 for(int i=0;i<32768;++i){auto w=M::width_q32(q15(i)),s=M::sync_ratio_q16(q15(i)),j=M::jitter_millihz(q15(i));REQUIRE(w>=width&&s>=sync&&j>=jrate);width=w;sync=s;jrate=j;}
 M m;REQUIRE(m.num_params==11&&m.modern()&&!m.note_mode());
 for(int source:{0,16384,32767})for(int edge:{0,16384,32767}){
  M a,b;for(M* m:{&a,&b}){for(int p=0;p<M::P_COUNT;++p)param(*m,p,edge);param(*m,M::P_SOURCE,source);param(*m,M::P_GAIN,32767);m->reset();}
  REQUIRE(pcm(a,16384)==pcm(b,16384,true));
  b.reset();a.reset();REQUIRE(pcm(a,256,true,true)==pcm(b,256,false,true));
 }
 // Actual new controls alter the samples rather than merely the reference graph.
 for(int feature:{M::P_WIDTH,M::P_SYNC,M::P_JITTER_SPEED}){
  M a,b;for(M* m:{&a,&b}){param(*m,M::P_SOURCE,16384);param(*m,M::P_JITTER,24000);param(*m,M::P_JITTER_SPEED,32767);}
  param(b,feature,feature==M::P_SYNC?19000:0);a.reset();b.reset();
  REQUIRE(pcm(a,32000)!=pcm(b,32000));
 }
 {M a,b;param(a,M::P_SOURCE,0);param(b,M::P_SOURCE,0);param(b,M::P_WIDTH,0);a.reset();b.reset();REQUIRE(pcm(a)==pcm(b));}
 {M a(false),b(false);for(int p=6;p<M::P_COUNT;++p)param(b,p,32767);a.reset();b.reset();REQUIRE(pcm(a)==pcm(b));}
 {M a;param(a,M::P_GAIN,0);for(auto v:pcm(a))REQUIRE(v==0);}
 {M a;param(a,M::P_MODE,32767);param(a,M::P_NOTE,musical_note::encode(96));param(a,M::P_SYNC,32767);a.set_arp_pitch(48);REQUIRE(a.nominal_millihz()==14000000&&a.sync_limited());pcm(a,4096);}
 {M a(false);auto out=pcm(a,100);a.set_modern(true);q15 v;a.process(&v,1);REQUIRE(v==out.back());}
 // DC removal under asymmetric PWM settles, including after a dynamic edit.
 {M a;param(a,M::P_SOURCE,16384);param(a,M::P_WIDTH,3000);param(a,M::P_JITTER,0);param(a,M::P_LOAD,0);auto out=pcm(a,32000*3);int64_t sum=0;for(int i=64000;i<96000;++i)sum+=out[i];REQUIRE(std::abs(sum/32000)<300);}
}
static void persistence_tests(){
 auto e=std::make_unique<Engine>();auto* motor=new M;e->chains[0].units[0]=motor;
 for(int p=0;p<M::P_COUNT;++p){motor->params[p].base=q15(1000+p*2300);motor->params[p].src=ModSource::SigilE;motor->params[p].mod_depth=q15(p*71);motor->params[p].mod_enabled=p%2;motor->params[p].snap();}
 for(auto& scene:e->scenes[0]){scene.kind[0]=UnitKind::Motor;for(int p=0;p<M::P_COUNT;++p)scene.base[0][p]=q15(p*1234);}
 for(auto& bank:e->pattern_banks)for(int p=6;p<M::P_COUNT;++p){auto& step=bank.steps[0][p];step.lock_count=1;step.locks[0]={0,uint8_t(p),q15(p*1900)};}
 for(int p=6;p<M::P_COUNT;++p){auto& step=e->pattern.steps[0][p];step.lock_count=1;step.locks[0]={0,uint8_t(p),q15(p*1900)};}
 auto& pad=e->macro_pads[0];pad.route_count=1;pad.routes[0].chain=0;pad.routes[0].slot=0;pad.routes[0].param=M::P_SYNC;
 REQUIRE(patchio::save(*e,(directory+"/modern.snc").c_str()));
 auto bytes=read_bytes(directory+"/modern.snc");REQUIRE(bytes.size()>5&&bytes[3]=='8'&&bytes[4]==8);
 auto loaded=std::make_unique<Engine>();REQUIRE(patchio::load(*loaded,(directory+"/modern.snc").c_str()));
 auto* m=static_cast<M*>(loaded->chains[0].units[0]);REQUIRE(m&&m->modern()&&m->num_params==M::P_COUNT);
 for(int p=0;p<M::P_COUNT;++p){REQUIRE(m->params[p].base==motor->params[p].base);REQUIRE(m->params[p].src==motor->params[p].src);REQUIRE(m->params[p].mod_depth==motor->params[p].mod_depth);REQUIRE(m->params[p].mod_enabled==motor->params[p].mod_enabled);}
 for(const auto& bank:loaded->pattern_banks)for(int p=6;p<M::P_COUNT;++p)REQUIRE(bank.steps[0][p].lock_count==1);
 REQUIRE(loaded->macro_pads[0].route_count==1);
 for(const auto& scene:loaded->scenes[0])for(int p=6;p<M::P_COUNT;++p)REQUIRE(scene.base[0][p]==q15(p*1234));
 std::unique_ptr<Unit> cloned(clone_unit(m));REQUIRE(static_cast<M*>(cloned.get())->modern());
 for(int p=0;p<M::P_COUNT;++p)REQUIRE(cloned->params[p].base==m->params[p].base);
 // Old container with authored-looking padding never silently enables anything.
 bytes[3]='7';bytes[4]=7;save_bytes("poisoned-legacy.snc",bytes.data(),bytes.size());
 REQUIRE(patchio::load(*loaded,(directory+"/poisoned-legacy.snc").c_str()));m=static_cast<M*>(loaded->chains[0].units[0]);REQUIRE(!m->modern()&&m->num_params==6);
 for(int p=6;p<PARAM_BANK_SIZE;++p)REQUIRE(!m->params[p].base&&m->params[p].src==ModSource::None&&m->params[p].mod_enabled);
 for(const auto& bank:loaded->pattern_banks)for(int p=6;p<M::P_COUNT;++p)REQUIRE(bank.steps[0][p].lock_count==0);
 REQUIRE(loaded->macro_pads[0].route_count==0);
 for(const auto& scene:loaded->scenes[0])for(int p=6;p<M::P_COUNT;++p)REQUIRE(scene.base[0][p]==0);
 cloned.reset(clone_unit(m));REQUIRE(!static_cast<M*>(cloned.get())->modern()&&cloned->num_params==6);
 REQUIRE(patchio::save(*loaded,(directory+"/resaved-legacy.snc").c_str()));REQUIRE(read_bytes(directory+"/resaved-legacy.snc")[3]=='7');
 // Unknown modern flags must reject transactionally, not replace a live patch.
 bytes=read_bytes(directory+"/modern.snc");size_t at=5;
 while(at+8<bytes.size()){
  uint32_t size=0;for(int i=0;i<4;++i)size|=uint32_t(uint8_t(bytes[at+4+i]))<<(8*i);
  if(!std::memcmp(bytes.data()+at,"UNIT",4)){bytes[at+8]=2;break;}at+=8+size;
 }
 REQUIRE(at+8<bytes.size());save_bytes("bad-flags.snc",bytes.data(),bytes.size());
 Unit* previous=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/bad-flags.snc").c_str()));REQUIRE(loaded->chains[0].units[0]==previous);
 // Legacy load and resave retain the actual pre-edit document exactly.
 REQUIRE(patchio::load(*loaded,(directory+"/legacy.snc").c_str()));
 REQUIRE(patchio::save(*loaded,(directory+"/legacy-roundtrip.snc").c_str()));REQUIRE(read_bytes(directory+"/legacy.snc")==read_bytes(directory+"/legacy-roundtrip.snc"));
}
static void arp_tests(){
 M m(false);REQUIRE(!ArpUnit::accepts(&m));m.set_modern(true);REQUIRE(!ArpUnit::accepts(&m));param(m,M::P_MODE,32767);REQUIRE(ArpUnit::accepts(&m));
 param(m,M::P_NOTE,musical_note::encode(45));m.set_arp_pitch(12);REQUIRE(m.nominal_millihz()==220000);
 auto e=std::make_unique<Engine>();auto* a=new ArpUnit;auto* source=new M;
 e->chains[0].units[0]=a;e->chains[0].units[1]=source;param(*source,M::P_MODE,32767);
 for(int p=ArpUnit::P_NOTE1;p<=ArpUnit::P_NOTE4;++p){a->params[p].base=ArpUnit::note_value(p==ArpUnit::P_NOTE1?12:ArpUnit::NOTE_OFF);a->params[p].snap();}
 e->transport_playing=true;
 std::array<q15,256> out{};e->render(out.data(),128);REQUIRE(source->arp_voice.owner==0&&source->arp_voice.semitones==12);
 const q15 root=source->params[M::P_NOTE].base;REQUIRE(root==musical_note::encode(45));
 param(*source,M::P_MODE,0);e->render(out.data(),128);REQUIRE(source->arp_voice.owner<0&&source->arp_voice.semitones==0);REQUIRE(source->params[M::P_NOTE].base==root);
 param(*source,M::P_MODE,32767);e->render(out.data(),128);REQUIRE(source->arp_voice.owner==0);
 a->bypass=true;e->render(out.data(),128);REQUIRE(source->arp_voice.owner<0&&source->arp_voice.semitones==0);
}
static void extension_tests(){parameter_tests();persistence_tests();arp_tests();}
#endif
int main(int argc,char**argv){
 if(argc==3&&std::string(argv[1])=="--old-reader"){
  auto e=std::make_unique<Engine>();e->chains[0].units[0]=new M;Unit* previous=e->chains[0].units[0];
  REQUIRE(!patchio::load(*e,argv[2]));REQUIRE(e->chains[0].units[0]==previous);std::puts("old actual reader rejected extended MOTOR without mutation");return 0;
 }
 REQUIRE(argc==2);directory=argv[1];legacy_fixtures();
#ifndef MOTOR_BASELINE
 extension_tests();
#endif
 std::printf("motor focused: %zu checks passed\n",checks);
}
