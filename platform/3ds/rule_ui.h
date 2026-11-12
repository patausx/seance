#pragma once
#include "rule_visual.h"
#include "word_menu.h"
#include "rule_readout.h"
#include "../../core/engine/rule_unit.h"
#include <type_traits>
namespace seance::platform::ui3ds::rule_ui {
struct Box {int x,y,w,h;bool has(int xx,int yy)const{return xx>=x&&xx<x+w&&yy>=y&&yy<y+h;}};
inline constexpr Box source{76,4,174,24},setup{262,4,44,24},scale{14,222,68,18},back{258,222,48,18};
inline Box parameter(int p){return {(p&1)?174:14,100+(p/2)*24,132,24};}
inline Box table(int i){return {14+i*292/8,48,36,22};}
inline Box cell(int c){return {18+c*19,73,17,13};}
using rule_readout::scales;
inline constexpr const char* names[]={"rate","density","root","range","attack","release","mutate","spread","voices","level","rule","scale"};
inline int q8(q15 v,bool bi=false){return (std::clamp(int(v),bi?-32767:0,32767)+(bi?32767:0))*256/(bi?65534:32767);}
struct Frame {
    rule_visual::RuleFrame art;
    RuleUnit::Visual visual;
    q15 edit[12]={},live[12]={};
    char source_leaf[40]={},anchor[16]={};
    char rows[6][20]={};
    word_menu::Layout layout{};word_menu::Frame motion{};
    uint16_t disabled=0,linked=0;
    uint8_t menu=0,count=0,focus=0,root=60,phrase=1;
    int sample=-1,chain=-1,slot=-1,bank=-1;
    uint32_t generation=0,frames=0,beat=16000;
    bool valid=false,lens=false,bypass=false,mute=false,busy=false,loaded=false,overwritten=false,eligible=false;
};
static_assert(sizeof(Frame)<=1024,"RULE bounded pointer-free paired frame");
static_assert(std::is_trivially_copyable<Frame>::value,"RULE no engine/bank/string pointers");
const Frame& frozen();bool draw_bottom();
using rule_readout::value_text;
}
