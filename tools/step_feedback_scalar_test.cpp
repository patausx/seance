// Bounded scalar feedback proof, separate from the real-controller raster tests.
#include "../platform/3ds/step_feedback.h"
#include "../platform/3ds/ui_palette.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>
namespace fb=seance::platform::ui3ds::step_feedback;
namespace sf=seance::platform::ui3ds::step_focus;
namespace pal=seance::platform::ui3ds::palette;
static bool guarded=false;
static unsigned allocations=0;
static unsigned long long checks=0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t,size_t);
extern "C" void* __real_realloc(void*,size_t);
extern "C" void* __wrap_malloc(size_t n){if(guarded)++allocations;return __real_malloc(n);}
extern "C" void* __wrap_calloc(size_t n,size_t s){if(guarded)++allocations;return __real_calloc(n,s);}
extern "C" void* __wrap_realloc(void* p,size_t n){if(guarded)++allocations;return __real_realloc(p,n);}
void* operator new(size_t n){if(guarded)++allocations;if(void* p=__real_malloc(n))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
#define REQUIRE(x) do{++checks;if(!(x)){guarded=false;std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void envelopes(){
    for(unsigned d=0;d<=140;++d)for(unsigned a=0;a<=65535;++a){
        const double attack=std::min(30.,d*.3);
        const double expected=a>=d?0:a<attack?a/attack:std::pow((d-a)/(d-attack),2);
        const float actual=fb::hit_power(a,d);
        REQUIRE(std::isfinite(actual)&&actual>=0&&actual<=1);
        REQUIRE(std::abs(actual-expected)<.000001);
    }
    for(unsigned a=0;a<=65535;++a){
        const double expected=a>=110?0:std::pow((110-a)/110.,2);
        REQUIRE(std::abs(fb::press_power(a)-expected)<.000001);
    }
}
static void states(){
    bool keys[256]={};
    for(int layer=0;layer<8;++layer)for(int bank=0;bank<8;++bank)for(int page=0;page<4;++page){
        const int key=page+4*(bank+8*layer);REQUIRE(key>=0&&key<256&&!keys[key]);keys[key]=true;
        for(int row=0;row<4;++row)for(int step=0;step<64;++step){
            fb::Press s;REQUIRE(s.power(10000,row,step)==0);
            s.fire(10000,key,row,step);
            for(unsigned a=0;a<=112;++a){
                s.update(10000+a,key);
                REQUIRE(s.power(10000+a,row,step)==fb::press_power(a));
                REQUIRE(s.power(10000+a,(row+1)%4,step)==0);
                REQUIRE(s.power(10000+a,row,(step+1)%64)==0);
            }
            s.fire(10000,key,row,step);s.update(10050,key);s.fire(10051,key,row,step);
            REQUIRE(s.power(10051,row,step)==1);REQUIRE(s.power(10081,row,step)==fb::press_power(30));
            s.update(10082,key^1);REQUIRE(s.power(10082,row,step)==0);
            s.fire(10000,key,row,step);s.update(9999,key);REQUIRE(s.power(10000,row,step)==0);
            s.fire(10000,key,row,step);s.update(10201,key);REQUIRE(s.power(10201,row,step)==0);
            s.fire(UINT64_MAX-50,key,row,step);s.update(UINT64_MAX-1,key);
            REQUIRE(s.power(UINT64_MAX-1,row,step)==fb::press_power(49));
            s.update(0,key);REQUIRE(s.power(0,row,step)==0);
            s.fire(0,key,row,step);s.reset();REQUIRE(s.power(0,row,step)==0);
        }
    }
}
static void geometry_and_color(){
    const float powers[]={0,.001f,.159f,.16f,.5f,1};
    for(int row=0;row<4;++row)for(int col=0;col<16;++col)for(float hit:powers)for(float press:powers){
        const auto b=sf::raster(sf::target(row,col));const auto f=fb::face(b,hit,press);
        const int inset=std::max(hit,press)>=.16f?1:0;
        REQUIRE(f.x==b.x+inset&&f.y==b.y+inset&&f.w==18-2*inset&&f.h==18-2*inset);
        REQUIRE(f.x>=0&&f.x+f.w<=320&&f.y>=41&&f.y+f.h<=192);
    }
    // Every possible component pair, representative powers, independent nearest
    // byte oracle; tolerate only scalar float error near an exact half-integer.
    for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b)for(float p:powers){
        const auto got=fb::tint(0xff000000u+a*0x010101u,0xff000000u+b*0x010101u,p);
        const double ideal=a+(double(b)-a)*double(p)*.28;
        const int expected=int(std::floor(ideal+.5));const int byte=got&255;
        REQUIRE((got>>24)==255&&((got>>8)&255)==unsigned(byte)&&((got>>16)&255)==unsigned(byte));
        REQUIRE(byte==expected||std::abs(ideal-(std::floor(ideal)+.5))<.00002);
        REQUIRE(byte>=int(std::min(a,b))&&byte<=int(std::max(a,b)));
        if(p==0||a==b)REQUIRE(byte==int(a));
    }
}
int main(){
    guarded=true;envelopes();states();geometry_and_color();guarded=false;
    REQUIRE(allocations==0);REQUIRE(sizeof(fb::Press)<=32);
    std::printf("feedback scalar: %llu checks; Press=%zu Box=%zu; C/C++ guarded allocations=%u\n",checks,sizeof(fb::Press),sizeof(sf::Box),allocations);
}
