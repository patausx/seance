#pragma once
#include <cstdint>

namespace seance::platform::ui3ds::seq_cuts {
constexpr int X0=24, X1=295, WAVE_TOP=48, WAVE_BOTTOM=119;
// View state only. No unit/sample pointers, no added engine/patch fields.
struct Frame {
    bool active=false, add_armed=false, busy=false;
    uint8_t selected=0, confirm=0; // confirm: 4/8/16 auto, 255 clear
    int slot=-1, sample=-1;
    uint16_t present=0;
    uint32_t chops[16]={}, frames=0;
    uint32_t selected_begin=0, selected_end=0;
};
static_assert(sizeof(Frame)<=104, "bounded cuts inspector");
}
namespace seance::platform::ui3ds {
const seq_cuts::Frame& sequencer_cuts();
}
