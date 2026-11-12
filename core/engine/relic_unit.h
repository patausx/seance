#pragma once
#include "unit.h"
#include "relic_audio.h" // bounded observation types only; no Audio instance
namespace seance {
// Retired API observation envelope, not a Unit or a runtime replacement.
// Kept for cross-controller cancellation/observation signatures only.
class RelicUnit final {
public:
    RelicUnit() = delete;
    enum { P_GAIN=0,P_DEPTH,P_START,P_LENGTH,P_COUNT };
    struct Visual {
        relic::Surface surface;
        relic::Visual audio;
        uint32_t instance_id=0,bank_revision=0;
        uint64_t control_epoch=0;
        q15 ports[4]={};
        bool valid=false,own_audio_enabled=false,contributing=false,pipe_admitted=false;
        bool sound_requested=false,sound_running=false; // actual Unit admission, not parked internal ramp
        q15 output_gain=0; // recipe audio.energy precedes this ordinary gain
    };
};
static_assert(sizeof(RelicUnit::Visual)<4608,"focused only relic observation");
} // namespace seance
