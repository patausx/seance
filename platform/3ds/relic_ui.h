#pragma once
#include "../../core/engine/relic_unit.h"
#include <type_traits>
#include <algorithm>
namespace seance::platform::ui3ds::relic_ui {
struct Box {int x,y,w,h; bool has(int px,int py)const{return px>=x&&px<x+w&&py>=y&&py<y+h;}};
constexpr Box source{76,6,152,26},audio{232,6,74,26},field{25,39,267,155};
constexpr Box hold{108,220,102,20},mod{14,220,72,20},back{236,220,70,20};
constexpr Box parameter(int p){return {14+(p%2)*164,196+(p/2)*14,128,14};}
constexpr const char* names[]={"gain","depth","start","length"};
constexpr const char* ports[]={"pull","compress","bend","wave"};
// Native projector, packed once AFTER guard. 9-bit x / 8-bit y / 7-bit tone.
struct Point {uint8_t bytes[3]={}; void set(int x,int y,int tone){uint32_t v=uint32_t(x)|(uint32_t(y)<<9)|(uint32_t(tone)<<17);for(int i=0;i<3;++i)bytes[i]=uint8_t(v>>(i*8));}
    uint32_t bits()const{return uint32_t(bytes[0])|(uint32_t(bytes[1])<<8)|(uint32_t(bytes[2])<<16);}
    int x()const{return bits()&511;} int y()const{return (bits()>>9)&255;} int tone()const{return bits()>>17;}};
struct Parameter {q15 edit=0,live=0,target=0,depth=0; uint8_t source=0; bool linked=false;};
struct Head {uint16_t position=0;uint8_t phase=0,energy=0,layer=0;};
struct Frame {
    Point vertices[relic::SURFACE_VERTICES];
    Parameter params[4];Head heads[8];q15 ports[4]={};
    uint64_t epoch=0;uint32_t instance=0,revision=0,bank_revision=0,generation=0,frames=0;
    char source_name[25]={};
    uint16_t energy=0,mid=0,side=0,phase=0;uint8_t readers[3]={},head_count=0,selected=0,chain=0,slot=0;
    bool valid=false,hold=false,held=false,lock=false,lens=false,bypass=false,mute=false,busy=false;
    bool own_audio=false,contributing=false,pipe=false,requested=false,running=false,loaded=false;
};
static_assert(sizeof(Frame)<=1024,"Relic dual draw DTO is bounded; no Visual in hot mailbox");
static_assert(std::is_trivially_copyable<Frame>::value,"pointer-free relic draw");
// Inverse of the material projector y=122+.94*y-.36*z. XY dragging
// preserves local Z; pad depth is a separate axis, not a screen-Y shortcut.
inline int material_dy(int screen_delta){const int n=screen_delta*12800;return n>=0?(n+47)/94:-((-n+47)/94);}
// Circle pad DOWN is negative dy. No time integration or synthetic pressure.
inline q15 pad_depth(int dy){const int n=std::clamp(-dy-18,0,138);return q15(int64_t(32767)*n*n*(414-2*n)/(int64_t(138)*138*138));}
const Frame& frozen();
} // namespace seance::platform::ui3ds::relic_ui
