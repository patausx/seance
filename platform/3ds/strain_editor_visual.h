#pragma once
#include "module_frame_layout.h"
#include "strain_reference_data.h"
#include "../../core/engine/units.h"
#include <algorithm>
#include <cstdint>

namespace seance::platform::ui3ds::strain_editor_visual {
constexpr int kColumns=353,kLeft=34,kRight=386,kZero=109,kHeight=34;
constexpr int kWindow=1024,kMotionPoints=177;
constexpr int kControlLeft[2]={14,172},kControlRight[2]={148,306};
constexpr int kMainY[4]={43,89,135,181},kSetupY=79;
struct Column {int8_t first=0,last=0,low=0,high=0;};
struct Plot {Column out[kColumns]={};uint8_t motion[kMotionPoints]={};uint8_t body=0;};
static_assert(sizeof(Plot)<1664,"bounded actual reset model, never live PCM");
static_assert(module_frame_layout::kLeft==14 && module_frame_layout::kHeaderTextY==14 &&
    module_frame_layout::kFooterTextY==228,"strain uses the fixed shared frame");
// Approved .75fs/250hz burst: 2ms raised-cosine attack, 14ms release, 16ms silence.
constexpr q15 source_sample(int i){return i<512?strain_reference::data::BURST[i]:0;}
constexpr int8_t ordinate(q15 v){const int n=int(v)*kHeight;return int8_t(n>=0?(n+16384)/32768:-((-n+16384)/32768));}
constexpr uint8_t body_y(int v){return uint8_t(213-(std::max(0,std::min(32767,v))*27+16383)/32767);}
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
inline void calculate(const q15* values,bool bypass,Plot& plot){
    // Private real voice, reset state and pre-block 1khz ticks. Never advance the
    // live unit or perform this work under an audio guard / in a GPU scene.
    StrainUnit reference(true);
    for(int p=0;p<StrainUnit::P_COUNT;++p){reference.params[p].base=values[p];reference.params[p].snap();}
    reference.reset();
    q15 scratch[kWindow];
    for(int n=0;n<kWindow;n+=CONTROL_INTERVAL){
        for(int i=0;i<CONTROL_INTERVAL;++i)scratch[n+i]=source_sample(n+i);
        if(!bypass){reference.control_tick();reference.process(scratch+n,CONTROL_INTERVAL);}
    }
    plot={};
    for(int x=0;x<kColumns;++x){
        const int first=x*kWindow/kColumns,last=(x+1)*kWindow/kColumns;
        q15 lo=scratch[first],hi=lo;
        for(int i=first;i<last;++i){lo=std::min(lo,scratch[i]);hi=std::max(hi,scratch[i]);}
        plot.out[x]={ordinate(scratch[first]),ordinate(scratch[last-1]),ordinate(lo),ordinate(hi)};
    }
    // A SEPARATE idle/zero-energy whole-cycle model of the actual core coefficient
    // motion. Not the burst's history, live telemetry, or an output-level meter.
    const q15 body=values[StrainUnit::P_BODY],amount=values[StrainUnit::P_MOTION];
    plot.body=body_y(body);
    for(int i=0;i<kMotionPoints;++i){
        const uint32_t phase=uint32_t((uint64_t(i)<<32)/(kMotionPoints-1));
        plot.motion[i]=body_y(int(body)+StrainUnit::model_motion(body,amount,phase,0));
    }
}
} // namespace seance::platform::ui3ds::strain_editor_visual
