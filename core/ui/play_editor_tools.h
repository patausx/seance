#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace seance::play_editor {
constexpr int CUTS = 16;
struct Cuts {
    uint32_t at[CUTS];
    uint16_t reverse = 0;
    uint16_t speed_q8[CUTS]; // 64..1024, 256 = 1x; follows stable id, not sort order
    Cuts() { for (auto& p : at) p = UINT32_MAX; for (auto& s : speed_q8) s = 256; }
    bool operator==(const Cuts& b) const { return reverse == b.reverse && !std::memcmp(at,b.at,sizeof at) && !std::memcmp(speed_q8,b.speed_q8,sizeof speed_q8); }
    bool operator!=(const Cuts& b) const { return !(*this==b); }
    uint32_t end(int id,uint32_t frames) const {
        uint32_t out=frames;
        if(id<0||id>=CUTS||at[id]>=frames)return 0;
        for(auto p:at)if(p>at[id]&&p<out)out=p;
        return out;
    }
    bool valid(int id,uint32_t frames)const{return id>=0&&id<CUTS&&at[id]<frames&&end(id,frames)-at[id]>=2;}
    int count(uint32_t frames)const{int n=0;for(int i=0;i<CUTS;++i)n+=valid(i,frames);return n;}
    int select(uint32_t frame,uint32_t frames)const{
        int found=-1;for(int i=0;i<CUTS;++i)if(valid(i,frames)&&frame>=at[i]&&frame<end(i,frames))found=i;return found;
    }
    int insert(uint32_t frame,uint32_t frames){
        if(frames<2||frame>frames-2)return -1;
        int free=-1;bool any=false;
        for(int i=0;i<CUTS;++i){if(at[i]==UINT32_MAX){if(free<0)free=i;continue;}
            any=true;if(at[i]>frame?at[i]-frame<2:frame-at[i]<2)return -1;}
        if(free<0)return -1;
        if(!any&&frame){at[free]=0;speed_q8[free]=256;reverse&=uint16_t(~(1u<<free));return insert(frame,frames);}
        at[free]=frame;speed_q8[free]=256;reverse&=uint16_t(~(1u<<free));return free;
    }
    bool move(int id,int64_t target,uint32_t frames){
        if(!valid(id,frames))return false;
        uint32_t lo=0,hi=frames-2;bool left=false;
        for(auto p:at){if(p<at[id]){lo=std::max(lo,p+2);left=true;}else if(p>at[id]&&p!=UINT32_MAX)hi=std::min(hi,p-2);}
        if(!left||hi<lo)return false;
        uint32_t v=uint32_t(std::max<int64_t>(lo,std::min<int64_t>(hi,target)));
        if(v==at[id])return false;
        at[id]=v;return true;
    }
    int merge(int id,uint32_t frames){
        if(!valid(id,frames))return -1;
        int left=-1;for(int i=0;i<CUTS;++i)if(valid(i,frames)&&at[i]<at[id]&&(left<0||at[i]>at[left]))left=i;
        if(left<0)return -1;
        at[id]=UINT32_MAX;speed_q8[id]=256;reverse&=uint16_t(~(1u<<id));return left;
    }
    static Cuts equal(int n,uint32_t frames){
        Cuts c;if(n<1||n>CUTS||frames<uint32_t(n*2))return c;
        for(int i=0;i<n;++i)c.at[i]=uint32_t(uint64_t(i)*frames/n);
        return c;
    }
};

// Every frame/channel is consumed, unlike the previous sparse 8-probe display.
// Caller copies small validated PCM blocks under guard and calls feed outside it.
template<int N> struct Envelope {
    uint32_t first=0,last=0,next=0;
    int16_t low[N]={},high[N]={};
    bool ready=false;
    void reset(uint32_t a,uint32_t b){first=next=a;last=b;ready=b<=a;
        for(int i=0;i<N;++i){low[i]=32767;high[i]=-32768;}}
    void feed(const int16_t* pcm,uint32_t start,uint32_t count,int channels){
        if(ready||start!=next||(channels!=1&&channels!=2))return;
        count=std::min(count,last-next);
        for(uint32_t f=0;f<count;++f){const int x=std::min(N-1,int(uint64_t(start+f-first)*N/(last-first)));
            for(int ch=0;ch<channels;++ch){auto v=pcm[size_t(f)*channels+ch];low[x]=std::min(low[x],v);high[x]=std::max(high[x],v);}}
        next+=count;ready=next==last;
    }
    void freeze(int8_t* lo,int8_t* hi,uint16_t gain)const{
        // At sample-level zoom, duplicate the nearest populated bin, not fake zero.
        int previous=-1;
        for(int i=0;i<N;++i){int from=i;
            if(low[i]>high[i]){if(previous>=0)from=previous;else{while(from<N&&low[from]>high[from])++from;}}
            else previous=i;
            if(from>=N){lo[i]=hi[i]=0;continue;}
            lo[i]=int8_t(std::max(-128,std::min(127,int(low[from])*gain/65536)));
            hi[i]=int8_t(std::max(-128,std::min(127,int(high[from])*gain/65536)));}
    }
};
struct Attacks {
    struct Point{uint32_t frame=0;uint16_t strength=0;};
    Point points[256]={};int count=0;uint32_t next=0,last=0;int previous=0,peak=0;uint32_t peak_frame=0;
    void reset(){*this={};}
    void feed(const int16_t* pcm,uint32_t start,uint32_t frames,int channels){
        if(start!=next)return;
        for(uint32_t f=0;f<frames;++f){int magnitude=0;
            for(int ch=0;ch<channels;++ch)magnitude=std::max(magnitude,std::abs(int(pcm[size_t(f)*channels+ch])));
            // Positive energy envelope change; 8ms detector bins, 24ms refractory.
            const int flux=std::max(0,magnitude-previous);previous+=(magnitude-previous)/32;
            if(flux>peak){peak=flux;peak_frame=start+f;}
            if(((start+f)&255u)==255u){if(peak>256&&peak_frame>=last+768){
                    Point p{peak_frame,uint16_t(peak)};
                    if(count<256)points[count++]=p;
                    else{int weak=0;for(int i=1;i<count;++i)if(points[i].strength<points[weak].strength)weak=i;
                        if(p.strength>points[weak].strength)points[weak]=p;}
                    last=peak_frame;}
                peak=0;}}
        next+=frames;
    }
    Cuts cuts(uint32_t frames,int sensitivity)const{
        Cuts c;c.insert(0,frames);Point chosen[256];std::copy(points,points+count,chosen);
        std::sort(chosen,chosen+count,[](Point a,Point b){return a.strength>b.strength;});
        const int threshold=256+(100-std::max(0,std::min(100,sensitivity)))*160;
        uint32_t sorted[15];int n=0;
        for(int i=0;i<count&&n<15;++i)if(chosen[i].strength>=threshold)sorted[n++]=chosen[i].frame;
        std::sort(sorted,sorted+n);for(int i=0;i<n;++i)c.insert(sorted[i],frames);return c;
    }
    uint32_t nearest(uint32_t at,uint32_t radius)const{
        uint32_t best=at,distance=radius+1;
        for(int i=0;i<count;++i){uint32_t p=points[i].frame,d=p>at?p-at:at-p;
            if(d<distance){best=p;distance=d;}}
        return best;
    }
};
} // namespace seance::play_editor
