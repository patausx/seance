#pragma once
#include "../../core/engine/engine.h"
#include "../../core/engine/paul_unit.h"
#include <algorithm>
#include <type_traits>
#include "paul_visual_qol.h"
namespace seance::platform::ui3ds::paul_ui {
struct Box { int x,y,w,h; bool has(int px,int py) const {return px>=x&&px<x+w&&py>=y&&py<y+h;} };
inline constexpr Box source{76,8,162,24},setup{252,8,54,24},freeze{222,36,84,28};
inline constexpr Box direction{116,36,100,28},setup_direction{190,36,116,28};
inline constexpr Box strip{14,72,292,38},stretch{14,124,292,36},window{14,170,292,36};
inline constexpr Box mod{14,216,80,24},back{226,216,80,24},flow{14,36,80,28},gate{102,36,80,28};
inline constexpr Box parameter(int p) {return {14+(p%2)*156,66+(p/2)*30,136,29};}
// Same source-time coordinate and binding on both screens. Actual PCM support is
// inclusive; neither position nor window is reconstructed from current knobs.
inline int project_source(int64_t q16,uint32_t frames,int left,int width) {
    if(!frames||width<1||q16<0||q16>int64_t(frames-1)*65536)return -1;
    return frames==1?left:left+int(q16*(width-1)/(int64_t(frames-1)*65536));
}
inline int parameter_x(q15 v,bool bipolar,int left,int travel) {
    return left+(std::clamp(int(v),bipolar?-32767:0,32767)+(bipolar?32767:0))*travel/(bipolar?65534:32767);
}
struct Frame {
    bool valid=false,loaded=false,bypass=false,mute=false,lens=false,busy=false,direction_disabled=true;
    uint8_t mode=0,direction=0,page=0,selected=0;int chain=0,slot=0,sample_slot=-1;
    uint32_t generation=0,frames=0;
    PaulUnit::Visual visual{};
    q15 edit[PaulUnit::P_COUNT]={};ParamInspector params[PaulUnit::P_COUNT]={};
    int top_cursor=-1,bottom_cursor=-1,window_left=-1,window_right=-1;
    uint8_t low[372]={},high[372]={},spectrum[372]={};
    char source_name[28]={},values[PaulUnit::P_COUNT][24]={},duration[24]={},position[24]={};
};
static_assert(sizeof(Frame)<4096&&std::is_trivially_copyable<Frame>::value,"bounded pointer-free PAUL pair");
const Frame& frozen();
#ifdef SEANCE_PAUL_UI_TEST
QolStats test_qol_stats();
#endif
}
