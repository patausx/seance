#pragma once
#include "../../core/engine/units.h"
#include "../../core/engine/engine.h"
#include "word_menu.h"
#include <algorithm>
#include <type_traits>

namespace seance::platform::ui3ds::scratch_ui {
struct Box {
    int x,y,w,h;
    bool has(int px,int py) const {return px>=x&&px<x+w&&py>=y&&py<y+h;}
};
inline constexpr Box field{14,78,292,128},source{76,8,162,24},setup{252,8,54,24};
inline constexpr Box scratch{14,36,80,24},hold{102,36,72,24},mapping{194,36,112,24};
inline constexpr Box speed{194,36,48,24},position{246,36,60,24};
inline constexpr Box mod{14,216,80,24},latch{112,216,102,24},back{226,216,80,24};
inline constexpr Box axis(int i) {return {14+i*156,62,136,16};}
inline constexpr Box parameter(int i) {return {14+(i%2)*156,80+(i/2)*34,136,32};}
// Both screens use this SAME source-time projection. An invisible/no-voice
// witness is absent, never a fabricated clipped-edge playback line.
inline int project_source(int64_t q16,uint32_t first,uint32_t last,int left,int width) {
    if(width<1||last<first||q16<int64_t(first)*65536||q16>int64_t(last)*65536)return -1;
    if(last==first)return left;
    return left+int((q16-int64_t(first)*65536)*(width-1)/(int64_t(last-first)*65536));
}
struct Frame {
    bool valid=false,loaded=false,bypass=false,mute=false,lock=false,lens=false,gesture=false;
    bool latched=false,latch_available=false,gesture_failed=false;
    uint8_t mode=0,map=0,page=0,selected=0;
    int axis_x=ScratchUnit::P_SPEED,axis_y=ScratchUnit::P_PITCH;
    int menu_axis=-1,menu_count=0,menu_selected=0,menu_params[5]={};
    word_menu::Layout menu_layout{};
    word_menu::Frame menu_motion{};
    char axis_labels[2][24]={};
    int chain=0,slot=0,sample_slot=-1;
    uint32_t generation=0,frames=0,view_first=0,view_last=0;
    ScratchUnit::Visual visual{};
    q15 edit[ScratchUnit::P_COUNT]={};
    ParamInspector params[ScratchUnit::P_COUNT]={};
    q15 origin_x=0,origin_y=0;
    int top_cursor=-1,bottom_cursor=-1,window_left=-1,window_right=-1;
    uint8_t trace_count=0,trace_alpha[12]={};
    q15 trace_x[12]={},trace_y[12]={};
    bool trace_join[12]={};
    uint8_t low[372]={},high[372]={};
    char source_name[28]={},values[ScratchUnit::P_COUNT][24]={};
};
static_assert(sizeof(Frame)<2048&&std::is_trivially_copyable<Frame>::value,"bounded shared SCRT frame, no engine or PCM pointers");
// Read-only frozen production DTO; never a second capture or independent model.
const Frame& frozen();
}
