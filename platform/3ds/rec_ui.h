#pragma once
#include "rec_type_data.h"
#include "../../core/ui/draw.h"
#include <cstring>
namespace seance::platform::ui3ds::rec_ui {
struct Box {int x,y,w,h;bool has(int a,int b)const{return a>=x&&b>=y&&a<x+w&&b<y+h;}};
constexpr Box stop{12,46,142,104},dot{166,46,142,104},setup{226,0,86,28};
constexpr Box gran{12,181,142,41},play{166,181,142,41},back{8,216,304,24};
constexpr Box row(int i){return {8,48+i*25,304,24};}
inline void mask(int x,int baseline,int offset,int w,int h,int top,uint32_t colour){
    const int stride=(w+7)/8;
    for(int y=0;y<h;++y){int run=-1;
        for(int col=0;col<=w;++col){const bool on=col<w&&(rec_type::bitmap[offset+y*stride+col/8]&(0x80u>>(col&7)));
            if(on&&run<0)run=col;
            if(!on&&run>=0){ui::rect(x+run,baseline+top+y,col-run,1,colour);run=-1;}}}
}
inline void word(int x,int baseline,const char* text,uint32_t colour,bool center=false){
    for(const auto&l:rec_type::labels)if(!std::strcmp(l.text,text)){
        mask(x-(center?l.width/2:0),baseline,l.offset,l.width,l.height,l.top,colour);return;}
    ui::text(x-(center?ui::text_w(text)/2:0),baseline-8,text,colour);
}
inline const rec_type::Glyph* glyph(char c){for(const auto&g:rec_type::glyphs)if(g.character==c)return &g;return nullptr;}
inline void timer(int center,int baseline,const char* value,uint32_t colour){
    int width=0;for(const char*c=value;*c;++c){const auto*g=glyph(*c);if(g)width+=g->advance;}
    int x=center-width/2;for(const char*c=value;*c;++c){const auto*g=glyph(*c);if(g){mask(x+g->left,baseline,g->offset,g->width,g->height,g->top,colour);x+=g->advance;}}
}
} // namespace seance::platform::ui3ds::rec_ui
