#pragma once
#include "rot_visual.h"
#include "word_menu.h"
#include "rot_readout.h"
#include "../../core/engine/rot_unit.h"
namespace seance::platform::ui3ds::rot_ui {
struct Box {int x,y,w,h;bool has(int xx,int yy)const{return xx>=x&&xx<x+w&&yy>=y&&yy<y+h;}};
inline constexpr Box source{76,4,174,24},setup{262,4,44,24},back{258,222,48,18};
inline constexpr Box modes[]={{14,34,38,24},{146,34,38,24},{270,34,36,24}};
inline Box parameter(int p){return {(p&1)?174:14,100+(p/2)*24,132,24};}
inline constexpr const char* names[]={"pitch","level","rot","spread","smear","holes","crackle","wow","bits","heal","start","length"};
inline int q8(q15 v,bool bi=false){return (std::clamp(int(v),bi?-32767:0,32767)+(bi?32767:0))*256/(bi?65534:32767);}
struct Frame {
    rot_visual::RotFrame art;
    RotUnit::Visual visual;
    q15 edit[12]={},live[12]={};
    word_menu::Layout layout{};word_menu::Frame motion{};
    char rows[6][20]={},anchor[16]={},source_leaf[40]={};
    uint16_t disabled=0,linked=0,passes=14;
    uint32_t seed=11,generation=0,frames=0;
    int sample=-1,chain=-1,slot=-1,bank=-1,edit_start=0,edit_end=0;
    uint8_t menu=0,count=0,focus=0,config_mode=0;
    bool valid=false,lens=false,bypass=false,mute=false,busy=false,loaded=false,overwritten=false,eligible=false,pending=false;
};
static_assert(sizeof(Frame)<=1024,"ROT bounded pointer-free paired frame");
static_assert(std::is_trivially_copyable<Frame>::value,"ROT no engine/bank/string pointers");
const Frame& frozen();const rot_visual::Artwork& artwork();bool draw_bottom();
using rot_readout::value_text;
}
