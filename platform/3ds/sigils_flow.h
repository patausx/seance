#pragma once
// Selected sigils upper: captured modulation contour, not PCM or sound latency.
// Only the first saved destination is projected; the total count stays in the
// existing editor frame. No new timeline, texture or live draw pointers.
#include "sigil_surface.h"
#include "sigils_soft_upper_arc.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace seance::platform::ui3ds::sigils_flow {
enum class TargetKind : uint8_t { None, Module, Clock, Rate };
enum class RouteState : uint8_t { On, Off, Zero, Mute, Bypass, Suspended };
struct Target {
    char module[10]={},param[23]={},identity[12]={};
    int16_t effective=0;
    TargetKind kind=TargetKind::None;
    RouteState state=RouteState::On;
};
struct Frame {
    int16_t y[97]={};
    Target target;
    uint8_t count=0;
    bool valid=false,source_active=false,connected=false;
};
static_assert(sizeof(Frame)<=256,"bounded sigil flow addition");
static_assert(std::is_trivially_copyable<Frame>::value,"frozen flow has no pointers");

// Capture-only spatial projection of the existing physical-phase oracle.
// phase + distance travels right as actual Engine phase grows. The quadratic
// contraction stretches the contour into its destination without a new clock.
// The final point always equals the actual captured output, even for stochastic
// shapes. Those shapes remain a held scalar: no future RNG or invented cycle.
template<class Source> void project(Frame& out,const Source& source) {
    if(!out.valid)return;
    out.connected=out.source_active&&source.route_count&&out.target.kind==TargetKind::Module&&
        out.target.state==RouteState::On;
    out.count=source.periodic?97:2;
    for(unsigned i=0;i<out.count;++i){
        const unsigned n=out.count-1,remaining=n-i;
        int value=source.value;
        if(source.periodic&&i<n){
            const uint64_t distance=uint64_t(remaining)*remaining*5*(uint64_t(1)<<30)/(uint64_t(n)*n);
            const uint32_t phase=source.phase+uint32_t(distance);
            const uint64_t address=uint64_t(phase)*127;
            const unsigned at=unsigned(address>>32),fraction=unsigned((address>>16)&65535);
            const int raw=int(source.wave[at])+(int64_t(int(source.wave[at+1])-source.wave[at])*fraction/65536);
            value=(raw*int(remaining)+int(source.value)*int(i))/int(n);
        }
        out.y[i]=int16_t(126-value*42/32767);
    }
}

inline const char* route_status(const Frame& f,unsigned routes) {
    if(!f.valid)return "context suspended";
    if(!routes)return "no destination / open sigilbay";
    switch(f.target.state){
        case RouteState::Off:return "route off";
        case RouteState::Zero:return "route depth zero";
        case RouteState::Mute:return "chain muted";
        case RouteState::Bypass:return "module bypass";
        case RouteState::Suspended:return "destination suspended";
        default:return f.source_active?"":"source silent / route retained";
    }
}

// Exact selected flow composition: open paper, moving arc into a native module
// heading/parameter. Draw reads captured geometry and palette only.
inline void draw(const sigil_surface::TopPainter& p,const Frame& f,unsigned sigil,
                 unsigned routes,const char* source_status) {
    namespace m=menu_style;
    char text[48];
    std::snprintf(text,sizeof text,"%c / %s",'a'+sigil,source_status);
    p.text(28,74,text,m::ghost());
    if(f.valid)soft_upper::arc(f);
    if(f.valid&&routes){
        const auto& t=f.target;
        p.text(284,60,t.identity,m::ghost());
        p.heading(284,83,t.module,m::ink());
        if(std::strlen(t.param)>16)std::snprintf(text,sizeof text,"%.13s...",t.param);
        else std::snprintf(text,sizeof text,"%s",t.param);
        p.text(284,122,text,m::ink());
        if(t.kind==TargetKind::Module){
            std::snprintf(text,sizeof text,"live %d%%",int(t.effective)*100/32767);
            p.text(284,150,text,m::ghost());
        }else p.text(28,173,t.kind==TargetKind::Clock?"clock route / use clock view":"rate route / use sigil view",m::ghost());
    }
    // Valid-state/link content is frozen by soft_upper; invalid context stays here.
    if(!f.valid)p.text(28,188,route_status(f,routes),m::ghost());
}
} // namespace seance::platform::ui3ds::sigils_flow
