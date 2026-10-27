#pragma once
// UI-only phase. One capture advances both LCDs; drawing never reads a clock.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace seance::platform::ui3ds::editor_transition {
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
    const float age=float(now<state.began?0:std::min<uint64_t>(1000,now-state.began));
    if(state.closing)state.progress=std::min(state.progress,std::max(0.f,state.from-age/160.f));
    else state.progress=std::max(state.progress,std::min(1.f,state.from+age/200.f));
    return state.closing&&state.progress<=0;
}
inline void snap() { if(state.active&&!state.closing)state.progress=state.from=1; }
inline void close(uint64_t now) {
    if(!state.active||state.closing)return;
    sample(now);state.from=state.progress;state.began=now;state.closing=true;
}
inline float smooth(float t) { t=std::clamp(t,0.f,1.f);return t*t*(3-2*t); }
struct Bounds { int left,top,right,bottom; };
inline Bounds bounds(Bounds origin,float progress,bool top) {
    const float r=1-std::clamp(progress,0.f,1.f),t=1-r*r*r;
    // One virtual 400px sheet. The lower LCD crops +40: sides leave its viewport,
    // rather than compressing the editor to the 280px picker or stretching text.
    const int left=int(std::lround(origin.left+(-40-origin.left)*t));
    const int right=int(std::lround(origin.right+(359-origin.right)*t));
    return {left+(top?40:0),top?int(std::lround(40*(1-t))):0,
            right+(top?40:0),top?239:int(std::lround(origin.bottom+(239-origin.bottom)*t))};
}
} // namespace seance::platform::ui3ds::editor_transition
