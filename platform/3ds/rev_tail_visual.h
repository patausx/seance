#pragma once
// Abstract reflected-space contours, NOT PCM, an IR, spectrum or RT60 plot.
// Built once outside the audio guard. Only the copied wet peak envelope animates
// the shape; no UI clock, invented oscillation, private DSP, heap or texture.
#include "../../core/engine/unit.h"
#include <algorithm>
#include <type_traits>

namespace seance::platform::ui3ds::rev_tail_visual {
constexpr int MAX_STROKES=272;
enum Ink : uint8_t { Branch, Ghost, Solid, Accent };
enum class State : uint8_t { Running, Requested, Effective, Entering, Held, Leaving };
struct Stroke { uint8_t x0,y0,x1,y1,ink; }; // normalized 0..255 x 0..95
struct Plot {
    Stroke strokes[MAX_STROKES]{};
    uint16_t count=0;
    uint8_t strength=0;
    State state=State::Running;
};
static_assert(sizeof(Plot)<=1376 && std::is_trivially_copyable<Plot>::value,"bounded frozen REV geometry");
inline int clamp(int n,int lo,int hi) {return std::max(lo,std::min(n,hi));}
inline State freeze_state(bool requested,bool effective,q15 transition) {
    if(transition>0 && transition<32767)return effective?State::Entering:State::Leaving;
    if(transition==32767)return effective?State::Held:State::Leaving;
    return effective?State::Effective:requested?State::Requested:State::Running;
}
inline uint8_t strength(q15 energy) {
    // Square-root display law, not loudness. Exactly zero remains exactly zero.
    const unsigned target=unsigned(std::max(0,int(energy)))*65025u/32767u;
    unsigned lo=0,hi=255;
    while(lo<hi){const unsigned mid=(lo+hi+1)/2;if(mid*mid<=target)lo=mid;else hi=mid-1;}
    return uint8_t(lo);
}
inline void stroke(Plot& p,int x0,int y0,int x1,int y1,Ink ink) {
    x0=clamp(x0,0,255);x1=clamp(x1,0,255);y0=clamp(y0,0,95);y1=clamp(y1,0,95);
    if((x0==x1 && y0==y1) || p.count>=MAX_STROKES)return;
    p.strokes[p.count++]={uint8_t(x0),uint8_t(y0),uint8_t(x1),uint8_t(y1),uint8_t(ink)};
}
inline void build(q15 size,q15 decay,q15 damp,q15 energy,bool modern,bool bypass,
                  bool requested,bool effective,q15 transition,Plot& out) {
    out={};out.state=freeze_state(requested,effective,transition);
    out.strength=bypass?0:strength(energy);
    const int s=std::max(0,int(size)),d=std::max(0,int(decay)),dark=std::max(0,int(damp));
    // Tabulated unit circle: fixed 24 vertices, no trigonometry in capture/draw.
    constexpr int sn[25]={0,265,512,724,887,989,1024,989,887,724,512,265,0,-265,-512,-724,-887,-989,-1024,-989,-887,-724,-512,-265,0};
    constexpr int cs[25]={1024,989,887,724,512,265,0,-265,-512,-724,-887,-989,-1024,-989,-887,-724,-512,-265,0,265,512,724,887,989,1024};
    const int rings=modern?9:6;
    const int held=std::max(0,int(transition));
    const int extent=120+(d+(32767-d)*held/32767)*58/32767;
    const int aperture=17+s*15/32767+out.strength*12/255;
    int retain=154+d*94/32767;
    retain+=(256-retain)*std::max(0,int(transition))/32767;
    int fall=256;
    for(int i=0;i<rings;++i) {
        const int cx=33+i*extent/(rings-1),cy=48;
        const int h=std::max(2,aperture*fall/256);
        const int w=std::max(3,(19+s*8/32767)*fall/256);
        const int lean=h*(modern?10:5)/24;
        auto point=[&](int j,int& x,int& y) {
            x=cx+(cs[j]*w+sn[j]*lean)/1024;
            // Damping softens the upper contour only: a visual metaphor, not
            // a claimed frequency-response trace or separately measured band.
            const int height=sn[j]<0?h*(32767-dark/3)/32767:h;
            y=cy+sn[j]*height/1024;
        };
        const Ink ink=bypass||!out.strength?Branch:
            ((i==1 || i==2) && out.strength>12)?Accent:i==0?Solid:i<5?Ghost:Branch;
        for(int j=1;j<=24;++j) {
            // An opening in ordinary contours; captured space closes gradually.
            if(j>=4 && j<=6 && transition<q15((7-j)*8000))continue;
            int x0,y0,x1,y1;point(j-1,x0,y0);point(j,x1,y1);
            stroke(out,x0,y0,x1,y1,ink);
        }
        fall=fall*retain/256;
    }
    // Quiet retained shape is intentional, but it never glows without actual
    // wet-tail energy. Held captures have no implied new excitation arrow.
    if(out.state==State::Running && out.strength) {
        const int height=3+out.strength*9/255;
        stroke(out,9,48-height,14,48+height,Accent);
        stroke(out,15,48-height/2,18,48+height/2,Ghost);
    }
}
} // namespace seance::platform::ui3ds::rev_tail_visual
