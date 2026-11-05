#pragma once
// Pure waveform-window geometry/projection. No engine, clock, gesture ownership,
// saved values or drawing state; the caller supplies the CURRENT underlying span.
#include <algorithm>
#include <cstdint>

namespace seance::platform::ui3ds::fract_window {
struct Rect { int x, y, w, h; };
constexpr Rect kTouch{14,35,292,76};
constexpr Rect kHoldTouch{236,113,72,20};
constexpr Rect kWave{20,41,280,63};
constexpr Rect kHistory{20,60,360,56};
constexpr Rect kReader{20,158,360,37};
constexpr int kBracketTop=38, kBracketBottom=107;
constexpr int kLabelY=118, kFullRight=kWave.x+kWave.w-1;
constexpr int kTravel=kWave.w-1, kDeadzone=4;
static_assert(kTravel==279 && kFullRight==299);
static_assert(kTouch.y+kTouch.h<=kHoldTouch.y && kHoldTouch.y+kHoldTouch.h<=136);

inline int right(uint32_t frames,uint32_t span,int x,int width) {
    if(!span || width<=1)return x;
    return x+int(uint64_t(std::min(frames,span))*uint32_t(width-1)/span);
}

inline int fraction(int tx,int origin_x,uint32_t underlying,uint32_t min_frames,bool moved) {
    // DOWN and stationary jitter always request the full span, regardless of
    // where the hand first landed. Empty history cannot invent a window length.
    if(!moved || !underlying)return 32767;
    const int minimum=right(std::min(min_frames,underlying),underlying,kWave.x,kWave.w);
    if(minimum>=kFullRight)return 32767; // a damaged span below the floor is indivisible
    const int64_t desired=int64_t(kFullRight)+int64_t(tx)-origin_x;
    const int boundary=int(std::max<int64_t>(minimum,std::min<int64_t>(kFullRight,desired)));
    return (boundary-minimum)*32767/(kFullRight-minimum);
}
} // namespace seance::platform::ui3ds::fract_window
