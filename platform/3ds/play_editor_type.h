#pragma once
#include "play_editor_type_data.h"
#include "../../core/ui/draw.h"
#include <cstring>
namespace seance::platform::ui3ds::play_editor_type {
inline void mask(int x,int baseline,unsigned offset,int w,int h,int top,uint32_t color){
    const int stride=(w+7)/8;
    for(int y=0;y<h;++y){int run=-1;for(int col=0;col<=w;++col){
        const bool on=col<w&&(bitmap[offset+y*stride+col/8]&(0x80u>>(col&7)));
        if(on&&run<0)run=col;
        if(!on&&run>=0){ui::rect(x+run,baseline+top+y,col-run,1,color);run=-1;}
    }}
}
inline const Glyph* find(char c,int size){for(const auto&g:glyphs)if(g.character==c&&g.size==size)return &g;return nullptr;}
inline int value_width(const char* text,int size){int x=0,end=0;for(const char*p=text;*p;++p){const auto*g=find(*p,size);if(!g)return ui::text_w(text);end=x+g->left+g->width;x+=g->advance;}return end;}
inline int value(int x,int baseline,const char*text,int size,uint32_t color){
    for(const char*p=text;*p;++p)if(!find(*p,size)){ui::text(x,baseline-8,text,color);return ui::text_w(text);}
    for(const char*p=text;*p;++p){const auto*g=find(*p,size);mask(x+g->left,baseline,g->offset,g->width,g->height,g->top,color);x+=g->advance;}return value_width(text,size);
}
inline void word(int x,int baseline,const char*text,int size,uint32_t color,bool centered=false){
    for(const auto&l:labels)if(l.size==size&&!std::strcmp(text,l.text)){mask(centered?x-l.width/2:x,baseline,l.offset,l.width,l.height,l.top,color);return;}
    ui::text(centered?x-ui::text_w(text)/2:x,baseline-8,text,color);
}
} // namespace seance::platform::ui3ds::play_editor_type
