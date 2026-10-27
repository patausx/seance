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
using namespace seance;using S=StrainUnit;
static size_t checks=0;static std::string directory;
#define REQUIRE(x) do{++checks;if(!(x)){std::fprintf(stderr,"failed %s:%d: %s\n",__FILE__,__LINE__,#x);std::abort();}}while(0)
static void save(const std::string& name,const void* p,size_t n){std::ofstream f(directory+"/"+name,std::ios::binary);f.write(static_cast<const char*>(p),n);REQUIRE(bool(f));}
static std::vector<uint8_t> read(const std::string& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void param(S& s,int p,int value){s.params[p].base=q15(value);s.params[p].snap();}
static std::vector<q15> input(int n,int kind=0){std::vector<q15> a(n);uint32_t rng=0xA578D9u;for(int i=0;i<n;++i){rng=rng*1664525u+1013904223u;if(kind==1)a[i]=0;else if(kind==2)a[i]=q15(24000*std::sin(6.283185307179586*i*250/32000));else a[i]=i%16000>=12000?0:i%7==0?q15(int(rng>>16)-32768):q15((i%256-128)*160);}return a;}
static std::vector<q15> render(S& s,int n,int split=0,int kind=0){auto a=input(n,kind);for(int block=0;block<n;block+=32){s.control_tick();const int end=std::min(n,block+32);for(int i=block;i<end;){const int count=std::min(end-i,split==1?1:split==2?1+(i*17)%19:32);s.process(a.data()+i,count);i+=count;}}return a;}
static void configure(S& s,int variant){const int defaults[]={15000,0,15000,12000,9000,24500};for(int p=0;p<6;++p)param(s,p,defaults[p]);if(variant){for(int p=0;p<6;++p)param(s,p,(variant*7919+p*1701)%32768);param(s,1,variant%2?-19660:19660);if(variant<7)param(s,variant-1,variant%2?0:32767);}s.reset();}
static void legacy(){
 for(int variant=0;variant<22;++variant){
#ifdef STRAIN_BASELINE
  S s;
#else
  S s(false);
#endif
  configure(s,variant);auto a=render(s,variant>=20?2080000:16384,2);save("legacy-"+std::to_string(variant)+".pcm",a.data(),a.size()*2);
 }
 auto e=std::make_unique<Engine>();
#ifdef STRAIN_BASELINE
 auto* s=new S;
#else
 auto* s=new S(false);
#endif
 e->chains[0].units[0]=s;param(*s,1,-12345);s->params[2].src=ModSource::SigilB;s->params[2].mod_depth=3333;
 REQUIRE(patchio::save(*e,(directory+"/legacy.snc").c_str()));
}
#ifndef STRAIN_BASELINE
static size_t chunk(const std::vector<uint8_t>& b,const char* id){for(size_t at=5;at+8<=b.size();){uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(b[at+4+i])<<(8*i);if(!std::memcmp(b.data()+at,id,4))return at;at+=8+n;}REQUIRE(false);return 0;}
static void maps(){
 REQUIRE(S::P_COUNT==9&&S::LEGACY_COUNT==6&&PARAM_BANK_SIZE==12);
 REQUIRE(S::rate_microhz(0)==10000&&S::rate_microhz(S::DEFAULT_RATE)==32000&&S::rate_microhz(32767)==8000000);
 REQUIRE(S::motion_increment(S::DEFAULT_RATE)==137439u);
 REQUIRE(S::trim_centidb(0)==-1800&&S::trim_centidb(S::DEFAULT_TRIM)==0&&S::trim_centidb(32767)==600);
 for(int i=1;i<=32767;++i){REQUIRE(S::rate_microhz(q15(i))>=S::rate_microhz(q15(i-1)));REQUIRE(S::trim_centidb(q15(i))>=S::trim_centidb(q15(i-1)));}
 for(int body:{0,15000,32767})for(int amount:{0,17000,32767})for(int energy:{0,16000,32767})for(uint64_t phase=0;phase<(1ull<<32);phase+=13743900u){const auto motion=S::model_motion(q15(body),q15(amount),uint32_t(phase),q15(energy));REQUIRE(motion>=-16384&&motion<=16384);if(!amount)REQUIRE(motion==0);}
}
static void sound(){
 // Default extensions retain old sound exactly, including two full slow cycles.
 for(int variant=0;variant<22;++variant){S a(false),b;configure(a,variant);configure(b,variant);const int n=variant>=20?2080000:16384;REQUIRE(render(a,n,2)==render(b,n,2));}
 for(int variant=0;variant<12;++variant){S a,b;for(S* s:{&a,&b}){configure(*s,variant);for(int p=6;p<9;++p)param(*s,p,(variant*7919+p*1739)%32768);s->reset();}auto one=render(a,32000),split=render(b,32000,2);REQUIRE(one==split);save("modern-"+std::to_string(variant)+".pcm",one.data(),one.size()*2);a.reset();REQUIRE(render(a,32000,1)==one);}
 for(int bias:{-32767,0,32767})for(int amount:{0,32767}){S s;for(int p=0;p<9;++p)param(s,p,32767);param(s,1,bias);param(s,S::P_MOTION,amount);s.reset();auto a=render(s,32000,0,1);REQUIRE(std::all_of(a.begin(),a.end(),[](q15 x){return x==0;}));}
 for(int p:{S::P_MOTION,S::P_TRIM,S::P_RATE}){S a,b;param(a,5,32767);param(b,5,32767);param(b,p,p==S::P_RATE?32767:0);a.reset();b.reset();REQUIRE(render(a,64000)!=render(b,64000));}
 // Trim cannot alter feedback or motion: undo trim after identical input histories.
 {S a,b;param(b,S::P_TRIM,0);a.reset();b.reset();auto aa=render(a,64000),bb=render(b,64000);REQUIRE(aa!=bb&&a.visual_motion()==b.visual_motion());param(b,S::P_TRIM,S::DEFAULT_TRIM);REQUIRE(render(a,32000)==render(b,32000));}
 // Mix0 and motion0 are real semantic endpoints even after ordinary integer slew.
 for(int p:{S::P_MIX,S::P_MOTION}){S s;param(s,p,12345);s.params[p].resolve(0);s.params[p].base=0;for(int t=0;t<120;++t){s.params[p].resolve(0);s.control_tick();}REQUIRE(s.control_value(p)==0);if(p==S::P_MIX)REQUIRE(render(s,8192)==input(8192));else{render(s,64000);REQUIRE(s.visual_motion()==0);}}
 {S a,b;param(a,S::P_MOTION,0);param(b,S::P_MOTION,0);param(b,S::P_RATE,32767);a.reset();b.reset();REQUIRE(render(a,64000)==render(b,64000));}
 {S a,b;param(b,S::P_MIX,0);a.reset();b.reset();render(a,64000);REQUIRE(render(b,64000)==input(64000));param(b,S::P_MIX,24500);REQUIRE(render(a,8192)==render(b,8192));}
 // Warm conversion is bounded and explicitly starts at prior output; no state reset click.
 {S s(false);auto a=render(s,1024);param(s,6,32767);param(s,7,S::DEFAULT_TRIM);param(s,8,S::DEFAULT_RATE);s.set_modern(true);s.control_tick();q15 x=12000;s.process(&x,1);REQUIRE(x==a.back());}
 // Shared exact model against silent real control ticks, including full 100s slow cycle.
 for(int raw:{0,S::DEFAULT_RATE,32767}){S s;param(s,S::P_RATE,raw);s.reset();uint32_t phase=S::motion_increment(q15(raw));for(int tick=0;tick<100001;++tick){REQUIRE(s.visual_motion()==S::model_motion(s.params[2].effective,32767,phase));s.control_tick();phase+=S::motion_increment(q15(raw));}}
 // Continuous extreme automation and long asymmetric excitation->silence; finite q15 by construction.
 {S s;std::array<q15,32> a;for(int tick=0;tick<4000;++tick){for(int p=0;p<9;++p){s.params[p].base=q15(p==1?((tick*137)%65535)-32767:(tick*317+p*7919)%32768);s.params[p].resolve(0);}s.control_tick();a.fill(tick%2?-32768:32767);s.process(a.data(),32);}}
 for(int bias:{-32767,32767}){S s;for(int p=0;p<9;++p)param(s,p,32767);param(s,1,bias);s.reset();render(s,320000,0,2);auto tail=render(s,2080000,0,1);long long sum=0;int peak=0;for(size_t i=tail.size()-32000;i<tail.size();++i){sum+=tail[i];peak=std::max(peak,std::abs(int(tail[i])));}std::printf("extreme bias %d tail peak=%d mean=%.4f q15 (diagnostic, not a zero-DC promise)\n",bias,peak,double(sum)/32000);}
}
static void engine_locks(){
 auto e=std::make_unique<Engine>();auto* s=new S;
 e->chains[0].units[0]=new OscUnit;e->chains[0].units[1]=s;
 const int stored[3]={17000,S::DEFAULT_TRIM,S::DEFAULT_RATE};
 for(int p=6;p<9;++p)param(*s,p,stored[p-6]);
 e->pattern.steps[0][0].flags=SeqStep::Trig;e->set_sequence_length(0,1);
 REQUIRE(e->set_step_lock(0,0,1,S::P_MOTION,0));
 REQUIRE(e->set_step_lock(0,0,1,S::P_TRIM,0));
 REQUIRE(e->set_step_lock(0,0,1,S::P_RATE,32767));
 e->set_sequencer_enabled(true);e->transport_reset();e->transport_play();
 std::array<q15,256> out{};for(int i=0;i<80;++i)e->render(out.data(),128);
 REQUIRE(s->params[S::P_MOTION].step_lock_override&&s->control_value(S::P_MOTION)==0&&s->visual_motion()==0);
 REQUIRE(s->control_value(S::P_TRIM)==0&&s->control_value(S::P_RATE)==32767);
 for(int p=6;p<9;++p)REQUIRE(s->params[p].base==stored[p-6]);
 e->set_sequencer_enabled(false);e->transport_stop();for(int i=0;i<40;++i)e->render(out.data(),128);
 for(int p=6;p<9;++p)REQUIRE(!s->params[p].step_lock_override&&s->control_value(p)==stored[p-6]);
}
static void persistence(){
 auto e=std::make_unique<Engine>();auto* s=new S;e->chains[0].units[0]=s;
 for(int p=0;p<9;++p){param(*s,p,p==1?-12345:1000+p*2200);s->params[p].src=ModSource::SigilG;s->params[p].mod_depth=q15(p*151);s->params[p].mod_enabled=p%2;}
 for(auto& scene:e->scenes[0]){scene.kind[0]=UnitKind::Strain;for(int p=0;p<12;++p)scene.base[0][p]=q15(p==1?-12345:p*1999);}
 for(auto& bank:e->pattern_banks)for(int p=0;p<9;++p){auto& st=bank.steps[0][p];st.lock_count=1;st.locks[0]={0,uint8_t(p),q15(p==1?-12345:p*1900)};}
 for(int p=0;p<9;++p){auto& st=e->pattern.steps[0][p];st.lock_count=1;st.locks[0]={0,uint8_t(p),q15(p==1?-12345:p*1900)};}
 e->macro_pads[0].route_count=1;e->macro_pads[0].routes[0].chain=0;e->macro_pads[0].routes[0].slot=0;e->macro_pads[0].routes[0].param=8;
 // Canonical scene padding is not authored state of this nine-control card.
 for(auto& scene:e->scenes[0])for(int p=9;p<12;++p)scene.base[0][p]=0;
 REQUIRE(patchio::save(*e,(directory+"/modern.snc").c_str()));auto bytes=read(directory+"/modern.snc");REQUIRE(bytes[3]=='B'&&bytes[4]==11&&bytes[chunk(bytes,"UNIT")+8]==1);
 auto loaded=std::make_unique<Engine>();REQUIRE(patchio::load(*loaded,(directory+"/modern.snc").c_str()));auto* got=static_cast<S*>(loaded->chains[0].units[0]);REQUIRE(got->modern()&&got->num_params==9&&got->params[1].bipolar);
 for(int p=0;p<9;++p){REQUIRE(got->params[p].base==s->params[p].base&&got->params[p].src==s->params[p].src&&got->params[p].mod_depth==s->params[p].mod_depth&&got->params[p].mod_enabled==s->params[p].mod_enabled);for(int b=0;b<NUM_PATTERNS;++b)REQUIRE(loaded->sequence_pattern(b).steps[0][p].lock_count==1);for(const auto& scene:loaded->scenes[0])REQUIRE(scene.base[0][p]==e->scenes[0][0].base[0][p]);}
 REQUIRE(loaded->macro_pads[0].route_count==1);
 std::unique_ptr<Unit> clone(clone_unit(got));REQUIRE(static_cast<S*>(clone.get())->modern());for(int p=0;p<9;++p)REQUIRE(clone->params[p].base==got->params[p].base);
 REQUIRE(patchio::save(*loaded,(directory+"/modern-roundtrip.snc").c_str()));REQUIRE(bytes==read(directory+"/modern-roundtrip.snc"));
 {auto v=bytes;size_t at=chunk(v,"UNIT"),n=8+NUM_CHAINS*UNITS_PER_CHAIN;std::vector<uint8_t> u(v.begin()+at,v.begin()+at+n);v.erase(v.begin()+at,v.begin()+at+n);v.insert(v.begin()+5,u.begin(),u.end());save("reordered.snc",v.data(),v.size());REQUIRE(patchio::load(*loaded,(directory+"/reordered.snc").c_str()));REQUIRE(static_cast<S*>(loaded->chains[0].units[0])->modern());}
 for(int version:{7,8,9,10}){auto v=bytes;v[3]=uint8_t(version==10?'A':'0'+version);v[4]=uint8_t(version);v[chunk(v,"UNIT")+8]=0;save("old-padding.snc",v.data(),v.size());REQUIRE(patchio::load(*loaded,(directory+"/old-padding.snc").c_str()));got=static_cast<S*>(loaded->chains[0].units[0]);REQUIRE(!got->modern()&&got->num_params==6);for(int p=6;p<12;++p){REQUIRE(got->params[p].base==0&&got->params[p].src==ModSource::None);for(int b=0;b<NUM_PATTERNS;++b)if(p<9)REQUIRE(loaded->sequence_pattern(b).steps[0][p].lock_count==0);for(const auto& scene:loaded->scenes[0])REQUIRE(scene.base[0][p]==0);}REQUIRE(loaded->macro_pads[0].route_count==0);clone.reset(clone_unit(got));REQUIRE(!static_cast<S*>(clone.get())->modern()&&clone->num_params==6);
  v[chunk(v,"UNIT")+8]=1;save("bad-version.snc",v.data(),v.size());Unit* prev=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/bad-version.snc").c_str())&&loaded->chains[0].units[0]==prev);
 }
 for(int flags:{2,3,128}){auto v=bytes;v[chunk(v,"UNIT")+8]=uint8_t(flags);save("bad.snc",v.data(),v.size());Unit* prev=loaded->chains[0].units[0];REQUIRE(!patchio::load(*loaded,(directory+"/bad.snc").c_str())&&loaded->chains[0].units[0]==prev);}
 REQUIRE(patchio::load(*loaded,(directory+"/legacy.snc").c_str()));REQUIRE(patchio::save(*loaded,(directory+"/legacy-roundtrip.snc").c_str()));REQUIRE(read(directory+"/legacy.snc")==read(directory+"/legacy-roundtrip.snc"));
 e->chains[0].units[1]=new BitUnit;e->chains[0].units[2]=new NoiseUnit;e->chains[0].units[3]=new MotorUnit;REQUIRE(patchio::save(*e,(directory+"/all-modern.snc").c_str()));REQUIRE(patchio::load(*loaded,(directory+"/all-modern.snc").c_str()));REQUIRE(static_cast<BitUnit*>(loaded->chains[0].units[1])->modern()&&static_cast<NoiseUnit*>(loaded->chains[0].units[2])->modern()&&static_cast<MotorUnit*>(loaded->chains[0].units[3])->modern());
 delete e->chains[0].units[0];e->chains[0].units[0]=new S(false);REQUIRE(patchio::save(*e,(directory+"/without-strain-modern.snc").c_str()));REQUIRE(read(directory+"/without-strain-modern.snc")[3]=='A');
}
#endif
int main(int argc,char** argv){if(argc==3&&std::string(argv[1])=="--old-reader"){auto e=std::make_unique<Engine>();e->chains[0].units[0]=new S;Unit* previous=e->chains[0].units[0];REQUIRE(!patchio::load(*e,argv[2])&&e->chains[0].units[0]==previous);std::puts("actual pre-edit reader rejects extended Strain without mutation");return 0;}REQUIRE(argc==2);directory=argv[1];legacy();
#ifndef STRAIN_BASELINE
 maps();sound();engine_locks();persistence();
#endif
 std::printf("strain focused: %zu checks passed\n",checks);
}
