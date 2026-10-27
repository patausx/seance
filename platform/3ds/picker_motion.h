#pragma once
// Picker-local UI motion. Time is sampled by input/capture, never either draw.
// No input lock: a fresh touch snaps before hit-testing; A snaps once, B reverses.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace seance::platform::ui3ds::picker_motion {
struct State {
    float progress=0, from=0;
    uint64_t began=0;
    bool active=false, closing=false;
};
inline State state;
inline void reset() { state={}; }
inline void begin(uint64_t now) { state={0,0,now,true,false}; }
inline bool sample(uint64_t now) {
    if(!state.active)return false;
    const float elapsed=float(now<state.began?0:std::min<uint64_t>(1000,now-state.began));
    if(state.closing) {
        state.progress=std::max(0.f,state.from-elapsed/160.f);
        if(state.progress<=0){state.active=false;return true;}
    } else state.progress=std::min(1.f,state.from+elapsed/200.f);
    return false;
}
inline void snap() { if(state.active&&!state.closing)state.progress=state.from=1; }
inline void close(uint64_t now) {
    if(!state.active||state.closing)return;
    sample(now); state.from=state.progress;state.began=now;state.closing=true;
}
inline float smooth(float t) { t=std::clamp(t,0.f,1.f);return t*t*(3-2*t); }
struct Bounds { int left,top,right,bottom; };
inline Bounds bounds(Bounds origin,float progress,bool top) {
    const float t=std::clamp(progress,0.f,1.f),r=1-t,f=1-r*r*r;
    const int width=int(std::lround(origin.right-origin.left+1+(280-(origin.right-origin.left+1))*f));
    const float center=(origin.left+origin.right)*.5f+(159.5f-(origin.left+origin.right)*.5f)*f;
    const int left=int(std::lround(center-(width-1)*.5f));
    return {left+(top?40:0),top?int(std::lround(40-32*f)):int(std::lround(origin.top*(1-f))),
            left+width-1+(top?40:0),top?239:int(std::lround(origin.bottom+(216-origin.bottom)*f))};
}
} // namespace seance::platform::ui3ds::picker_motion
