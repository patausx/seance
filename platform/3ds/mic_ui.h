#pragma once
#include "mic_type_data.h"
#include "../../core/ui/draw.h"
#include <cstring>
namespace seance::platform::ui3ds::mic_ui {
struct Box {int x,y,w,h;bool has(int a,int b)const{return a>=x&&b>=y&&a<x+w&&b<y+h;}};
// REC return owns [174,238); keep bypass disjoint without moving its visible label.
constexpr Box bypass{238,0,32,28},gain_name{12,32,88,41},gain_value{104,32,204,41},gain_rail{12,74,296,39};
constexpr Box input_name{12,122,92,42},open{110,122,97,42},mute{213,122,95,42},cut_name{12,176,92,41};
constexpr Box cut(int i){return i==0?Box{108,176,62,41}:i==1?Box{176,176,62,41}:Box{244,176,64,41};}
inline void mask(int x,int baseline,int offset,int w,int h,int top,uint32_t colour){
    const int stride=(w+7)/8;
    for(int y=0;y<h;++y){int run=-1;
        for(int col=0;col<=w;++col){const bool on=col<w&&(mic_type::bitmap[offset+y*stride+col/8]&(0x80u>>(col&7)));
            if(on&&run<0)run=col;
            if(!on&&run>=0){ui::rect(x+run,baseline+top+y,col-run,1,colour);run=-1;}}}
}
inline void word(int x,int baseline,const char* text,uint32_t colour,int align=0){
    for(const auto&l:mic_type::labels)if(!std::strcmp(l.text,text)){
        mask(x-(align==1?l.width/2:align==2?l.width:0),baseline,l.offset,l.width,l.height,l.top,colour);return;}
    ui::text(x-(align==1?ui::text_w(text)/2:align==2?ui::text_w(text):0),baseline-8,text,colour);
}
inline const mic_type::Glyph* glyph(char c){for(const auto&g:mic_type::glyphs)if(g.character==c)return &g;return nullptr;}
inline int number(int right,int baseline,const char* value,uint32_t colour){
    int width=0;for(const char*c=value;*c;++c){const auto*g=glyph(*c);if(g)width+=g->advance;}
    int x=right-width;const int left=x;
    for(const char*c=value;*c;++c){const auto*g=glyph(*c);if(g){mask(x+g->left,baseline,g->offset,g->width,g->height,g->top,colour);x+=g->advance;}}
    return left;
}
} // namespace seance::platform::ui3ds::mic_ui
