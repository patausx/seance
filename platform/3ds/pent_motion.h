#pragma once
#include <cstdint>
namespace seance::platform::ui3ds::pent_motion {
// Retiming of the selected 24 x 80ms sheet, NOT oscillator/PM telemetry.
// Only actual accepted source samples select frames. Wall time expires a lease;
// it never extrapolates motion when NDSP/Engine stops producing audio.
inline constexpr uint64_t samples_per_art = 2560; // 80ms at the native 32kHz
inline constexpr uint64_t stale_ms = 100; // 16ms NDSP blocks + bounded jitter
inline constexpr uint8_t quiet_art = 15;
struct Sample { bool fresh=false,live=false;uint8_t art=quiet_art; };
struct State {
    uint64_t frames=0,arrival_ms=0,observed_ms=0;
    bool observed=false,leased=false;
    void reset(){*this={};}
    Sample capture(uint64_t next,uint64_t now,bool accepted){
        // First sight of a nonzero cached counter is NOT evidence of new audio.
        // Context/lifecycle/width entry must first baseline, then see advancement.
        if(!observed||next<frames||now<observed_ms){
            frames=next;observed_ms=now;observed=true;leased=false;return {};
        }
        const bool fresh=next>frames;
        frames=next;observed_ms=now;
        if(fresh){leased=accepted;arrival_ms=now;}
        if(!accepted||now-arrival_ms>=stale_ms)leased=false;
        const bool live=leased&&accepted;
        return {fresh,live,live?uint8_t((next/samples_per_art+quiet_art)%24):quiet_art};
    }
};
} // namespace seance::platform::ui3ds::pent_motion
