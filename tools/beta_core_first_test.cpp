// Focused beta core-first reproduction. No existing test runner/suite modified.
// --readiness intentionally FAILS on frozen pre-fix Delay (saved log); --after passes.
// --invalid runs each bad call in isolated child, --pcm prints output FNV to
// compare frozen vs current valid-buffer sound. No hardware/audio-device input.
#include "engine/units.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
using namespace seance;
namespace {
bool fail_legacy=false,fail_modern=false,fail_chorus=false;
unsigned checks=0;
#define CHECK(expr) do {++checks;if(!(expr)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr);return 1;}} while(0)
void params(DelayUnit&u){for(int p=0;p<u.num_params;++p)u.params[p].snap();u.control_tick();}
int readiness(){
 {DelayUnit u(false);CHECK(u.ready()&&!u.modern());CHECK(!u.visual_state().ready);
  params(u);CHECK(!u.visual_state().ready);
  q15 sample=321;u.process(nullptr,1);u.process(&sample,0);u.process(&sample,-5);
  CHECK(sample==321&&!u.visual_state().ready);
  u.process(&sample,1);CHECK(u.visual_state().ready);CHECK(u.visual_state().read_a_q8>0);
  const auto processed=u.visual_state();u.process(nullptr,1);u.process(&sample,0);CHECK(u.visual_state().ready);
  CHECK(u.visual_state().read_a_q8==processed.read_a_q8);
  u.reset();CHECK(u.ready()&&!u.visual_state().ready);params(u);CHECK(!u.visual_state().ready);
  q15 restarted[3]={321,0,0};u.process(restarted,3);CHECK(u.visual_state().ready);
  CHECK(u.set_modern(true)&&u.ready()&&!u.visual_state().ready);
  u.process(nullptr,1);u.process(&sample,0);CHECK(!u.visual_state().ready);
  u.process(&sample,1);CHECK(u.visual_state().ready);u.reset();CHECK(!u.visual_state().ready);
  CHECK(u.set_modern(false)&&u.ready()&&!u.visual_state().ready);
  u.process(&sample,1);CHECK(u.visual_state().ready);
  // Transactional failed upgrade retains real last-read readiness, old mode and PCM.
  fail_modern=true;CHECK(!u.set_modern(true));fail_modern=false;
  CHECK(!u.modern()&&u.ready()&&u.visual_state().ready);
 }
 {DelayUnit u(true);CHECK(u.ready()&&u.modern()&&!u.visual_state().ready);
  q15 sample=123;u.process(nullptr,100);u.process(&sample,-1);u.process(&sample,0);
  CHECK(sample==123&&!u.visual_state().ready);u.process(&sample,1);CHECK(u.visual_state().ready);
  u.reset();CHECK(u.ready()&&!u.visual_state().ready);
 }
 {fail_legacy=true;DelayUnit u(false);fail_legacy=false;CHECK(!u.ready()&&!u.visual_state().ready);
  q15 sample=713;u.process(&sample,1);u.process(nullptr,1);CHECK(sample==713&&!u.visual_state().ready);
  CHECK(u.set_modern(false)&&u.ready()&&!u.visual_state().ready);u.process(&sample,1);CHECK(u.visual_state().ready);
 }
 {fail_modern=true;DelayUnit u(true);fail_modern=false;CHECK(!u.ready()&&!u.visual_state().ready);
  q15 sample=713;u.process(&sample,1);CHECK(sample==713&&!u.visual_state().ready);
  CHECK(u.set_modern(true)&&u.ready()&&!u.visual_state().ready);
 }
 std::printf("ok delay fresh/reset/failed-allocation/invalid/positive and transactional mode switch: %u checks\n",checks);
 return 0;
}
using Factory = Unit*(*)();
Unit* fold(){return new FoldUnit;}Unit* bit(){return new BitUnit;}Unit* chorus(){return new ChorusUnit;}
int invalid(){
 const struct Entry{const char*name;Factory make;} entries[]={{"Fold",fold},{"Bit",bit},{"Chorus",chorus}};
 for(const auto&e:entries){pid_t pid=fork();if(pid<0)throw std::runtime_error("fork failed");
  if(pid==0){Unit*u=e.make();u->process(nullptr,1);delete u;_exit(0);}
  int status=0;if(waitpid(pid,&status,0)!=pid)throw std::runtime_error("waitpid failed");
  std::printf("%s nullptr/positive: %s:%d\n",e.name,WIFSIGNALED(status)?"signal":WIFEXITED(status)?"exit":"other",WIFSIGNALED(status)?WTERMSIG(status):WIFEXITED(status)?WEXITSTATUS(status):-1);
  if(!(WIFEXITED(status)&&WEXITSTATUS(status)==0))++checks;
 }
 return 0;
}
uint64_t hash(uint64_t h,q15 x){h^=(uint16_t)x;return h*1099511628211ull;}
int pcm(){
 // Includes valid full-scale signed input, seven nondefault Bit controls and all Fold anchors.
 uint32_t seed=0x71cfc58du;
 for(const auto&entry:{std::pair<Factory,const char*>{fold,"fold"},{bit,"bit"},{chorus,"chorus"}}){
  Unit*u=entry.first();uint64_t h=1469598103934665603ull;
  for(int block=0;block<7;++block){
   if(u->kind==UnitKind::Fold){const q15 v[][5]={{8192,32767,0,0,32767},{32767,11000,16384,-32767,10000},{32767,32767,32767,32767,32767},{0,0,8192,0,0},{32767,16384,24000,-32767,32766},{8192,32767,8000,18000,8192},{32767,32767,32767,-32768,32767}};
    for(int p=0;p<5;++p)u->params[p].effective=v[block][p];}
   if(u->kind==UnitKind::Bitcrush){const q15 v[][7]={{20000,28000,32767,0,0,0,0},{0,32767,32767,32767,0,-32767,32767},{32767,0,32767,32767,32767,32767,32767},{14000,13000,0,16384,7000,0,10000},{100,32767,32767,0,20000,-20000,0},{27000,7000,32766,32767,30000,32767,32767},{32767,32767,32767,0,0,0,0}};
    for(int p=0;p<7;++p)u->params[p].effective=v[block][p];}
   if(u->kind==UnitKind::Chorus){const q15 v[][3]={{7000,12000,12000},{32767,32767,32767},{0,0,32767},{17000,24000,0},{1,1,1},{32767,30000,16384},{32767,32767,32767}};
    for(int p=0;p<3;++p)u->params[p].effective=v[block][p];}
   q15 pcm[1024];for(auto&x:pcm){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;x=q15(seed>>16);}
   u->process(pcm,1024);for(q15 x:pcm)h=hash(h,x);
  }
  std::printf("%s valid PCM 7168 fnv64=%016llx\n",entry.second,(unsigned long long)h);delete u;
 }
 return 0;
}
}
void* operator new[](size_t n){if((fail_legacy&&n==64000)||(fail_modern&&n==256004)||(fail_chorus&&n==4096))throw std::bad_alloc();if(void*p=std::malloc(n))return p;throw std::bad_alloc();}
void* operator new[](size_t n,const std::nothrow_t&) noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete[](void*p) noexcept{std::free(p);}void operator delete[](void*p,size_t) noexcept{std::free(p);}void operator delete[](void*p,const std::nothrow_t&) noexcept{std::free(p);}
int main(int argc,char**argv){if(argc!=2)return 2;const std::string mode=argv[1];if(mode=="--readiness")return readiness();if(mode=="--invalid")return invalid();if(mode=="--pcm")return pcm();return 2;}
