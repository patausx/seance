#pragma once
// Presentation only: the approved text-motion study, evaluated on native ink.
// No geometry displacement, allocations, clock reads, RNG, engine or audio work.
#include <cstdint>
namespace seance::platform::ui3ds::cabinet_ink {
constexpr unsigned kLoopMs=16000;
constexpr int kPulseMs=520, kFocusMs=180;
constexpr float kPi=3.14159265358979323846f;
inline float clamp(float x) { return x<0?0:x>1?1:x; }
inline float smooth(float x) { x=clamp(x);return x*x*(3-2*x); }
// Periodic smooth sine approximation, max absolute error <0.00111. Bounded
// internal arguments (<128 radians); avoids a transcendental per ink sample.
inline float sine(float radians) {
    const float turns=(radians+kPi)/(2*kPi);
    int whole=int(turns);if(float(whole)>turns)--whole;
    const float x=(turns-float(whole))*(2*kPi)-kPi;
    const float ax=x<0?-x:x;
    const float y=(4/kPi)*x-(4/(kPi*kPi))*x*ax;
    const float ay=y<0?-y:y;
    return y+.225f*(y*ay-y);
}
struct Clock {
    unsigned ms=0;
    int pulse_age[3]={kPulseMs,kPulseMs,kPulseMs};
    void tick(unsigned dt) {
        ms=(ms+dt%kLoopMs)%kLoopMs;
        for(int& age:pulse_age) {
            const unsigned remaining=unsigned(kPulseMs-age);
            age+=int(dt<remaining?dt:remaining);
        }
    }
    // A rapid second accepted open carries on its still-visible pulse instead
    // of snapping that heading's tint back to phase zero. Idle pulses restart.
    void pulse(int row) {
        if(row>=0&&row<3&&pulse_age[row]>=kPulseMs)pulse_age[row]=0;
    }
};
struct Shade { uint8_t tint,alpha; };
inline Shade heading(const Clock& clock,int row,int x,int y,bool edge,int focus) {
    const float phase=(2*kPi/8000)*float(clock.ms);
    const float a=.5f+.5f*sine(x*.056f-y*.065f-phase+row*1.27f);
    const float pool=a*a;
    const float second=.5f+.5f*sine(x*.095f+y*.11f+phase*.5f+row*2.1f);
    const float density=edge?1-.24f*pool-.04f*second:1-.10f*pool;
    const float f=float(focus)/255;
    float tint=(.06f+.20f*pool)*(1-f)+f*(.57f+.43f*second);
    if(clock.pulse_age[row]<kPulseMs)
        tint-=.48f*sine(kPi*float(clock.pulse_age[row])/kPulseMs)*pool;
    // A fixed 16-step tint and 32-step density let adjacent native pixels share
    // runs; no temporally adaptive dithering or changes to the bitmap contour.
    const int t=int(clamp(tint)*15+.5f)*17;
    int alpha=int(clamp(density)*255/8+.5f)*8;if(alpha>255)alpha=255;
    return {uint8_t(t),uint8_t(alpha)};
}
inline int row_at(int panel,int y) {
    const int r=(y-28)/(panel==0?36:44),last=panel==0?5:2;
    return r<0?0:r>last?last:r;
}
inline int body_alpha(const Clock& clock,int panel,int x,int y) {
    const float phase=(2*kPi/8000)*float(clock.ms);
    const float w=.5f+.5f*sine(x*.052f-y*.035f-phase+row_at(panel,y)*.7f);
    return int((1-.07f*w*w)*255+.5f);
}
inline float rank(int panel,int x,int y) {
    return .38f+.16f*sine(x*.135f+row_at(panel,y)*.83f)
          +.13f*sine(y*.22f-x*.035f+kPi*.5f)+.07f*sine(x*.041f+y*.11f);
}
static_assert(sizeof(Clock)<=16,"one bounded phase and three accepted-open envelopes");
} // namespace seance::platform::ui3ds::cabinet_ink
