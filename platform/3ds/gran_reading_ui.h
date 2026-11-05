#pragma once
// Approved setup-only reading disclosure; geometry/labels, no live engine access.
#include "gran_ui.h"
#include "word_menu.h"
#include "sigil_surface.h"
#include "module_fader_painter.h"
#include "module_heading.h"
#include "editor_style.h"
namespace seance::platform::ui3ds::gran_reading_ui {
namespace m = editor_style;
inline constexpr const char* labels[]={"pitch sign","forward","reverse","mixed"};
inline constexpr word_menu::Layout layout{214,230,64,24,214,306};
inline int row(int x,int y){
    if(x<214||x>=312)return -1;
    for(int i=0;i<4;++i)if(y>=layout.y+i*layout.step-8&&y<layout.y+i*layout.step+16)return i;
    return -1;
}
// The native/cache closed page uses this same mask, not a linker interception.
inline void header(const sigil_surface::Painter& p,uint32_t color){
    if(!p.cache){module_heading::header(UnitKind::Grain,color);return;}
    const auto* l=module_heading::label(UnitKind::Grain);
    const auto* bits=module_heading_data::bitmap+l->offset;const int stride=(l->width+7)/8;
    for(int y=0;y<l->height;){
        const auto* row=bits+y*stride;int end=y+1;
        while(end<l->height&&!std::memcmp(row,bits+end*stride,stride))++end;
        int run=-1;for(int x=0;x<=l->width;++x){
            const bool on=x<l->width&&(row[x/8]&(128u>>(x%8)));
            if(on&&run<0)run=x;
            if(!on&&run>=0){p.rect(module_heading::kX+run,module_heading::kY+y,x-run,end-y,color);run=-1;}
        }y=end;
    }
}
// Native and cached chrome keep the shared module metadata/footer anchors.
inline void header_text(const sigil_surface::Painter& p,int x,const char* text,uint32_t color){
    p.text(x,m::kHeaderTextY,text,color);
}
inline void footer(const sigil_surface::Painter& p,int width,const char* left,const char* right){
    p.text(m::kLeft,m::kTextY,left,m::ghost());
    p.right(m::right_edge(width),m::kTextY,right,m::ghost());
}
inline void word(const sigil_surface::Painter& p,int x,int baseline,const char* text,uint32_t color,bool center=false){
    if(!p.cache){gran_ui::word(x,baseline,text,color,center);return;}
    for(const auto& l:gran_type::labels)if(!std::strcmp(l.text,text)){
        const int first=x-(center?l.width/2:0),stride=(l.width+7)/8;
        for(int y=0;y<l.height;++y){int run=-1;for(int col=0;col<=l.width;++col){
            const bool on=col<l.width&&(gran_type::bitmap[l.offset+y*stride+col/8]&(128u>>(col%8)));
            if(on&&run<0)run=col;
            if(!on&&run>=0){p.rect(first+run,baseline+l.top+y,col-run,1,color);run=-1;}
        }}return;
    }
    p.text(x-(center?ui::text_w(text)/2:0),baseline-8,text,color);
}
}
