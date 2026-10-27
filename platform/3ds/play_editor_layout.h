#pragma once
// Private PLAY editor geometry. Half-open rectangles, native 320x240 pixels.
namespace seance::platform::ui3ds::play_editor_layout {
struct Box { int x0,y0,x1,y1; constexpr bool contains(int x,int y)const{return x>=x0&&x<x1&&y>=y0&&y<y1;} };
inline constexpr Box source{10,4,225,28},primary{10,30,110,83},secondary{116,30,225,83};
inline constexpr Box wave{14,120,222,174},config{236,208,318,238};
constexpr Box mode(int i){return {236,36+i*32,318,64+i*32};}
constexpr Box tool(int i){return {i==0?10:i==1?82:156,85,i==0?80:i==1?154:225,109};}
constexpr Box tab(int i){return {10+i*72,40,78+i*72,64};}
constexpr Box row(int i){return {10,69+i*24,225,93+i*24};}
constexpr Box context(int i){return {i==0?10:i==1?82:156,190,i==0?80:i==1?154:225,214};}
constexpr Box footer(int i){return {i==0?10:i==1?82:156,216,i==0?80:i==1?154:225,240};}
inline constexpr Box foot{10,216,225,240},context_left{10,190,112,214},context_right{116,190,225,214};
} // namespace seance::platform::ui3ds::play_editor_layout
