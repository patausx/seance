#pragma once
#include "../../core/engine/engine.h"
#include "../../core/engine/pent_unit.h"
#include "word_menu.h"
#include "pent_reactive.h"
#include <algorithm>
#include <type_traits>
namespace seance::platform::ui3ds::pent_ui {
struct Box {int x,y,w,h;bool has(int px,int py)const{return px>=x&&px<x+w&&py>=y&&py<y+h;}};
// All twelve musical parameters are inline. Only the shared rational palette
// and explicit channel width disclose; MOD belongs to the physical left trigger.
inline constexpr Box presets{14,216,54,24},width{250,6,56,20},back{226,216,80,24};
inline constexpr Box voice(int i){return {14+i*58,68,48,20};}
inline constexpr Box ratio(int i){return {14+i*58,88,48,28};}
inline constexpr bool is_ratio(int p){return p>=PentUnit::P_RATIO_A&&p<=PentUnit::P_RATIO_E;}
inline constexpr Box main_parameter(int p){return p==PentUnit::P_ROOT?Box{14,36,130,30}:
    p==PentUnit::P_GAIN?Box{174,36,132,30}:p==PentUnit::P_SPREAD?Box{14,119,130,40}:
    p==PentUnit::P_BOND?Box{174,119,132,40}:p==PentUnit::P_DRIFT?Box{14,168,64,40}:
    p==PentUnit::P_DRIFT_DEPTH?Box{94,168,64,40}:p==PentUnit::P_DRIVE?Box{174,168,132,40}:
    is_ratio(p)?ratio(p-PentUnit::P_RATIO_A):Box{0,0,0,0};}
inline constexpr int rail_left(int p){return is_ratio(p)?20+(p-PentUnit::P_RATIO_A)*58:
    p==PentUnit::P_DRIFT_DEPTH?100:p==PentUnit::P_BOND||p==PentUnit::P_DRIVE||p==PentUnit::P_GAIN?180:20;}
inline constexpr int rail_right(int p){return is_ratio(p)?56+(p-PentUnit::P_RATIO_A)*58:
    p==PentUnit::P_DRIFT?72:p==PentUnit::P_DRIFT_DEPTH?152:
    p==PentUnit::P_BOND||p==PentUnit::P_DRIVE||p==PentUnit::P_GAIN?300:138;}
inline constexpr int rail_y(int p){return is_ratio(p)?108:p==PentUnit::P_ROOT||p==PentUnit::P_GAIN?58:
    p==PentUnit::P_DRIFT||p==PentUnit::P_DRIVE||p==PentUnit::P_DRIFT_DEPTH?197:148;}
// Compact ratio rails retain a whole-screen relative tuning span, not four
// octaves in 36px. Leaving a cell during an owned drag never edits its neighbour.
inline constexpr int gesture_span(int p){return is_ratio(p)?292:rail_right(p)-rail_left(p);}
inline int parameter_x(q15 v,int left,int right){return left+std::clamp(int(v),0,32767)*(right-left)/32767;}
enum class Menu:uint8_t {None,Ratio,Width};
enum class Op:int8_t {Preset,Mono,Stereo};
struct Row {char label[16]={};int8_t param=-1;Op op=Op::Preset;q15 value=0;bool enabled=true;};
struct Frame {
    bool valid=false,bypass=false,mute=false,busy=false,lens=false,playing=false;
    // fresh = new source counter edge; eligible = accepted audio lease (holds
    // between NDSP blocks, expires without progress). Draw remains frozen-only.
    bool eligible=false,fresh=false,overwritten=false,unsupported=false,width_disabled=true;
    uint8_t selected=PentUnit::P_BOND,voice=0,width=0,art=15,menu_focus=0,count=0;
    int8_t chain=-1,slot=-1,bank=-1;
    uint16_t linked=0,disabled=0;
    Menu menu=Menu::None;word_menu::Layout layout{};word_menu::Frame motion{};
    int16_t anchor_x=14,anchor_y=44;char anchor[16]={};
    PentUnit::Visual visual{};ParamInspector route{};
    pent_reactive::Frame response{}; // accepted source response + distinct owned-input feedback
    q15 edit[PentUnit::P_COUNT]={},live[PentUnit::P_COUNT]={};
    char values[PentUnit::P_COUNT][16]={},context[28]={},status[28]={},tuning[2][22]={};
    Row rows[10]{};
};
static_assert(sizeof(Frame)<=1024&&std::is_trivially_copyable<Frame>::value,"PENT <=1024 byte frozen LCD pair; no live pointers");
inline int menu_row(const Frame& f,int x,int y){
    const auto& l=f.layout;
    if(x<l.left||x>=l.right||y<l.y-4||y>=l.y-4+int(f.count)*l.step)return -1;
    return (y-l.y+4)/std::max(1,l.step);
}
const Frame& frozen();
// No Unit argument: public lower draw is frozen-only, including after detach.
bool draw_bottom();
} // namespace seance::platform::ui3ds::pent_ui
