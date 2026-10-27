#pragma once
// Input-envelope relief, NOT a spectrum. UI-only: raw magnitude history, no
// oscillator, phase, random ridges, normalization, heap or audio-state changes.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
namespace seance::platform::ui3ds::mic_relief {
constexpr int ROWS=15, POINTS=65, SAMPLES=1048, COLUMNS=349;
constexpr uint64_t INTERVAL_MS=32, STALE_MS=150;
constexpr uint8_t HIDDEN=255;
enum class Status : uint8_t { Waiting, Live, Stale, Unavailable };
inline int width(int row){return 240+108*row/(ROWS-1);}
inline int left(int row){return 200-width(row)/2+(ROWS-1-row)*13/(ROWS-1);}
inline int baseline(int row){return 105+79*row/(ROWS-1);}
inline uint32_t root(uint32_t v){ // floor sqrt, bounded sixteen iterations
    uint32_t n=0,bit=1u<<30;
    for(int i=0;i<16;++i,bit>>=2){if(v>=n+bit){v-=n+bit;n=(n>>1)+bit;}else n>>=1;}
    return n;
}
struct Plot { uint8_t y[ROWS][COLUMNS]{}; };
struct History {
    uint16_t rows[ROWS][POINTS]{}; // fixed-scale magnitude, old -> new
    uint32_t serial=0;
    uint64_t clock=0,arrival=0,inserted=0;
    bool seeded=false,received=false,has_row=false;
    Status status=Status::Waiting;
    void reset(){*this=History{};}
    void update(const int16_t* pcm,int count,uint32_t next,uint64_t now,bool device){
        count=std::max(0,std::min(SAMPLES,count));
        if(!device){reset();status=Status::Unavailable;return;}
        if(!seeded||now<clock){reset();seeded=true;serial=next;clock=now;return;}
        clock=now;
        const bool changed=next!=serial;serial=next;
        const bool expired=received && now-arrival>=STALE_MS;
        if(expired){std::memset(rows,0,sizeof rows);has_row=false;}
        if(changed){
            arrival=now;received=count>0&&pcm;
            if(!received){std::memset(rows,0,sizeof rows);has_row=false;status=Status::Waiting;return;}
            status=Status::Live;
            if(has_row&&now-inserted<INTERVAL_MS)return;
            inserted=now;has_row=true;
            uint16_t bins[POINTS]{};
            const int missing=SAMPLES-count;
            for(int p=0;p<POINTS;++p){
                const int a=p*SAMPLES/POINTS,b=(p+1)*SAMPLES/POINTS;
                uint32_t sum=0;
                for(int i=std::max(a,missing);i<b;++i){const int v=pcm[i-missing];sum+=uint32_t(v<0?-v:v);}
                bins[p]=sum/(b-a); // zero-pad only the missing prefix, no autoscale
            }
            std::memmove(rows[0],rows[1],sizeof rows-sizeof rows[0]);
            for(int p=0;p<POINTS;++p){
                uint32_t sum=0;
                for(int j=-2;j<=2;++j)sum+=bins[std::max(0,std::min(POINTS-1,p+j))]*(3-std::abs(j));
                // Fixed perceptual sqrt law + fixed edge taper. Purely visual,
                // not a dB scale and never used by gain/low-cut/recording.
                const uint32_t mag=root((sum/9)*32768u);
                const int taper=std::min(6,std::min(p,POINTS-1-p));
                rows[ROWS-1][p]=mag*taper/6;
            }
        }else if(expired)status=Status::Stale;
    }
    void project(Plot& out) const {
        std::memset(out.y,HIDDEN,sizeof out.y);
        if(status!=Status::Live)return;
        uint8_t horizon[400];std::memset(horizon,193,sizeof horizon);
        // Front-to-back horizon clipping leaves the exact existing paper visible;
        // it never paints fake solid rectangles over the textured material.
        for(int r=ROWS-1;r>=0;--r){const int w=width(r),x0=left(r),height=32+12*r/(ROWS-1);
            for(int x=0;x<=w;++x){const int q=x*(POINTS-1),p=q/w,part=q%w;
                const uint32_t mag=(uint32_t(rows[r][p])*(w-part)+uint32_t(rows[r][std::min(p+1,POINTS-1)])*part)/w;
                const uint8_t y=baseline(r)-(mag*height+16384)/32768;
                if(y<horizon[x0+x])out.y[r][x]=y;
                horizon[x0+x]=std::min(horizon[x0+x],y);
            }
        }
    }
};
static_assert(sizeof(Plot)==5235 && sizeof(History)<2100,"bounded UI-only relief storage");
} // namespace seance::platform::ui3ds::mic_relief
