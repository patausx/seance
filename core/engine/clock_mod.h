// persistent physical-unit global clock routes; runtime inspector is not patch data.
#pragma once
#include "unit.h"
namespace seance {
enum class ClockTarget : uint8_t { Tempo = 0, Swing = 1, Count };
constexpr int CLOCK_TEMPO_DEPTH_MAX = 280;
constexpr int CLOCK_SWING_DEPTH_MAX = 25;
struct ClockMod { ModSource src = ModSource::None; int16_t depth = 0; };
struct ClockInspector {
    ClockMod mod;
    int base = 0;
    q15 source = 0;
    int32_t target_q16 = 0, applied_q16 = 0;
    bool limited = false;
};
} // namespace seance
