#pragma once
#include "play_editor_layout.h"
#include "word_menu.h"
#include "sigil_surface.h"
#include "module_fader_painter.h"
#include "module_heading.h"
#include <cstring>
namespace seance::platform::ui3ds::play_word_layout {
namespace m = editor_style;
constexpr int kFooterY=m::kTextY;
using Box=play_editor_layout::Box;
constexpr Box cell(int col,int row){return {14+154*col,90+32*row,152+154*col,120+32*row};}
// Every list starts below its actual invoking text, including nested rows.
// Callers place six-row disclosures high; spacing may compact to16px, never
// move the list above its anchor. Only the shared sigil tree is drawn; no
// connector from the invoking text is added.
inline word_menu::Layout below(int ax,int ay,int count,int widest){
    const int y=ay+20;
    const int step=count<=1?28:std::min(28,(208-y)/(count-1));
    const int branch=std::clamp(ax+8,14,std::max(14,306-widest-14));
    return {branch,branch+14,y,std::max(16,step),branch,306};
}
// The same foreground command stream supplies native drawing and cache capture.
// Only PLAY's UI-thread capture installs a cache; draw never changes this pen.
inline sigil_surface::Painter pen{};
inline void rect(int x,int y,int w,int h,uint32_t c){if(pen.cache)pen.rect(x,y,w,h,c);else ui::rect(x,y,w,h,c);}
inline void line(int x,int y,int xx,int yy,uint32_t c){if(pen.cache)pen.line(x,y,xx,yy,c);else ui::line(x,y,xx,yy,c);}
inline void text(int x,int y,const char*s,uint32_t c){if(pen.cache)pen.text(x,y,s,c);else ui::text(x,y,s,c);}
inline void header(int x,int r,const char*s,uint32_t c){
    if(!pen.cache){m::header_text(x,r,s,c);return;}
    char t[64];const int n=std::max(0,std::min(63,(r-x)/6));
    if(ui::text_w(s)<=r-x)pen.text(x,m::kHeaderTextY,s,c);
    else if(n>=3){std::memcpy(t,s,size_t(n-3));std::memcpy(t+n-3,"...",4);pen.text(x,m::kHeaderTextY,t,c);}
}
inline void identity(){
    if(!pen.cache){module_heading::header(UnitKind::SamplePlayer,m::spot());return;}
    const auto*l=module_heading::label(UnitKind::SamplePlayer);if(!l)return;
    const int stride=(l->width+7)/8;const auto*b=module_heading_data::bitmap+l->offset;
    for(int y=0;y<l->height;++y)for(int x=0;x<l->width;++x)
        if(b[y*stride+x/8]&(0x80>>(x&7)))pen.rect(module_heading::kX+x,module_heading::kY+y,1,1,m::spot());
}
} // namespace seance::platform::ui3ds::play_word_layout
