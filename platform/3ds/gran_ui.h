#pragma once
// Approved wave-first geometry, half-open targets. No engine or UI global access.
#include "gran_type_data.h"
#include "../../core/ui/draw.h"
#include <cstring>
namespace seance::platform::ui3ds::gran_ui {
struct Box {int x,y,w,h; bool has(int a,int b)const{return a>=x&&b>=y&&a<x+w&&b<y+h;}};
// Header actions move with their labels; the module identity owns x14..54.
constexpr Box scan{122,0,52,24},fine{174,0,54,24},bypass{230,0,36,24};
constexpr Box pad{214,26,98,30},source{8,58,170,24},wave{8,84,304,74};
constexpr Box mode(int i){return {8+62*i,26,58,30};}
constexpr Box tab(int i){return {182+42*i,58,40,24};}
constexpr Box param(int i){return {8+(i%3)*104,161+(i/3)*34,96,33};}
constexpr Box setup(int i){return {8+(i%2)*156,161+(i/2)*34,148,33};}
inline void mask(int x,int baseline,int offset,int w,int h,int top,uint32_t colour){
    const int stride=(w+7)/8;
    for(int y=0;y<h;++y){int run=-1;
        for(int col=0;col<=w;++col){const bool on=col<w&&(gran_type::bitmap[offset+y*stride+col/8]&(0x80u>>(col&7)));
            if(on&&run<0)run=col;
            if(!on&&run>=0){ui::rect(x+run,baseline+top+y,col-run,1,colour);run=-1;}}
    }
}
inline void word(int x,int baseline,const char* text,uint32_t colour,bool center=false){
    for(const auto& l:gran_type::labels)if(!std::strcmp(l.text,text)){
        mask(x-(center?l.width/2:0),baseline,l.offset,l.width,l.height,l.top,colour);return;}
    ui::text(x-(center?ui::text_w(text)/2:0),baseline-8,text,colour);
}
inline const gran_type::Glyph* glyph(char c){for(const auto&g:gran_type::glyphs)if(g.character==c)return &g;return nullptr;}
inline void number(int x,int baseline,const char* value,uint32_t colour,uint32_t suffix_colour){
    int width=0;const char* end=value;bool numeric=false;
    for(;*end;++end){const auto*g=glyph(*end);if(!g)break;width+=g->advance;numeric|=*end>='0'&&*end<='9';}
    if(!numeric||width+8+ui::text_w(end)>150){ui::text(x,baseline-16,value,colour);return;}
    int first_left=glyph(value[0])->left;
    for(const char*at=value;at<end;++at){const auto*g=glyph(*at);mask(x+g->left-first_left,baseline,g->offset,g->width,g->height,g->top,colour);x+=g->advance;}
    if(*end)ui::text(x+8,baseline-13,end,suffix_colour);
}
} // namespace seance::platform::ui3ds::gran_ui
