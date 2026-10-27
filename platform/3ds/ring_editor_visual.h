#pragma once
#include "module_frame_layout.h"
#include "ring_reference.h"
#include <algorithm>
#include <cstdint>

namespace seance::platform::ui3ds::ring_editor_visual {
using C=RingUnit;
constexpr int kColumns=353,kLeft=34,kRight=386,kBudget=2048;
constexpr int kFast=1024,kSlow=4*ENGINE_SR;
constexpr int kControlLeft[2]={14,172},kControlRight[2]={148,306},kRows[4]={70,110,150,190};
constexpr int kZero[3]={86,120,168},kHeight[3]={12,12,28};
struct Column {int8_t first=0,last=0,low=0,high=0;};
struct Plot {Column trace[3][kColumns]={};};
static_assert(sizeof(Plot)==12*kColumns,"fixed, pointer-free Ring reference projection");
static_assert(module_frame_layout::kLeft==14&&module_frame_layout::kHeaderTextY==14&&module_frame_layout::kFooterTextY==228,"shared Ring frame");
struct Key {q15 values[C::P_COUNT]={};bool bypass=false,slow=false;};
inline bool same(const Key& a,const Key& b){return a.bypass==b.bypass&&a.slow==b.slow&&
    std::equal(a.values,a.values+C::P_COUNT,b.values);}
// Quantized .75FS sine period; no per-sample libm in the UI model.
inline constexpr q15 kTone[128]={
    0,1206,2409,3606,4794,5971,7134,8279,9404,10507,11585,12634,13653,14639,15590,16504,
    17377,18209,18997,19739,20433,21079,21673,22216,22704,23138,23517,23839,24103,24309,24457,24545,
    24575,24545,24457,24309,24103,23839,23517,23138,22704,22216,21673,21079,20433,19739,18997,18209,
    17377,16504,15590,14639,13653,12634,11585,10507,9404,8279,7134,5971,4794,3606,2409,1206,
    0,-1206,-2409,-3606,-4794,-5971,-7134,-8279,-9404,-10507,-11585,-12634,-13653,-14639,-15590,-16504,
    -17377,-18209,-18997,-19739,-20433,-21079,-21673,-22216,-22704,-23138,-23517,-23839,-24103,-24309,-24457,-24545,
    -24575,-24545,-24457,-24309,-24103,-23839,-23517,-23138,-22704,-22216,-21673,-21079,-20433,-19739,-18997,-18209,
    -17377,-16504,-15590,-14639,-13653,-12634,-11585,-10507,-9404,-8279,-7134,-5971,-4794,-3606,-2409,-1206};
inline q15 source_sample(int i,bool slow){
    if(!slow)return ring_reference::source_sample(i);
    // Distinct 4s source: q15 250Hz/.75FS sine, 16ms attack, hold to .5s,
    // linear release until 2s, then 2s silence; not a stretched 32ms burst.
    const int denominator=i<512?512:ENGINE_SR*3/2;
    const int numerator=i<512?i:i<ENGINE_SR/2?denominator:std::max(0,2*ENGINE_SR-i);
    const int product=int(kTone[i%128])*numerator;
    return q15(product>=0?(product+denominator/2)/denominator:-((-product+denominator/2)/denominator));
}
inline int8_t ordinate(q15 sample,int trace){return int8_t(int(sample)*kHeight[trace]/32768);}
inline void append(Column& c,int8_t v,bool first){
    if(first)c.first=c.low=c.high=v;else {c.low=std::min(c.low,v);c.high=std::max(c.high,v);}c.last=v;
}
// UI-owned, disconnected DSP. Only advance() performs model work, never draw or
// an audio Guard. New keys restart/coalesce; incomplete plots are never published.
struct Model {
    C unit{true};Key key{};Plot plot{};int done=0,work=0,starts=0;bool valid=false;
    void invalidate(){valid=false;done=work=0;}
    int total()const{return key.slow?kSlow:kFast;}
    bool ready()const{return valid&&done==total();}
    void advance(const Key& next){
        work=0;
        if(!valid||!same(key,next)){
            key=next;plot={};done=0;valid=true;++starts;
            for(int p=0;p<C::P_COUNT;++p){unit.params[p].base=key.values[p];unit.params[p].snap();}
            unit.reset();unit.control_tick();
        }
        const int end=std::min(total(),done+kBudget);
        q15 input[128],out[128],carrier[128];
        while(done<end){const int count=std::min(128,end-done);
            for(int i=0;i<count;++i){input[i]=out[i]=source_sample(done+i,key.slow);carrier[i]=0;}
            if(!key.bypass)unit.process_with_carrier(out,carrier,count);
            for(int i=0;i<count;++i){const int n=done+i,col=n*kColumns/total();
                const bool first=n==0||(n-1)*kColumns/total()!=col;
                append(plot.trace[0][col],ordinate(input[i],0),first);
                append(plot.trace[1][col],ordinate(carrier[i],1),first);
                append(plot.trace[2][col],ordinate(out[i],2),first);
            }
            done+=count;work+=count;
        }
    }
};
}
