#include "engine/engine.h"
#include "engine/units.h"
#include "engine/reverb_dsp.h"
#include "engine/patch_io.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <new>
// Failure injection is narrowly scoped to the new fixed REV state size range.
// Allocate through the normal C++ allocator so ASAN allocation families match.
static bool fail_rev_state = false;
static size_t rev_state_bytes = 0;
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    if (n > 48u*1024u && n <= 52u*1024u) {
        rev_state_bytes = n;
        if (fail_rev_state) return nullptr;
    }
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
using namespace seance;
namespace {
using R=ReverbUnit; using Bytes=std::vector<uint8_t>;
uint64_t checks=0; std::string directory;
#define CHECK(x) do { ++checks; if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);} } while(0)
void write_bytes(const std::string& n,const void* d,size_t z){std::ofstream f(directory+"/"+n,std::ios::binary);f.write((const char*)d,(std::streamsize)z);CHECK(f.good());}
Bytes read_bytes(const std::string& n){std::ifstream f(directory+"/"+n,std::ios::binary);CHECK(f.good());return {std::istreambuf_iterator<char>(f),{}};}
void w16(Bytes& b,size_t at,int n){CHECK(at+2<=b.size());b[at]=(uint8_t)n;b[at+1]=(uint8_t)((uint16_t)n>>8);}
uint32_t u32(const Bytes& b,size_t at){CHECK(at+4<=b.size());return b[at]|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24;}
size_t chunk(const Bytes& b,const char* id){for(size_t at=5;at+8<=b.size();at+=8+u32(b,at+4))if(!std::memcmp(b.data()+at,id,4))return at+8;CHECK(false);return 0;}
uint32_t rng(uint32_t& s){s^=s<<13;s^=s>>17;s^=s<<5;return s;}
std::vector<q15> input(size_t n,uint32_t seed=0x891023u){std::vector<q15>a(n);for(size_t i=0;i<n;++i)a[i]=(q15)((int)(rng(seed)&65535u)-32768);return a;}
void set(R& u,int p,int value){u.params[p].base=(q15)value;u.params[p].snap();}
void store_pcm(const std::string& name,const std::vector<q15>& p){Bytes b(p.size()*2);for(size_t i=0;i<p.size();++i)w16(b,i*2,p[i]);write_bytes(name,b.data(),b.size());}
std::vector<q15> render_reset(R& u){u.reset();for(int p=0;p<u.num_params;++p)u.params[p].snap();auto a=input(24000);u.process(a.data(),317);u.process(a.data()+317,(int)a.size()-317);return a;}
void frozen_direct(){
 for(bool modern:{false,true}){
  R u(modern);CHECK(u.ready());std::vector<q15>total;
  auto append=[&](int n,int signal){auto a=input(n,(uint32_t)(n+signal+127));if(signal==1)std::fill(a.begin(),a.end(),0);if(signal==2)for(int i=0;i<n;++i)a[i]=i%2?-32768:32767;u.process(a.data(),37);u.process(a.data()+37,n-37);total.insert(total.end(),a.begin(),a.end());};
  append(5000,0);
  for(int size:{0,18000,32767})for(int decay:{0,20000,32767})for(int damp:{0,9000,32767})for(int mix:{0,9000,32767}){
   set(u,0,size);set(u,1,decay);set(u,2,mix);set(u,3,damp);append(1024,2);
  }
  set(u,4,32767);append(11000,0);set(u,0,0);set(u,1,0);set(u,3,32767);append(5000,1);
  set(u,4,0);append(7000,0);set(u,0,32767);append(37+211,0);set(u,4,32767);append(8000,1);
  set(u,2,0);append(3000,0);set(u,2,32767);append(7000,1);set(u,4,0);append(10000,1);
  store_pcm(modern?"neutral-dense.pcm":"neutral-legacy.pcm",total);
 }
 std::puts("ok frozen direct: defaults/extrema/size transitions/freeze-during-size/held edit/mix dry/resume/legacy+dense");
}
void legacy_fixtures(){
 for(int version=1;version<=6;++version){
  const std::string prefix="snc"+std::to_string(version);Bytes b={'S','N','C',(uint8_t)('0'+version),(uint8_t)version};
  for(int c=0;c<NUM_CHAINS;++c){b.insert(b.end(),{255,127});if(version>=2)b.insert(b.end(),{0,0});b.push_back(0);size_t first=b.size();b.insert(b.end(),UNITS_PER_CHAIN*(3+6*5),0);
   if(!c){b[first]=(uint8_t)UnitKind::Reverb;int raw[]={23001,29000,19000,11000,0,31001};for(int p=0;p<6;++p){size_t at=first+3+p*5;w16(b,at,raw[p]);b[at+2]=(uint8_t)(p%4+1);w16(b,at+3,1071+p*197);}}
  }
  b.insert(b.end(),4*(1+SIGIL_POINTS*2+2+2)+4*3,0);if(version>=3){b.insert(b.end(),{137,0});b.insert(b.end(),4*3,0);}if(version>=4)b.insert(b.end(),NUM_CHAINS*UNITS_PER_CHAIN,0);if(version>=5){b.insert(b.end(),patchio::SampleManifest::SLOT_COUNT*4,0);b.insert(b.end(),{0,4,0});}if(version>=6)b.insert(b.end(),NUM_CHAINS*UNITS_PER_CHAIN*11,0);
  write_bytes(prefix+"-old.snc",b.data(),b.size());auto e=std::make_unique<Engine>();CHECK(patchio::load(*e,(directory+"/"+prefix+"-old.snc").c_str()));auto* u=static_cast<R*>(e->chains[0].units[0]);CHECK(u&&!u->modern());store_pcm(prefix+".pcm",render_reset(*u));CHECK(patchio::save(*e,(directory+"/"+prefix+"-roundtrip.snc").c_str()));
#ifndef REVERB_FIELD_BASELINE_ONLY
  for(int p=5;p<8;++p)CHECK(u->params[p].base==0&&u->params[p].src==ModSource::None&&u->params[p].mod_depth==0&&u->params[p].mod_enabled);
#endif
 }
 for(bool modern:{false,true}){
  std::string prefix=modern?"snc7-dense":"snc7-legacy";auto e=std::make_unique<Engine>();auto*u=new R(modern);delete e->replace_unit(0,0,u);set(*u,0,21713);set(*u,1,29777);set(*u,2,19033);set(*u,3,7011);
  e->capture_scene(0,0);set(*u,0,15713);e->capture_scene(0,1);set(*u,0,21713);
  for(int bank=0;bank<NUM_PATTERNS;++bank){auto& step=e->sequence_pattern(bank).steps[0][3];step.lock_count=1;step.locks[0]={0,1,(q15)(17001+bank*200)};}
  CHECK(patchio::save(*e,(directory+"/"+prefix+"-old.snc").c_str()));auto clean=read_bytes(prefix+"-old.snc");auto loaded=std::make_unique<Engine>();CHECK(patchio::load(*loaded,(directory+"/"+prefix+"-old.snc").c_str()));CHECK(patchio::save(*loaded,(directory+"/"+prefix+"-roundtrip.snc").c_str()));CHECK(clean==read_bytes(prefix+"-roundtrip.snc"));store_pcm(prefix+".pcm",render_reset(*static_cast<R*>(loaded->chains[0].units[0])));
  // Noncanonical old CORE padding must stay inaudible, including muted cable bits.
  auto poisoned=clean;const size_t base=chunk(poisoned,"CORE")+5+3;for(int p=5;p<8;++p){w16(poisoned,base+p*5,28000+p);poisoned[base+p*5+2]=3;w16(poisoned,base+p*5+3,27000);}
  const size_t masks=chunk(poisoned,"USIG")+5+8*274;w16(poisoned,masks,0xe0);
  write_bytes(prefix+"-poisoned.snc",poisoned.data(),poisoned.size());CHECK(patchio::load(*loaded,(directory+"/"+prefix+"-poisoned.snc").c_str()));store_pcm(prefix+"-poisoned.pcm",render_reset(*static_cast<R*>(loaded->chains[0].units[0])));
#ifndef REVERB_FIELD_BASELINE_ONLY
  auto* got=static_cast<R*>(loaded->chains[0].units[0]);for(int p=5;p<8;++p)CHECK(got->params[p].base==0&&got->params[p].src==ModSource::None&&got->params[p].mod_depth==0&&got->params[p].mod_enabled);
#endif
 }
 std::puts("ok synthetic SNC1..7 legacy/current-dense PCM+neutral byteexact resave and poisoned dormant CORE/mute");
}
#ifndef REVERB_FIELD_BASELINE_ONLY
namespace dsp=seance::reverb_dsp;
void mappings() {
 static_assert(R::P_SIZE==0&&R::P_DECAY==1&&R::P_MIX==2&&R::P_DAMP==3&&R::P_FREEZE==4&&R::P_PREDELAY==5&&R::P_LOW_CUT==6&&R::P_DUCK==7&&R::P_COUNT==8);
 R modern;R legacy(false);CHECK(modern.num_params==8&&legacy.num_params==5);
 CHECK(modern.params[5].base==0&&modern.params[6].base==0&&modern.params[7].base==0);
 CHECK(!modern.visual_ready()&&modern.visual_duck_gain()==32767);
 uint32_t pre=0,hz=0,pct=0;
 for(int raw=-32768;raw<=32767;++raw){
  const int u=std::max(0,raw);CHECK(dsp::predelay_samples((q15)raw)==(uint32_t)std::floor(4800.0*u/32767.0+.5));
  CHECK(dsp::predelay_ms((q15)raw)==(uint32_t)std::floor(150.0*u/32767.0+.5));
  CHECK(dsp::duck_percent((q15)raw)==(uint32_t)std::floor(100.0*u/32767.0+.5));
  const uint32_t f=u?20u+(uint32_t)std::floor(980.0*u*u/(32767.0*32767.0)+.5):0u;
  CHECK(dsp::low_cut_hz((q15)raw)==f);CHECK(f>=hz&&f<=1000);hz=f;
  CHECK(dsp::predelay_samples((q15)raw)>=pre);pre=dsp::predelay_samples((q15)raw);
  CHECK(dsp::duck_percent((q15)raw)>=pct);pct=dsp::duck_percent((q15)raw);
  double expected=(32000.0-3.1415929203539825*f)/(32000.0+3.1415929203539825*f);
  CHECK(std::abs((double)dsp::low_cut_pole((q15)raw)/dsp::ONE-expected)<.000002);
 }
 int prev=dsp::ONE;
 for(int x=0;x<=4*dsp::ONE;x+=17){const int g=dsp::duck_gain(x);CHECK(g<=prev&&g>0&&g<=dsp::ONE);prev=g;CHECK(std::abs((double)g/dsp::ONE-1.0/(1.0+(double)x/dsp::ONE))<.001);}
 std::printf("ok complete raw utility mappings/reciprocal monotonicity+analytic error; fixed state %zu bytes\n",rev_state_bytes);
}
std::vector<q15> impulse(int pre,int cut=0){R u;set(u,0,0);set(u,1,0);set(u,2,32767);set(u,3,0);set(u,5,pre);set(u,6,cut);std::vector<q15>a(20000);a[0]=32767;u.process(a.data(),(int)a.size());return a;}
void predelay() {
 auto original=impulse(0);
 CHECK(std::find_if(original.begin(),original.end(),[](q15 x){return x!=0;})-original.begin()==443);
 for(int raw:{0,1,177,16000,32766,32767}){
  auto delayed=impulse(raw);const size_t n=(size_t)std::floor(4800.0*raw/32767.0+.5);
  for(size_t i=0;i<delayed.size();++i)CHECK(delayed[i]==(i<n?0:original[i-n]));
 }
 // Drive rapid target changes through a separate committed-target scheduler.
 // Initial controls snap; later taps finish their 640-sample handoff before
 // accepting the latest pending edit (never restart from an intermediate tap).
 R tested;set(tested,2,32767);uint32_t seed=117;
 int a=0,b=0,pos=0,pending=0,accepted=0;bool changing=false;
 std::vector<q15>total;
 for(int i=0;i<23000;++i){
  if(i%157==0){pending=(int)(rng(seed)%32768);set(tested,5,pending);}
  if(i==0){accepted=pending;a=(int)std::floor(4800.0*pending/32767.0+.5);}
  else if(!changing&&pending!=accepted){
   int next=(int)std::floor(4800.0*pending/32767.0+.5);accepted=pending;
   if(next!=a){b=next;changing=true;pos=0;}
  }
  if(changing&&++pos==640){a=b;changing=false;}
  q15 out=(q15)((int)(rng(seed)%24001)-12000);tested.process(&out,1);total.push_back(out);
  CHECK(tested.visual_effective(5)==accepted);
 }
 // A new impulse after a settled delay edit must equal a separately reset
 // endpoint impulse, not simply report the requested caption correctly.
 for(int raw:{0,177,16000,32767}){
  R changed;set(changed,0,0);set(changed,1,0);set(changed,2,32767);set(changed,3,0);
  set(changed,5,raw?0:32767);std::vector<q15>zero(1000);changed.process(zero.data(),1000);
  set(changed,5,raw);changed.process(zero.data(),640);CHECK(changed.visual_effective(5)==raw);
  std::vector<q15>hit(20000);hit[0]=32767;changed.process(hit.data(),(int)hit.size());CHECK(hit==impulse(raw));
 }
 store_pcm("predelay-changing.pcm",total);
 std::puts("ok predelay impulse shift exact at six raw endpoints, independent accepted-head scheduler under rapid changes, settled-edit impulse exact");
}
double energy(const std::vector<q15>&a,size_t first=0){double sum=0;for(size_t i=first;i<a.size();++i)sum+=(double)a[i]*a[i];return sum;}
std::vector<q15> tone(int hz,int samples){std::vector<q15>a(samples);for(int i=0;i<samples;++i)a[i]=(q15)std::lround(22000*std::sin(6.2831853071795864769*hz*i/32000.0));return a;}
void lowcut_duck() {
 for(int hz:{40,200,4000}){R off,on;for(auto*u:{&off,&on}){set(*u,2,32767);set(*u,1,0);set(*u,3,0);}set(on,6,32767);auto a=tone(hz,96000),b=a;off.process(a.data(),(int)a.size());on.process(b.data(),(int)b.size());const double ratio=energy(b,48000)/energy(a,48000);if(hz==40)CHECK(ratio<.01);if(hz==200)CHECK(ratio<.08);if(hz==4000)CHECK(ratio>.8&&ratio<1.1);std::printf("  lowcut %dHz energy ratio %.6f\n",hz,ratio);}
 {R u;set(u,2,32767);set(u,1,0);set(u,6,32767);std::vector<q15>a(160000,20000);u.process(a.data(),(int)a.size());CHECK(energy(a,128000)==0);}
 R off,on;set(off,2,32767);set(on,2,32767);set(on,7,32767);auto a=tone(1200,48000),b=a;off.process(a.data(),(int)a.size());on.process(b.data(),(int)b.size());CHECK(energy(b,16000)<energy(a,16000)*.3);CHECK(on.visual_duck_gain()<14000&&on.visual_duck_gain()>6000);for(int lane=0;lane<4;++lane)CHECK(off.visual_return(lane)==on.visual_return(lane));CHECK(off.visual_energy()==on.visual_energy());
 int gain=on.visual_duck_gain();std::vector<q15>silence(100000);on.process(silence.data(),(int)silence.size());CHECK(on.visual_duck_gain()>gain&&on.visual_duck_gain()==32767);
 for(bool modern:{false,true}){R dry(modern);set(dry,2,0);for(int p=5;p<8;++p)set(dry,p,32767);auto x=input(30000),copy=x;dry.process(x.data(),(int)x.size());if(modern)CHECK(x==copy);else {R reference(false);set(reference,2,0);reference.process(copy.data(),(int)copy.size());CHECK(x==copy);} }
 std::puts("ok send lowcut frequency/DC, wet-only duck suppression/recovery/raw-return independence, unchanged dry/legacy");
}
void freeze_telemetry_split() {
 R a,b;for(auto*u:{&a,&b}){set(*u,2,32767);set(*u,5,32767);set(*u,6,17000);set(*u,7,32767);}auto warm=input(27000),warm2=warm;a.process(warm.data(),(int)warm.size());b.process(warm2.data(),(int)warm2.size());CHECK(warm==warm2);for(int lane=0;lane<4;++lane)CHECK(a.visual_return(lane)>0);
 for(auto*u:{&a,&b})set(*u,4,32767);
 std::vector<q15>settle(64);auto settle2=settle;a.process(settle.data(),64);b.process(settle2.data(),64);CHECK(a.visual_freeze()==32767&&a.visual_duck_gain()==32767);
 const int accepted[]={a.visual_effective(0),a.visual_effective(1),a.visual_effective(3)};
 for(auto*u:{&a,&b}){set(*u,0,0);set(*u,1,0);set(*u,3,32767);}
 auto loud=input(512),quiet=std::vector<q15>(512);a.process(loud.data(),512);b.process(quiet.data(),512);CHECK(loud==quiet);CHECK(a.visual_effective(0)==accepted[0]&&a.visual_effective(1)==accepted[1]&&a.visual_effective(3)==accepted[2]);CHECK(a.visual_duck_gain()==32767);
 // Release before the maximum predelay drains: frozen-period input must not be
 // in send/filter/diffuser/detector history and emerge 150ms later.
 for(auto*u:{&a,&b})set(*u,4,0);
 loud.assign(16000,0);quiet=loud;a.process(loud.data(),16000);b.process(quiet.data(),16000);CHECK(loud==quiet);
 a.reset();b.reset();for(auto*u:{&a,&b}){set(*u,0,18000);set(*u,1,24000);set(*u,3,10000);}
 uint32_t seed=3171;
 for(int event=0;event<32;++event){for(int p:{0,1,2,3,4,5,6,7}){int raw=(int)(rng(seed)%32768);set(a,p,raw);set(b,p,raw);}auto whole=input(3401,(uint32_t)event+17),split=whole;a.process(whole.data(),3401);int at=0;while(at<3401){int n=std::min(3401-at,1+(int)(rng(seed)%71));b.process(split.data()+at,n);at+=n;
   q15 z=17171;const auto e=b.visual_energy();b.process(nullptr,10);b.process(&z,0);b.process(&z,-1);CHECK(z==17171&&b.visual_energy()==e);
  }CHECK(whole==split);for(int p=0;p<8;++p)CHECK(a.visual_effective(p)==b.visual_effective(p));for(int lane=0;lane<4;++lane)CHECK(a.visual_return(lane)==b.visual_return(lane)&&a.visual_return(lane)>=0);CHECK(a.visual_duck_gain()==b.visual_duck_gain());CHECK(a.visual_size()==b.visual_size()&&a.visual_freeze()==b.visual_freeze());}
 a.reset();CHECK(!a.visual_ready()&&a.visual_duck_gain()==32767&&a.visual_energy()==0);for(int lane=0;lane<4;++lane)CHECK(a.visual_return(lane)==0);for(int p=0;p<8;++p)CHECK(a.visual_effective(p)==0);
 std::vector<q15>zero(4000);a.process(zero.data(),(int)zero.size());CHECK(a.visual_ready());for(int lane=0;lane<4;++lane)CHECK(a.visual_return(lane)==0);
 R legacy(false);auto x=input(3000);legacy.process(x.data(),3000);CHECK(legacy.visual_ready());for(int lane=0;lane<4;++lane)CHECK(legacy.visual_return(lane)==0);for(int p=5;p<8;++p)CHECK(legacy.visual_effective(p)==0);
 std::puts("ok unity duck full freeze/captured parameters/frozen-input non-return after short hold; arbitrary block partitions, telemetry activity/reset/invalid calls");
}
Bytes saved(Engine&e,const std::string& name){CHECK(patchio::save(e,(directory+"/"+name).c_str()));return read_bytes(name);}
bool loaded(Engine&e,const Bytes&b,const std::string&name="load-case.snc"){write_bytes(name,b.data(),b.size());return patchio::load(e,(directory+"/"+name).c_str());}
Bytes reorder(const Bytes&b){std::vector<Bytes>chunks;for(size_t at=5;at<b.size();){size_t n=8+u32(b,at+4);CHECK(at+n<=b.size());chunks.emplace_back(b.begin()+at,b.begin()+at+n);at+=n;}Bytes out(b.begin(),b.begin()+5);for(auto it=chunks.rbegin();it!=chunks.rend();++it)out.insert(out.end(),it->begin(),it->end());return out;}
void persistence() {
 // Every kind of utility authorship can exist with zero current base. Test all
 // three utility indices individually, and every inactive pattern bank.
 for(int p=5;p<8;++p)for(int mode=0;mode<15;++mode){auto e=std::make_unique<Engine>();auto*u=new R;delete e->replace_unit(0,0,u);
  if(mode==0)set(*u,p,17777);
  if(mode==1)u->params[p].src=ModSource::SigilA;
  if(mode==2)u->params[p].mod_depth=-17777;
  if(mode==3)u->params[p].mod_enabled=false;
  if(mode==4){set(*u,p,17777);e->capture_scene(0,1);set(*u,p,0);}if(mode==5){e->macro_pads[0].route_count=1;e->macro_pads[0].routes[0]={0,0,(uint8_t)p,0,17777};}
  if(mode>=6){auto& pattern=mode==14?e->pattern:e->sequence_pattern(mode-6);auto&step=pattern.steps[0][0];step.lock_count=1;step.locks[0]={0,(uint8_t)p,0};}
  auto bytes=saved(*e,"author-"+std::to_string(p)+"-"+std::to_string(mode)+".snc");CHECK(bytes[chunk(bytes,"UNIT")]==3);auto l=std::make_unique<Engine>();CHECK(loaded(*l,reorder(bytes)));auto*got=static_cast<R*>(l->chains[0].units[0]);CHECK(got->modern()&&got->num_params==8&&!got->visual_ready());CHECK(saved(*l,"author-roundtrip.snc")==bytes);
 }
 auto e=std::make_unique<Engine>();auto*u=new R;delete e->replace_unit(0,0,u);
 for(int p=0;p<8;++p){set(*u,p,p==4?0:17000+p*701);u->params[p].src=ModSource::SigilA;u->params[p].mod_depth=(q15)(-12000+p*991);u->params[p].mod_enabled=p%2==0;}
 e->capture_scene(0,0);for(int p=5;p<8;++p)set(*u,p,30000-p);e->capture_scene(0,1);
 for(int bank=0;bank<8;++bank)for(int p=1;p<8;++p){auto&step=e->sequence_pattern(bank).steps[0][p];step.lock_count=1;step.locks[0]={0,(uint8_t)p,(q15)(19000+bank*100+p)};}
 e->macro_pads[0].route_count=2;e->macro_pads[0].routes[0]={0,0,1,0,20000};e->macro_pads[0].routes[1]={0,0,7,0,23000};
 auto bytes=saved(*e,"enhanced.snc");CHECK(bytes[chunk(bytes,"UNIT")]==3);auto pcm=input(15000);u->process(pcm.data(),15000);CHECK(saved(*e,"enhanced-after-audio.snc")==bytes);
 std::unique_ptr<Unit> copy(clone_unit(u));auto* clone=static_cast<R*>(copy.get());CHECK(clone&&clone->modern()&&clone->num_params==8&&!clone->visual_ready());for(int p=0;p<8;++p)CHECK(clone->params[p].base==u->params[p].base&&clone->params[p].src==u->params[p].src&&clone->params[p].mod_enabled==u->params[p].mod_enabled);CHECK(render_reset(*clone)==render_reset(*u));
 auto target=std::make_unique<Engine>();CHECK(loaded(*target,bytes));auto* got=static_cast<R*>(target->chains[0].units[0]);CHECK(!got->visual_ready());CHECK(render_reset(*got)==render_reset(*u));
 // Strip the marker from a fully authored payload: emulate old noncanonical
 // files, with ALL chunks after UNIT. New slots and every reference must vanish.
 for(int flags:{0,1})for(int order=0;order<2;++order){auto old=bytes;old[chunk(old,"UNIT")]=(uint8_t)flags;if(order)old=reorder(old);CHECK(loaded(*target,old));got=static_cast<R*>(target->chains[0].units[0]);CHECK(got->modern()==bool(flags));for(int p=5;p<8;++p){CHECK(got->params[p].base==0&&got->params[p].src==ModSource::None&&got->params[p].mod_depth==0&&got->params[p].mod_enabled);for(int which=0;which<2;++which)CHECK(target->scenes[0][which].base[0][p]==0);for(int bank=0;bank<8;++bank)CHECK(target->sequence_pattern(bank).steps[0][p].lock_count==0);}CHECK(target->macro_pads[0].route_count==1&&target->macro_pads[0].routes[0].param==1);for(int bank=0;bank<8;++bank)CHECK(target->sequence_pattern(bank).steps[0][1].lock_count==1);CHECK(got->params[1].base==u->params[1].base);auto clean=saved(*target,"canonical.snc");CHECK(clean[chunk(clean,"UNIT")]==flags);}
 // Transactional rejection: original live pointer, all persistent bytes and
 // processed tail stay untouched for unknown flags, legacy+marker, duplicates,
 // malformed lengths and state-allocation failure.
 CHECK(loaded(*target,bytes));got=static_cast<R*>(target->chains[0].units[0]);auto before=saved(*target,"transaction-before.snc");auto prime=input(7000);got->process(prime.data(),7000);const auto meter=got->visual_energy();
 auto reject=[&](const Bytes&bad){CHECK(!loaded(*target,bad));CHECK(target->chains[0].units[0]==got&&got->visual_energy()==meter);CHECK(saved(*target,"transaction-after.snc")==before);};
 for(int flag:{2,4,5,7,128,255}){auto bad=bytes;bad[chunk(bad,"UNIT")]=(uint8_t)flag;reject(bad);}
 {auto bad=bytes;const size_t at=chunk(bytes,"UNIT")-8;bad.insert(bad.end(),bytes.begin()+at,bytes.begin()+at+8+NUM_CHAINS*UNITS_PER_CHAIN);reject(bad);}
 {auto bad=bytes;const size_t at=chunk(bytes,"UNIT")-4;bad[at]--;reject(bad);}
 fail_rev_state=true;reject(bytes);fail_rev_state=false;
 R legacy(false);const auto legacy_pcm=render_reset(legacy);fail_rev_state=true;CHECK(!legacy.set_modern(true)&&!legacy.modern()&&legacy.ready()&&legacy.num_params==5);fail_rev_state=false;CHECK(render_reset(legacy)==legacy_pcm);
 fail_rev_state=true;std::unique_ptr<Unit> failed(clone_unit(u));CHECK(!failed);R not_ready;CHECK(!not_ready.ready());q15 untouched=111;not_ready.process(&untouched,1);CHECK(untouched==111&&!not_ready.visual_ready());fail_rev_state=false;
 std::puts("ok 45 individual authored flags incl zero-value all8-bank locks, marked roundtrip/reversed chunks, unmarked all-reference canonicalization, clone/reset, transactional rejects/allocation failure");
}
void raw_telemetry() {
 R u,unobserved;for(auto*unit:{&u,&unobserved}){set(*unit,0,0);set(*unit,1,0);set(*unit,2,32767);set(*unit,3,0);}
 // At minimum size the first four raw taps occur at floor(BASE/2). No tank
 // feedback can reach each line before its first read; the immediate diffuser
 // output is +1/4 of the original DC-blocked quarter-send.
 const int delay[]={443,575,725,911};
 const int magnitude=int(((int64_t(32767)*1024*511/512)/4)/4/1024);
 for(int i=0;i<3000;++i){q15 a=i==0?32767:0,b=a;u.process(&a,1);unobserved.process(&b,1);CHECK(a==b);
  for(int lane=0;lane<4;++lane){if(i<delay[lane])CHECK(u.visual_return(lane)==0);if(i==delay[lane])CHECK(u.visual_return(lane)==magnitude);CHECK(u.visual_return(lane)==unobserved.visual_return(lane));}
  const auto meter=u.visual_energy();for(int j=0;j<13;++j){CHECK(u.visual_energy()==meter);CHECK(u.visual_return(-7)==u.visual_return(0)&&u.visual_return(9)==u.visual_return(3));CHECK(u.visual_effective(-7)==u.visual_effective(0)&&u.visual_effective(9)==u.visual_effective(7));}
 }
 CHECK(u.visual_return(0)!=u.visual_return(3));
 // Processed rather than merely edited target reporting.
 const int before=u.visual_effective(1);u.params[1].effective=30000;CHECK(u.visual_effective(1)==before);q15 zero=0;u.process(&zero,1);CHECK(u.visual_effective(1)==30000);
 std::puts("ok four independent first-arrival envelopes, exact raw impulse magnitudes, observation-only queries and last-processed targets");
}
void layers_engine() {
 for(int p=5;p<8;++p){R u;auto&v=u.params[p];CHECK(!v.bipolar);v.base=4000;v.step_lock=8000;v.audition=12000;v.performance=16000;v.temp=20000;
  for(int mask=0;mask<16;++mask)for(int enabled:{0,1})for(int depth:{-32767,17000,32767})for(int mod:{-32767,0,32767}){
   v.step_lock_override=mask&1;v.audition_override=mask&2;v.performance_override=mask&4;v.temp_override=mask&8;v.mod_enabled=enabled;v.mod_depth=(q15)depth;v.slew_ready=false;
   int control=mask&8?20000:mask&4?16000:mask&2?12000:mask&1?8000:4000;
   int delta=enabled?(int)std::floor((double)mod*depth/32768.0):0;v.resolve((q15)mod);CHECK(v.target==std::max(0,std::min(32767,control+delta))&&v.effective==v.target&&v.base==4000);
  }
 }
 auto e=std::make_unique<Engine>();auto*u=new R;delete e->replace_unit(0,0,u);q15 stereo[64]={};
 for(int p=5;p<8;++p){auto&v=u->params[p];v.base=5000;v.src=ModSource::SigilA;v.mod_depth=16000;v.slew_ready=false;e->render(stereo,32);CHECK(v.target==std::max(0,std::min(32767,5000+(int)std::floor((double)e->sigils[0].value*16000/32768.0))));CHECK(u->visual_effective(p)==v.effective);
  v.mod_enabled=false;e->render(stereo,32);CHECK(v.target==5000&&v.src==ModSource::SigilA);CHECK(e->set_step_lock(0,0,0,p,17000+p));CHECK(e->audition_sequence_step(0,0));CHECK(v.audition_override&&v.audition==17000+p);CHECK(e->begin_param_temp(0,0,p));CHECK(e->set_param_temp(22000+p));e->render(stereo,32);CHECK(v.temp_override&&v.target==22000+p&&v.base==5000);e->cancel_param_temp();e->cancel_sequence_audition();e->render(stereo,32);CHECK(!v.temp_override&&!v.audition_override&&v.target==5000);
 }
 for(int p=5;p<8;++p)set(*u,p,0);
 e->capture_scene(0,0);for(int p=5;p<8;++p)set(*u,p,32767);e->capture_scene(0,1);e->apply_scene_morph(0,16384);for(int p=5;p<8;++p)CHECK(u->params[p].base==16383);
 e->macro_pads[0].route_count=3;for(int p=5;p<8;++p)e->macro_pads[0].routes[p-5]={0,0,(uint8_t)p,0,32767};e->apply_macro_pad(0,0);for(int p=5;p<8;++p)CHECK(u->params[p].base==0);e->apply_macro_pad(0,32767);for(int p=5;p<8;++p)CHECK(u->params[p].base>=32764);
 e->set_sequencer_enabled(true);e->transport_reset();e->transport_play();for(int i=0;i<128;++i)e->render(stereo,32);for(int p=5;p<8;++p)CHECK(u->params[p].step_lock_override&&u->params[p].step_lock==17000+p&&u->params[p].base>=32764);e->transport_stop();e->render(stereo,32);for(int p=5;p<8;++p)CHECK(!u->params[p].step_lock_override);
 std::puts("ok all three utility unipolar Param priorities; real engine sigil/mute/audition/temp/scenes/macro/scheduled locks without base mutation");
}
void focused(){mappings();predelay();lowcut_duck();freeze_telemetry_split();raw_telemetry();layers_engine();persistence();}
#endif
}
int main(int argc,char**argv){
#ifdef REVERB_FIELD_BASELINE_ONLY
 if(argc==3&&std::strcmp(argv[1],"--old-reader")==0){
  directory=argv[2];auto e=std::make_unique<Engine>();
  for(const char* name:{"snc7-dense-old.snc","snc7-legacy-old.snc"})CHECK(patchio::load(*e,(directory+"/"+name).c_str()));
  Unit* original=e->chains[0].units[0];
  CHECK(!patchio::load(*e,(directory+"/enhanced.snc").c_str()));CHECK(e->chains[0].units[0]==original);
  for(int p=5;p<8;++p)for(int mode=0;mode<15;++mode){CHECK(!patchio::load(*e,(directory+"/author-"+std::to_string(p)+"-"+std::to_string(mode)+".snc").c_str()));CHECK(e->chains[0].units[0]==original);}
  std::printf("old frozen reader: rejects all46 enhanced fixtures transactionally, accepts2 neutral fixtures (%llu checks)\n",(unsigned long long)checks);return 0;
 }
#endif
 CHECK(argc==2);directory=argv[1];frozen_direct();legacy_fixtures();
#ifndef REVERB_FIELD_BASELINE_ONLY
focused();
#endif
std::printf("reverb field core: passed (%llu checks)\n",(unsigned long long)checks);}
