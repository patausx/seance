#pragma once
#include "word_menu.h"
#include "sigil_surface.h"
#include "module_fader_painter.h"
#include "step_focus_ink.h"
#include "module_frame_layout.h"
#include <cstdio>
#include "../../core/engine/musical_scale.h"

namespace seance::platform::ui3ds::sequencer_scale {
constexpr int kX=122,kY=199;
// Read-only frozen motion for native integration probes; no controller access.
word_menu::Frame motion(int group);
inline void label(char* out,size_t size,MusicalScale s) {
    if(!musical_scale::active(s))std::snprintf(out,size,"chrom");
    else std::snprintf(out,size,"%s %s",musical_scale::tonic_name(s.tonic),musical_scale::short_name(s.mode));
}
constexpr word_menu::Layout tonic_layout{22,36,34,14,22,108};
constexpr word_menu::Layout scale_layout{142,156,34,16,142,306};
// One production command stream. The capture pass only changes its destination;
// native drawing still delegates to the original primitives/focus/lancet.
namespace drawing {
namespace pixel=seance::ui::pixel;
inline sigil_surface::Painter pen{};
inline int text_w(const char* s,int scale=1){return seance::ui::text_w(s,scale);}
inline void rect(float x,float y,float w,float h,uint32_t c){
    if(pen.cache){const auto b=seance::ui::pixel::bounds(x,y,w,h);pen.rect(b.x,b.y,b.w,b.h,c);}
    else seance::ui::rect(x,y,w,h,c);
}
inline void text(int x,int y,const char* s,uint32_t c,int scale=1){
    if(!pen.cache){seance::ui::text(x,y,s,c,scale);return;}
    if(scale==1){pen.text(x,y,s,c);return;}
    for(int cx=x;*s;++s,cx+=6*scale)for(int row=0;row<8;++row)for(int col=0;col<6;++col)
        if(trackr::ui::glyph_row(*s,row)&(1u<<(5-col)))pen.rect(cx+col*scale,y+row*scale,scale,scale,c);
}
inline void line(int x,int y,int xx,int yy,uint32_t c,int thickness=1){
    if(!pen.cache){seance::ui::line(x,y,xx,yy,c,thickness);return;}
    seance::ui::pixel::Box b{};
    if(seance::ui::pixel::axis_line(x,y,xx,yy,thickness,b))pen.rect(b.x,b.y,b.w,b.h,c);
    else pen.line(x,y,xx,yy,c);
}
inline void frame(float x,float y,float w,float h,uint32_t c){
    if(!pen.cache){seance::ui::frame(x,y,w,h,c);return;}
    seance::ui::pixel::frame(seance::ui::pixel::bounds(x,y,w,h),[&](seance::ui::pixel::Box b){pen.rect(b.x,b.y,b.w,b.h,c);});
}
inline void circle(float x,float y,float radius,uint32_t c,bool filled=true){
    if(!pen.cache){seance::ui::circle(x,y,radius,c,filled);return;}
    const int r=int(std::max(1.f,radius)+.5f),cx=int(x+.5f),cy=int(y+.5f);
    for(int dy=-r;dy<=r;++dy){int dx=0;while((dx+1)*(dx+1)+dy*dy<=r*r)++dx;
        if(filled){if(dx)pen.rect(cx-dx,cy+dy,dx*2+1,1,c);}
        else {pen.rect(cx-dx,cy+dy,1,1,c);if(dx)pen.rect(cx+dx,cy+dy,1,1,c);}}
}
inline void paper(int width){
    if(!pen.cache){menu_style::paper(width);return;}
    for(int y=0;y<sigil_surface::H;++y)for(int x=0;x<sigil_surface::W;++x){
        uint32_t c=0xff000000u;
        for(unsigned shift:{0u,8u,16u}){unsigned sum=0;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                sum+=(menu_style::patch_sample(false,float(x*2+dx),float(y*2+dy))>>shift)&255;
            c|=((sum+2)/4)<<shift;
        }
        pen.cache->pixels[y*sigil_surface::W+x]=c;
    }
}
inline void paper_rect(int x,int y,int w,int h){
    if(!pen.cache){menu_style::paper_rect(x,y,w,h);return;}
    for(int row=y;row<y+h;++row)pen.rect(x,row,w,1,menu_style::paper_color(row));
}
inline void footer(int width,const char* left,const char* right){
    if(!pen.cache){menu_style::footer(width,left,right);return;}
    if(left)pen.text(20,module_frame_layout::kFooterTextY,left,menu_style::ghost());
    if(right)pen.right(width-20,module_frame_layout::kFooterTextY,right,menu_style::ghost());
}
inline void fill(step_focus::Box b,uint32_t c){
    if(!pen.cache){step_focus::fill(b,c);return;}
    for(int i=0;i<5;++i){const auto s=step_focus::span(b,i);pen.rect(s.x,s.y,s.w,s.h,c);}
}
inline void clipped_rect(step_focus::Box mask,step_focus::Box r,uint32_t c){
    if(!pen.cache){step_focus::clipped_rect(mask,r,c);return;}
    for(int i=0;i<5;++i){const auto b=step_focus::intersect(step_focus::span(mask,i),r);pen.rect(b.x,b.y,b.w,b.h,c);}
}
inline void clipped_text(step_focus::Box mask,int x,int y,const char* s,uint32_t c){
    if(!pen.cache){step_focus::clipped_text(mask,x,y,s,c);return;}
    for(;*s;++s,x+=6)for(int row=0;row<8;++row)for(int col=0;col<6;++col)
        if(trackr::ui::glyph_row(*s,row)&(1u<<(5-col)))drawing::clipped_rect(mask,{x+col,y+row,1,1},c);
}
inline void clipped_line(step_focus::Box mask,int x,int y,int xx,int yy,uint32_t c,int thickness){
    if(!pen.cache){step_focus::clipped_line(mask,x,y,xx,yy,c,thickness);return;}
    for(int i=0;i<5;++i){const auto s=step_focus::span(mask,i);auto p=pen;
        p.clip_left=s.x;p.clip_top=s.y;p.clip_right=s.x+s.w;p.clip_bottom=s.y+s.h;p.line(x,y,xx,yy,c);}
}
inline void horizontal(int x,int xx,int y,int edit,int live,module_fader::State state={},int neutral=-1){
    if(pen.cache)module_fader::horizontal(pen,x,xx,y,edit,live,state,neutral);
    else module_fader::horizontal(x,xx,y,edit,live,state,neutral);
}
} // namespace drawing
} // namespace seance::platform::ui3ds::sequencer_scale
