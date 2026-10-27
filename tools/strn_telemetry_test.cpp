// Differential test: actual optimized STRN vs the pinned original process body.
// The runner generates the reference class and count-only copies outside runtime.
#include "engine/units.h"
#include "strn_reference.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>

using seance::StranularUnit;
using seance::StrnReference;
using seance::q15;
namespace strn_work { uint64_t current_positions=0, current_wave=0, legacy_positions=0, legacy_wave=0; }
static bool no_heap=false, fail_buffer=false;
static unsigned heap_calls=0, pcm_frames=0, comparisons=0;
extern "C" void* __real__Znwm(size_t);
extern "C" void* __real__Znam(size_t);
extern "C" void* __real__ZnamRKSt9nothrow_t(size_t, const std::nothrow_t&);
extern "C" void* __wrap__Znwm(size_t n) { if (no_heap) ++heap_calls; return __real__Znwm(n); }
extern "C" void* __wrap__Znam(size_t n) { if (no_heap) ++heap_calls; return __real__Znam(n); }
extern "C" void* __wrap__ZnamRKSt9nothrow_t(size_t n, const std::nothrow_t& t) {
    if (no_heap) ++heap_calls;
    return fail_buffer && n==65536*sizeof(q15) ? nullptr : __real__ZnamRKSt9nothrow_t(n,t);
}
struct Guard { Guard() { assert(!no_heap); no_heap=true; } ~Guard() { no_heap=false; assert(!heap_calls); } };
struct Pair {
    StranularUnit current;
    StrnReference old;
    uint32_t input=0xabc92721u;
    uint64_t frame=0;
    void param(int index, q15 value) { current.params[index].effective=old.params[index].effective=value; }
    void long_mode(bool longer) { current.long_mode=old.long_mode=longer; }
    void window(int mode) { current.window_mode=old.window_mode=mode; }
    void compare(bool wave=true) {
        Guard guard;
        assert(current.ready()==old.ready());
        assert(current.visual_grains()==old.visual_grains());
        // Includes inactive/stale slots, not merely currently drawn heads.
        assert(!std::memcmp(current.visual_positions(),old.visual_positions(),StranularUnit::MAX_GRAINS));
        assert(!std::memcmp(current.visual_increments(),old.visual_increments(),StranularUnit::MAX_GRAINS*sizeof(int32_t)));
        assert(current.visual_fill()==old.visual_fill() && current.visual_mix()==old.visual_mix());
        assert(current.visual_frozen()==old.visual_frozen());
        assert(current.scan_span_frames()==old.scan_span_frames());
        assert(current.active_grain_count()==old.active_grain_count());
        if (wave) {
            const auto* actual=current.visual_wave();
            assert(!std::memcmp(actual,old.visual_wave(),StranularUnit::VIZ_POINTS*sizeof(q15)));
            const auto work=strn_work::current_wave;
            assert(current.visual_wave()==actual && current.visual_wave()==actual);
            assert(strn_work::current_wave==work); // repeat capture never rebuilds
        }
        ++comparisons;
    }
    void run(int n, bool wave=true) {
        assert(n>=0 && n<=512);
        q15 a[512], b[512];
        for (int i=0;i<n;++i) { input=input*1664525u+1013904223u; a[i]=b[i]=int(input>>16)-32768; }
        const auto before=strn_work::current_positions;
        { Guard guard; current.process(a,n); old.process(b,n); }
        assert(!std::memcmp(a,b,size_t(n)*sizeof(q15)));
        assert(strn_work::current_positions-before<=StranularUnit::MAX_GRAINS);
        frame+=n; pcm_frames+=n;
        compare(wave);
    }
    void advance(unsigned frames, bool wave=false) {
        while (frames) { const unsigned n=std::min(512u,frames); run(n,wave); frames-=n; }
    }
    void reset() { Guard guard; current.reset(); old.reset(); frame=0; }
};

