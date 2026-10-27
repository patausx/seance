#pragma once
// Four abstract memory imprints, not PCM, a spectrum or separate well meters.
// Pure fixed-size projection after the audio guard; no clock, heap or DSP work.
#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace seance::platform::ui3ds::ghost_memory_visual {
constexpr int WELLS=4, ECHOES=3, SEGMENTS=32, EXTENT=1024;
struct Point { int16_t x=0,y=0; };
struct Geometry {
    Point points[WELLS*ECHOES][SEGMENTS+1]{};
    uint8_t strength=0;
    bool quiet=true,sealed=false;
};
static_assert(sizeof(Geometry)<=1600 && std::is_trivially_copyable<Geometry>::value,
              "bounded frozen ghost geometry");
inline int clamp(int n,int lo,int hi){return std::max(lo,std::min(n,hi));}
inline uint8_t strength(int energy) {
    // Display-only square root. Silence stays zero; no loudness/RT60 claim.
    const unsigned target=unsigned(clamp(energy,0,32767))*65025u/32767u;
    unsigned lo=0,hi=255;
    while(lo<hi){const unsigned mid=(lo+hi+1)/2;if(mid*mid<=target)lo=mid;else hi=mid-1;}
    return uint8_t(lo);
}
inline void project(int energy,int freeze,int drift,bool quiet,Geometry& out) {
    out={};
    const int held=clamp(freeze,0,32767),motion=clamp(drift,-32767,32767);
    out.strength=quiet?0:strength(energy);
    out.quiet=!out.strength;out.sealed=held==32767;
    // As the actual freeze ramp seals, the outline settles to one fixed imprint.
    // The real energy still controls ink, not a fabricated frozen motion clock.
    const int pose_strength=out.strength+(255-out.strength)*held/32767;
    // The approved asymmetric poses, in 1/1024 normalized coordinates. All
    // contours share ONE measured envelope/drift; sizes are authored, not meters.
    constexpr int cx[4]={123,297,748,922},cy[4]={543,502,512,563};
    constexpr int rw[4]={48,56,54,44},rh[4]={307,440,389,287},lean[4]={23,33,-29,-16};
    constexpr int sn[33]={0,200,392,569,724,851,946,1004,1024,1004,946,851,724,569,392,200,0,-200,-392,-569,-724,-851,-946,-1004,-1024,-1004,-946,-851,-724,-569,-392,-200,0};
    constexpr int cs[33]={1024,1004,946,851,724,569,392,200,0,-200,-392,-569,-724,-851,-946,-1004,-1024,-1004,-946,-851,-724,-569,-392,-200,0,200,392,569,724,851,946,1004,1024};
    for(int well=0;well<WELLS;++well)for(int echo=0;echo<ECHOES;++echo) {
        const int scale=1024-echo*108;
        const int bend=lean[well]*(32767-held*72/100)/32767
            + (well<2?1:-1)*motion*13/32767*(32767-held)/32767;
        const int width=rw[well]*(224+pose_strength*32/255)/256;
        const int height=rh[well]*(240+pose_strength*16/255)/256;
        auto& points=out.points[well*ECHOES+echo];
        for(int j=0;j<SEGMENTS;++j) {
            const int asymmetric=width*sn[j]/1024*(1024+cs[j]*16/100)/1024;
            points[j].x=int16_t(cx[well]+echo*(well<2?10:-10)
                +(asymmetric+bend*cs[j]/1024)*scale/1024);
            points[j].y=int16_t(cy[well]+echo*28+height*cs[j]/1024*scale/1024);
        }
        // Exact closure at every energy/drift/freeze endpoint, not a near-circle.
        points[SEGMENTS]=points[0];
    }
}
} // namespace seance::platform::ui3ds::ghost_memory_visual
