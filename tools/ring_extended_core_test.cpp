#include "engine/engine.h"
#include "engine/units.h"
#include "engine/patch_io.h"
#include "engine/musical_note.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
using namespace seance;using R=RingUnit;
static size_t checks=0;static std::string directory;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"failed %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)
static void save(const std::string& name,const void* p,size_t n){std::ofstream f(directory+"/"+name,std::ios::binary);f.write(static_cast<const char*>(p),n);REQUIRE(bool(f));}
static std::vector<uint8_t> read(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void param(R& r,int p,int v){r.params[p].base=q15(v);r.params[p].snap();}
static std::vector<q15> input(int n){std::vector<q15> a(n);uint32_t rng=1234567;for(int i=0;i<n;++i){rng=rng*1664525u+1013904223u;a[i]=i%5==0?q15(int(rng>>16)-32768):q15((i%256-128)*192);}return a;}
static std::vector<q15> render(R& r,int n,bool split=false){auto a=input(n);for(int i=0;i<n;){int count=split?std::min(n-i,1+(i*7+11)%197):n;r.process(a.data()+i,count);i+=count;}return a;}
static void legacy(){
 for(int variant=0;variant<24;++variant){
#ifdef RING_BASELINE
  R r;
#else
  R r(false);
#endif
  const int values[]={19000,24576,0,0,24576,0};for(int p=0;p<6;++p)param(r,p,values[p]);
  if(variant){for(int p=0;p<6;++p)param(r,p,(variant*7919+p*1701)%32768);param(r,2,variant%2?-32767:32767);if(variant<13)param(r,(variant-1)/2,variant%2?0:32767);}
  auto a=render(r,8192,true);param(r,5,32767);param(r,2,-16000);auto tail=render(r,4096,true);a.insert(a.end(),tail.begin(),tail.end());r.reset();tail=render(r,4096,true);a.insert(a.end(),tail.begin(),tail.end());save("legacy-"+std::to_string(variant)+".pcm",a.data(),a.size()*2);
 }
 auto e=std::make_unique<Engine>();
#ifdef RING_BASELINE
 auto* r=new R;
#else
 auto* r=new R(false);
#endif
 e->chains[0].units[0]=r;param(*r,2,-12345);r->params[1].src=ModSource::SigilB;r->params[1].mod_depth=3333;
 REQUIRE(patchio::save(*e,(directory+"/legacy.snc").c_str()));
}
#ifndef RING_BASELINE
static size_t chunk(const std::vector<uint8_t>& b,const char* id){for(size_t at=5;at+8<=b.size();){uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(b[at+4+i])<<(8*i);if(!std::memcmp(b.data()+at,id,4))return at;at+=8+n;}REQUIRE(false);return 0;}
static void maps(){
 REQUIRE(R::P_COUNT==12&&R::LEGACY_COUNT==6&&PARAM_BANK_SIZE==12);
 REQUIRE(R::frequency_millihz(0)==500&&R::frequency_millihz(32767)==8189749);
 REQUIRE(R::follow_time_us(0)==1000&&R::follow_time_us(32767)==1000000);
 REQUIRE(R::drive_centidb(0)==0&&R::drive_centidb(32767)==1800);
 REQUIRE(R::fine_centicents(-32768)==-10000&&R::fine_centicents(32767)==10000&&R::fine_centicents(0)==0);
 REQUIRE(R::wave_index(0)==0&&R::wave_index(16384)==1&&R::wave_index(32767)==2);
 REQUIRE(R::note_millihz(musical_note::encode(69))==440000);
 for(int v=1;v<=32767;++v){REQUIRE(R::frequency_millihz(q15(v))>=R::frequency_millihz(q15(v-1)));REQUIRE(R::follow_time_us(q15(v))>=R::follow_time_us(q15(v-1)));REQUIRE(R::drive_centidb(q15(v))>=R::drive_centidb(q15(v-1)));REQUIRE(R::fine_centicents(q15(v))>=R::fine_centicents(q15(v-1)));}
 q15 v[12]={};v[R::P_MODE]=32767;v[R::P_NOTE]=musical_note::encode(69);REQUIRE(R::base_millihz(v)==440000);v[R::P_FINE]=32767;REQUIRE(std::abs(double(R::base_millihz(v))/440000.-std::exp2(1./12.))<.00001);
}
static void sound(){
 for(int dry_param:{R::P_MIX,R::P_DEPTH})for(int edge:{0,32767}){R r;for(int p=0;p<12;++p)param(r,p,edge);param(r,dry_param,0);r.reset();REQUIRE(render(r,8192,true)==input(8192));}
 for(int variant=0;variant<18;++variant){R a,b;for(R* r:{&a,&b}){for(int p=0;p<12;++p)param(*r,p,(variant*7919+p*1701)%32768);param(*r,R::P_FOLLOW,variant%2?-18000:21000);param(*r,R::P_FEEDBACK,variant%3?-20000:32767);r->reset();}auto one=render(a,16384),split=render(b,16384,true);REQUIRE(one==split);save("modern-"+std::to_string(variant)+".pcm",one.data(),one.size()*2);}
 for(int p=0;p<12;++p){R a,b;for(R* r:{&a,&b}){param(*r,R::P_FOLLOW,16000);param(*r,R::P_MODE,p==R::P_NOTE?32767:0);r->reset();}param(b,p,p==R::P_DEPTH||p==R::P_MIX?0:p==R::P_WAVE?32767:p==R::P_FOLLOW?-20000:30000);REQUIRE(render(a,8192)!=render(b,8192));}
 // Optional real carrier capture must not alter sound/state or block partitioning.
 {R a,b;param(a,R::P_WAVE,32767);param(b,R::P_WAVE,32767);auto x=input(4096),y=x;std::vector<q15> carrier(x.size());a.process(x.data(),int(x.size()));for(int i=0;i<4096;++i)b.process_with_carrier(&y[i],&carrier[i],1);REQUIRE(x==y);REQUIRE(render(a,4096)==render(b,4096));REQUIRE(*std::max_element(carrier.begin(),carrier.end())>20000);}
 for(int shape:{0,16384,32767}){R r;param(r,R::P_WAVE,shape);auto a=render(r,4096);r.reset();REQUIRE(render(r,4096)==a);}
 {R r;std::vector<q15> a(4096);r.process(a.data(),int(a.size()));REQUIRE(std::all_of(a.begin(),a.end(),[](q15 v){return v==0;}));}
 // Depth zero does not advance carrier or follower; mix zero does.
 {R r;param(r,R::P_DEPTH,0);std::vector<q15> a(1000,12000),c(a.size());r.reset();r.process_with_carrier(a.data(),c.data(),int(a.size()));REQUIRE(r.visual_phase()==0&&r.visual_envelope()==0);REQUIRE(std::all_of(c.begin(),c.end(),[](q15 v){return v==0;}));param(r,R::P_DEPTH,32767);param(r,R::P_MIX,0);r.process(a.data(),int(a.size()));REQUIRE(r.visual_phase()!=0&&r.visual_envelope()>0);}
 // Signed follower moves a known1Hz carrier in both directions, not pitch tracking.
 {int raw=0;while(R::frequency_millihz(q15(raw))<1000)++raw;uint16_t phases[3]={};int j=0;for(int follow:{-32767,0,32767}){R r;param(r,0,raw);param(r,5,follow);r.reset();std::vector<q15> a(320,16384);r.process(a.data(),int(a.size()));phases[j++]=r.visual_phase();}REQUIRE(phases[0]<phases[1]&&phases[1]<phases[2]);}
 // Envelope release has its advertised time constant in actual state, not a UI estimate.
 for(int t:{0,16000,32767}){R r;param(r,R::P_FOLLOW_TIME,t);r.reset();q15 loud=32767;r.process(&loud,1);int frames=int((uint64_t(R::follow_time_us(q15(t)))*ENGINE_SR+500000)/1000000);std::vector<q15> silence(frames);r.process(silence.data(),frames);double expected=32766.*std::exp(-double(frames)*1000000/(double(R::follow_time_us(q15(t)))*ENGINE_SR));REQUIRE(std::abs(double(r.visual_envelope())-expected)<45.);}
 // Shared slew stays unchanged; local semantic zero/endpoints settle exactly.
 for(int p:{R::P_DEPTH,R::P_MIX,R::P_FOLLOW,R::P_FINE,R::P_DRIVE}){R r;param(r,p,12000);r.params[p].resolve(0);r.params[p].base=0;for(int i=0;i<100;++i){r.params[p].resolve(0);r.control_tick();}REQUIRE(r.params[p].effective>0&&r.control_value(p)==0);}
 {R r;param(r,R::P_NOTE,musical_note::encode(45));r.params[R::P_NOTE].resolve(0);r.params[R::P_NOTE].base=musical_note::encode(57);r.params[R::P_NOTE].resolve(0);REQUIRE(r.control_value(R::P_NOTE)==r.params[R::P_NOTE].target);}
 {R r(false);auto out=render(r,1000);r.set_modern(true);q15 v=12000;r.process(&v,1);REQUIRE(v==out.back()&&r.num_params==12&&r.params[R::P_FOLLOW].bipolar);}
 // Many live changes exercise bounded feedback, discontinuous shapes and both follow polarities.
 {R r;std::array<q15,32> a{};for(int tick=0;tick<3000;++tick){for(int p=0;p<12;++p){param(r,p,(tick*173+p*7919)%32768);if((p==R::P_FOLLOW||p==R::P_FINE||p==R::P_FEEDBACK)&&tick%2)param(r,p,-r.params[p].base);}r.process(a.data(),int(a.size()));}}
}
static double amplitude(const std::vector<q15>& samples,double hz){double re=0,im=0;const size_t warm=1024;for(size_t i=warm;i<samples.size();++i){const double phase=6.283185307179586*hz*double(i)/ENGINE_SR;re+=samples[i]*std::cos(phase);im-=samples[i]*std::sin(phase);}return 2*std::sqrt(re*re+im*im)/double(samples.size()-warm)/32768.;}
static void spectral(){
 // A focused comparison, NOT an alias-free/whole-domain quality certificate.
 int raw=0;while(R::frequency_millihz(q15(raw))<6250000)++raw;
 R r;param(r,R::P_FREQ,raw);param(r,R::P_WAVE,32767);param(r,R::P_DEPTH,32767);param(r,R::P_MIX,32767);r.reset();std::vector<q15> data(33024),carrier(data.size()),naive(data.size());r.process_with_carrier(data.data(),carrier.data(),int(data.size()));
 const double hz=double(R::frequency_millihz(q15(raw)))/1000.,alias=32000.-3*hz;
 for(size_t i=0;i<naive.size();++i)naive[i]=std::fmod(hz*double(i)/32000.,1.)<.5?32767:-32767;
 const double modern=amplitude(carrier,alias),old=amplitude(naive,alias),fund=amplitude(carrier,hz);
 std::printf("pulse diagnostic %.3fhz: folded3rd %.6f vs naive %.6f; fundamental %.6f\n",hz,modern,old,fund);
 REQUIRE(fund>.6&&modern<old*.6);
 save("carrier.pcm",carrier.data(),carrier.size()*2);
}
static void engine_locks(){
 for(int group=0;group<4;++group){auto e=std::make_unique<Engine>();auto* r=new R;e->chains[0].units[0]=new OscUnit;e->chains[0].units[1]=r;
  q15 stored[12];for(int p=0;p<12;++p)stored[p]=r->params[p].base;
  const q15 lock[12]={21000,32767,-16000,24000,32767,-24000,20000,30000,32767,musical_note::encode(57),32767,-12000};
  e->pattern.steps[0][0].flags=SeqStep::Trig;e->set_sequence_length(0,1);
  for(int p=group*3;p<group*3+3;++p)REQUIRE(e->set_step_lock(0,0,1,p,lock[p]));
  e->set_sequencer_enabled(true);e->transport_reset();e->transport_play();std::array<q15,256> out{};
  for(int i=0;i<80;++i)e->render(out.data(),128);
  for(int p=group*3;p<group*3+3;++p){REQUIRE(r->params[p].step_lock_override&&r->control_value(p)==lock[p]);REQUIRE(r->params[p].base==stored[p]);}
  e->set_sequencer_enabled(false);e->transport_stop();for(int i=0;i<40;++i)e->render(out.data(),128);
  for(int p=0;p<12;++p)REQUIRE(!r->params[p].step_lock_override&&r->control_value(p)==stored[p]);
 }
}
static void persistence(){
 auto e=std::make_unique<Engine>();auto* r=new R;e->chains[0].units[0]=r;
 for(int p=0;p<12;++p){param(*r,p,p==2||p==5||p==11?-12345:1000+p*2200);r->params[p].src=ModSource::SigilG;r->params[p].mod_depth=q15(p*151);r->params[p].mod_enabled=p%2;}
 for(auto& scene:e->scenes[0]){scene.kind[0]=UnitKind::Ring;for(int p=0;p<12;++p)scene.base[0][p]=q15(p==5?-12345:p*1999);}
 for(auto& bank:e->pattern_banks)for(int p=0;p<12;++p){auto& s=bank.steps[0][p];s.lock_count=1;s.locks[0]={0,uint8_t(p),q15(p==5?-12345:p*1900)};}
 for(int p=0;p<12;++p){auto& s=e->pattern.steps[0][p];s.lock_count=1;s.locks[0]={0,uint8_t(p),q15(p==5?-12345:p*1900)};}
 e->macro_pads[0].route_count=1;e->macro_pads[0].routes[0].chain=0;e->macro_pads[0].routes[0].slot=0;e->macro_pads[0].routes[0].param=11;
 REQUIRE(patchio::save(*e,(directory+"/modern.snc").c_str()));auto bytes=read(directory+"/modern.snc");REQUIRE(bytes[3]=='C'&&bytes[4]==12&&bytes[chunk(bytes,"UNIT")+8]==1);
 auto loaded=std::make_unique<Engine>();REQUIRE(patchio::load(*loaded,(directory+"/modern.snc").c_str()));auto* got=static_cast<R*>(loaded->chains[0].units[0]);REQUIRE(got->modern()&&got->num_params==12&&got->params[5].bipolar&&got->params[11].bipolar);
 for(int p=0;p<12;++p){REQUIRE(got->params[p].base==r->params[p].base&&got->params[p].src==r->params[p].src&&got->params[p].mod_depth==r->params[p].mod_depth&&got->params[p].mod_enabled==r->params[p].mod_enabled);for(const auto& bank:loaded->pattern_banks)REQUIRE(bank.steps[0][p].lock_count==1);}
 REQUIRE(loaded->scenes[0][0].base[0][11]==21989&&loaded->macro_pads[0].route_count==1);
 std::unique_ptr<Unit> clone(clone_unit(got));REQUIRE(static_cast<R*>(clone.get())->modern());for(int p=0;p<12;++p)REQUIRE(clone->params[p].base==got->params[p].base);
 REQUIRE(patchio::save(*loaded,(directory+"/modern-roundtrip.snc").c_str()));REQUIRE(bytes==read(directory+"/modern-roundtrip.snc"));
 {auto v=bytes;size_t at=chunk(v,"UNIT"),n=8+NUM_CHAINS*UNITS_PER_CHAIN;std::vector<uint8_t> u(v.begin()+at,v.begin()+at+n);v.erase(v.begin()+at,v.begin()+at+n);v.insert(v.begin()+5,u.begin(),u.end());save("reordered.snc",v.data(),v.size());REQUIRE(patchio::load(*loaded,(directory+"/reordered.snc").c_str()));REQUIRE(static_cast<R*>(loaded->chains[0].units[0])->modern());}
 for(int version:{7,8,9,10,11}){auto v=bytes;v[3]=uint8_t(version<10?'0'+version:'A'+version-10);v[4]=uint8_t(version);v[chunk(v,"UNIT")+8]=0;save("old-padding.snc",v.data(),v.size());REQUIRE(patchio::load(*loaded,(directory+"/old-padding.snc").c_str()));got=static_cast<R*>(loaded->chains[0].units[0]);REQUIRE(!got->modern()&&got->num_params==6&&!got->params[5].bipolar);for(int p=6;p<12;++p){REQUIRE(got->params[p].base==0&&got->params[p].src==ModSource::None);for(const auto& bank:loaded->pattern_banks)REQUIRE(bank.steps[0][p].lock_count==0);for(const auto& scene:loaded->scenes[0])REQUIRE(scene.base[0][p]==0);}REQUIRE(loaded->macro_pads[0].route_count==0);clone.reset(clone_unit(got));REQUIRE(!static_cast<R*>(clone.get())->modern()&&clone->num_params==6);}
 for(int flags:{2,3,128}){auto bad=bytes;bad[chunk(bad,"UNIT")+8]=uint8_t(flags);save("bad.snc",bad.data(),bad.size());Unit* previous=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/bad.snc").c_str())&&loaded->chains[0].units[0]==previous);}
 {auto bad=bytes;bad[3]='B';bad[4]=11;save("bad-version.snc",bad.data(),bad.size());Unit* previous=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/bad-version.snc").c_str())&&loaded->chains[0].units[0]==previous);}
 REQUIRE(patchio::load(*loaded,(directory+"/legacy.snc").c_str()));REQUIRE(patchio::save(*loaded,(directory+"/legacy-roundtrip.snc").c_str()));REQUIRE(read(directory+"/legacy.snc")==read(directory+"/legacy-roundtrip.snc"));
 e->chains[0].units[1]=new StrainUnit;e->chains[0].units[2]=new BitUnit;e->chains[0].units[3]=new NoiseUnit;e->chains[0].units[4]=new MotorUnit;REQUIRE(patchio::save(*e,(directory+"/all-modern.snc").c_str()));REQUIRE(patchio::load(*loaded,(directory+"/all-modern.snc").c_str()));REQUIRE(static_cast<StrainUnit*>(loaded->chains[0].units[1])->modern()&&static_cast<BitUnit*>(loaded->chains[0].units[2])->modern()&&static_cast<NoiseUnit*>(loaded->chains[0].units[3])->modern()&&static_cast<MotorUnit*>(loaded->chains[0].units[4])->modern());
 auto neighbors=std::make_unique<Engine>();neighbors->chains[0].units[0]=new StrainUnit;neighbors->chains[0].units[1]=new R(false);REQUIRE(patchio::save(*neighbors,(directory+"/strain11.snc").c_str()));REQUIRE(read(directory+"/strain11.snc")[3]=='B');
}
static void modern(){maps();sound();spectral();engine_locks();persistence();}
#endif
int main(int argc,char** argv){if(argc==3&&std::string(argv[1])=="--old-reader"){auto e=std::make_unique<Engine>();e->chains[0].units[0]=new R;Unit* previous=e->chains[0].units[0];REQUIRE(!patchio::load(*e,argv[2])&&e->chains[0].units[0]==previous);std::puts("actual pre-edit reader rejects extended Ring without mutation");return 0;}REQUIRE(argc==2);directory=argv[1];legacy();
#ifndef RING_BASELINE
 modern();
#endif
 std::printf("ring focused: %zu checks passed\n",checks);
}
