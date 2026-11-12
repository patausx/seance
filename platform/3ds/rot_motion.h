#pragma once
#include <cstdint>
namespace seance::platform::ui3ds::rot_motion {
// Circular reader positions are observations, not a monotonic sample counter.
// A real wrap in HOLD is fresh; repeated identical captures are not. Wall time
// only expires the SAME 100ms source lease, never creates motion or a future pass.
struct Sample {bool fresh=false,live=false;};
struct State {
    uint32_t serial=0,phase=0;uint16_t drop=0;
    uint64_t arrival_ms=0,observed_ms=0;bool observed=false,leased=false;
    void reset(){*this={};}
    Sample capture(uint32_t next_serial,uint32_t next_phase,uint16_t next_drop,uint64_t now,bool accepted){
        if(!observed||now<observed_ms){serial=next_serial;phase=next_phase;drop=next_drop;observed_ms=now;observed=true;leased=false;return {};}
        const bool fresh=serial!=next_serial||phase!=next_phase||drop!=next_drop;
        serial=next_serial;phase=next_phase;drop=next_drop;observed_ms=now;
        if(fresh){arrival_ms=now;leased=accepted;}
        if(!accepted||now-arrival_ms>=100)leased=false;
        return {fresh,leased&&accepted};
    }
};
}
