#pragma once
#include "module_frame_layout.h"
#include "../../core/engine/units.h"
#include <algorithm>
#include <cstdint>

namespace seance::platform::ui3ds::bit_editor_visual {
constexpr int kColumns=353,kLeft=34,kRight=386,kZero=113,kHeight=36;
constexpr int kWindow=1024,kWarmup=2048,kPacked=(kColumns+7)/8;
constexpr int kControlLeft[2]={14,172},kControlRight[2]={148,306};
constexpr int kMainY[4]={48,94,140,186},kSetupY[4]={40,85,130,175};
struct Column {int8_t first=0,last=0,low=0,high=0;};
struct Plot {Column out[kColumns]={};uint8_t capture[kPacked]={},drop[kPacked]={};};
static_assert(sizeof(Plot)<1536,"bounded actual reset model, never live PCM");
static_assert(module_frame_layout::kLeft==14 && module_frame_layout::kHeaderTextY==14 &&
    module_frame_layout::kFooterTextY==228,"BIT uses the fixed shared frame");
constexpr q15 source_sample(int i){const int phase=i%256;return q15(phase<128?-20480+phase*320:61440-phase*320);}
constexpr int8_t ordinate(q15 v){return int8_t(int(v)*kHeight/32768);}
// The test input is constant, so its exact same-bin projection is compiled once.
// This preserves clean input==output visually without any model calculation in draw.
struct Source {
    Column columns[kColumns]={};
    constexpr Source(){for(int x=0;x<kColumns;++x){
        const int first=x*kWindow/kColumns,last=(x+1)*kWindow/kColumns;
        q15 lo=source_sample(first),hi=lo;
        for(int i=first;i<last;++i){const q15 s=source_sample(i);lo=std::min(lo,s);hi=std::max(hi,s);}
        columns[x]={ordinate(source_sample(first)),ordinate(source_sample(last-1)),ordinate(lo),ordinate(hi)};
    }}
};
inline constexpr Source source{};
inline bool event(const uint8_t* packed,int i){return (packed[i/8]&(1u<<(i%8)))!=0;}
inline void calculate(const q15* values,bool bypass,Plot& plot){
    // This private voice is never attached to a chain. Every cache miss uses
    // the actual DSP with its reset seed, 64ms warmup, then the same 32ms input.
    // Call only after releasing the audio guard and before GPU submission.
    BitUnit reference(true);
    for(int p=0;p<BitUnit::P_COUNT;++p){reference.params[p].base=values[p];reference.params[p].snap();}
    reference.reset();reference.control_tick();
    q15 scratch[kWindow];uint8_t events[kWindow];
    for(int n=0;n<kWarmup;n+=kWindow){
        for(int i=0;i<kWindow;++i)scratch[i]=source_sample(n+i);
        if(!bypass)reference.process(scratch,kWindow);
    }
    for(int i=0;i<kWindow;++i){scratch[i]=source_sample(kWarmup+i);events[i]=0;}
    if(!bypass)reference.process_with_events(scratch,events,kWindow);
    plot={};
    for(int x=0;x<kColumns;++x){
        const int first=x*kWindow/kColumns,last=(x+1)*kWindow/kColumns;
        q15 lo=scratch[first],hi=lo;unsigned flags=0;
        for(int i=first;i<last;++i){lo=std::min(lo,scratch[i]);hi=std::max(hi,scratch[i]);flags|=events[i];}
        plot.out[x]={ordinate(scratch[first]),ordinate(scratch[last-1]),ordinate(lo),ordinate(hi)};
        if(flags&1)plot.capture[x/8]|=uint8_t(1u<<(x%8));
        if(flags&2)plot.drop[x/8]|=uint8_t(1u<<(x%8));
    }
}
} // namespace seance::platform::ui3ds::bit_editor_visual
