// Narrow allocation/host-cost probe; not a hardware CPU estimate.
#include "engine/units.h"
#include <new>
#include <cstdlib>
#include <cstdio>
#include <chrono>
static bool hot=false;
static unsigned allocations=0;
void* operator new(std::size_t n){if(hot)++allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t,std::size_t);
extern "C" void* __real_realloc(void*,std::size_t);
extern "C" void* __wrap_malloc(std::size_t n){if(hot)++allocations;return __real_malloc(n);}
extern "C" void* __wrap_calloc(std::size_t n,std::size_t s){if(hot)++allocations;return __real_calloc(n,s);}
extern "C" void* __wrap_realloc(void* p,std::size_t n){if(hot)++allocations;return __real_realloc(p,n);}
int main(){
    seance::ShiftUnit u;seance::q15 b[32];volatile long checksum=0;
    hot=true;const auto start=std::chrono::steady_clock::now();
    for(int n=0;n<32768;++n){
        u.params[0].effective=seance::q15(n*7);u.params[1].effective=seance::q15(n);u.params[2].effective=seance::q15(n);
        u.control_tick();for(int i=0;i<32;++i)b[i]=seance::q15(n*71+i*499);
        u.process(b,32);checksum+=b[0];if(n%2048==0)u.reset();
    }
    const auto end=std::chrono::steady_clock::now();hot=false;
    const double us=std::chrono::duration<double>(end-start).count()*1e6;
    std::printf("hot audit: %u allocations;1048576 frames; %0.3f us/32-frame control+process; checksum%ld; host only, not 3DS\n",allocations,us/32768,checksum);
    return allocations?1:0;
}
