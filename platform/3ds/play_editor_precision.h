#pragma once
// Private, bounded source-time math and exhaustive PCM detail for PLAY's indirect editor.
#include "../../core/ui/play_editor_tools.h"
#include <algorithm>
#include <cstdint>
namespace seance::platform::ui3ds::play_editor_precision {
struct View {uint32_t first=0,span=0;int scale=1;};
inline View zoom(uint32_t total,int requested,uint32_t edge){
    if(!total)return {};
    const uint32_t floor=std::min<uint32_t>(total,372);
    int scale=1;
    while(scale<256&&scale*2<=requested&&total/uint32_t(scale*2)>=floor)scale*=2;
    const uint32_t span=total/uint32_t(scale);
    const int64_t first=std::max<int64_t>(0,std::min<int64_t>(total-span,int64_t(edge)-span/2));
    return {uint32_t(first),span,scale};
}
inline uint32_t strip_frame(int x,uint32_t first,uint32_t span){
    return first+uint32_t(uint64_t(std::clamp(x,14,306)-14)*span/292);
}
inline int64_t displacement(int x,int press,uint32_t span,bool fine){
    return int64_t(std::clamp(x,14,306)-press)*span/(292*(fine?8:1));
}
// floor-partitioned half-open bins: column x consumes [floor(x*span/N),floor((x+1)*span/N)).
// Tiny sources repeat the actual corresponding sample, never an empty/invalid PCM read.
template<int N> struct Envelope : seance::play_editor::Envelope<N> {
    using Base=seance::play_editor::Envelope<N>;
    void feed(const int16_t* pcm,uint32_t start,uint32_t count,int channels){
        if(this->ready||start!=this->next||(channels!=1&&channels!=2))return;
        count=std::min(count,this->last-this->next);
        const uint32_t span=this->last-this->first;
        for(uint32_t f=0;f<count;++f){
            const int x=int((uint64_t(start+f-this->first+1)*N-1)/span);
            for(int ch=0;ch<channels;++ch){const auto v=pcm[size_t(f)*channels+ch];
                this->low[x]=std::min(this->low[x],v);this->high[x]=std::max(this->high[x],v);}
        }
        this->next+=count;this->ready=this->next==this->last;
    }
    void freeze(int8_t* lo,int8_t* hi,uint16_t gain)const{
        int from=0;
        for(int i=0;i<N;++i){
            from=std::max(from,i);
            while(from<N&&this->low[from]>this->high[from])++from;
            if(from>=N){lo[i]=hi[i]=0;continue;}
            lo[i]=int8_t(std::clamp(int(this->low[from])*gain/65536,-128,127));
            hi[i]=int8_t(std::clamp(int(this->high[from])*gain/65536,-128,127));
        }
    }
};
} // namespace seance::platform::ui3ds::play_editor_precision