static void trajectories() {
    Pair p;
    p.compare();
    const int blocks[]={1,7,31,32,33,63,64,65,257,512,3};
    for (int step=0;step<2600;++step) {
        if (!(step%97)) {
            const int phase=step/97;
            p.long_mode((phase&1)!=0); p.window(phase%3);
            p.param(StranularUnit::P_SIZE,phase%3==0?0:phase%3==1?7000:32767);
            p.param(StranularUnit::P_DENS,phase%4==0?0:32767);
            p.param(StranularUnit::P_POS,q15((phase*6821)%32768));
            p.param(StranularUnit::P_SPRAY,q15((phase*3187)%32768));
            p.param(StranularUnit::P_PITCH,q15((phase%5-2)*16383));
            p.param(StranularUnit::P_PITCH_ENV,q15((phase%3-1)*32767));
            p.param(StranularUnit::P_REVERSE,phase%3==0?0:phase%3==1?17000:32767);
            p.param(StranularUnit::P_CLUSTER,q15((phase%4)*10922));
            p.param(StranularUnit::P_MIX,q15((phase*7393)%32768));
            p.param(StranularUnit::P_SYNC,phase%4==2?32767:0);
            p.current.sync_division=p.old.sync_division=phase%4;
            p.current.set_transport_bpm(60+phase*7); p.old.set_transport_bpm(60+phase*7);
        }
        p.param(StranularUnit::P_HOLD,step%157<19?32767:0);
        p.param(StranularUnit::P_FREEZE,step%211>=153?32767:0);
        p.run(blocks[step%11],step%5==0);
        if (step==1301) { p.reset(); p.compare(); }
    }
    p.compare();
    std::puts("pass STRN trajectories: exact PCM/all telemetry, short and long blocks, windows/pitch/reverse/hold/freeze/sync/cluster/reset");
}
static void retirement() {
    Pair p;
    p.param(StranularUnit::P_HOLD,32767); p.advance(8192);
    p.param(StranularUnit::P_SIZE,0); p.param(StranularUnit::P_DENS,0);
    p.param(StranularUnit::P_SPRAY,0); p.param(StranularUnit::P_PITCH_ENV,-12345);
    p.param(StranularUnit::P_HOLD,0); p.run(1);
    assert(p.current.active_grain_count()==1);
    p.param(StranularUnit::P_HOLD,32767); p.run(126); p.run(1);
    assert(!p.current.active_grain_count() && p.current.visual_grains()==1);
    p.run(1); assert(!p.current.visual_grains()); // retired final-sample marker is not carried forward
    p.reset(); p.param(StranularUnit::P_PITCH_ENV,0); p.advance(65536);
    p.param(StranularUnit::P_POS,0); p.param(StranularUnit::P_PITCH,32767);
    p.param(StranularUnit::P_HOLD,0); p.run(1);
    p.param(StranularUnit::P_HOLD,32767); p.run(63);
    assert(p.current.active_grain_count()==1 && !p.current.visual_grains()); // live head outside visible history
    p.run(64); assert(!p.current.active_grain_count());
    std::puts("pass STRN last-rendered coordinates: final-sample retirement, mid-block exit and stale hidden slots");
}
static void deferred_wave() {
    Pair p;
    p.param(StranularUnit::P_HOLD,32767); p.long_mode(true);
    p.advance(65536); // never request wave: leave the last tick dirty
    p.run(63,false); p.compare(); // oldest physical point has been overwritten
    for (unsigned offset=0;offset<64;++offset) {
        p.run((64-p.frame%64)%64,false);
        p.run(64,false); p.run(offset,false); p.compare();
    }
    // Freeze a nearly-full ring, then unfreeze/wrap before the next visual tick.
    p.reset(); p.advance(65530); p.param(StranularUnit::P_FREEZE,32767); p.run(6,false);
    p.param(StranularUnit::P_FREEZE,0); p.run(63,false); p.compare();
    // Reset discards pending cache, including the separately preserved oldest bin.
    p.run(1,false); p.reset(); p.compare(); p.run(0);
    p.run(1,false); p.param(StranularUnit::P_FREEZE,32767); p.run(63,false);
    p.long_mode(false); p.run(0,false); p.compare(); // one-frame frozen span; no invented next-read view
    p.param(StranularUnit::P_FREEZE,0); p.run(63,false); p.compare();
    std::puts("pass STRN deferred wave: original 64-frame age, all 64 offsets/full ring/partial freeze/overwrite/reset/repeat captures");
}
static void bounded_work() {
    Pair p;
    p.long_mode(true); p.param(StranularUnit::P_HOLD,32767); p.advance(65536);
    p.param(StranularUnit::P_SIZE,32767); p.param(StranularUnit::P_DENS,32767);
    p.param(StranularUnit::P_CLUSTER,32767); p.param(StranularUnit::P_FREEZE,32767);
    p.param(StranularUnit::P_HOLD,0); p.run(66);
    p.param(StranularUnit::P_HOLD,32767);
    assert(p.current.active_grain_count()==8 && p.current.visual_grains()==255);
    using namespace strn_work;
    current_positions=current_wave=legacy_positions=legacy_wave=0;
    for (int audio_block=0;audio_block<32;++audio_block) {
        for (int control=0;control<16;++control) p.run(32,false);
        p.compare();
    }
    assert(legacy_positions==131072 && current_positions==4096);
    assert(legacy_wave==256 && current_wave==32);
    std::printf("pass STRN counted work / 16384 frames: position divisions %llu -> %llu; wave rebuilds %llu -> %llu\n",
                (unsigned long long)legacy_positions,(unsigned long long)current_positions,
                (unsigned long long)legacy_wave,(unsigned long long)current_wave);
    current_wave=legacy_wave=0;
    for (int control=0;control<128;++control) p.run(32,false);
    assert(!current_wave && legacy_wave==64); // no consumer, no 40-point reconstruction
    p.compare(); assert(current_wave==1);
    fail_buffer=true; { Pair empty; assert(!empty.current.ready()); empty.run(512); empty.reset(); empty.compare(); } fail_buffer=false;
    std::printf("pass STRN bounded memory: guarded heap calls=%u, lazy hidden wave, allocation failure; sizeof unit=%zu\n",
                heap_calls,sizeof(StranularUnit));
}
int main() {
    trajectories(); retirement(); deferred_wave(); bounded_work();
    std::printf("STRN differential: %u PCM frames and %u telemetry comparisons; no hardware timing claims\n",pcm_frames,comparisons);
}
