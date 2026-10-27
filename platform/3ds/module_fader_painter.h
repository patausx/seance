#pragma once
// Submit the shared lancet commands through a page's existing clipped/cache pen.
// Geometry, palette and hollow/live semantics remain in module_fader.h.
#include "module_fader.h"

namespace seance::platform::ui3ds::module_fader {
namespace detail {
template<class Painter> struct PainterPen {
    const Painter& painter;
    int x,y;
    bool vertical;
    void block(int u,int v,int w,int h,uint32_t color) const {
        if(vertical)painter.rect(x+v,y-u-w+1,h,w,color);
        else painter.rect(x+u,y+v,w,h,color);
    }
};
}
template<class Painter>
inline void horizontal(const Painter& painter,int x0,int x1,int y,int edit_x,int live_x,
                       State state={},int neutral_x=-1,bool compact=false) {
    detail::draw(detail::PainterPen<Painter>{painter,x0,y,false},x1-x0,edit_x-x0,live_x-x0,state,
                 neutral_x<0?-1:neutral_x-x0,compact);
}
template<class Painter>
inline void vertical(const Painter& painter,int x,int top,int bottom,int edit_y,int live_y,
                     State state={},int neutral_y=-1,bool compact=false) {
    detail::draw(detail::PainterPen<Painter>{painter,x,bottom,true},bottom-top,bottom-edit_y,bottom-live_y,state,
                 neutral_y<0?-1:bottom-neutral_y,compact);
}
template<class Painter>
inline void horizontal_track(const Painter& painter,int x0,int x1,int y) {
    detail::track(detail::PainterPen<Painter>{painter,x0,y,false},x1-x0);
}
}
